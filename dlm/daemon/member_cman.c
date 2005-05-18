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

#include "dlm_daemon.h"
#include "libcman.h"

static cman_handle_t	ch;
static cman_node_t	cluster_nodes[MAX_NODES];
static cman_node_t	new_nodes[MAX_NODES];
static int		cluster_count;
static char		id[256];
static char		ip[256];
static int		member_cb;
static int		member_reason;


static void set_idip(int nodeid, char *addr)
{
	struct sockaddr_in *sin = (struct sockaddr_in *) addr;

	memset(id, 0, 256);
	memset(ip, 0, 256);

	snprintf(id, 256, "%d", nodeid);
	inet_ntop(AF_INET, &sin->sin_addr, ip, 256);
}

static int do_set_local(int nodeid, char *addr)
{
	char *argv[] = { id, ip };

	set_idip(nodeid, addr);
	log_debug("set_local %s %s", argv[0], argv[1]);
	return set_local(2, argv);
}

static int do_set_node(int nodeid, char *addr)
{
	char *argv[] = { id, ip };

	set_idip(nodeid, addr);
	log_debug("set_node %s %s", argv[0], argv[1]);
	return set_node(2, argv);
}

static cman_node_t *find_cluster_node(int nodeid)
{
	int i;

	for (i = 0; i < cluster_count; i++) {
		if (cluster_nodes[i].cn_nodeid == nodeid)
			return &cluster_nodes[i];
	}
	return NULL;
}

static void process_member_cb(void)
{
	int i, rv, count;
	cman_node_t *cn;

	count = 0;
	memset(&new_nodes, 0, sizeof(new_nodes));

	rv = cman_get_nodes(ch, MAX_NODES, &count, new_nodes);
	if (rv < 0) {
		log_error("cman_get_nodes error %d %d", rv, errno);
		return;
	}

	if (count < cluster_count) {
		log_error("decrease in cluster nodes %d %d",
			  count, cluster_count);
		return;
	}

	for (i = 0; i < count; i++) {
		cn = find_cluster_node(new_nodes[i].cn_nodeid);
		if (cn)
			continue;
		do_set_node(new_nodes[i].cn_nodeid,
			    new_nodes[i].cn_address.cna_address);
	}

	cluster_count = count;
	memcpy(cluster_nodes, new_nodes, MAX_NODES);
}

static void member_callback(cman_handle_t h, void *private, int reason, int arg)
{
	member_cb = 1;
	member_reason = reason;
}

int process_member(void)
{
	while (1) {
		cman_dispatch(ch, CMAN_DISPATCH_ONE);

		if (member_cb) {
			member_cb = 0;
			process_member_cb();
		} else
			break;
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

	rv = cman_get_node(ch, CMAN_NODEID_US, &node);
	if (rv < 0) {
		log_error("cman_get_node us error %d %d", rv, errno);
		cman_stop_notification(ch);
		cman_finish(ch);
		fd = rv;
		goto out;
	}

	do_set_local(node.cn_nodeid, node.cn_address.cna_address);

	/* this will just initialize gd_nodes, etc */
	member_reason = CMAN_REASON_STATECHANGE;
	process_member_cb();

 out:
	return fd;
}

