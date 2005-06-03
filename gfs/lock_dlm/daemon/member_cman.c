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

static int              message_cb;
static int              message_nodeid;
static int              message_len;
static char             message_buf[MAX_MSGLEN];

/* MAX_MSGLEN of 1024 will support up to 96 group members:
   (1024 - 256) / (2 * 4) = 96 */

void receive_journals(char *buf, int len, int nodeid);


static void message_callback(cman_handle_t h, void *private, char *buf,
                             int len, uint8_t port, int nodeid)
{
	log_debug("message callback nodeid %d len %d", nodeid, len);

	message_cb = 1;
	memcpy(message_buf, buf, len);
	message_len = len;
	message_nodeid = nodeid;
}

int process_member(void)
{
	while (1) {
		cman_dispatch(ch, CMAN_DISPATCH_ONE);
		if (message_cb) {
			message_cb = 0;
			receive_journals(message_buf, message_len,
					 message_nodeid);
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
		log_error("cman_init error %d", errno);
		return -ENOTCONN;
	}

	/* FIXME: wait here for us to be a member of the cluster */

	rv = cman_get_cluster(ch, &cluster);
	if (rv < 0) {
		log_error("cman_get_cluster error %d %d", rv, errno);
		cman_finish(ch);
		return -EEXIST;
	}

	clustername = cluster.ci_name;

	rv = cman_get_node(ch, CMAN_NODEID_US, &node);
	if (rv < 0) {
		log_error("cman_get_node error %d %d", rv, errno);
		cman_finish(ch);
		return -ENOENT;
	}

	our_nodeid = node.cn_nodeid;

	rv = cman_start_recv_data(ch, message_callback, LOCK_DLM_PORT);
	if (rv < 0) {
		log_error("cman_start_recv_data error %d %d", rv, errno);
		cman_finish(ch);
		return rv;
	}

	fd = cman_get_fd(ch);

	log_debug("member %d", fd);

	return fd;
}

int send_journals_message(int nodeid, char *buf, int len)
{
	int error;

	error = cman_send_data(ch, buf, len, 0, LOCK_DLM_PORT, nodeid);
	if (error < 0)
		log_error("cman_send_data error %d errno %d", error, errno);
	return 0;
}

