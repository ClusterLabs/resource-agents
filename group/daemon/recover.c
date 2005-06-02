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
 * A collection of sg's which need to be recovered due to a failed member.
 * These sg's are recovered in order of level.  An sg subject to cascading
 * failures is moved from one of these structs to a newer one.
 */

struct recover {
	struct list_head	list;
	struct list_head	groups[MAX_LEVELS];
	int			event_id;
	int			cur_level;
};
typedef struct recover recover_t;

static struct list_head	recoveries;


void init_recovery(void)
{
	INIT_LIST_HEAD(&recoveries);
}

/*
 * Given some number of dead nodes, flag SG's the dead nodes were part of.
 * This requires a number of loops because each node structure does not keep a
 * list of SG's it's in.
 */

static int mark_effected_groups(void)
{
	group_t *g;
	node_t *node, *gd_node;
	int effected = 0;

	list_for_each_entry(gd_node, &gd_nodes, list) {
		if (!test_bit(NFL_NEED_RECOVERY, &gd_node->flags))
			continue;

		/* check if dead node is among each group's members */

		list_for_each_entry(g, &gd_groups, list) {
			list_for_each_entry(node, &g->memb, list) {
				if (node->id == gd_node->id) {
					set_bit(GFL_NEED_RECOVERY, &g->flags);
					effected++;
					break;
				}
			}
		}
	}

	return effected;
}

static recover_t *alloc_recover(void)
{
	recover_t *rev;
	int i;

	rev = malloc(sizeof(*rev));
	memset(rev, 0, sizeof(*rev));

	set_event_id(&rev->event_id);

	for (i = 0; i < MAX_LEVELS; i++)
		INIT_LIST_HEAD(&rev->groups[i]);

	return rev;
}

/*
 * An in-progress revent re-start for an SG is interrupted by another node
 * failure in the SG.  Cancel an outstanding barrier if there is one.  The SG
 * will be moved to the new revent and re-started as part of that.
 */

static void cancel_prev_recovery(group_t *g)
{
	if (g->recover_state == RECOVER_BARRIERWAIT)
		cancel_recover_barrier(g);
}

static void pre_recover_group(group_t *g, recover_t *rev)
{
	if (g->state == GST_RECOVER) {
		cancel_prev_recovery(g);
		list_del(&g->recover_list);
	}

	group_stop(g);
	g->state = GST_RECOVER;
	g->recover_state = RECOVER_NONE;
	g->recover_data = rev;
	list_add(&g->recover_list, &(rev->groups[g->level])); 
}

/*
 * When adjust_members finds that some nodes are dead and mark_effected_sgs
 * finds that some SG's are effected by departed nodes, this is called to
 * collect together the SG's which need to be recovered.  An revent (recovery
 * event) is the group of effected SG's.
 */

static int new_recovery(void)
{
	group_t *g;
	recover_t *rev;
	node_t *node, *gd_node, *safe;
	int i = 0;

	rev = alloc_recover();
	list_add_tail(&rev->list, &recoveries);

	/*
	 * Stop effected SG's and add them to the rev
	 */

	list_for_each_entry(g, &gd_groups, list) {
		if (test_bit(GFL_NEED_RECOVERY, &g->flags)) {
			clear_bit(GFL_NEED_RECOVERY, &g->flags);
			if (g->state == GST_JOIN)
				continue;
			pre_recover_group(g, rev);
			i++;
		}
	}

	log_print("%d groups need recovery", i);

	/*
	 * For an SG needing recovery, remove dead nodes from sg->memb list
	 */

	for (i = 0; i < MAX_LEVELS; i++) {
		list_for_each_entry(g, &rev->groups[i], recover_list) {

			/* Remove dead members from group's member list */
			list_for_each_entry_safe(node, safe, &g->memb, list) {

				gd_node = find_node(node->id);
				ASSERT(gd_node,
				       log_error(g, "id %u", node->id););

				if (test_bit(NFL_NEED_RECOVERY,
							&gd_node->flags)) {
					list_del(&node->list);
					g->memb_count--;
					log_group(g, "remove node %u count %d",
						  node->id, g->memb_count);
					free(node);
				}
			}
		}
	}

	rev->cur_level = 0;
	return 0;
}

/*
 * Start the service of the given SG.  The service must be given an array of
 * nodeids specifying the new sg membership.  The service is responsible to
 * free this chunk of memory when done with it.
 */

static void start_group(group_t *g, uint32_t event_id)
{
	node_t *node;
	int *memb;
	int count = 0;

	memb = malloc(g->memb_count * sizeof(int));

	list_for_each_entry(node, &g->memb, list)
		memb[count++] = node->id;

	group_start(g, memb, count, event_id, NODE_FAILED);
}

static int recovery_barrier(group_t *g)
{
	char bname[MAX_BARRIERLEN];
	int error, len;

	memset(bname, 0, MAX_BARRIERLEN);

	/* bypass the barrier if we're the only member */
	if (g->memb_count == 1)
		goto done;

	len = snprintf(bname, MAX_BARRIERLEN, "sm.%u.%u.RECOV.%u",
		       g->global_id, g->recover_stop, g->memb_count);

	/* We save this barrier name so we can cancel it if needed. */
	memset(g->recover_barrier, 0, MAX_BARRIERLEN);
	memcpy(g->recover_barrier, bname, len);

	error = do_barrier(g, bname, g->memb_count, GD_BARRIER_RECOVERY);
	if (error < 0) {
		log_error(g, "recovery_barrier error %d", error);
		return error;
	}

	return error;

 done:
	if (!g->recover_stop)
		g->recover_state = RECOVER_STOP;
	else
		g->recover_state = RECOVER_BARRIERDONE;
	return 0;
}

static int recover_group(group_t *g, int event_id)
{
	int rv = 1;

	log_group(g, "recover state %d", g->recover_state);

	switch (g->recover_state) {

	case RECOVER_NONE:
		/* must wait for recovery to stop sg on all nodes */
		g->recover_state = RECOVER_BARRIERWAIT;
		g->recover_stop = 0;
		recovery_barrier(g);
		break;

	/* RECOVER_BARRIERWAIT - default - nothing to do */

	case RECOVER_STOP:
		/* barrier callback sets state STOP */
		g->recover_stop = 1;
		g->recover_state = RECOVER_START;
		start_group(g, event_id);
		break;

	/* RECOVER_START - default - nothing to do */

	case RECOVER_STARTDONE:
		/* service callback sets state STARTDONE */
		g->recover_state = RECOVER_BARRIERWAIT;
		recovery_barrier(g);
		break;

	case RECOVER_BARRIERDONE:
		/* barrier callback sets state BARRIERDONE */
		group_finish(g, event_id);
		list_del(&g->recover_list);
		g->recover_state = RECOVER_NONE;
		g->state = GST_RUN;

		/* Continue a previous, interrupted attempt to leave the sg */
		if (g->event) {
			event_t *ev = g->event;
			log_group(g, "restart leave %lx", ev->flags);
			clear_bit(EFL_DELAY_RECOVERY, &ev->flags);
		}
		break;

	default:
		/*
		log_error(g, "no recovery processing for state %u",
			  g->recover_state);
		*/
		rv = 0;
	}

	return rv;
}

static int recover_level(recover_t *rev, int level)
{
	group_t *g, *safe;
	int rv = 0;

	list_for_each_entry_safe(g, safe, &rev->groups[level], recover_list)
		rv += recover_group(g, rev->event_id);

	return rv;
}

static int recover_levels(recover_t *rev)
{
	int rv = 0;

	for (;;) {
		rv += recover_level(rev, rev->cur_level);

		if (list_empty(&rev->groups[rev->cur_level])) {
			if (rev->cur_level == MAX_LEVELS - 1) {
				list_del(&rev->list);
				free(rev);
				return;
			}
			rev->cur_level++;
			continue;
		}
		break;
	}

	return rv;
}

/*
 * Called by SM thread when the cluster is quorate.  It restarts
 * SG's that were stopped in new_recovery() due to a member death.
 * It waits for all SG's at level N to complete restart before
 * restarting SG's at level N+1.
 */

int process_recoveries(void)
{
	group_t *g;
	recover_t *rev, *safe;
	int rv = 0;

	list_for_each_entry_safe(rev, safe, &recoveries, list)
		rv += recover_levels(rev);

	return rv;
}

/*
 * The cnxman membership has changed.  Check if there's still quorum and
 * whether any nodes have died.  If nodes have died, initiate recovery on any
 * SG's they were in.  This begins immediately if the cluster remains quorate;
 * if not this waits until the cluster regains quorum.
 */

void process_nodechange(void)
{
	int effected;

	effected = mark_effected_groups();

	/* FIXME: before we actually go messing with things, make sure all
	   groupd's are working off the same cluster generation */

	backout_events();
	cancel_updates(&effected);

	if (effected > 0)
		new_recovery();
}

int check_recovery(group_t *g, int event_id)
{
	if (g->state == GST_RECOVER) {
		recover_t *rev = (recover_t *) g->recover_data;
		if (rev && rev->event_id == event_id)
			return 1;
	}
	return 0;
}

void process_recover_msg(msg_t *msg, int nodeid)
{
        group_t *g;
	recover_t *rev;

	g = find_group_id(msg->ms_group_id);
	if (!g) {
		log_print("process_recover_msg: unknown group id %x",
			  msg->ms_group_id);
		return;
	}

	/* we already know about the recovery and can ignore the msg */
	if (g->state == GST_RECOVER)
		return;

	if (in_update(g)) {
		/* we will initiate recovery on our own if we know about the
		   uevent so we can ignore this */
		log_group(g, "process_recover_msg: ignore from %u", nodeid);
		return;
	}

	log_group(g, "recovery initiated by msg from %u", nodeid);
	rev = alloc_recover();
	list_add_tail(&rev->list, &recoveries);
	pre_recover_group(g, rev);
}
