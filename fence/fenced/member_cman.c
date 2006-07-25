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
#include "fd.h"

#define BUFLEN		128

static cman_handle_t	ch;
static int		cman_cb;
static int		cman_reason;
static int		cman_quorate;
static cman_node_t	cman_nodes[MAX_NODES];
static int		cman_node_count;
static char		name_buf[CMAN_MAX_NODENAME_LEN+1];

extern struct list_head domains;

char			*our_name;
int			our_nodeid;


static int name_equal(char *name1, char *name2)
{
	char name3[BUFLEN], name4[BUFLEN];
	int i, len1, len2;

	len1 = strlen(name1);
	len2 = strlen(name2);

	if (len1 == len2 && !strncmp(name1, name2, len1))
		return TRUE;

	memset(name3, 0, BUFLEN);
	memset(name4, 0, BUFLEN);

	for (i = 0; i < BUFLEN && i < len1; i++) {
		if (name1[i] != '.')
			name3[i] = name1[i];
		else
			break;
	}

	for (i = 0; i < BUFLEN && i < len2; i++) {
		if (name2[i] != '.')
			name4[i] = name2[i];
		else
			break;
	}

	len1 = strlen(name3);
	len2 = strlen(name4);

	if (len1 == len2 && !strncmp(name3, name4, len1))
		return TRUE;

	return FALSE;
}

static cman_node_t *find_cluster_node_name(char *name)
{
	int i;

	for (i = 0; i < cman_node_count; i++) {
		if (name_equal(cman_nodes[i].cn_name, name))
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

static void process_cman_callback(void)
{
	switch (cman_reason) {
	case CMAN_REASON_STATECHANGE:
		statechange();
		break;
	default:
		break;
	}
}

static void cman_callback(cman_handle_t h, void *private, int reason, int arg)
{
	cman_cb = 1;
	cman_reason = reason;

	if (reason == CMAN_REASON_TRY_SHUTDOWN) {
		if (list_empty(&domains))
			cman_replyto_shutdown(ch, 1);
		else {
			log_debug("no to cman shutdown");
			cman_replyto_shutdown(ch, 0);
		}
	}
}

int process_member(void)
{
	int rv;

	while (1) {
		rv = cman_dispatch(ch, CMAN_DISPATCH_ONE);
		if (rv < 0)
			break;

		if (cman_cb) {
			cman_cb = 0;
			process_cman_callback();
		} else
			break;
	}

	if (rv == -1 && errno == EHOSTDOWN) {
		log_error("cluster is down, exiting");
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

	memset(name_buf, 0, sizeof(name_buf));
	strncpy(name_buf, node.cn_name, CMAN_MAX_NODENAME_LEN);
	our_name = name_buf;
	our_nodeid = node.cn_nodeid;

	log_debug("our_nodeid %d our_name %s", our_nodeid, our_name);
	rv = 0;

 out:
	return fd;
}

void exit_member(void)
{
	cman_finish(ch);
}

int is_member(char *name)
{
	cman_node_t *cn;

	cn = find_cluster_node_name(name);
	if (cn && cn->cn_member) {
		if (in_groupd_cpg(cn->cn_nodeid))
			return 1;
		log_debug("node \"%s\" not in groupd cpg", name);
		return 0;
	}

	log_debug("node \"%s\" not a cman member, cn %d", name, cn ? 1 : 0);
	return 0;
}

fd_node_t *get_new_node(fd_t *fd, int nodeid, char *in_name)
{
	cman_node_t cn;
	fd_node_t *node = NULL;
	char *name = in_name;
	int rv;

	if (!name) {
		memset(&cn, 0, sizeof(cn));
		rv = cman_get_node(ch, nodeid, &cn);
		name = cn.cn_name;
	}

	node = malloc(sizeof(*node));
	memset(node, 0, sizeof(*node));

	node->nodeid = nodeid;
	strcpy(node->name, name);

	return node;
}

