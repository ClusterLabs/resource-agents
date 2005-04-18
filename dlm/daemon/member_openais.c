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

#include "evs.h"
#include "ais_types.h"
#include "saClm.h"

#define MAX_NODES	(256)

#define log_error(fmt, args...) fprintf(stderr, fmt "\n", ##args)
#define log_debug(fmt, args...) fprintf(stderr, fmt "\n", ##args)

static evs_handle_t	eh;
static int		cluster_nodes[MAX_NODES];
static int		cluster_count;
static char		id[256];
static char		ip[256];

int set_local(int argc, char **argv);
int set_node(int argc, char **argv);


static void set_idip(int nodeid, struct in_addr *a)
{
	memset(id, 0, 256);
	memset(ip, 0, 256);

	snprintf(id, 256, "%d", nodeid);
	inet_ntop(AF_INET, a, ip, 256);
}

static int do_set_local(int nodeid, struct in_addr *a)
{
	char *argv[] = { id, ip };

	set_idip(nodeid, a);
	log_debug("set_local %s %s", argv[0], argv[1]);
	return set_local(2, argv);
}

static int do_set_node(int nodeid, struct in_addr *a)
{
	char *argv[] = { id, ip };

	set_idip(nodeid, a);
	log_debug("set_node %s %s", argv[0], argv[1]);
	return set_node(2, argv);
}

static void add_cluster_node(int new_nodeid)
{
	cluster_nodes[cluster_count] = new_nodeid;
	cluster_count++;
}

static int find_cluster_node(int nodeid)
{
	int i;

	for (i = 0; i < cluster_count; i++) {
		if (cluster_nodes[i] == nodeid)
			return 1;
	}
	return 0;
}

static void process_member_cb(struct in_addr *member_list, int count)
{
	int i, found, nodeid;

	for (i = 0; i < count; i++) {
		found = find_cluster_node(member_list[i].s_addr);
		if (found)
			continue;

		nodeid = (int) member_list[i].s_addr;

		add_cluster_node(nodeid);

		do_set_node(nodeid, &member_list[i]);
	}
}

static void member_callback(struct in_addr *member_list,
                            int member_list_entries,
                            struct in_addr *left_list,
                            int left_list_entries,
                            struct in_addr *joined_list,
                            int joined_list_entries)
{
	process_member_cb(member_list, member_list_entries);
}

int process_member(void)
{
        int rv;

        rv = evs_dispatch(eh, EVS_DISPATCH_ALL);
        if (rv != EVS_OK)
                log_error("evs_dispatch error %d %d", rv, errno);

        return 0;
}

/* All this SaClm stuff just to get the local nodeid which isn't
   available through the evs api. */

void foo(SaInvocationT i, const SaClmClusterNodeT *node, SaAisErrorT error)
{
}

void bar (const SaClmClusterNotificationBufferT *b, SaUint32T n, SaAisErrorT e)
{
}

SaClmCallbacksT clm_callbacks = {
        .saClmClusterNodeGetCallback = foo,
        .saClmClusterTrackCallback = bar
};

int set_our_nodeid(void)
{
        SaVersionT version = { 'B', 1, 1 };
        SaClmHandleT handle;
        SaClmClusterNodeT node;
	struct in_addr a;
        int rv;

        rv = saClmInitialize(&handle, &clm_callbacks, &version);
        if (rv != SA_OK) {
                log_error("saClmInitialize error %d %d", rv, errno);
                return rv;
        }

        rv = saClmClusterNodeGet(handle, SA_CLM_LOCAL_NODE_ID, 0, &node);
        if (rv != SA_OK) {
                log_error("saClmClusterNodeGet error %d %d", rv, errno);
                return rv;
        }

        saClmFinalize(handle);

	a.s_addr = node.nodeId;
	do_set_local((int) node.nodeId, &a);

        return 0;
}

static void dummy(struct in_addr source_addr, void *msg, int len)
{
}

evs_callbacks_t callbacks = {
	dummy,
        member_callback
};

int setup_member(void)
{
	int rv, fd;

        rv = evs_initialize(&eh, &callbacks);
        if (rv != EVS_OK) {
                log_error("evs_initialize error %d %d", rv, errno);
                return -ENOTCONN;
        }

        rv = evs_fd_get(eh, &fd);
        if (rv != EVS_OK) {
                log_error("evs_fd_get error %d %d", rv, errno);
                return rv;
        }

        rv = set_our_nodeid();
        if (rv < 0) {
                evs_finalize(eh);
                fd = rv;
        }

	return fd;
}

