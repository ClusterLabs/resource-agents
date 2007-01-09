/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <libcman.h>
#include "dlm_daemon.h"

static cman_handle_t	ch;
static cman_node_t      old_nodes[MAX_NODES];
static int              old_node_count;
static cman_node_t      cman_nodes[MAX_NODES];
static int              cman_node_count;
static int		local_nodeid;
extern struct list_head lockspaces;


static int is_member(cman_node_t *node_list, int count, int nodeid)
{
	int i;

	for (i = 0; i < count; i++) {
		if (node_list[i].cn_nodeid == nodeid)
			return node_list[i].cn_member;
	}
	return 0;
}

static int is_old_member(int nodeid)
{
	return is_member(old_nodes, old_node_count, nodeid);
}

int is_cman_member(int nodeid)
{
	return is_member(cman_nodes, cman_node_count, nodeid);
}

static cman_node_t *find_cman_node(int nodeid)
{
	int i;

	for (i = 0; i < cman_node_count; i++) {
		if (cman_nodes[i].cn_nodeid == nodeid)
			return &cman_nodes[i];
	}
	return NULL;
}

char *nodeid2name(int nodeid)
{
	cman_node_t *cn;

	cn = find_cman_node(nodeid);
	if (!cn)
		return NULL;
	return cn->cn_name;
}

/* add a configfs dir for cluster members that don't have one,
   del the configfs dir for cluster members that are now gone */

static void statechange(void)
{
	int i, rv;

	old_node_count = cman_node_count;
	memcpy(&old_nodes, &cman_nodes, sizeof(old_nodes));

	cman_node_count = 0;
	memset(&cman_nodes, 0, sizeof(cman_nodes));
	rv = cman_get_nodes(ch, MAX_NODES, &cman_node_count, cman_nodes);
	if (rv < 0) {
		log_debug("cman_get_nodes error %d %d", rv, errno);
		return;
	}

	for (i = 0; i < old_node_count; i++) {
		if (old_nodes[i].cn_member &&
		    !is_cman_member(old_nodes[i].cn_nodeid)) {

			log_debug("cman: node %d removed",
				   old_nodes[i].cn_nodeid);

			del_configfs_node(old_nodes[i].cn_nodeid);
		}
	}

	for (i = 0; i < cman_node_count; i++) {
		if (cman_nodes[i].cn_member &&
		    !is_old_member(cman_nodes[i].cn_nodeid)) {

			log_debug("cman: node %d added",
				  cman_nodes[i].cn_nodeid);

			add_configfs_node(cman_nodes[i].cn_nodeid,
					  cman_nodes[i].cn_address.cna_address,
					  cman_nodes[i].cn_address.cna_addrlen,
					  (cman_nodes[i].cn_nodeid ==
					   local_nodeid));
		}
	}
}

static void member_callback(cman_handle_t h, void *private, int reason, int arg)
{
	switch (reason) {
	case CMAN_REASON_TRY_SHUTDOWN:
		if (list_empty(&lockspaces))
			cman_replyto_shutdown(ch, 1);
		else {
			log_debug("no to cman shutdown");
			cman_replyto_shutdown(ch, 0);
		}
		break;
	case CMAN_REASON_STATECHANGE:
		statechange();
		break;
	}
}

int process_member(void)
{
	int rv;

	rv = cman_dispatch(ch, CMAN_DISPATCH_ALL);
	if (rv == -1 && errno == EHOSTDOWN) {
		/* do we want to try to forcibly clean some stuff up
		   in the kernel here? */
		log_error("cluster is down, exiting");
		clear_configfs();
		exit(1);
	}
	return 0;
}

int setup_member(void)
{
	cman_node_t node;
	int rv, fd;

	ch = cman_init(NULL);
	if (!ch) {
		log_error("cman_init error %d %d", (int) ch, errno);
		return -ENOTCONN;
	}

	rv = cman_start_notification(ch, member_callback);
	if (rv < 0) {
		log_error("cman_start_notification error %d %d", rv, errno);
		cman_finish(ch);
		return rv;
	}

	fd = cman_get_fd(ch);

	/* FIXME: wait here for us to be a member of the cluster */
	memset(&node, 0, sizeof(node));
	rv = cman_get_node(ch, CMAN_NODEID_US, &node);
	if (rv < 0) {
		log_error("cman_get_node us error %d %d", rv, errno);
		cman_stop_notification(ch);
		cman_finish(ch);
		fd = rv;
		goto out;
	}
	local_nodeid = node.cn_nodeid;

	/* if this daemon was killed and the cluster shut down, and
	   then the cluster brought back up and this daemon restarted,
	   there will be old configfs entries we need to clear out */

	clear_configfs();

	set_configfs_debug(kernel_debug_opt);

	old_node_count = 0;
	memset(&old_nodes, 0, sizeof(old_nodes));
	cman_node_count = 0;
	memset(&cman_nodes, 0, sizeof(cman_nodes));

	/* add configfs entries for existing nodes */
	statechange();
 out:
	return fd;
}

/* Force re-read of cman nodes */
void cman_statechange()
{
	statechange();
}
