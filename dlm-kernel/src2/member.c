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
#include "member_ioctl.h"
#include "member_sysfs.h"
#include "lockspace.h"
#include "member.h"
#include "recoverd.h"
#include "recover.h"
#include "reccomms.h"

/* Local values must be set before the dlm is started and then
   not modified, otherwise a lock is needed. */

static char *           local_addr[DLM_MAX_ADDR_COUNT];
static int              local_nodeid;
static int              local_weight;
static int              local_count;
static struct list_head nodes;
static struct semaphore nodes_sem;


int dlm_our_addr(int i, char *addr)
{
	if (!local_count)
		return -ENOENT;
	if (i > local_count - 1)
		return -EINVAL;
	memcpy(addr, local_addr[i], DLM_ADDR_LEN);
	return 0;
}

int dlm_our_nodeid(void)
{
	return local_nodeid;
}

static struct dlm_node *search_node(int nodeid)
{
	struct dlm_node *node;

	list_for_each_entry(node, &nodes, list) {
		if (node->nodeid == nodeid)
			goto out;
	}
	node = NULL;
 out:
	return node;
}

static struct dlm_node *search_node_addr(char *addr)
{
	struct dlm_node *node;

	list_for_each_entry(node, &nodes, list) {
		if (!memcmp(node->addr, addr, DLM_ADDR_LEN))
			goto out;
	}
	node = NULL;
 out:
	return node;
}

static int _get_node(int nodeid, struct dlm_node **node_ret)
{
	struct dlm_node *node;
	int error = 0;

	node = search_node(nodeid);
	if (node)
		goto out;

	node = kmalloc(sizeof(struct dlm_node), GFP_KERNEL);
	if (!node) {
		error = -ENOMEM;
		goto out;
	}
	memset(node, 0, sizeof(struct dlm_node));
	node->nodeid = nodeid;
	list_add_tail(&node->list, &nodes);
 out:
	*node_ret = node;
	return error;
}

static int get_node(int nodeid, struct dlm_node **node_ret)
{
	int error;
	down(&nodes_sem);
	error = _get_node(nodeid, node_ret);
	up(&nodes_sem);
	return error;
}

int dlm_nodeid_addr(int nodeid, char *addr)
{
	struct dlm_node *node;

	down(&nodes_sem);
	node = search_node(nodeid);
	up(&nodes_sem);
	if (!node)
		return -1;
	memcpy(addr, node->addr, DLM_ADDR_LEN);
	return 0;
}

int dlm_addr_nodeid(char *addr, int *nodeid)
{
	struct dlm_node *node;

	down(&nodes_sem);
	node = search_node_addr(addr);
	up(&nodes_sem);
	if (!node)
		return -1;
	*nodeid = node->nodeid;
	return 0;
}

int __init dlm_member_init(void)
{
	int error;

	INIT_LIST_HEAD(&nodes);
	init_MUTEX(&nodes_sem);

	error = dlm_member_ioctl_init();
	if (error)
		return error;

	error = dlm_member_sysfs_init();
	if (error)
		dlm_member_ioctl_exit();

	return error;
}

void dlm_member_exit(void)
{
	int i;

	dlm_member_sysfs_exit();
	dlm_member_ioctl_exit();
	for (i = 0; i < local_count; i++)
		kfree(local_addr[i]);
	local_nodeid = 0;
	local_weight = 0;
	local_count = 0;
}

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
		if (new->node->nodeid < memb->node->nodeid)
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

int dlm_add_member(struct dlm_ls *ls, int nodeid)
{
	struct dlm_node *node;
	struct dlm_member *memb;
	int error;

	error = get_node(nodeid, &node);
	if (error)
		return error;

	memb = kmalloc(sizeof(struct dlm_member), GFP_KERNEL);
	if (!memb)
		return -ENOMEM;

	memb->node = node;
	add_ordered_member(ls, memb);
	ls->ls_num_nodes++;
	return 0;
}

void dlm_remove_member(struct dlm_ls *ls, struct dlm_member *memb)
{
	list_move(&memb->list, &ls->ls_nodes_gone);
	ls->ls_num_nodes--;
}

int dlm_is_member(struct dlm_ls *ls, int nodeid)
{
	struct dlm_member *memb;

	list_for_each_entry(memb, &ls->ls_nodes, list) {
		if (memb->node->nodeid == nodeid)
			return TRUE;
	}
	return FALSE;
}

int dlm_is_removed(struct dlm_ls *ls, int nodeid)
{
	struct dlm_member *memb;

	list_for_each_entry(memb, &ls->ls_nodes_gone, list) {
		if (memb->node->nodeid == nodeid)
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
		array[i++] = memb->node->nodeid;

	ls->ls_node_array = array;
}

int dlm_recover_members_wait(struct dlm_ls *ls)
{
	int error;

	if (ls->ls_low_nodeid == local_nodeid) {
		error = dlm_wait_status_all(ls, NODES_VALID);
		if (!error)
			set_bit(LSFL_ALL_NODES_VALID, &ls->ls_flags);

		/* Experimental: this delay should allow any final messages
		 * from the previous node to be received before beginning
		 * recovery. */

		if (ls->ls_num_nodes == 1) {
			current->state = TASK_UNINTERRUPTIBLE;
			schedule_timeout((2) * HZ);
		}
	} else
		error = dlm_wait_status_low(ls, NODES_ALL_VALID);

	return error;
}

int dlm_recover_members(struct dlm_ls *ls, struct dlm_recover *rv, int *neg_out)
{
	struct dlm_member *memb, *safe;
	int i, error, found, pos = 0, neg = 0, low = -1;

	/* move departed members from ls_nodes to ls_nodes_gone */

	list_for_each_entry_safe(memb, safe, &ls->ls_nodes, list) {
		found = FALSE;
		for (i = 0; i < rv->node_count; i++) {
			if (memb->node->nodeid == rv->nodeids[i]) {
				found = TRUE;
				break;
			}
		}

		if (!found) {
			neg++;
			memb->gone_event = rv->event_id;
			dlm_remove_member(ls, memb);
			log_debug(ls, "remove member %d", memb->node->nodeid);
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
		if (low == -1 || memb->node->nodeid < low)
			low = memb->node->nodeid;
	}
	ls->ls_low_nodeid = low;

	make_member_array(ls);
	set_bit(LSFL_NODES_VALID, &ls->ls_flags);
	*neg_out = neg;

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

	error = dlm_recover_members_wait(ls);
	log_debug(ls, "total members %d", ls->ls_num_nodes);
	return error;
}

/*
 * Following called from member_ioctl.c
 */

int dlm_set_node(struct dlm_member_ioctl *param)
{
	struct dlm_node *node;
	int error;

	down(&nodes_sem);
	error = _get_node(param->nodeid, &node);
	if (!error) {
		memcpy(node->addr, param->addr, DLM_ADDR_LEN);
		node->weight = param->weight;
	}
	up(&nodes_sem);
	return error;
}

int dlm_set_local(struct dlm_member_ioctl *param)
{
	char *p;

	if (local_count > DLM_MAX_ADDR_COUNT - 1)
		return -EINVAL;
	local_nodeid = param->nodeid;
	local_weight = param->weight;

	p = kmalloc(DLM_ADDR_LEN, GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	memcpy(p, param->addr, DLM_ADDR_LEN);
	local_addr[local_count++] = p;
	return 0;
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
	clear_bit(LSFL_RESDIR_VALID, &ls->ls_flags);
	clear_bit(LSFL_ALL_RESDIR_VALID, &ls->ls_flags);
	clear_bit(LSFL_NODES_VALID, &ls->ls_flags);
	clear_bit(LSFL_ALL_NODES_VALID, &ls->ls_flags);
	dlm_recoverd_resume(ls);
	dlm_recoverd_kick(ls);
	log_error(ls, "dlm_ls_stop");
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
	if (!ls->ls_nodeids_next) {
		spin_unlock(&ls->ls_recover_lock);
		log_error(ls, "existing nodeids_next");
		kfree(rv);
		error = -EINVAL;
		goto out;
	}
	rv->nodeids = ls->ls_nodeids_next;
	ls->ls_nodeids_next = NULL;
	rv->node_count = ls->ls_nodeids_next_count;

	if (ls->ls_last_start == event_nr)
		log_debug(ls, "repeated start %d stop %d finish %d",
			  event_nr, ls->ls_last_stop, ls->ls_last_finish);

	rv->event_id = event_nr;
	ls->ls_last_start = event_nr;
	list_add_tail(&rv->list, &ls->ls_recover);
	set_bit(LSFL_LS_START, &ls->ls_flags);
	spin_unlock(&ls->ls_recover_lock);

	set_bit(LSFL_JOIN_DONE, &ls->ls_flags);
	wake_up(&ls->ls_wait_member);
	dlm_recoverd_kick(ls);
	log_error(ls, "dlm_ls_start %d", event_nr);
 out:
	return error;
}

int dlm_ls_finish(struct dlm_ls *ls, int event_nr)
{
	spin_lock(&ls->ls_recover_lock);
	ls->ls_last_finish = event_nr;
	set_bit(LSFL_LS_FINISH, &ls->ls_flags);
	spin_unlock(&ls->ls_recover_lock);
	dlm_recoverd_kick(ls);
	log_error(ls, "dlm_ls_finish %d", event_nr);
	return 0;
}

