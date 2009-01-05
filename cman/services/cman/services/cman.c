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
#include <corosync/ipc_cman.h>
#include <corosync/cman.h>

#define CMAN_MAJOR_VERSION 6
#define CMAN_MINOR_VERSION 3
#define CMAN_PATCH_VERSION 0

LOGSYS_DECLARE_SUBSYS ("CMAN", LOG_INFO);

/* Messages we send on port 0 */
#define CLUSTER_MSG_PORTOPENED   2
#define CLUSTER_MSG_PORTCLOSED   3
#define CLUSTER_MSG_PORTENQ      9
#define CLUSTER_MSG_PORTSTATUS  10

/* This structure is tacked onto the start of a cluster message packet for our
 * own nefarious purposes. */
struct cman_protheader {
	unsigned char  tgtport; /* Target port number */
	unsigned char  srcport; /* Source (originating) port number */
	unsigned short pad;
	unsigned int   flags;
	int            srcid;	/* Node ID of the sender */
	int            tgtid;	/* Node ID of the target */
};

static cs_tpg_handle group_handle;
static struct corosync_tpg_group cman_group[1] = {
        { .group          = "CMAN", .group_len      = 4},
};

struct cman_pd {
	void *conn;
	unsigned char port; /* Bound port number */
	struct list_head list;
};

struct cluster_node {
	int nodeid;
#define NODE_FLAG_PORTS_VALID 1
	int flags;
#define PORT_BITS_SIZE 32
 	unsigned char port_bits[PORT_BITS_SIZE]; /* bitmap of ports open on this node */
	struct list_head list;
};

/* An array of open 'ports' containing the connection to send
   responses to */
static void *ports[256];
static struct cluster_node our_node;
static struct corosync_api_v1 *corosync_api;
static struct list_head conn_list;
static struct list_head node_list;

#define list_iterate(v, head) \
        for (v = (head)->next; v != head; v = v->next)


/*
 * Service Interfaces required by service_message_handler struct
 */

static void cman_deliver_fn(unsigned int nodeid, struct iovec *iovec, int iov_len,
			      int endian_conversion_required);

static void cman_confchg_fn(enum totem_configuration_type configuration_type,
			    unsigned int *member_list, int member_list_entries,
			    unsigned int *left_list, int left_list_entries,
			    unsigned int *joined_list, int joined_list_entries,
			    struct memb_ring_id *ring_id);

static int cman_exec_init_fn (struct corosync_api_v1 *corosync_api);

static int cman_lib_init_fn (void *conn);

static int cman_lib_exit_fn (void *conn);

static void message_handler_req_lib_cman_is_listening (void *conn, void *msg);
static void message_handler_req_lib_cman_sendmsg (void *conn, void *msg);
static void message_handler_req_lib_cman_unbind (void *conn, void *msg);
static void message_handler_req_lib_cman_bind (void *conn, void *msg);


/*
 * Library Handler Definition
 */
static struct corosync_lib_handler cman_lib_service[] =
{
	{ /* 0 */
		.lib_handler_fn				= message_handler_req_lib_cman_sendmsg,
		.response_size				= sizeof (mar_res_header_t),
		.response_id				= MESSAGE_RES_CMAN_SENDMSG,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn				= message_handler_req_lib_cman_is_listening,
		.response_size				= sizeof (struct res_lib_cman_is_listening),
		.response_id				= MESSAGE_RES_CMAN_IS_LISTENING,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn				= message_handler_req_lib_cman_bind,
		.response_size				= sizeof (mar_res_header_t),
		.response_id				= MESSAGE_RES_CMAN_BIND,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},

	{ /* 3 */
		.lib_handler_fn				= message_handler_req_lib_cman_unbind,
		.response_size				= sizeof (mar_res_header_t),
		.response_id				= MESSAGE_RES_CMAN_UNBIND,
		.flow_control				= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	}
};

static struct corosync_service_engine cman_service_handler = {
	.name				        = "corosync cluster cman service v3.01",
	.id					= CMAN_SERVICE,
	.private_data_size			= sizeof (struct cman_pd),
	.allow_inquorate			= CS_LIB_ALLOW_INQUORATE,
	.flow_control				= COROSYNC_LIB_FLOW_CONTROL_REQUIRED,
	.lib_init_fn				= cman_lib_init_fn,
	.lib_exit_fn				= cman_lib_exit_fn,
	.lib_engine				= cman_lib_service,
	.lib_engine_count			= sizeof (cman_lib_service) / sizeof (struct corosync_lib_handler),
	.exec_init_fn				= cman_exec_init_fn,
	.exec_engine				= NULL,
	.exec_engine_count		        = 0,
};

/*
 * Dynamic loader definition
 */
static struct corosync_service_engine *cman_get_service_handler_ver0 (void);

static struct corosync_service_engine_iface_ver0 cman_service_handler_iface = {
	.corosync_get_service_engine_ver0 = cman_get_service_handler_ver0
};

static struct lcr_iface corosync_cman_ver0[1] = {
	{
		.name				= "corosync_cman",
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

static struct lcr_comp cman_comp_ver0 = {
	.iface_count			= 1,
	.ifaces			        = corosync_cman_ver0
};


static struct corosync_service_engine *cman_get_service_handler_ver0 (void)
{
	return (&cman_service_handler);
}

__attribute__ ((constructor)) static void cman_comp_register (void) {
        lcr_interfaces_set (&corosync_cman_ver0[0], &cman_service_handler_iface);

	lcr_component_register (&cman_comp_ver0);
}



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

static void set_port_bit(struct cluster_node *node, uint8_t port)
{
	int byte;
	int bit;

	byte = port/8;
	bit  = port%8;

	node->port_bits[byte] |= 1<<bit;
}

static void clear_port_bit(struct cluster_node *node, uint8_t port)
{
	int byte;
	int bit;

	byte = port/8;
	bit  = port%8;

	node->port_bits[byte] &= ~(1<<bit);
}

static int get_port_bit(struct cluster_node *node, uint8_t port)
{
	int byte;
	int bit;

	byte = port/8;
	bit  = port%8;

	return ((node->port_bits[byte] & (1<<bit)) != 0);
}

static struct cluster_node *find_node(int nodeid, int allocate)
{
	struct list_head *tmp;
	struct cluster_node *node;

	list_iterate(tmp, &node_list) {
		node = list_entry(tmp, struct cluster_node, list);
		if (node->nodeid == nodeid)
			return node;
	}
	if (allocate) {
		node = malloc(sizeof(struct cluster_node));
		if (node) {
			memset(node, 0, sizeof(*node));
			node->nodeid = nodeid;
			list_add(&node->list, &node_list);
		}
	}
	else {
		node = NULL;
	}

	return node;
}


static int cman_send_message(int fromport, int toport, int tonode, void *message, int len)
{
	struct iovec iov[2];
	struct cman_protheader header;

	header.tgtport = toport;
	header.srcport = fromport;
	header.flags   = 0;
	header.srcid   = our_node.nodeid;
	header.tgtid   = tonode;

	iov[0].iov_base = &header;
	iov[0].iov_len  = sizeof(header);
	iov[1].iov_base = message;
	iov[1].iov_len  = len;

	return corosync_api->tpg_joined_mcast(group_handle, iov, 2, TOTEM_AGREED);
}

static int cman_exec_init_fn (struct corosync_api_v1 *api)
{
//	unsigned int object_handle;
	unsigned int find_handle;

	log_printf(LOG_LEVEL_NOTICE, "cman_exec_init_fn \n");

	corosync_api = api;

	memset(ports, 0, sizeof(ports));
	memset(&our_node, 0, sizeof(our_node));
	list_init(&conn_list);
	list_init(&node_list);

	our_node.nodeid = corosync_api->totem_nodeid_get();
	list_add(&our_node.list, &node_list);
	set_port_bit(&our_node, 1);
	our_node.flags |= NODE_FLAG_PORTS_VALID;

	/* Get configuration variables */
	corosync_api->object_find_create(OBJECT_PARENT_HANDLE, "cman", strlen("cman"), &find_handle);
	// TODO ??
	corosync_api->object_find_destroy(find_handle);

	api->tpg_init(&group_handle, cman_deliver_fn, cman_confchg_fn);
	api->tpg_join(group_handle, cman_group, 1);
	return (0);
}

static int cman_lib_init_fn (void *conn)
{
	struct cman_pd *cman_pd = (struct cman_pd *)corosync_api->ipc_private_data_get (conn);

	list_add(&cman_pd->list, &conn_list);
	return 0;
}

static int cman_lib_exit_fn (void *conn)
{
	struct cman_pd *cman_pd = (struct cman_pd *)corosync_api->ipc_private_data_get (conn);

	if (cman_pd->port) {
		char portmsg[2];

		ports[cman_pd->port] = NULL;

		/* Tell the cluster */
		portmsg[0] = CLUSTER_MSG_PORTCLOSED;
		portmsg[1] = cman_pd->port;
		cman_send_message(0,0, 0, portmsg, 2);
	}

	list_del(&cman_pd->list);
	return (0);
}

static void cman_confchg_fn(enum totem_configuration_type configuration_type,
			    unsigned int *member_list, int member_list_entries,
			    unsigned int *left_list, int left_list_entries,
			    unsigned int *joined_list, int joined_list_entries,
			    struct memb_ring_id *ring_id)
{
	int i;
	struct cluster_node *node;

	/* Clear out removed nodes */
	for (i=0; i<left_list_entries; i++) {
		node = find_node(left_list[i], 0);
		if (node)
			node->flags &= ~NODE_FLAG_PORTS_VALID;
	}
}


static void cman_deliver_fn(unsigned int nodeid, struct iovec *iovec, int iov_len,
			    int endian_conversion_required)
{
	struct cman_protheader *header = iovec->iov_base;
	char *buf;

	if (endian_conversion_required) {
		header->srcid = swab32(header->srcid);
		header->tgtid = swab32(header->tgtid);
		header->flags = swab32(header->flags);
	}

	/* Messages to be sent to clients */
	if (header->tgtport != 0 &&
	    (header->tgtid == our_node.nodeid ||
	     header->tgtid == 0)) {
		buf = iovec->iov_base + sizeof(struct cman_protheader);

		if (ports[header->tgtport]) {
			corosync_api->ipc_conn_send_response(ports[header->tgtport], buf,  iovec->iov_len - sizeof(struct cman_protheader));
		}
	}

	/* Our messages. Careful here, messages for the quorum module on port 0 also
	   arrive here and must be ignored */
	if (header->tgtport == 0 &&
	    (header->tgtid == our_node.nodeid ||
	     header->tgtid == 0)) {
		struct cluster_node *node;

		buf = iovec->iov_base + sizeof(struct cman_protheader);
		node = find_node(header->tgtid, 1);

		switch (*buf) {
		case CLUSTER_MSG_PORTOPENED:
			if (node) {
				if (!(node->flags & NODE_FLAG_PORTS_VALID)) {
					char reqmsg = CLUSTER_MSG_PORTENQ;
					cman_send_message(0,0, nodeid, &reqmsg, 1);
				}
				set_port_bit(node, buf[2]);
			}
			break;
		case CLUSTER_MSG_PORTCLOSED:
			if (node) {
				if (!(node->flags & NODE_FLAG_PORTS_VALID)) {
					char reqmsg = CLUSTER_MSG_PORTENQ;
					cman_send_message(0,0, nodeid, &reqmsg, 1);
				}
				clear_port_bit(node, buf[2]);
			}
			break;
		case CLUSTER_MSG_PORTENQ:
			if (node) {
				char portresult[PORT_BITS_SIZE+1];

				portresult[0] = CLUSTER_MSG_PORTSTATUS;
				memcpy(portresult+1, our_node.port_bits, PORT_BITS_SIZE);
				cman_send_message(0,0, 0, portresult, PORT_BITS_SIZE+1);
			}
			break;
		case CLUSTER_MSG_PORTSTATUS:
			if (node && node != &our_node) {
				memcpy(node->port_bits, buf+1, PORT_BITS_SIZE);
				node->flags |= NODE_FLAG_PORTS_VALID;
			}
			break;
		}
	}
}

static void message_handler_req_lib_cman_bind (void *conn, void *msg)
{
        mar_res_header_t res;
	struct req_lib_cman_bind *req_lib_cman_bind = (struct req_lib_cman_bind *)msg;
	struct cman_pd *cman_pd = (struct cman_pd *)corosync_api->ipc_private_data_get (conn);
	int error = 0;
	char portmsg[2];

	if (req_lib_cman_bind->port  < 0 ||
	    req_lib_cman_bind->port > 255)
		error = EINVAL;

	if (cman_pd->port || ports[req_lib_cman_bind->port]) {
		error = EADDRINUSE;
	}
	if (error == CS_OK) {
		cman_pd->port = req_lib_cman_bind->port;
		ports[cman_pd->port] = corosync_api->ipc_conn_partner_get(conn);

		/* Tell the cluster */
		portmsg[0] = CLUSTER_MSG_PORTOPENED;
		portmsg[1] = cman_pd->port;
		cman_send_message(0,0, 0, portmsg, 2);
	}

	res.size = sizeof(res);
	res.id = MESSAGE_RES_CMAN_BIND;
	res.error = error;
	corosync_api->ipc_conn_send_response(conn, &res, sizeof(res));
}

static void message_handler_req_lib_cman_unbind (void *conn, void *msg)
{
	mar_res_header_t res;
	struct cman_pd *cman_pd = (struct cman_pd *)corosync_api->ipc_private_data_get (conn);
	int error = 0;
	char portmsg[2];

	if (cman_pd->port) {
		ports[cman_pd->port] = NULL;
		cman_pd->port = 0;

		/* Tell the cluster */
		portmsg[0] = CLUSTER_MSG_PORTCLOSED;
		portmsg[1] = cman_pd->port;
		cman_send_message(0,0, 0, portmsg, 2);
	}

	res.size = sizeof(res);
	res.id = MESSAGE_RES_CMAN_UNBIND;
	res.error = error;
	corosync_api->ipc_conn_send_response(conn, &res, sizeof(res));
}

static void message_handler_req_lib_cman_sendmsg (void *conn, void *msg)
{
	struct req_lib_cman_sendmsg *req_lib_cman_sendmsg = (struct req_lib_cman_sendmsg *)msg;
	mar_res_header_t res;
	struct cman_pd *cman_pd = (struct cman_pd *)corosync_api->ipc_private_data_get (conn);
	int error = CS_OK;


	if (!cman_pd->port) {
		error = EINVAL;
	}
	else {
		error = cman_send_message(cman_pd->port,
					  req_lib_cman_sendmsg->to_port,
					  req_lib_cman_sendmsg->to_node,
					  req_lib_cman_sendmsg->message,
					  req_lib_cman_sendmsg->msglen);
	}

	res.size = sizeof(res);
	res.id = MESSAGE_RES_CMAN_SENDMSG;
	res.error = error;
	corosync_api->ipc_conn_send_response(conn, &res, sizeof(res));
}

static void message_handler_req_lib_cman_is_listening (void *conn, void *msg)
{
	struct req_lib_cman_is_listening *req_lib_cman_is_listening = (struct req_lib_cman_is_listening *)msg;
	struct res_lib_cman_is_listening res_lib_cman_is_listening;;
	int error = CS_OK;
	struct cluster_node *node;

// How I think this should work:
//	There's a flag on the node that says whether we have complete port info or not.
//	If we do, then we can just return it.
//	If not then we do a port_enquire to get it (and return EBUSY).
//	If we get a PORTOPEN or PORTCLOSE and we don't have complete info then request it,
//	otherwise just update the record.
//      Remember - this needs to be backwards compatible

	node = find_node(req_lib_cman_is_listening->nodeid, 0);
	if (!node)
		error = ENOENT;
	if (node && !(node->flags & NODE_FLAG_PORTS_VALID))
		error = EBUSY;

	if (error == EBUSY) {
		char reqmsg = CLUSTER_MSG_PORTENQ;
		cman_send_message(0,0, req_lib_cman_is_listening->nodeid, &reqmsg, 1);
	}
	else {
		res_lib_cman_is_listening.status = get_port_bit(node, req_lib_cman_is_listening->port);
		error = 0;
	}

	res_lib_cman_is_listening.header.size = sizeof(res_lib_cman_is_listening);
	res_lib_cman_is_listening.header.id = MESSAGE_RES_CMAN_SENDMSG;
	res_lib_cman_is_listening.header.error = error;
	corosync_api->ipc_conn_send_response(conn, &res_lib_cman_is_listening, sizeof(res_lib_cman_is_listening));
}

