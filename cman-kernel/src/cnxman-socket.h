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

/* CMAN socket interface header,
   may be include by user or kernel code */

#ifndef __CNXMAN_SOCKET_H
#define __CNXMAN_SOCKET_H

/* A currently unused number. TIPC also uses this number and you're unlikely
   to be using both.
 */
#define AF_CLUSTER 30
#define PF_CLUSTER AF_CLUSTER

/* Protocol(socket) types */
#define CLPROTO_MASTER 2
#define CLPROTO_CLIENT 3

/* ioctls -- should register these properly */
#define SIOCCLUSTER_NOTIFY            _IOW('x', 0x01, int)
#define SIOCCLUSTER_REMOVENOTIFY      _IO( 'x', 0x02)
#define SIOCCLUSTER_GETMEMBERS        _IOR('x', 0x03, struct cl_cluster_nodelist)
#define SIOCCLUSTER_SETEXPECTED_VOTES _IOW('x', 0x04, int)
#define SIOCCLUSTER_ISQUORATE         _IO( 'x', 0x05)
#define SIOCCLUSTER_ISLISTENING       _IOW('x', 0x06, struct cl_listen_request)
#define SIOCCLUSTER_GETALLMEMBERS     _IOR('x', 0x07, struct cl_cluster_nodelist)
#define SIOCCLUSTER_SET_VOTES         _IOW('x', 0x08, int)
#define SIOCCLUSTER_GET_VERSION       _IOR('x', 0x09, struct cl_version)
#define SIOCCLUSTER_SET_VERSION       _IOW('x', 0x0a, struct cl_version)
#define SIOCCLUSTER_ISACTIVE          _IO( 'x', 0x0b)
#define SIOCCLUSTER_KILLNODE          _IOW('x', 0x0c, int)
#define SIOCCLUSTER_GET_JOINCOUNT     _IO( 'x', 0x0d)
#define SIOCCLUSTER_SERVICE_REGISTER  _IOW('x', 0x0e, char)
#define SIOCCLUSTER_SERVICE_UNREGISTER _IO('x', 0x0f)
#define SIOCCLUSTER_SERVICE_JOIN      _IO( 'x', 0x10)
#define SIOCCLUSTER_SERVICE_LEAVE     _IO( 'x', 0x20)
#define SIOCCLUSTER_SERVICE_SETSIGNAL _IOW('x', 0x30, int)
#define SIOCCLUSTER_SERVICE_STARTDONE _IOW('x', 0x40, unsigned int)
#define SIOCCLUSTER_SERVICE_GETEVENT  _IOR('x', 0x50, struct cl_service_event)
#define SIOCCLUSTER_SERVICE_GETMEMBERS _IOR('x', 0x60, struct cl_cluster_nodelist)
#define SIOCCLUSTER_SERVICE_GLOBALID  _IOR('x', 0x70, uint32_t)
#define SIOCCLUSTER_SERVICE_SETLEVEL  _IOR('x', 0x80, int)
#define SIOCCLUSTER_GETNODE	      _IOWR('x', 0x90, struct cl_cluster_node)
#define SIOCCLUSTER_BARRIER           _IOW('x', 0x0a0, struct cl_barrier_info)

/* These were setsockopts */
#define SIOCCLUSTER_PASS_SOCKET       _IOW('x', 0x0b0, struct cl_passed_sock)
#define SIOCCLUSTER_SET_NODENAME      _IOW('x', 0x0b1, char *)
#define SIOCCLUSTER_SET_NODEID        _IOW('x', 0x0b2, int)
#define SIOCCLUSTER_JOIN_CLUSTER      _IOW('x', 0x0b3, struct cl_join_cluster_info)
#define SIOCCLUSTER_LEAVE_CLUSTER     _IOW('x', 0x0b4, int)


/* Maximum size of a cluster message */
#define MAX_CLUSTER_MESSAGE          1500
#define MAX_CLUSTER_MEMBER_NAME_LEN   255
#define MAX_BARRIER_NAME_LEN           33
#define MAX_SA_ADDR_LEN                12
#define MAX_CLUSTER_NAME_LEN           16

/* Well-known cluster port numbers */
#define CLUSTER_PORT_MEMBERSHIP  1	/* Mustn't block during cluster
					 * transitions! */
#define CLUSTER_PORT_SERVICES    2
#define CLUSTER_PORT_SYSMAN      10	/* Remote execution daemon */
#define CLUSTER_PORT_CLVMD       11	/* Cluster LVM daemon */
#define CLUSTER_PORT_SLM         12	/* LVM SLM (simple lock manager) */

/* Port numbers above this will be blocked when the cluster is inquorate or in
 * transition */
#define HIGH_PROTECTED_PORT      9

/* Reasons for leaving the cluster */
#define CLUSTER_LEAVEFLAG_DOWN     0	/* Normal shutdown */
#define CLUSTER_LEAVEFLAG_KILLED   1
#define CLUSTER_LEAVEFLAG_PANIC    2
#define CLUSTER_LEAVEFLAG_REMOVED  3	/* This one can reduce quorum */
#define CLUSTER_LEAVEFLAG_REJECTED 4	/* Not allowed into the cluster in the
					 * first place */
#define CLUSTER_LEAVEFLAG_INCONSISTENT 5	/* Our view of the cluster is
						 * in a minority */
#define CLUSTER_LEAVEFLAG_DEAD         6	/* Discovered to be dead */
#define CLUSTER_LEAVEFLAG_FORCE     0x10	/* Forced by command-line */

/* OOB messages sent to a local socket */
#define CLUSTER_OOB_MSG_PORTCLOSED  1
#define CLUSTER_OOB_MSG_STATECHANGE 2
#define CLUSTER_OOB_MSG_SERVICEEVENT 3

/* Sendmsg flags, these are above the normal sendmsg flags so they don't
 * interfere */
#define MSG_NOACK     0x010000	/* Don't need an ACK for this message */
#define MSG_QUEUE     0x020000	/* Queue the message for sending later */
#define MSG_MULTICAST 0x080000	/* Message was sent to all nodes in the cluster
				 */
#define MSG_ALLINT    0x100000	/* Send out of all interfaces */

typedef enum { NODESTATE_REMOTEMEMBER, NODESTATE_JOINING, NODESTATE_MEMBER,
	       NODESTATE_DEAD } nodestate_t;


struct sockaddr_cl {
	unsigned short scl_family;
	unsigned char scl_flags;
	unsigned char scl_port;
	int           scl_nodeid;
};

/*
 * This is how we pass the multicast & receive sockets into kernel space.
 */
struct cl_passed_sock {
	int fd;			/* FD of master socket to do multicast on */
	int number;		/* Socket number, to match up recvonly & bcast
				 * sockets */
        int multicast;          /* Is it multicast or receive ? */
};

/* Cluster configuration info passed when we join the cluster */
struct cl_join_cluster_info {
	unsigned char votes;
	unsigned int expected_votes;
	unsigned int two_node;
	unsigned int config_version;

        char cluster_name[17];
};


/* This is the structure, per node, returned from the membership ioctl */
struct cl_cluster_node {
	unsigned int size;
	unsigned int node_id;
	unsigned int us;
	unsigned int leave_reason;
	unsigned int incarnation;
	nodestate_t state;
	char name[MAX_CLUSTER_MEMBER_NAME_LEN];
	unsigned char votes;
};

/* The struct passed to the membership ioctls */
struct cl_cluster_nodelist {
        uint32_t max_members;
        struct cl_cluster_node *nodes;
};

/* Structure passed to SIOCCLUSTER_ISLISTENING */
struct cl_listen_request {
	unsigned char port;
        int           nodeid;
};

/* A Cluster PORTCLOSED message - received by a local user as an OOB message */
struct cl_portclosed_oob {
	unsigned char cmd;	/* CLUSTER_OOB_MSG_PORTCLOSED */
	unsigned char port;
};

/* Get all version numbers or set the config version */
struct cl_version {
	unsigned int major;
	unsigned int minor;
	unsigned int patch;
	unsigned int config;
};

/* structure passed to barrier ioctls */
struct cl_barrier_info {
	char cmd;
	char name[MAX_BARRIER_NAME_LEN];
	unsigned int flags;
	unsigned long arg;
};

typedef enum { SERVICE_EVENT_STOP, SERVICE_EVENT_START, SERVICE_EVENT_FINISH,
		SERVICE_EVENT_LEAVEDONE } service_event_t;

typedef enum { SERVICE_START_FAILED, SERVICE_START_JOIN, SERVICE_START_LEAVE }
		service_start_t;

struct cl_service_event {
	service_event_t type;
	service_start_t start_type;
	unsigned int event_id;
	unsigned int last_stop;
	unsigned int last_start;
	unsigned int last_finish;
	unsigned int node_count;
};


/* Commands to the barrier ioctl */
#define BARRIER_IOCTL_REGISTER 1
#define BARRIER_IOCTL_CHANGE   2
#define BARRIER_IOCTL_DELETE   3
#define BARRIER_IOCTL_WAIT     4

/* Attributes of a barrier - bitmask */
#define BARRIER_ATTR_AUTODELETE 1
#define BARRIER_ATTR_MULTISTEP  2
#define BARRIER_ATTR_MANUAL     4
#define BARRIER_ATTR_ENABLED    8
#define BARRIER_ATTR_CALLBACK  16

/* Attribute setting commands */
#define BARRIER_SETATTR_AUTODELETE 1
#define BARRIER_SETATTR_MULTISTEP  2
#define BARRIER_SETATTR_ENABLED    3
#define BARRIER_SETATTR_NODES      4
#define BARRIER_SETATTR_CALLBACK   5
#define BARRIER_SETATTR_TIMEOUT    6

#endif
