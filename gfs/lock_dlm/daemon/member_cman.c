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

#include "libcman.h"

int			our_nodeid;
char *			clustername;
static cman_cluster_t	cluster;
static cman_handle_t	ch;

int setup_member(void)
{
	cman_node_t node;
	int rv;

	ch = cman_init(NULL);
	if (!ch)
		return -ENOTCONN;

	/* FIXME: wait here for us to be a member of the cluster */

	rv = cman_get_cluster(ch, &cluster);
	if (rv < 0) {
		cman_finish(ch);
		return -EEXIST;
	}

	clustername = cluster.ci_name;

	rv = cman_get_node(ch, CMAN_NODEID_US, &node);
	if (rv < 0) {
		cman_finish(ch);
		return -ENOENT;
	}

	our_nodeid = node.cn_nodeid;

	return 0;
}

