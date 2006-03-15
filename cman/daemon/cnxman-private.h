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

#ifndef __CNXMAN_PRIVATE_H
#define __CNXMAN_PRIVATE_H

// TODO move this
#include "totemip.h"


/* Protocol Version triplet */
#define CNXMAN_MAJOR_VERSION 6
#define CNXMAN_MINOR_VERSION 0
#define CNXMAN_PATCH_VERSION 1

/* How we announce ourself in console events */
#define CMAN_NAME "CMAN"

/* This is now just a convenient way to pass around
   node/port pairs */
struct sockaddr_cl {
	unsigned char scl_port;
	int           scl_nodeid;
};


struct cman_timer
{
	struct list list;
	struct timeval tv;
	int active;
	void (*callback)(void *arg);
	void *arg;
};

struct cluster_node_addr {
	struct list list;
	unsigned char addr[sizeof(struct sockaddr_storage)];/* A large sockaddr */
	int addr_len;
};


/* A cluster internal protocol message - port number 0 */
struct cl_protmsg {
	unsigned char cmd;
};


/* A Cluster PORT OPENED/CLOSED message */
struct cl_portmsg {
	unsigned char cmd;	/* CLUSTER_CMD_PORTOPENED/CLOSED */
	unsigned char port;
};

/* Structure of a newly dead node, passed from cnxman to kmembershipd */
struct cl_new_dead_node {
	struct list         list;
	struct cluster_node *node;
};

/* Subcommands for BARRIER message */
#define BARRIER_REGISTER 1
#define BARRIER_CHANGE   2
#define BARRIER_WAIT     4
#define BARRIER_COMPLETE 5

/* A Cluster BARRIER message */
struct cl_barriermsg {
	unsigned char  cmd;	/* CLUSTER_CMD_BARRIER */
	unsigned char  subcmd;	/* BARRIER sub command */
	unsigned short pad;

	char name[MAX_BARRIER_NAME_LEN];
};

struct cl_transmsg {
	unsigned char cmd;
	unsigned char pad;
	uint16_t cluster_id;
	int high_nodeid;
	int expected_votes;

	unsigned int   major_version;	/* Not backwards compatible */
	unsigned int   minor_version;	/* Backwards compatible */
	unsigned int   patch_version;	/* Backwards/forwards compatible */
	unsigned int   config_version;
        char           clustername[16];
};

struct cl_killmsg {
	unsigned char cmd;
	unsigned char pad1;
	uint16_t reason;
	int nodeid;
};

struct cl_leavemsg {
	unsigned char cmd;
	unsigned char pad1;
	uint16_t reason;
};


/* Reconfigure a cluster parameter */
struct cl_reconfig_msg {
	unsigned char  cmd;
	unsigned char  param;
	unsigned short pad;
	int            nodeid;
	unsigned int   value;
};


typedef enum {CON_COMMS, CON_CLIENT_RENDEZVOUS, CON_ADMIN_RENDEZVOUS,
	      CON_CLIENT, CON_ADMIN} con_type_t;

/* One of these for every connection we have open
   and need to select() on */
struct connection
{
	int fd;
	con_type_t type;
	uint32_t   port;        /* If bound client */
	enum {SHUTDOWN_REPLY_UNK=0, SHUTDOWN_REPLY_YES, SHUTDOWN_REPLY_NO} shutdown_reply;
	uint32_t   events;      /* Registered for events */
	struct list write_msgs; /* Queued messages to go to data clients */
	struct cl_comms_socket *clsock;
	struct connection *next;
	struct list list;       /* when on the client_list */
};

/* Parameters for RECONFIG command */
#define RECONFIG_PARAM_EXPECTED_VOTES 1
#define RECONFIG_PARAM_NODE_VOTES     2
#define RECONFIG_PARAM_CONFIG_VERSION 3
#define RECONFIG_PARAM_CCS            4

/* There's one of these for each node in the cluster */
struct cluster_node {
	struct list list;
	char *name;		/* Node/host name of node */
	struct list addr_list;
	int us;			/* This node is us */
	unsigned int node_id;	/* Unique node ID */
	struct totem_ip_address ais_node;
	nodestate_t state;
	struct timeval join_time;

	struct timeval last_hello; /* Only used for quorum devices */

	unsigned int votes;
	unsigned int expected_votes;
	unsigned int leave_reason;
	uint64_t     incarnation;

	/* 32 bytes gives us enough for 256 bits (8 bit port number) */
#define PORT_BITS_SIZE 32
 	unsigned char port_bits[PORT_BITS_SIZE]; /* bitmap of ports open on this node */
};


/* Cluster protocol commands sent to port 0 */
#define CLUSTER_MSG_ACK         1
#define CLUSTER_MSG_PORTOPENED  2
#define CLUSTER_MSG_PORTCLOSED  3
#define CLUSTER_MSG_BARRIER     4
#define CLUSTER_MSG_TRANSITION  5
#define CLUSTER_MSG_KILLNODE    6
#define CLUSTER_MSG_LEAVE       7
#define CLUSTER_MSG_RECONFIGURE 8
#define CLUSTER_MSG_PORTENQ     9
#define CLUSTER_MSG_PORTSTATUS 10

/* Kill reasons */
#define CLUSTER_KILL_REJECTED   1
#define CLUSTER_KILL_CMANTOOL   2

#define MAX_ADDR_PRINTED_LEN (address_length*3 + 1)

#endif
