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

#ifndef __CNXMAN_H
#define __CNXMAN_H

#include "linux/in6.h"
#include "cluster/cnxman-socket.h"

/* In-kernel API */

/* This is the structure, per node, returned from the membership request */
struct kcl_cluster_node {
	unsigned int size;
	unsigned int node_id;
	unsigned int us;
	unsigned int leave_reason;
	unsigned int incarnation;
	nodestate_t state;
	struct list_head list;
	char name[MAX_CLUSTER_MEMBER_NAME_LEN];
	unsigned char votes;
};

struct cluster_node_addr {
	struct list_head list;
	unsigned char addr[sizeof(struct sockaddr_in6)];/* A large sockaddr */
	int addr_len;
};


/* Reasons for a kernel membership callback */
typedef enum { CLUSTER_RECONFIG, DIED, LEAVING, NEWNODE } kcl_callback_reason;

/* Kernel version of above, the void *sock is a struct socket */
struct kcl_multicast_sock {
	void *sock;
	int number;		/* Socket number, to match up recvonly & bcast
				 * sockets */
};

extern int kcl_sendmsg(struct socket *sock, void *buf, int size,
		       struct sockaddr_cl *caddr, int addr_len,
		       unsigned int flags);
extern int kcl_register_read_callback(struct socket *sock,
				      int (*routine) (char *, int, char *, int,
						      unsigned int));
extern int kcl_add_callback(void (*callback) (kcl_callback_reason, long));
extern int kcl_remove_callback(void (*callback) (kcl_callback_reason, long));
extern int kcl_get_members(struct list_head *list);
extern int kcl_get_member_ids(uint32_t * idbuf, int size);
extern int kcl_get_all_members(struct list_head *list);
extern int kcl_get_node_by_addr(unsigned char *addr, int addr_len,
				struct kcl_cluster_node *n);
extern int kcl_get_node_by_name(unsigned char *name,
				struct kcl_cluster_node *n);
extern int kcl_get_node_by_nodeid(int nodeid, struct kcl_cluster_node *n);
extern int kcl_is_quorate(void);
extern int kcl_addref_cluster(void);
extern int kcl_releaseref_cluster(void);
extern int kcl_cluster_name(char **cname);
extern int kcl_get_current_interface(void);
extern struct list_head *kcl_get_node_addresses(int nodeid);

extern int kcl_barrier_register(char *name, unsigned int flags,
				unsigned int nodes);
extern int kcl_barrier_setattr(char *name, unsigned int attr,
			       unsigned long arg);
extern int kcl_barrier_delete(char *name);
extern int kcl_barrier_wait(char *name);
extern int kcl_barrier_cancel(char *name);

extern int kcl_register_quorum_device(char *name, int votes);
extern int kcl_unregister_quorum_device(void);
extern int kcl_quorum_device_available(int yesno);

#endif
