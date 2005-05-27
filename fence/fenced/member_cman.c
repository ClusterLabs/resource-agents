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

#include "fd.h"
#include "libcman.h"

#define BUFLEN		128

static cman_handle_t	ch;
static cman_node_t	cluster_nodes[MAX_NODES];
static cman_node_t	new_nodes[MAX_NODES];
static int		cluster_count;
static char		name_buf[CMAN_MAX_NODENAME_LEN+1];

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

	for (i = 0; i < cluster_count; i++) {
		if (name_equal(cluster_nodes[i].cn_name, name))
			return &cluster_nodes[i];
	}
	return NULL;
}

int setup_member(void)
{
	cman_node_t node;
	int rv;

	ch = cman_init(NULL);
	if (!ch) {
		log_error("cman_init error %d %d", (int) ch, errno);
		return -ENOTCONN;
	}

	/* FIXME: wait here for us to be a member of the cluster */

	rv = cman_get_node(ch, CMAN_NODEID_US, &node);
	if (rv < 0) {
		log_error("cman_get_node us error %d %d", rv, errno);
		cman_finish(ch);
		goto out;
	}

	memset(name_buf, 0, sizeof(name_buf));
	strncpy(name_buf, node.cn_name, CMAN_MAX_NODENAME_LEN);
	our_name = name_buf;
	our_nodeid = node.cn_nodeid;

	log_debug("our_nodeid %d our_name %s", our_nodeid, our_name);
	rv = 0;
 out:
	return rv;
}

void exit_member(void)
{
	cman_finish(ch);
}

int update_cluster_members(void)
{
	int rv, count;

	count = 0;
	memset(new_nodes, 0, sizeof(new_nodes));

	rv = cman_get_nodes(ch, MAX_NODES, &count, new_nodes);
	if (rv < 0) {
		log_error("cman_get_nodes error %d %d", rv, errno);
		return rv;
	}

	if (count < cluster_count)
		log_error("decrease in cluster nodes %d %d",
			  count, cluster_count);

	cluster_count = count;
	memcpy(cluster_nodes, new_nodes, sizeof(cluster_nodes));

	log_debug("node count %d", count);
	return 0;
}

/* update_cluster_members() is usually called prior to calling this */

int is_member(char *name)
{
	cman_node_t *cn;

	cn = find_cluster_node_name(name);
	if (cn && cn->cn_member)
		return TRUE;
	log_debug("node \"%s\" not a member, cn %d", name, cn ? 1 : 0);
	return FALSE;
}

fd_node_t *get_new_node(fd_t *fd, int nodeid, char *in_name)
{
	cman_node_t cn;
	fd_node_t *node = NULL;
	char *name = in_name;
	int rv;

	if (!name) {
		rv = cman_get_node(ch, nodeid, &cn);
		name = cn.cn_name;
	}

	node = malloc(sizeof(*node));
	memset(node, 0, sizeof(*node));

	node->nodeid = nodeid;
	strcpy(node->name, name);

	return node;
}

