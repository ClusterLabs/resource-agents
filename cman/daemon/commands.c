/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-5 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/errno.h>

#include "list.h"
#include "cnxman-socket.h"
#include "cnxman-private.h"
#include "cnxman.h"
#include "daemon.h"
#include "barrier.h"
#include "membership.h"
#include "logging.h"
#include "config.h"

/* Cluster configuration version that must be the same among members. */
unsigned int config_version;

/* Array of "ports" allocated. This is just a list of pointers to the sock that
 * has this port bound. Speed is a major issue here so 1-2K of allocated
 * storage is worth sacrificing. Port 0 is reserved for protocol messages */
static struct connection *port_array[256];
static pthread_mutex_t port_array_lock;

/* List of outstanding ISLISTENING requests */
static struct list listenreq_list;
static pthread_mutex_t listenreq_lock;

extern node_state_t  node_state;

/* Send a listen request to a node */
static void send_listen_request(int tag, int nodeid, unsigned char port)
{
	struct cl_listenmsg listenmsg;
	struct sockaddr_cl caddr;

	memset(&caddr, 0, sizeof (caddr));

	/* Build the header */
	listenmsg.cmd = CLUSTER_CMD_LISTENREQ;
	listenmsg.target_port = port;
	listenmsg.listening = 0;
	listenmsg.tag = tag;

	caddr.scl_port = 0;
	caddr.scl_nodeid = nodeid;

	send_or_queue_message(&listenmsg, sizeof(listenmsg), &caddr, MSG_REPLYEXP);
	return;
}

/* Return 1 or 0 to indicate if we have a listener on the requested port */
static void send_listen_response(int nodeid,
				 unsigned char port, unsigned short tag)
{
	struct cl_listenmsg listenmsg;
	struct sockaddr_cl caddr;
	int status;

	memset(&caddr, 0, sizeof (caddr));

	/* Build the message */
	listenmsg.cmd = CLUSTER_CMD_LISTENRESP;
	listenmsg.target_port = port;
	listenmsg.tag = tag;
	listenmsg.listening = (port_array[port] != 0) ? 1 : 0;

	caddr.scl_port = 0;
	caddr.scl_nodeid = nodeid;

	status = send_or_queue_message(&listenmsg,
				       sizeof (listenmsg),
				       &caddr, 0);

	return;
}


/* Copy internal node format to userland format */
static void copy_to_usernode(struct cluster_node *node,
			     struct cl_cluster_node *unode)
{
	int i;
	struct cluster_node_addr *current_addr;
	struct cluster_node_addr *node_addr;

	strcpy(unode->name, node->name);
	unode->jointime = node->join_time;
	unode->size = sizeof (struct cl_cluster_node);
	unode->votes = node->votes;
	unode->state = node->state;
	unode->us = node->us;
	unode->node_id = node->node_id;
	unode->leave_reason = node->leave_reason;
	unode->incarnation = node->incarnation;

	/* Get the address that maps to our current interface */
	i=0; /* i/f numbers start at 1 */
	list_iterate_items(node_addr, &node->addr_list) {
	        if (current_interface_num() == ++i) {
		        current_addr = node_addr;
			break;
		}
	}

	/* If that failed then just use the first one */
	if (!current_addr)
 	        current_addr = (struct cluster_node_addr *)node->addr_list.n;

	memcpy(unode->addr, current_addr->addr, sizeof(struct sockaddr_storage));
}


/* command processing functions */

static int do_cmd_set_version(char *cmdbuf, int *retlen)
{
	struct cl_version *version = (struct cl_version *)cmdbuf;

	if (!we_are_a_cluster_member)
		return -ENOENT;

	if (version->major != CNXMAN_MAJOR_VERSION ||
	    version->minor != CNXMAN_MINOR_VERSION ||
	    version->patch != CNXMAN_PATCH_VERSION)
		return -EINVAL;

	if (config_version == version->config)
		return 0;

	config_version = version->config;
	send_reconfigure(RECONFIG_PARAM_CONFIG_VERSION, config_version);
	return 0;
}

static int do_cmd_get_extrainfo(char *cmdbuf, char **retbuf, int retsize, int *retlen, int offset)
{
	char *outbuf = *retbuf + offset;
	struct cl_extra_info *einfo = (struct cl_extra_info *)outbuf;
	int total_votes = 0;
	int max_expected = 0;
	struct cluster_node *node;
	char *ptr;

	pthread_mutex_lock(&cluster_members_lock);
	list_iterate_items(node, &cluster_members_list) {
		if (node->state == NODESTATE_MEMBER) {
			total_votes += node->votes;
			max_expected = max(max_expected, node->expected_votes);

		}
	}
	pthread_mutex_unlock(&cluster_members_lock);
	if (quorum_device && quorum_device->state == NODESTATE_MEMBER)
		total_votes += quorum_device->votes;

        /* Enough room for addresses ? */
	if (retsize < (sizeof(struct cl_extra_info) +
		       sizeof(struct sockaddr_storage) * num_interfaces)) {

		*retbuf = malloc(sizeof(struct cl_extra_info) + sizeof(struct sockaddr_storage) * num_interfaces);
		outbuf = *retbuf + offset;
		einfo = (struct cl_extra_info *)outbuf;

		P_COMMS("get_extrainfo: allocated new buffer\n");
	}

	einfo->node_state = node_state;
	if (master_node)
		einfo->master_node = master_node->node_id;
	else
		einfo->master_node = 0;
	einfo->node_votes = us->votes;
	einfo->total_votes = total_votes;
	einfo->expected_votes = max_expected;
	einfo->quorum = get_quorum();
	einfo->members = cluster_members;
	einfo->num_addresses = num_interfaces;

	ptr = einfo->addresses;
	ptr = get_interface_addresses(ptr);

	*retlen = ptr - outbuf;
	return 0;
}

static int do_cmd_get_all_members(char *cmdbuf, char **retbuf, int retsize, int *retlen, int offset)
{
	struct cluster_node *node;
	struct cl_cluster_node *user_node;
	struct list *nodelist;
	char *outbuf = *retbuf + offset;
	int num_nodes = 0;
	int i;
	int total_nodes = 0;
	int highest_node;

	if (!we_are_a_cluster_member)
		return -ENOENT;

	highest_node = get_highest_nodeid();

	/* Count nodes */
	pthread_mutex_lock(&cluster_members_lock);
	list_iterate(nodelist, &cluster_members_list) {
		total_nodes++;
	}
	pthread_mutex_unlock(&cluster_members_lock);
	if (quorum_device)
		total_nodes++;

	/* If there is not enough space in the default buffer, allocate some more. */
	if ((retsize / sizeof(struct cl_cluster_node)) < total_nodes) {
		*retbuf = malloc(sizeof(struct cl_cluster_node) * total_nodes + offset);
		outbuf = *retbuf + offset;
		P_COMMS("get_all_members: allocated new buffer\n");
	}

	user_node = (struct cl_cluster_node *)outbuf;

	for (i=1; i <= highest_node; i++) {
		node = find_node_by_nodeid(i);
		if (node) {
			copy_to_usernode(node, user_node);

			user_node++;
			num_nodes++;
		}
	}
	if (quorum_device) {
		copy_to_usernode(quorum_device, user_node);
		user_node++;
		num_nodes++;
	}

	*retlen = sizeof(struct cl_cluster_node) * num_nodes;
	P_COMMS("get_all_members: retlen = %d\n", *retlen);
	return num_nodes;
}


static int do_cmd_get_cluster(char *cmdbuf, char *retbuf, int *retlen)
{
	struct cl_cluster_info *info = (struct cl_cluster_info *)retbuf;

	info->number = cluster_id;
	info->generation = cluster_generation;
	memcpy(&info->name, cluster_name, strlen(cluster_name)+1);
	*retlen = sizeof(struct cl_cluster_info);

	return 0;
}

static int do_cmd_get_node(char *cmdbuf, char *retbuf, int *retlen)
{
	struct cluster_node *node;
	struct cl_cluster_node *u_node = (struct cl_cluster_node *)cmdbuf;
	struct cl_cluster_node *r_node = (struct cl_cluster_node *)retbuf;

	if (!we_are_a_cluster_member)
		return -ENOENT;

	if (!u_node->name[0]) {
		if (u_node->node_id == 0)
			u_node->node_id = us->node_id;
		node = find_node_by_nodeid(u_node->node_id);
	}
	else
		node = find_node_by_name(u_node->name);

	if (!node)
		return -ENOENT;

	copy_to_usernode(node, r_node);
	*retlen = sizeof(struct cl_cluster_node);

	return 0;
}

static int do_cmd_set_expected(char *cmdbuf, int *retlen)
{
	struct list *nodelist;
	struct cluster_node *node;
	unsigned int total_votes;
	unsigned int newquorum;
	unsigned int newexp;

	if (!we_are_a_cluster_member)
		return -ENOENT;
	memcpy(&newexp, cmdbuf, sizeof(int));
	newquorum = calculate_quorum(1, newexp, &total_votes);

	if (newquorum < total_votes / 2
	    || newquorum > total_votes) {
		return -EINVAL;
	}

	/* Now do it */
	pthread_mutex_lock(&cluster_members_lock);
	list_iterate(nodelist, &cluster_members_list) {
		node = list_item(nodelist, struct cluster_node);
		if (node->state == NODESTATE_MEMBER
		    && node->expected_votes > newexp) {
			node->expected_votes = newexp;
		}
	}
	pthread_mutex_unlock(&cluster_members_lock);

	recalculate_quorum(1);

	send_reconfigure(RECONFIG_PARAM_EXPECTED_VOTES, newexp);

	return 0;
}

static int do_cmd_kill_node(char *cmdbuf, int *retlen)
{
	struct cluster_node *node;
	int nodeid;

	if (!we_are_a_cluster_member)
		return -ENOENT;

	memcpy(&nodeid, cmdbuf, sizeof(int));

	if ((node = find_node_by_nodeid(nodeid)) == NULL)
		return -EINVAL;

	/* Can't kill us */
	if (node->us)
		return -EINVAL;

	if (node->state != NODESTATE_MEMBER)
		return -EINVAL;

	/* Just in case it is alive, send a KILL message */
	send_kill(nodeid, 1);

	node->leave_reason = CLUSTER_LEAVEFLAG_KILLED;
	a_node_just_died(node, 1);

	return 0;
}


static int do_cmd_islistening(struct connection *con, char *cmdbuf, int *retlen)
{
	struct cl_listen_request rq;
	struct cluster_node *rem_node;
	int nodeid;
	struct cl_waiting_listen_request *listen_request;

	if (!we_are_a_cluster_member)
		return -ENOENT;

	memcpy(&rq, cmdbuf, sizeof (rq));

	nodeid = rq.nodeid;
	if (!nodeid)
		nodeid = us->node_id;

	rem_node = find_node_by_nodeid(nodeid);

	/* Node not in the cluster */
	if (!rem_node)
		return -ENOENT;

	if (rem_node->state != NODESTATE_MEMBER)
		return -ENOTCONN;

	/* If the request is for us then just look in the ports
	 * array */
	if (rem_node->us)
		return (port_array[rq.port] != 0) ? 1 : 0;

	/* For a remote node we need to send a request out */
	listen_request = malloc(sizeof (struct cl_waiting_listen_request));
	if (!listen_request)
		return -ENOMEM;

	/* Build the request */
	listen_request->waiting = 1;
	listen_request->result = 0;
	listen_request->tag = con->fd;
	listen_request->nodeid = nodeid;
	listen_request->connection = con;

	pthread_mutex_lock(&listenreq_lock);
	list_add(&listenreq_list, &listen_request->list);
	pthread_mutex_unlock(&listenreq_lock);

	/* Now wait for the response to come back */
	send_listen_request(con->fd, rq.nodeid, rq.port);

	/* We don't actually return anything to the user until
	   the reply comes back */
	return -EWOULDBLOCK;
}

static int do_cmd_set_votes(char *cmdbuf, int *retlen)
{
	unsigned int total_votes;
	unsigned int newquorum;
	int saved_votes;
	int arg;

	if (!we_are_a_cluster_member)
		return -ENOENT;

	memcpy(&arg, cmdbuf, sizeof(int));

	/* Check votes is valid */
	saved_votes = us->votes;
	us->votes = arg;

	newquorum = calculate_quorum(1, 0, &total_votes);

	if (newquorum < total_votes / 2 || newquorum > total_votes) {
		us->votes = saved_votes;
		return -EINVAL;
	}

	recalculate_quorum(1);

	send_reconfigure(RECONFIG_PARAM_NODE_VOTES, arg);

	return 0;
}

static int do_cmd_set_nodename(char *cmdbuf, int *retlen)
{
	if (cnxman_running)
		return -EALREADY;

	strncpy(nodename, cmdbuf, MAX_CLUSTER_MEMBER_NAME_LEN);
	return 0;
}

static int do_cmd_set_nodeid(char *cmdbuf, int *retlen)
{
	int nodeid;

	memcpy(&nodeid, cmdbuf, sizeof(int));

	if (cnxman_running)
		return -EALREADY;

	if (nodeid < 0 || nodeid > 4096)
		return -EINVAL;

	wanted_nodeid = nodeid;
	return 0;
}


static int do_cmd_bind(struct connection *con, char *cmdbuf)
{
	int port;
	int ret = -EADDRINUSE;

	memcpy(&port, cmdbuf, sizeof(int));

	/* TODO: the kernel version caused a wait here. I don't
	   think we really need it though */
	if (port > HIGH_PROTECTED_PORT &&
	    (!cluster_is_quorate || in_transition())) {
	}

	pthread_mutex_lock(&port_array_lock);
	if (port_array[port])
		goto out;

	ret = 0;
	port_array[port] = con;
	con->port = port;

	pthread_mutex_unlock(&port_array_lock);

 out:
	return ret;
}


static uint16_t generate_cluster_id(char *name)
{
	int i;
	int value = 0;

	for (i=0; i<strlen(name); i++) {
		value <<= 1;
		value += name[i];
	}
	P_COMMS("Generated cluster id for '%s' is %d\n", name, value & 0xFFFF);
	return value & 0xFFFF;
}

static int do_cmd_join_cluster(char *cmdbuf, int *retlen)
{
	struct cl_join_cluster_info *join_info = (struct cl_join_cluster_info *)cmdbuf;
	struct utsname un;

	if (cnxman_running)
		return -EALREADY;

	if (strlen(join_info->cluster_name) > MAX_CLUSTER_NAME_LEN)
		return -EINVAL;

	if (!num_interfaces)
		return -ENOTCONN;

	set_votes(join_info->votes, join_info->expected_votes);
	cluster_id = generate_cluster_id(join_info->cluster_name);
	strncpy(cluster_name, join_info->cluster_name, MAX_CLUSTER_NAME_LEN);
	two_node = join_info->two_node;
	config_version = join_info->config_version;

	quit_threads = 0;
	cnxman_running = 1;

	/* Make sure we have a node name */
	if (nodename[0] == '\0') {
		uname(&un);
		strcpy(nodename, un.nodename);
	}

	if (start_membership_services()) {
		return -ENOMEM;
	}

	return 0;
}

static int do_cmd_leave_cluster(char *cmdbuf, int *retlen)
{
	int leave_flags;

	if (!cnxman_running)
		return -ENOTCONN;

	if (!we_are_a_cluster_member)
		return -ENOENT;

	if (in_transition())
		return -EBUSY;

	memcpy(&leave_flags, cmdbuf, sizeof(int));

	/* Ignore the use count if FORCE is set */
	if (!(leave_flags & CLUSTER_LEAVEFLAG_FORCE)) {
		if (use_count)
			return -ENOTCONN;
	}

	us->leave_reason = leave_flags;
	quit_threads = 1;

	stop_membership_thread();
	use_count = 0;
	return 0;
}

static int do_cmd_register_quorum_device(char *cmdbuf, int *retlen)
{
	int votes;
	char *name = cmdbuf+sizeof(int);

	if (!cnxman_running)
		return -ENOTCONN;

	if (!we_are_a_cluster_member)
		return -ENOENT;

	if (quorum_device)
                return -EBUSY;

	if (strlen(name) > MAX_CLUSTER_MEMBER_NAME_LEN)
		return -EINVAL;

	if (find_node_by_name(name))
                return -EALREADY;

	memcpy(&votes, cmdbuf, sizeof(int));

	quorum_device = malloc(sizeof (struct cluster_node));
        if (!quorum_device)
                return -ENOMEM;
        memset(quorum_device, 0, sizeof (struct cluster_node));

        quorum_device->name = malloc(strlen(name) + 1);
        if (!quorum_device->name) {
                free(quorum_device);
                quorum_device = NULL;
                return -ENOMEM;
        }

        strcpy(quorum_device->name, name);
        quorum_device->votes = votes;
        quorum_device->state = NODESTATE_DEAD;
	gettimeofday(&quorum_device->join_time, NULL);

        /* Keep this list valid so it doesn't confuse other code */
        list_init(&quorum_device->addr_list);

        return 0;
}

static int do_cmd_unregister_quorum_device(char *cmdbuf, int *retlen)
{
        if (!quorum_device)
                return -EINVAL;

        if (quorum_device->state == NODESTATE_MEMBER)
                return -EBUSY;

	free(quorum_device->name);
	free(quorum_device);

        quorum_device = NULL;

        return 0;
}

static int do_cmd_poll_quorum_device(char *cmdbuf, int *retlen)
{
	int yesno;

        if (!quorum_device)
                return -EINVAL;

	memcpy(&yesno, cmdbuf, sizeof(int));

        if (yesno) {
                quorum_device->last_hello = gettime();
                if (quorum_device->state == NODESTATE_DEAD) {
                        quorum_device->state = NODESTATE_MEMBER;
                        recalculate_quorum(0);
                }
        }
        else {
                if (quorum_device->state == NODESTATE_MEMBER) {
                        quorum_device->state = NODESTATE_DEAD;
                        recalculate_quorum(0);
                }
        }
	return 0;
}

int process_command(struct connection *con, int cmd, char *cmdbuf,
		    char **retbuf, int *retlen, int retsize, int offset)
{
	int err = -EINVAL;
	struct cl_version cnxman_version;
	char *outbuf = *retbuf;

	P_COMMS("command to process is %x\n", cmd);

	switch (cmd) {

		/* Return the cnxman version number */
	case CMAN_CMD_GET_VERSION:
		err = 0;
		cnxman_version.major = CNXMAN_MAJOR_VERSION;
		cnxman_version.minor = CNXMAN_MINOR_VERSION;
		cnxman_version.patch = CNXMAN_PATCH_VERSION;
		cnxman_version.config = config_version;
		memcpy(outbuf+offset, &cnxman_version, sizeof (struct cl_version));
		*retlen = sizeof(struct cl_version);
		break;

		/* Set the cnxman config version number */
	case CMAN_CMD_SET_VERSION:
		err = do_cmd_set_version(cmdbuf, retlen);
		break;

		/* Bind to a "port" */
	case CMAN_CMD_BIND:
		err = do_cmd_bind(con, cmdbuf);
		break;

		/* Return the full membership list including dead nodes */
	case CMAN_CMD_GETALLMEMBERS:
		err = do_cmd_get_all_members(cmdbuf, retbuf, retsize, retlen, offset);
		break;

	case CMAN_CMD_GETNODE:
		err = do_cmd_get_node(cmdbuf, outbuf+offset, retlen);
		break;

	case CMAN_CMD_GETCLUSTER:
		err = do_cmd_get_cluster(cmdbuf, outbuf+offset, retlen);
		break;

	case CMAN_CMD_GETEXTRAINFO:
		err = do_cmd_get_extrainfo(cmdbuf, retbuf, retsize, retlen, offset);
		break;

	case CMAN_CMD_ISQUORATE:
		return cluster_is_quorate;

	case CMAN_CMD_ISACTIVE:
		return cnxman_running;

	case CMAN_CMD_SETEXPECTED_VOTES:
		err = do_cmd_set_expected(cmdbuf, retlen);
		break;

		/* Change the number of votes for this node */
	case CMAN_CMD_SET_VOTES:
		err = do_cmd_set_votes(cmdbuf, retlen);
		break;

		/* Return 1 if the specified node is listening on a given port */
	case CMAN_CMD_ISLISTENING:
		err = do_cmd_islistening(con, cmdbuf, retlen);
		break;

		/* Forcibly kill a node */
	case CMAN_CMD_KILLNODE:
		err = do_cmd_kill_node(cmdbuf, retlen);
		break;

	case CMAN_CMD_BARRIER:
		err = do_cmd_barrier(con, cmdbuf, retlen);
		break;

	case CMAN_CMD_SET_NODENAME:
		err = do_cmd_set_nodename(cmdbuf, retlen);
		break;

	case CMAN_CMD_SET_NODEID:
		err = do_cmd_set_nodeid(cmdbuf, retlen);
		break;

	case CMAN_CMD_JOIN_CLUSTER:
		err = do_cmd_join_cluster(cmdbuf, retlen);
		break;

	case CMAN_CMD_LEAVE_CLUSTER:
		err = do_cmd_leave_cluster(cmdbuf, retlen);
		break;

	case CMAN_CMD_GET_JOINCOUNT:
		err = num_connections;
		break;

	case CMAN_CMD_REG_QUORUMDEV:
		err = do_cmd_register_quorum_device(cmdbuf, retlen);
		break;

	case CMAN_CMD_UNREG_QUORUMDEV:
		err = do_cmd_unregister_quorum_device(cmdbuf, retlen);
		break;

	case CMAN_CMD_POLL_QUORUMDEV:
		err = do_cmd_poll_quorum_device(cmdbuf, retlen);
		break;
	}
	P_COMMS("command return code is %d\n", err);
	return err;
}


/* Returns 1 if it was passed to a listening user, 0 otherwise */
int send_to_user_port(struct cl_protheader *header,
		      struct msghdr *msg,
		      char *recv_buf, int len)
{
	int ret = 0;

        /* Get the port number and look for a listener */
	pthread_mutex_lock(&port_array_lock);
	if (port_array[header->tgtport]) {
		struct connection *c = port_array[header->tgtport];

		send_data_reply(c, le32_to_cpu(header->srcid), header->srcport,
				recv_buf, len);

		pthread_mutex_unlock(&port_array_lock);
		ret = 1;
	}
	else {
		/* Nobody listening, drop it */
		pthread_mutex_unlock(&port_array_lock);
		ret = 0;
	}
	return ret;
}

/* Send a port closedown message to all cluster nodes - this tells them that a
 * port listener has gone away */
static void send_port_close_msg(unsigned char port)
{
	struct cl_closemsg closemsg;
	struct sockaddr_cl caddr;

	caddr.scl_port = 0;
	caddr.scl_nodeid = 0;

	/* Build the header */
	closemsg.cmd = CLUSTER_CMD_PORTCLOSED;
	closemsg.port = port;

	send_or_queue_message(&closemsg, sizeof (closemsg), &caddr, 0);
	return;
}

void unbind_con(struct connection *con)
{
	pthread_mutex_lock(&port_array_lock);
	if (con->port) {
		port_array[con->port] = NULL;
		send_port_close_msg(con->port);
	}
	pthread_mutex_unlock(&port_array_lock);

	con->port = 0;
}

static struct cl_waiting_listen_request *find_listen_request(unsigned short tag)
{
	struct list *llist;
	struct cl_waiting_listen_request *listener;

	list_iterate(llist, &listenreq_list) {
		listener = list_item(llist, struct cl_waiting_listen_request);
		if (listener->tag == tag) {
			return listener;
		}
	}
	return NULL;
}


/* A remote port has been closed - post an OOB message to the local listen on
 * that port (if there is one) */
static void post_close_event(unsigned char port, int nodeid)
{
	struct connection *con;

	pthread_mutex_lock(&port_array_lock);
	con = port_array[port];
	pthread_mutex_unlock(&port_array_lock);

	if (con)
		notify_listeners(con, EVENT_REASON_PORTCLOSED, nodeid);
}

/* Return 1 if caller needs to ACK this message */
int process_cnxman_message(char *data,
			   int len, char *addr, int addrlen,
			   struct cluster_node *rem_node)
{
	struct cl_protmsg *msg = (struct cl_protmsg *) data;
	struct cl_protheader *header = (struct cl_protheader *) data;
	struct cl_ackmsg *ackmsg;
	struct cl_listenmsg *listenmsg;
	struct cl_closemsg *closemsg;
	struct cl_barriermsg *barriermsg;
	struct cl_waiting_listen_request *listen_request;
	int ret = 0;

	P_COMMS("Message on port 0 is %d\n", msg->cmd);
	switch (msg->cmd) {
	case CLUSTER_CMD_ACK:
		ackmsg = (struct cl_ackmsg *) data;

		if (rem_node && (ackmsg->aflags & 1)) {
			log_msg(LOG_WARNING, "WARNING no listener for port %d on node %s\n",
				ackmsg->remport, rem_node->name);
		}
		/* ACK processing has already happened */
		break;

		/* Return 1 if we have a listener on this port, 0 if not */
	case CLUSTER_CMD_LISTENREQ:
		listenmsg =
		    (struct cl_listenmsg *) (data +
					     sizeof (struct cl_protheader));
		send_listen_response(le32_to_cpu(header->srcid),
				     listenmsg->target_port, listenmsg->tag);
		ret = 1;
		break;

	case CLUSTER_CMD_LISTENRESP:
		/* Wake up process waiting for listen response */
		listenmsg =
		    (struct cl_listenmsg *) (data +
					     sizeof (struct cl_protheader));
		pthread_mutex_lock(&listenreq_lock);
		listen_request = find_listen_request(listenmsg->tag);
		if (listen_request) {
			send_status_return(listen_request->connection, CMAN_CMD_ISLISTENING, listenmsg->listening);
			list_del(&listen_request->list);
			free(listen_request);
		}
		pthread_mutex_unlock(&listenreq_lock);
		ret = 1;
		break;

	case CLUSTER_CMD_PORTCLOSED:
		closemsg =
		    (struct cl_closemsg *) (data +
					    sizeof (struct cl_protheader));
		post_close_event(closemsg->port, le32_to_cpu(header->srcid));
		ret = 1;

		break;

	case CLUSTER_CMD_BARRIER:
		barriermsg =
		    (struct cl_barriermsg *) (data +
					      sizeof (struct cl_protheader));
		if (rem_node)
			process_barrier_msg(barriermsg, rem_node);
		ret = 1;
		break;

	default:
		log_msg(LOG_WARNING, "Unknown protocol message %d received\n", msg->cmd);
		break;

	}
	return ret;
}

void clean_dead_listeners()
{
	struct list *llist;

	/* check for outstanding listen requests for dead nodes and
	 * cancel them */
	pthread_mutex_lock(&listenreq_lock);
	list_iterate(llist, &listenreq_list) {
		struct cl_waiting_listen_request *lrequest =
		    list_item(llist, struct cl_waiting_listen_request);
		struct cluster_node *node =
		    find_node_by_nodeid(lrequest->nodeid);

		if (node && node->state != NODESTATE_MEMBER) {
			send_status_return(lrequest->connection, CMAN_CMD_ISLISTENING, -ENOTCONN);
		}
	}
	pthread_mutex_unlock(&listenreq_lock);
}

void commands_init()
{
	pthread_mutex_init(&port_array_lock, NULL);
	pthread_mutex_init(&listenreq_lock, NULL);

	list_init(&listenreq_list);
}
