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
static uint32_t local_nodeid;
static struct semaphore local_init_lock;


void dlm_nodes_init(void)
{
	INIT_LIST_HEAD(&cluster_nodes);
	spin_lock_init(&node_lock);
	local_nodeid = 0;
	init_MUTEX(&local_init_lock);
}

static gd_node_t *search_node(uint32_t nodeid)
{
	gd_node_t *node;

	list_for_each_entry(node, &cluster_nodes, gn_list) {
		if (node->gn_nodeid == nodeid)
			goto out;
	}
	node = NULL;
      out:
	return node;
}

static void put_node(gd_node_t *node)
{
	spin_lock(&node_lock);
	node->gn_refcount--;
	if (node->gn_refcount == 0) {
		list_del(&node->gn_list);
		spin_unlock(&node_lock);
		kfree(node);
		return;
	}
	spin_unlock(&node_lock);
}

static int get_node(uint32_t nodeid, gd_node_t **ndp)
{
	gd_node_t *node, *node2;
	int error = -ENOMEM;

	spin_lock(&node_lock);
	node = search_node(nodeid);
	if (node)
		node->gn_refcount++;
	spin_unlock(&node_lock);

	if (node)
		goto out;

	node = (gd_node_t *) kmalloc(sizeof(gd_node_t), GFP_KERNEL);
	if (!node)
		goto fail;

	memset(node, 0, sizeof(gd_node_t));
	node->gn_nodeid = nodeid;

	spin_lock(&node_lock);
	node2 = search_node(nodeid);
	if (node2) {
		node2->gn_refcount++;
		spin_unlock(&node_lock);
		kfree(node);
		node = node2;
		goto out;
	}

	node->gn_refcount = 1;
	list_add_tail(&node->gn_list, &cluster_nodes);
	spin_unlock(&node_lock);

      out:
	*ndp = node;
	return 0;

      fail:
	return error;
}

int init_new_csb(uint32_t nodeid, gd_csb_t **ret_csb)
{
	gd_csb_t *csb;
	gd_node_t *node;
	int error = -ENOMEM;

	csb = (gd_csb_t *) kmalloc(sizeof(gd_csb_t), GFP_KERNEL);
	if (!csb)
		goto fail;

	memset(csb, 0, sizeof(gd_csb_t));

	error = get_node(nodeid, &node);
	if (error)
		goto fail_free;

	csb->csb_node = node;

	down(&local_init_lock);

	if (!local_nodeid) {
		if (nodeid == our_nodeid()) {
			local_nodeid = node->gn_nodeid;
		}
	}
	up(&local_init_lock);

	*ret_csb = csb;
	return 0;

      fail_free:
	kfree(csb);
      fail:
	return error;
}

void release_csb(gd_csb_t *csb)
{
	put_node(csb->csb_node);
	kfree(csb);
}

uint32_t our_nodeid(void)
{
	return lowcomms_our_nodeid();
}

int nodes_reconfig_wait(gd_ls_t *ls)
{
	int error;

	if (ls->ls_low_nodeid == our_nodeid()) {
		error = gdlm_wait_status_all(ls, NODES_VALID);
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
		error = gdlm_wait_status_low(ls, NODES_ALL_VALID);

	return error;
}

static void add_ordered_node(gd_ls_t *ls, gd_csb_t *new)
{
	gd_csb_t *csb = NULL;
	struct list_head *tmp;
	struct list_head *newlist = &new->csb_list;
	struct list_head *head = &ls->ls_nodes;

	list_for_each(tmp, head) {
		csb = list_entry(tmp, gd_csb_t, csb_list);

		if (new->csb_node->gn_nodeid < csb->csb_node->gn_nodeid)
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

int ls_nodes_reconfig(gd_ls_t *ls, gd_recover_t *gr, int *neg_out)
{
	gd_csb_t *csb, *safe;
	int error, i, found, pos = 0, neg = 0;
	uint32_t low = (uint32_t) (-1);

	/* 
	 * Remove (and save) departed nodes from lockspace's nodes list
	 */

	list_for_each_entry_safe(csb, safe, &ls->ls_nodes, csb_list) {
		found = FALSE;
		for (i = 0; i < gr->gr_node_count; i++) {
			if (csb->csb_node->gn_nodeid == gr->gr_nodeids[i]) {
				found = TRUE;
				break;
			}
		}

		if (!found) {
			neg++;
			csb->csb_gone_event = gr->gr_event_id;
			list_del(&csb->csb_list);
			list_add_tail(&csb->csb_list, &ls->ls_nodes_gone);
			ls->ls_num_nodes--;
			log_all(ls, "remove node %u", csb->csb_node->gn_nodeid);
		}
	}

	/* 
	 * Add new nodes to lockspace's nodes list
	 */

	for (i = 0; i < gr->gr_node_count; i++) {
		found = FALSE;
		list_for_each_entry(csb, &ls->ls_nodes, csb_list) {
			if (csb->csb_node->gn_nodeid == gr->gr_nodeids[i]) {
				found = TRUE;
				break;
			}
		}

		if (!found) {
			pos++;

			error = init_new_csb(gr->gr_nodeids[i], &csb);
			GDLM_ASSERT(!error,);

			add_ordered_node(ls, csb);
			ls->ls_num_nodes++;
			log_all(ls, "add node %u", csb->csb_node->gn_nodeid);
		}
	}

	list_for_each_entry(csb, &ls->ls_nodes, csb_list) {
		if (csb->csb_node->gn_nodeid < low)
			low = csb->csb_node->gn_nodeid;
	}

	rcom_log_clear(ls);
	ls->ls_low_nodeid = low;
	ls->ls_nodes_mask = gdlm_next_power2(ls->ls_num_nodes) - 1;
	set_bit(LSFL_NODES_VALID, &ls->ls_flags);
	*neg_out = neg;

	error = nodes_reconfig_wait(ls);

	log_all(ls, "total nodes %d", ls->ls_num_nodes);

	return error;
}

int ls_nodes_init(gd_ls_t *ls, gd_recover_t *gr)
{
	gd_csb_t *csb;
	int i, error;
	uint32_t low = (uint32_t) (-1);

	log_all(ls, "add nodes");

	for (i = 0; i < gr->gr_node_count; i++) {
		error = init_new_csb(gr->gr_nodeids[i], &csb);
		if (error)
			goto fail;

		add_ordered_node(ls, csb);
		ls->ls_num_nodes++;

		if (csb->csb_node->gn_nodeid < low)
			low = csb->csb_node->gn_nodeid;
	}

	ls->ls_low_nodeid = low;
	ls->ls_nodes_mask = gdlm_next_power2(ls->ls_num_nodes) - 1;
	set_bit(LSFL_NODES_VALID, &ls->ls_flags);

	error = nodes_reconfig_wait(ls);

	log_all(ls, "total nodes %d", ls->ls_num_nodes);

	return error;

      fail:
	while (!list_empty(&ls->ls_nodes)) {
		csb = list_entry(ls->ls_nodes.next, gd_csb_t, csb_list);
		list_del(&csb->csb_list);
		release_csb(csb);
	}
	ls->ls_num_nodes = 0;

	return error;
}

int in_nodes_gone(gd_ls_t *ls, uint32_t nodeid)
{
	gd_csb_t *csb;

	list_for_each_entry(csb, &ls->ls_nodes_gone, csb_list) {
		if (csb->csb_node->gn_nodeid == nodeid)
			return TRUE;
	}
	return FALSE;
}
