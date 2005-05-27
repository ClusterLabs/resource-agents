/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

/*
   openais details:

   - All nodes in one cluster must be set to run on the same port
     and have the same /etc/authkey.  There is no unique name string
     that identifies a cluster.  We may be able to use the port
     number as the name since all nodes running openais on that
     port should always be a part of the same cluster.

   - A node's only id is its IPv4 address that is used for openais comms.

   - The dlm will use the same network as openais because it needs to
     get the nodeid/addr pairs for all nodes from openais.

   - Groupd is running on all nodes that are running openais.

   Quorum:
   - Assert that every node has one vote.
   - Provide expected_votes (E) as command line arg to groupd.
   - Require all groupd's to have same expected_votes.
   - Groupd will only recognize the first E nodes that join the cluster.
   - Groupd keeps record of these first E nodes even after they leave.

*/


#include "gd_internal.h"
#include "evs.h"

extern struct list_head gd_nodes;
extern int              gd_node_count;
extern int              gd_member_count;
extern int              gd_quorate;
extern int              gd_nodeid;

static evs_handle_t	eh;
static struct in_addr	cluster_nodes[MAX_NODES];
static int		cluster_count;

static int		member_cb;
static int		message_cb;
static int		message_nodeid;
static int		message_len;
static char		message_buf[MAX_MSGLEN];

static struct evs_group egroup;


int wait_for_groupd(void)
{
	/* wait until we can communicate via messages with other
	   groupd's on all cluster members */
}

node_t *find_node(int nodeid)
{
	node_t *node;

	list_for_each_entry(node, &gd_nodes, list) {
		if (node->id == nodeid)
			return node;
	}
	return NULL;
}

static struct in_addr *find_cluster_node(int nodeid)
{
	int i;

	for (i = 0; i < cluster_count; i++) {
		if (cluster_nodes[i].s_addr == nodeid)
			return &cluster_nodes[i];
	}
	return NULL;
}

static int process_cluster_nodes(void)
{
	node_t *node;
	struct in_addr *cn;
	int i, rv, sub = 0, add = 0;

	/* find who's gone */

	list_for_each_entry(node, &gd_nodes, list) {

		cn = find_cluster_node(node->id);

		if (cn) {
			if (!test_bit(NFL_CLUSTER_MEMBER, &node->flags)) {
				/* former member is back */
				set_bit(NFL_CLUSTER_MEMBER, &node->flags);
				add++;
				gd_member_count++;
				log_debug("member re-added %d", node->id);
			} else {
				/* current member is still alive */
			}
		} else {
			/* current member has died */
			if (test_bit(NFL_CLUSTER_MEMBER, &node->flags)) {
				clear_bit(NFL_CLUSTER_MEMBER, &node->flags);
				set_bit(NFL_NEED_RECOVERY, &node->flags);
				sub++;
				gd_member_count--;
				log_debug("member removed %d", node->id);
			}
		}
	}

	/* find who's new */

	for (i = 0; i < cluster_count; i++) {

		node = find_node(cluster_nodes[i].s_addr);
		if (node)
			continue;

#if 0
		/* this is a bit lame, but for now we require a new
		   member to start up groupd right away because we
		   don't have a good way of dealing with cluster
		   members who aren't running groupd. */

		rv = wait_for_groupd(cluster_nodes[i].s_addr);
		if (rv)
			continue;
#endif
		node = new_node(cluster_nodes[i].s_addr);
		set_bit(NFL_CLUSTER_MEMBER, &node->flags);
		add++;
		list_add_tail(&node->list, &gd_nodes);
		gd_node_count++;
		gd_member_count++;
		log_debug("member added %d", node->id);
	}

	return sub;
}

static void process_member(void)
{
	node_t *node;
	int rv, gone;

#if 0
	quorate = cman_is_quorate(ch);
	gd_quorate = quorate;
#endif
	gd_quorate = 1;

	/* Update our own gd_nodes list and determine if any nodes are gone.
	   If so, process_nodechange() applies these changes to groups */

	gone = process_cluster_nodes();
	if (gone > 0)
		process_nodechange();

	list_for_each_entry(node, &gd_nodes, list)
		clear_bit(NFL_NEED_RECOVERY, &node->flags);
}

static void member_callback(struct in_addr *member_list,
			    int member_list_entries,
			    struct in_addr *left_list,
			    int left_list_entries,
			    struct in_addr *joined_list,
			    int joined_list_entries)
{
	int i;

	log_in("member callback");

	member_cb = 1;

	printf("member list\n");
	for (i = 0; i < member_list_entries; i++) {
		printf("%s\n", inet_ntoa(member_list[i]));
		cluster_nodes[i] = member_list[i];
	}
	cluster_count = member_list_entries;

	printf("left list\n");
	for (i = 0; i < left_list_entries; i++)
		printf("%s\n", inet_ntoa(left_list[i]));

	printf("joined list\n");
	for (i = 0; i < joined_list_entries; i++)
		printf("%s\n", inet_ntoa(joined_list[i]));

	process_member();
}

static void message_callback(struct in_addr source_addr, void *msg, int len)
{
	log_in("message callback nodeid %d len %d",
	       (int) source_addr.s_addr, len);

	message_cb = 1;
	memcpy(message_buf, msg, len);
	message_len = len;
	message_nodeid = (int) source_addr.s_addr;

	process_message(message_buf, message_len, message_nodeid);
}

int process_member_message(void)
{
	int rv;

	rv = evs_dispatch(eh, EVS_DISPATCH_ALL);
	if (rv != EVS_OK)
		log_print("evs_dispatch error %d %d", rv, errno);

	return 0;
}

int set_our_nodeid(void)
{
	struct in_addr addr;
	int member_list_entries = 0;

	evs_membership_get(eh, &addr, NULL, &member_list_entries);

	gd_nodeid = addr.s_addr;

	return 0;
}

evs_callbacks_t callbacks = {
	message_callback,	/* deliver_fn */
	member_callback		/* confchg_fn */
};

int setup_member_message(void)
{
	int rv, fd;

	INIT_LIST_HEAD(&gd_barriers);
	INIT_LIST_HEAD(&gd_nodes);
	gd_node_count = 0;
	gd_member_count = 0;

	rv = evs_initialize(&eh, &callbacks);
	if (rv != EVS_OK) {
		log_print("evs_initialize error %d %d", rv, errno);
		return -ENOTCONN;
	}

	rv = evs_fd_get(eh, &fd);
	if (rv != EVS_OK) {
		log_print("evs_fd_get error %d %d", rv, errno);
		return rv;
	}

	rv = set_our_nodeid();
	if (rv < 0) {
		evs_finalize(eh);
		fd = rv;
		goto out;
	}

	memset(&egroup, 0, sizeof(egroup));
	sprintf(egroup.key, "groupd");

	rv = evs_join(eh, &egroup, 1);
	if (rv != EVS_OK) {
		evs_finalize(eh);
		fd = rv;
	}

 out:
	return fd;
}

int send_nodeid_message(char *buf, int len, int nodeid)
{
	msg_t *msg = (msg_t *) buf;
	struct iovec iov = { buf, len };
	int error = 0;

	msg->ms_to_nodeid = nodeid;

	if (nodeid == gd_nodeid) {
		process_message(buf, len, nodeid);
		goto out;
	}

	do {
		error = evs_mcast_groups(eh, EVS_TYPE_AGREED, &egroup, 1,
					 &iov, 1);
	} while (error == EVS_ERR_TRY_AGAIN);

 out:
	return 0;
}

int send_broadcast_message(char *buf, int len)
{
	msg_t *msg = (msg_t *) buf;
	struct iovec iov = { buf, len };
	int error;

	msg->ms_to_nodeid = 0;

	do {
		error = evs_mcast_groups(eh, EVS_TYPE_AGREED, &egroup, 1,
					 &iov, 1);
	} while (error == EVS_ERR_TRY_AGAIN);

	return 0;
}

/* FIXME: should probably use an evs_group per group_t for sending
   group-specific messages.  For now, nodes without this group should should
   just ignore the message when they can't find the given group_id. */

int send_members_message(group_t *g, char *buf, int len)
{
	return send_broadcast_message(buf, len);
}

int send_members_message_ev(group_t *g, char *buf, int len, event_t *ev)
{
	int error;
	msg_t *msg = (msg_t *) buf;

	set_allowed_msgtype(ev, msg->ms_type);
	ev->reply_count = 0;

	error = send_members_message(g, buf, len);
	if (error < 0)
		clear_allowed_msgtype(ev, msg->ms_type);

	return error;
}

int send_broadcast_message_ev(char *buf, int len, event_t *ev)
{
	int error;
	msg_t *msg = (msg_t *) buf;

	set_allowed_msgtype(sev, msg->ms_type);
	ev->reply_count = 0;

	error = send_broadcast_message(buf, len);
	if (error < 0)
		clear_allowed_msgtype(ev, msg->ms_type);

	return error;
}

