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
#include "daemon.h"
#include "barrier.h"
#include "logging.h"
#include "config.h"
#include "ais.h"
#include "aispoll.h"

#define max(a,b) (((a) > (b)) ? (a) : (b))


/* Cluster configuration version that must be the same among members. */
static unsigned int config_version;

/* Reference counting for cluster applications */
static int use_count;

/* Array of "ports" allocated. This is just a list of pointers to the connection that
 * has this port bound. Port 0 is reserved for protocol messages */
static struct connection *port_array[256];

// Stuff that was more global
static LIST_INIT(cluster_members_list);
       int cluster_members;
       int we_are_a_cluster_member;
static struct cluster_node *us;
static int quorum;
static int two_node;
static int cluster_is_quorate;
       char cluster_name[MAX_CLUSTER_NAME_LEN+1];
static char nodename[MAX_CLUSTER_MEMBER_NAME_LEN+1];
static int wanted_nodeid;
static int expected_votes;
extern int num_interfaces;
static struct cluster_node *quorum_device;
static uint16_t cluster_id;
static int ais_running;

static struct cluster_node *find_node_by_nodeid(int nodeid);
static struct cluster_node *find_node_by_name(char *name);
static struct cluster_node *find_node_by_ais_nodeid(unsigned int ais_nodeid);
static struct cluster_node *get_lowest_node();
static int get_node_count(void);
static int get_highest_nodeid(void);
static int send_port_open_msg(unsigned char port);
static void process_internal_message(char *data, int len, int nodeid, uint32_t ais_nodeid);
static struct cluster_node *find_joining(void);
static void recalculate_quorum(int allow_decrease);

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

/* If "cluster_is_quorate" is 0 then all activity apart from protected ports is
 * blocked. */
static void set_quorate(int total_votes)
{
	int quorate;

	if (quorum > total_votes) {
		quorate = 0;
	}
	else {
		quorate = 1;
	}

	if (cluster_is_quorate && !quorate)
		log_msg(LOG_INFO, "quorum lost, blocking activity\n");
	if (!cluster_is_quorate && quorate)
		log_msg(LOG_INFO, "quorum regained, resuming activity\n");

	cluster_is_quorate = quorate;

}

static struct cluster_node *add_new_node(char *name, int nodeid, int votes, int expected_votes,
					 nodestate_t state, uint32_t ais_nodeid)
{
	struct cluster_node *newnode;
	int newalloc = 0;

	/* Look for AIS node entry. there should be one */
	newnode = find_node_by_ais_nodeid(ais_nodeid);
	if (!newnode) {
		newnode = malloc(sizeof(struct cluster_node));
		if (!newnode) {
			// TODO what ??
			return NULL;
		}
		memset(newnode, 0, sizeof(struct cluster_node));
		newalloc = 1;
		newnode->state = NODESTATE_DEAD;
	}
	if (!newnode->name) {
		newnode->name = malloc(strlen(name)+1);
		if (!newnode->name) {
			if (newalloc)
				free(newnode);
			return NULL;
		}
		strcpy(newnode->name, name);
	}

	newnode->state = state;
	newnode->us = 0;
	if (!newnode->node_id) /* Don't clobber existing nodeid */
		newnode->node_id = nodeid;
	newnode->votes = votes;
	newnode->expected_votes = expected_votes;
	newnode->ais_nodeid = ais_nodeid;
	newnode->incarnation = incarnation;
	memset(newnode->port_bits, 0, sizeof(us->port_bits));
	set_port_bit(newnode, 0);
	if (newalloc)
		list_add(&cluster_members_list, &newnode->list);

	P_MEMB("add_new_node: %s, (id=%d, votes=%d) newalloc=%d\n",
	       name, nodeid, votes, newalloc);

	if (newnode->state == NODESTATE_MEMBER)
		cluster_members++;

	return newnode;
}

static void send_reconfigure(int nodeid, int param, int value)
{
	struct cl_reconfig_msg msg;

	msg.cmd = CLUSTER_MSG_RECONFIGURE;
	msg.param = param;
	msg.nodeid = nodeid;
	msg.value = value;

	comms_send_message((char *)&msg, sizeof(msg),
			   0,0,
			   0,  /* multicast */
			   0); /* flags */
}

static void send_joinconf(int nodeid, int real_nodeid)
{
	char buf[sizeof(struct cl_joinconf_head) +
		 sizeof(struct cl_joinconf_node) * get_node_count()];
	struct cl_joinconf_head *head = (struct cl_joinconf_head *)buf;
	struct cl_joinconf_node *node = (struct cl_joinconf_node *)(buf+sizeof(struct cl_joinconf_head));
	struct cluster_node *clnode;
	int len = sizeof(struct cl_joinconf_head);

	P_MEMB("Sending JOINCONF base = %p, real_nodeid = %d\n", buf, real_nodeid);
	head->cmd = CLUSTER_MSG_JOINCONF;
	head->nodeid = real_nodeid;

	list_iterate_items(clnode, &cluster_members_list) {
		if (clnode->node_id) {
			node->nodeid = clnode->node_id;
			node->expected_votes = clnode->expected_votes;
			node->votes = clnode->votes;
			node->state = clnode->state;
			node->ais_nodeid = clnode->ais_nodeid;
			memcpy(node->port_bits, clnode->port_bits, sizeof(clnode->port_bits));
			strcpy(node->name, clnode->name);
		}
		node++;
		len += sizeof(struct cl_joinconf_node);
	}
	comms_send_message(buf, len,
			   0,0,
			   nodeid,
			   0); /* flags */
}

static void do_process_joinconf(int nodeid, char *buf, int len)
{
	struct cl_joinconf_head *head = (struct cl_joinconf_head *)buf;
	struct cl_joinconf_node *node = (struct cl_joinconf_node *)(buf + sizeof(struct cl_joinconf_head));

	P_MEMB("Got JOINCONF, our state = %d, buf=%p, len=%d\n", us->state, buf, len);
	/* joinconf for new node - unpack nodelist */
	if (us->state == NODESTATE_JOINING) {

		P_MEMB("Joinconf says our nodeid is %d\n", head->nodeid);
		/* We must get the node ID we wanted */
		assert(wanted_nodeid == 0 ||
		       wanted_nodeid == head->nodeid);

		us->node_id = head->nodeid;
		while ((char *)node < buf+len) {
			struct cluster_node *newnode;

			newnode = add_new_node(node->name, node->nodeid, node->votes, node->expected_votes,
					       node->state, node->ais_nodeid);
			memcpy(newnode->port_bits, node->port_bits, sizeof(newnode->port_bits));
			node++;
		}

		us->state = NODESTATE_MEMBER;
		cluster_members++;
		we_are_a_cluster_member = 1;
		P_MEMB("We are now a cluster member\n");
	}
	else {
		/* Someone else's joinconf, but we need to know the nodeid */
		struct cluster_node *node = find_joining();

		P_MEMB("process_joinconf: new node's ID = %d, node=%p\n", head->nodeid, node);
		if (head->nodeid && node) {
			node->node_id = head->nodeid;
			node->state = NODESTATE_MEMBER;
			cluster_members++;
			recalculate_quorum(0);
		}
	}
}

static int calculate_quorum(int allow_decrease, int max_expected, unsigned int *ret_total_votes)
{
	struct list *nodelist;
	struct cluster_node *node;
	unsigned int total_votes = 0;
	unsigned int highest_expected = 0;
	unsigned int newquorum, q1, q2;

	list_iterate(nodelist, &cluster_members_list) {
		node = list_item(nodelist, struct cluster_node);

		if (node->state == NODESTATE_MEMBER) {
			highest_expected =
				max(highest_expected, node->expected_votes);
			total_votes += node->votes;
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
	 * fencing (as both nodes try to fence each other in split-brain.) */
	if (two_node)
		newquorum = 1;

	if (ret_total_votes)
		*ret_total_votes = total_votes;
	return newquorum;
}

/* Recalculate cluster quorum, set quorate and notify changes */
static void recalculate_quorum(int allow_decrease)
{
	unsigned int total_votes;

	quorum = calculate_quorum(allow_decrease, 0, &total_votes);
	set_quorate(total_votes);
	notify_listeners(NULL, EVENT_REASON_STATECHANGE, 0);
}

/* Copy internal node format to userland format */
static void copy_to_usernode(struct cluster_node *node,
			     struct cl_cluster_node *unode)
{
	struct sockaddr_in *sin;

	strcpy(unode->name, node->name);
	unode->jointime = node->join_time;
	unode->size = sizeof(struct cl_cluster_node);
	unode->votes = node->votes;
	unode->state = node->state;
	unode->us = node->us;
	unode->node_id = node->node_id;
	unode->leave_reason = node->leave_reason;
	unode->incarnation = node->incarnation;

	// TODO: IPv6 support when I do it in AIS
	sin = (struct sockaddr_in *)unode->addr;
	sin->sin_family = AF_INET;
	sin->sin_port = 0;
	sin->sin_addr.s_addr = node->ais_nodeid;
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
	send_reconfigure(us->node_id, RECONFIG_PARAM_CONFIG_VERSION, config_version);
	return 0;
}

static int do_cmd_get_extrainfo(char *cmdbuf, char **retbuf, int retsize, int *retlen, int offset)
{
	char *outbuf = *retbuf + offset;
	struct cl_extra_info *einfo = (struct cl_extra_info *)outbuf;
	int total_votes = 0;
	int max_expected = 0;
	struct cluster_node *node;
	struct sockaddr_in *sin;
	char *ptr;

	if (!we_are_a_cluster_member)
		return -ENOENT;

	list_iterate_items(node, &cluster_members_list) {
		if (node->state == NODESTATE_MEMBER) {
			total_votes += node->votes;
			max_expected = max(max_expected, node->expected_votes);
		}
	}
	if (quorum_device && quorum_device->state == NODESTATE_MEMBER)
		total_votes += quorum_device->votes;

        /* Enough room for addresses ? */
	if (retsize < (sizeof(struct cl_extra_info) +
		       sizeof(struct sockaddr_storage) * num_interfaces)) {

		*retbuf = malloc(sizeof(struct cl_extra_info) + sizeof(struct sockaddr_storage) * num_interfaces);
		outbuf = *retbuf + offset;
		einfo = (struct cl_extra_info *)outbuf;

		P_MEMB("get_extrainfo: allocated new buffer\n");
	}

	einfo->node_state = us->state;
	einfo->master_node = 0;
	einfo->node_votes = us->votes;
	einfo->total_votes = total_votes;
	einfo->expected_votes = max_expected;
	einfo->quorum = quorum;
	einfo->members = cluster_members;
	einfo->num_addresses = num_interfaces;

	ptr = einfo->addresses;
//TODO	ptr = get_interface_addresses(ptr);
	sin = (struct sockaddr_in *)ptr;
	sin->sin_family = AF_INET;
	sin->sin_port = 0;
	sin->sin_addr.s_addr = us->ais_nodeid;
	ptr += sizeof(struct sockaddr_in);

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
	list_iterate(nodelist, &cluster_members_list) {
		total_nodes++;
	}
	if (quorum_device)
		total_nodes++;

	/* if retsize == 0 then don't return node information */
	if (retsize) {
		/* If there is not enough space in the default buffer, allocate some more. */
		if ((retsize / sizeof(struct cl_cluster_node)) < total_nodes) {
			*retbuf = malloc(sizeof(struct cl_cluster_node) * total_nodes + offset);
			outbuf = *retbuf + offset;
			P_MEMB("get_all_members: allocated new buffer (retsize=%d)\n", retsize);
		}
	}
	user_node = (struct cl_cluster_node *)outbuf;

#if 1
	/* This returns nodes sorted by nodeid */
	for (i=1; i <= highest_node; i++) {
		node = find_node_by_nodeid(i);
		if (node && retsize) {
			copy_to_usernode(node, user_node);

			user_node++;
			num_nodes++;
		}
	}
#else
	/* This just returns the full list */
	list_iterate_items(node, &cluster_members_list) {
		if (retsize) {
			copy_to_usernode(node, user_node);

			user_node++;
			num_nodes++;
		}
	}
#endif
	if (quorum_device) {
		copy_to_usernode(quorum_device, user_node);
		user_node++;
		num_nodes++;
	}

	*retlen = sizeof(struct cl_cluster_node) * num_nodes;
	P_MEMB("get_all_members: retlen = %d\n", *retlen);
	return num_nodes;
}


static int do_cmd_get_cluster(char *cmdbuf, char *retbuf, int *retlen)
{
	struct cl_cluster_info *info = (struct cl_cluster_info *)retbuf;

	info->number = cluster_id;
	info->generation = incarnation;
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
	list_iterate(nodelist, &cluster_members_list) {
		node = list_item(nodelist, struct cluster_node);
		if (node->state == NODESTATE_MEMBER
		    && node->expected_votes > newexp) {
			node->expected_votes = newexp;
		}
	}

	recalculate_quorum(1);

	send_reconfigure(us->node_id, RECONFIG_PARAM_EXPECTED_VOTES, newexp);

	return 0;
}

static void send_kill(int nodeid, int wanted_nodeid)
{
	struct cl_killmsg msg;

	P_MEMB("Sending KILL to node %d, wanted %d\n", nodeid, wanted_nodeid);

	msg.cmd = CLUSTER_MSG_KILLNODE;
	msg.wanted_nodeid = wanted_nodeid;

	comms_send_message((char *)&msg, sizeof(msg),
			   0,0,
			   nodeid,
			   0); /* flags */
}

static void send_leave(uint16_t reason)
{
	struct cl_leavemsg msg;

	P_MEMB("Sending LEAVE, reason %d\n", reason);

	msg.cmd = CLUSTER_MSG_LEAVE;
	msg.reason = reason;

	comms_send_message((char *)&msg, sizeof(msg),
			   0,0,
			   0,  /* multicast */
			   0); /* flags */
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

	if (node->state != NODESTATE_MEMBER)
		return -EINVAL;

	node->leave_reason = CLUSTER_LEAVEFLAG_KILLED;
	node->state = NODESTATE_LEAVING;

	/* Send a KILL message */
	send_kill(nodeid, nodeid);

	return 0;
}


static int do_cmd_islistening(struct connection *con, char *cmdbuf, int *retlen)
{
	struct cl_listen_request rq;
	struct cluster_node *rem_node;
	int nodeid;

	if (!we_are_a_cluster_member)
		return -ENOENT;

	memcpy(&rq, cmdbuf, sizeof(rq));

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

	return get_port_bit(rem_node, rq.port);
}


static int do_cmd_set_votes(char *cmdbuf, int *retlen)
{
	unsigned int total_votes;
	unsigned int newquorum;
	int saved_votes;
	struct cl_set_votes arg;
	struct cluster_node *node;

	if (!we_are_a_cluster_member)
		return -ENOTCONN;

	memcpy(&arg, cmdbuf, sizeof(arg));

	if (!arg.nodeid)
		arg.nodeid = us->node_id;

	P_MEMB("Setting votes for node %d to %d\n", arg.nodeid, arg.newvotes);

	node = find_node_by_nodeid(arg.nodeid);
	if (!node)
		return -ENOENT;

	/* Check votes is valid */
	saved_votes = node->votes;
	node->votes = arg.newvotes;

	newquorum = calculate_quorum(1, 0, &total_votes);

	if (newquorum < total_votes / 2 || newquorum > total_votes) {
		node->votes = saved_votes;
		return -EINVAL;
	}

	recalculate_quorum(1);

	send_reconfigure(arg.nodeid, RECONFIG_PARAM_NODE_VOTES, arg.newvotes);

	return 0;
}

static int do_cmd_set_nodename(char *cmdbuf, int *retlen)
{
	if (ais_running)
		return -EALREADY;

	strncpy(nodename, cmdbuf, MAX_CLUSTER_MEMBER_NAME_LEN);
	return 0;
}

static int do_cmd_set_nodeid(char *cmdbuf, int *retlen)
{
	int nodeid;

	memcpy(&nodeid, cmdbuf, sizeof(int));

	if (ais_running)
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
	if (port > HIGH_PROTECTED_PORT && (!cluster_is_quorate)) {
	}

	if (port_array[port])
		goto out;

	ret = 0;
	port_array[port] = con;
	con->port = port;


	set_port_bit(us, con->port);
	send_port_open_msg(con->port);
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
	P_MEMB("Generated cluster id for '%s' is %d\n", name, value & 0xFFFF);
	return value & 0xFFFF;
}

static int do_cmd_join_cluster(char *cmdbuf, int *retlen)
{
	struct cl_join_cluster_info *join_info = (struct cl_join_cluster_info *)cmdbuf;
	struct utsname un;

	if (ais_running)
		return -EALREADY;

	if (strlen(join_info->cluster_name) > MAX_CLUSTER_NAME_LEN)
		return -EINVAL;

	if (!num_interfaces)
		return -ENOTCONN;

	expected_votes = join_info->expected_votes;

	cluster_id = generate_cluster_id(join_info->cluster_name);
	strncpy(cluster_name, join_info->cluster_name, MAX_CLUSTER_NAME_LEN);
	two_node = join_info->two_node;
	config_version = join_info->config_version;

	quit_threads = 0;
	ais_running = 1;

	/* Make sure we have a node name */
	if (nodename[0] == '\0') {
		uname(&un);
		strcpy(nodename, un.nodename);
	}

	us = add_new_node(nodename, wanted_nodeid, join_info->votes, join_info->expected_votes,
			  NODESTATE_JOINING, 0);
	set_port_bit(us, 0);
	us->us = 1;

	return comms_init_ais(join_info->port);
}

static int do_cmd_leave_cluster(char *cmdbuf, int *retlen)
{
	int leave_flags;

	if (!ais_running)
		return -ENOTCONN;

	if (!we_are_a_cluster_member)
		return -ENOENT;

	memcpy(&leave_flags, cmdbuf, sizeof(int));

	/* Ignore the use count if FORCE is set */
	if (!(leave_flags & CLUSTER_LEAVEFLAG_FORCE)) {
		if (use_count)
			return -ENOTCONN;
	}

	us->leave_reason = leave_flags;
	quit_threads = 1;

	send_leave(leave_flags);
	use_count = 0;

	/* When we get our leave message back, then quit */
	return 0;
}

static int do_cmd_register_quorum_device(char *cmdbuf, int *retlen)
{
	int votes;
	char *name = cmdbuf+sizeof(int);

	if (!ais_running)
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

	quorum_device = malloc(sizeof(struct cluster_node));
        if (!quorum_device)
                return -ENOMEM;
        memset(quorum_device, 0, sizeof(struct cluster_node));

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
#if 0 // TODO
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
#endif
	return 0;
}

static int do_cmd_add_mcast(char *cmdbuf, int *retlen)
{
	static int got_mcast = 0;

	/* Only 1 multicast address allowed */
	if (got_mcast)
		return -EADDRINUSE;

	got_mcast = 1;
	return ais_set_mcast(cmdbuf);
}

static int do_cmd_add_ifaddr(char *cmdbuf, int *retlen)
{
	return ais_add_ifaddr(cmdbuf);
}

int process_command(struct connection *con, int cmd, char *cmdbuf,
		    char **retbuf, int *retlen, int retsize, int offset)
{
	int err = -EINVAL;
	struct cl_version cnxman_version;
	char *outbuf = *retbuf;

	P_MEMB("command to process is %x\n", cmd);

	switch (cmd) {

		/* Return the cnxman version number */
	case CMAN_CMD_GET_VERSION:
		err = 0;
		cnxman_version.major = CNXMAN_MAJOR_VERSION;
		cnxman_version.minor = CNXMAN_MINOR_VERSION;
		cnxman_version.patch = CNXMAN_PATCH_VERSION;
		cnxman_version.config = config_version;
		memcpy(outbuf+offset, &cnxman_version, sizeof(struct cl_version));
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
		return ais_running;

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

	case CMAN_CMD_ADD_MCAST:
		err = do_cmd_add_mcast(cmdbuf, retlen);
		break;

	case CMAN_CMD_ADD_IFADDR:
		err = do_cmd_add_ifaddr(cmdbuf, retlen);
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
	P_MEMB("command return code is %d\n", err);
	return err;
}


int send_to_userport(unsigned char fromport, unsigned char toport,
		     int nodeid, int tgtid,
		     uint32_t ais_nodeid,
		     char *recv_buf, int len, int endian_conv)
{
	int ret = -1;

	/* Only allow tgt==-1 if we are joining */
	if (tgtid == -1 && us->state != NODESTATE_JOINING)
		return 0;

	if (toport == 0) {
		process_internal_message(recv_buf, len, nodeid, ais_nodeid);
		ret = 0;
	}
	else {
		/* Send to external listener */
		if (port_array[toport]) {
			struct connection *c = port_array[toport];

			P_MEMB("send_to_userport. cmd=%d, len=%d, endian_conv=%d\n", recv_buf[0], len, endian_conv);

			send_data_reply(c, nodeid, fromport, recv_buf, len);
			ret = 0;
		}
	}
	return ret;
}

/* Send a port closedown message to all cluster nodes - this tells them that a
 * port listener has gone away */
static int send_port_close_msg(unsigned char port)
{
	struct cl_portmsg portmsg;

	/* Build the header */
	portmsg.cmd = CLUSTER_MSG_PORTCLOSED;
	portmsg.port = port;

	return comms_send_message(&portmsg, sizeof(portmsg), 0,0, 0, 0);
}

static int send_port_open_msg(unsigned char port)
{
	struct cl_portmsg portmsg;

	/* Build the header */
	portmsg.cmd = CLUSTER_MSG_PORTOPENED;
	portmsg.port = port;

	return comms_send_message(&portmsg, sizeof(portmsg), 0,0, 0, 0);
}

void unbind_con(struct connection *con)
{
	if (con->port) {
		port_array[con->port] = NULL;
		send_port_close_msg(con->port);
	}

	clear_port_bit(us, con->port);
	con->port = 0;
}

/* A remote port has been closed - post an OOB message to the local listen on
 * that port (if there is one) */
static void post_close_event(unsigned char port, int nodeid)
{
	struct connection *con;
	struct cluster_node *node;

	con = port_array[port];

	/* Make a note here too */
	node = find_node_by_nodeid(nodeid);
	if (node)
		clear_port_bit(node, port);

	if (con)
		notify_listeners(con, EVENT_REASON_PORTCLOSED, nodeid);
}

int our_nodeid()
{
	if (us)
		return us->node_id;
	else
		return 0;
}

/* Sanity check JOIN message */
static int valid_nodemsg(struct cl_nodemsg *nodemsg)
{
	struct cluster_node *node_byid;
	struct cluster_node *node_byname;

	if (strcmp(nodemsg->clustername, cluster_name) != 0) {
		log_msg(LOG_ERR, "Node refused, remote cluster name='%s', local='%s'\n", nodemsg->clustername, cluster_name);
		return -1;
	}

	if (nodemsg->cluster_id != cluster_id) {
		log_msg(LOG_ERR, "Node refused, remote cluster id=%d, local=%d\n", nodemsg->cluster_id, cluster_id);
		return -1;
	}


	node_byid = find_node_by_nodeid(nodemsg->nodeid);
	node_byname = find_node_by_name(nodemsg->name);
	if (nodemsg->nodeid && node_byid != node_byname) {
		log_msg(LOG_ERR, "Node refused, nodeid %d already in use\n", nodemsg->nodeid);
		return -1;
	}

	if (nodemsg->major_version != CNXMAN_MAJOR_VERSION) {

		log_msg(LOG_ERR, "Node refused, remote version id=%d, local=%d\n", nodemsg->major_version, CNXMAN_MAJOR_VERSION);
		return -1;
	}

	if (nodemsg->config_version != config_version) {
		log_msg(LOG_ERR, "Node refused, remote config version id=%d, local=%d\n", nodemsg->config_version, config_version);
		return -1;
	}

	return 0;
}


// TODO What happens if we boot several nodes together ???????
static poll_timer_handle join_timer;
extern poll_handle ais_poll_handle;
static void join_timeout(void *data)
{
	P_MEMB("Join_timeout\n");
	if (cluster_members == 0) {

		P_MEMB("We are in a 1 node cluster\n");
		we_are_a_cluster_member = 1;
		us->state = NODESTATE_MEMBER;
		cluster_members++;

		if (!us->node_id)
			us->node_id = 1;

		recalculate_quorum(0);
	}
}

void send_joinreq()
{
	char buf[sizeof(struct cl_nodemsg)+strlen(nodename)];
	struct cl_nodemsg *msg = (struct cl_nodemsg *)buf;

	P_MEMB("sending JOINREQ message\n");
	msg->cmd = CLUSTER_MSG_JOINREQ;
	msg->nodeid = wanted_nodeid;
	msg->votes  = us->votes;
	msg->expected_votes = us->expected_votes;
	msg->cluster_id = cluster_id;
	msg->major_version = CNXMAN_MAJOR_VERSION;
	msg->minor_version = CNXMAN_MINOR_VERSION;
	msg->patch_version = CNXMAN_PATCH_VERSION;
	msg->config_version = config_version;
	strcpy(msg->clustername, cluster_name);
	strcpy(msg->name, nodename);

	comms_send_message(buf, sizeof(buf),
			   0,0,
			   0,  /* multicast */
			   0); /* flags */

	/* If this timer expires then we are in a 1 node cluster */
	poll_timer_add(ais_poll_handle, 1000, NULL, join_timeout, &join_timer);
}

static void do_reconfigure_msg(void *data)
{
	struct cl_reconfig_msg *msg = data;
	struct cluster_node *node;
	struct list *nodelist;

	node = find_node_by_nodeid(msg->nodeid);
	if (!node)
		return;

	switch(msg->param)
	{
	case RECONFIG_PARAM_EXPECTED_VOTES:
		node->expected_votes = msg->value;

		list_iterate(nodelist, &cluster_members_list) {
			node = list_item(nodelist, struct cluster_node);
			if (node->state == NODESTATE_MEMBER &&
			    node->expected_votes > msg->value) {
				node->expected_votes = msg->value;
			}
		}
		recalculate_quorum(1);  /* Allow decrease */
		break;

	case RECONFIG_PARAM_NODE_VOTES:
		node->votes = msg->value;
		recalculate_quorum(1);  /* Allow decrease */
		break;

	case RECONFIG_PARAM_CONFIG_VERSION:
		config_version = msg->value;
		break;
	}
}


static void do_process_joinreq(char *data, int len, int nodeid, uint32_t ais_nodeid)
{
	struct cl_nodemsg *nodemsg;
	struct cluster_node *node;
	struct cluster_node *lownode;
	int new_nodeid = 0;

	nodemsg = (struct cl_nodemsg *)data;
	if (we_are_a_cluster_member && valid_nodemsg(nodemsg) == -1) {
		send_kill(-1, nodemsg->nodeid);
		return;
	}

	/* This is our join message returned */
	P_MEMB("process_joinreq, our state = %d\n", us->state);
	if (us->state == NODESTATE_JOINING && nodemsg->nodeid == wanted_nodeid) {
		us->incarnation = incarnation;
		return;
	}
	if (ais_nodeid == us->ais_nodeid) {
		P_MEMB("Discarding our JOINREQ message\n");
		return;
	}

	node = add_new_node(nodemsg->name, nodemsg->nodeid, nodemsg->votes, nodemsg->expected_votes,
			    NODESTATE_JOINING, ais_nodeid);
	recalculate_quorum(0);

	lownode = get_lowest_node();
	P_MEMB("lownode = %s (%d)\n", lownode->name, lownode->node_id);
	if (lownode != us)
		return;

	/* Allocate a new nodeid if needed */
	if (!node->node_id) {
		new_nodeid = get_highest_nodeid()+1;
		node->node_id = new_nodeid;
		P_MEMB("Allocating new nodeid %d for %s\n", new_nodeid, node->name);
	}
	send_joinconf(0, node->node_id);
}

static void process_internal_message(char *data, int len, int nodeid, uint32_t ais_nodeid)
{
	struct cl_protmsg *msg = (struct cl_protmsg *)data;
	struct cl_portmsg *portmsg;
	struct cl_barriermsg *barriermsg;
	struct cl_killmsg *killmsg;
	struct cl_leavemsg *leavemsg;
	struct cluster_node *node = find_node_by_nodeid(nodeid);

	P_MEMB("Message on port 0 is %d (len = %d)\n", msg->cmd, len);

	// TODO Byteswap messages

	switch (msg->cmd) {
	case CLUSTER_MSG_PORTOPENED:
		portmsg = (struct cl_portmsg *)data;
		if (node)
			set_port_bit(node, portmsg->port);
		break;

	case CLUSTER_MSG_PORTCLOSED:
		portmsg = (struct cl_portmsg *)data;
		if (node)
			clear_port_bit(node, portmsg->port);
		post_close_event(portmsg->port, nodeid);
		break;

	case CLUSTER_MSG_JOINREQ:
		do_process_joinreq(data, len, nodeid, ais_nodeid);
		break;

	case CLUSTER_MSG_JOINCONF:
		do_process_joinconf(nodeid, data, len);
		break;

	case CLUSTER_MSG_KILLNODE:
		killmsg = (struct cl_killmsg *)data;
		P_MEMB("got KILL for wanted node %d\n", killmsg->wanted_nodeid);
		if (killmsg->wanted_nodeid == wanted_nodeid)
			exit(1);
		break;

	case CLUSTER_MSG_LEAVE:
		leavemsg = (struct cl_leavemsg *)data;
		P_MEMB("got LEAVE from node %d, reason = %d\n", nodeid, leavemsg->reason);

		/* We got our own leave message back. now quit */
		if (node->node_id == us->node_id)
			exit(0);

		/* Someone else, make a note of the reason for leaving */
		if (node)
			node->leave_reason = leavemsg->reason;

		/* Mark it as leaving, and remove it when we get an AIS node down event for it */
		if (node && node->state == NODESTATE_MEMBER)
			node->state = NODESTATE_LEAVING;
		break;

	case CLUSTER_MSG_BARRIER:
		// TODO test barriers & maybe slim them down now we have VS
		barriermsg = (struct cl_barriermsg *)data;
		if (node)
			process_barrier_msg(barriermsg, node);
		break;

	case CLUSTER_MSG_RECONFIGURE:
		do_reconfigure_msg(data);
		break;

	default:
		log_msg(LOG_WARNING, "Unknown protocol message %d received\n", msg->cmd);
		break;

	}
}

void add_ais_node(uint32_t ais_nodeid, uint64_t incarnation, int total_members)
{
	struct cluster_node *node;

	P_MEMB("add_ais_node %x, incarnation = %d\n", ais_nodeid, incarnation);

	node = find_node_by_ais_nodeid(ais_nodeid);
	if (!node && total_members == 1) {
		node = us;
		P_MEMB("Adding AIS node for US\n");
	}

	if (!node) {
		node = malloc(sizeof(struct cluster_node));
		if (!node) {
			// TODO what ??
			return;
		}
		memset(node, 0, sizeof(struct cluster_node));
		list_add(&cluster_members_list, &node->list);
		node->state = NODESTATE_JOINING;
	}

	node->incarnation = incarnation;
	node->ais_nodeid = ais_nodeid;
	gettimeofday(&node->join_time, NULL);

	if (node->state == NODESTATE_DEAD)
		node->state = NODESTATE_JOINING;
}

void del_ais_node(uint32_t ais_nodeid)
{
	struct cluster_node *node;
	P_MEMB("del_ais_node %x\n", ais_nodeid);

	node = find_node_by_ais_nodeid(ais_nodeid);
	assert(node);

	if (node->state == NODESTATE_MEMBER) {
		node->state = NODESTATE_DEAD;
		cluster_members--;
		recalculate_quorum(0);
		return;
	}
	if (node->state == NODESTATE_LEAVING) {
		node->state = NODESTATE_DEAD;
		cluster_members--;

		if ((node->leave_reason & 0xF) == CLUSTER_LEAVEFLAG_REMOVED)
			recalculate_quorum(1);
		else
			recalculate_quorum(0);
	}
}

// TODO make these more efficient!
static int get_highest_nodeid()
{
	int highest = 0;
	struct cluster_node *node;

	list_iterate_items(node, &cluster_members_list) {
		if (node->node_id > highest)
			highest = node->node_id;
	}
	return highest;
}

static int get_node_count()
{
	int count = 0;

	struct cluster_node *node;

	list_iterate_items(node, &cluster_members_list) {
		count++;
	}
	return count;
}

static struct cluster_node *get_lowest_node()
{
	int lowest = INT_MAX;
	struct cluster_node *node;
	struct cluster_node *lnode;

	list_iterate_items(node, &cluster_members_list) {
		if (node->node_id && node->state == NODESTATE_MEMBER && node->node_id < lowest) {
			lnode = node;
			lowest = lnode->node_id;
		}
	}
	return lnode;
}


static struct cluster_node *find_node_by_nodeid(int nodeid)
{
	struct cluster_node *node;

	list_iterate_items(node, &cluster_members_list) {
		if (node->node_id == nodeid)
			return node;
	}
	return NULL;
}

static struct cluster_node *find_node_by_ais_nodeid(unsigned int ais_nodeid)
{
	struct cluster_node *node;

	list_iterate_items(node, &cluster_members_list) {
		if (node->ais_nodeid == ais_nodeid)
			return node;
	}
	return NULL;
}

static struct cluster_node *find_node_by_name(char *name)
{
	struct cluster_node *node;

	list_iterate_items(node, &cluster_members_list) {
		if (node->name && strcmp(node->name, name) == 0)
			return node;
	}
	return NULL;
}

static struct cluster_node *find_joining()
{
	struct cluster_node *node;
	struct cluster_node *jnode = NULL;

	list_iterate_items(node, &cluster_members_list) {
		if (node->state == NODESTATE_JOINING) {
			assert(!jnode);

			jnode = node;
		}
	}
	return jnode;
}

