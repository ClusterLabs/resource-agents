/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005-2008 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "fd.h"
#include <libcman.h>

static cman_handle_t	ch;
static cman_node_t	cman_nodes[MAX_NODES];
static int		cman_node_count;

static cman_node_t *find_cman_node(int nodeid)
{
	int i;

	for (i = 0; i < cman_node_count; i++) {
		if (cman_nodes[i].cn_nodeid == nodeid)
			return &cman_nodes[i];
	}
	return NULL;
}

static void statechange(void)
{
	int rv;

	cman_quorate = cman_is_quorate(ch);
	cman_node_count = 0;
	memset(&cman_nodes, 0, sizeof(cman_nodes));

	rv = cman_get_nodes(ch, MAX_NODES, &cman_node_count, cman_nodes);
	if (rv < 0)
		log_error("cman_get_nodes error %d %d", rv, errno);
}

static void cman_callback(cman_handle_t h, void *private, int reason, int arg)
{
	int quorate = cman_quorate;

	switch (reason) {
	case CMAN_REASON_TRY_SHUTDOWN:
		if (list_empty(&domains))
			cman_replyto_shutdown(ch, 1);
		else {
			log_debug("no to cman shutdown");
			cman_replyto_shutdown(ch, 0);
		}
		break;
	case CMAN_REASON_STATECHANGE:
		statechange();

		/* domain may have been waiting for quorum */
		if (!quorate && cman_quorate && (group_mode == GROUP_LIBCPG))
			process_fd_changes();
		break;
	}
}

void process_cman(int ci)
{
	int rv;

	rv = cman_dispatch(ch, CMAN_DISPATCH_ALL);
	if (rv == -1 && errno == EHOSTDOWN) {
		log_error("cluster is down, exiting");
		exit(1);
	}
}

int setup_cman(void)
{
	cman_node_t node;
	int rv, fd;

	ch = cman_init(NULL);
	if (!ch) {
		log_error("cman_init error %p %d", ch, errno);
		return -ENOTCONN;
	}

	rv = cman_start_notification(ch, cman_callback);
	if (rv < 0) {
		log_error("cman_start_notification error %d %d", rv, errno);
		cman_finish(ch);
		return rv;
	}

	statechange();

	fd = cman_get_fd(ch);

	/* FIXME: wait here for us to be a member of the cluster */
	memset(&node, 0, sizeof(node));
	rv = cman_get_node(ch, CMAN_NODEID_US, &node);
	if (rv < 0) {
		log_error("cman_get_node us error %d %d", rv, errno);
		cman_finish(ch);
		fd = rv;
		goto out;
	}

	memset(our_name, 0, sizeof(our_name));
	strncpy(our_name, node.cn_name, CMAN_MAX_NODENAME_LEN);
	our_nodeid = node.cn_nodeid;

	log_debug("our_nodeid %d our_name %s", our_nodeid, our_name);
 out:
	return fd;
}

/*
void exit_member(void)
{
	cman_finish(ch);
}
*/

int is_cman_member(int nodeid)
{
	cman_node_t *cn;

	/* Note: in fence delay loop we aren't processing callbacks so won't
	   have done a statechange() in response to a cman callback */
	statechange();

	cn = find_cman_node(nodeid);
	if (cn && cn->cn_member)
		return 1;

	log_debug("node %d not a cman member, cn %d", nodeid, cn ? 1 : 0);
	return 0;
}

char *nodeid_to_name(int nodeid)
{
	cman_node_t *cn;

	cn = find_cman_node(nodeid);
	if (cn)
		return cn->cn_name;

	return "unknown";
}

struct node *get_new_node(struct fd *fd, int nodeid)
{
	cman_node_t cn;
	struct node *node;
	int rv;

	node = malloc(sizeof(*node));
	if (!node)
		return NULL;
	memset(node, 0, sizeof(*node));

	node->nodeid = nodeid;

	memset(&cn, 0, sizeof(cn));
	rv = cman_get_node(ch, nodeid, &cn);
	if (rv < 0)
		log_debug("get_new_node %d no cman node %d", nodeid, rv);
	else
		strncpy(node->name, cn.cn_name, MAX_NODENAME_LEN);

	return node;
}

