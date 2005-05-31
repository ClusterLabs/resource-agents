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

#include "gd_internal.h"
#include "libcman.h"

extern struct list_head	gd_nodes;
extern int		gd_node_count;
extern int		gd_member_count;
extern int		gd_quorate;
extern int		gd_nodeid;

static cman_handle_t	ch;
static cman_node_t	cluster_nodes[MAX_NODES];
static int		cluster_count;
static int		cluster_generation;

static int		member_cb;
static int		member_reason;
static int		message_cb;
static int		message_nodeid;
static int		message_len;
static char		message_buf[MAX_MSGLEN];


node_t *find_node(int nodeid)
{
	node_t *node;

	list_for_each_entry(node, &gd_nodes, list) {
		if (node->id == nodeid)
			return node;
	}
	return NULL;
}

static int wait_for_groupd(int nodeid)
{
	cman_node_t cn;
	int rv;

	while (1) {
		if (cman_is_listening(ch, nodeid, GROUPD_PORT)) {
			rv = 0;
			break;
		}

		rv = cman_get_node(ch, nodeid, &cn);
		if (rv < 0) {
			log_print("no status for new node %d", nodeid);
			break;
		}

		if (!cn.cn_member) {
			log_print("new member %d failed", nodeid);
			rv = -1;
			break;
		}

		log_print("waiting for groupd on new member %d", nodeid);
		sleep(1);
	}
	return rv;
}

static cman_node_t *find_cluster_node(int nodeid)
{
	int i;

	for (i = 0; i < cluster_count; i++) {
		if (cluster_nodes[i].cn_nodeid == nodeid)
			return &cluster_nodes[i];
	}
	return NULL;
}

static int process_cluster_nodes(void)
{
	node_t *node;
	cman_node_t *cn;
	int i, rv, sub = 0, add = 0;


	/* find who's gone */

	list_for_each_entry(node, &gd_nodes, list) {

		cn = find_cluster_node(node->id);

		if (cn && cn->cn_member) {
			if (!test_bit(NFL_CLUSTER_MEMBER, &node->flags)) {
				/* former member is back */
				set_bit(NFL_CLUSTER_MEMBER, &node->flags);
				node->incarnation = cn->cn_incarnation;
				add++;
				gd_member_count++;
				log_debug("member re-added %d", node->id);
			} else {
				/* current member is still alive - if the
				   incarnation number is different it died and
				   returned between checks */

				if (node->incarnation != cn->cn_incarnation) {
					set_bit(NFL_NEED_RECOVERY,&node->flags);
					node->incarnation = cn->cn_incarnation;
					sub++;
					log_debug("member in/out %d", node->id);
				}
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

		/* make it easier to find who's new next */
		if (cn)
			cn->cn_member = 0;
	}

	/* find who's new */

	for (i = 0; i < cluster_count; i++) {
		if (!cluster_nodes[i].cn_member)
			continue;

		/* this is a bit lame, but for now we require a new
		   member to start up groupd right away because we
		   don't have a good way of dealing with cluster
		   members who aren't running groupd. */

		rv = wait_for_groupd(cluster_nodes[i].cn_nodeid);
		if (rv)
			continue;

		node = new_node(cluster_nodes[i].cn_nodeid);
		node->incarnation = cluster_nodes[i].cn_incarnation;
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
	cman_cluster_t info1, info2;
	node_t *node;
	int rv, quorate, count, gone;


	/* FIXME: PORTCLOSED indicates the failure of a remote groupd.
	   We should treat this like the complete failure of that node */

 retry:
	rv = cman_get_cluster(ch, &info1);
	if (rv < 0) {
		log_print("cman_get_cluster error %d %d", rv, errno);
		return;
	}

	quorate = cman_is_quorate(ch);

	count = 0;
	memset(&cluster_nodes, 0, sizeof(cluster_nodes));

	rv = cman_get_nodes(ch, MAX_NODES, &count, cluster_nodes);
	if (rv < 0) {
		log_print("cman_get_nodes error %d %d", rv, errno);
		return;
	}

	rv = cman_get_cluster(ch, &info2);
	if (rv < 0) {
		log_print("cman_get_cluster error %d %d", rv, errno);
		return;
	}

	if (info1.ci_generation != info2.ci_generation) {
		log_print("generation mismatch %d %d",
			  info1.ci_generation, info2.ci_generation);
		sleep(1);
		goto retry;
	}

	cluster_generation = info1.ci_generation;
	gd_quorate = quorate;
	cluster_count = count;

	log_debug("member reason %d quorate %d generation %d nodes %d",
		  member_reason, quorate, cluster_generation, count);

	/* Update our own gd_nodes list and determine if any nodes are gone.
	   If so, process_nodechange() applies these changes to groups */

	gone = process_cluster_nodes();
	if (gone > 0)
		process_nodechange();

	list_for_each_entry(node, &gd_nodes, list)
		clear_bit(NFL_NEED_RECOVERY, &node->flags);
}

static void member_callback(cman_handle_t h, void *private, int reason, int arg)
{
	log_debug("member callback reason %d", reason);

	member_cb = 1;
	member_reason = reason;
}

static void message_callback(cman_handle_t h, void *private, char *buf,
			     int len, uint8_t port, int nodeid)
{
	/*
	log_debug("message callback nodeid %d len %d", nodeid, len);
	*/

	message_cb = 1;
	memcpy(message_buf, buf, len);
	message_len = len;
	message_nodeid = nodeid;
}

int process_member_message(void)
{
	int rv = 0;

	while (1) {
		cman_dispatch(ch, CMAN_DISPATCH_ONE);

		if (member_cb) {
			member_cb = 0;
			process_member();
			rv = 1;
		} else if (message_cb) {
			message_cb = 0;
			process_message(message_buf, message_len,
					message_nodeid);
			rv = 1;
		} else
			break;
	}
	return rv;
}

int setup_member_message(void)
{
	cman_node_t node;
	int rv, fd;

	INIT_LIST_HEAD(&gd_nodes);
	gd_node_count = 0;
	gd_member_count = 0;

	ch = cman_init(NULL);
	if (!ch) {
		log_print("cman_init error %d %d", (int) ch, errno);
		return -ENOTCONN;
	}

	rv = cman_start_notification(ch, member_callback);
	if (rv < 0) {
		log_print("cman_start_notification error %d %d", rv, errno);
		cman_finish(ch);
		return rv;
	}

	rv = cman_start_recv_data(ch, message_callback, GROUPD_PORT);
	if (rv < 0) {
		log_print("cman_start_recv_data error %d %d", rv, errno);
		cman_stop_notification(ch);
		cman_finish(ch);
		return rv;
	}

	fd = cman_get_fd(ch);

	/* FIXME: wait here for us to be a member of the cluster */

	rv = cman_get_node(ch, CMAN_NODEID_US, &node);
	if (rv < 0) {
		log_print("cman_get_node us error %d %d", rv, errno);
		cman_end_recv_data(ch);
		cman_stop_notification(ch);
		cman_finish(ch);
		fd = rv;
		goto out;
	}

	gd_nodeid = node.cn_nodeid;
	log_debug("member our nodeid %d", gd_nodeid);

	/* this will just initialize gd_nodes, etc */
	member_reason = CMAN_REASON_STATECHANGE;
	process_member();

 out:
	return fd;
}

int send_nodeid_message(char *buf, int len, int nodeid)
{
	msg_t *msg = (msg_t *) buf;
	int error = 0;

	msg->ms_to_nodeid = nodeid;

	if (nodeid == gd_nodeid) {
		process_message(buf, len, nodeid);
		goto out;
	}

	error = cman_send_data(ch, buf, len, 0, GROUPD_PORT, nodeid);
	if (error < 0)
		log_print("send_nodeid_message error %d to %u", error, nodeid);
	else
		error = 0;
 out:
	return error;
}

int send_broadcast_message(char *buf, int len)
{
	int error;

	error = cman_send_data(ch, buf, len, 0, GROUPD_PORT, 0);
	if (error < 0)
		log_print("send_broadcast_message error %d", error);
	else
		error = 0;

	process_message(buf, len, gd_nodeid);
 out:
	return error;
}

int send_members_message(group_t *g, char *buf, int len)
{
	node_t *node;
	int error = 0;

	list_for_each_entry(node, &g->memb, list) {
		error = send_nodeid_message(buf, len, node->id);
		if (error < 0)
			log_group(g, "send to %d error %d", node->id, error);
	}
	return error;
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

	set_allowed_msgtype(ev, msg->ms_type);
	ev->reply_count = 0;

	error = send_broadcast_message(buf, len);
	if (error < 0)
		clear_allowed_msgtype(ev, msg->ms_type);

	return error;
}

