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
#include "config.h"

/*
 * A collection of sg's which need to be recovered due to a failed member.
 * These sg's are recovered in order of level.  An sg subject to cascading
 * failures is moved from one of these structs to a newer one.
 */

struct recover {
	struct list_head	list;		/* list of current re's */
	struct list_head	sgs[SG_LEVELS];	/* lists of sg's by level */
	int			event_id;	/* event id */
	int			cur_level;
};
typedef struct recover recover_t;


extern uint32_t *	sm_new_nodeids;
extern int		sm_quorum, sm_quorum_next;
extern uint32_t		sm_our_nodeid;
extern struct list_head	sm_members;
extern int		sm_member_count;
static struct list_head	recoveries;


void init_recovery(void)
{
	INIT_LIST_HEAD(&recoveries);
}

/* 
 * This is the first thing called when a change is announced in cluster
 * membership.  Nodes are marked as being a CLUSTER_MEMBER or not.  SM adds new
 * nodes to its sm_members list which it's not seen before.  Nodes which were
 * alive but are now gone are marked as "need recovery".
 *
 * The "need recovery" status of nodes is propagated to the node's SG's in
 * mark_effected_sgs.  The effected SG's are themselves marked as needing
 * recovery and in new_recovery the dead nodes are removed from the SG's
 * individual member lists.  The "need recovery" status of nodes is cleared in
 * adjust_members_done().
 */

static int adjust_members(void)
{
	sm_node_t *node;
	struct kcl_cluster_node knode;
	int i, error, num_nodes, sub = 0, add = 0, found;

	/* 
	 * Get list of current members from cnxman
	 */

	memset(sm_new_nodeids, 0, cman_config.max_nodes * sizeof(uint32_t));
	num_nodes = kcl_get_member_ids(sm_new_nodeids, cman_config.max_nodes);

	/* 
	 * Determine who's gone
	 */

	list_for_each_entry(node, &sm_members, list) {
		found = FALSE;
		for (i = 0; i < num_nodes; i++) {
			if (node->id == sm_new_nodeids[i]) {
				found = TRUE;
				sm_new_nodeids[i] = 0;
				break;
			}
		}

		if (found) {
			error = kcl_get_node_by_nodeid(node->id, &knode);
			SM_ASSERT(!error, printk("error=%d\n", error););

			if (!test_bit(SNFL_CLUSTER_MEMBER, &node->flags)) {
				/* former member is back */
				set_bit(SNFL_CLUSTER_MEMBER, &node->flags);
				node->incarnation = knode.incarnation;
				add++;
			} else {
				/* current member is still alive - if the
				 * incarnation number is different it died and
				 * returned between checks */
				if (node->incarnation != knode.incarnation) {
					set_bit(SNFL_NEED_RECOVERY,
						&node->flags);
					node->incarnation = knode.incarnation;
					sub++;
				}
			}
		} else {
			/* current member has died */
			if (test_and_clear_bit(SNFL_CLUSTER_MEMBER,
					       &node->flags)) {
				set_bit(SNFL_NEED_RECOVERY, &node->flags);
				sub++;
			}
		}
	}

	/*
	 * Look for new nodes
	 */

	for (i = 0; i < num_nodes; i++) {
		if (sm_new_nodeids[i]) {
			node = sm_new_node(sm_new_nodeids[i]);
			set_bit(SNFL_CLUSTER_MEMBER, &node->flags);
			add++;
			list_add_tail(&node->list, &sm_members);
			sm_member_count++;
		}
	}

	/*
	 * Get our own nodeid
	 */

	if (!sm_our_nodeid) {
		list_for_each_entry(node, &sm_members, list) {
			error = kcl_get_node_by_nodeid(node->id, &knode);
			SM_ASSERT(!error, printk("error=%d\n", error););

			if (knode.us) {
				sm_our_nodeid = knode.node_id;
				break;
			}
		}
	}

	return sub;
}

/*
 * Given some number of dead nodes, flag SG's the dead nodes were part of.
 * This requires a number of loops because each node structure does not keep a
 * list of SG's it's in.
 */

static int mark_effected_sgs(void)
{
	sm_group_t *sg;
	sm_node_t *node, *sgnode;
	uint32_t dead_id;
	int i, effected = 0;

	down(&sm_sglock);

	list_for_each_entry(node, &sm_members, list) {
		if (!test_bit(SNFL_NEED_RECOVERY, &node->flags))
			continue;

		dead_id = node->id;

		for (i = 0; i < SG_LEVELS; i++) {
			list_for_each_entry(sg, &sm_sg[i], list) {
				/* check if dead node is among sg's members */
				list_for_each_entry(sgnode, &sg->memb, list) {
					if (sgnode->id == dead_id) {
						set_bit(SGFL_NEED_RECOVERY,
							&sg->flags);
						effected++;
						break;
					}
				}
				schedule();
			}
		}
		schedule();
	}
	up(&sm_sglock);

	return effected;
}

static recover_t *alloc_recover(void)
{
	recover_t *rev;
	int i;

	SM_RETRY(rev = kmalloc(sizeof(recover_t), GFP_KERNEL), rev);

	memset(rev, 0, sizeof(recover_t));

	sm_set_event_id(&rev->event_id);

	for (i = 0; i < SG_LEVELS; i++) {
		INIT_LIST_HEAD(&rev->sgs[i]);
	}

	return rev;
}

/*
 * An in-progress revent re-start for an SG is interrupted by another node
 * failure in the SG.  Cancel an outstanding barrier if there is one.  The SG
 * will be moved to the new revent and re-started as part of that.
 */

static void cancel_prev_recovery(sm_group_t *sg)
{
	int error;

	if (sg->recover_state == RECOVER_BARRIERWAIT) {
		error = kcl_barrier_cancel(sg->recover_barrier);
		if (error)
			log_error(sg, "cancel_prev_recovery: error %d", error);
	}
}

static void pre_recover_sg(sm_group_t *sg, recover_t *rev)
{
	if (sg->state == SGST_RECOVER) {
		cancel_prev_recovery(sg);
		list_del(&sg->recover_list);
	}

	sg->ops->stop(sg->service_data);
	sg->state = SGST_RECOVER;
	sg->recover_state = RECOVER_NONE;
	sg->recover_data = rev;
	list_add(&sg->recover_list, &rev->sgs[sg->level]); 
}

/*
 * When adjust_members finds that some nodes are dead and mark_effected_sgs
 * finds that some SG's are effected by departed nodes, this is called to
 * collect together the SG's which need to be recovered.  An revent (recovery
 * event) is the group of effected SG's.
 */

static int new_recovery(void)
{
	sm_group_t *sg;
	recover_t *rev;
	sm_node_t *node, *sgnode, *safe;
	int i;

	rev = alloc_recover();
	list_add_tail(&rev->list, &recoveries);

	down(&sm_sglock);

	/*
	 * Stop effected SG's and add them to the rev
	 */

	for (i = 0; i < SG_LEVELS; i++) {
		list_for_each_entry(sg, &sm_sg[i], list) {
			if (test_and_clear_bit(SGFL_NEED_RECOVERY, &sg->flags)){
				if (sg->state == SGST_JOIN)
					continue;
				pre_recover_sg(sg, rev);
			}
		}
		schedule();
	}

	/*
	 * For an SG needing recovery, remove dead nodes from sg->memb list
	 */

	for (i = 0; i < SG_LEVELS; i++) {
		list_for_each_entry(sg, &rev->sgs[i], recover_list) {

			/* Remove dead members from SG's member list */
			list_for_each_entry_safe(sgnode, safe, &sg->memb, list){

				node = sm_find_member(sgnode->id);
				SM_ASSERT(node, printk("id %u\n", sgnode->id););

				if (test_bit(SNFL_NEED_RECOVERY, &node->flags)){
					list_del(&sgnode->list);
					sg->memb_count--;
					log_debug(sg, "remove node %u count %d",
						  sgnode->id, sg->memb_count);
					kfree(sgnode);
				}
				schedule();
			}
			schedule();
		}
	}

	up(&sm_sglock);
	rev->cur_level = 0;
	return 0;
}

/*
 * The NEED_RECOVERY bit on MML nodes is set in adjust_members() and is used in
 * mark_effected_sgs() and add_revent().  After that, we're done using the bit
 * and we clear it here.
 */

static void adjust_members_done(void)
{
	sm_node_t *node;

	list_for_each_entry(node, &sm_members, list)
		clear_bit(SNFL_NEED_RECOVERY, &node->flags);
}

/*
 * Start the service of the given SG.  The service must be given an array of
 * nodeids specifying the new sg membership.  The service is responsible to
 * free this chunk of memory when done with it.
 */

static void start_sg(sm_group_t *sg, uint32_t event_id)
{
	sm_node_t *node;
	uint32_t *memb;
	int count = 0;

	SM_RETRY(memb = kmalloc(sg->memb_count * sizeof(uint32_t), GFP_KERNEL),
		 memb);

	list_for_each_entry(node, &sg->memb, list)
		memb[count++] = node->id;

	sg->ops->start(sg->service_data, memb, count, event_id,
		       SERVICE_NODE_FAILED);
}

static void recovery_barrier(sm_group_t *sg)
{
	char bname[MAX_BARRIER_NAME_LEN];
	int error, len;

	memset(bname, 0, MAX_BARRIER_NAME_LEN);

	/* bypass the barrier if we're the only member */
	if (sg->memb_count == 1) {
		process_recovery_barrier(sg, 0);
		return;
	}

	len = snprintf(bname, MAX_BARRIER_NAME_LEN, "sm.%u.%u.RECOV.%u",
		       sg->global_id, sg->recover_stop, sg->memb_count);

	/* We save this barrier name so we can cancel it if needed. */
	memset(sg->recover_barrier, 0, MAX_BARRIER_NAME_LEN);
	memcpy(sg->recover_barrier, bname, len);

	error = sm_barrier(bname, sg->memb_count, SM_BARRIER_RECOVERY);
	if (error)
		log_error(sg, "recovery_barrier error %d: %s", error, bname);
}

static void recover_sg(sm_group_t *sg, int event_id)
{
	log_debug(sg, "recover state %d", sg->recover_state);

	switch (sg->recover_state) {

	case RECOVER_NONE:
		/* must wait for recovery to stop sg on all nodes */
		sg->recover_state = RECOVER_BARRIERWAIT;
		sg->recover_stop = 0;
		recovery_barrier(sg);
		break;

	case RECOVER_BARRIERWAIT:
		break;

	case RECOVER_STOP:
		/* barrier callback sets state STOP */
		sg->recover_stop = 1;
		sg->recover_state = RECOVER_START;
		start_sg(sg, event_id);
		break;

	case RECOVER_START:
		break;

	case RECOVER_STARTDONE:
		/* service callback sets state STARTDONE */
		sg->recover_state = RECOVER_BARRIERWAIT;
		recovery_barrier(sg);
		break;

	case RECOVER_BARRIERDONE:
		/* barrier callback sets state BARRIERDONE */
		sg->ops->finish(sg->service_data, event_id);
		list_del(&sg->recover_list);
		sg->recover_state = RECOVER_NONE;
		sg->state = SGST_RUN;

		/* Continue a previous, interrupted attempt to leave the sg */
		if (sg->sevent) {
			sm_sevent_t *sev = sg->sevent;
			log_debug(sg, "restart leave %lx", sev->se_flags);
			clear_bit(SEFL_DELAY_RECOVERY, &sev->se_flags);
			set_bit(SEFL_CHECK, &sev->se_flags);
			wake_serviced(DO_JOINLEAVE);
		}
		break;

	default:
		log_error(sg, "invalid recover_state %u", sg->recover_state);
	}
}

static void recover_level(recover_t *rev, int level)
{
	sm_group_t *sg, *safe;

	list_for_each_entry_safe(sg, safe, &rev->sgs[level], recover_list) {
		recover_sg(sg, rev->event_id);
		schedule();
	}
}

static void recover_levels(recover_t *rev)
{
	for (;;) {
		recover_level(rev, rev->cur_level);

		if (list_empty(&rev->sgs[rev->cur_level])) {
			if (rev->cur_level == SG_LEVELS - 1) {
				list_del(&rev->list);
				kfree(rev);
				return;
			}
			rev->cur_level++;
			continue;
		}
		break;
	}
}

/*
 * Called by SM thread when the cluster is quorate.  It restarts
 * SG's that were stopped in new_recovery() due to a member death.
 * It waits for all SG's at level N to complete restart before
 * restarting SG's at level N+1.
 */

void process_recoveries(void)
{
	recover_t *rev, *safe;

	down(&sm_sglock);
	list_for_each_entry_safe(rev, safe, &recoveries, list)
		recover_levels(rev);
	up(&sm_sglock);
}

/*
 * The cnxman membership has changed.  Check if there's still quorum and
 * whether any nodes have died.  If nodes have died, initiate recovery on any
 * SG's they were in.  This begins immediately if the cluster remains quorate;
 * if not this waits until the cluster regains quorum.
 */

void process_nodechange(void)
{
	int gone, effected;

	if ((sm_quorum = sm_quorum_next))
		wake_serviced(DO_RUN);

	gone = adjust_members();
	if (gone > 0) {
		effected = mark_effected_sgs();

		backout_sevents();
		cancel_uevents(&effected);

		if (effected > 0) {
			new_recovery();
			wake_serviced(DO_RECOVERIES);
		}
	}
	adjust_members_done();
}

int check_recovery(sm_group_t *sg, int event_id)
{
	if (sg->state == SGST_RECOVER) {
		recover_t *rev = (recover_t *) sg->recover_data;
		if (rev && rev->event_id == event_id)
			return 1;
	}
	return 0;
}

void process_recover_msg(sm_msg_t *smsg, uint32_t nodeid)
{
        sm_group_t *sg;
	recover_t *rev;

	sg = sm_global_id_to_sg(smsg->ms_global_sgid);
	if (!sg) {
		log_print("process_recover_msg: unknown sg id %x",
			  smsg->ms_global_sgid);
		return;
	}

	/* we already know about the recovery and can ignore the msg */
	if (sg->state == SGST_RECOVER)
		return;

	if (test_bit(SGFL_UEVENT, &sg->flags)) {
		/* we will initiate recovery on our own if we know about the
		   uevent so we can ignore this */
		log_debug(sg, "process_recover_msg: ignore from %u", nodeid);
		return;
	}

	log_debug(sg, "recovery initiated by msg from %u", nodeid);
	rev = alloc_recover();
	list_add_tail(&rev->list, &recoveries);
	pre_recover_sg(sg, rev);
	wake_serviced(DO_RECOVERIES);
}
