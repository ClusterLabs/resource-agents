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
 * Routines for SG members to handle other nodes joining or leaving the SG.
 * These "update" membership update routines are the response to an "sevent" on
 * a joining/leaving node.
 */

static void del_memb_node(group_t *g, int nodeid)
{
	node_t *node;

	list_for_each_entry(node, &g->memb, list) {
		if (node->id != nodeid)
			continue;
		list_del(&node->list);
		free(node);
		g->memb_count--;
		log_group(g, "del node %u count %d", nodeid, g->memb_count);
		break;
	}
}

static void add_memb_node(group_t *g, node_t *node)
{
	list_add_tail(&node->list, &g->memb);
	g->memb_count++;
	log_group(g, "add node %u count %d", node->id, g->memb_count);
}

/*
 * Join 1.  The receive end of send_join_stop() from a node requesting to join
 * the SG.  We stop the service so it can be restarted with the new node.
 */

static int process_join_stop(group_t *g)
{
	update_t *up = g->update;
	node_t *node;

	if (up->num_nodes != g->memb_count + 1) {
		log_error(g, "process_join_stop: bad num nodes %u %u",
			  up->num_nodes, g->memb_count);
		return -1;
	}

	node = find_joiner(g, up->nodeid);
	if (!node) {
		log_error(g, "process_join_stop: no node %d", up->nodeid);
		return -1;
	}

	g->state = GST_UPDATE;
	group_stop(g);
	return 0;
}

static int process_join_stopdone(group_t *g)
{
	update_t *up = g->update;
	msg_t reply;
	int error;

	reply.ms_type = SMSG_JSTOP_REP;
	reply.ms_status = STATUS_POS;
	reply.ms_event_id = up->remote_seid;
	strcpy(reply.ms_info, g->join_info);
	reply.ms_to_nodeid = up->nodeid;
	msg_copy_out(&reply);

	error = send_nodeid_message((char *) &reply, sizeof(reply), up->nodeid);
	if (error < 0)
		return error;
	return 0;
}

/*
 * Join 2.  The receive end of send_join_start() from a node joining the SG.
 * We are re-starting the service with the new member added.
 */

static int process_join_start(group_t *g)
{
	update_t *up = g->update;
	node_t *node;
	int *memb;
	int count = 0;

	memb = malloc((g->memb_count + 1) * sizeof(int));

	/* transfer joining node from joining list to member list */
	node = find_joiner(g, up->nodeid);
	ASSERT(node, log_error(g, "nodeid=%u", up->nodeid););
	list_del(&node->list);
	add_memb_node(g, node);

	/* the new member list for the service */
	list_for_each_entry(node, &g->memb, list)
		memb[count++] = node->id;

	set_bit(UFL_ALLOW_STARTDONE, &up->flags);
	group_start(g, memb, count, up->id, NODE_JOIN);
	return 0;
}

/*
 * Join 3.  When done starting their local service, every previous SG member
 * calls startdone_barrier() and the new/joining member calls
 * startdone_barrier_new().  The barrier returns when everyone has started
 * their service and joined the barrier.
 */

static int startdone_barrier(group_t *g)
{
	update_t *up = g->update;
	char bname[MAX_BARRIERLEN];
	int error;

	memset(bname, 0, MAX_BARRIERLEN);
	up->barrier_status = -1;

	set_bit(UFL_ALLOW_BARRIER, &up->flags);

	/* If we're the only member, skip the barrier */
	if (g->memb_count == 1)
		goto done;

	snprintf(bname, MAX_BARRIERLEN, "sm.%u.%u.%u.%u",
		 g->global_id, up->nodeid, up->remote_seid,
		 g->memb_count);

	error = do_barrier(g, bname, g->memb_count, GD_BARRIER_STARTDONE);
	if (error < 0) {
		log_error(g, "startdone_barrier error %d", error);
		clear_bit(UFL_ALLOW_BARRIER, &up->flags);
	}

	return error;

 done:
	clear_bit(UFL_ALLOW_BARRIER, &up->flags);
	up->barrier_status = 0;
	up->state = UST_BARRIER_DONE;
	return 0;
}

/*
 * Join 4.  Check that the "all started" barrier returned a successful status.
 * The newly joined member calls check_startdone_barrier_new().
 */

static int check_startdone_barrier(group_t *g)
{
	int error = g->update->barrier_status;
	return error;
}

/*
 * Join 5.  Send the service a "finish" indicating that all members have
 * successfully started.  The newly joined member calls do_finish_new().
 */

static void do_finish(group_t *g)
{
	g->state = GST_RUN;
	clear_bit(GFL_UPDATE, &g->flags);
	group_finish(g, g->update->id);
}

/*
 * Join 6.  The update is done.  If this was a update for a node leaving the
 * SG, then send a final message to the departed node signalling that the
 * remaining nodes have restarted since it left.
 */

static void update_done(group_t *g)
{
	update_t *up = g->update;
	msg_t reply;

	if (test_bit(UFL_LEAVE, &up->flags)) {
		reply.ms_type = SMSG_LSTART_DONE;
		reply.ms_status = STATUS_POS;
		reply.ms_event_id = up->remote_seid;
		reply.ms_to_nodeid = up->nodeid;
		msg_copy_out(&reply);
		send_nodeid_message((char *) &reply, sizeof(reply), up->nodeid);
	}
	free(up);
	g->update = NULL;
}

/*
 * Leave 1.  The receive end of send_leave_stop() from a node leaving the SG.
 */

static int process_leave_stop(group_t *g)
{
	update_t *up = g->update;

	g->state = GST_UPDATE;
	group_stop(g);
	return 0;
}

static int process_leave_stopdone(group_t *g)
{
	update_t *up = g->update;
	msg_t reply;
	int error;

	reply.ms_type = SMSG_LSTOP_REP;
	reply.ms_status = STATUS_POS;
	reply.ms_event_id = up->remote_seid;
	reply.ms_to_nodeid = up->nodeid;
	msg_copy_out(&reply);

	error = send_nodeid_message((char *) &reply, sizeof(reply), up->nodeid);
	if (error < 0)
		return error;
	return 0;
}

/*
 * Leave 2.  The receive end of send_leave_start() from a node leaving the SG.
 * We are re-starting the service (without the node that's left naturally.)
 */

static int process_leave_start(group_t *g)
{
	update_t *up = g->update;
	node_t *node;
	int *memb;
	int count = 0;

	ASSERT(g->memb_count > 1,
	       log_error(g, "memb_count=%u", g->memb_count););

	memb = malloc((g->memb_count - 1) * sizeof(int));

	/* remove departed member from sg member list */
	del_memb_node(g, up->nodeid);

	/* build member list to pass to service */
	list_for_each_entry(node, &g->memb, list)
		memb[count++] = node->id;

	/* allow us to accept the start_done callback for this start */
	set_bit(UFL_ALLOW_STARTDONE, &up->flags);

	group_start(g, memb, count, up->id, NODE_LEAVE);
	return 0;
}

/*
 * Move through the steps of another node joining or leaving the SG.
 */

static int process_one_update(group_t *g)
{
	update_t *up = g->update;
	int rv = 1, error = 0;

	switch (up->state) {

		/*
		 * a update is initialized with state JSTOP in
		 * process_stop_request
		 */

	case UST_JSTOP:
		up->state = UST_JSTOP_SERVICEWAIT;
		error = process_join_stop(g);
		break;

		/*
		 * state is changed from JSTOP_SERVICEWAIT to
		 * JSTOP_SERVICEDONE in do_stopdone
		 */

	case UST_JSTOP_SERVICEDONE:
		up->state = UST_JSTART_WAITCMD;
		error = process_join_stopdone(g);
		break;

		/*
		 * state is changed from JSTART_WAITCMD to JSTART in
		 * process_start_request
		 */

	case UST_JSTART:
		up->state = UST_JSTART_SERVICEWAIT;
		error = process_join_start(g);
		break;

		/*
		 * state is changed from JSTART_SERVICEWAIT to
		 * JSTART_SERVICEDONE in kcl_start_done
		 */

	case UST_JSTART_SERVICEDONE:
		up->state = UST_BARRIER_WAIT;
		error = startdone_barrier(g);
		break;

		/*
		 * state is changed from BARRIER_WAIT to BARRIER_DONE in
		 * process_startdone_barrier
		 */

	case UST_BARRIER_DONE:
		error = check_startdone_barrier(g);
		if (error)
			break;

		do_finish(g);
		update_done(g);
		up = NULL;
		break;

		/*
		 * a update is initialized with state LSTOP in
		 * process_stop_request
		 */

	case UST_LSTOP:
		up->state = UST_LSTOP_SERVICEWAIT;
		error = process_leave_stop(g);
		break;

		/*
		 * state is changed from LSTOP_SERVICEWAIT to
		 * LSTOP_SERVICEDONE in do_stopdone
		 */

	case UST_LSTOP_SERVICEDONE:
		up->state = UST_LSTART_WAITCMD;
		error = process_leave_stopdone(g);
		break;

		/*
		 * a update is changed from LSTART_WAITCMD to LSTART in
		 * process_start_request
		 */

	case UST_LSTART:
		up->state = UST_LSTART_SERVICEWAIT;
		error = process_leave_start(g);
		break;

		/*
		 * a update is changed from LSTART_SERVICEWAIT to to
		 * LSTART_SERVICEDONE in kcl_start_done
		 */

	case UST_LSTART_SERVICEDONE:
		up->state = UST_BARRIER_WAIT;
		error = startdone_barrier(g);
		break;

	default:
		/*
		log_error(g, "no update processing for state %u", up->state);
		*/
		up = NULL;
		rv = 0;
	}

	if (up)
		log_group(g, "update state %u node %u", up->state, up->nodeid);

	/* If we encounter an error during these routines, we do nothing, 
	   expecting that a node failure related to this sg will cause a
	   recovery event to arrive and call cancel_one_update(). */

	if (error)
		log_error(g, "process_one_update error %d state %u",
			  error, up->state);
	return rv;
}

static node_t *failed_memb(group_t *g, int *count)
{
	node_t *node, *gd_node, *failed_up_node = NULL;

	list_for_each_entry(node, &g->memb, list) {
		gd_node = find_node(node->id);
		ASSERT(gd_node, );

		if (test_bit(NFL_NEED_RECOVERY, &gd_node->flags)) {
			(*count)++;
			if (node->id == g->update->nodeid)
				failed_up_node = gd_node;
		}
	}
	return failed_up_node;
}

static void send_recover_msg(group_t *g)
{
	char *buf;
	int len = 0;

	buf = create_msg(g, SMSG_RECOVER, 0, &len, NULL);
	send_members_message(g, buf, len);
}

static void cancel_one_update(group_t *g, int *effected)
{
	update_t *up = g->update;
	int failed_count;
	node_t *node, *failed_joiner, *failed_leaver;

	log_group(g, "cancel update state %u node %u", up->state,
		  up->nodeid);

	switch (up->state) {

	case UST_JSTOP:
	case UST_JSTART_WAITCMD:
	case UST_JSTART:

		group_stop(g);

		failed_count = 0;
		failed_joiner = failed_memb(g, &failed_count);
		ASSERT(!failed_joiner, );

		node = find_node(up->nodeid);
		if (node && test_bit(NFL_NEED_RECOVERY, &node->flags))
			failed_joiner = node;

		if (!failed_count) {
			/* only joining node failed */
			ASSERT(failed_joiner, );
			ASSERT(!test_bit(GFL_NEED_RECOVERY, &g->flags), );
			set_bit(GFL_NEED_RECOVERY, &g->flags);
			(*effected)++;
			/* some nodes may not have gotten a JSTOP message
			   in which case this will tell them to begin
			   recovery for this sg. */
			send_recover_msg(g);

		} else {
			/* a member node failed (and possibly joining node, it
			   doesn't matter) */
			ASSERT(test_bit(GFL_NEED_RECOVERY, &g->flags), );
		}

		clear_bit(GFL_UPDATE, &g->flags);
		free(up);
		g->update = NULL;
		break;


	case UST_JSTART_SERVICEWAIT:
	case UST_JSTART_SERVICEDONE:

		clear_bit(UFL_ALLOW_STARTDONE, &up->flags);
		group_stop(g);

		failed_count = 0;
		failed_joiner = failed_memb(g, &failed_count);
		ASSERT(failed_count, );
		ASSERT(test_bit(GFL_NEED_RECOVERY, &g->flags), );

		if (failed_count == 1 && failed_joiner) {
			/* only joining node failed */

		} else if (failed_count && failed_joiner) {
			/* joining node and another member failed */

		} else {
			/* other member failed, joining node still alive */
			ASSERT(!failed_joiner, );
			del_memb_node(g, up->nodeid);
		}

		clear_bit(GFL_UPDATE, &g->flags);
		free(up);
		g->update = NULL;
		break;


	case UST_LSTOP:
	case UST_LSTART_WAITCMD:
	case UST_LSTART:

		group_stop(g);

		failed_count = 0;
		failed_leaver = failed_memb(g, &failed_count);
		ASSERT(failed_count, );
		ASSERT(test_bit(GFL_NEED_RECOVERY, &g->flags), );

		if (failed_count == 1 && failed_leaver) {
			/* only leaving node failed */

		} else if (failed_count && failed_leaver) {
			/* leaving node and another member failed */

		} else {
			/* other member failed, leaving node still alive */
			ASSERT(!failed_leaver, );
		}

		clear_bit(GFL_UPDATE, &g->flags);
		free(up);
		g->update = NULL;
		break;


	case UST_LSTART_SERVICEWAIT:
	case UST_LSTART_SERVICEDONE:

		clear_bit(UFL_ALLOW_STARTDONE, &up->flags);
		group_stop(g);

		failed_count = 0;
		failed_leaver = failed_memb(g, &failed_count);
		ASSERT(!failed_leaver, );

		node = find_node(up->nodeid);
		if (node && test_bit(NFL_NEED_RECOVERY, &node->flags))
			failed_leaver = node;

		if (!failed_count) {
			/* only leaving node failed */
			ASSERT(failed_leaver, );
			ASSERT(!test_bit(GFL_NEED_RECOVERY, &g->flags), );
			set_bit(GFL_NEED_RECOVERY, &g->flags);
			(*effected)++;

		} else if (failed_count && failed_leaver) {
			/* leaving node and another member failed */
			ASSERT(test_bit(GFL_NEED_RECOVERY, &g->flags), );

		} else {
			/* other member failed, leaving node still alive */
			ASSERT(failed_count, );
			ASSERT(!failed_leaver, );
			ASSERT(test_bit(GFL_NEED_RECOVERY, &g->flags), );
			node = new_node(g->update->nodeid);
			add_memb_node(g, node);
		}

		clear_bit(GFL_UPDATE, &g->flags);
		free(up);
		g->update = NULL;
		break;


	case UST_BARRIER_WAIT:

		if (test_bit(UFL_LEAVE, &up->flags))
			goto barrier_wait_leave;

		group_stop(g);
		cancel_update_barrier(g);

 	      barrier_wait_join:

		failed_count = 0;
		failed_joiner = failed_memb(g, &failed_count);
		ASSERT(failed_count, );
		ASSERT(test_bit(GFL_NEED_RECOVERY, &g->flags), );

		if (failed_count == 1 && failed_joiner) {
			/* only joining node failed */

		} else if (failed_count && failed_joiner) {
			/* joining node and another member failed */

		} else {
			/* other member failed, joining node still alive */
			ASSERT(!failed_joiner, );
			del_memb_node(g, up->nodeid);
		}

		clear_bit(GFL_UPDATE, &g->flags);
		free(up);
		g->update = NULL;
		break;

              barrier_wait_leave:

		failed_count = 0;
		failed_leaver = failed_memb(g, &failed_count);
		ASSERT(!failed_leaver, );

		node = find_node(up->nodeid);
		if (node && test_bit(NFL_NEED_RECOVERY, &node->flags))
			failed_leaver = node;

		if (!failed_count) {
			/* only leaving node failed */
			ASSERT(failed_leaver, );
			ASSERT(!test_bit(GFL_NEED_RECOVERY, &g->flags), );
			set_bit(GFL_NEED_RECOVERY, &g->flags);
			(*effected)++;

		} else if (failed_count && failed_leaver) {
			/* leaving node and another member failed */
			ASSERT(test_bit(GFL_NEED_RECOVERY, &g->flags), );

		} else {
			/* other member failed, leaving node still alive */
			ASSERT(failed_count, );
			ASSERT(!failed_leaver, );
			ASSERT(test_bit(GFL_NEED_RECOVERY, &g->flags), );
			node = new_node(g->update->nodeid);
			add_memb_node(g, node);
		}

		clear_bit(GFL_UPDATE, &g->flags);
		free(up);
		g->update = NULL;
		break;


	case UST_BARRIER_DONE:

		if (!up->barrier_status) {
			do_finish(g);
			update_done(g);
			break;
		} 

		if (test_bit(UFL_LEAVE, &up->flags))
			goto barrier_wait_leave;
		else
			goto barrier_wait_join;


	default:
		log_error(g, "cancel_one_update: state %d", up->state);
	}
}

void cancel_updates(int *effected)
{
	group_t *g;
	node_t *node, *gnode;
	int i;

	list_for_each_entry(node, &gd_nodes, list) {
		if (!test_bit(NFL_NEED_RECOVERY, &node->flags))
			continue;

		/*
		 * Clear this dead node from the "interested in joining" list
		 * of any SG.  The node is added to this list before the update
		 * begins.
		 */

		list_for_each_entry(g, &gd_groups, list) {
			gnode = find_joiner(g, node->id);
			if (gnode) {
				log_group(g, "clear joining node %u",
					  gnode->id);
				list_del(&gnode->list);
				free(gnode);
			}
		}
	}

	 /* Adjust any updates in sg's effected by the failed node(s) */

	for (i = 0; i < MAX_LEVELS; i++) {
		list_for_each_entry(g, &gd_levels[i], level_list) {
			if (!test_bit(GFL_UPDATE, &g->flags))
				continue;

			/* We may have some cancelling to do if this sg is
			   flagged as having a failed member, or if a joining
			   or leaving node has died. */
			   
			if (test_bit(GFL_NEED_RECOVERY, &g->flags))
				cancel_one_update(g, effected);
			else if (g->update->nodeid) {
				node = find_node(g->update->nodeid);
				ASSERT(node, );
				if (test_bit(NFL_NEED_RECOVERY, &node->flags))
					cancel_one_update(g, effected);
			}
		}
	}
}

int process_updates(void)
{
	group_t *g;
	int rv = 0;

	/*
	if (recoveries_exist())
		goto out;
	*/

	list_for_each_entry(g, &gd_groups, list) {
		if (test_bit(GFL_UPDATE, &g->flags))
			rv += process_one_update(g);
	}
 out:
	return rv;
}

