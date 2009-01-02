#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <syslog.h>
#include <string.h>
#include <time.h>
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
#include <dlfcn.h>

#include <corosync/ipc_gen.h>
#include <corosync/engine/coroapi.h>
#include <corosync/engine/logsys.h>
#include "list.h"
#include "cman.h"
#include "cnxman-socket.h"
#include "cnxman-private.h"
#include "daemon.h"
#include "barrier.h"
#include "logging.h"
#define OBJDB_API struct corosync_api_v1
#include "cmanconfig.h"
#include "nodelist.h"
#include "commands.h"
#include "ais.h"

#define max(a,b) (((a) > (b)) ? (a) : (b))
LOGSYS_DECLARE_SUBSYS (CMAN_NAME, LOG_INFO);

/* Reference counting for cluster applications */
static int use_count;

/* Array of "ports" allocated. This is just a list of pointers to the connection that
 * has this port bound. Port 0 is reserved for protocol messages */
static struct connection *port_array[256];

// Stuff that was more global
static LIST_INIT(cluster_members_list);
       int cluster_members;
       int we_are_a_cluster_member;
       unsigned int config_version;
static struct cluster_node *us;
static int quorum;
extern int two_node;
       unsigned int quorumdev_poll=DEFAULT_QUORUMDEV_POLL;
       unsigned int shutdown_timeout=DEFAULT_SHUTDOWN_TIMEOUT;
       unsigned int ccsd_poll_interval=DEFAULT_CCSD_POLL;
static int cluster_is_quorate;
       char cluster_name[MAX_CLUSTER_NAME_LEN+1];
static char nodename[MAX_CLUSTER_MEMBER_NAME_LEN+1];
static int wanted_nodeid;
static struct cluster_node *quorum_device;
static uint16_t cluster_id;
static int ais_running;
static time_t join_time;
static corosync_timer_handle_t quorum_device_timer;
static struct corosync_api_v1 *corosync;

/* If CCS gets out of sync, we poll it until it isn't */
static corosync_timer_handle_t ccsd_timer;
static unsigned int wanted_config_version;
static int config_error;

static corosync_timer_handle_t shutdown_timer;
static struct connection *shutdown_con;
static uint32_t shutdown_flags;
static int shutdown_yes;
static int shutdown_no;
static int shutdown_expected;

static struct cluster_node *find_node_by_nodeid(int nodeid);
static struct cluster_node *find_node_by_name(char *name);
static int get_node_count(void);
static int get_highest_nodeid(void);
static int send_port_open_msg(unsigned char port);
static int send_port_enquire(int nodeid);
static void process_internal_message(char *data, int nodeid, int byteswap);
static void recalculate_quorum(int allow_decrease, int by_current_nodes);
static void send_kill(int nodeid, uint16_t reason);
static char *killmsg_reason(int reason);
static void ccsd_timer_fn(void *arg);


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

static int have_disallowed(void)
{
	struct cluster_node *node;

	list_iterate_items(node, &cluster_members_list) {
		if (node->state == NODESTATE_AISONLY)
			return 1;
	}

	return 0;
}

/* Make a totem_ip_address into a usable sockaddr_storage */
static int totemip_to_sockaddr(struct totem_ip_address *ip_addr,
			       uint16_t port, struct sockaddr_storage *saddr, int *addrlen)
{
	int ret = -1;

	if (ip_addr->family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)saddr;

		memset(sin, 0, sizeof(struct sockaddr_in));
		sin->sin_family = ip_addr->family;
		sin->sin_port = port;
		memcpy(&sin->sin_addr, ip_addr->addr, sizeof(struct in_addr));
		*addrlen = sizeof(struct sockaddr_in);
		ret = 0;
	}

	if (ip_addr->family == AF_INET6) {
		struct sockaddr_in6 *sin = (struct sockaddr_in6 *)saddr;

		memset(sin, 0, sizeof(struct sockaddr_in6));
		sin->sin6_family = ip_addr->family;
		sin->sin6_port = port;
		sin->sin6_scope_id = 2;
		memcpy(&sin->sin6_addr, ip_addr->addr, sizeof(struct in6_addr));

		*addrlen = sizeof(struct sockaddr_in6);
		ret = 0;
	}

	return ret;
}

/* If "cluster_is_quorate" is 0 then all activity apart from protected ports is
 * blocked. */
static void set_quorate(int total_votes)
{
	int quorate;
	unsigned int nodelist[PROCESSOR_COUNT_MAX];
	int nodecount = 0;
	struct cluster_node *node = NULL;
	struct list *tmp;

	if (quorum > total_votes || config_error) {
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

		list_iterate(tmp, &cluster_members_list) {
			node = list_item(tmp, struct cluster_node);
			if (node->state == NODESTATE_AISONLY)
				send_kill(node->node_id, CLUSTER_KILL_REJOIN);
		}
	}

	cluster_is_quorate = quorate;

	/* Inform corosync subsystems */
	list_iterate(tmp, &cluster_members_list) {
		node = list_item(tmp, struct cluster_node);
		if (node->state == NODESTATE_MEMBER) {
			nodelist[nodecount++] = node->node_id;
		}
	}

	corosync_set_quorum(nodelist, nodecount, quorate, &cman_ring_id);
}

static void node_add_ordered(struct cluster_node *newnode)
{
	struct cluster_node *node = NULL;
	struct list *tmp;
	struct list *newlist = &newnode->list;

	list_iterate(tmp, &cluster_members_list) {
		node = list_item(tmp, struct cluster_node);

                if (newnode->node_id < node->node_id)
                        break;
        }

        if (!node)
		list_add(&cluster_members_list, &newnode->list);
        else {
                newlist->p = tmp->p;
                newlist->n = tmp;
                tmp->p->n = newlist;
                tmp->p = newlist;
        }
}

static struct cluster_node *add_new_node(char *name, int nodeid, int votes, int expected_votes,
					 nodestate_t state)
{
	struct cluster_node *newnode = NULL;
	int newalloc = 0;

	if (nodeid)
		newnode = find_node_by_nodeid(nodeid);

	if (!newnode) {
		newnode = malloc(sizeof(struct cluster_node));
		if (!newnode) {
			log_printf(LOG_ERR, "Unable to allocate memory for node %s\n", name);
			return NULL;
		}
		memset(newnode, 0, sizeof(struct cluster_node));
		newalloc = 1;
		newnode->state = state;
		if (state == NODESTATE_MEMBER)
			newnode->incarnation = incarnation;
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

	if (!newnode->node_id) /* Don't clobber existing nodeid */
		newnode->node_id = nodeid;
	if (votes >= 0)
		newnode->votes = votes;
	if (expected_votes)
		newnode->expected_votes = expected_votes;

	/* If this node has a name passed in then use that rather than a previous generated one */
	if (name && newnode->name && strcmp(name, newnode->name)) {
		char *newname;

		newname = strdup(name);
		if (newname) {
			P_MEMB("replacing old node name %s with %s\n", newnode->name, name);
			free(newnode->name);
			newnode->name = newname;
		}
	}

	if (newalloc)
		node_add_ordered(newnode);

	newnode->flags |= NODE_FLAGS_REREAD;

	P_MEMB("add_new_node: %s, (id=%d, votes=%d) newalloc=%d\n",
	       name, nodeid, votes, newalloc);

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

static int calculate_quorum(int allow_decrease, int max_expected, unsigned int *ret_total_votes)
{
	struct list *nodelist;
	struct cluster_node *node;
	unsigned int total_votes = 0;
	unsigned int highest_expected = 0;
	unsigned int newquorum, q1, q2;
	unsigned int total_nodes = 0;

	list_iterate(nodelist, &cluster_members_list) {
		node = list_item(nodelist, struct cluster_node);

		if (node->state == NODESTATE_MEMBER) {
			if (max_expected)
				node->expected_votes = max_expected;
			else
				highest_expected = max(highest_expected, node->expected_votes);
			total_votes += node->votes;
			total_nodes++;
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
	return newquorum;
}

/* Recalculate cluster quorum, set quorate and notify changes */
static void recalculate_quorum(int allow_decrease, int by_current_nodes)
{
	unsigned int total_votes;

	quorum = calculate_quorum(allow_decrease, by_current_nodes?cluster_members:0, &total_votes);
	set_quorate(total_votes);
	notify_listeners(NULL, EVENT_REASON_STATECHANGE, cluster_is_quorate);
}

/* Copy internal node format to userland format */
static void copy_to_usernode(struct cluster_node *node,
			     struct cl_cluster_node *unode)
{
	struct sockaddr_storage ss;
	int addrlen=0;
	unsigned int numaddrs=1;
	char **status;
	struct totem_ip_address node_ifs[INTERFACE_MAX];
	/* totempg_ifaces_get always copies INTERFACE_MAX addresses */

	strcpy(unode->name, node->name);
	unode->jointime = node->join_time;
	unode->size = sizeof(struct cl_cluster_node);
	unode->votes = node->votes;
	unode->state = node->state;
	unode->us = node->us;
	unode->node_id = node->node_id;
	unode->leave_reason = node->leave_reason;
	unode->incarnation = node->incarnation;

	/* Just send the first address. If the user wants the full set they
	   must ask for them */
	corosync->totem_ifaces_get(node->node_id, node_ifs, &status, &numaddrs);

	totemip_to_sockaddr(&node_ifs[0], 0, &ss, &addrlen);
	memcpy(unode->addr, &ss, addrlen);
	unode->addrlen = addrlen;
}


int cman_set_nodename(char *name)
{
	if (ais_running)
		return -EALREADY;

	strncpy(nodename, name, MAX_CLUSTER_MEMBER_NAME_LEN);
	return 0;
}

int cman_set_nodeid(int nodeid)
{
	if (ais_running)
		return -EALREADY;

	wanted_nodeid = nodeid;
	return 0;
}

int cman_join_cluster(struct corosync_api_v1 *api,
		      char *name, unsigned short cl_id,
		      int two_node_flag, int votes, int expected_votes)
{
	if (ais_running)
		return -EALREADY;

	if (strlen(name) > MAX_CLUSTER_NAME_LEN)
		return -EINVAL;

	cluster_id = cl_id;
	strncpy(cluster_name, name, MAX_CLUSTER_NAME_LEN);
	two_node = two_node_flag;
	corosync = api;

	quit_threads = 0;
	ais_running = 1;

	/* Make sure we have a node name */
	if (nodename[0] == '\0') {
		struct utsname un;
		uname(&un);
		strcpy(nodename, un.nodename);
	}

	time(&join_time);
	us = add_new_node(nodename, wanted_nodeid, votes, expected_votes,
			  NODESTATE_DEAD);
	set_port_bit(us, 0);
	us->us = 1;

	return 0;
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

	/* We will re-read CCS when we get our own message back */
	send_reconfigure(us->node_id, RECONFIG_PARAM_CONFIG_VERSION, version->config);
	return 0;
}

static int do_cmd_get_extrainfo(char *cmdbuf, char **retbuf, int retsize, int *retlen, int offset)
{
	char *outbuf = *retbuf + offset;
	struct cl_extra_info *einfo = (struct cl_extra_info *)outbuf;
	struct totem_ip_address node_ifs[MAX_INTERFACES];
	int total_votes = 0;
	int max_expected = 0;
	int addrlen;
	int uncounted = 0;
	unsigned int num_interfaces;
	unsigned int totem_object_handle;
	unsigned int object_handle;
	unsigned int totem_find_handle;
	unsigned int iface_find_handle;
	char **status;
	struct cluster_node *node;
	struct sockaddr_storage *ss;
	char *ptr;
	int i;

	if (!we_are_a_cluster_member)
		return -ENOENT;

	corosync->totem_ifaces_get(us->node_id, node_ifs, &status, &num_interfaces);

	list_iterate_items(node, &cluster_members_list) {
		if (node->state == NODESTATE_MEMBER) {
			total_votes += node->votes;
			max_expected = max(max_expected, node->expected_votes);
		}
		if (node->state == NODESTATE_AISONLY)
			uncounted = 1;
	}
	if (quorum_device && quorum_device->state == NODESTATE_MEMBER)
		total_votes += quorum_device->votes;

        /* Enough room for addresses ? */
	if (retsize < (sizeof(struct cl_extra_info) +
		       sizeof(struct sockaddr_storage) * (MAX_INTERFACES*2))) {

		*retbuf = malloc(sizeof(struct cl_extra_info) + sizeof(struct sockaddr_storage) * (MAX_INTERFACES*2));
		outbuf = *retbuf + offset;
		einfo = (struct cl_extra_info *)outbuf;

		P_MEMB("get_extrainfo: allocated new buffer\n");
	}

	einfo->node_state = us->state;
	einfo->node_votes = us->votes;
	einfo->total_votes = total_votes;
	einfo->expected_votes = max_expected;
	einfo->quorum = quorum;
	einfo->members = cluster_members;
	einfo->num_addresses = num_interfaces;
	memcpy(einfo->ports, us->port_bits, 32);
	einfo->flags = 0;
	if (two_node)
		einfo->flags |= CMAN_EXTRA_FLAG_2NODE;
	if (config_error)
		einfo->flags |= CMAN_EXTRA_FLAG_ERROR;
	if (shutdown_con)
		einfo->flags |= CMAN_EXTRA_FLAG_SHUTDOWN;
	if (uncounted)
		einfo->flags |= CMAN_EXTRA_FLAG_UNCOUNTED;
	if (us->flags & NODE_FLAGS_DIRTY)
		einfo->flags |= CMAN_EXTRA_FLAG_DIRTY;

	ptr = einfo->addresses;

	corosync->object_find_create(OBJECT_PARENT_HANDLE, "totem", strlen("totem"), &totem_find_handle);
	if (corosync->object_find_next(totem_find_handle, &totem_object_handle) == 0) {

		corosync->object_find_destroy(totem_find_handle);

		corosync->object_find_create(totem_object_handle, "interface", strlen("interface"), &iface_find_handle);
		while (corosync->object_find_next(iface_find_handle, &object_handle) == 0) {

			char *mcast;
			struct sockaddr_in *saddr4;
			struct sockaddr_in6 *saddr6;

			objdb_get_string(corosync, object_handle, "mcastaddr", &mcast);
			memset(ptr, 0, sizeof(struct sockaddr_storage));

			saddr4 = (struct sockaddr_in *)ptr;
			saddr6 = (struct sockaddr_in6 *)ptr;
			if ( inet_pton(AF_INET, mcast, &saddr4->sin_addr) >0) {
				saddr4->sin_family = AF_INET;
			}
			else {
				if (inet_pton(AF_INET6, mcast, &saddr6->sin6_addr) > 0)
					saddr4->sin_family = AF_INET6;
			}
			ptr += sizeof(struct sockaddr_storage);
		}
	}
	corosync->object_find_destroy(iface_find_handle);

	for (i=0; i<num_interfaces; i++) {
		ss = (struct sockaddr_storage *)ptr;
		totemip_to_sockaddr(&node_ifs[i], 0, ss, &addrlen);
		ptr += sizeof(struct sockaddr_storage);
	}

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

	/* This returns the full list */
	list_iterate_items(node, &cluster_members_list) {
		if (retsize) {
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

	if (u_node->node_id == CLUSTER_GETNODE_QUORUMDEV) {
		if (quorum_device)
			node = quorum_device;
		else
			return -ENOENT;
	}
	else {
		if (!u_node->name[0]) {
			if (u_node->node_id == 0)
				u_node->node_id = us->node_id;
			node = find_node_by_nodeid(u_node->node_id);
		}
		else
			node = find_node_by_name(u_node->name);

		if (!node) {
			P_MEMB("cmd_get_node failed: id=%d, name='%s'\n", u_node->node_id, u_node->name);
			return -ENOENT;
		}
	}

	copy_to_usernode(node, r_node);
	*retlen = sizeof(struct cl_cluster_node);

	return 0;
}

static int do_cmd_set_expected(char *cmdbuf, int *retlen)
{
	unsigned int total_votes;
	unsigned int newquorum;
	unsigned int newexp;
	struct cluster_node *node = NULL;
	struct list *tmp;

	if (!we_are_a_cluster_member)
		return -ENOENT;

	/* If there are any AISONLY nodes then we can't allow
	   the user to set expected votes as it may destroy data */
	list_iterate(tmp, &cluster_members_list) {
		node = list_item(tmp, struct cluster_node);
		if (node->state == NODESTATE_AISONLY) {
			log_printf(LOG_NOTICE, "Attempt to set expected votes when cluster has AISONLY nodes in it.");
			return -EINVAL;
		}
	}

	memcpy(&newexp, cmdbuf, sizeof(int));
	newquorum = calculate_quorum(1, newexp, &total_votes);

	if (newquorum < total_votes / 2
	    || newquorum > total_votes) {
		return -EINVAL;
	}

	override_expected(newexp);
	send_reconfigure(us->node_id, RECONFIG_PARAM_EXPECTED_VOTES, newexp);

	/* We will recalculate quorum when we get our own message back */
	return 0;
}

static void send_kill(int nodeid, uint16_t reason)
{
	struct cl_killmsg msg;

	P_MEMB("Sending KILL to node %d\n", nodeid);

	msg.cmd = CLUSTER_MSG_KILLNODE;
	msg.reason = reason;
	msg.nodeid = nodeid;

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

	if (node->state != NODESTATE_MEMBER && node->state != NODESTATE_AISONLY)
		return -EINVAL;

	node->leave_reason = CLUSTER_LEAVEFLAG_KILLED;
	node->state = NODESTATE_LEAVING;

	/* Send a KILL message */
	send_kill(nodeid, CLUSTER_KILL_CMANTOOL);

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


	/* If we don't know the node's port status then ask it.
	   This should only need to be done when we are the new node in
	   a cluster that has been running for a while
	*/
       	if (!get_port_bit(rem_node, 0)) {
		P_MEMB("islistening, no data for node %d, sending PORTENQ\n", nodeid);
		send_port_enquire(rem_node->node_id);

		/* Admit our ignorance */
		return -EBUSY;
	}
	else {
		P_MEMB("islistening, for node %d, low bytes are %x %x\n", nodeid, rem_node->port_bits[0], rem_node->port_bits[1]);
		return get_port_bit(rem_node, rq.port);
	}
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

	recalculate_quorum(1, 0);

	send_reconfigure(arg.nodeid, RECONFIG_PARAM_NODE_VOTES, arg.newvotes);

	return 0;
}

static int do_cmd_bind(struct connection *con, char *cmdbuf)
{
	unsigned int port;
	int ret = -EADDRINUSE;

	memcpy(&port, cmdbuf, sizeof(int));

	P_MEMB("requested bind to port %d, (us=%p)\n", port, con);

	if (port == 0 || port > 255)
		return -EINVAL;

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

static int do_cmd_leave_cluster(char *cmdbuf, int *retlen)
{
	int leave_flags;

	if (!ais_running)
		return -ENOTCONN;

	memcpy(&leave_flags, cmdbuf, sizeof(int));

	/* Ignore the use count if FORCE is set */
	if (!(leave_flags & CLUSTER_LEAVEFLAG_FORCE)) {
		if (use_count)
			return -ENOTCONN;
	}

	us->leave_reason = leave_flags;
	quit_threads = 1;

	/* No messaging available yet, just die */
	if (!we_are_a_cluster_member)
		exit(0);

	send_leave(leave_flags);
	use_count = 0;

	/* When we get our leave message back, then quit */
	return 0;
}

static void check_shutdown_status()
{
	int reply;
	int leaveflags = CLUSTER_LEAVEFLAG_DOWN;

	/* All replies safely gathered in ? */
	if (shutdown_yes + shutdown_no >= shutdown_expected) {

		corosync->timer_delete(shutdown_timer);

		if (shutdown_yes >= shutdown_expected ||
		    shutdown_flags & SHUTDOWN_ANYWAY) {
			quit_threads = 1;
			if (shutdown_flags & SHUTDOWN_REMOVE)
				leaveflags |= CLUSTER_LEAVEFLAG_REMOVED;
			send_leave(leaveflags);
			reply = 0;
		}
		else {
			reply = -EBUSY;

			/* Tell originator that shutdown was cancelled */
			send_status_return(shutdown_con, CMAN_CMD_TRY_SHUTDOWN, reply);
			shutdown_con = NULL;
		}

		P_MEMB("shutdown decision is: %d (yes=%d, no=%d) flags=%x\n", reply, shutdown_yes, shutdown_no, shutdown_flags);
	}
}

/* Not all nodes responded to the shutdown */
static void shutdown_timer_fn(void *arg)
{
	P_MEMB("Shutdown timer fired. flags = %x\n", shutdown_flags);

	/* Mark undecideds as "NO" */
	shutdown_no = shutdown_expected;
	check_shutdown_status();
}

/* A service's response to a TRY_SHUTDOWN event. This NEVER returns a response */
static int do_cmd_shutdown_reply(struct connection *con, char *cmdbuf)
{
	int response = *(int *)cmdbuf;

	/* Not shutting down, but don't respond. */
	if (!shutdown_con)
		return -EWOULDBLOCK;

	P_MEMB("Shutdown reply is %d\n", response);

	/* We only need to keep a track of a client's response in
	   case it pulls the connection before the shutdown process
	   has completed */
	if (response) {
		shutdown_yes++;
		con->shutdown_reply = SHUTDOWN_REPLY_YES;
	}
	else {
		shutdown_no++;
		con->shutdown_reply = SHUTDOWN_REPLY_NO;
	}
	check_shutdown_status();

	/* No response needed to this message */
	return -EWOULDBLOCK;
}

/* User requested shutdown. We poll all listening clients and see if they are
   willing to shutdown */
static int do_cmd_try_shutdown(struct connection *con, char *cmdbuf)
{
	int flags = *(int *)cmdbuf;

	/* Are we already in shutdown ? */
	if (shutdown_con || quit_threads)
		return -EALREADY;

	shutdown_con = con;
	shutdown_flags = flags;
	shutdown_yes = 0;
	shutdown_no = 0;
	shutdown_expected = num_listeners();

	/* If no-one is listening for events then we can just go down now */
	if (shutdown_expected == 0) {
		quit_threads = 1;
		send_leave(CLUSTER_LEAVEFLAG_DOWN);
		return 0;
	}
	else {

		/* Start the timer. If we don't get a full set of replies before this goes
		   off we'll cancel the shutdown */
		corosync->timer_add_duration((unsigned long long)shutdown_timeout*1000000, NULL,
					   shutdown_timer_fn, &shutdown_timer);

		notify_listeners(NULL, EVENT_REASON_TRY_SHUTDOWN, flags);

		return -EWOULDBLOCK;
	}
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

	log_printf(LOG_INFO, "quorum device registered\n");
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

	log_printf(LOG_INFO, "quorum device unregistered\n");
        return 0;
}

static int reread_config(int new_version)
{
	int read_err;
	char *reload_err = NULL;

	wanted_config_version = new_version;

	/* Tell objdb to reload */
	read_err = corosync->object_reload_config(1, &reload_err);

	/* Now get our bits */
	if (!read_err)
		read_err = read_cman_nodes(corosync, &config_version, 0);

	if (read_err) {
		config_error = 1;
		log_printf(LOG_ERR, "Can't get updated config version %d: %s. Activity suspended on this node\n",
			   wanted_config_version, reload_err?reload_err:"version mismatch on this node");
	}

	/* Still too old?? */
	if (config_version < wanted_config_version) {
		log_printf(LOG_ERR, "Can't get updated config version %d, config file is version %d.\n",
			   wanted_config_version, config_version);
	}

	/* Keep looking */
	if (read_err || config_version < wanted_config_version) {
		corosync->timer_add_duration((unsigned long long)ccsd_poll_interval*1000000, NULL,
					     ccsd_timer_fn, &ccsd_timer);
	}

	return read_err;
}

static void ccsd_timer_fn(void *arg)
{
	log_printf(LOG_DEBUG, "Polling configuration for updated information\n");
	if (!reread_config(wanted_config_version) && config_version >= wanted_config_version) {
		log_printf(LOG_ERR, "Now got config information version %d, continuing\n", config_version);
		config_error = 0;
		recalculate_quorum(0, 0);
		notify_listeners(NULL, EVENT_REASON_CONFIG_UPDATE, config_version);
	}
}


static void quorum_device_timer_fn(void *arg)
{
	struct timeval now;
	if (!quorum_device || quorum_device->state == NODESTATE_DEAD)
		return;

	P_MEMB("quorum_device_timer_fn\n");
	gettimeofday(&now, NULL);
	if (quorum_device->last_hello.tv_sec + quorumdev_poll/1000 < now.tv_sec) {
		quorum_device->state = NODESTATE_DEAD;
		log_printf(LOG_INFO, "lost contact with quorum device\n");
		recalculate_quorum(0, 0);
	}
	else {
		corosync->timer_add_duration((unsigned long long)quorumdev_poll*1000000, quorum_device,
					   quorum_device_timer_fn, &quorum_device_timer);
	}
}

static int do_cmd_poll_quorum_device(char *cmdbuf, int *retlen)
{
	int yesno;

        if (!quorum_device)
                return -EINVAL;

	memcpy(&yesno, cmdbuf, sizeof(int));

        if (yesno) {
		gettimeofday(&quorum_device->last_hello, NULL);
                if (quorum_device->state == NODESTATE_DEAD) {
                        quorum_device->state = NODESTATE_MEMBER;
                        recalculate_quorum(0, 0);

			corosync->timer_add_duration((unsigned long long)quorumdev_poll*1000000, quorum_device,
						   quorum_device_timer_fn, &quorum_device_timer);
                }
        }
        else {
                if (quorum_device->state == NODESTATE_MEMBER) {
                        quorum_device->state = NODESTATE_DEAD;
                        recalculate_quorum(0, 0);
			corosync->timer_delete(quorum_device_timer);
                }
        }

	return 0;
}

/* fence_tool tells us it has fenced a node */
static int do_cmd_update_fence_info(char *cmdbuf)
{
	struct cl_fence_info *f = (struct cl_fence_info *)cmdbuf;
	struct cluster_node *node;
	char msg[sizeof(struct cl_fencemsg)+strlen(f->fence_agent)+1];
	struct cl_fencemsg *fence_msg = (struct cl_fencemsg *)msg;

	node = find_node_by_nodeid(f->nodeid);
	if (!node)
		return -EINVAL;

	if (strlen(f->fence_agent) >= MAX_FENCE_AGENT_NAME_LEN)
		return -EINVAL;

	node->flags |= NODE_FLAGS_FENCED;
	if (node->state == NODESTATE_MEMBER)
		node->flags |= NODE_FLAGS_FENCEDWHILEUP;

	/* Tell the rest of the cluster (and us!) */
	fence_msg->cmd = CLUSTER_MSG_FENCESTATUS;
	fence_msg->nodeid = f->nodeid;
	fence_msg->timesec = f->fence_time;
	fence_msg->fenced = 1;
	strcpy(fence_msg->agent, f->fence_agent);
	comms_send_message(msg, sizeof(msg), 0,0, 0, 0);

	P_MEMB("node %d fenced by %s\n", f->nodeid, f->fence_agent);
	return 0;
}

static int do_cmd_get_fence_info(char *cmdbuf, char **retbuf, int retsize, int *retlen, int offset)
{
	int nodeid;
	char *outbuf = *retbuf + offset;
	struct cl_fence_info *f = (struct cl_fence_info *)outbuf;
	struct cluster_node *node;

	if (retsize < sizeof(struct cl_fence_info))
		return -EINVAL;
	memcpy(&nodeid, cmdbuf, sizeof(nodeid));

	node = find_node_by_nodeid(nodeid);
	if (!node)
		return -EINVAL;

	f->nodeid = nodeid;
	f->fence_time = node->fence_time;
	f->flags = node->flags&NODE_FLAGS_FENCED;

	if (node->fence_agent)
		strcpy(f->fence_agent, node->fence_agent);
	else
		f->fence_agent[0] = '\0';
	*retlen = sizeof(struct cl_fence_info);
	return 0;
}

static int do_cmd_get_node_addrs(char *cmdbuf, char **retbuf, int retsize, int *retlen, int offset)
{
	int nodeid;
	int i;
	char *outbuf = *retbuf + offset;
	struct cl_get_node_addrs *addrs = (struct cl_get_node_addrs *)outbuf;
	struct totem_ip_address node_ifs[INTERFACE_MAX]; /* totempg_ifaces_get always copies INTERFACE_MAX addresses */
	struct cluster_node *node;
	char **status;

	if (retsize < sizeof(struct cl_node_addrs))
		return -EINVAL;
	memcpy(&nodeid, cmdbuf, sizeof(nodeid));

	node = find_node_by_nodeid(nodeid);
	if (!node)
		return -EINVAL;

	memset(outbuf, 0, retsize - offset);

	/* AIS doesn't know about nodes that are not members */
	if (node->state != NODESTATE_MEMBER)  {
		addrs->numaddrs = 0;
		*retlen = sizeof(struct cl_get_node_addrs);
		return 0;
	}

	if (corosync->totem_ifaces_get(nodeid, node_ifs, &status, (unsigned int *)&addrs->numaddrs))
		return -errno;

	for (i=0; i<addrs->numaddrs; i++) {
		totemip_to_sockaddr(&node_ifs[i], 0,
				    &addrs->addrs[i].addr,
				    &addrs->addrs[i].addrlen);
	}
	*retlen = sizeof(struct cl_get_node_addrs) +
		addrs->numaddrs * sizeof(struct cl_node_addrs);

	return 0;
}

int process_command(struct connection *con, int cmd, char *cmdbuf,
		    char **retbuf, int *retlen, int retsize, int offset)
{
	int err = -EINVAL;
	struct cl_version cnxman_version;
	char *outbuf = *retbuf;
	int value;
#ifdef DEBUG
	FILE *dumpfile;
#endif

	P_MEMB("command to process is %x\n", cmd);

	switch (cmd) {

	case CMAN_CMD_NOTIFY:
		con->events = 1;
		err = 0;
		/* If a shutdown is in progress, ask the newcomer what it thinks... */
		if (shutdown_con) {
			notify_listeners(con, EVENT_REASON_TRY_SHUTDOWN, shutdown_flags);
			shutdown_expected++;
		}
		break;

	case CMAN_CMD_REMOVENOTIFY:
		con->events = 0;
		err = 0;
		break;

	case CMAN_CMD_SET_DIRTY:
		us->flags |= NODE_FLAGS_DIRTY;
		err = 0;
		break;

	case CMAN_CMD_SET_DEBUGLOG:
		memcpy(&value, cmdbuf, sizeof(int));
		set_debuglog(value);
		err = 0;
		break;
#ifdef DEBUG
	case CMAN_CMD_DUMP_OBJDB:
		dumpfile = fopen(cmdbuf, "w+");
		if (dumpfile)  {
			corosync->object_dump(OBJECT_PARENT_HANDLE, dumpfile);
			fclose(dumpfile);
			err = 0;
		}
		else {
			err = -errno;
		}
		break;
#endif
	case CMAN_CMD_START_CONFCHG:
		con->confchg = 1;
		err = 0;
		break;

	case CMAN_CMD_STOP_CONFCHG:
		con->confchg = 0;
		err = 0;
		break;

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

	case CMAN_CMD_GETNODECOUNT:
		err = get_node_count();
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

	case CMAN_CMD_LEAVE_CLUSTER:
		err = do_cmd_leave_cluster(cmdbuf, retlen);
		break;

	case CMAN_CMD_GET_JOINCOUNT:
		err = num_connections;
		break;

	case CMAN_CMD_TRY_SHUTDOWN:
		err = do_cmd_try_shutdown(con, cmdbuf);
		break;

	case CMAN_CMD_SHUTDOWN_REPLY:
		err = do_cmd_shutdown_reply(con, cmdbuf);
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

	case CMAN_CMD_UPDATE_FENCE_INFO:
		err = do_cmd_update_fence_info(cmdbuf);
		break;

	case CMAN_CMD_GET_FENCE_INFO:
		err = do_cmd_get_fence_info(cmdbuf, retbuf, retsize, retlen, offset);
		break;

	case CMAN_CMD_GET_NODEADDRS:
		err = do_cmd_get_node_addrs(cmdbuf, retbuf, retsize, retlen, offset);
		break;
	}
	P_MEMB("command return code is %d\n", err);
	return err;
}


int send_to_userport(unsigned char fromport, unsigned char toport,
		     int nodeid, int tgtid,
		     char *recv_buf, int len,
		     int endian_conv)
{
	int ret = -1;

	if (toport == 0) {
		process_internal_message(recv_buf, nodeid, endian_conv);
		ret = 0;
	}
	else {
		/* Send to external listener */
		if (port_array[toport]) {
			struct connection *c = port_array[toport];

			P_MEMB("send_to_userport. cmd=%d,  endian_conv=%d\n", recv_buf[0],endian_conv);

			send_data_reply(c, nodeid, fromport, recv_buf, len);
			ret = 0;
		}
	}
	return ret;
}

void cman_send_confchg(unsigned int *member_list, int member_list_entries,
		       unsigned int *left_list, int left_list_entries,
		       unsigned int *joined_list, int joined_list_entries)
{
	char buf[sizeof(struct sock_confchg_message) +
		 (member_list_entries+left_list_entries+joined_list_entries) * sizeof(int)];
	struct sock_confchg_message *msg = (struct sock_confchg_message *)buf;

	msg->header.magic = CMAN_MAGIC;
	msg->header.command = CMAN_CMD_CONFCHG;
	msg->header.length = sizeof(buf);
	msg->header.flags = 0;

	msg->member_entries = member_list_entries;
	msg->joined_entries = joined_list_entries;
	msg->left_entries = left_list_entries;

	memcpy(msg->entries, member_list, sizeof(int)*member_list_entries);
	memcpy(msg->entries+member_list_entries, left_list, sizeof(int)*left_list_entries);
	memcpy(msg->entries+member_list_entries+left_list_entries, joined_list, sizeof(int)*joined_list_entries);

	notify_confchg((struct sock_header *)msg);
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

static int send_port_enquire(int nodeid)
{
	char msg[1];

	/* Build the header */
	msg[0] = CLUSTER_MSG_PORTENQ;

	return comms_send_message(msg, 1, 0,0, nodeid, 0);
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
		P_MEMB("Unbinding con for port %d\n", con->port);
		port_array[con->port] = NULL;
		send_port_close_msg(con->port);
		clear_port_bit(us, con->port);
		con->port = 0;
	}

	/* If we're in shutdown and this client was listening for events
	   then we take its closedown as a "Yes" to the "can we shutdown"
	   question. If it previously answered "No", we need to change its vote */
	if (shutdown_con && con->events) {
		if (con->shutdown_reply) {
			if (con->shutdown_reply == SHUTDOWN_REPLY_YES)
				shutdown_yes--;
			if (con->shutdown_reply == SHUTDOWN_REPLY_NO)
				shutdown_no--;
		}
		con->shutdown_reply = SHUTDOWN_REPLY_YES; /* I'll take that as a "Yes" then */
		shutdown_yes++;

		check_shutdown_status();
	}

	/* If the controlling shutdown process has quit, then cancel the
	   shutdown session */
	if (con == shutdown_con)
		shutdown_con = NULL;
}

/* Post a PORT OPEN/CLOSE event to anyone listening on this end */
static void post_port_event(int reason, unsigned char port, int nodeid)
{
	struct connection *con = port_array[port];

	if (con)
		notify_listeners(con, reason, nodeid);
}

int our_nodeid()
{
	if (us)
		return us->node_id;
	else
		return 0;
}

/* Sanity check TRANSITION message */
static int valid_transition_msg(int nodeid, struct cl_transmsg *msg)
{
	if (strcmp(msg->clustername, cluster_name) != 0) {
		log_printf(LOG_ERR, "Node %d conflict, remote cluster name='%s', local='%s'\n",
			nodeid, msg->clustername, cluster_name);
		return -1;
	}

	if (msg->cluster_id != cluster_id) {
		log_printf(LOG_ERR, "Node %d conflict, remote cluster id=%d, local=%d\n",
			nodeid, msg->cluster_id, cluster_id);
		return -1;
	}

	if (msg->major_version != CNXMAN_MAJOR_VERSION) {

		log_printf(LOG_ERR, "Node %d conflict, remote version id=%d, local=%d\n",
			nodeid, msg->major_version, CNXMAN_MAJOR_VERSION);
		return -1;
	}

	/* New config version - try to read new file */
	if (msg->config_version > config_version) {

		reread_config(msg->config_version);

		if (config_version > msg->config_version) {
			/* Tell everyone else to update */
			send_reconfigure(us->node_id, RECONFIG_PARAM_CONFIG_VERSION, config_version);
		}
		recalculate_quorum(0, 0);
		notify_listeners(NULL, EVENT_REASON_CONFIG_UPDATE, config_version);
	}


	if (msg->config_version != config_version) {
		log_printf(LOG_ERR, "Node %d conflict, remote config version id=%d, local=%d\n",
			nodeid, msg->config_version, config_version);
		return -1;
	}

	return 0;
}


void send_transition_msg(int last_memb_count, int first_trans)
{
	char buf[sizeof(struct cl_transmsg)+1024] __attribute__((aligned(8)));
	struct cl_transmsg *msg = (struct cl_transmsg *)buf;
	int len = sizeof(struct cl_transmsg);

	we_are_a_cluster_member = 1;

	P_MEMB("sending TRANSITION message. cluster_name = %s\n", cluster_name);
	msg->cmd = CLUSTER_MSG_TRANSITION;
	msg->first_trans = first_trans;
	msg->votes = us->votes;
	msg->expected_votes = us->expected_votes;
	msg->cluster_id = cluster_id;
	msg->major_version = CNXMAN_MAJOR_VERSION;
	msg->minor_version = CNXMAN_MINOR_VERSION;
	msg->patch_version = CNXMAN_PATCH_VERSION;
	msg->config_version = config_version;
	msg->flags = us->flags;
	msg->fence_time = us->fence_time;
	msg->join_time = join_time;
	strcpy(msg->clustername, cluster_name);
	if (us->fence_agent)
	{
		strcpy(msg->fence_agent, us->fence_agent);
		len += strlen(us->fence_agent)+1;
	}
	else
	{
		msg->fence_agent[0] = '\0';
		len += 1;
	}

	if (have_disallowed())
		msg->flags |= NODE_FLAGS_SEESDISALLOWED;

	comms_send_message(msg, len,
			   0,0,
			   0,  /* multicast */
			   0); /* flags */
}

static void byteswap_internal_message(char *data)
{
	struct cl_protmsg *msg = (struct cl_protmsg *)data;
	struct cl_barriermsg *barriermsg;
	struct cl_killmsg *killmsg;
	struct cl_leavemsg *leavemsg;
	struct cl_transmsg *transmsg;
	struct cl_fencemsg *fencemsg;
	struct cl_reconfig_msg *reconfmsg;

	switch (msg->cmd) {
	case CLUSTER_MSG_PORTOPENED:
	case CLUSTER_MSG_PORTCLOSED:
		/* Just a byte */
		break;

	case CLUSTER_MSG_TRANSITION:
		transmsg = (struct cl_transmsg *)data;
		transmsg->cluster_id = swab16(transmsg->cluster_id);
		transmsg->votes = swab32(transmsg->votes);
		transmsg->expected_votes = swab32(transmsg->expected_votes);
		transmsg->major_version = swab32(transmsg->major_version);
		transmsg->minor_version = swab32(transmsg->minor_version);
		transmsg->patch_version = swab32(transmsg->patch_version);
		transmsg->config_version = swab32(transmsg->config_version);
		transmsg->flags = swab32(transmsg->flags);
		transmsg->fence_time = swab64(transmsg->fence_time);
		break;

	case CLUSTER_MSG_KILLNODE:
		killmsg = (struct cl_killmsg *)data;
		killmsg->reason = swab16(killmsg->reason);
		killmsg->nodeid = swab32(killmsg->nodeid);
		break;

	case CLUSTER_MSG_LEAVE:
		leavemsg = (struct cl_leavemsg *)data;
		leavemsg->reason = swab16(leavemsg->reason);
		break;

	case CLUSTER_MSG_BARRIER:
		barriermsg = (struct cl_barriermsg *)data;
		break;

	case CLUSTER_MSG_RECONFIGURE:
		reconfmsg = (struct cl_reconfig_msg *)data;
		reconfmsg->nodeid = swab32(reconfmsg->nodeid);
		reconfmsg->value = swab32(reconfmsg->value);
		break;

	case CLUSTER_MSG_FENCESTATUS:
		fencemsg = (struct cl_fencemsg *)data;
		fencemsg->timesec = swab64(fencemsg->timesec);
		fencemsg->nodeid = swab32(fencemsg->nodeid);
		break;
	}
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
		recalculate_quorum(1, 0);  /* Allow decrease */
		break;

	case RECONFIG_PARAM_NODE_VOTES:
		node->votes = msg->value;
		recalculate_quorum(1, 0);  /* Allow decrease */
		break;

	case RECONFIG_PARAM_CONFIG_VERSION:
		if (config_version != msg->value) {
			reread_config(msg->value);
		}
		notify_listeners(NULL, EVENT_REASON_CONFIG_UPDATE, config_version);
		break;
	}
}

static void do_fence_msg(void *data)
{
	struct cl_fencemsg *msg = data;
	struct cluster_node *node;

	P_DAEMON("got FENCE message, node %d fenced by %s\n", msg->nodeid, msg->agent);

	node = find_node_by_nodeid(msg->nodeid);
	if (!node)
		return;

	node->fence_time = msg->timesec;
	if (node->fence_agent)
		free(node->fence_agent);
	node->fence_agent = strdup(msg->agent);
	if (msg->fenced) {
		node->flags |= NODE_FLAGS_FENCED;

		if (node->state == NODESTATE_MEMBER)
			node->flags |= NODE_FLAGS_FENCEDWHILEUP;
	}

}

static void do_process_transition(int nodeid, char *data)
{
	struct cl_transmsg *msg = (struct cl_transmsg *)data;
	struct cluster_node *node;

	if (valid_transition_msg(nodeid, msg) != 0) {
		P_MEMB("Transition message from %d does not match current config - should quit ?\n", nodeid);
		// Now what ??
		return;
	}

	/* If the remote node can see AISONLY nodes then we can't join as we don't
	   know the full state */
	if (msg->flags & NODE_FLAGS_SEESDISALLOWED && !have_disallowed()) {
		/* Must use syslog directly here or the message will never arrive */
		syslog(LOG_CRIT, "CMAN: Joined a cluster with disallowed nodes. must die");
		exit(2);
	}
	msg->flags &= ~NODE_FLAGS_SEESDISALLOWED;

	node = find_node_by_nodeid(nodeid);
	if (!node) {
		add_ais_node(nodeid, incarnation, num_ais_nodes);
		node = find_node_by_nodeid(nodeid);
	}
	assert(node);

	P_MEMB("Got TRANSITION message. msg->flags=%x, node->flags=%x, first_trans=%d\n",
	       msg->flags, node->flags, msg->first_trans);

	/* Newer nodes 6.1.0 onwards, set the DIRTY flag if they have state. If the new node has been down
	   and has state then we mark it disallowed because we cannot merge stateful nodes */
	if ( (msg->flags & NODE_FLAGS_DIRTY && (node->flags & NODE_FLAGS_BEENDOWN)) ||
	     (msg->flags & NODE_FLAGS_DIRTY && msg->first_trans && !node->us)) {
		/* Don't duplicate messages */
		if (node->state != NODESTATE_AISONLY) {
			if (cluster_is_quorate) {
				P_MEMB("Killing node %s because it has rejoined the cluster with existing state", node->name);
				log_printf(LOG_CRIT, "Killing node %s because it has rejoined the cluster with existing state", node->name);
				node->state = NODESTATE_AISONLY;
				send_kill(nodeid, CLUSTER_KILL_REJOIN);
			}
			else {
				P_MEMB("Node %s not joined to cman because it has existing state", node->name);
				log_printf(LOG_CRIT, "Node %s not joined to cman because it has existing state", node->name);
				node->state = NODESTATE_AISONLY;
			}
		}
		return;
	}

        /* This is for older nodes. If the join_time of the node matches that already stored AND
	   the node has been down, then we kill it as this must be a rejoin */
	if (msg->minor_version == 0 &&
	    msg->join_time == node->cman_join_time && node->flags & NODE_FLAGS_BEENDOWN) {
		/* Don't duplicate messages */
		if (node->state != NODESTATE_AISONLY) {
			if (cluster_is_quorate) {
				P_MEMB("Killing node %s because it has rejoined the cluster without cman_tool join", node->name);
				log_printf(LOG_CRIT, "Killing node %s because it has rejoined the cluster without cman_tool join", node->name);
				node->state = NODESTATE_AISONLY;
				send_kill(nodeid, CLUSTER_KILL_REJOIN);
			}
			else {
				P_MEMB("Node %s not joined to cman because it has rejoined an inquorate cluster", node->name);
				log_printf(LOG_CRIT, "Node %s not joined to cman because it has rejoined an inquorate cluster", node->name);
				node->state = NODESTATE_AISONLY;
			}
		}
		return;
	}
	else {
		node->cman_join_time = msg->join_time;
		add_ais_node(nodeid, incarnation, num_ais_nodes);
	}

	/* If the cluster already has some AISONLY nodes then we can't make
	   sense of the membership. So the new node has to also be AISONLY
	   until we are consistent again */
	if (have_disallowed() && !node->us)
		node->state = NODESTATE_AISONLY;

	node->flags = msg->flags; /* This will clear the BEENDOWN flag of course */

	/* Take into account any new expected_votes value that the new node has */
	node->expected_votes = msg->expected_votes;
	recalculate_quorum(0, 0);

	if (node->fence_agent && msg->fence_agent[0] && strcmp(node->fence_agent, msg->fence_agent))
	{
		free(node->fence_agent);
		node->fence_agent = strdup(msg->fence_agent);
		node->fence_time = msg->fence_time;
	}

	/*
	 * If this is a rejoined node then it won't know about its own fence data, send it
	 * some
	 */
	if (node->fence_time && !msg->fence_time &&
	    node->fence_agent && !msg->fence_agent[0])
	{
		char msg[sizeof(struct cl_fencemsg)+strlen(node->fence_agent)+1];
		struct cl_fencemsg *fence_msg = (struct cl_fencemsg *)msg;

		fence_msg->cmd = CLUSTER_MSG_FENCESTATUS;
		fence_msg->nodeid = nodeid;
		fence_msg->timesec = node->fence_time;
		fence_msg->fenced = 0;
		strcpy(fence_msg->agent, node->fence_agent);
		comms_send_message(msg, sizeof(msg), 0,0, nodeid, 0);
	}
}

static void process_internal_message(char *data, int nodeid, int need_byteswap)
{
	struct cl_protmsg *msg = (struct cl_protmsg *)data;
	struct cl_portmsg *portmsg;
	struct cl_barriermsg *barriermsg;
	struct cl_killmsg *killmsg;
	struct cl_leavemsg *leavemsg;
	struct cluster_node *node = find_node_by_nodeid(nodeid);
	unsigned char portresult[PORT_BITS_SIZE+1];

	P_MEMB("Message on port 0 is %d\n", msg->cmd);

	/* Byteswap messages if needed */
	if (need_byteswap)
		byteswap_internal_message(data);

	switch (msg->cmd) {
	case CLUSTER_MSG_PORTOPENED:
		portmsg = (struct cl_portmsg *)data;
		if (node)
			set_port_bit(node, portmsg->port);
		post_port_event(EVENT_REASON_PORTOPENED, portmsg->port, nodeid);
		break;

	case CLUSTER_MSG_PORTCLOSED:
		portmsg = (struct cl_portmsg *)data;
		if (node)
			clear_port_bit(node, portmsg->port);
		post_port_event(EVENT_REASON_PORTCLOSED, portmsg->port, nodeid);
		break;

	case CLUSTER_MSG_PORTENQ:
		portresult[0] = CLUSTER_MSG_PORTSTATUS;
		memcpy(portresult+1, us->port_bits, PORT_BITS_SIZE);
		P_MEMB("Sending PORTRESULT, low bytes = %x %x\n", us->port_bits[0], us->port_bits[1]);

		/* Broadcast reply as other new nodes may be interested */
		comms_send_message(portresult, PORT_BITS_SIZE+1, 0,0, 0, 0);
		break;

	case CLUSTER_MSG_PORTSTATUS:
		if (nodeid != us->node_id) {
			P_MEMB("got PORTRESULT from %d, low bytes = %x %x\n", nodeid, data[1], data[2]);
			if (node)
				memcpy(node->port_bits, data+1, PORT_BITS_SIZE);
		}
		break;

	case CLUSTER_MSG_TRANSITION:
		P_MEMB("got TRANSITION from node %d\n", nodeid);
		do_process_transition(nodeid, data);
		break;

	case CLUSTER_MSG_KILLNODE:
		killmsg = (struct cl_killmsg *)data;
		P_MEMB("got KILL for node %d\n", killmsg->nodeid);
		if (killmsg->nodeid == wanted_nodeid) {
			/* Must use syslog directly here or the message will never arrive */
			syslog(LOG_CRIT, "cman killed by node %d because %s\n", nodeid,
			       killmsg_reason(killmsg->reason));
			exit(1);
		}
		break;

	case CLUSTER_MSG_LEAVE:
		leavemsg = (struct cl_leavemsg *)data;
		P_MEMB("got LEAVE from node %d, reason = %d\n", nodeid, leavemsg->reason);

		/* We got our own leave message back. now quit */
		if (node && node->node_id == us->node_id) {
			/* Tell whomever asked us to leave that we are now going down */
			if (shutdown_con)
				send_status_return(shutdown_con, CMAN_CMD_TRY_SHUTDOWN, 0);
			exit(0);
		}

		/* Someone else, make a note of the reason for leaving */
		if (node)
			node->leave_reason = leavemsg->reason;

		/* Mark it as leaving, and remove it when we get an AIS node down event for it */
		if (node && (node->state == NODESTATE_MEMBER || node->state == NODESTATE_AISONLY))
			node->state = NODESTATE_LEAVING;
		break;

	case CLUSTER_MSG_BARRIER:
		barriermsg = (struct cl_barriermsg *)data;
		if (node)
			process_barrier_msg(barriermsg, node);
		break;

	case CLUSTER_MSG_RECONFIGURE:
		do_reconfigure_msg(data);
		break;

	case CLUSTER_MSG_FENCESTATUS:
		do_fence_msg(data);
		break;

	default:
		log_printf(LOG_WARNING, "Unknown protocol message %d received\n", msg->cmd);
		break;

	}
}

void override_expected(int newexp)
{
	struct list *nodelist;
	struct cluster_node *node;

	list_iterate(nodelist, &cluster_members_list) {
		node = list_item(nodelist, struct cluster_node);
		if (node->state == NODESTATE_MEMBER
		    && node->expected_votes > newexp) {
			node->expected_votes = newexp;
		}
	}
}

void clear_reread_flags()
{
	struct list *nodelist;
	struct cluster_node *node;

	list_iterate(nodelist, &cluster_members_list) {
		node = list_item(nodelist, struct cluster_node);
		node->flags &= ~NODE_FLAGS_REREAD;
	}
}

void remove_unread_nodes()
{
	struct list *nodelist, *tmp;
	struct cluster_node *node;

	list_iterate_safe(nodelist, tmp, &cluster_members_list) {
		node = list_item(nodelist, struct cluster_node);
		if (!(node->flags & NODE_FLAGS_REREAD) &&
		    node->state == NODESTATE_DEAD) {

			list_del(&node->list);
			free(node);
		}
	}
}

/* Add a node from CCS, note that it may already exist if user has simply updated the config file */
void add_ccs_node(char *nodename, int nodeid, int votes, int expected_votes)
{
	/* Update node entry */
	add_new_node(nodename, nodeid, votes, expected_votes, NODESTATE_DEAD);
}

void add_ais_node(int nodeid, uint64_t incarnation, int total_members)
{
	struct cluster_node *node;

	P_MEMB("add_ais_node ID=%d, incarnation = %" PRIu64 "\n",nodeid, incarnation);

	node = find_node_by_nodeid(nodeid);
	if (!node && total_members == 1) {
		node = us;
		P_MEMB("Adding AIS node for 'us'\n");
	}

 	/* This really should exist!! */
	if (!node) {
		char tempname[256];
		log_printf(LOG_ERR, "Got node from AIS id %d with no config entry\n", nodeid);

		/* Emergency nodename */
		sprintf(tempname, "Node%d", nodeid);
		node = add_new_node(tempname, nodeid, 1, total_members, NODESTATE_DEAD);
	}

	if (node->state == NODESTATE_DEAD || node->state == NODESTATE_LEAVING) {
		gettimeofday(&node->join_time, NULL);
		node->incarnation = incarnation;
		node->state = NODESTATE_MEMBER;
		cluster_members++;
		recalculate_quorum(0, 0);
	}
}

void del_ais_node(int nodeid)
{
	struct cluster_node *node;
	time_t t;
	P_MEMB("del_ais_node %d\n", nodeid);

	node = find_node_by_nodeid(nodeid);
	if (!node)
		return;

	/* If the node was fenced while up (ie independantly of fenced) then
	 * don't clear the fenced flag. There is a timeout associated with
	 * this so if we get the node down more than 2 minutes after the
	 * fence message then we still clear fenced just to be certain that
	 * fenced will do the job too.
	 */
	time(&t);
	if (!(node->flags & NODE_FLAGS_FENCEDWHILEUP) || (t - node->fence_time > 120))
		node->flags &= ~NODE_FLAGS_FENCED;

	node->flags &= ~NODE_FLAGS_FENCEDWHILEUP;
	node->flags |= NODE_FLAGS_BEENDOWN;

	switch (node->state) {
	case NODESTATE_MEMBER:
		node->state = NODESTATE_DEAD;
		memset(&node->port_bits, 0, sizeof(node->port_bits));
		cluster_members--;
		recalculate_quorum(0, 0);
		break;

	case NODESTATE_AISONLY:
		node->state = NODESTATE_DEAD;
		break;

	case NODESTATE_LEAVING:
		node->state = NODESTATE_DEAD;
		cluster_members--;

		P_MEMB("del_ais_node %s, leave_reason=%x\n", node->name, node->leave_reason);
		if ((node->leave_reason & 0xF) == CLUSTER_LEAVEFLAG_REMOVED)
			recalculate_quorum(1, 1);
		else
			recalculate_quorum(0, 0);
		break;

	case NODESTATE_JOINING:
	case NODESTATE_DEAD:
		break;
	}
}

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

static struct cluster_node *find_node_by_nodeid(int nodeid)
{
	struct cluster_node *node;

	list_iterate_items(node, &cluster_members_list) {
		if (node->node_id == nodeid)
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

static char *killmsg_reason(int reason)
{
	static char msg[1024];

	switch (reason)
	{
	case CLUSTER_KILL_REJECTED:
		return "our membership application was rejected";

	case CLUSTER_KILL_CMANTOOL:
		return "we were killed by cman_tool or other application";

	case CLUSTER_KILL_REJOIN:
		return "we rejoined the cluster without a full restart";

	default:
		sprintf(msg, "we got kill message number %d", reason);
		return msg;
	}
}
