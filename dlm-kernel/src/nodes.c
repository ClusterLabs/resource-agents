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

#include <net/sock.h>
#include <cluster/cnxman.h>

#include "dlm_internal.h"
#include "lowcomms.h"
#include "nodes.h"
#include "recover.h"
#include "reccomms.h"
#include "util.h"

static struct list_head cluster_nodes;
static spinlock_t node_lock;


void dlm_nodes_init(void)
{
	INIT_LIST_HEAD(&cluster_nodes);
	spin_lock_init(&node_lock);
}

static struct dlm_node *search_node(uint32_t nodeid)
{
	struct dlm_node *node;

	list_for_each_entry(node, &cluster_nodes, list) {
		if (node->nodeid == nodeid)
			goto out;
	}
	node = NULL;
 out:
	return node;
}

static void put_node(struct dlm_node *node)
{
	spin_lock(&node_lock);
	if (atomic_dec_and_test(&node->refcount)) {
		list_del(&node->list);
		spin_unlock(&node_lock);
		lowcomms_close(node->nodeid);
		kfree(node);
		return;
	}
	spin_unlock(&node_lock);
}

static int get_node(uint32_t nodeid, struct dlm_node **ndp)
{
	struct dlm_node *node, *node2;
	int error = -ENOMEM;

	spin_lock(&node_lock);
	node = search_node(nodeid);
	if (node)
		atomic_inc(&node->refcount);
	spin_unlock(&node_lock);

	if (node)
		goto out;

	node = kmalloc(sizeof(struct dlm_node), GFP_KERNEL);
	if (!node)
		goto fail;

	memset(node, 0, sizeof(struct dlm_node));
	node->nodeid = nodeid;

	spin_lock(&node_lock);
	node2 = search_node(nodeid);
	if (node2) {
		atomic_inc(&node2->refcount);
		spin_unlock(&node_lock);
		kfree(node);
		node = node2;
		goto out;
	}

	atomic_set(&node->refcount, 1);
	list_add_tail(&node->list, &cluster_nodes);
	spin_unlock(&node_lock);

 out:
	*ndp = node;
	return 0;
 fail:
	return error;
}

int init_new_csb(uint32_t nodeid, struct dlm_csb **ret_csb)
{
	struct dlm_csb *csb;
	struct dlm_node *node;
	int error = -ENOMEM;

	csb = kmalloc(sizeof(struct dlm_csb), GFP_KERNEL);
	if (!csb)
		goto fail;

	memset(csb, 0, sizeof(struct dlm_csb));

	error = get_node(nodeid, &node);
	if (error)
		goto fail_free;

	csb->node = node;
	*ret_csb = csb;
	return 0;

 fail_free:
	kfree(csb);
 fail:
	return error;
}

void release_csb(struct dlm_csb *csb)
{
	put_node(csb->node);
	kfree(csb);
}

uint32_t our_nodeid(void)
{
	return lowcomms_our_nodeid();
}

static void make_node_array(struct dlm_ls *ls)
{
	struct dlm_csb *csb;
	uint32_t *array;
	int i = 0;

	if (ls->ls_node_array) {
		kfree(ls->ls_node_array);
		ls->ls_node_array = NULL;
	}

	array = kmalloc(sizeof(uint32_t) * ls->ls_num_nodes, GFP_KERNEL);
	if (!array)
		return;

	list_for_each_entry(csb, &ls->ls_nodes, list)
		array[i++] = csb->node->nodeid;

	ls->ls_node_array = array;
}

int nodes_reconfig_wait(struct dlm_ls *ls)
{
	int error;

	if (ls->ls_low_nodeid == our_nodeid()) {
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

static void add_ordered_node(struct dlm_ls *ls, struct dlm_csb *new)
{
	struct dlm_csb *csb = NULL;
	struct list_head *tmp;
	struct list_head *newlist = &new->list;
	struct list_head *head = &ls->ls_nodes;

	list_for_each(tmp, head) {
		csb = list_entry(tmp, struct dlm_csb, list);

		if (new->node->nodeid < csb->node->nodeid)
			break;
	}

	if (!csb)
		list_add_tail(newlist, head);
	else {
		/* FIXME: can use list macro here */
		newlist->prev = tmp->prev;
		newlist->next = tmp;
		tmp->prev->next = newlist;
		tmp->prev = newlist;
	}
}

int ls_nodes_reconfig(struct dlm_ls *ls, struct dlm_recover *rv, int *neg_out)
{
	struct dlm_csb *csb, *safe;
	int error, i, found, pos = 0, neg = 0;
	uint32_t low = (uint32_t) (-1);

	/* 
	 * Remove (and save) departed nodes from lockspace's nodes list
	 */

	list_for_each_entry_safe(csb, safe, &ls->ls_nodes, list) {
		found = FALSE;
		for (i = 0; i < rv->node_count; i++) {
			if (csb->node->nodeid == rv->nodeids[i]) {
				found = TRUE;
				break;
			}
		}

		if (!found) {
			neg++;
			csb->gone_event = rv->event_id;
			list_del(&csb->list);
			list_add_tail(&csb->list, &ls->ls_nodes_gone);
			ls->ls_num_nodes--;
			log_all(ls, "remove node %u", csb->node->nodeid);
		}
	}

	/* 
	 * Add new nodes to lockspace's nodes list
	 */

	for (i = 0; i < rv->node_count; i++) {
		found = FALSE;
		list_for_each_entry(csb, &ls->ls_nodes, list) {
			if (csb->node->nodeid == rv->nodeids[i]) {
				found = TRUE;
				break;
			}
		}

		if (!found) {
			pos++;

			error = init_new_csb(rv->nodeids[i], &csb);
			DLM_ASSERT(!error,);

			add_ordered_node(ls, csb);
			ls->ls_num_nodes++;
			log_all(ls, "add node %u", csb->node->nodeid);
		}
	}

	list_for_each_entry(csb, &ls->ls_nodes, list) {
		if (csb->node->nodeid < low)
			low = csb->node->nodeid;
	}

	ls->ls_low_nodeid = low;
	set_bit(LSFL_NODES_VALID, &ls->ls_flags);
	*neg_out = neg;
	make_node_array(ls);

	error = nodes_reconfig_wait(ls);

	log_all(ls, "total nodes %d", ls->ls_num_nodes);

	return error;
}

static void nodes_clear(struct list_head *head)
{
	struct dlm_csb *csb;

	while (!list_empty(head)) {
		csb = list_entry(head->next, struct dlm_csb, list);
		list_del(&csb->list);
		release_csb(csb);
	}
}

void ls_nodes_clear(struct dlm_ls *ls)
{
	nodes_clear(&ls->ls_nodes);
	ls->ls_num_nodes = 0;
}

void ls_nodes_gone_clear(struct dlm_ls *ls)
{
	nodes_clear(&ls->ls_nodes_gone);
}

int ls_nodes_init(struct dlm_ls *ls, struct dlm_recover *rv)
{
	struct dlm_csb *csb;
	int i, error;
	uint32_t low = (uint32_t) (-1);

	/* nodes may be left from a previous failed start */
	ls_nodes_clear(ls);

	log_all(ls, "add nodes");

	for (i = 0; i < rv->node_count; i++) {
		error = init_new_csb(rv->nodeids[i], &csb);
		if (error)
			goto fail;

		add_ordered_node(ls, csb);
		ls->ls_num_nodes++;

		if (csb->node->nodeid < low)
			low = csb->node->nodeid;
	}

	ls->ls_low_nodeid = low;
	set_bit(LSFL_NODES_VALID, &ls->ls_flags);
	make_node_array(ls);

	error = nodes_reconfig_wait(ls);

	log_all(ls, "total nodes %d", ls->ls_num_nodes);
	return error;
 fail:
	ls_nodes_clear(ls);
	return error;
}

int in_nodes_gone(struct dlm_ls *ls, uint32_t nodeid)
{
	struct dlm_csb *csb;

	list_for_each_entry(csb, &ls->ls_nodes_gone, list) {
		if (csb->node->nodeid == nodeid)
			return TRUE;
	}
	return FALSE;
}
