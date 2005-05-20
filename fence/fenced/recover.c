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

#include "fd.h"
#include "ccs.h"

extern int our_nodeid;
extern commandline_t comline;

/* Fencing recovery algorithm

   do_recovery (service event start)
   - complete = list of nodes in previous completed fence domain
   - cl_nodes = list of current domain members provided by start
   - victims = list of nodes in complete that are not in cl_nodes
   - prev = saved version of cl_nodes (in list format)
   - fence_victims() fences nodes in victims list

   do_recovery_done (service event finish)
   - complete = prev

   Notes:
   - When fenced is started, the complete list is initialized to all
   the nodes in cluster.conf.
   - fence_victims actually only runs on one of the nodes in the domain
   so that a victim isn't fenced by everyone.
   - The node to run fence_victims is the node with lowest id that's in both
   complete and prev lists.
   - This node will never be a node that's just joining since by definition
   the joining node wasn't in the last complete group.
   - An exception to this is when there is just one node in the group
   in which case it's chosen even if it wasn't in the last complete group.
   - There's also a leaving list that parallels the victims list but are
   not fenced.
*/


static void free_node_list(struct list_head *head)
{
	fd_node_t *node;
	while (!list_empty(head)) {
		node = list_entry(head->next, fd_node_t, list);
		list_del(&node->list);
		free(node);
	}
}

static inline void free_victims(fd_t *fd)
{
	free_node_list(&fd->victims);
}

static inline void free_leaving(fd_t *fd)
{
	free_node_list(&fd->leaving);
}

static inline void free_prev(fd_t *fd)
{
	free_node_list(&fd->prev);
}

static inline void free_complete(fd_t *fd)
{
	free_node_list(&fd->complete);
}

static int next_complete_nodeid(fd_t *fd, int gt)
{
	fd_node_t *node;
	int low = 0xFFFFFFFF;

	/* find lowest node id in fd_complete greater than gt */

	list_for_each_entry(node, &fd->complete, list) {
		if ((node->nodeid > gt) && (node->nodeid < low))
			low = node->nodeid;
	}
	return low;
}

static int find_master_nodeid(fd_t *fd, char **master_name)
{
	fd_node_t *node;
	int low = 0;

	/* Find the lowest nodeid common to fd->fd_prev (newest member list)
	 * and fd->fd_complete (last complete member list). */

	for (;;) {
		low = next_complete_nodeid(fd, low);

		if (low == 0xFFFFFFFF)
			break;

		list_for_each_entry(node, &fd->prev, list) {
			if (low != node->nodeid)
				continue;
			*master_name = node->name;
			goto out;
		}
	}

	/* Special case: we're the first and only FD member */

	if (fd->prev_count == 1)
		low = our_nodeid;

	/* We end up returning -1 when we're not the only node and we've just
	   joined.  Because we've just joined we weren't in the last complete
	   domain group and won't be chosen as master.  We defer to someone who
	   _was_ in the last complete group.  All we know is it isn't us. */

	*master_name = "prior member";
 out:
	return low;
}

void add_complete_node(fd_t *fd, int nodeid, char *name)
{
	fd_node_t *node;
	node = get_new_node(fd, nodeid, name);
	list_add(&node->list, &fd->complete);
}

static void new_prev_nodes(fd_t *fd, int member_count, int *nodeids)
{
	fd_node_t *node;
	int i;

	for (i = 0; i < member_count; i++) {
		node = get_new_node(fd, nodeids[i], NULL);
		list_add(&node->list, &fd->prev);
	}

	fd->prev_count = member_count;
}

static void add_first_victims(fd_t *fd)
{
	fd_node_t *prev_node, *safe;
	int error;

	error = update_cluster_members();

	/* complete list initialised in init_nodes() to all nodes from ccs */
	if (list_empty(&fd->complete))
		log_debug("first complete list empty warning");

	list_for_each_entry_safe(prev_node, safe, &fd->complete, list) {
		if (!in_cluster_members(prev_node->name, 0)) {
			list_del(&prev_node->list);
			list_add(&prev_node->list, &fd->victims);
			log_debug("add first victim %s", prev_node->name);
		}
	}
}

static int list_count(struct list_head *head)
{
	struct list_head *tmp;
	int count = 0;

	list_for_each(tmp, head)
		count++;
	return count;
}

static int id_in_nodeids(int nodeid, int count, int *nodeids)
{
	int i;

	for (i = 0; i < count; i++) {
		if (nodeid == nodeids[i])
			return TRUE;
	}
	return FALSE;
}

/* This routine should probe other indicators to check if victims
   can be reduced.  Right now we just check if the victim has rejoined the
   cluster. */

static int reduce_victims(fd_t *fd)
{
	fd_node_t *node, *safe;
	int num_victims;

	num_victims = list_count(&fd->victims);

	update_cluster_members();

	list_for_each_entry_safe(node, safe, &fd->victims, list) {
		if (in_cluster_members(NULL, node->nodeid)) {
			list_del(&node->list);
			log_debug("reduce victim %s", node->name);
			free(node);
			num_victims--;
		}
	}

	return num_victims;
}

/* If there are victims after a node has joined, it's a good indication that
   they may be joining the cluster shortly.  If we delay a bit they might
   become members and we can avoid fencing them.  This is only really an issue
   when the fencing method reboots the victims.  Otherwise, the nodes should
   unfence themselves when they start up. */

static void delay_fencing(fd_t *fd, int start_type)
{
	struct timeval first, last, start, now;
	int victim_count, last_count = 0, delay = 0;
	fd_node_t *node;
	char *delay_type;

	if (start_type == GROUP_NODE_JOIN) {
		delay = comline.post_join_delay;
		delay_type = "post_join_delay";
	} else {
		delay = comline.post_fail_delay;
		delay_type = "post_fail_delay";
	}

	if (delay == 0)
		goto out;

	gettimeofday(&first, NULL);
	gettimeofday(&start, NULL);

	for (;;) {
		sleep(1);

		victim_count = reduce_victims(fd);

		if (victim_count == 0)
			break;

		if (victim_count < last_count) {
			gettimeofday(&start, NULL);
			if (delay > 0 && comline.post_join_delay > delay) {
				delay = comline.post_join_delay;
				delay_type = "post_join_delay (modified)";
			}
		}

		last_count = victim_count;

		/* negative delay means wait forever */
		if (delay == -1)
			continue;

		gettimeofday(&now, NULL);
		if (now.tv_sec - start.tv_sec >= delay)
			break;
	}

	gettimeofday(&last, NULL);

	log_debug("delay of %ds leaves %d victims",
		  (int) (last.tv_sec - first.tv_sec), victim_count);
 out:
	list_for_each_entry(node, &fd->victims, list) {
		syslog(LOG_INFO, "%s not a cluster member after %d sec %s",
		       node->name, delay, delay_type);
	}
}

static void fence_victims(fd_t *fd, int start_type)
{
	fd_node_t *node;
	char *master_name;
	uint32_t master;
	int error, cd;

	master = find_master_nodeid(fd, &master_name);

	if (master != fd->our_nodeid) {
		log_debug("defer fencing to %u %s", master, master_name);
		syslog(LOG_INFO, "fencing deferred to %s", master_name);
		return;
	}

	delay_fencing(fd, start_type);

	while ((cd = ccs_connect()) < 0)
		sleep(1);

	while (!list_empty(&fd->victims)) {
		node = list_entry(fd->victims.next, fd_node_t, list);

		if (can_avert_fence(fd, node)) {
			log_debug("averting fence of node %s", node->name);
			list_del(&node->list);
			free(node);
			continue;
		}

		log_debug("fencing node %s", node->name);
		syslog(LOG_INFO, "fencing node \"%s\"", node->name);

		error = dispatch_fence_agent(cd, node->name);

		syslog(LOG_INFO, "fence \"%s\" %s", node->name,
		       error ? "failed" : "success");

		if (!error) {
			list_del(&node->list);
			free(node);
		}
		sleep(5);
	}

	ccs_disconnect(cd);
}

static void add_victims(fd_t *fd, int start_type, int member_count,
			int *nodeids)
{
	fd_node_t *node, *safe;

	/* nodes which haven't completed leaving when a failure restart happens
	 * are dead (and need fencing) or are still members */

	if (start_type == GROUP_NODE_FAILED) {
		list_for_each_entry_safe(node, safe, &fd->leaving, list) {
			list_del(&node->list);
			if (id_in_nodeids(node->nodeid, member_count, nodeids))
				list_add(&node->list, &fd->complete);
			else {
				list_add(&node->list, &fd->victims);
				log_debug("add victim %u, was leaving",
					  node->nodeid);
			}
		}
	}

	/* nodes in last completed group but missing from fr_nodeids are added
	 * to victims list or leaving list, depending on the type of start. */

	if (list_empty(&fd->complete))
		log_debug("complete list empty warning");

	list_for_each_entry_safe(node, safe, &fd->complete, list) {
		if (!id_in_nodeids(node->nodeid, member_count, nodeids)) {
			list_del(&node->list);

			if (start_type == GROUP_NODE_FAILED)
				list_add(&node->list, &fd->victims);
			else
				list_add(&node->list, &fd->leaving);

			log_debug("add node %u to list %u", node->nodeid,
				  start_type);
		}
	}
}

void do_recovery(fd_t *fd, int start_type, int member_count, int *nodeids)
{
	/* Reset things when the last stop aborted our first
	 * start, i.e. there was no finish; we got a
	 * start/stop/start immediately upon joining. */

	if (!fd->last_finish && fd->last_stop) {
		log_debug("revert aborted first start");
		fd->last_stop = 0;
		fd->first_recovery = FALSE;
		free_prev(fd);
		free_victims(fd);
		free_leaving(fd);
	}

	log_debug("do_recovery stop %d start %d finish %d",
		  fd->last_stop, fd->last_start, fd->last_finish);

	if (!fd->first_recovery) {
		fd->first_recovery = TRUE;
		add_first_victims(fd);
	} else
		add_victims(fd, start_type, member_count, nodeids);

	free_prev(fd);
	new_prev_nodes(fd, member_count, nodeids);

	if (!list_empty(&fd->victims))
		fence_victims(fd, start_type);
}

void do_recovery_done(fd_t *fd)
{
	fd_node_t *node, *safe;

	if (fd->last_finish == fd->last_start) {
		free_leaving(fd);
		free_victims(fd);
	}

	/* Save a copy of this set of nodes which constitutes the latest
	 * complete group.  Any of these nodes missing in the next start will
	 * either be leaving or victims.  For the next recovery, the lowest
	 * remaining nodeid in this group will be the master. */

	free_complete(fd);
	list_for_each_entry_safe(node, safe, &fd->prev, list) {
		list_del(&node->list);
		list_add(&node->list, &fd->complete);
	}
}

