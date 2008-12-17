#include <sys/types.h>
#ifndef COROSYNC_BSD
#include <alloca.h>
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <corosync/corotypes.h>
#include <corosync/cfg.h>
#include <corosync/list.h>
#include <corosync/lcr/lcr_comp.h>
#include <corosync/engine/logsys.h>
#include <corosync/ipc_gen.h>
#include <corosync/engine/coroapi.h>
#include <corosync/engine/quorum.h>
#include <corosync/ipc_cmanquorum.h>
#include <corosync/list.h>

#define CMANQUORUM_MAJOR_VERSION 6
#define CMANQUORUM_MINOR_VERSION 3
#define CMANQUORUM_PATCH_VERSION 0

 /* Silly default to prevent accidents! */
#define DEFAULT_EXPECTED   1024
#define DEFAULT_QDEV_POLL 10000

LOGSYS_DECLARE_SUBSYS ("CMANQUORUM", LOG_INFO);

enum quorum_message_req_types {
	MESSAGE_REQ_EXEC_CMANQUORUM_NODEINFO  = 0,
	MESSAGE_REQ_EXEC_CMANQUORUM_RECONFIGURE = 1,
	MESSAGE_REQ_EXEC_CMANQUORUM_KILLNODE = 2,
};

#define NODE_FLAGS_BEENDOWN         1
#define NODE_FLAGS_SEESDISALLOWED   8
#define NODE_FLAGS_DIRTY           16
#define NODE_FLAGS_QDISK           32
#define NODE_FLAGS_REMOVED         64
#define NODE_FLAGS_US             128


typedef enum { NODESTATE_JOINING=1, NODESTATE_MEMBER,
	       NODESTATE_DEAD, NODESTATE_LEAVING, NODESTATE_AISONLY } nodestate_t;


/* This structure is tacked onto the start of a cluster message packet for our
 * own nefarious purposes. */
struct q_protheader {
	unsigned char  tgtport; /* Target port number */
	unsigned char  srcport; /* Source (originating) port number */
	unsigned short pad;
	unsigned int   flags;
	int            srcid;	/* Node ID of the sender */
	int            tgtid;	/* Node ID of the target */
} __attribute__((packed));

struct cluster_node {
	int flags;
	int node_id;
	unsigned int expected_votes;
	unsigned int votes;
	time_t join_time;

	nodestate_t state;

	struct timeval last_hello; /* Only used for quorum devices */

	struct list_head list;
};

#define CMANQUORUM_FLAG_FEATURE_DISALLOWED 1

static int quorum_flags;
static int quorum;
static int cluster_is_quorate;
static int first_trans = 1;
static unsigned int two_node;
static unsigned int quorumdev_poll = DEFAULT_QDEV_POLL;

static struct cluster_node *us;
static struct cluster_node *quorum_device = NULL;
static char quorum_device_name[CMANQUORUM_MAX_QDISK_NAME_LEN];
static corosync_timer_handle_t quorum_device_timer;
static struct list_head cluster_members_list;
static struct corosync_api_v1 *corosync_api;
static struct list_head trackers_list;
static unsigned int cman_members[PROCESSOR_COUNT_MAX+1];
static int cman_members_entries = 0;
static struct memb_ring_id cman_ringid;

#define max(a,b) (((a) > (b)) ? (a) : (b))
static struct cluster_node *find_node_by_nodeid(int nodeid);
static struct cluster_node *allocate_node(int nodeid);
static char *kill_reason(int reason);

static cs_tpg_handle group_handle;

#define CMAN_COMPATIBILITY
#ifdef CMAN_COMPATIBILITY
static struct corosync_tpg_group quorum_group[1] = {
        { .group          = "CMAN", .group_len      = 4},
};
static char clustername[16];
static uint32_t cluster_id;
static uint32_t config_version;
#else
static struct corosync_tpg_group quorum_group[1] = {
        { .group          = "CMANQUORUM", .group_len      = 6},
};
#endif

#define list_iterate(v, head) \
        for (v = (head)->next; v != head; v = v->next)

struct quorum_pd {
	unsigned char track_flags;
	int tracking_enabled;
	struct list_head list;
	void *conn;
};

/*
 * Service Interfaces required by service_message_handler struct
 */

static void cmanquorum_init(struct corosync_api_v1 *api,
			    quorum_set_quorate_fn_t report);

static void quorum_confchg_fn (
	enum totem_configuration_type configuration_type,
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id);

static void quorum_deliver_fn(unsigned int nodeid, struct iovec *iovec, int iov_len,
			      int endian_conversion_required);

static int cmanquorum_exec_init_fn (struct corosync_api_v1 *corosync_api);

static int quorum_lib_init_fn (void *conn);

static int quorum_lib_exit_fn (void *conn);

static void message_handler_req_exec_quorum_nodeinfo (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_quorum_reconfigure (
	void *message,
	unsigned int nodeid);

static void message_handler_req_exec_quorum_killnode (
	void *message,
	unsigned int nodeid);


static void message_handler_req_lib_cmanquorum_getinfo (void *conn, void *message);

static void message_handler_req_lib_cmanquorum_setexpected (void *conn, void *message);

static void message_handler_req_lib_cmanquorum_setvotes (void *conn, void *message);

static void message_handler_req_lib_cmanquorum_qdisk_register (void *conn, void *message);

static void message_handler_req_lib_cmanquorum_qdisk_unregister (void *conn, void *message);

static void message_handler_req_lib_cmanquorum_qdisk_poll (void *conn, void *message);

static void message_handler_req_lib_cmanquorum_qdisk_getinfo (void *conn, void *message);

static void message_handler_req_lib_cmanquorum_setdirty (void *conn, void *message);

static void message_handler_req_lib_cmanquorum_killnode (void *conn, void *message);

static void message_handler_req_lib_cmanquorum_leaving (void *conn, void *message);
static void message_handler_req_lib_cmanquorum_trackstart (void *conn, void *msg);
static void message_handler_req_lib_cmanquorum_trackstop (void *conn, void *msg);

static int quorum_exec_send_nodeinfo(void);
static int quorum_exec_send_reconfigure(int param, int nodeid, int value);
static int quorum_exec_send_killnode(int nodeid, unsigned int reason);

/*
 * Library Handler Definition
 */
static struct corosync_lib_handler quorum_lib_service[] =
{
	{ /* 0 */
		.lib_handler_fn				= message_handler_req_lib_cmanquorum_getinfo,
		.response_size				= sizeof (struct res_lib_cmanquorum_getinfo),
		.response_id				= MESSAGE_RES_CMANQUORUM_GETINFO,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn				= message_handler_req_lib_cmanquorum_setexpected,
		.response_size				= sizeof (struct res_lib_cmanquorum_status),
		.response_id				= MESSAGE_RES_CMANQUORUM_STATUS,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn				= message_handler_req_lib_cmanquorum_setvotes,
		.response_size				= sizeof (struct res_lib_cmanquorum_status),
		.response_id				= MESSAGE_RES_CMANQUORUM_STATUS,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 3 */
		.lib_handler_fn				= message_handler_req_lib_cmanquorum_qdisk_register,
		.response_size				= sizeof (struct res_lib_cmanquorum_status),
		.response_id				= MESSAGE_RES_CMANQUORUM_STATUS,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 4 */
		.lib_handler_fn				= message_handler_req_lib_cmanquorum_qdisk_unregister,
		.response_size				= sizeof (struct res_lib_cmanquorum_status),
		.response_id				= MESSAGE_RES_CMANQUORUM_STATUS,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 5 */
		.lib_handler_fn				= message_handler_req_lib_cmanquorum_qdisk_poll,
		.response_size				= sizeof (struct res_lib_cmanquorum_status),
		.response_id				= MESSAGE_RES_CMANQUORUM_STATUS,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 6 */
		.lib_handler_fn				= message_handler_req_lib_cmanquorum_qdisk_getinfo,
		.response_size				= sizeof (struct res_lib_cmanquorum_qdisk_getinfo),
		.response_id				= MESSAGE_RES_CMANQUORUM_QDISK_GETINFO,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 7 */
		.lib_handler_fn				= message_handler_req_lib_cmanquorum_setdirty,
		.response_size				= sizeof (struct res_lib_cmanquorum_status),
		.response_id				= MESSAGE_RES_CMANQUORUM_STATUS,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 8 */
		.lib_handler_fn				= message_handler_req_lib_cmanquorum_killnode,
		.response_size				= sizeof (struct res_lib_cmanquorum_status),
		.response_id				= MESSAGE_RES_CMANQUORUM_STATUS,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 9 */
		.lib_handler_fn				= message_handler_req_lib_cmanquorum_leaving,
		.response_size				= sizeof (struct res_lib_cmanquorum_status),
		.response_id				= MESSAGE_RES_CMANQUORUM_STATUS,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 10 */
		.lib_handler_fn				= message_handler_req_lib_cmanquorum_trackstart,
		.response_size				= sizeof (struct res_lib_cmanquorum_status),
		.response_id				= MESSAGE_RES_CMANQUORUM_STATUS,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 11 */
		.lib_handler_fn				= message_handler_req_lib_cmanquorum_trackstop,
		.response_size				= sizeof (struct res_lib_cmanquorum_status),
		.response_id				= MESSAGE_RES_CMANQUORUM_STATUS,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	}
};

static quorum_set_quorate_fn_t set_quorum;
/*
 * lcrso object definition
 */
static struct quorum_services_api_ver1 cmanquorum_iface_ver0 = {
	.init				= cmanquorum_init
};

static struct corosync_service_engine quorum_service_handler = {
	.name				        = "corosync cman quorum service v0.90",
	.id					= CMANQUORUM_SERVICE,
	.private_data_size			= sizeof (struct quorum_pd),
	.flow_control				= COROSYNC_LIB_FLOW_CONTROL_REQUIRED,
	.lib_init_fn				= quorum_lib_init_fn,
	.lib_exit_fn				= quorum_lib_exit_fn,
	.lib_engine				= quorum_lib_service,
	.lib_engine_count			= sizeof (quorum_lib_service) / sizeof (struct corosync_lib_handler),
	.exec_init_fn				= cmanquorum_exec_init_fn,
	.exec_engine				= NULL,
	.exec_engine_count		        = 0,
	.confchg_fn                             = NULL, /* Invoked by tpg */
};

/*
 * Dynamic loader definition
 */
static struct corosync_service_engine *quorum_get_service_handler_ver0 (void);

static struct corosync_service_engine_iface_ver0 quorum_service_handler_iface = {
	.corosync_get_service_engine_ver0 = quorum_get_service_handler_ver0
};

static struct lcr_iface corosync_quorum_ver0[2] = {
	{
		.name				= "corosync_cmanquorum",
		.version			= 0,
		.versions_replace		= 0,
		.versions_replace_count         = 0,
		.dependencies			= 0,
		.dependency_count		= 0,
		.constructor			= NULL,
		.destructor			= NULL,
		.interfaces			= (void **)(void *)&cmanquorum_iface_ver0
	},
	{
		.name				= "corosync_cmanquorum_iface",
		.version			= 0,
		.versions_replace		= 0,
		.versions_replace_count         = 0,
		.dependencies			= 0,
		.dependency_count		= 0,
		.constructor			= NULL,
		.destructor			= NULL,
		.interfaces			= NULL
	}
};

static struct lcr_comp quorum_comp_ver0 = {
	.iface_count			= 2,
	.ifaces			        = corosync_quorum_ver0
};


static struct corosync_service_engine *quorum_get_service_handler_ver0 (void)
{
	return (&quorum_service_handler);
}

__attribute__ ((constructor)) static void quorum_comp_register (void) {
        lcr_interfaces_set (&corosync_quorum_ver0[0], &cmanquorum_iface_ver0);
	lcr_interfaces_set (&corosync_quorum_ver0[1], &quorum_service_handler_iface);
	lcr_component_register (&quorum_comp_ver0);
}

static void cmanquorum_init(struct corosync_api_v1 *api,
			    quorum_set_quorate_fn_t report)
{
	ENTER();
	set_quorum = report;

	/* Load the library-servicing part of this module */
	api->service_link_and_init(api, "corosync_cmanquorum_iface", 0);

	LEAVE();
}

#define CMANQUORUM_MSG_ACK          1
#define CMANQUORUM_MSG_PORTOPENED   2
#define CMANQUORUM_MSG_PORTCLOSED   3
#define CMANQUORUM_MSG_BARRIER      4
#define CMANQUORUM_MSG_NODEINFO     5
#define CMANQUORUM_MSG_KILLNODE     6
#define CMANQUORUM_MSG_LEAVE        7
#define CMANQUORUM_MSG_RECONFIGURE  8
#define CMANQUORUM_MSG_PORTENQ      9
#define CMANQUORUM_MSG_PORTSTATUS  10
#define CMANQUORUM_MSG_FENCESTATUS 11

struct req_exec_quorum_nodeinfo {
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
#ifdef CMAN_COMPATIBILITY
	uint64_t       fence_time;      /* not used */
	uint64_t       join_time;
        char           clustername[16]; /* not used */
	char           fence_agent[];   /* not used */
#endif
} __attribute__((packed));

/* Parameters for RECONFIG command */
#define RECONFIG_PARAM_EXPECTED_VOTES 1
#define RECONFIG_PARAM_NODE_VOTES     2
#define RECONFIG_PARAM_LEAVING        3

struct req_exec_quorum_reconfigure {
	unsigned char  cmd;
	unsigned char  param;
	unsigned short pad;
	int            nodeid;
	unsigned int   value;
};

struct req_exec_quorum_killnode {
	unsigned char cmd;
	unsigned char pad1;
	uint16_t reason;
	int nodeid;
};

#ifdef CMAN_COMPATIBILITY
static uint16_t generate_cluster_id(char *name)
{
	int i;
	int value = 0;

	for (i=0; i<strlen(name); i++) {
		value <<= 1;
		value += name[i];
	}
	return value & 0xFFFF;
}
#endif

/* These just make the access a little neater */
static inline int objdb_get_string(struct corosync_api_v1 *corosync, unsigned int object_service_handle,
				   char *key, char **value)
{
	int res;

	*value = NULL;
	if ( !(res = corosync->object_key_get(object_service_handle,
					      key,
					      strlen(key),
					      (void *)value,
					      NULL))) {
		if (*value)
			return 0;
	}
	return -1;
}

static inline void objdb_get_int(struct corosync_api_v1 *corosync, unsigned int object_service_handle,
				 char *key, unsigned int *intvalue, unsigned int default_value)
{
	char *value = NULL;

	*intvalue = default_value;

	if (!corosync->object_key_get(object_service_handle, key, strlen(key),
				 (void *)&value, NULL)) {
		if (value) {
			*intvalue = atoi(value);
		}
	}
}

static int quorum_send_message(void *message, int len)
{
	struct iovec iov[2];
	struct q_protheader header;

	header.tgtport = 0;
	header.srcport = 0;
	header.flags   = 0;
	header.srcid   = us->node_id;
	header.tgtid   = 0;

	iov[0].iov_base = &header;
	iov[0].iov_len  = sizeof(header);
	iov[1].iov_base = message;
	iov[1].iov_len  = len;

	return corosync_api->tpg_joined_mcast(group_handle, iov, 2, TOTEM_AGREED);
}

static int cmanquorum_exec_init_fn (struct corosync_api_v1 *api)
{
	unsigned int object_handle;
	unsigned int find_handle;

	ENTER();

	corosync_api = api;

	list_init(&cluster_members_list);
	list_init(&trackers_list);

	/* Allocate a cluster_node for us */
	us = allocate_node(corosync_api->totem_nodeid_get());
	if (!us)
		return (1);

	us->flags |= NODE_FLAGS_US;
	us->state = NODESTATE_MEMBER;
	us->expected_votes = DEFAULT_EXPECTED;
	us->votes = 1;
	time(&us->join_time);

	/* Get configuration variables */
	corosync_api->object_find_create(OBJECT_PARENT_HANDLE, "quorum", strlen("quorum"), &find_handle);

	if (corosync_api->object_find_next(find_handle, &object_handle) == 0) {
		unsigned int value = 0;
		objdb_get_int(corosync_api, object_handle, "expected_votes", &us->expected_votes, DEFAULT_EXPECTED);
		objdb_get_int(corosync_api, object_handle, "votes", &us->votes, 1);
		objdb_get_int(corosync_api, object_handle, "two_node", &two_node, 0);
		objdb_get_int(corosync_api, object_handle, "quorumdev_poll", &quorumdev_poll, DEFAULT_QDEV_POLL);
		objdb_get_int(corosync_api, object_handle, "disallowed", &value, 0);
		if (value)
			quorum_flags |= CMANQUORUM_FLAG_FEATURE_DISALLOWED;
	}
	corosync_api->object_find_destroy(find_handle);

#ifdef CMAN_COMPATIBILITY
	corosync_api->object_find_create(OBJECT_PARENT_HANDLE, "cluster", strlen("cluster"), &find_handle);

	if (corosync_api->object_find_next(find_handle, &object_handle) == 0) {
		char *name;
		objdb_get_string(corosync_api, object_handle, "name", &name);
		if (strlen(name) < 16)
			strcpy(clustername, name);

		objdb_get_int(corosync_api, object_handle, "cluster_id", &cluster_id, 0);
		if (cluster_id == 0)
			cluster_id = generate_cluster_id(clustername);
		objdb_get_int(corosync_api, object_handle, "config_version", &config_version, 0);
	}
	corosync_api->object_find_destroy(find_handle);
#endif


	api->tpg_init(&group_handle, quorum_deliver_fn, quorum_confchg_fn);
	api->tpg_join(group_handle, quorum_group, 1);

	LEAVE();
	return (0);
}

static int quorum_lib_exit_fn (void *conn)
{
	struct quorum_pd *quorum_pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);

	ENTER();
	if (quorum_pd->tracking_enabled) {
		list_del (&quorum_pd->list);
		list_init (&quorum_pd->list);
	}
	LEAVE();
	return (0);
}


static int send_quorum_notification(void *conn)
{
	struct res_lib_cmanquorum_notification *res_lib_cmanquorum_notification;
	struct list_head *tmp;
	struct cluster_node *node;
	int cluster_members = 0;
	int i = 0;
	int size;
	char *buf;

	ENTER();
	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);
		cluster_members++;
        }
	if (quorum_device)
		cluster_members++;

	size = sizeof(struct res_lib_cmanquorum_notification) + sizeof(struct cmanquorum_node) * cluster_members;
	buf = alloca(size);
	if (!buf) {
		LEAVE();
		return -1;
	}

	res_lib_cmanquorum_notification = (struct res_lib_cmanquorum_notification *)buf;
	res_lib_cmanquorum_notification->quorate = cluster_is_quorate;
	res_lib_cmanquorum_notification->node_list_entries = cluster_members;
	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);
		res_lib_cmanquorum_notification->node_list[i].nodeid = node->node_id;
		res_lib_cmanquorum_notification->node_list[i++].state = node->state;
        }
	if (quorum_device) {
		res_lib_cmanquorum_notification->node_list[i].nodeid = 0;
		res_lib_cmanquorum_notification->node_list[i++].state = quorum_device->state | 0x80;
	}
	res_lib_cmanquorum_notification->header.id = MESSAGE_RES_CMANQUORUM_NOTIFICATION;
	res_lib_cmanquorum_notification->header.size = size;
	res_lib_cmanquorum_notification->header.error = CS_OK;

	/* Send it to all interested parties */
	if (conn) {
		int ret = corosync_api->ipc_conn_send_response(conn, buf, size);
		LEAVE();
		return ret;
	}
	else {
		struct quorum_pd *qpd;

		list_iterate(tmp, &trackers_list) {
			qpd = list_entry(tmp, struct quorum_pd, list);

			corosync_api->ipc_conn_send_response(corosync_api->ipc_conn_partner_get(qpd->conn), buf, size);
		}
	}
	LEAVE();
	return 0;
}

static void set_quorate(int total_votes)
{
	int quorate;

	ENTER();
	if (quorum > total_votes) {
		quorate = 0;
	}
	else {
		quorate = 1;
	}

	if (cluster_is_quorate && !quorate)
		log_printf(LOG_INFO, "quorum lost, blocking activity\n");
	if (!cluster_is_quorate && quorate)
		log_printf(LOG_INFO, "quorum regained, resuming activity\n");

	/* If we are newly quorate, then kill any AISONLY nodes */
	if (!cluster_is_quorate && quorate) {
		struct cluster_node *node = NULL;
		struct list_head *tmp;

		list_iterate(tmp, &cluster_members_list) {
			node = list_entry(tmp, struct cluster_node, list);
			if (node->state == NODESTATE_AISONLY)
				quorum_exec_send_killnode(node->node_id, CMANQUORUM_REASON_KILL_REJOIN);
		}
	}

	cluster_is_quorate = quorate;
	set_quorum(cman_members, cman_members_entries, quorate, &cman_ringid);
	ENTER();
}

static int calculate_quorum(int allow_decrease, unsigned int *ret_total_votes)
{
	struct list_head *nodelist;
	struct cluster_node *node;
	unsigned int total_votes = 0;
	unsigned int highest_expected = 0;
	unsigned int newquorum, q1, q2;
	unsigned int total_nodes = 0;
	unsigned int max_expected = 0;
	unsigned int leaving = 0;

	ENTER();
	list_iterate(nodelist, &cluster_members_list) {
		node = list_entry(nodelist, struct cluster_node, list);

		log_printf(LOG_DEBUG, "node %x state=%d, votes=%d, expected=%d\n",
			   node->node_id, node->state, node->votes, node->expected_votes);

		if (node->state == NODESTATE_MEMBER) {
			highest_expected =
				max(highest_expected, node->expected_votes);
			total_votes += node->votes;
			total_nodes++;
		}
		if (node->state == NODESTATE_LEAVING) {
			leaving = 1;
		}
	}

	if (quorum_device && quorum_device->state == NODESTATE_MEMBER)
		total_votes += quorum_device->votes;

	if (max_expected > 0)
		highest_expected = max_expected;

	/* This quorum calculation is taken from the OpenVMS Cluster Systems
	 * manual, but, then, you guessed that didn't you */
	q1 = (highest_expected + 2) / 2;
	q2 = (total_votes + 2) / 2;
	newquorum = max(q1, q2);

	/* Normally quorum never decreases but the system administrator can
	 * force it down by setting expected votes to a maximum value */
	if (!allow_decrease)
		newquorum = max(quorum, newquorum);

	/* The special two_node mode allows each of the two nodes to retain
	 * quorum if the other fails.  Only one of the two should live past
	 * fencing (as both nodes try to fence each other in split-brain.)
	 * Also: if there are more than two nodes, force us inquorate to avoid
	 * any damage or confusion.
	 */
	if (two_node && total_nodes <= 2)
		newquorum = 1;

	if (ret_total_votes)
		*ret_total_votes = total_votes;

	LEAVE();
	return newquorum;
}

/* Recalculate cluster quorum, set quorate and notify changes */
static void recalculate_quorum(int allow_decrease)
{
	unsigned int total_votes;

	ENTER();
	quorum = calculate_quorum(allow_decrease, &total_votes);
	set_quorate(total_votes);
	send_quorum_notification(NULL);
	LEAVE();
}

static int have_disallowed(void)
{
	struct cluster_node *node;
	struct list_head *tmp;

	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);
		if (node->state == NODESTATE_AISONLY)
			return 1;
	}

	return 0;
}

static void node_add_ordered(struct cluster_node *newnode)
{
	struct cluster_node *node = NULL;
	struct list_head *tmp;
	struct list_head *newlist = &newnode->list;

	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);

                if (newnode->node_id < node->node_id)
                        break;
        }

        if (!node)
		list_add(&newnode->list, &cluster_members_list);
        else {
                newlist->prev = tmp->prev;
                newlist->next = tmp;
                tmp->prev->next = newlist;
                tmp->prev = newlist;
        }
}

static struct cluster_node *allocate_node(int nodeid)
{
	struct cluster_node *cl;

	cl = malloc(sizeof(struct cluster_node));
	if (cl) {
		memset(cl, 0, sizeof(struct cluster_node));
		cl->node_id = nodeid;
		if (nodeid)
			node_add_ordered(cl);
	}
	return cl;
}

static struct cluster_node *find_node_by_nodeid(int nodeid)
{
	struct cluster_node *node;
	struct list_head *tmp;

	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);
		if (node->node_id == nodeid)
			return node;
	}
	return NULL;
}


static int quorum_exec_send_nodeinfo()
{
	struct req_exec_quorum_nodeinfo req_exec_quorum_nodeinfo;
	int ret;

	ENTER();

	req_exec_quorum_nodeinfo.cmd = CMANQUORUM_MSG_NODEINFO;
	req_exec_quorum_nodeinfo.config_version = config_version;
	req_exec_quorum_nodeinfo.expected_votes = us->expected_votes;
	req_exec_quorum_nodeinfo.votes = us->votes;
	req_exec_quorum_nodeinfo.major_version = CMANQUORUM_MAJOR_VERSION;
	req_exec_quorum_nodeinfo.minor_version = CMANQUORUM_MINOR_VERSION;
	req_exec_quorum_nodeinfo.patch_version = CMANQUORUM_PATCH_VERSION;
	req_exec_quorum_nodeinfo.flags = us->flags;
	req_exec_quorum_nodeinfo.join_time = us->join_time;
	req_exec_quorum_nodeinfo.first_trans = first_trans;
	if (have_disallowed())
		req_exec_quorum_nodeinfo.flags |= NODE_FLAGS_SEESDISALLOWED;

#ifdef CMAN_COMPATIBILITY
	strcpy(	req_exec_quorum_nodeinfo.clustername, clustername);
	req_exec_quorum_nodeinfo.cluster_id = cluster_id;
#endif

	ret = quorum_send_message(&req_exec_quorum_nodeinfo, sizeof(req_exec_quorum_nodeinfo));
	LEAVE();
	return ret;
}


static int quorum_exec_send_reconfigure(int param, int nodeid, int value)
{
	struct req_exec_quorum_reconfigure req_exec_quorum_reconfigure;
	int ret;

	ENTER();

	req_exec_quorum_reconfigure.cmd = CMANQUORUM_MSG_RECONFIGURE;
	req_exec_quorum_reconfigure.param = param;
	req_exec_quorum_reconfigure.nodeid = nodeid;
	req_exec_quorum_reconfigure.value = value;

	ret = quorum_send_message(&req_exec_quorum_reconfigure, sizeof(req_exec_quorum_reconfigure));
	LEAVE();
	return ret;
}

static int quorum_exec_send_killnode(int nodeid, unsigned int reason)
{
	struct req_exec_quorum_killnode req_exec_quorum_killnode;
	int ret;

	ENTER();

	req_exec_quorum_killnode.cmd = CMANQUORUM_MSG_KILLNODE;
	req_exec_quorum_killnode.nodeid = nodeid;
	req_exec_quorum_killnode.reason = reason;

	ret = quorum_send_message(&req_exec_quorum_killnode, sizeof(req_exec_quorum_killnode));
	LEAVE();
	return ret;
}

static void quorum_confchg_fn (
	enum totem_configuration_type configuration_type,
	unsigned int *member_list, int member_list_entries,
	unsigned int *left_list, int left_list_entries,
	unsigned int *joined_list, int joined_list_entries,
	struct memb_ring_id *ring_id)
{
	int i;
	int leaving = 0;
	struct cluster_node *node;

	ENTER();
	if (member_list_entries > 1)
		first_trans = 0;

	if (left_list_entries) {
		for (i = 0; i< left_list_entries; i++) {
			node = find_node_by_nodeid(left_list[i]);
			if (node) {
				if (node->state == NODESTATE_LEAVING)
					leaving = 1;
				node->state = NODESTATE_DEAD;
				node->flags |= NODE_FLAGS_BEENDOWN;
			}
		}
		recalculate_quorum(leaving);
	}

	if (member_list_entries) {
		memcpy(cman_members, member_list, sizeof(unsigned int) * member_list_entries);
		cman_members_entries = member_list_entries;
		if (quorum_device) {
			cman_members[cman_members_entries++] = 0;
		}
		quorum_exec_send_nodeinfo();
	}

	memcpy(&cman_ringid, ring_id, sizeof(*ring_id));
	LEAVE();
}

static void exec_quorum_nodeinfo_endian_convert (void *msg)
{
	struct req_exec_quorum_nodeinfo *nodeinfo = (struct req_exec_quorum_nodeinfo *)msg;

	nodeinfo->cluster_id = swab16(nodeinfo->cluster_id);
	nodeinfo->votes = swab32(nodeinfo->votes);
	nodeinfo->expected_votes = swab32(nodeinfo->expected_votes);
	nodeinfo->major_version = swab32(nodeinfo->major_version);
	nodeinfo->minor_version = swab32(nodeinfo->minor_version);
	nodeinfo->patch_version = swab32(nodeinfo->patch_version);
	nodeinfo->config_version = swab32(nodeinfo->config_version);
	nodeinfo->flags = swab32(nodeinfo->flags);
#ifdef CMAN_COMPATIBILITY
	nodeinfo->fence_time = swab64(nodeinfo->fence_time);
#endif
}

static void exec_quorum_reconfigure_endian_convert (void *msg)
{
	struct req_exec_quorum_reconfigure *reconfigure = (struct req_exec_quorum_reconfigure *)msg;
	reconfigure->nodeid = swab32(reconfigure->nodeid);
	reconfigure->value = swab32(reconfigure->value);
}

static void exec_quorum_killnode_endian_convert (void *msg)
{
	struct req_exec_quorum_killnode *killnode = (struct req_exec_quorum_killnode *)msg;
	killnode->reason = swab16(killnode->reason);
	killnode->nodeid = swab32(killnode->nodeid);
}

static void quorum_deliver_fn(unsigned int nodeid, struct iovec *iovec, int iov_len,
			      int endian_conversion_required)
{
	struct q_protheader *header = iovec->iov_base;
	char *buf;

	ENTER();

	if (endian_conversion_required) {
		header->srcid = swab32(header->srcid);
		header->tgtid = swab32(header->tgtid);
		header->flags = swab32(header->flags);
	}

	/* Only pass on messages for us or everyone */
	if (header->tgtport == 0 &&
	    (header->tgtid == us->node_id ||
	     header->tgtid == 0)) {
		buf = iovec->iov_base + sizeof(struct q_protheader);
		switch (*buf) {

		case CMANQUORUM_MSG_NODEINFO:
			if (endian_conversion_required)
				exec_quorum_nodeinfo_endian_convert(buf);
			message_handler_req_exec_quorum_nodeinfo (buf, header->srcid);
			break;
		case CMANQUORUM_MSG_RECONFIGURE:
			if (endian_conversion_required)
				exec_quorum_reconfigure_endian_convert(buf);
			message_handler_req_exec_quorum_reconfigure (buf, header->srcid);
			break;
		case CMANQUORUM_MSG_KILLNODE:
			if (endian_conversion_required)
				exec_quorum_killnode_endian_convert(buf);
			message_handler_req_exec_quorum_killnode (buf, header->srcid);
			break;

			/* Just ignore other messages */
		}
	}
	LEAVE();
}

static void message_handler_req_exec_quorum_nodeinfo (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_quorum_nodeinfo *req_exec_quorum_nodeinfo = (struct req_exec_quorum_nodeinfo *)message;
	struct cluster_node *node;

	ENTER();
	log_printf(LOG_LEVEL_DEBUG, "got nodeinfo message from cluster node %d\n", nodeid);

	node = find_node_by_nodeid(nodeid);
	if (!node) {
		node = allocate_node(nodeid);
	}
	if (!node) {
		// TODO enomem error
		return;
	}

	if (req_exec_quorum_nodeinfo->flags & NODE_FLAGS_SEESDISALLOWED && !have_disallowed()) {
		/* Must use syslog directly here or the message will never arrive */
		syslog(LOG_CRIT, "[CMANQUORUM]: Joined a cluster with disallowed nodes. must die");
		corosync_api->fatal_error(2, __FILE__, __LINE__); // CC:
		exit(2);
	}

	/* Update node state */
	if (req_exec_quorum_nodeinfo->minor_version >= 2)
		node->votes = req_exec_quorum_nodeinfo->votes;
	node->expected_votes = req_exec_quorum_nodeinfo->expected_votes;
	node->state = NODESTATE_MEMBER;

	/* Check flags for disallowed (if enabled) */
	if (quorum_flags & CMANQUORUM_FLAG_FEATURE_DISALLOWED) {
		if ((req_exec_quorum_nodeinfo->flags & NODE_FLAGS_DIRTY && node->flags & NODE_FLAGS_BEENDOWN) ||
		    (req_exec_quorum_nodeinfo->flags & NODE_FLAGS_DIRTY && req_exec_quorum_nodeinfo->first_trans && !(node->flags & NODE_FLAGS_US))) {
			if (node->state != NODESTATE_AISONLY) {
				if (cluster_is_quorate) {
					log_printf(LOG_CRIT, "Killing node %d because it has rejoined the cluster with existing state", node->node_id);
					node->state = NODESTATE_AISONLY;
					quorum_exec_send_killnode(nodeid, CMANQUORUM_REASON_KILL_REJOIN);
				}
				else {
					log_printf(LOG_CRIT, "Node %d not joined to quorum because it has existing state", node->node_id);
					node->state = NODESTATE_AISONLY;
				}
			}
		}
	}
	node->flags &= ~NODE_FLAGS_BEENDOWN;

	// TODO do we need this as well as in confchg ?
	recalculate_quorum(0);
	LEAVE();
}

static void message_handler_req_exec_quorum_killnode (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_quorum_killnode *req_exec_quorum_killnode = (struct req_exec_quorum_killnode *)message;

	if (req_exec_quorum_killnode->nodeid == corosync_api->totem_nodeid_get()) {
		log_printf(LOG_CRIT, "Killed by node %d: %s\n", nodeid, kill_reason(req_exec_quorum_killnode->reason));

		// Is there a better way!! ????
		exit(1);
	}
}

static void message_handler_req_exec_quorum_reconfigure (
	void *message,
	unsigned int nodeid)
{
	struct req_exec_quorum_reconfigure *req_exec_quorum_reconfigure = (struct req_exec_quorum_reconfigure *)message;
	struct cluster_node *node;
	struct list_head *nodelist;

	log_printf(LOG_LEVEL_DEBUG, "got reconfigure message from cluster node %d\n", nodeid);

	node = find_node_by_nodeid(req_exec_quorum_reconfigure->nodeid);
	if (!node)
		return;

	switch(req_exec_quorum_reconfigure->param)
	{
	case RECONFIG_PARAM_EXPECTED_VOTES:
		node->expected_votes = req_exec_quorum_reconfigure->value;

		list_iterate(nodelist, &cluster_members_list) {
			node = list_entry(nodelist, struct cluster_node, list);
			if (node->state == NODESTATE_MEMBER &&
			    node->expected_votes > req_exec_quorum_reconfigure->value) {
				node->expected_votes = req_exec_quorum_reconfigure->value;
			}
		}
		recalculate_quorum(1);  /* Allow decrease */
		break;

	case RECONFIG_PARAM_NODE_VOTES:
		node->votes = req_exec_quorum_reconfigure->value;
		recalculate_quorum(1);  /* Allow decrease */
		break;

	case RECONFIG_PARAM_LEAVING:
		node->state = NODESTATE_LEAVING;
		break;
	}
}

static int quorum_lib_init_fn (void *conn)
{
	struct quorum_pd *pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);

	ENTER();

	list_init (&pd->list);
	pd->conn = conn;

	LEAVE();
	return (0);
}

/* Message from the library */
static void message_handler_req_lib_cmanquorum_getinfo (void *conn, void *message)
{
	struct req_lib_cmanquorum_getinfo *req_lib_cmanquorum_getinfo = (struct req_lib_cmanquorum_getinfo *)message;
	struct res_lib_cmanquorum_getinfo res_lib_cmanquorum_getinfo;
	struct cluster_node *node;
	int highest_expected = 0;
	int total_votes = 0;
	cs_error_t error = CS_OK;

	log_printf(LOG_LEVEL_DEBUG, "got getinfo request on %p for node %d\n", conn, req_lib_cmanquorum_getinfo->nodeid);

	if (req_lib_cmanquorum_getinfo->nodeid) {
		node = find_node_by_nodeid(req_lib_cmanquorum_getinfo->nodeid);
	}
	else {
		node = us;
	}

	if (node) {
		struct cluster_node *iternode;
		struct list_head *nodelist;

		list_iterate(nodelist, &cluster_members_list) {
			iternode = list_entry(nodelist, struct cluster_node, list);

			if (node->state == NODESTATE_MEMBER) {
				highest_expected =
					max(highest_expected, node->expected_votes);
				total_votes += node->votes;
			}
		}

		if (quorum_device && quorum_device->state == NODESTATE_MEMBER) {
			total_votes += quorum_device->votes;
		}

		res_lib_cmanquorum_getinfo.votes = us->votes;
		res_lib_cmanquorum_getinfo.expected_votes = us->expected_votes;
		res_lib_cmanquorum_getinfo.highest_expected = highest_expected;

		res_lib_cmanquorum_getinfo.quorum = quorum;
		res_lib_cmanquorum_getinfo.total_votes = total_votes;
		res_lib_cmanquorum_getinfo.flags = 0;

		if (us->flags & NODE_FLAGS_DIRTY)
			res_lib_cmanquorum_getinfo.flags |= CMANQUORUM_INFO_FLAG_DIRTY;
		if (two_node)
			res_lib_cmanquorum_getinfo.flags |= CMANQUORUM_INFO_FLAG_TWONODE;
		if (cluster_is_quorate)
			res_lib_cmanquorum_getinfo.flags |= CMANQUORUM_INFO_FLAG_QUORATE;
		if (us->flags & NODE_FLAGS_SEESDISALLOWED)
			res_lib_cmanquorum_getinfo.flags |= CMANQUORUM_INFO_FLAG_DISALLOWED;
	}
	else {
		error = CS_ERR_NOT_EXIST;
	}

	res_lib_cmanquorum_getinfo.header.size = sizeof(res_lib_cmanquorum_getinfo);
	res_lib_cmanquorum_getinfo.header.id = MESSAGE_RES_CMANQUORUM_GETINFO;
	res_lib_cmanquorum_getinfo.header.error = error;
	corosync_api->ipc_conn_send_response(conn, &res_lib_cmanquorum_getinfo, sizeof(res_lib_cmanquorum_getinfo));
	log_printf(LOG_LEVEL_DEBUG, "getinfo response error: %d\n", error);
}

/* Message from the library */
static void message_handler_req_lib_cmanquorum_killnode (void *conn, void *message)
{
	struct req_lib_cmanquorum_killnode *req_lib_cmanquorum_killnode = (struct req_lib_cmanquorum_killnode *)message;
	struct res_lib_cmanquorum_status res_lib_cmanquorum_status;
	cs_error_t error = CS_OK;

	ENTER();

	quorum_exec_send_killnode(req_lib_cmanquorum_killnode->nodeid, req_lib_cmanquorum_killnode->reason);

	/* send status */
	res_lib_cmanquorum_status.header.size = sizeof(res_lib_cmanquorum_status);
	res_lib_cmanquorum_status.header.id = MESSAGE_RES_CMANQUORUM_STATUS;
	res_lib_cmanquorum_status.header.error = error;
	corosync_api->ipc_conn_send_response(conn, &res_lib_cmanquorum_status, sizeof(res_lib_cmanquorum_status));
	LEAVE();
}

/* Message from the library */
static void message_handler_req_lib_cmanquorum_setexpected (void *conn, void *message)
{
	struct req_lib_cmanquorum_setexpected *req_lib_cmanquorum_setexpected = (struct req_lib_cmanquorum_setexpected *)message;
	struct res_lib_cmanquorum_status res_lib_cmanquorum_status;
	cs_error_t error = CS_OK;

	ENTER();

	// TODO validate as cman does

	quorum_exec_send_reconfigure(RECONFIG_PARAM_EXPECTED_VOTES, us->node_id, req_lib_cmanquorum_setexpected->expected_votes);

	/* send status */
	res_lib_cmanquorum_status.header.size = sizeof(res_lib_cmanquorum_status);
	res_lib_cmanquorum_status.header.id = MESSAGE_RES_CMANQUORUM_STATUS;
	res_lib_cmanquorum_status.header.error = error;
	corosync_api->ipc_conn_send_response(conn, &res_lib_cmanquorum_status, sizeof(res_lib_cmanquorum_status));
	LEAVE();
}

/* Message from the library */
static void message_handler_req_lib_cmanquorum_setvotes (void *conn, void *message)
{
	struct req_lib_cmanquorum_setvotes *req_lib_cmanquorum_setvotes = (struct req_lib_cmanquorum_setvotes *)message;
	struct res_lib_cmanquorum_status res_lib_cmanquorum_status;
	cs_error_t error = CS_OK;

	ENTER();

	// TODO validate as cman does

	if (!req_lib_cmanquorum_setvotes->nodeid)
		req_lib_cmanquorum_setvotes->nodeid = corosync_api->totem_nodeid_get();

	quorum_exec_send_reconfigure(RECONFIG_PARAM_NODE_VOTES, req_lib_cmanquorum_setvotes->nodeid, req_lib_cmanquorum_setvotes->votes);

	/* send status */
	res_lib_cmanquorum_status.header.size = sizeof(res_lib_cmanquorum_status);
	res_lib_cmanquorum_status.header.id = MESSAGE_RES_CMANQUORUM_STATUS;
	res_lib_cmanquorum_status.header.error = error;
	corosync_api->ipc_conn_send_response(conn, &res_lib_cmanquorum_status, sizeof(res_lib_cmanquorum_status));
	LEAVE();
}

static void message_handler_req_lib_cmanquorum_leaving (void *conn, void *message)
{
	struct res_lib_cmanquorum_status res_lib_cmanquorum_status;
	cs_error_t error = CS_OK;

	ENTER();

	quorum_exec_send_reconfigure(RECONFIG_PARAM_LEAVING, us->node_id, 0);

	/* send status */
	res_lib_cmanquorum_status.header.size = sizeof(res_lib_cmanquorum_status);
	res_lib_cmanquorum_status.header.id = MESSAGE_RES_CMANQUORUM_STATUS;
	res_lib_cmanquorum_status.header.error = error;
	corosync_api->ipc_conn_send_response(conn, &res_lib_cmanquorum_status, sizeof(res_lib_cmanquorum_status));
	LEAVE();
}

static void quorum_device_timer_fn(void *arg)
{
	struct timeval now;

	ENTER();
	if (!quorum_device || quorum_device->state == NODESTATE_DEAD)
		return;
	gettimeofday(&now, NULL);
	if (quorum_device->last_hello.tv_sec + quorumdev_poll/1000 < now.tv_sec) {
		quorum_device->state = NODESTATE_DEAD;
		log_printf(LOG_INFO, "lost contact with quorum device\n");
		recalculate_quorum(0);
	}
	else {
		corosync_api->timer_add_duration((unsigned long long)quorumdev_poll*1000000, quorum_device,
						 quorum_device_timer_fn, &quorum_device_timer);
	}
	LEAVE();
}


static void message_handler_req_lib_cmanquorum_qdisk_register (void *conn, void *message)
{
	struct req_lib_cmanquorum_qdisk_register *req_lib_cmanquorum_qdisk_register = (struct req_lib_cmanquorum_qdisk_register *)message;
	struct res_lib_cmanquorum_status res_lib_cmanquorum_status;
	cs_error_t error = CS_OK;

	ENTER();

	if (quorum_device) {
		error = CS_ERR_EXIST;
	}
	else {
		quorum_device = allocate_node(0);
		quorum_device->state = NODESTATE_DEAD;
		quorum_device->votes = req_lib_cmanquorum_qdisk_register->votes;
		strcpy(quorum_device_name, req_lib_cmanquorum_qdisk_register->name);
		list_add(&quorum_device->list, &cluster_members_list);
	}

	/* send status */
	res_lib_cmanquorum_status.header.size = sizeof(res_lib_cmanquorum_status);
	res_lib_cmanquorum_status.header.id = MESSAGE_RES_CMANQUORUM_STATUS;
	res_lib_cmanquorum_status.header.error = error;
	corosync_api->ipc_conn_send_response(conn, &res_lib_cmanquorum_status, sizeof(res_lib_cmanquorum_status));
	LEAVE();
}

static void message_handler_req_lib_cmanquorum_qdisk_unregister (void *conn, void *message)
{
	struct res_lib_cmanquorum_status res_lib_cmanquorum_status;
	cs_error_t error = CS_OK;

	ENTER();

	if (quorum_device) {
		struct cluster_node *node = quorum_device;

		quorum_device = NULL;
		list_del(&node->list);
		free(node);
		recalculate_quorum(0);
	}
	else {
		error = CS_ERR_NOT_EXIST;
	}

	/* send status */
	res_lib_cmanquorum_status.header.size = sizeof(res_lib_cmanquorum_status);
	res_lib_cmanquorum_status.header.id = MESSAGE_RES_CMANQUORUM_STATUS;
	res_lib_cmanquorum_status.header.error = error;
	corosync_api->ipc_conn_send_response(conn, &res_lib_cmanquorum_status, sizeof(res_lib_cmanquorum_status));
	LEAVE();
}

static void message_handler_req_lib_cmanquorum_qdisk_poll (void *conn, void *message)
{
	struct req_lib_cmanquorum_qdisk_poll *req_lib_cmanquorum_qdisk_poll = (struct req_lib_cmanquorum_qdisk_poll *)message;
	struct res_lib_cmanquorum_status res_lib_cmanquorum_status;
	cs_error_t error = CS_OK;

	ENTER();

	if (quorum_device) {
		if (req_lib_cmanquorum_qdisk_poll->state) {
			gettimeofday(&quorum_device->last_hello, NULL);
			if (quorum_device->state == NODESTATE_DEAD) {
				quorum_device->state = NODESTATE_MEMBER;
				recalculate_quorum(0);

				corosync_api->timer_add_duration((unsigned long long)quorumdev_poll*1000000, quorum_device,
								 quorum_device_timer_fn, &quorum_device_timer);
			}
		}
		else {
			if (quorum_device->state == NODESTATE_MEMBER) {
				quorum_device->state = NODESTATE_DEAD;
				recalculate_quorum(0);
				corosync_api->timer_delete(quorum_device_timer);
			}
		}
	}
	else {
		error = CS_ERR_NOT_EXIST;
	}

	/* send status */
	res_lib_cmanquorum_status.header.size = sizeof(res_lib_cmanquorum_status);
	res_lib_cmanquorum_status.header.id = MESSAGE_RES_CMANQUORUM_STATUS;
	res_lib_cmanquorum_status.header.error = error;
	corosync_api->ipc_conn_send_response(conn, &res_lib_cmanquorum_status, sizeof(res_lib_cmanquorum_status));

	LEAVE();
}

static void message_handler_req_lib_cmanquorum_qdisk_getinfo (void *conn, void *message)
{
	struct res_lib_cmanquorum_qdisk_getinfo res_lib_cmanquorum_qdisk_getinfo;
	cs_error_t error = CS_OK;

	ENTER();

	if (quorum_device) {
		log_printf(LOG_LEVEL_DEBUG, "got qdisk_getinfo state %d\n", quorum_device->state);
		res_lib_cmanquorum_qdisk_getinfo.votes = quorum_device->votes;
		if (quorum_device->state == NODESTATE_MEMBER)
			res_lib_cmanquorum_qdisk_getinfo.state = 1;
		else
			res_lib_cmanquorum_qdisk_getinfo.state = 0;
		strcpy(res_lib_cmanquorum_qdisk_getinfo.name, quorum_device_name);
	}
	else {
		error = CS_ERR_NOT_EXIST;
	}

	/* send status */
	res_lib_cmanquorum_qdisk_getinfo.header.size = sizeof(res_lib_cmanquorum_qdisk_getinfo);
	res_lib_cmanquorum_qdisk_getinfo.header.id = MESSAGE_RES_CMANQUORUM_GETINFO;
	res_lib_cmanquorum_qdisk_getinfo.header.error = error;
	corosync_api->ipc_conn_send_response(conn, &res_lib_cmanquorum_qdisk_getinfo, sizeof(res_lib_cmanquorum_qdisk_getinfo));

	LEAVE();
}

static void message_handler_req_lib_cmanquorum_setdirty (void *conn, void *message)
{
	struct res_lib_cmanquorum_status res_lib_cmanquorum_status;
	cs_error_t error = CS_OK;

	ENTER();

	us->flags |= NODE_FLAGS_DIRTY;

	/* send status */
	res_lib_cmanquorum_status.header.size = sizeof(res_lib_cmanquorum_status);
	res_lib_cmanquorum_status.header.id = MESSAGE_RES_CMANQUORUM_STATUS;
	res_lib_cmanquorum_status.header.error = error;
	corosync_api->ipc_conn_send_response(conn, &res_lib_cmanquorum_status, sizeof(res_lib_cmanquorum_status));

	LEAVE();
}

static void message_handler_req_lib_cmanquorum_trackstart (void *conn, void *msg)
{
	struct req_lib_cmanquorum_trackstart *req_lib_cmanquorum_trackstart = (struct req_lib_cmanquorum_trackstart *)msg;
	struct res_lib_cmanquorum_status res_lib_cmanquorum_status;
	struct quorum_pd *quorum_pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);

	ENTER();
	/*
	 * If an immediate listing of the current cluster membership
	 * is requested, generate membership list
	 */
	if (req_lib_cmanquorum_trackstart->track_flags & CS_TRACK_CURRENT ||
	    req_lib_cmanquorum_trackstart->track_flags & CS_TRACK_CHANGES) {
		log_printf(LOG_LEVEL_DEBUG, "sending initial status to %p\n", conn);
		send_quorum_notification(corosync_api->ipc_conn_partner_get (conn));
	}

	/*
	 * Record requests for tracking
	 */
	if (req_lib_cmanquorum_trackstart->track_flags & CS_TRACK_CHANGES ||
	    req_lib_cmanquorum_trackstart->track_flags & CS_TRACK_CHANGES_ONLY) {

		quorum_pd->track_flags = req_lib_cmanquorum_trackstart->track_flags;
		quorum_pd->tracking_enabled = 1;

		list_add (&quorum_pd->list, &trackers_list);
	}

	/* send status */
	res_lib_cmanquorum_status.header.size = sizeof(res_lib_cmanquorum_status);
	res_lib_cmanquorum_status.header.id = MESSAGE_RES_CMANQUORUM_STATUS;
	res_lib_cmanquorum_status.header.error = CS_OK;
	corosync_api->ipc_conn_send_response(conn, &res_lib_cmanquorum_status, sizeof(res_lib_cmanquorum_status));

	LEAVE();
}

static void message_handler_req_lib_cmanquorum_trackstop (void *conn, void *msg)
{
	struct res_lib_cmanquorum_status res_lib_cmanquorum_status;
	struct quorum_pd *quorum_pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);

	ENTER();

	if (quorum_pd->tracking_enabled) {
		res_lib_cmanquorum_status.header.error = CS_OK;
		quorum_pd->tracking_enabled = 0;
		list_del (&quorum_pd->list);
		list_init (&quorum_pd->list);
	} else {
		res_lib_cmanquorum_status.header.error = CS_ERR_NOT_EXIST;
	}

	/* send status */
	res_lib_cmanquorum_status.header.size = sizeof(res_lib_cmanquorum_status);
	res_lib_cmanquorum_status.header.id = MESSAGE_RES_CMANQUORUM_STATUS;
	res_lib_cmanquorum_status.header.error = CS_OK;
	corosync_api->ipc_conn_send_response(conn, &res_lib_cmanquorum_status, sizeof(res_lib_cmanquorum_status));

	LEAVE();
}


static char *kill_reason(int reason)
{
	static char msg[1024];

	switch (reason)
	{
	case CMANQUORUM_REASON_KILL_REJECTED:
		return "our membership application was rejected";

	case CMANQUORUM_REASON_KILL_APPLICATION:
		return "we were killed by an application request";

	case CMANQUORUM_REASON_KILL_REJOIN:
		return "we rejoined the cluster without a full restart";

	default:
		sprintf(msg, "we got kill message number %d", reason);
		return msg;
	}
}

