/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**  
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "sm.h"
#include "config.h"

struct socket *		sm_socket;
uint32_t *		sm_new_nodeids;
uint32_t		sm_our_nodeid;
int			sm_quorum, sm_quorum_next;
struct list_head	sm_members;
int			sm_member_count;


/* 
 * Context: cnxman
 * Called by cnxman when it has a new member list.
 */

void sm_member_update(int quorate)
{
	sm_quorum_next = quorate;
	wake_serviced(DO_START_RECOVERY);
}

/* 
 * Context: cnxman
 * Called when module is loaded.
 */

void sm_init(void)
{
	sm_socket = NULL;
	sm_new_nodeids = NULL;
	sm_quorum = 0;
	sm_quorum_next = 0;
	sm_our_nodeid = 0;
	INIT_LIST_HEAD(&sm_members);
	sm_member_count = 0;

	init_services();
	init_messages();
	init_barriers();
	init_serviced();
	init_recovery();
	init_joinleave();
	init_sm_misc();
}

/* 
 * Context: cnxman
 * Called at beginning of cluster join procedure.
 */

void sm_start(void)
{
	struct sockaddr_cl saddr;
	struct socket *sock;
	int result;

	/* Create a communication channel among service managers */

	result = sock_create_kern(AF_CLUSTER, SOCK_DGRAM, CLPROTO_CLIENT, &sock);
	if (result < 0) {
		log_print("can't create socket %d", result);
		goto fail;
	}

	sm_socket = sock;

	saddr.scl_family = AF_CLUSTER;
	saddr.scl_port = CLUSTER_PORT_SERVICES;

	result = sock->ops->bind(sock, (struct sockaddr *) &saddr,
				 sizeof(saddr));
	if (result < 0) {
		log_print("can't bind socket %d", result);
		goto fail_release;
	}

	result = kcl_register_read_callback(sm_socket, sm_cluster_message);
	if (result < 0) {
		log_print("can't register read callback %d", result);
		goto fail_release;
	}

	sm_new_nodeids = (uint32_t *) kmalloc(cman_config.max_nodes *
						     sizeof(uint32_t),
						     GFP_KERNEL);
	start_serviced();

	/* cnxman should call sm_member_update() once we've joined - then we
	 * can get our first list of members and our own nodeid */

	return;

      fail_release:
	sock_release(sm_socket);
	sm_socket = NULL;

      fail:
	return;
}

/* 
 * Context: cnxman
 * Called before cnxman leaves the cluster.  If this returns an error to cman,
 * cman should not leave the cluster but return EBUSY.
 * If force is set we go away anyway. cman knows best in this case
 */

int sm_stop(int force)
{
	struct list_head *head;
	sm_group_t *sg;
	sm_node_t *node;
	int i, busy = FALSE, error = -EBUSY;

	for (i = 0; i < SG_LEVELS; i++) {
		if (!list_empty(&sm_sg[i])) {
			sg = list_entry(sm_sg[i].next, sm_group_t, list);
			log_error(sg, "sm_stop: SG still joined");
			busy = TRUE;
		}
	}

	if (!busy || force) {
		stop_serviced();

		if (sm_socket)
			sock_release(sm_socket);

		head = &sm_members;
		while (!list_empty(head)) {
			node = list_entry(head->next, sm_node_t, list);
			list_del(&node->list);
			sm_member_count--;
			kfree(node);
		}

		kfree(sm_new_nodeids);
		sm_init();
		error = 0;
	}
	return error;
}
