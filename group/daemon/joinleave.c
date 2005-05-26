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

/*
 * Routines used by nodes that are joining or leaving a SG.  These "sevent"
 * routines initiate membership changes to a SG.  Existing SG members respond
 * using the "uevent" membership update routines.
 */

struct list_head	gd_groups;
struct list_head	gd_levels[MAX_LEVELS];

static struct list_head joinleave_events;
static uint32_t		event_id = 1;

void set_event_id(uint32_t *id)
{
	*id = event_id++;
}

void init_joinleave(void)
{
	int i;

	INIT_LIST_HEAD(&joinleave_events);
	INIT_LIST_HEAD(&gd_groups);

	for (i = 0; i < MAX_LEVELS; i++)
		INIT_LIST_HEAD(&gd_levels[i]);
}

void add_joinleave_event(event_t *ev)
{
	list_add_tail(&ev->list, &joinleave_events);
}

event_t *find_event(unsigned int id)
{
	event_t *ev;

	list_for_each_entry(ev, &joinleave_events, list) {
		if (ev->id == id)
			return ev;
	}
	return NULL;
}

static void release_event(event_t *ev)
{
	if (ev->len_ids) {
		free(ev->node_ids);
		ev->node_ids = NULL;
	}

	if (ev->len_status) {
		free(ev->node_status);
		ev->node_status = NULL;
	}

	ev->node_count = 0;
	ev->memb_count = 0;
	ev->reply_count = 0;
}

static int init_event(event_t *ev)
{
	node_t *node;
	int len1, len2, count;

	/* clear state from any previous attempt */
	release_event(ev);

	ev->node_count = gd_member_count;
	ev->memb_count = ev->group->memb_count;

	/*
	 * When joining, we need a node array the size of the entire cluster
	 * member list because we get responses from all nodes.  When leaving,
	 * we only get responses from SG members, so the node array need only
	 * be that large.
	 */

	if (ev->state < EST_LEAVE_BEGIN)
		count = ev->node_count;
	else
		count = ev->memb_count;

	len1 = count * sizeof(int);
	ev->len_ids = len1;

	ev->node_ids = malloc(len1);
	if (!ev->node_ids)
		goto fail;

	len2 = count * sizeof(char);
	ev->len_status = len2;

	ev->node_status = malloc(len2);
	if (!ev->node_status)
		goto fail_free;

	memset(ev->node_status, 0, len2);
	memset(ev->node_ids, 0, len1);

	return 0;

 fail_free:
	free(ev->node_ids);
	ev->node_ids = NULL;
	ev->len_ids = 0;
 fail:
	return -ENOMEM;
}

static void event_restart(event_t *ev)
{
	clear_bit(EFL_DELAY, &ev->flags);
}

static void schedule_event_restart(event_t *ev)
{
	set_bit(EFL_DELAY, &ev->flags);
	ev->restart_time = time() + RETRY_DELAY;
}

void free_group_memb(group_t *g)
{
	node_t *node;

	while (!list_empty(&g->memb)) {
		node = list_entry(g->memb.next, node_t, list);
		list_del(&node->list);
		free(node);
	}
	g->memb_count = 0;
}

/*
 * 1.  First step in joining a SG - send a message to all nodes in the cluster
 * asking to join the named SG.  If any nodes are members they will reply with
 * a POS, or a WAIT (wait means try again, only one node can join at a time).
 * If no one knows about this SG, they all send NEG replies which means we form
 * the SG with just ourself as a member.
 */

static int send_join_notice(event_t *ev)
{
	group_t *g = ev->group;
	node_t *node;
	char *mbuf;
	msg_t *msg;
	int i = 0, error, namelen, len = 0;

	/*
	 * Create node array from member list in which to collect responses.
	 */

	error = init_event(ev);
	if (error)
		goto out;

	list_for_each_entry(node, &gd_nodes, list) {
		if (test_bit(NFL_CLUSTER_MEMBER, &node->flags))
			ev->node_ids[i++] = node->id;
	}

	/*
	 * Create and send a join request message.
	 *
	 * Other nodes then run process_join_request and reply to us; we
	 * collect the responses in process_reply and check them in
	 * check_join_notice.
	 */

	namelen = g->namelen;
	mbuf = create_msg(g, SMSG_JOIN_REQ, namelen, &len, ev);
	msg = (msg_t *) mbuf;
	memcpy(msg->ms_info, g->join_info, GROUP_INFO_LEN);
	memcpy(mbuf + sizeof(msg_t), g->name, namelen);

	error = send_broadcast_message_ev(mbuf, len, ev);
 out:
	return error;
}

/*
 * 2.  Second step in joining a SG - after we collect all replies to our join
 * request, we look at them.  If anyone told us to wait, we'll wait a while, go
 * back and start at step 1 again.
 */

static int check_join_notice(event_t *ev)
{
	int pos = 0, wait = 0, neg = 0, restart = 0, i, error = 0;

	for (i = 0; i < ev->node_count; i++) {
		switch (ev->node_status[i]) {
		case STATUS_POS:
			/* this node is in the SG and will be in new proposed
			 * memb list */
			pos++;
			break;

		case STATUS_WAIT:
			/* this node is in the SG but something else is
			 * happening with it at the moment. */
			wait++;
			break;

		case STATUS_NEG:
			/* this node has no record of the SG we're interested
			 * in */
			neg++;

			if (ev->node_ids[i] == gd_nodeid)
				ev->node_status[i] = STATUS_POS;
			break;

		default:
			/* we didn't get a valid response from this node,
			 * restart the entire sev. */
			restart++;
			break;
		}
	}

	if (pos && !wait && !restart) {
		/* all current members of this sg pos'ed our entry */
	} else if (!pos && !wait && !restart && neg) {
		/* we're the first in the cluster to join this sg */
		ev->group->global_id = new_global_id(ev->group->level);
	} else
		error = -1;

	return error;
}

/*
 * 3.  Third step in joining the SG - tell the nodes that are already members
 * to "stop" the service.  We stop them so that everyone can restart with the
 * new member (us!) added.
 */

static int send_join_stop(event_t *ev)
{
	group_t *g = ev->group;
	node_t *node;
	char *mbuf;
	uint32_t count;
	int i, len = 0, error = 0;

	/*
	 * Form the SG memb list with us in it.
	 */

	for (i = 0; i < ev->node_count; i++) {
		if (ev->node_status[i] != STATUS_POS)
			continue;

		node = new_node(ev->node_ids[i]);
		list_add_tail(&node->list, &g->memb);
		g->memb_count++;
		log_print("create group memb: nodeid %d status %d total %d",
			  node->id, ev->node_status[i], g->memb_count);
	}

	/*
	 * Re-init the node vector in which to collect responses again.
	 */

	ev->memb_count = g->memb_count;

	memset(ev->node_status, 0, ev->len_status);
	memset(ev->node_ids, 0, ev->len_ids);
	i = 0;

	list_for_each_entry(node, &g->memb, list)
		ev->node_ids[i++] = node->id;

	/*
	 * Create and send a stop message.
	 *
	 * Other nodes then run process_stop_request and process_join_stop and
	 * reply to us.  They stop the group we're trying to join if they agree.
	 * We collect responses in process_reply and check them in
	 * check_join_stop.
	 */

	mbuf = create_msg(g, SMSG_JSTOP_REQ, sizeof(uint32_t), &len, ev);
	count = htonl(g->memb_count);
	memcpy(mbuf + sizeof(msg_t), &count, sizeof(uint32_t));

	error = send_members_message_ev(g, mbuf, len, ev);
	if (error < 0)
		goto fail;

	return 0;

 fail:
	free_group_memb(g);
	return error;
}

/*
 * 4.  Fourth step in joining the SG - after we collect replies to our stop
 * request, we look at them.  Everyone sending POS agrees with us joining and
 * has stopped their SG.  If some nodes sent NEG, something is wrong and we
 * don't have a good way to address that yet since some nodes may have sent
 * POS.
 *
 * FIXME: even nodes replying with NEG should stop their SG so we can send an
 * abort and have everyone at the same place to start from again.
 */

static int check_join_stop(event_t *ev)
{
	group_t *g = ev->group;
	int i, pos = 0, neg = 0;

	for (i = 0; i < ev->memb_count; i++) {
		switch (ev->node_status[i]) {
		case STATUS_POS:
			pos++;
			break;

		case STATUS_NEG:
			log_error(g, "check_join_stop: neg from nodeid %u "
				  "(%d, %d, %u)", ev->node_ids[i],
				  pos, neg, ev->memb_count);
			neg++;
			break;

		default:
			log_error(g, "check_join_stop: unknown status=%u "
				  "nodeid=%u", ev->node_status[i],
				  ev->node_ids[i]);
			neg++;
			break;
		}
	}

	if (pos == g->memb_count)
		return 0;

	free_group_memb(g);
	return -1;
}

/*
 * 5.  Fifth step in joining the SG - everyone has stopped their service and we
 * all now start the service with us, the new member, added to the SG member
 * list.  We send start to our own service here and send a message to the other
 * members that they should also start their service.
 */

static int send_join_start(event_t *ev)
{
	group_t *g = ev->group;
	node_t *node;
	int *memb;
	char *mbuf;
	int error, count = 0, len = 0;

	/*
	 * Create a start message and send it.
	 */

	mbuf = create_msg(g, SMSG_JSTART_CMD, 0, &len, ev);

	error = send_members_message(g, mbuf, len);
	if (error < 0)
		goto fail;

	/*
	 * Start the service ourself.  The chunk of memory with the member ids
	 * must be freed by the service when it is done with it.
	 */

	memb = malloc(g->memb_count * sizeof(int));

	list_for_each_entry(node, &g->memb, list)
		memb[count++] = node->id;

	set_bit(EFL_ALLOW_STARTDONE, &ev->flags);

	group_setid(g);
	group_start(g, memb, count, ev->id, NODE_JOIN);
	return 0;
 fail:
	free_group_memb(g);
	return error;
}

/*
 * 6.  Sixth step in joining the SG - once the service has completed its start,
 * it does a kcl_start_done() to signal us that it's done.  That gets us here
 * and we do a barrier with all other members which join the barrier when their
 * service is done starting.
 */

static int startdone_barrier_new(event_t *ev)
{
	group_t *g = ev->group;
	char bname[MAX_BARRIERLEN];
	int error = 0;

	memset(bname, 0, MAX_BARRIERLEN);
	ev->barrier_status = -1;

	set_bit(EFL_ALLOW_BARRIER, &ev->flags);

	/* If we're the only member, skip the barrier */
	if (g->memb_count == 1)
		goto done;

	snprintf(bname, MAX_BARRIERLEN, "sm.%u.%u.%u.%u",
		 g->global_id, gd_nodeid, ev->id, g->memb_count);

	error = do_barrier(g, bname, g->memb_count, GD_BARRIER_STARTDONE_NEW);
	if (!error)
		goto done;

	if (error < 0) {
		log_error(g, "startdone_barrier_new error %d", error);
		clear_bit(EFL_ALLOW_BARRIER, &ev->flags);
		group_stop(g);
		free_group_memb(g);
	} else if (error > 0)
		error = 0;

	return error;

 done:
	clear_bit(EFL_ALLOW_BARRIER, &ev->flags);
	ev->barrier_status = 0;
	ev->state = EST_BARRIER_DONE;
	return 0;

}

/*
 * 7.  Seventh step in joining the SG - check that the barrier we joined with
 * all other members returned with a successful status.
 */

static int check_startdone_barrier_new(event_t *ev)
{
	group_t *g = ev->group;
	int error = ev->barrier_status;

	if (error) {
		group_stop(g);
		free_group_memb(g);
	}
	return error;
}

/*
 * 8.  Eigth step in joining the SG - send the service a "finish" indicating
 * that all members have successfully started the service.
 */

static void do_finish_new(event_t *ev)
{
	group_t *g = ev->group;

	g->state = GST_RUN;
	g->event = NULL;
	clear_bit(GFL_JOINING, &g->flags);
	set_bit(GFL_MEMBER, &g->flags);
	group_finish(g, ev->id);
}

/*
 * 9.  Ninth step in joining the SG - it's done so get rid of the sevent stuff
 * and tell the process which initiated the join that it's done.
 */

static void event_done(event_t *ev)
{
	group_t *g = ev->group;

	list_del(&ev->list);
	release_event(ev);
	free(ev);
}

/*
 * Move through the steps of a join.  Summary:
 *
 * 1. Send a join notice to all cluster members.
 * 2. Collect and check replies to the join notice.
 * 3. Send a stop message to all SG members.
 * 4. Collect and check replies to the stop message.
 * 5. Send a start message to all SG members and start service ourself.
 * 6. Use barrier to wait for all nodes to complete the start.
 * 7. Check that all SG members joined the barrier.
 * 8. Send finish to the service indicating that all nodes started it.
 * 9. Clean up sevent and signal completion to the process that started the join
 */

static int process_join_event(event_t *ev)
{
	int rv = 1, error = 0;

	/*
	 * We may cancel the current join attempt if another node is also
	 * attempting to join or leave. (Only a single node can join or leave
	 * at once.)  If cancelled, 0ur join attempt will be restarted later.
	 */

	if (test_bit(EFL_CANCEL, &ev->flags)) {
		clear_bit(EFL_CANCEL, &ev->flags);
		error = 1;
		goto cancel;
	}

	log_group(ev->group, "event state %u", ev->state);

	switch (ev->state) {

		/*
		 * An sevent is created in kcl_join_service with a state of
		 * JOIN_BEGIN.
		 */

	case EST_JOIN_BEGIN:
		ev->state = EST_JOIN_ACKWAIT;
		error = send_join_notice(ev);
		break;

		/*
		 * se_state is changed from JOIN_ACKWAIT to JOIN_ACKED in 
		 * process_reply  (when all the replies have been received)
		 */

	case EST_JOIN_ACKED:
		error = check_join_notice(ev);
		if (error)
			break;

		ev->state = EST_JSTOP_ACKWAIT;
		error = send_join_stop(ev);
		break;

		/*
		 * se_state is changed from JSTOP_ACKWAIT to JSTOP_ACKED in
		 * proces_reply  (when all the replies have been received)
		 */

	case EST_JSTOP_ACKED:
		error = check_join_stop(ev);
		if (error)
			break;

		ev->state = EST_JSTART_SERVICEWAIT;
		error = send_join_start(ev);
		break;

		/*
		 * se_state is changed from JSTART_SERVICEWAIT to
		 * JSTART_SERVICEDONE in kcl_start_done
		 */

	case EST_JSTART_SERVICEDONE:
		ev->state = EST_BARRIER_WAIT;
		error = startdone_barrier_new(ev);
		break;

		/*
		 * se_state is changed from BARRIER_WAIT to BARRIER_DONE in
		 * process_startdone_barrier_new 
		 */

	case EST_BARRIER_DONE:
		error = check_startdone_barrier_new(ev);
		if (error)
			break;

		do_finish_new(ev);
		event_done(ev);
		break;

	default:
		log_error(ev->group, "no join processing for state %u",
			  ev->state);
		rv = 0;
	}

 cancel:
	if (error) {
		/* restart the sevent from the beginning */
		log_group(ev->group, "process_join error %d %lx", error,
			  ev->flags);
		ev->state = EST_JOIN_BEGIN;
		ev->group->global_id = 0;
		schedule_event_restart(ev);
	}

	return rv;
}

/*
 * 1.  First step in leaving an SG - send a message to other SG members asking
 * to leave the SG.  Nodes that don't have another active sevent or uevent for
 * this SG will return POS.
 */

static int send_leave_notice(event_t *ev)
{
	group_t *g = ev->group;
	node_t *node;
	char *mbuf;
	msg_t *msg;
	int i = 0, error = -1, len = 0;

	/*
	 * Create a node array from member list in which to collect responses.
	 */

	error = init_event(ev);
	if (error)
		goto out;

	list_for_each_entry(node, &g->memb, list)
		ev->node_ids[i++] = node->id;

	/*
	 * Create and send a leave request message.
	 */

	mbuf = create_msg(g, SMSG_LEAVE_REQ, 0, &len, ev);
	msg = (msg_t *) mbuf;
	memcpy(msg->ms_info, g->leave_info, GROUP_INFO_LEN);

	error = send_members_message_ev(g, mbuf, len, ev);
 out:
	return error;
}

/*
 * 2.  Second step in leaving an SG - after we collect all replies to our leave
 * request, we look at them.  If anyone replied with WAIT, we abort our attempt
 * at leaving and try again in a bit.
 */

static int check_leave_notice(event_t *ev)
{
	int pos = 0, wait = 0, neg = 0, restart = 0, i;

	for (i = 0; i < ev->memb_count; i++) {
		switch (ev->node_status[i]) {
		case STATUS_POS:
			pos++;
			break;

		case STATUS_WAIT:
			wait++;
			break;

		case STATUS_NEG:
			neg++;
			break;

		default:
			/* we didn't get a valid response from this node,
			 * restart the entire sev. */
			restart++;
			break;
		}
	}

	/* all members approve */
	if (pos && !wait && !restart)
		return 0;

	return -1;
}

/*
 * 3.  Third step in leaving the SG - tell the member nodes to "stop" the SG.
 * They must be stopped in order to restart without us as a member.
 */

static int send_leave_stop(event_t *ev)
{
	group_t *g = ev->group;
	char *mbuf;
	int error, len = 0;

	/*
	 * Re-init the status vector in which to collect responses.
	 */

	memset(ev->node_status, 0, ev->len_status);

	/*
	 * Create and send a stop message.
	 */

	mbuf = create_msg(g, SMSG_LSTOP_REQ, 0, &len, ev);

	error = send_members_message_ev(g, mbuf, len, ev);
	if (error < 0)
		goto out;

	/*
	 * we and all others stop the SG now 
	 */

	group_stop(g);
 out:
	return error;
}

/*
 * 4.  Fourth step in leaving the SG - check the replies to our stop request.
 * Same problem with getting different replies as check_join_stop.
 */

static int check_leave_stop(event_t *ev)
{
	group_t *g = ev->group;
	int i, pos = 0, neg = 0;

	for (i = 0; i < ev->memb_count; i++) {
		switch (ev->node_status[i]) {
		case STATUS_POS:
			pos++;
			break;

		case STATUS_NEG:
			log_error(g, "check_leave_stop: fail from nodeid %u "
				  "(%d, %d, %u)", ev->node_ids[i],
				  pos, neg, ev->memb_count);
			neg++;
			break;

		default:
			log_error(g, "check_leave_stop: status %u nodeid %u",
				  ev->node_status[i], ev->node_ids[i]);
			neg++;
			break;
		}
	}

	if (pos == g->memb_count)
		return 0;

	return -1;
}

/*
 * 5.  Fifth step in leaving the SG - tell the other SG members to restart the
 * service without us.  We, of course, don't start our own stopped service.  If
 * we're the last SG member and leaving, we jump right to the next step.
 */

static int send_leave_start(event_t *ev)
{
	group_t *g = ev->group;
	char *mbuf;
	int error = 0, len = 0;

	if (g->memb_count == 1)
		ev->state = EST_LSTART_REMOTEDONE;
	else {
		mbuf = create_msg(g, SMSG_LSTART_CMD, 0, &len, ev);
		error = send_members_message(g, mbuf, len);
	}
	return error;
}

/*
 * Move through the steps of a leave.  Summary:
 *
 * 1. Send a leave notice to all SG members.
 * 2. Collect and check replies to the leave notice.
 * 3. Send a stop message to all SG members and stop our own SG.
 * 4. Collect and check replies to the stop message.
 * 5. Send a start message to SG members.
 * 6. Clean up sevent and signal completion to the process that
 *    started the leave.
 */

static int process_leave_event(event_t *ev)
{
	group_t *g;
	int rv = 1, error = 0;

	/*
	 * We may cancel the current leave attempt if another node is also
	 * attempting to join or leave. (Only a single node can join or leave
	 * at once.)  Our leave attempt will be restarted after being
	 * cancelled.
	 */

	if (test_bit(EFL_CANCEL, &ev->flags)) {
		clear_bit(EFL_CANCEL, &ev->flags);
		error = 1;
		goto cancel;
	}

	if (in_update(ev->group)) {
		error = 2;
		goto cancel;
	}

	if (!list_empty(&ev->group->joining)) {
		error = 3;
		goto cancel;
	}

	log_group(ev->group, "event state %u", ev->state);

	switch (ev->state) {

		/*
		 * An sevent is created in kcl_leave_service with a state of
		 * LEAVE_BEGIN.
		 */

	case EST_LEAVE_BEGIN:
		ev->state = EST_LEAVE_ACKWAIT;
		error = send_leave_notice(ev);
		break;

		/*
		 * se_state is changed from LEAVE_ACKWAIT to LEAVE_ACKED in 
		 * process_reply  (when all the replies have been received)
		 */

	case EST_LEAVE_ACKED:
		error = check_leave_notice(ev);
		if (error)
			break;

		ev->state = EST_LSTOP_ACKWAIT;
		error = send_leave_stop(ev);
		break;

		/*
		 * se_state is changed from LSTOP_ACKWAIT to LSTOP_ACKED in
		 * process_reply
		 */

	case EST_LSTOP_ACKED:
		error = check_leave_stop(ev);
		if (error)
			break;

		ev->state = EST_LSTART_WAITREMOTE;
		error = send_leave_start(ev);
		break;

		/*
		 * se_state is changed from LSTART_WAITREMOTE to
		 * LSTART_REMOTEDONE in process_leave_done
		 */

	case EST_LSTART_REMOTEDONE:
		g = ev->group;
		group_terminate(g);
		event_done(ev);
		remove_group(g);
		break;

	default:
		log_error(ev->group, "no leave processing for state %u",
			  ev->state);
		rv = 0;
	}

 cancel:
	if (error) {
		log_group(ev->group, "process_leave error %d %lx", error,
			  ev->flags);
		/* restart the event from the beginning */
		ev->state = EST_LEAVE_BEGIN;
		schedule_event_restart(ev);
	}

	return rv;
}

/*
 * Event backout code.  Take appropriate steps when a recovery occurs while
 * we're in the midst of an event.  The recovery may or may not affect the
 * event.  If it does, it usually means cancelling the event and restarting
 * it from the beginning once the recovery processing is done.
 */

/*
 * If any of the nodes that replied with OK is dead, we give up on the current
 * join attempt and restart.  Otherwise, this sevent can continue.
 */

static int backout_join_acked(event_t *ev)
{
	node_t *node;
	int i;

	for (i = 0; i < ev->node_count; i++) {
		if (ev->node_status[i] != STATUS_POS)
			continue;

		list_for_each_entry(node, &gd_nodes, list) {
			if (test_bit(NFL_NEED_RECOVERY, &node->flags) &&
			    (node->id == ev->node_ids[i]))
				return TRUE;
		}
	}
	return FALSE;
}

/*
 * In this state our sg member list exists and mark_affected_sgs() will have
 * set NEED_RECOVERY if any of the nodes in the sg we're joining is dead.  We
 * restart the join process if this is the case, otherwise this sevent can
 * continue.
 */

static int backout_jstop_ackwait(event_t *ev)
{
	group_t *g = ev->group;

	if (!test_bit(GFL_NEED_RECOVERY, &g->flags))
		return FALSE;

	clear_bit(EFL_ALLOW_JSTOP, &ev->flags);
	free_group_memb(g);
	return TRUE;
}

/*
 * Same as previous.
 */

static int backout_jstop_acked(event_t *ev)
{
	return backout_jstop_ackwait(ev);
}

/*
 * If NEED_RECOVERY is set a member of the sg we're joining died while we were
 * starting our service.  The recovery process will restart the service on all
 * the prior sg members (not including those that died or us).  We will
 * reattempt our join which should be accepted once the nodes are done with
 * recovery.
 */

static int backout_jstart_servicewait(event_t *ev)
{
	group_t *g = ev->group;

	if (!test_bit(GFL_NEED_RECOVERY, &g->flags))
		return FALSE;

	clear_bit(EFL_ALLOW_STARTDONE, &ev->flags);
	group_stop(g);
	free_group_memb(g);
	return TRUE;
}

/*
 * Same as previous.
 */

static int backout_jstart_servicedone(event_t *ev)
{
	return backout_jstart_servicewait(ev);
}

/*
 * If NEED_RECOVERY is set a member of the sg we're joining died while we were
 * waiting on the "all done" barrier.  Stop our service that we just started
 * and cancel the barrier.  The recovery process will restart the service on
 * all the prior sg members (not including those that died or us).  We will
 * reattempt our join which should be accepted once the nodes are done with
 * recovery.
 */

static int backout_barrier_wait(event_t *ev)
{
	group_t *g = ev->group;
	char bname[MAX_BARRIERLEN];

	if (!test_bit(GFL_NEED_RECOVERY, &g->flags))
		return FALSE;

	clear_bit(EFL_ALLOW_BARRIER, &ev->flags);

	group_stop(g);

	memset(bname, 0, MAX_BARRIERLEN);
	snprintf(bname, MAX_BARRIERLEN, "sm.%u.%u.%u.%u",
		 g->global_id, gd_nodeid, ev->id, g->memb_count);

	/* kcl_barrier_cancel(bname); */

	free_group_memb(g);
	return TRUE;
}

/*
 * If NEED_RECOVERY is set, a member of the sg we just joined has failed.  The
 * recovery began after the barrier callback.  If the result in the callback is
 * "success" then we are joined, this sevent is finished and we'll process the
 * sg within the forthcoming recovery with the other members.
 *
 * We rely upon cnxman to guarantee that once all nodes have joined a barrier,
 * all nodes will receive the corresponding barrier callback *before any*
 * receive an sm_member_update() due to one of those nodes failing just after
 * joining the barrier.  If some nodes receive the sm_member_update() before
 * the barrier callback and others receive the barrier callback before the
 * sm_member_update() then they will disagree as to whether the node joining/
 * leaving is in/out of the sg.
 */

static int backout_barrier_done(event_t *ev)
{
	group_t *g = ev->group;

	if (!test_bit(GFL_NEED_RECOVERY, &g->flags))
		return FALSE;

	if (!ev->barrier_status) {
		do_finish_new(ev);
		event_done(ev);
		return FALSE;
	} else {
		group_stop(g);
		free_group_memb(g);
		return TRUE;
	}
}

/*
 * We've done nothing yet, just restart when recovery is done (if sg is flagged
 * with recovery.)
 */

static int backout_leave_begin(event_t *ev)
{
	group_t *g = ev->group;

	if (!test_bit(GFL_NEED_RECOVERY, &g->flags))
		return FALSE;

	return TRUE;
}

/*
 * Ignore any replies to our leave notice and restart when recovery is done (if
 * sg is flagged with recovery.)
 */

static int backout_leave_ackwait(event_t *ev)
{
	group_t *g = ev->group;

	if (!test_bit(GFL_NEED_RECOVERY, &g->flags))
		return FALSE;

	clear_bit(EFL_ALLOW_LEAVE, &ev->flags);

	return TRUE;
}

/*
 * Same as previous.
 */

static int backout_leave_acked(event_t *ev)
{
	return backout_leave_ackwait(ev);
}

/*
 * Ignore any stop replies.  All the members will be stopped anyway to do the
 * recovery.  Let that happen and restart our leave when done.
 */

static int backout_lstop_ackwait(event_t *ev)
{
	group_t *g = ev->group;

	if (!test_bit(GFL_NEED_RECOVERY, &g->flags))
		return FALSE;

	clear_bit(EFL_ALLOW_LSTOP, &ev->flags);

	return TRUE;
}

/*
 * Same as previous.
 */

static int backout_lstop_acked(event_t *ev)
{
	return backout_lstop_ackwait(ev);
}

/*
 * All members will be stopped due to recovery and restarted by recovery
 * processing.  That includes us, we have to retry the leave once the recovery
 * is done.
 */

static int backout_lstart_waitremote(event_t *ev)
{
	group_t *g = ev->group;

	if (!test_bit(GFL_NEED_RECOVERY, &g->flags))
		return FALSE;

	return TRUE;
}

/*
 * Reset an sevent to its beginning so it can be restarted.  This is necessary
 * when recovery affects an SG while we're trying to join or leave (ie. a node
 * in the SG fails).
 */

void backout_events(void)
{
	event_t *ev, *safe;
	int delay;

	list_for_each_entry_safe(ev, safe, &joinleave_events, list) {

		delay = FALSE;

		log_group(ev->group, "backout sevent state %u", ev->state);

		switch (ev->state) {

		/* backout after kcl_join_service and before
		 * send_join_notice */
		case EST_JOIN_BEGIN:
			break;

		/* backout after send_join_notice and before final
		 * process_reply */
		case EST_JOIN_ACKWAIT:
			clear_bit(EFL_ALLOW_JOIN, &ev->flags);
			ev->state = EST_JOIN_BEGIN;
			break;

		/* backout after final process_reply and before
		 * check_join_notice */
		case EST_JOIN_ACKED:
			delay = backout_join_acked(ev);
			break;

		/* backout after send_join_stop and before final
		 * process_reply */
		case EST_JSTOP_ACKWAIT:
			delay = backout_jstop_ackwait(ev);
			break;

		/* backout after final process_reply and before
		 * check_join_stop */
		case EST_JSTOP_ACKED:
			delay = backout_jstop_acked(ev);
			break;

		/* backout after send_join_start and before
		 * kcl_start_done */
		case EST_JSTART_SERVICEWAIT:
			delay = backout_jstart_servicewait(ev);
			break;

		/* backout after kcl_start_done and before
		 * startdone_barrier_new */
		case EST_JSTART_SERVICEDONE:
			delay = backout_jstart_servicedone(ev);
			break;

		/* backout after startdone_barrier_new and before
		 * callback_startdone_barrier_new */
		case EST_BARRIER_WAIT:
			delay = backout_barrier_wait(ev);
			break;

		/* backout after callback_startdone_barrier_new and
		 * before check_startdone_barrier_new */
		case EST_BARRIER_DONE:
			delay = backout_barrier_done(ev);
			break;

		/* backout after kcl_leave_service and before
		 * send_leave_notice */
		case EST_LEAVE_BEGIN:
			delay = backout_leave_begin(ev);
			break;

		/* backout after send_leave_notice and before final
		 * process_reply */
		case EST_LEAVE_ACKWAIT:
			delay = backout_leave_ackwait(ev);
			break;

		/* backout after final process_reply and before
		 * check_leave_notice */
		case EST_LEAVE_ACKED:
			delay = backout_leave_acked(ev);
			break;

		/* backout after send_leave_stop and before final
		 * process_reply */
		case EST_LSTOP_ACKWAIT:
			delay = backout_lstop_ackwait(ev);
			break;

		/* backout after final process_reply and before
		 * check_leave_stop */
		case EST_LSTOP_ACKED:
			delay = backout_lstop_acked(ev);
			break;

		/* backout after send_leave_start and before
		 * process_lstart_done */
		case EST_LSTART_WAITREMOTE:
			delay = backout_lstart_waitremote(ev);
			break;

		/* backout after process_lstart_done and before
		 * process_leave_event */
		case EST_LSTART_REMOTEDONE:
			event_done(ev);
			delay = FALSE;
			break;

		default:
			log_error(ev->group, "backout_events: bad state %d",
				  ev->state);
		}

		if (delay) {
			if (test_bit(GFL_LEAVING, &ev->group->flags)) {
				ev->state = EST_LEAVE_BEGIN;
				set_bit(EFL_DELAY_RECOVERY, &ev->flags);
			} else
				ev->state = EST_JOIN_BEGIN;
		}
	}
}

int needs_work(event_t *ev)
{
	if (test_bit(EFL_DELAY, &ev->flags)) {
		if (time() >= ev->restart_time) {
			log_group(ev->group, "restart delayed event from %d",
				  ev->state);
			clear_bit(EFL_DELAY, &ev->flags);
			return 1;
		}
		return 0;
	}

	/* DELAY_RECOVERY is cleared by the recovery code when
	   recovery is complete */

	if (test_bit(EFL_DELAY_RECOVERY, &ev->flags))
		return 0;

	return 1;
}

int process_joinleave(void)
{
	event_t *ev, *safe;
	int rv = 0, barrier_pending = 0, delay_pending = 0;

	list_for_each_entry_safe(ev, safe, &joinleave_events, list) {
		if (!needs_work(ev))
			continue;

		/* positive return value means call again as there
		   may be more work to do */

		if (ev->state < EST_LEAVE_BEGIN)
			rv += process_join_event(ev);
		else
			rv += process_leave_event(ev);
	}

	/* positive values for these pending things results in
	   a timeout being set for the main poll loop */

	list_for_each_entry(ev, &joinleave_events, list) {
		if (ev->state == EST_BARRIER_WAIT)
			barrier_pending++;
		if (test_bit(EFL_DELAY, &ev->flags))
			delay_pending++;
	}
	gd_event_barriers = barrier_pending;
	gd_event_delays = delay_pending;

	return rv;
}

group_t *find_group_id(int id)
{
	group_t *g;

	list_for_each_entry(g, &gd_groups, list) {
		if (g->global_id == id)
			return g;
	}
	return NULL;
}

group_t *find_group_level(char *name, int level)
{
	group_t *g;

	list_for_each_entry(g, &gd_levels[level], level_list) {
		if (!strcmp(g->name, name))
			return g;
	}
	return NULL;
}

int in_event(group_t *g)
{
	if (test_bit(GFL_JOINING, &g->flags) ||
	    test_bit(GFL_LEAVING, &g->flags))
		return 1;
	return 0;
}

int in_update(group_t *g)
{
	if (test_bit(GFL_UPDATE, &g->flags))
		return 1;
	return 0;
}

event_t *create_event(group_t *g)
{
	event_t *ev;

	ev = malloc(sizeof(event_t));
	memset(ev, 0, sizeof(*ev));

	ev->group = g;

	if (test_bit(GFL_MEMBER, &g->flags)) {
		set_bit(GFL_LEAVING, &g->flags);
		ev->state = EST_LEAVE_BEGIN;
	} else {
		set_bit(GFL_JOINING, &g->flags);
		ev->state = EST_JOIN_BEGIN;
	}
	set_event_id(&ev->id);
	return ev;
}

int create_group(char *name, int level, group_t **g_out)
{
	group_t *g;

	g = find_group_level(name, level);
	if (g)
		return -EEXIST;

	g = malloc(sizeof(group_t) + strlen(name));
	memset(g, 0, sizeof(group_t) + strlen(name));

	strcpy(g->name, name);
	g->namelen = strlen(name);
	g->level = level;
	g->state = GST_JOIN;
	INIT_LIST_HEAD(&g->memb);
	INIT_LIST_HEAD(&g->joining);

	list_add_tail(&g->list, &gd_groups);
	list_add_tail(&g->level_list, &gd_levels[level]);

	*g_out = g;
	return 0;
}

void remove_group(group_t *g)
{
	list_del(&g->list);
	list_del(&g->level_list);
	free_group_memb(g);
	free(g);
}

int do_join(char *name, int level, int ci, char *info)
{
	group_t *g;
	int error;

	error = create_group(name, level, &g);
	if (error) {
		log_print("do_join group exists");
		return error;
	}
	g->client = ci;

	if (info)
		strncpy(g->join_info, info, GROUP_INFO_LEN);

	g->event = create_event(g);
	add_joinleave_event(g->event);
	return 0;
}

int do_leave(char *name, int level, int nowait, char *info)
{
	group_t *g;
	int error;

	g = find_group_level(name, level);
	if (!g) {
		log_print("do_leave no group");
		return -ENOENT;
	}
	if (in_event(g)) {
		log_print("do_leave group busy %x", g->flags);
		return -EBUSY;
	}
	if (nowait && in_update(g))
		return -EAGAIN;

	if (info)
		strncpy(g->leave_info, info, GROUP_INFO_LEN);

	g->event = create_event(g);
	add_joinleave_event(g->event);
	return 0;
}

node_t *new_node(int nodeid)
{
	node_t *node;

	node = malloc(sizeof(*node));
	memset(node, 0, sizeof(*node));
	node->id = nodeid;
	return node;
}

node_t *find_member(group_t *g, int nodeid)
{
	node_t *node;

	list_for_each_entry(node, &g->memb, list) {
		if (node->id == nodeid)
			return node;
	}
	return NULL;
}

