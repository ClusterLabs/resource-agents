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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "lock_dlm.h"
#include "libcman.h"

int			our_nodeid;
char *			clustername;
static cman_cluster_t	cluster;
static cman_handle_t	ch;

int setup_cman(void)
{
	cman_node_t node;
	int rv;

	ch = cman_init(NULL);
	if (!ch) {
		log_error("cman_init error %d", errno);
		return -ENOTCONN;
	}

	/* FIXME: wait here for us to be a member of the cluster */

	rv = cman_get_cluster(ch, &cluster);
	if (rv < 0) {
		log_error("cman_get_cluster error %d %d", rv, errno);
		goto out;
	}

	clustername = cluster.ci_name;

	memset(&node, 0, sizeof(node));
	rv = cman_get_node(ch, CMAN_NODEID_US, &node);
	if (rv < 0)
		log_error("cman_get_node error %d %d", rv, errno);
	else
		our_nodeid = node.cn_nodeid;
 out:
	cman_finish(ch);

	return rv;
}

