#ifndef __CNXMAN_PRIVATE_H
#define __CNXMAN_PRIVATE_H

/* Protocol Version triplet */
#define CNXMAN_MAJOR_VERSION 6
#define CNXMAN_MINOR_VERSION 2
#define CNXMAN_PATCH_VERSION 0

struct cman_timer
{
	struct list list;
	struct timeval tv;
	int active;
	void (*callback)(void *arg);
	void *arg;
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
	unsigned char first_trans;
	uint16_t cluster_id;
	int votes;
	int expected_votes;

	unsigned int   major_version;	/* Not backwards compatible */
	unsigned int   minor_version;	/* Backwards compatible */
	unsigned int   patch_version;	/* Backwards/forwards compatible */
	unsigned int   config_version;
	unsigned int   flags;
	uint64_t       fence_time;
	uint64_t       join_time;
        char           clustername[16];
	char           fence_agent[];
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

struct cl_fencemsg {
	unsigned char cmd;
	unsigned char fenced;
	uint16_t      pad;
	int           nodeid;
	uint64_t      timesec;
	char          agent[0];
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
	uint32_t   confchg;     /* Registered for confchg */
	struct list write_msgs; /* Queued messages to go to data clients */
	uint32_t    num_write_msgs; /* Count of messages */
	struct connection *next;
	struct list list;       /* when on the client_list */
};

/* Parameters for RECONFIG command */
#define RECONFIG_PARAM_EXPECTED_VOTES 1
#define RECONFIG_PARAM_NODE_VOTES     2
#define RECONFIG_PARAM_CONFIG_VERSION 3
#define RECONFIG_PARAM_CCS            4

/* NODE_FLAGS_BEENDOWN       - This node has been down.
   NODE_FLAGS_FENCED         - This node has been fenced since it last went down.
   NODE_FLAGS_FENCEDWHILEUP  - This node was fenced manually (probably).
   NODE_FLAGS_SEESDISALLOWED - Only set in a transition message
   NODE_FLAGS_DIRTY          - This node has internal state and must not join
                               a cluster that also has state.
   NODE_FLAGS_REREAD	     - Set when the node is re-read from config, so
                               we can spot deleted nodes
*/
#define NODE_FLAGS_BEENDOWN           1
#define NODE_FLAGS_FENCED             2
#define NODE_FLAGS_FENCEDWHILEUP      4
#define NODE_FLAGS_SEESDISALLOWED     8
#define NODE_FLAGS_DIRTY             16
#define NODE_FLAGS_REREAD            32

/* There's one of these for each node in the cluster */
struct cluster_node {
	struct list list;
	char *name;		/* Node/host name of node */
	struct list addr_list;
	int us;			/* This node is us */
	unsigned int node_id;	/* Unique node ID */
	int flags;
	nodestate_t state;
	struct timeval join_time;

	/* When & how this node was last fenced */
	uint64_t fence_time; /* A time_t */
	char    *fence_agent;

	uint64_t cman_join_time; /* A time_t */

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
#define CLUSTER_MSG_ACK          1
#define CLUSTER_MSG_PORTOPENED   2
#define CLUSTER_MSG_PORTCLOSED   3
#define CLUSTER_MSG_BARRIER      4
#define CLUSTER_MSG_TRANSITION   5
#define CLUSTER_MSG_KILLNODE     6
#define CLUSTER_MSG_LEAVE        7
#define CLUSTER_MSG_RECONFIGURE  8
#define CLUSTER_MSG_PORTENQ      9
#define CLUSTER_MSG_PORTSTATUS  10
#define CLUSTER_MSG_FENCESTATUS 11

/* Kill reasons */
#define CLUSTER_KILL_REJECTED   1
#define CLUSTER_KILL_CMANTOOL   2
#define CLUSTER_KILL_REJOIN     3

#endif
