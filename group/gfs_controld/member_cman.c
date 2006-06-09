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
cman_cluster_t		cluster;
static cman_handle_t	ch;
extern struct list_head mounts;


static void cman_callback(cman_handle_t h, void *private, int reason, int arg)
{
	if (reason == CMAN_REASON_TRY_SHUTDOWN) {
		if (list_empty(&mounts))
			cman_replyto_shutdown(ch, 1);
		else {
			log_debug("no to cman shutdown");
			cman_replyto_shutdown(ch, 0);
		}
	}
}

void exit_cman(void)
{
	/* do we want to try to forcibly clean some stuff up
	   in the kernel here? */
	log_error("cluster is down, exiting");
	exit_libdlm();
	exit(1);
}

int process_cman(void)
{
	int rv;

	rv = cman_dispatch(ch, CMAN_DISPATCH_ALL);

	if (rv == -1 && errno == EHOSTDOWN)
		exit_cman();

	return 0;
}

int setup_cman(void)
{
	cman_node_t node;
	int rv, fd;

	ch = cman_init(NULL);
	if (!ch) {
		log_error("cman_init error %d", errno);
		return -ENOTCONN;
	}

	rv = cman_start_notification(ch, cman_callback);
	if (rv < 0) {
		log_error("cman_start_notification error %d %d", rv, errno);
		goto fail_finish;
	}

	/* FIXME: wait here for us to be a member of the cluster */

	memset(&cluster, 0, sizeof(cluster));
	rv = cman_get_cluster(ch, &cluster);
	if (rv < 0) {
		log_error("cman_get_cluster error %d %d", rv, errno);
		goto fail_stop;
	}
	clustername = cluster.ci_name;

	memset(&node, 0, sizeof(node));
	rv = cman_get_node(ch, CMAN_NODEID_US, &node);
	if (rv < 0) {
		log_error("cman_get_node error %d %d", rv, errno);
		goto fail_stop;
	}
	our_nodeid = node.cn_nodeid;

	fd = cman_get_fd(ch);
	return fd;

 fail_stop:
	cman_stop_notification(ch);
 fail_finish:
	cman_finish(ch);
	return rv;
}

