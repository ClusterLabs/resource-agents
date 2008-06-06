/*
 * CMAN socket interface header
 * Should only be used by libcman - if you want to call CMAN then use the library!
 */

#ifndef __CNXMAN_SOCKET_H
#define __CNXMAN_SOCKET_H

/*
 * Commands on the socket.
 * if the top bit is set then it is only allowed
 * on the ADMIN socket.
 */
#define CMAN_CMD_NOTIFY             0x00000001
#define CMAN_CMD_REMOVENOTIFY       0x00000002
#define CMAN_CMD_SETEXPECTED_VOTES  0x80000004
#define CMAN_CMD_ISQUORATE          0x00000005
#define CMAN_CMD_ISLISTENING        0x00000006
#define CMAN_CMD_GETALLMEMBERS      0x00000007
#define CMAN_CMD_SET_VOTES          0x80000008
#define CMAN_CMD_GET_VERSION        0x00000009
#define CMAN_CMD_SET_VERSION        0x8000000a
#define CMAN_CMD_ISACTIVE           0x0000000b
#define CMAN_CMD_KILLNODE           0x8000000c
#define CMAN_CMD_GET_JOINCOUNT      0x0000000d
#define CMAN_CMD_GETNODECOUNT       0x0000000e
#define CMAN_CMD_GETNODE	    0x00000090
#define CMAN_CMD_GETCLUSTER	    0x00000091
#define CMAN_CMD_GETEXTRAINFO	    0x00000092
#define CMAN_CMD_BARRIER            0x000000a0
#define CMAN_CMD_LEAVE_CLUSTER      0x800000b4
#define CMAN_CMD_REG_QUORUMDEV      0x800000b5
#define CMAN_CMD_UNREG_QUORUMDEV    0x800000b6
#define CMAN_CMD_POLL_QUORUMDEV     0x800000b7
#define CMAN_CMD_TRY_SHUTDOWN       0x800000bb
#define CMAN_CMD_SHUTDOWN_REPLY     0x000000bc
#define CMAN_CMD_UPDATE_FENCE_INFO  0x800000bd
#define CMAN_CMD_GET_FENCE_INFO     0x000000be
#define CMAN_CMD_GET_NODEADDRS      0x000000bf
#define CMAN_CMD_START_CONFCHG      0x000000c0
#define CMAN_CMD_STOP_CONFCHG       0x000000c1
#define CMAN_CMD_SET_DIRTY          0x800000c2
#define CMAN_CMD_SET_DEBUGLOG       0x800000c3
#define CMAN_CMD_DUMP_OBJDB         0x800000c4

#define CMAN_CMD_DATA               0x00000100
#define CMAN_CMD_BIND               0x00000101
#define CMAN_CMD_EVENT              0x00000102
#define CMAN_CMD_CONFCHG            0x00000103

#define CMAN_CMDFLAG_PRIV           0x80000000
#define CMAN_CMDFLAG_REPLY          0x40000000
#define CMAN_CMDMASK_CMD            0x0000FFFF


/* Maximum size of a cluster message */
#define MAX_CLUSTER_MESSAGE          1500
#define MAX_CLUSTER_MEMBER_NAME_LEN   255
#define MAX_BARRIER_NAME_LEN           33
#define MAX_CLUSTER_NAME_LEN           16
#define MAX_FENCE_AGENT_NAME_LEN      255

/* Well-known cluster port numbers */
#define CLUSTER_PORT_MEMBERSHIP  1	/* Mustn't block during cluster
					 * transitions! */
#define CLUSTER_PORT_SERVICES    2
#define CLUSTER_PORT_SYSMAN      10	/* Remote execution daemon */
#define CLUSTER_PORT_CLVMD       11	/* Cluster LVM daemon */

/* Port numbers above this will be blocked when the cluster is inquorate or in
 * transition */
#define HIGH_PROTECTED_PORT      9

/* Nodeid passed to CMD_GETNODE to return the quorum device info */
#define CLUSTER_GETNODE_QUORUMDEV -1

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
#define CLUSTER_LEAVEFLAG_NORESPONSE   7        /* Didn't ACK message */
#define CLUSTER_LEAVEFLAG_FORCE     0x10	/* Forced by command-line */

/* CMAN_CMD_EVENT reason codes */
#define EVENT_REASON_PORTCLOSED   0
#define EVENT_REASON_STATECHANGE  1
#define EVENT_REASON_PORTOPENED   2
#define EVENT_REASON_TRY_SHUTDOWN 3

/* Shutdown flags */
#define SHUTDOWN_ANYWAY           1
#define SHUTDOWN_REMOVE           2

/*
 * Sendmsg flags, these are above the normal sendmsg flags so they don't
 * interfere
 */
#define MSG_TOTEM_AGREED 0x1000000
#define MSG_TOTEM_SAFE   0x2000000
#define MSG_BCASTSELF    0x4000000

typedef enum { NODESTATE_JOINING=1, NODESTATE_MEMBER,
	       NODESTATE_DEAD, NODESTATE_LEAVING, NODESTATE_AISONLY } nodestate_t;

static const char CLIENT_SOCKNAME[]= "/var/run/cman_client";
static const char ADMIN_SOCKNAME[]=  "/var/run/cman_admin";

/* This struct should be in front of all messages
 * passed down the client and admin sockets.
 */
#define CMAN_MAGIC 0x434d414e
#define CMAN_VERSION 0x10000003
struct sock_header {
	uint32_t magic;
	uint32_t version;
	uint32_t length;
	uint32_t command;
	uint32_t flags;
};

/* Data message header */
struct sock_data_header {
	struct sock_header header;
	int nodeid;
	uint32_t port;
	/* Data follows */
};

/* Reply message */
struct sock_reply_header {
	struct sock_header header;
	int status;
	/* Any returned information follows */
};

/* Event message */
struct sock_event_message {
	struct sock_header header;
	int reason;
	int arg;
};

/* confchg message */
struct sock_confchg_message {
	struct sock_header header;
	int member_entries;
	int left_entries;
	int joined_entries;
	unsigned int entries[]; // In above order.
};

/* Flags */
#define CMAN_EXTRA_FLAG_2NODE     1
#define CMAN_EXTRA_FLAG_ERROR     2
#define CMAN_EXTRA_FLAG_SHUTDOWN  4
#define CMAN_EXTRA_FLAG_UNCOUNTED 8
#define CMAN_EXTRA_FLAG_DIRTY    16

struct cl_extra_info {
	int           node_state;
	uint32_t      flags;
	int           node_votes;
	int           total_votes;
	int           expected_votes;
	int           quorum;
	int           members;
	char          ports[32];
	int           num_addresses; /* Number of real addresses, so the array below has
					<n>*2 addresses in it */
	char          addresses[1];  /* Array of num_addresses sockaddr_storage
					1st n are multicast addresses */
};

/* This is the structure, per node, returned from the membership call */
struct cl_cluster_node {
	unsigned int size;
	unsigned int node_id;
	unsigned int us;
	unsigned int leave_reason;
	unsigned int incarnation;
	nodestate_t state;
	char name[MAX_CLUSTER_MEMBER_NAME_LEN];
	char addr[sizeof(struct sockaddr_storage)];
	unsigned int addrlen;
	struct timeval jointime;
	unsigned char votes;
};

/* Structure passed to CMAN_CMD_ISLISTENING */
struct cl_listen_request {
	unsigned char port;
        int           nodeid;
};

/* Get all version numbers or set the config version */
struct cl_version {
	unsigned int major;
	unsigned int minor;
	unsigned int patch;
	unsigned int config;
};

/* structure passed to barrier command */
struct cl_barrier_info {
	char cmd;
	char name[MAX_BARRIER_NAME_LEN];
	unsigned int flags;
	unsigned long arg;
};

struct cl_cluster_info {
	char name[MAX_CLUSTER_NAME_LEN+1];
	uint16_t number;
	uint32_t generation;
};

struct cl_set_votes {
	int nodeid;
	int newvotes;
};

/* An array of these is returned */
struct cl_node_addrs {
	int addrlen;
	struct sockaddr_storage addr;
};

struct cl_get_node_addrs {
	int numaddrs;
	struct cl_node_addrs addrs[];
};

#define FENCE_FLAGS_FENCED 2
struct cl_fence_info {
	int nodeid;
	int flags;
	uint64_t fence_time;
	char fence_agent[MAX_FENCE_AGENT_NAME_LEN];
};

struct cl_qdev_info {
	char name[MAX_CLUSTER_MEMBER_NAME_LEN];
	int state;
	int votes;
};

/* Commands to the barrier cmd */
#define BARRIER_CMD_REGISTER 1
#define BARRIER_CMD_CHANGE   2
#define BARRIER_CMD_DELETE   3
#define BARRIER_CMD_WAIT     4

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
