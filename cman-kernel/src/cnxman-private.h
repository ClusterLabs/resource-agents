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

/* Version triplet */
#define CNXMAN_MAJOR_VERSION 2
#define CNXMAN_MINOR_VERSION 0
#define CNXMAN_PATCH_VERSION 1

#define MAX_RETRIES 3		/* Maximum number of send retries */
#define CAP_CLUSTER CAP_SYS_ADMIN	/* Capability needed to manage the
					 * cluster */
#ifdef __KERNEL__

/* How we announce ourself in console events */
#define CMAN_NAME "CMAN"

/* One of these per AF_CLUSTER socket */
struct cluster_sock {
	/* WARNING: sk has to be the first member */
	struct sock sk;

	unsigned char port;	/* Bound port or zero */
	int (*kernel_callback) (char *, int, char *, int, unsigned int);
	void *service_data;
};

#define cluster_sk(__sk) ((struct cluster_sock *)__sk)

/* We have one of these for each socket we use for communications */
struct cl_comms_socket {
	struct socket *sock;
	int broadcast;		/* This is a broadcast socket */
	int recv_only;		/* This is the unicast receive end of a
				 * multicast socket */
	struct sockaddr_in6 saddr; /* Socket address, contains the sockaddr for
				 * the remote end(s) */
	int addr_len;		/* Length of above */
	int number;		/* Internal socket number, used to cycle around
				 * sockets in case of network errors */
	struct file *file;	/* file pointer for user-passed in sockets */

	wait_queue_t wait;

	/* The socket list */
	struct list_head list;

	/* On here when it has something to say */
	struct list_head active_list;
	unsigned long active;
};

/* A client socket. We keep a list of these so we can notify clients of cluster
 * events */
struct cl_client_socket {
	struct socket    *sock;
	struct list_head  list;
};

/* This structure is tacked onto the start of a cluster message packet for our
 * own nefarious purposes. */
struct cl_protheader {
	unsigned char  port;
	unsigned char  flags;
	unsigned short cluster;	/* Our cluster number, little-endian */
	unsigned short seq;	/* Packet sequence number, little-endian */
	unsigned short ack;	/* inline ACK */
	int            srcid;	/* Node ID of the sender */
	int            tgtid;	/* Node ID of the target or 0 for multicast
				 * messages */
};

/* A cluster internal protocol message - port number 0 */
struct cl_protmsg {
	struct cl_protheader header;
	unsigned char cmd;
};

/* A Cluster ACK message */
struct cl_ackmsg {
	struct cl_protheader header;
	unsigned char  cmd;	/* Always CLUSTER_CMD_ACK */
	unsigned char  remport;	/* Remote port number the original message was
				 * for */
	unsigned char  aflags;	/* ACK flags 0=OK, 1=No listener */
	unsigned char  pad;
};

/* A Cluster LISTENREQ/LISTENRESP message */
struct cl_listenmsg {
	unsigned char  cmd;	/* CLUSTER_CMD_LISTENRESP/REQ */
	unsigned char  target_port;	/* Port to probe */
	unsigned char  listening;	/* Always 0 for LISTENREQ */
	unsigned char  pad;
	unsigned short tag;	/* PID of remote waiting process */
};

/* A Cluster PORTCLOSED message */
struct cl_closemsg {
	unsigned char cmd;	/* CLUSTER_CMD_PORTCLOSED */
	unsigned char port;
};

/* Structure of a newly dead node, passed from cnxman to kmembershipd */
struct cl_new_dead_node {
	struct list_head     list;
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
	unsigned int   flags;
	unsigned int   nodes;
	char name[MAX_BARRIER_NAME_LEN];
};

/* Membership services messages, the cl_protheader is added transparently */
struct cl_mem_hello_msg {
	unsigned char  cmd;
	unsigned char  flags;
	unsigned short members;	    /* Number of nodes in the cluster,
				     * little-endian */
	unsigned int   generation;  /* Current cluster generation number */
};

struct cl_mem_endtrans_msg {
	unsigned char  cmd;
	unsigned char  pad1;
	unsigned short pad2;
	unsigned int   quorum;
	unsigned int   total_votes;
	unsigned int   generation;	/* Current cluster generation number */
	unsigned int   new_node_id;	/* If reason is a new node joining */
};

/* ACK types for JOINACK message */
#define JOINACK_TYPE_OK   1	/* You can join */
#define JOINACK_TYPE_NAK  2	/* You can NOT join */
#define JOINACK_TYPE_WAIT 3	/* Wait a bit longer - cluster is in transition
				 * already */

struct cl_mem_joinack_msg {
	unsigned char cmd;
	unsigned char acktype;
};

/* This is used by JOINREQ message */
struct cl_mem_join_msg {
	unsigned char  cmd;
	unsigned char  votes;
	unsigned short num_addr;	/* Number of addresses for this node */
	unsigned int   expected_votes;
	unsigned int   members;	/* Number of nodes in the cluster,
				 * little-endian */
	unsigned int   major_version;	/* Not backwards compatible */
	unsigned int   minor_version;	/* Backwards compatible */
	unsigned int   patch_version;	/* Backwards/forwards compatible */
	unsigned int   config_version;
        unsigned int   addr_len;        /* length of node addresses */
        char           clustername[16];
	/* Followed by <num_addr> addresses of `address_length` bytes and a
	 * NUL-terminated node name */
};

/* State transition start reasons: */
#define TRANS_NEWNODE        1	/* A new node is joining the cluster */
#define TRANS_REMNODE        2	/* a node has left the cluster */
#define TRANS_ANOTHERREMNODE 3	/* A node left the cluster while we were in
				 * transition */
#define TRANS_NEWMASTER      4	/* We have had an election and I am the new
				 * master */
#define TRANS_CHECK          5	/* A consistency check was called for */
#define TRANS_RESTART        6	/* Transition restarted because of a previous
				 * timeout */
#define TRANS_DEADMASTER     7	/* The master died during transition and I have
				 * taken over */

/* This is used to start a state transition */
struct cl_mem_starttrans_msg {
	unsigned char  cmd;
	unsigned char  reason;	/* Why a start transition was started - see
				 * above */
	unsigned char  flags;
	unsigned char  votes;
	unsigned int   expected_votes;
	unsigned int   generation;	/* Incremented for each STARTTRANS sent
					 */
	int            nodeid;	/* Node to be removed */
	unsigned short num_addrs;
	/* If reason == TRANS_NEWNODE: Followed by <num_addr> addresses of
	 * `address_length` bytes and a NUL-terminated node name */
};

struct cl_mem_startack_msg {
	unsigned char  cmd;
	unsigned char  reason;
	unsigned short pad;
	unsigned int   generation;
	unsigned int   node_id;	/* node_id we think new node should have */
	unsigned int   highest_node_id;	/* highest node_id on this system */
};

/* Reconfigure a cluster parameter */
struct cl_mem_reconfig_msg {
	unsigned char  cmd;
	unsigned char  param;
	unsigned short pad;
	unsigned int   value;
};

/* Structure containing information about an outstanding listen request */
struct cl_waiting_listen_request {
	wait_queue_head_t waitq;
	int               result;
	int               waiting;
	unsigned short    tag;
	int               nodeid;
	struct list_head  list;
};

/* Messages from membership services */
#define CLUSTER_MEM_JOINCONF   1
#define CLUSTER_MEM_JOINREQ    2
#define CLUSTER_MEM_LEAVE      3
#define CLUSTER_MEM_HELLO      4
#define CLUSTER_MEM_KILL       5
#define CLUSTER_MEM_JOINACK    6
#define CLUSTER_MEM_ENDTRANS   7
#define CLUSTER_MEM_RECONFIG   8
#define CLUSTER_MEM_MASTERVIEW 9
#define CLUSTER_MEM_STARTTRANS 10
#define CLUSTER_MEM_JOINREJ    11
#define CLUSTER_MEM_VIEWACK    12
#define CLUSTER_MEM_STARTACK   13
#define CLUSTER_MEM_TRANSITION 14
#define CLUSTER_MEM_NEWCLUSTER 15
#define CLUSTER_MEM_CONFACK    16
#define CLUSTER_MEM_NOMINATE   17

/* Flags in the HELLO message */
#define HELLO_FLAG_MASTER       1
#define HELLO_FLAG_QUORATE      2

/* Parameters for RECONFIG command */
#define RECONFIG_PARAM_EXPECTED_VOTES 1
#define RECONFIG_PARAM_NODE_VOTES     2
#define RECONFIG_PARAM_CONFIG_VERSION 3

/* Data associated with an outgoing socket */
struct cl_socket {
	struct file *file;	/* The real file */
	struct socket *socket;	/* The real sock */
	int num_nodes;		/* On this link */
	int retransmit_count;
};

/* There's one of these for each node in the cluster */
struct cluster_node {
	struct list_head list;
	char *name;		/* Node/host name of node */
	struct list_head addr_list;
	int us;			/* This node is us */
	unsigned int node_id;	/* Unique node ID */
	nodestate_t state;
	unsigned short last_seq_recv;
	unsigned short last_seq_acked;
	unsigned short last_seq_sent;
	unsigned int votes;
	unsigned int expected_votes;
	unsigned int leave_reason;
	unsigned int incarnation;	/* Incremented each time a node joins
					 * the cluster */
	unsigned long last_hello;	/* Jiffies */
};

/* This is how we keep a list of user processes that are listening for cluster
 * membership events */
struct notify_struct {
	struct list_head list;
	pid_t pid;
	int signal;
};

/* This is how we keep a list of kernel callbacks that are registered for
 * cluster membership events */
struct kernel_notify_struct {
	struct list_head list;
	void (*callback) (kcl_callback_reason, long arg);
};

/* A message waiting to be sent */
struct queued_message {
	struct list_head list;

	struct socket *socket;
	struct sockaddr_cl addr;
	int addr_len;
	int msg_len;
	unsigned char port;
	unsigned int flags;
	char msg_buffer[MAX_CLUSTER_MESSAGE];
};

/* A barrier */
struct cl_barrier {
	struct list_head list;

	char name[MAX_BARRIER_NAME_LEN];
	unsigned int flags;
	enum { BARRIER_STATE_WAITING, BARRIER_STATE_INACTIVE,
		    BARRIER_STATE_COMPLETE } state;
	unsigned int expected_nodes;
	unsigned int registered_nodes;
	atomic_t     got_nodes;
	atomic_t     completed_nodes;
	unsigned int inuse;
	unsigned int waitsent;
	unsigned int phase;	/* Completion phase */
	unsigned int endreason;	/* Reason we were woken, usually 0 */
	unsigned long timeout;	/* In seconds */

	void (*callback) (char *name, int status);
	wait_queue_head_t waitq;
	struct semaphore lock;	/* To synch with cnxman messages */
	spinlock_t phase2_spinlock;	/* Need to synchronise with timer
					 * interrupts */
	struct timer_list timer;
};

/* Cluster protocol commands sent to port 0 */
#define CLUSTER_CMD_ACK        1
#define CLUSTER_CMD_LISTENREQ  2
#define CLUSTER_CMD_LISTENRESP 3
#define CLUSTER_CMD_PORTCLOSED 4
#define CLUSTER_CMD_BARRIER    5

extern struct cluster_node *find_node_by_addr(unsigned char *addr,
					      int addr_len);
extern struct cluster_node *find_node_by_nodeid(unsigned int id);
extern struct cluster_node *find_node_by_name(char *name);
extern void set_quorate(int);
extern void notify_kernel_listeners(kcl_callback_reason reason, long arg);
extern void notify_listeners(void);
extern void free_nodeid_array(void);
extern int send_reconfigure(int param, unsigned int value);
extern int calculate_quorum(int, int, int *);
extern void recalculate_quorum(int);
extern int send_leave(unsigned char);
extern int get_quorum(void);
extern void set_votes(int, int);
extern void kcl_wait_for_all_acks(void);
extern char *membership_state(char *, int);
extern void a_node_just_died(struct cluster_node *node);
extern void check_barrier_returns(void);
extern int in_transition(void);
extern void get_local_addresses(struct cluster_node *node);
extern int add_node_address(struct cluster_node *node, unsigned char *addr, int len);
extern void create_proc_entries(void);
extern void cleanup_proc_entries(void);
extern unsigned int get_highest_nodeid(void);
extern int allocate_nodeid_array(void);
extern void queue_oob_skb(struct socket *sock, int cmd);
extern int new_temp_nodeid(char *addr, int addrlen);
extern int get_addr_from_temp_nodeid(int nodeid, char *addr, int *addrlen);
extern void purge_temp_nodeids(void);
extern inline char *print_addr(unsigned char *addr, int len, char *buf)
{
	int i;
	int ptr = 0;

	for (i = 0; i < len; i++)
		ptr += sprintf(buf + ptr, "%02x ", addr[i]);

	return buf;
}

#define MAX_ADDR_PRINTED_LEN (address_length*3 + 1)

/* Debug enabling macros. Sorry about the C++ comments but they're easier to
 * get rid of than C ones... */

// #define DEBUG_MEMB
// #define DEBUG_COMMS
// #define DEBUG_BARRIER

/* Debug macros */
#ifdef DEBUG_COMMS
#define P_COMMS(fmt, args...) printk(KERN_DEBUG "cman comms: " fmt, ## args)
#else
#define P_COMMS(fmt, args...)
#endif

#ifdef DEBUG_BARRIER
#define P_BARRIER(fmt, args...) printk(KERN_DEBUG "cman barrier: " fmt, ## args)
#else
#define P_BARRIER(fmt, args...)
#endif

#ifdef DEBUG_MEMB
#define P_MEMB(fmt, args...) printk(KERN_DEBUG "cman memb: " fmt, ## args)
#define C_MEMB(fmt, args...) printk(fmt, ## args)
#else
#define P_MEMB(fmt, args...)
#define C_MEMB(fmt, args...)
#endif

#endif				/* __KERNEL */

#endif
