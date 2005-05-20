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

#include "gd_internal.h"

#define SMSG_BUF_SIZE (sizeof(msg_t) + MAX_NAMELEN + 1)

static uint32_t 	global_last_id;
static char		msg_buf[SMSG_BUF_SIZE];


void print_bytes(char *buf, int len)
{
	int i;
	for (i = 0; i < len; i++)
		printf("%02x ", buf[i]);
	printf("\n");
}

node_t *find_joiner(group_t *g, int nodeid)
{
	node_t *node;

	list_for_each_entry(node, &g->joining, list) {
		if (node->id == nodeid)
			return node;
	}
	return NULL;
}

void add_joiner(group_t *g, int nodeid)
{
	node_t *node;

	node = find_joiner(g, nodeid);
	if (!node) {
		node = new_node(nodeid);
		list_add_tail(&node->list, &g->joining);
	}
}

uint32_t new_global_id(int level)
{
	uint32_t id = global_last_id++;
	uint8_t l = (uint8_t) level;

	if (level > 255)
		return 0;

	if (id > 0x00FFFFFF)
		return 0;

	id |= (l << 24);
	return id;
}

static int save_global_id(event_t *ev, msg_t *msg)
{
	group_t *g = ev->group;

	if (!msg->ms_group_id) {
		log_error(g, "save_global_id: zero sg id");
		return -1;
	}

	if (!g->global_id)
		g->global_id = msg->ms_group_id;

	if (g->global_id != msg->ms_group_id) {
		log_error(g, "save_global_id: id %x", msg->ms_group_id);
		return -1;
	}
	return 0;
}

static void save_lastid(msg_t *msg)
{
	uint32_t gid = msg->ms_last_id & 0x00FFFFFF;

	/*
	 * Keep track of the highst SG id which has been used
	 * in the cluster in case we need to choose a new SG id.
	 */

	if (gid > global_last_id)
		global_last_id = gid;
}

static void msg_copy_in(msg_t *m)
{
	m->ms_event_id	= le16_to_cpu(m->ms_event_id);
	m->ms_group_id	= le32_to_cpu(m->ms_group_id);
	m->ms_last_id	= le32_to_cpu(m->ms_last_id);
	m->ms_to_nodeid	= le32_to_cpu(m->ms_to_nodeid);
	m->ms_level	= le16_to_cpu(m->ms_level);
	m->ms_length	= le16_to_cpu(m->ms_length);
}

static void msg_copy_out(msg_t *m)
{
	m->ms_event_id	= cpu_to_le16(m->ms_event_id);
	m->ms_group_id	= cpu_to_le32(m->ms_group_id);
	m->ms_last_id	= cpu_to_le32(m->ms_last_id);
	m->ms_to_nodeid	= cpu_to_le32(m->ms_to_nodeid);
	m->ms_level	= cpu_to_le16(m->ms_level);
	m->ms_length	= cpu_to_le16(m->ms_length);
}

#if 0
static unsigned int msgtype_to_flag(int type)
{
	unsigned int flag;

	switch (type) {
	case SMSG_JOIN_REP:
	case SMSG_JOIN_REQ:
		flag = EFL_ALLOW_JOIN;
		break;

	case SMSG_JSTOP_REP:
	case SMSG_JSTOP_REQ:
		flag = EFL_ALLOW_JSTOP;
		break;

	case SMSG_LEAVE_REP:
	case SMSG_LEAVE_REQ:
		flag = EFL_ALLOW_LEAVE;
		break;

	case SMSG_LSTOP_REP:
	case SMSG_LSTOP_REQ:
		flag = EFL_ALLOW_LSTOP;
		break;

	default:
		ASSERT(0, log_print("msgtype_to_flag bad type %d", type););
	}
	return flag;
}

static int test_allowed_msgtype(event_t *sev, int type)
{
	unsigned int flag = msgtype_to_flag(type);

	return test_bit(flag, &sev->se_flags);
}

static void clear_allowed_msgtype(event_t *sev, int type)
{
	unsigned int flag = msgtype_to_flag(type);

	clear_bit(flag, &sev->se_flags);
}

static void set_allowed_msgtype(event_t *sev, int type)
{
	unsigned int flag = msgtype_to_flag(type);

	set_bit(flag, &sev->se_flags);
}
#endif

static int next_event_state(int msg_type, int cur_state)
{
	int next = 0;

	switch (msg_type) {
	case SMSG_JOIN_REP:
		ASSERT(cur_state == EST_JOIN_ACKWAIT,);
		next = EST_JOIN_ACKED;
		break;

	case SMSG_JSTOP_REP:
		ASSERT(cur_state == EST_JSTOP_ACKWAIT,);
		next = EST_JSTOP_ACKED;
		break;

	case SMSG_LEAVE_REP:
		ASSERT(cur_state == EST_LEAVE_ACKWAIT,);
		next = EST_LEAVE_ACKED;
		break;

	case SMSG_LSTOP_REP:
		ASSERT(cur_state == EST_LSTOP_ACKWAIT,);
		next = EST_LSTOP_ACKED;
		break;
	}
	return next;
}

/*
 * Functions in sevent.c send messages to other nodes and then expect replies.
 * This function collects the replies for the sevent messages and moves the
 * sevent to the next stage when all the expected replies have been received.
 */

static void process_reply(msg_t *msg, int nodeid)
{
	group_t *g;
	event_t *ev;
	int i, expected, type = msg->ms_type, found = 0;

	ev = find_event(msg->ms_event_id);
	if (!ev) {
		log_print("process_reply invalid id=%u nodeid=%u",
			  msg->ms_event_id, nodeid);
		goto out;
	}
	g = ev->group;

	/*
	if (!test_allowed_msgtype(ev, type)) {
		log_debug(g, "process_reply ignored type %u from %u id %u",
			  type, nodeid, ev->id);
		goto out;
	}
	*/

	expected = (type == SMSG_JOIN_REP) ? ev->node_count : ev->memb_count;

	ASSERT(expected > 0, );

	ASSERT(expected * sizeof(uint32_t) <= ev->len_ids,
	       log_print("type=%d expected=%d len_ids=%d node_count=%d "
			 "memb_count=%d", type, expected, ev->len_ids,
			 ev->node_count, ev->memb_count););

	ASSERT(expected * sizeof(char) <= ev->len_status,
	       log_print("type=%d expected=%d len_status=%d node_count=%d "
			 "memb_count=%d\n", type, expected, ev->len_status,
			 ev->node_count, ev->memb_count););

	for (i = 0; i < expected; i++) {
		if (ev->node_ids[i] != nodeid)
			continue;

		/*
		 * Save the status from the replying node
		 */

		if (!ev->node_status[i])
			ev->node_status[i] = msg->ms_status;
		else {
			log_error(g, "process_reply dup id %u from %u %u/%u",
				  ev->id, nodeid, ev->node_status[i],
				  msg->ms_status);
			goto out;
		}

		if (type == SMSG_JOIN_REP) {
			save_lastid(msg);
			if (msg->ms_status == STATUS_POS)
				save_global_id(ev, msg);
		}

		if (++ev->reply_count == expected) {
			/* clear_allowed_msgtype(ev, type); */
			ev->state = next_event_state(type, ev->state);
		}

		log_group(g, "reply type %d from %d is %d of %d state %d",
			  type, nodeid, ev->reply_count, expected, ev->state);
		found = 1;
		break;
	}

	if (!found)
		log_group(g, "reply %d not expected from %d", type, nodeid);
 out:
	return;
}

/*
 * A node wants to join an SG and has run send_join_notice.  If we know nothing
 * about the SG , then we have no objection - send back STATUS_POS.  If we're a
 * member of the SG, then send back STATUS_POS (go ahead and join) if there's
 * no sevent or uevent of higher priority in progress (only a single join or
 * leave is permitted for the SG at once).  If there happens to be a higher
 * priority sevent/uevent in progress, send back STATUS_WAIT to defer the
 * requested join for a bit.
 */

static void process_join_request(msg_t *msg, int nodeid, char *name)
{
	group_t *g = NULL;
	event_t *ev = NULL;
	node_t *node;
	int found = FALSE;
	int level = msg->ms_level;
	msg_t reply;

	memset(&reply, 0, sizeof(reply));

	if (nodeid == gd_nodeid)
		goto next;

	g = find_group_level(name, level);

	/*
	 * build reply message
	 */

      next:

	if (!g) {
		reply.ms_type = SMSG_JOIN_REP;
		reply.ms_status = STATUS_NEG;
		reply.ms_last_id = global_last_id;
		reply.ms_event_id = msg->ms_event_id;
	} else {
		reply.ms_type = SMSG_JOIN_REP;
		reply.ms_status = STATUS_POS;
		reply.ms_event_id = msg->ms_event_id;
		reply.ms_group_id = g->global_id;
		reply.ms_last_id = global_last_id;

		/*
		 * The node trying to join should wait and try again until
		 * we're done with recovery.
		 */

		if (g->state == GST_RECOVER) {
			reply.ms_status = STATUS_WAIT;
			goto send;
		}

		/*
		 * An sevent node trying to join may have gotten as far as
		 * creating a uevent with us and then backed out.  That node
		 * will retry joining from the beginning so we should not turn
		 * them away.  If we're handling a uevent for another node,
		 * tell the joining node to wait.
		 */

		if (in_update(g)) {
			if (g->update->nodeid != nodeid)
				reply.ms_status = STATUS_WAIT;
			goto send;
		}

		/*
		 * We're trying to join or leave the SG at the moment.
		 */

		if (in_event(g)) {
			ev = g->event;

			/*
			 * We're trying to leave.  Make the join wait until
			 * we've left if we're beyond LEAVE_ACKWAIT.
			 */

			if (test_bit(GFL_LEAVING, &g->flags)) {
				if (ev->state > EST_LEAVE_ACKED)
					reply.ms_status = STATUS_WAIT;
				else {
					reply.ms_status = STATUS_POS;
					clear_bit(EFL_ALLOW_LEAVE, &ev->flags);
					set_bit(EFL_CANCEL, &ev->flags);
				}
			}

			/*
			 * We're trying to join.  Making the other join wait
			 * until we're joined if we're beyond JOIN_ACKWAIT or
			 * if we have a lower id.  (Send NEG to allow the other
			 * node to go ahead because we're not in the SG.)
			 */

			else {
				if (ev->state > EST_JOIN_ACKED)
					reply.ms_status = STATUS_WAIT;
				else if (gd_nodeid < nodeid)
					reply.ms_status = STATUS_WAIT;
				else {
					reply.ms_status = STATUS_NEG;
					clear_bit(EFL_ALLOW_JOIN, &ev->flags);
					set_bit(EFL_CANCEL, &ev->flags);
				}
			}

			goto send;
		}

		/* no r,u,s event, stick with STATUS_POS */
	}

 send:

	if (reply.ms_status == STATUS_POS)
		add_joiner(g, nodeid);

	/* msg_bswap_out(&reply); */
	send_nodeid_message((char *) &reply, sizeof(reply), nodeid);
}

/*
 * Another node wants us to stop a service so it can join or leave the SG.  We
 * do this by saving the request info in a uevent and having the sm thread do
 * the processing and then replying.
 */

static void process_stop_request(msg_t *msg, int nodeid, uint32_t *msgbuf)
{
	group_t *g;
	update_t *up;
	msg_t reply;
	int type = msg->ms_type;

	if (nodeid == gd_nodeid)
		goto agree;

	g = find_group_id(msg->ms_group_id);
	if (!g) {
		log_print("process_stop_request: unknown group id %x",
			  msg->ms_group_id);
		return;
	}

	/*
	 * We shouldn't get here with uevent already set.
	 */

	if (in_update(g)) {
		log_error(g, "process_stop_request: update already set");
		return;
	}

	up = malloc(sizeof(*up));
	memset(up, 0, sizeof(*up));
	up->nodeid = nodeid;
	up->remote_seid = msg->ms_event_id;
	up->state = (type == SMSG_JSTOP_REQ) ? UST_JSTOP : UST_LSTOP;

	g->update = up;
	set_bit(GFL_UPDATE, &g->flags);

	if (type == SMSG_JSTOP_REQ) {
		up->num_nodes = ntohl(*msgbuf);
		ASSERT(up->num_nodes, );
	} else
		set_bit(UFL_LEAVE, &up->flags);

	/*
	 * Do process_join_stop() or process_leave_stop().
	 */

	return;

 agree:
	reply.ms_status = STATUS_POS;
	reply.ms_type = (type == SMSG_JSTOP_REQ) ? SMSG_JSTOP_REP : SMSG_LSTOP_REP;
	reply.ms_event_id = msg->ms_event_id;
	/* msg_bswap_out(&reply); */
	send_nodeid_message((char *) &reply, sizeof(reply), nodeid);
}

static void process_start_request(msg_t *msg, int nodeid)
{
	group_t *g;
	update_t *up;
	int type = msg->ms_type;

	if (nodeid == gd_nodeid)
		return;

	g = find_group_id(msg->ms_group_id);
	if (!g) {
		log_print("process_start_request: unknown sg id %x",
			  msg->ms_group_id);
		return;
	}

	if (!in_update(g)) {
		log_error(g, "process_start_request: no update");
		return;
	}

	up = g->update;

	if (type == SMSG_JSTART_CMD)
		up->state = UST_JSTART;
	else
		up->state = UST_LSTART;
}

static void process_leave_request(msg_t *msg, int nodeid)
{
	group_t *g;
	node_t *node;
	msg_t reply;
	event_t *ev;
	int found = FALSE;

	g = find_group_id(msg->ms_group_id);
	if (g) {
		if (nodeid == gd_nodeid)
			found = TRUE;
		else {
			list_for_each_entry(node, &g->memb, list) {
				if (node->id != nodeid)
					continue;
				set_bit(NFL_LEAVING, &node->flags);
				found = TRUE;
				break;
			}
		}
	}

	if (!found) {
		reply.ms_type = SMSG_LEAVE_REP;
		reply.ms_status = STATUS_NEG;
		reply.ms_event_id = msg->ms_event_id;
	} else {
		reply.ms_type = SMSG_LEAVE_REP;
		reply.ms_status = STATUS_POS;
		reply.ms_event_id = msg->ms_event_id;

		if (g->state == GST_RECOVER)
			reply.ms_status = STATUS_WAIT;

		else if (in_event(g) && nodeid != gd_nodeid ){
			ev = g->event;

			/*
			 * We're trying to join or leave at the moment.  If
			 * we're past JOIN/LEAVE_ACKWAIT, we make the requestor
			 * wait.  Otherwise, if joining we'll cancel to let the
			 * leave happen first, or if we're leaving allow the
			 * lower nodeid to leave first.
			 */

			if (test_bit(GFL_LEAVING, &g->flags)) {
				if (ev->state > EST_LEAVE_ACKWAIT)
					reply.ms_status = STATUS_WAIT;
				else if (gd_nodeid < nodeid)
					reply.ms_status = STATUS_WAIT;
				else {
					reply.ms_status = STATUS_POS;
					clear_bit(EFL_ALLOW_LEAVE, &ev->flags);
					set_bit(EFL_CANCEL, &ev->flags);
				}
			} else {
				if (ev->state > EST_JOIN_ACKWAIT)
					reply.ms_status = STATUS_WAIT;
				else {
					reply.ms_status = STATUS_NEG;
					clear_bit(EFL_ALLOW_JOIN, &ev->flags);
					set_bit(EFL_CANCEL, &ev->flags);
				}
			}
		}

		else if (test_bit(GFL_UPDATE, &g->flags)) {
			if (g->update->nodeid != nodeid)
				reply.ms_status = STATUS_WAIT;
		}

	}

	/* msg_bswap_out(&reply); */
	send_nodeid_message((char *) &reply, sizeof(reply), nodeid);
}

/*
 * Each remaining node will send us a done message.  We quit when we get the
 * first.  The subsequent done messages for the finished sevent get here and
 * are ignored.
 */

static void process_lstart_done(msg_t *msg, int nodeid)
{
	event_t *ev;

	ev = find_event(msg->ms_event_id);
	if (!ev)
		return;

	if (ev->state != EST_LSTART_WAITREMOTE)
		return;

	ev->state = EST_LSTART_REMOTEDONE;
}

void process_message(char *buf, int len, int nodeid)
{
	msg_t *msg = (msg_t *) buf;

	msg_copy_in(msg);

	log_in("message from %d type %d to_nodeid %d", nodeid, msg->ms_type,
		msg->ms_to_nodeid);

	if (msg->ms_to_nodeid && msg->ms_to_nodeid != gd_nodeid) {
		printf("ignore message to %d gd_nodeid %d\n",
			msg->ms_to_nodeid, gd_nodeid);
		return;
	}

	switch (msg->ms_type) {
	case SMSG_JOIN_REQ:
		process_join_request(msg, nodeid, buf + sizeof(msg_t));
		break;

	case SMSG_JSTOP_REQ:
		process_stop_request(msg, nodeid,
				     (uint32_t *) (buf + sizeof(msg_t)));
		break;

	case SMSG_LEAVE_REQ:
		process_leave_request(msg, nodeid);
		break;

	case SMSG_LSTOP_REQ:
		process_stop_request(msg, nodeid, NULL);
		break;

	case SMSG_JSTART_CMD:
	case SMSG_LSTART_CMD:
		process_start_request(msg, nodeid);
		break;

	case SMSG_LSTART_DONE:
		process_lstart_done(msg, nodeid);
		break;

	case SMSG_JOIN_REP:
	case SMSG_JSTOP_REP:
	case SMSG_LEAVE_REP:
	case SMSG_LSTOP_REP:
		process_reply(msg, nodeid);
		break;

	case SMSG_RECOVER:
		process_recover_msg(msg, nodeid);
		break;

	default:
		log_print("process_message: unknown type %u nodeid %u",
			  msg->ms_type, nodeid);
	}
}

char *create_msg(group_t *g, int type, int datalen, int *msglen, event_t *ev)
{
	msg_t *msg = (msg_t *) msg_buf;
	int fulllen = sizeof(msg_t) + datalen;

	memset(msg_buf, 0, SMSG_BUF_SIZE);

	if (fulllen > SMSG_BUF_SIZE) {
		log_error(g, "message too long %d", fulllen);
		return NULL;
	}

	msg = (msg_t *) msg_buf;
	msg->ms_type = type;
	msg->ms_group_id = g->global_id;
	msg->ms_level = g->level;
	msg->ms_length = datalen;
	msg->ms_event_id = ev ? ev->id : 0;

	msg_copy_out(msg);

	*msglen = fulllen;
	return (char *) msg;
}

