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

#include "dlm_internal.h"
#include "member_sysfs.h"
#include "lockspace.h"
#include "member.h"
#include "recoverd.h"
#include "recover.h"
#include "lowcomms.h"
#include "rcom.h"

/*
 * Following called by dlm_recoverd thread
 */

static void add_ordered_member(struct dlm_ls *ls, struct dlm_member *new)
{
	struct dlm_member *memb = NULL;
	struct list_head *tmp;
	struct list_head *newlist = &new->list;
	struct list_head *head = &ls->ls_nodes;

	list_for_each(tmp, head) {
		memb = list_entry(tmp, struct dlm_member, list);
		if (new->nodeid < memb->nodeid)
			break;
	}

	if (!memb)
		list_add_tail(newlist, head);
	else {
		/* FIXME: can use list macro here */
		newlist->prev = tmp->prev;
		newlist->next = tmp;
		tmp->prev->next = newlist;
		tmp->prev = newlist;
	}
}

static int dlm_add_member(struct dlm_ls *ls, int nodeid)
{
	struct dlm_member *memb;

	memb = kmalloc(sizeof(struct dlm_member), GFP_KERNEL);
	if (!memb)
		return -ENOMEM;

	memb->nodeid = nodeid;
	add_ordered_member(ls, memb);
	ls->ls_num_nodes++;
	return 0;
}

static void dlm_remove_member(struct dlm_ls *ls, struct dlm_member *memb)
{
	list_move(&memb->list, &ls->ls_nodes_gone);
	ls->ls_num_nodes--;
}

static int dlm_is_member(struct dlm_ls *ls, int nodeid)
{
	struct dlm_member *memb;

	list_for_each_entry(memb, &ls->ls_nodes, list) {
		if (memb->nodeid == nodeid)
			return TRUE;
	}
	return FALSE;
}

int dlm_is_removed(struct dlm_ls *ls, int nodeid)
{
	struct dlm_member *memb;

	list_for_each_entry(memb, &ls->ls_nodes_gone, list) {
		if (memb->nodeid == nodeid)
			return TRUE;
	}
	return FALSE;
}

static void clear_memb_list(struct list_head *head)
{
	struct dlm_member *memb;

	while (!list_empty(head)) {
		memb = list_entry(head->next, struct dlm_member, list);
		list_del(&memb->list);
		kfree(memb);
	}
}

void dlm_clear_members(struct dlm_ls *ls)
{
	clear_memb_list(&ls->ls_nodes);
	ls->ls_num_nodes = 0;
}

void dlm_clear_members_gone(struct dlm_ls *ls)
{
	clear_memb_list(&ls->ls_nodes_gone);
}

void dlm_clear_members_finish(struct dlm_ls *ls, int finish_event)
{
	struct dlm_member *memb, *safe;

	list_for_each_entry_safe(memb, safe, &ls->ls_nodes_gone, list) {
		if (memb->gone_event <= finish_event) {
			list_del(&memb->list);
			kfree(memb);
		}
	}
}

static void make_member_array(struct dlm_ls *ls)
{
	struct dlm_member *memb;
	int i = 0, *array;

	if (ls->ls_node_array) {
		kfree(ls->ls_node_array);
		ls->ls_node_array = NULL;
	}

	array = kmalloc(sizeof(int) * ls->ls_num_nodes, GFP_KERNEL);
	if (!array)
		return;

	list_for_each_entry(memb, &ls->ls_nodes, list)
		array[i++] = memb->nodeid;

	ls->ls_node_array = array;
}

/* send a status request to all members just to establish comms connections */

static void ping_members(struct dlm_ls *ls)
{
	struct dlm_member *memb;
	list_for_each_entry(memb, &ls->ls_nodes, list)
		dlm_rcom_status(ls, memb->nodeid);
}

int dlm_recover_members(struct dlm_ls *ls, struct dlm_recover *rv, int *neg_out)
{
	struct dlm_member *memb, *safe;
	int i, error, found, pos = 0, neg = 0, low = -1;

	/* move departed members from ls_nodes to ls_nodes_gone */

	list_for_each_entry_safe(memb, safe, &ls->ls_nodes, list) {
		found = FALSE;
		for (i = 0; i < rv->node_count; i++) {
			if (memb->nodeid == rv->nodeids[i]) {
				found = TRUE;
				break;
			}
		}

		if (!found) {
			neg++;
			memb->gone_event = rv->event_id;
			dlm_remove_member(ls, memb);
			log_debug(ls, "remove member %d", memb->nodeid);
		}
	}

	/* add new members to ls_nodes */

	for (i = 0; i < rv->node_count; i++) {
		if (dlm_is_member(ls, rv->nodeids[i]))
			continue;
		dlm_add_member(ls, rv->nodeids[i]);
		pos++;
		log_debug(ls, "add member %d", rv->nodeids[i]);
	}

	list_for_each_entry(memb, &ls->ls_nodes, list) {
		if (low == -1 || memb->nodeid < low)
			low = memb->nodeid;
	}
	ls->ls_low_nodeid = low;

	make_member_array(ls);
	set_bit(LSFL_NODES_VALID, &ls->ls_flags);
	*neg_out = neg;

	ping_members(ls);

	error = dlm_recover_members_wait(ls);
	log_debug(ls, "total members %d", ls->ls_num_nodes);
	return error;
}

int dlm_recover_members_first(struct dlm_ls *ls, struct dlm_recover *rv)
{
	int i, error, nodeid, low = -1;

	dlm_clear_members(ls);

	log_debug(ls, "add members");

	for (i = 0; i < rv->node_count; i++) {
		nodeid = rv->nodeids[i];
		dlm_add_member(ls, nodeid);

		if (low == -1 || nodeid < low)
			low = nodeid;
	}
	ls->ls_low_nodeid = low;

	make_member_array(ls);
	set_bit(LSFL_NODES_VALID, &ls->ls_flags);

	ping_members(ls);

	error = dlm_recover_members_wait(ls);
	log_debug(ls, "total members %d", ls->ls_num_nodes);
	return error;
}

/*
 * Following called from member_sysfs.c
 */

int dlm_ls_terminate(struct dlm_ls *ls)
{
	spin_lock(&ls->ls_recover_lock);
	set_bit(LSFL_LS_TERMINATE, &ls->ls_flags);
	set_bit(LSFL_JOIN_DONE, &ls->ls_flags);
	set_bit(LSFL_LEAVE_DONE, &ls->ls_flags);
	spin_unlock(&ls->ls_recover_lock);
	wake_up(&ls->ls_wait_member);
	log_error(ls, "dlm_ls_terminate");
	return 0;
}

int dlm_ls_stop(struct dlm_ls *ls)
{
	int new;

	spin_lock(&ls->ls_recover_lock);
	ls->ls_last_stop = ls->ls_last_start;
	set_bit(LSFL_LS_STOP, &ls->ls_flags);
	new = test_and_clear_bit(LSFL_LS_RUN, &ls->ls_flags);
	spin_unlock(&ls->ls_recover_lock);

	/*
	 * This in_recovery lock does two things:
	 *
	 * 1) Keeps this function from returning until all threads are out
	 *    of locking routines and locking is truely stopped.
	 * 2) Keeps any new requests from being processed until it's unlocked
	 *    when recovery is complete.
	 */

	if (new)
		down_write(&ls->ls_in_recovery);

	/*
	 * The recoverd suspend/resume makes sure that dlm_recoverd (if
	 * running) has noticed the clearing of LS_RUN above and quit
	 * processing the previous recovery.  This will be true for all nodes
	 * before any nodes get the start.
	 */

	dlm_recoverd_suspend(ls);
	clear_bit(LSFL_LOCKS_VALID, &ls->ls_flags);
	clear_bit(LSFL_ALL_LOCKS_VALID, &ls->ls_flags);
	clear_bit(LSFL_DIR_VALID, &ls->ls_flags);
	clear_bit(LSFL_ALL_DIR_VALID, &ls->ls_flags);
	clear_bit(LSFL_NODES_VALID, &ls->ls_flags);
	clear_bit(LSFL_ALL_NODES_VALID, &ls->ls_flags);
	dlm_recoverd_resume(ls);
	dlm_recoverd_kick(ls);
	return 0;
}

int dlm_ls_start(struct dlm_ls *ls, int event_nr)
{
	struct dlm_recover *rv;
	int error = 0;

	rv = kmalloc(sizeof(struct dlm_recover), GFP_KERNEL);
	if (!rv)
		return -ENOMEM;
	memset(rv, 0, sizeof(struct dlm_recover));

	spin_lock(&ls->ls_recover_lock);

	if (test_bit(LSFL_LS_RUN, &ls->ls_flags)) {
		spin_unlock(&ls->ls_recover_lock);
		log_error(ls, "start ignored: lockspace running");
		kfree(rv);
		error = -EINVAL;
		goto out;
	}

	if (!ls->ls_nodeids_next) {
		spin_unlock(&ls->ls_recover_lock);
		log_error(ls, "start ignored: existing nodeids_next");
		kfree(rv);
		error = -EINVAL;
		goto out;
	}

	if (event_nr <= ls->ls_last_start) {
		spin_unlock(&ls->ls_recover_lock);
		log_error(ls, "start event_nr %d not greater than last %d",
			  event_nr, ls->ls_last_start);
		kfree(rv);
		error = -EINVAL;
		goto out;
	}

	rv->nodeids = ls->ls_nodeids_next;
	ls->ls_nodeids_next = NULL;
	rv->node_count = ls->ls_nodeids_next_count;
	rv->event_id = event_nr;
	ls->ls_last_start = event_nr;
	list_add_tail(&rv->list, &ls->ls_recover);
	set_bit(LSFL_LS_START, &ls->ls_flags);
	spin_unlock(&ls->ls_recover_lock);

	set_bit(LSFL_JOIN_DONE, &ls->ls_flags);
	wake_up(&ls->ls_wait_member);
	dlm_recoverd_kick(ls);
 out:
	return error;
}

int dlm_ls_finish(struct dlm_ls *ls, int event_nr)
{
	spin_lock(&ls->ls_recover_lock);
	if (event_nr != ls->ls_last_start) {
		spin_unlock(&ls->ls_recover_lock);
		log_error(ls, "finish event_nr %d doesn't match start %d",
			  event_nr, ls->ls_last_start);
		return -EINVAL;
	}
	ls->ls_last_finish = event_nr;
	set_bit(LSFL_LS_FINISH, &ls->ls_flags);
	spin_unlock(&ls->ls_recover_lock);
	dlm_recoverd_kick(ls);
	return 0;
}

