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

#include "sm.h"

/*
 * Routines used by nodes that are joining or leaving a SG.  These "sevent"
 * routines initiate membership changes to a SG.  Existing SG members respond
 * using the "uevent" membership update routines.
 */

extern uint32_t 		sm_our_nodeid;
extern struct list_head 	sm_members;
static struct list_head 	new_event;
static spinlock_t 		new_event_lock;
static struct list_head		joinleave_events;

void init_joinleave(void)
{
	INIT_LIST_HEAD(&new_event);
	spin_lock_init(&new_event_lock);
	INIT_LIST_HEAD(&joinleave_events);
}

void new_joinleave(sm_sevent_t *sev)
{
	spin_lock(&new_event_lock);
	list_add_tail(&sev->se_list, &new_event);
	spin_unlock(&new_event_lock);
	wake_serviced(DO_JOINLEAVE);
}

sm_sevent_t *find_sevent(unsigned int id)
{
	sm_sevent_t *sev;

	list_for_each_entry(sev, &joinleave_events, se_list) {
		if (sev->se_id == id)
			return sev;
	}
	return NULL;
}

static void release_sevent(sm_sevent_t *sev)
{
	if (sev->se_len_ids) {
		kfree(sev->se_node_ids);
		sev->se_node_ids = NULL;
	}

	if (sev->se_len_status) {
		kfree(sev->se_node_status);
		sev->se_node_status = NULL;
	}

	sev->se_node_count = 0;
	sev->se_memb_count = 0;
	sev->se_reply_count = 0;
}

static int init_sevent(sm_sevent_t *sev)
{
	sm_node_t *node;
	int len1, len2, count, cluster_members = 0;

	/* clear state from any previous attempt */
	release_sevent(sev);

	list_for_each_entry(node, &sm_members, list) {
		if (test_bit(SNFL_CLUSTER_MEMBER, &node->flags))
			cluster_members++;
	}

	sev->se_node_count = cluster_members;
	sev->se_memb_count = sev->se_sg->memb_count;

	/*
	 * When joining, we need a node array the size of the entire cluster
	 * member list because we get responses from all nodes.  When leaving,
	 * we only get responses from SG members, so the node array need only
	 * be that large.
	 */

	if (sev->se_state < SEST_LEAVE_BEGIN)
		count = sev->se_node_count;
	else
		count = sev->se_memb_count;

	len1 = count * sizeof(uint32_t);
	sev->se_len_ids = len1;

	sev->se_node_ids = (uint32_t *) kmalloc(len1, GFP_KERNEL);
	if (!sev->se_node_ids)
		goto fail;

	len2 = count * sizeof (char);
	sev->se_len_status = len2;

	sev->se_node_status = (char *) kmalloc(len2, GFP_KERNEL);
	if (!sev->se_node_status)
		goto fail_free;

	memset(sev->se_node_status, 0, len2);
	memset(sev->se_node_ids, 0, len1);

	return 0;

      fail_free:
	kfree(sev->se_node_ids);
	sev->se_node_ids = NULL;
	sev->se_len_ids = 0;

      fail:
	return -ENOMEM;
}

/* Context: timer */

static void sev_restart(unsigned long data)
{
	sm_sevent_t *sev = (sm_sevent_t *) data;

	clear_bit(SEFL_DELAY, &sev->se_flags);
	set_bit(SEFL_CHECK, &sev->se_flags);
	wake_serviced(DO_JOINLEAVE);
}

static void schedule_sev_restart(sm_sevent_t *sev)
{
	init_timer(&sev->se_restart_timer);
	sev->se_restart_timer.function = sev_restart;
	sev->se_restart_timer.data = (long) sev;
	mod_timer(&sev->se_restart_timer, jiffies + (RETRY_DELAY * HZ));
}

void free_sg_memb(sm_group_t *sg)
{
	sm_node_t *node;

	while (!list_empty(&sg->memb)) {
		node = list_entry(sg->memb.next, sm_node_t, list);
		list_del(&node->list);
		kfree(node);
	}
	sg->memb_count = 0;
}

/*
 * 1.  First step in joining a SG - send a message to all nodes in the cluster
 * asking to join the named SG.  If any nodes are members they will reply with
 * a POS, or a WAIT (wait means try again, only one node can join at a time).
 * If no one knows about this SG, they all send NEG replies which means we form
 * the SG with just ourself as a member.
 */

static int send_join_notice(sm_sevent_t *sev)
{
	sm_group_t *sg = sev->se_sg;
	sm_node_t *node;
	char *msg;
	int i = 0, error, namelen, len = 0;

	/*
	 * Create node array from member list in which to collect responses.
	 */

	error = init_sevent(sev);
	if (error)
		goto out;

	list_for_each_entry(node, &sm_members, list) {
		if (test_bit(SNFL_CLUSTER_MEMBER, &node->flags))
			sev->se_node_ids[i++] = node->id;
	}

	/*
	 * Create and send a join request message.
	 *
	 * Other nodes then run process_join_request and reply to us; we
	 * collect the responses in process_reply and check them in
	 * check_join_notice.
	 */

	namelen = sg->namelen;
	msg = create_smsg(sg, SMSG_JOIN_REQ, namelen, &len, sev);
	memcpy(msg + sizeof(sm_msg_t), sg->name, namelen);

	error = send_broadcast_message_sev(msg, len, sev);

      out:
	return error;
}

/*
 * 2.  Second step in joining a SG - after we collect all replies to our join
 * request, we look at them.  If anyone told us to wait, we'll wait a while, go
 * back and start at step 1 again.
 */

static int check_join_notice(sm_sevent_t *sev)
{
	int pos = 0, wait = 0, neg = 0, restart = 0, i, error = 0;

	for (i = 0; i < sev->se_node_count; i++) {
		switch (sev->se_node_status[i]) {
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

			if (sev->se_node_ids[i] == sm_our_nodeid)
				sev->se_node_status[i] = STATUS_POS;
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
		sev->se_sg->global_id = sm_new_global_id(sev->se_sg->level);
	} else
		error = -1;

	return error;
}

/*
 * 3.  Third step in joining the SG - tell the nodes that are already members
 * to "stop" the service.  We stop them so that everyone can restart with the
 * new member (us!) added.
 */

static int send_join_stop(sm_sevent_t *sev)
{
	sm_group_t *sg = sev->se_sg;
	sm_node_t *node;
	char *msg;
	uint32_t be_count;
	int i, len = 0, error = 0;

	/*
	 * Form the SG memb list with us in it.
	 */

	for (i = 0; i < sev->se_node_count; i++) {
		if (sev->se_node_status[i] != STATUS_POS)
			continue;

		node = sm_new_node(sev->se_node_ids[i]);
		if (!node)
			goto fail;

		list_add_tail(&node->list, &sg->memb);
		sg->memb_count++;
	}

	/*
	 * Re-init the node vector in which to collect responses again.
	 */

	sev->se_memb_count = sg->memb_count;

	memset(sev->se_node_status, 0, sev->se_len_status);
	memset(sev->se_node_ids, 0, sev->se_len_ids);
	i = 0;

	list_for_each_entry(node, &sg->memb, list)
		sev->se_node_ids[i++] = node->id;

	/*
	 * Create and send a stop message.
	 *
	 * Other nodes then run process_stop_request and process_join_stop and
	 * reply to us.  They stop the sg we're trying to join if they agree.
	 * We collect responses in process_reply and check them in
	 * check_join_stop.
	 */

	msg = create_smsg(sg, SMSG_JSTOP_REQ, sizeof(uint32_t), &len, sev);
	be_count = cpu_to_be32(sg->memb_count);
	memcpy(msg + sizeof(sm_msg_t), &be_count, sizeof(uint32_t));

	error = send_members_message_sev(sg, msg, len, sev);
	if (error < 0)
		goto fail;

	return 0;

      fail:
	free_sg_memb(sg);
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

static int check_join_stop(sm_sevent_t *sev)
{
	sm_group_t *sg = sev->se_sg;
	int i, pos = 0, neg = 0;

	for (i = 0; i < sev->se_memb_count; i++) {
		switch (sev->se_node_status[i]) {
		case STATUS_POS:
			pos++;
			break;

		case STATUS_NEG:
			log_error(sg, "check_join_stop: neg from nodeid %u "
				  "(%d, %d, %u)", sev->se_node_ids[i],
				  pos, neg, sev->se_memb_count);
			neg++;
			break;

		default:
			log_error(sg, "check_join_stop: unknown status=%u "
				  "nodeid=%u", sev->se_node_status[i],
				  sev->se_node_ids[i]);
			neg++;
			break;
		}
	}

	if (pos == sg->memb_count)
		return 0;

	free_sg_memb(sg);
	return -1;
}

/*
 * 5.  Fifth step in joining the SG - everyone has stopped their service and we
 * all now start the service with us, the new member, added to the SG member
 * list.  We send start to our own service here and send a message to the other
 * members that they should also start their service.
 */

static int send_join_start(sm_sevent_t *sev)
{
	sm_group_t *sg = sev->se_sg;
	sm_node_t *node;
	uint32_t *memb;
	char *msg;
	int error, count = 0, len = 0;

	/*
	 * Create a start message and send it.
	 */

	msg = create_smsg(sg, SMSG_JSTART_CMD, 0, &len, sev);

	error = send_members_message(sg, msg, len);
	if (error < 0)
		goto fail;

	/*
	 * Start the service ourself.  The chunk of memory with the member ids
	 * must be freed by the service when it is done with it.
	 */

	SM_RETRY(memb = kmalloc(sg->memb_count * sizeof(uint32_t), GFP_KERNEL),
		 memb);

	list_for_each_entry(node, &sg->memb, list)
		memb[count++] = node->id;

	set_bit(SEFL_ALLOW_STARTDONE, &sev->se_flags);

	sg->ops->start(sg->service_data, memb, count, sev->se_id,
		       SERVICE_NODE_JOIN);
	return 0;

      fail:
	free_sg_memb(sg);
	return error;
}

/*
 * 6.  Sixth step in joining the SG - once the service has completed its start,
 * it does a kcl_start_done() to signal us that it's done.  That gets us here
 * and we do a barrier with all other members which join the barrier when their
 * service is done starting.
 */

static int startdone_barrier_new(sm_sevent_t *sev)
{
	sm_group_t *sg = sev->se_sg;
	char bname[MAX_BARRIER_NAME_LEN];
	int error;

	memset(bname, 0, MAX_BARRIER_NAME_LEN);
	sev->se_barrier_status = -1;

	set_bit(SEFL_ALLOW_BARRIER, &sev->se_flags);

	/* If we're the only member, skip the barrier */
	if (sg->memb_count == 1) {
		process_startdone_barrier_new(sg, 0);
		return 0;
	}

	snprintf(bname, MAX_BARRIER_NAME_LEN, "sm.%u.%u.%u.%u",
		 sg->global_id, sm_our_nodeid, sev->se_id, sg->memb_count);

	error = sm_barrier(bname, sg->memb_count, SM_BARRIER_STARTDONE_NEW);
	if (error)
		goto fail;

	return 0;

      fail:
	clear_bit(SEFL_ALLOW_BARRIER, &sev->se_flags);
	sg->ops->stop(sg->service_data);
	free_sg_memb(sg);
	return error;
}

/*
 * 7.  Seventh step in joining the SG - check that the barrier we joined with
 * all other members returned with a successful status.
 */

static int check_startdone_barrier_new(sm_sevent_t *sev)
{
	sm_group_t *sg = sev->se_sg;
	int error = sev->se_barrier_status;

	if (error) {
		sg->ops->stop(sg->service_data);
		free_sg_memb(sg);
	}
	return error;
}

/*
 * 8.  Eigth step in joining the SG - send the service a "finish" indicating
 * that all members have successfully started the service.
 */

static void do_finish_new(sm_sevent_t *sev)
{
	sm_group_t *sg = sev->se_sg;

	sg->state = SGST_RUN;
	sg->sevent = NULL;
	clear_bit(SGFL_SEVENT, &sg->flags);

	sg->ops->finish(sg->service_data, sev->se_id);
}

/*
 * 9.  Ninth step in joining the SG - it's done so get rid of the sevent stuff
 * and tell the process which initiated the join that it's done.
 */

static void sevent_done(sm_sevent_t *sev)
{
	sm_group_t *sg = sev->se_sg;

	list_del(&sev->se_list);
	release_sevent(sev);
	kfree(sev);
	complete(&sg->event_comp);
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

static void process_join_sevent(sm_sevent_t *sev)
{
	int error = 0;

	/*
	 * We may cancel the current join attempt if another node is also
	 * attempting to join or leave. (Only a single node can join or leave
	 * at once.)  If cancelled, 0ur join attempt will be restarted later.
	 */

	if (test_and_clear_bit(SEFL_CANCEL, &sev->se_flags)) {
		error = 1;
		goto cancel;
	}

	log_debug(sev->se_sg, "sevent state %u", sev->se_state);

	switch (sev->se_state) {

		/*
		 * An sevent is created in kcl_join_service with a state of
		 * JOIN_BEGIN.
		 */

	case SEST_JOIN_BEGIN:
		sev->se_state = SEST_JOIN_ACKWAIT;
		error = send_join_notice(sev);
		break;

		/*
		 * se_state is changed from JOIN_ACKWAIT to JOIN_ACKED in 
		 * process_reply  (when all the replies have been received)
		 */

	case SEST_JOIN_ACKED:
		error = check_join_notice(sev);
		if (error)
			break;

		sev->se_state = SEST_JSTOP_ACKWAIT;
		error = send_join_stop(sev);
		break;

		/*
		 * se_state is changed from JSTOP_ACKWAIT to JSTOP_ACKED in
		 * proces_reply  (when all the replies have been received)
		 */

	case SEST_JSTOP_ACKED:
		error = check_join_stop(sev);
		if (error)
			break;

		sev->se_state = SEST_JSTART_SERVICEWAIT;
		error = send_join_start(sev);
		break;

		/*
		 * se_state is changed from JSTART_SERVICEWAIT to
		 * JSTART_SERVICEDONE in kcl_start_done
		 */

	case SEST_JSTART_SERVICEDONE:
		sev->se_state = SEST_BARRIER_WAIT;
		error = startdone_barrier_new(sev);
		break;

		/*
		 * se_state is changed from BARRIER_WAIT to BARRIER_DONE in
		 * process_startdone_barrier_new 
		 */

	case SEST_BARRIER_DONE:
		error = check_startdone_barrier_new(sev);
		if (error)
			break;

		do_finish_new(sev);
		sevent_done(sev);
		break;

	default:
		log_error(sev->se_sg, "no join processing for state %u",
			  sev->se_state);
	}

      cancel:
	if (error) {
		/* restart the sevent from the beginning */
		log_debug(sev->se_sg, "process_join error %d %lx", error,
			  sev->se_flags);
		sev->se_state = SEST_JOIN_BEGIN;
		sev->se_sg->global_id = 0;
		set_bit(SEFL_DELAY, &sev->se_flags);
		schedule_sev_restart(sev);
	}
}

/*
 * 1.  First step in leaving an SG - send a message to other SG members asking
 * to leave the SG.  Nodes that don't have another active sevent or uevent for
 * this SG will return POS.
 */

static int send_leave_notice(sm_sevent_t *sev)
{
	sm_group_t *sg = sev->se_sg;
	sm_node_t *node;
	char *msg;
	int i = 0, error = -1, len = 0;

	/*
	 * Create a node array from member list in which to collect responses.
	 */

	error = init_sevent(sev);
	if (error)
		goto out;

	list_for_each_entry(node, &sg->memb, list)
		sev->se_node_ids[i++] = node->id;

	/*
	 * Create and send a leave request message.
	 */

	msg = create_smsg(sg, SMSG_LEAVE_REQ, 0, &len, sev);

	error = send_members_message_sev(sg, msg, len, sev);

      out:
	return error;
}

/*
 * 2.  Second step in leaving an SG - after we collect all replies to our leave
 * request, we look at them.  If anyone replied with WAIT, we abort our attempt
 * at leaving and try again in a bit.
 */

static int check_leave_notice(sm_sevent_t *sev)
{
	int pos = 0, wait = 0, neg = 0, restart = 0, i;

	for (i = 0; i < sev->se_memb_count; i++) {
		switch (sev->se_node_status[i]) {
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

static int send_leave_stop(sm_sevent_t *sev)
{
	sm_group_t *sg = sev->se_sg;
	char *msg;
	int error, len = 0;

	/*
	 * Re-init the status vector in which to collect responses.
	 */

	memset(sev->se_node_status, 0, sev->se_len_status);

	/*
	 * Create and send a stop message.
	 */

	msg = create_smsg(sg, SMSG_LSTOP_REQ, 0, &len, sev);

	error = send_members_message_sev(sg, msg, len, sev);
	if (error < 0)
		goto out;

	/*
	 * we and all others stop the SG now 
	 */

	sg->ops->stop(sg->service_data);

      out:
	return error;
}

/*
 * 4.  Fourth step in leaving the SG - check the replies to our stop request.
 * Same problem with getting different replies as check_join_stop.
 */

static int check_leave_stop(sm_sevent_t *sev)
{
	sm_group_t *sg = sev->se_sg;
	int i, pos = 0, neg = 0;

	for (i = 0; i < sev->se_memb_count; i++) {
		switch (sev->se_node_status[i]) {
		case STATUS_POS:
			pos++;
			break;

		case STATUS_NEG:
			log_error(sg, "check_leave_stop: fail from nodeid %u "
				  "(%d, %d, %u)", sev->se_node_ids[i],
				  pos, neg, sev->se_memb_count);
			neg++;
			break;

		default:
			log_error(sg, "check_leave_stop: status %u nodeid %u",
				  sev->se_node_status[i], sev->se_node_ids[i]);
			neg++;
			break;
		}
	}

	if (pos == sg->memb_count)
		return 0;

	return -1;
}

/*
 * 5.  Fifth step in leaving the SG - tell the other SG members to restart the
 * service without us.  We, of course, don't start our own stopped service.  If
 * we're the last SG member and leaving, we jump right to the next step.
 */

static int send_leave_start(sm_sevent_t *sev)
{
	sm_group_t *sg = sev->se_sg;
	char *msg;
	int error = 0, len = 0;

	if (sg->memb_count == 1) {
		sev->se_state = SEST_LSTART_REMOTEDONE;
		set_bit(SEFL_CHECK, &sev->se_flags);
		wake_serviced(DO_JOINLEAVE);
	} else {
		msg = create_smsg(sg, SMSG_LSTART_CMD, 0, &len, sev);
		error = send_members_message(sg, msg, len);
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

static void process_leave_sevent(sm_sevent_t *sev)
{
	int error = 0;

	/*
	 * We may cancel the current leave attempt if another node is also
	 * attempting to join or leave. (Only a single node can join or leave
	 * at once.)  Our leave attempt will be restarted after being
	 * cancelled.
	 */

	if (test_and_clear_bit(SEFL_CANCEL, &sev->se_flags)) {
		error = 1;
		goto cancel;
	}

	if (test_bit(SGFL_UEVENT, &sev->se_sg->flags)) {
		error = 2;
		goto cancel;
	}

	if (!list_empty(&sev->se_sg->joining)) {
		error = 3;
		goto cancel;
	}

	log_debug(sev->se_sg, "sevent state %u", sev->se_state);

	switch (sev->se_state) {

		/*
		 * An sevent is created in kcl_leave_service with a state of
		 * LEAVE_BEGIN.
		 */

	case SEST_LEAVE_BEGIN:
		sev->se_state = SEST_LEAVE_ACKWAIT;
		error = send_leave_notice(sev);
		break;

		/*
		 * se_state is changed from LEAVE_ACKWAIT to LEAVE_ACKED in 
		 * process_reply  (when all the replies have been received)
		 */

	case SEST_LEAVE_ACKED:
		error = check_leave_notice(sev);
		if (error)
			break;

		sev->se_state = SEST_LSTOP_ACKWAIT;
		error = send_leave_stop(sev);
		break;

		/*
		 * se_state is changed from LSTOP_ACKWAIT to LSTOP_ACKED in
		 * process_reply
		 */

	case SEST_LSTOP_ACKED:
		error = check_leave_stop(sev);
		if (error)
			break;

		sev->se_state = SEST_LSTART_WAITREMOTE;
		error = send_leave_start(sev);
		break;

		/*
		 * se_state is changed from LSTART_WAITREMOTE to
		 * LSTART_REMOTEDONE in process_leave_done
		 */

	case SEST_LSTART_REMOTEDONE:
		sevent_done(sev);
		break;

	default:
		log_error(sev->se_sg, "process_leave_sevent state=%u",
			  sev->se_state);
	}

 cancel:
	if (error) {
		log_debug(sev->se_sg, "process_leave error %d %lx", error,
			  sev->se_flags);
		/* restart the sevent from the beginning */
		sev->se_state = SEST_LEAVE_BEGIN;
		set_bit(SEFL_DELAY, &sev->se_flags);
		schedule_sev_restart(sev);
	}
}

/*
 * Sevent backout code.  Take appropriate steps when a recovery occurs while
 * we're in the midst of an sevent.  The recovery may or may not affect the
 * sevent.  If it does, it usually means cancelling the sevent and restarting
 * it from the beginning once the recovery processing is done.
 */

/*
 * If any of the nodes that replied with OK is dead, we give up on the current
 * join attempt and restart.  Otherwise, this sevent can continue.
 */

static int backout_join_acked(sm_sevent_t *sev)
{
	sm_node_t *node;
	int i;

	for (i = 0; i < sev->se_node_count; i++) {
		if (sev->se_node_status[i] != STATUS_POS)
			continue;

		list_for_each_entry(node, &sm_members, list) {
			if (test_bit(SNFL_NEED_RECOVERY, &node->flags) &&
			    (node->id == sev->se_node_ids[i]))
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

static int backout_jstop_ackwait(sm_sevent_t *sev)
{
	sm_group_t *sg = sev->se_sg;

	if (!test_bit(SGFL_NEED_RECOVERY, &sg->flags))
		return FALSE;

	clear_bit(SEFL_ALLOW_JSTOP, &sev->se_flags);
	free_sg_memb(sg);
	return TRUE;
}

/*
 * Same as previous.
 */

static int backout_jstop_acked(sm_sevent_t *sev)
{
	return backout_jstop_ackwait(sev);
}

/*
 * If NEED_RECOVERY is set a member of the sg we're joining died while we were
 * starting our service.  The recovery process will restart the service on all
 * the prior sg members (not including those that died or us).  We will
 * reattempt our join which should be accepted once the nodes are done with
 * recovery.
 */

static int backout_jstart_servicewait(sm_sevent_t *sev)
{
	sm_group_t *sg = sev->se_sg;

	if (!test_bit(SGFL_NEED_RECOVERY, &sg->flags))
		return FALSE;

	clear_bit(SEFL_ALLOW_STARTDONE, &sev->se_flags);
	sg->ops->stop(sg->service_data);
	free_sg_memb(sg);
	return TRUE;
}

/*
 * Same as previous.
 */

static int backout_jstart_servicedone(sm_sevent_t *sev)
{
	return backout_jstart_servicewait(sev);
}

/*
 * If NEED_RECOVERY is set a member of the sg we're joining died while we were
 * waiting on the "all done" barrier.  Stop our service that we just started
 * and cancel the barrier.  The recovery process will restart the service on
 * all the prior sg members (not including those that died or us).  We will
 * reattempt our join which should be accepted once the nodes are done with
 * recovery.
 */

static int backout_barrier_wait(sm_sevent_t *sev)
{
	sm_group_t *sg = sev->se_sg;
	char bname[MAX_BARRIER_NAME_LEN];

	if (!test_bit(SGFL_NEED_RECOVERY, &sg->flags))
		return FALSE;

	clear_bit(SEFL_ALLOW_BARRIER, &sev->se_flags);

	sg->ops->stop(sg->service_data);

	memset(bname, 0, MAX_BARRIER_NAME_LEN);
	snprintf(bname, MAX_BARRIER_NAME_LEN, "sm.%u.%u.%u.%u",
		 sg->global_id, sm_our_nodeid, sev->se_id,
		 sg->memb_count);
	kcl_barrier_cancel(bname);

	free_sg_memb(sg);
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

static int backout_barrier_done(sm_sevent_t *sev)
{
	sm_group_t *sg = sev->se_sg;

	if (!test_bit(SGFL_NEED_RECOVERY, &sg->flags))
		return FALSE;

	if (!sev->se_barrier_status) {
		do_finish_new(sev);
		sevent_done(sev);
		return FALSE;
	} else {
		sg->ops->stop(sg->service_data);
		free_sg_memb(sg);
		return TRUE;
	}
}

/*
 * We've done nothing yet, just restart when recovery is done (if sg is flagged
 * with recovery.)
 */

static int backout_leave_begin(sm_sevent_t *sev)
{
	sm_group_t *sg = sev->se_sg;

	if (!test_bit(SGFL_NEED_RECOVERY, &sg->flags))
		return FALSE;

	return TRUE;
}

/*
 * Ignore any replies to our leave notice and restart when recovery is done (if
 * sg is flagged with recovery.)
 */

static int backout_leave_ackwait(sm_sevent_t *sev)
{
	sm_group_t *sg = sev->se_sg;

	if (!test_bit(SGFL_NEED_RECOVERY, &sg->flags))
		return FALSE;

	clear_bit(SEFL_ALLOW_LEAVE, &sev->se_flags);

	return TRUE;
}

/*
 * Same as previous.
 */

static int backout_leave_acked(sm_sevent_t *sev)
{
	return backout_leave_ackwait(sev);
}

/*
 * Ignore any stop replies.  All the members will be stopped anyway to do the
 * recovery.  Let that happen and restart our leave when done.
 */

static int backout_lstop_ackwait(sm_sevent_t *sev)
{
	sm_group_t *sg = sev->se_sg;

	if (!test_bit(SGFL_NEED_RECOVERY, &sg->flags))
		return FALSE;

	clear_bit(SEFL_ALLOW_LSTOP, &sev->se_flags);

	return TRUE;
}

/*
 * Same as previous.
 */

static int backout_lstop_acked(sm_sevent_t *sev)
{
	return backout_lstop_ackwait(sev);
}

/*
 * All members will be stopped due to recovery and restarted by recovery
 * processing.  That includes us, we have to retry the leave once the recovery
 * is done.
 */

static int backout_lstart_waitremote(sm_sevent_t *sev)
{
	sm_group_t *sg = sev->se_sg;

	if (!test_bit(SGFL_NEED_RECOVERY, &sg->flags))
		return FALSE;

	return TRUE;
}

/*
 * Reset an sevent to its beginning so it can be restarted.  This is necessary
 * when recovery affects an SG while we're trying to join or leave (ie. a node
 * in the SG fails).
 */

void backout_sevents(void)
{
	sm_sevent_t *sev, *safe;
	int delay;

	list_for_each_entry_safe(sev, safe, &joinleave_events, se_list) {

		delay = FALSE;

		log_debug(sev->se_sg, "backout sevent state %u", sev->se_state);

		switch (sev->se_state) {

		/* backout after kcl_join_service and before
		 * send_join_notice */
		case SEST_JOIN_BEGIN:
			break;

		/* backout after send_join_notice and before final
		 * process_reply */
		case SEST_JOIN_ACKWAIT:
			clear_bit(SEFL_ALLOW_JOIN, &sev->se_flags);
			sev->se_state = SEST_JOIN_BEGIN;
			set_bit(SEFL_CHECK, &sev->se_flags);
			wake_serviced(DO_JOINLEAVE);
			break;

		/* backout after final process_reply and before
		 * check_join_notice */
		case SEST_JOIN_ACKED:
			delay = backout_join_acked(sev);
			break;

		/* backout after send_join_stop and before final
		 * process_reply */
		case SEST_JSTOP_ACKWAIT:
			delay = backout_jstop_ackwait(sev);
			break;

		/* backout after final process_reply and before
		 * check_join_stop */
		case SEST_JSTOP_ACKED:
			delay = backout_jstop_acked(sev);
			break;

		/* backout after send_join_start and before
		 * kcl_start_done */
		case SEST_JSTART_SERVICEWAIT:
			delay = backout_jstart_servicewait(sev);
			break;

		/* backout after kcl_start_done and before
		 * startdone_barrier_new */
		case SEST_JSTART_SERVICEDONE:
			delay = backout_jstart_servicedone(sev);
			break;

		/* backout after startdone_barrier_new and before
		 * callback_startdone_barrier_new */
		case SEST_BARRIER_WAIT:
			delay = backout_barrier_wait(sev);
			break;

		/* backout after callback_startdone_barrier_new and
		 * before check_startdone_barrier_new */
		case SEST_BARRIER_DONE:
			delay = backout_barrier_done(sev);
			break;

		/* backout after kcl_leave_service and before
		 * send_leave_notice */
		case SEST_LEAVE_BEGIN:
			delay = backout_leave_begin(sev);
			break;

		/* backout after send_leave_notice and before final
		 * process_reply */
		case SEST_LEAVE_ACKWAIT:
			delay = backout_leave_ackwait(sev);
			break;

		/* backout after final process_reply and before
		 * check_leave_notice */
		case SEST_LEAVE_ACKED:
			delay = backout_leave_acked(sev);
			break;

		/* backout after send_leave_stop and before final
		 * process_reply */
		case SEST_LSTOP_ACKWAIT:
			delay = backout_lstop_ackwait(sev);
			break;

		/* backout after final process_reply and before
		 * check_leave_stop */
		case SEST_LSTOP_ACKED:
			delay = backout_lstop_acked(sev);
			break;

		/* backout after send_leave_start and before
		 * process_lstart_done */
		case SEST_LSTART_WAITREMOTE:
			delay = backout_lstart_waitremote(sev);
			break;

		/* backout after process_lstart_done and before
		 * process_leave_sevent */
		case SEST_LSTART_REMOTEDONE:
			sevent_done(sev);
			delay = FALSE;
			break;

		default:
			log_error(sev->se_sg, "backout_sevents: bad state %d",
				  sev->se_state);
		}

		if (delay) {
			if (test_bit(SEFL_LEAVE, &sev->se_flags)) {
				sev->se_state = SEST_LEAVE_BEGIN;
				set_bit(SEFL_DELAY_RECOVERY, &sev->se_flags);
				set_bit(SEFL_CHECK, &sev->se_flags);
				wake_serviced(DO_JOINLEAVE);
			} else {
				sev->se_state = SEST_JOIN_BEGIN;
				set_bit(SEFL_CHECK, &sev->se_flags);
				wake_serviced(DO_JOINLEAVE);
			}
		}
	}
}

void process_joinleave(void)
{
	sm_sevent_t *sev = NULL, *safe;

	spin_lock(&new_event_lock);
	if (!list_empty(&new_event)) {
		sev = list_entry(new_event.next, sm_sevent_t, se_list);
		list_del(&sev->se_list);
		list_add_tail(&sev->se_list, &joinleave_events);
		set_bit(SEFL_CHECK, &sev->se_flags);
	}
	spin_unlock(&new_event_lock);

	list_for_each_entry_safe(sev, safe, &joinleave_events, se_list) {
		if (!test_and_clear_bit(SEFL_CHECK, &sev->se_flags))
			continue;

		if (test_bit(SEFL_DELAY, &sev->se_flags) ||
		    test_bit(SEFL_DELAY_RECOVERY, &sev->se_flags))
			continue;

		if (sev->se_state < SEST_LEAVE_BEGIN)
			process_join_sevent(sev);
		else
			process_leave_sevent(sev);
	}
}
