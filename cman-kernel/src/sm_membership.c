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

extern struct list_head		sm_members;

/*
 * Routines for SG members to handle other nodes joining or leaving the SG.
 * These "uevent" membership update routines are the response to an "sevent" on
 * a joining/leaving node.
 */

static void del_memb_node(sm_group_t *sg, uint32_t nodeid)
{
	sm_node_t *node;

	list_for_each_entry(node, &sg->memb, list) {
		if (node->id != nodeid)
			continue;
		list_del(&node->list);
		kfree(node);
		sg->memb_count--;
		log_debug(sg, "del node %u count %d", nodeid, sg->memb_count);
		break;
	}
}

static void add_memb_node(sm_group_t *sg, sm_node_t *node)
{
	list_add_tail(&node->list, &sg->memb);
	sg->memb_count++;
	log_debug(sg, "add node %u count %d", node->id, sg->memb_count);
}

/*
 * Join 1.  The receive end of send_join_stop() from a node requesting to join
 * the SG.  We stop the service so it can be restarted with the new node.
 */

static int process_join_stop(sm_group_t *sg)
{
	sm_uevent_t *uev = &sg->uevent;
	sm_node_t *node;
	sm_msg_t reply;
	int error;

	if (uev->ue_num_nodes != sg->memb_count + 1) {
		log_error(sg, "process_join_stop: bad num nodes %u %u",
			  uev->ue_num_nodes, sg->memb_count);
		return -1;
	}

	sm_set_event_id(&uev->ue_id);

	node = sm_find_joiner(sg, uev->ue_nodeid);
	if (!node) {
		log_error(sg, "process_join_stop: no node %d", uev->ue_nodeid);
		return -1;
	}

	sg->state = SGST_UEVENT;
	sg->ops->stop(sg->service_data);

	reply.ms_type = SMSG_JSTOP_REP;
	reply.ms_status = STATUS_POS;
	reply.ms_sevent_id = uev->ue_remote_seid;
	smsg_bswap_out(&reply);

	error = send_nodeid_message((char *) &reply, sizeof(reply),
				    uev->ue_nodeid);
	if (error < 0)
		return error;
	return 0;
}

/*
 * Join 2.  The receive end of send_join_start() from a node joining the SG.
 * We are re-starting the service with the new member added.
 */

static int process_join_start(sm_group_t *sg)
{
	sm_uevent_t *uev = &sg->uevent;
	sm_node_t *node;
	uint32_t *memb;
	int count = 0;

	/* this memory is passed to the service which must free it */
	SM_RETRY(memb =
		 kmalloc((sg->memb_count + 1) * sizeof(uint32_t), GFP_KERNEL),
		 memb);

	/* transfer joining node from joining list to member list */
	node = sm_find_joiner(sg, uev->ue_nodeid);
	SM_ASSERT(node, printk("nodeid=%u\n", uev->ue_nodeid););
	list_del(&node->list);
	add_memb_node(sg, node);

	/* the new member list for the service */
	list_for_each_entry(node, &sg->memb, list)
		memb[count++] = node->id;

	set_bit(UEFL_ALLOW_STARTDONE, &uev->ue_flags);

	sg->ops->start(sg->service_data, memb, count, uev->ue_id,
		       SERVICE_NODE_JOIN);
	return 0;
}

/*
 * Join 3.  When done starting their local service, every previous SG member
 * calls startdone_barrier() and the new/joining member calls
 * startdone_barrier_new().  The barrier returns when everyone has started
 * their service and joined the barrier.
 */

static int startdone_barrier(sm_group_t *sg)
{
	sm_uevent_t *uev = &sg->uevent;
	char bname[MAX_BARRIER_NAME_LEN];
	int error;

	memset(bname, 0, MAX_BARRIER_NAME_LEN);
	uev->ue_barrier_status = -1;

	set_bit(UEFL_ALLOW_BARRIER, &uev->ue_flags);

	/* If we're the only member, skip the barrier */
	if (sg->memb_count == 1) {
		process_startdone_barrier(sg, 0);
		return 0;
	}

	snprintf(bname, MAX_BARRIER_NAME_LEN, "sm.%u.%u.%u.%u",
		 sg->global_id, uev->ue_nodeid, uev->ue_remote_seid,
		 sg->memb_count);

	error = sm_barrier(bname, sg->memb_count, SM_BARRIER_STARTDONE);

	return error;
}

/*
 * Join 4.  Check that the "all started" barrier returned a successful status.
 * The newly joined member calls check_startdone_barrier_new().
 */

static int check_startdone_barrier(sm_group_t *sg)
{
	int error = sg->uevent.ue_barrier_status;
	return error;
}

/*
 * Join 5.  Send the service a "finish" indicating that all members have
 * successfully started.  The newly joined member calls do_finish_new().
 */

static void do_finish(sm_group_t *sg)
{
	sg->state = SGST_RUN;
	clear_bit(SGFL_UEVENT, &sg->flags);
	sg->ops->finish(sg->service_data, sg->uevent.ue_id);
}

/*
 * Join 6.  The uevent is done.  If this was a uevent for a node leaving the
 * SG, then send a final message to the departed node signalling that the
 * remaining nodes have restarted since it left.
 */

static void uevent_done(sm_group_t *sg)
{
	sm_uevent_t *uev = &sg->uevent;
	sm_msg_t reply;

	if (test_bit(UEFL_LEAVE, &uev->ue_flags)) {
		reply.ms_type = SMSG_LSTART_DONE;
		reply.ms_status = STATUS_POS;
		reply.ms_sevent_id = uev->ue_remote_seid;
		smsg_bswap_out(&reply);
		send_nodeid_message((char *) &reply, sizeof(reply),
				    uev->ue_nodeid);
	}
	memset(&sg->uevent, 0, sizeof(sm_uevent_t));
}

/*
 * Leave 1.  The receive end of send_leave_stop() from a node leaving the SG.
 */

static int process_leave_stop(sm_group_t *sg)
{
	sm_uevent_t *uev = &sg->uevent;
	sm_msg_t reply;
	int error;

	sm_set_event_id(&uev->ue_id);

	sg->state = SGST_UEVENT;
	sg->ops->stop(sg->service_data);

	reply.ms_type = SMSG_LSTOP_REP;
	reply.ms_status = STATUS_POS;
	reply.ms_sevent_id = uev->ue_remote_seid;
	smsg_bswap_out(&reply);

	error = send_nodeid_message((char *) &reply, sizeof(reply),
				    uev->ue_nodeid);
	if (error < 0)
		return error;
	return 0;
}

/*
 * Leave 2.  The receive end of send_leave_start() from a node leaving the SG.
 * We are re-starting the service (without the node that's left naturally.)
 */

static int process_leave_start(sm_group_t *sg)
{
	sm_uevent_t *uev = &sg->uevent;
	sm_node_t *node;
	uint32_t *memb;
	int count = 0;

	SM_ASSERT(sg->memb_count > 1,
		  printk("memb_count=%u\n", sg->memb_count););

	/* this memory is passed to the service which must free it */
	SM_RETRY(memb =
		 kmalloc((sg->memb_count - 1) * sizeof(uint32_t), GFP_KERNEL),
		 memb);

	/* remove departed member from sg member list */
	del_memb_node(sg, uev->ue_nodeid);

	/* build member list to pass to service */
	list_for_each_entry(node, &sg->memb, list)
		memb[count++] = node->id;

	/* allow us to accept the start_done callback for this start */
	set_bit(UEFL_ALLOW_STARTDONE, &uev->ue_flags);

	sg->ops->start(sg->service_data, memb, count, uev->ue_id,
		       SERVICE_NODE_LEAVE);
	return 0;
}

/*
 * Move through the steps of another node joining or leaving the SG.
 */

static void process_one_uevent(sm_group_t *sg)
{
	sm_uevent_t *uev = &sg->uevent;
	int error = 0;

	log_debug(sg, "uevent state %u node %u", uev->ue_state, uev->ue_nodeid);

	switch (uev->ue_state) {

		/*
		 * a uevent is initialized with state JSTOP in
		 * process_stop_request
		 */

	case UEST_JSTOP:
		uev->ue_state = UEST_JSTART_WAITCMD;
		error = process_join_stop(sg);
		break;

		/*
		 * ue_state is changed from JSTART_WAITCMD to JSTART in
		 * process_start_request
		 */

	case UEST_JSTART:
		uev->ue_state = UEST_JSTART_SERVICEWAIT;
		error = process_join_start(sg);
		break;

		/*
		 * ue_state is changed from JSTART_SERVICEWAIT to
		 * JSTART_SERVICEDONE in kcl_start_done
		 */

	case UEST_JSTART_SERVICEDONE:
		uev->ue_state = UEST_BARRIER_WAIT;
		error = startdone_barrier(sg);
		break;

		/*
		 * ue_state is changed from BARRIER_WAIT to BARRIER_DONE in
		 * process_startdone_barrier
		 */

	case UEST_BARRIER_DONE:
		error = check_startdone_barrier(sg);
		if (error)
			break;

		do_finish(sg);
		uevent_done(sg);
		break;

		/*
		 * a uevent is initialized with state LSTOP in
		 * process_stop_request
		 */

	case UEST_LSTOP:
		uev->ue_state = UEST_LSTART_WAITCMD;
		error = process_leave_stop(sg);
		break;

		/*
		 * a uevent is changed from LSTART_WAITCMD to LSTART in
		 * process_start_request
		 */

	case UEST_LSTART:
		uev->ue_state = UEST_LSTART_SERVICEWAIT;
		error = process_leave_start(sg);
		break;

		/*
		 * a uevent is changed from LSTART_SERVICEWAIT to to
		 * LSTART_SERVICEDONE in kcl_start_done
		 */

	case UEST_LSTART_SERVICEDONE:
		uev->ue_state = UEST_BARRIER_WAIT;
		error = startdone_barrier(sg);
		break;

	default:
		error = -1;
	}

	/* If we encounter an error during these routines, we do nothing, 
	   expecting that a node failure related to this sg will cause a
	   recovery event to arrive and call cancel_one_uevent(). */

	if (error)
		log_error(sg, "process_one_uevent error %d state %u",
			  error, uev->ue_state);
}

static sm_node_t *failed_memb(sm_group_t *sg, int *count)
{
	sm_node_t *node, *sm_node, *failed_uev_node = NULL;

	list_for_each_entry(node, &sg->memb, list) {

		sm_node = sm_find_member(node->id);
		SM_ASSERT(sm_node, );

		if (test_bit(SNFL_NEED_RECOVERY, &sm_node->flags)) {
			(*count)++;
			if (node->id == sg->uevent.ue_nodeid)
				failed_uev_node = sm_node;
		}
	}
	return failed_uev_node;
}

static void send_recover_msg(sm_group_t *sg)
{
	char *msg;
	int len = 0;
	msg = create_smsg(sg, SMSG_RECOVER, 0, &len, NULL);
	send_members_message(sg, msg, len);
}

static void cancel_barrier(sm_group_t *sg)
{
	sm_uevent_t *uev = &sg->uevent;
	char bname[MAX_BARRIER_NAME_LEN];

	clear_bit(UEFL_ALLOW_BARRIER, &uev->ue_flags);

	memset(bname, 0, MAX_BARRIER_NAME_LEN);
	snprintf(bname, MAX_BARRIER_NAME_LEN, "sm.%u.%u.%u.%u",
		 sg->global_id, uev->ue_nodeid, uev->ue_remote_seid,
		 sg->memb_count);
	kcl_barrier_cancel(bname);
}

static void cancel_one_uevent(sm_group_t *sg, int *effected)
{
	sm_uevent_t *uev = &sg->uevent;
	int failed_count;
	sm_node_t *node, *failed_joiner, *failed_leaver;

	log_debug(sg, "cancel uevent state %u node %u", uev->ue_state,
		  uev->ue_nodeid);

	switch (uev->ue_state) {

	case UEST_JSTOP:
	case UEST_JSTART_WAITCMD:
	case UEST_JSTART:

		sg->ops->stop(sg->service_data);

		failed_count = 0;
		failed_joiner = failed_memb(sg, &failed_count);
		SM_ASSERT(!failed_joiner, );

		node = sm_find_member(uev->ue_nodeid);
		if (test_bit(SNFL_NEED_RECOVERY, &node->flags))
			failed_joiner = node;

		if (!failed_count) {
			/* only joining node failed */
			SM_ASSERT(failed_joiner, );
			SM_ASSERT(!test_bit(SGFL_NEED_RECOVERY, &sg->flags), );
			set_bit(SGFL_NEED_RECOVERY, &sg->flags);
			(*effected)++;
			/* some nodes may not have gotten a JSTOP message
			   in which case this will tell them to begin
			   recovery for this sg. */
			send_recover_msg(sg);

		} else {
			/* a member node failed (and possibly joining node, it
			   doesn't matter) */
			SM_ASSERT(test_bit(SGFL_NEED_RECOVERY, &sg->flags), );
		}

		clear_bit(SGFL_UEVENT, &sg->flags);
		memset(uev, 0, sizeof(sm_uevent_t));
		break;


	case UEST_JSTART_SERVICEWAIT:
	case UEST_JSTART_SERVICEDONE:

		clear_bit(UEFL_ALLOW_STARTDONE, &uev->ue_flags);
		sg->ops->stop(sg->service_data);

		failed_count = 0;
		failed_joiner = failed_memb(sg, &failed_count);
		SM_ASSERT(failed_count, );
		SM_ASSERT(test_bit(SGFL_NEED_RECOVERY, &sg->flags), );

		if (failed_count == 1 && failed_joiner) {
			/* only joining node failed */

		} else if (failed_count && failed_joiner) {
			/* joining node and another member failed */

		} else {
			/* other member failed, joining node still alive */
			SM_ASSERT(!failed_joiner, );
			del_memb_node(sg, uev->ue_nodeid);
		}

		clear_bit(SGFL_UEVENT, &sg->flags);
		memset(uev, 0, sizeof(sm_uevent_t));
		break;


	case UEST_LSTOP:
	case UEST_LSTART_WAITCMD:
	case UEST_LSTART:

		sg->ops->stop(sg->service_data);

		failed_count = 0;
		failed_leaver = failed_memb(sg, &failed_count);
		SM_ASSERT(failed_count, );
		SM_ASSERT(test_bit(SGFL_NEED_RECOVERY, &sg->flags), );

		if (failed_count == 1 && failed_leaver) {
			/* only leaving node failed */

		} else if (failed_count && failed_leaver) {
			/* leaving node and another member failed */

		} else {
			/* other member failed, leaving node still alive */
			SM_ASSERT(!failed_leaver, );
		}

		clear_bit(SGFL_UEVENT, &sg->flags);
		memset(uev, 0, sizeof(sm_uevent_t));
		break;


	case UEST_LSTART_SERVICEWAIT:
	case UEST_LSTART_SERVICEDONE:

		clear_bit(UEFL_ALLOW_STARTDONE, &uev->ue_flags);
		sg->ops->stop(sg->service_data);

		failed_count = 0;
		failed_leaver = failed_memb(sg, &failed_count);
		SM_ASSERT(!failed_leaver, );

		node = sm_find_member(uev->ue_nodeid);
		if (test_bit(SNFL_NEED_RECOVERY, &node->flags))
			failed_leaver = node;

		if (!failed_count) {
			/* only leaving node failed */
			SM_ASSERT(failed_leaver, );
			SM_ASSERT(!test_bit(SGFL_NEED_RECOVERY, &sg->flags), );
			set_bit(SGFL_NEED_RECOVERY, &sg->flags);
			(*effected)++;

		} else if (failed_count && failed_leaver) {
			/* leaving node and another member failed */
			SM_ASSERT(test_bit(SGFL_NEED_RECOVERY, &sg->flags), );

		} else {
			/* other member failed, leaving node still alive */
			SM_ASSERT(failed_count, );
			SM_ASSERT(!failed_leaver, );
			SM_ASSERT(test_bit(SGFL_NEED_RECOVERY, &sg->flags), );
			node = sm_new_node(sg->uevent.ue_nodeid);
			add_memb_node(sg, node);
		}

		clear_bit(SGFL_UEVENT, &sg->flags);
		memset(uev, 0, sizeof(sm_uevent_t));
		break;


	case UEST_BARRIER_WAIT:

		if (test_bit(UEFL_LEAVE, &uev->ue_flags))
			goto barrier_wait_leave;

		sg->ops->stop(sg->service_data);
		cancel_barrier(sg);

 	      barrier_wait_join:

		failed_count = 0;
		failed_joiner = failed_memb(sg, &failed_count);
		SM_ASSERT(failed_count, );
		SM_ASSERT(test_bit(SGFL_NEED_RECOVERY, &sg->flags), );

		if (failed_count == 1 && failed_joiner) {
			/* only joining node failed */

		} else if (failed_count && failed_joiner) {
			/* joining node and another member failed */

		} else {
			/* other member failed, joining node still alive */
			SM_ASSERT(!failed_joiner, );
			del_memb_node(sg, uev->ue_nodeid);
		}

		clear_bit(SGFL_UEVENT, &sg->flags);
		memset(uev, 0, sizeof(sm_uevent_t));
		break;

              barrier_wait_leave:

		failed_count = 0;
		failed_leaver = failed_memb(sg, &failed_count);
		SM_ASSERT(!failed_leaver, );

		node = sm_find_member(uev->ue_nodeid);
		if (test_bit(SNFL_NEED_RECOVERY, &node->flags))
			failed_leaver = node;

		if (!failed_count) {
			/* only leaving node failed */
			SM_ASSERT(failed_leaver, );
			SM_ASSERT(!test_bit(SGFL_NEED_RECOVERY, &sg->flags), );
			set_bit(SGFL_NEED_RECOVERY, &sg->flags);
			(*effected)++;

		} else if (failed_count && failed_leaver) {
			/* leaving node and another member failed */
			SM_ASSERT(test_bit(SGFL_NEED_RECOVERY, &sg->flags), );

		} else {
			/* other member failed, leaving node still alive */
			SM_ASSERT(failed_count, );
			SM_ASSERT(!failed_leaver, );
			SM_ASSERT(test_bit(SGFL_NEED_RECOVERY, &sg->flags), );
			node = sm_new_node(sg->uevent.ue_nodeid);
			add_memb_node(sg, node);
		}

		clear_bit(SGFL_UEVENT, &sg->flags);
		memset(uev, 0, sizeof(sm_uevent_t));
		break;


	case UEST_BARRIER_DONE:

		if (!uev->ue_barrier_status) {
			do_finish(sg);
			uevent_done(sg);
			break;
		} 

		if (test_bit(UEFL_LEAVE, &uev->ue_flags))
			goto barrier_wait_leave;
		else
			goto barrier_wait_join;


	default:
		log_error(sg, "cancel_one_uevent: state %d", uev->ue_state);
	}
}

void cancel_uevents(int *effected)
{
	sm_group_t *sg;
	sm_node_t *node, *sgnode;
	int i;

	list_for_each_entry(node, &sm_members, list) {
		if (!test_bit(SNFL_NEED_RECOVERY, &node->flags))
			continue;

		/*
		 * Clear this dead node from the "interested in joining" list
		 * of any SG.  The node is added to this list before the uevent
		 * begins.
		 */

		for (i = 0; i < SG_LEVELS; i++) {
			list_for_each_entry(sg, &sm_sg[i], list) {
				sgnode = sm_find_joiner(sg, node->id);
				if (sgnode) {
					log_debug(sg, "clear joining node %u",
						  sgnode->id);
					list_del(&sgnode->list);
					kfree(sgnode);
				}
			}
		}
	}

	 /* Adjust any uevents in sg's effected by the failed node(s) */

	for (i = 0; i < SG_LEVELS; i++) {
		list_for_each_entry(sg, &sm_sg[i], list) {
			if (!test_bit(SGFL_UEVENT, &sg->flags))
				continue;

			/* We may have some cancelling to do if this sg is
			   flagged as having a failed member, or if a joining
			   or leaving node has died. */
			   
			if (test_bit(SGFL_NEED_RECOVERY, &sg->flags))
				cancel_one_uevent(sg, effected);
			else if (sg->uevent.ue_nodeid) {
				node = sm_find_member(sg->uevent.ue_nodeid);
				SM_ASSERT(node, );
				if (test_bit(SNFL_NEED_RECOVERY, &node->flags))
					cancel_one_uevent(sg, effected);
			}
		}
	}
}

void process_membership(void)
{
	sm_group_t *sg;
	int i;

	down(&sm_sglock);

	for (i = 0; i < SG_LEVELS; i++) {
		list_for_each_entry(sg, &sm_sg[i], list) {
			if (!test_bit(SGFL_UEVENT, &sg->flags))
				continue;

			if (!test_and_clear_bit(UEFL_CHECK,
						&sg->uevent.ue_flags))
				continue;

			process_one_uevent(sg);
		}
	}
	up(&sm_sglock);
}
