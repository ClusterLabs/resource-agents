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

#include "dlm_internal.h"
#include "locking.h"
#include "memory.h"
#include "lockqueue.h"
#include "nodes.h"
#include "dir.h"
#include "util.h"

static gd_res_t *search_hashchain(struct list_head *head, gd_res_t *parent,
				  char *name, int namelen)
{
	gd_res_t *r;

	list_for_each_entry(r, head, res_hashchain) {
		if ((parent == r->res_parent) && (namelen == r->res_length) &&
		    (memcmp(name, r->res_name, namelen) == 0)) {
			atomic_inc(&r->res_ref);
			return r;
		}
	}

	return NULL;
}

/*
 * A way to arbitrarily hold onto an rsb which we already have a reference to
 * to make sure it doesn't go away.  Opposite of release_rsb().
 */

void hold_rsb(gd_res_t *r)
{
	atomic_inc(&r->res_ref);
}

/*
 * release_rsb() - Decrement reference count on rsb struct.  Free the rsb
 * struct when there are zero references.  Every lkb for the rsb adds a
 * reference.  When ref is zero there can be no more lkb's for the rsb, on the
 * queue's or anywhere else.
 */

void release_rsb(gd_res_t *r)
{
	gd_ls_t *ls = r->res_ls;
	int removed = FALSE;

	write_lock(&ls->ls_reshash_lock);
	atomic_dec(&r->res_ref);

	if (!atomic_read(&r->res_ref)) {
		GDLM_ASSERT(list_empty(&r->res_grantqueue),);
		GDLM_ASSERT(list_empty(&r->res_waitqueue),);
		GDLM_ASSERT(list_empty(&r->res_convertqueue),);
		removed = TRUE;
		list_del(&r->res_hashchain);
	}
	write_unlock(&ls->ls_reshash_lock);

	if (removed) {
		down_read(&ls->ls_gap_rsblist);
		if (r->res_parent)
			list_del(&r->res_subreslist);
		else
			list_del(&r->res_rootlist);
		up_read(&ls->ls_gap_rsblist);

		/*
		 * Remove resdir entry if this was a locally mastered root rsb.
		 */
		if (!r->res_parent && !r->res_nodeid) {
			if (get_directory_nodeid(r) != our_nodeid())
				remote_remove_resdata(r->res_ls,
						      get_directory_nodeid(r),
						      r->res_name,
						      r->res_length,
						      r->res_resdir_seq);
			else
				remove_resdata(r->res_ls, our_nodeid(),
					       r->res_name, r->res_length,
					       r->res_resdir_seq);
		}

		if (r->res_lvbptr)
			free_lvb(r->res_lvbptr);

		free_rsb(r);
	}
}

/*
 * find_or_create_rsb() - Get an rsb struct, or create one if it doesn't exist.
 * If the rsb exists, its ref count is incremented by this function.  If it
 * doesn't exist, it's created with a ref count of one.
 */

int find_or_create_rsb(gd_ls_t *ls, gd_res_t *parent, char *name, int namelen,
		       int create, gd_res_t **rp)
{
	uint32_t hash;
	gd_res_t *r, *tmp;
	int error = -ENOMEM;

	GDLM_ASSERT(namelen <= DLM_RESNAME_MAXLEN,);

	hash = gdlm_hash(name, namelen);
	hash &= ls->ls_hashmask;

	read_lock(&ls->ls_reshash_lock);
	r = search_hashchain(&ls->ls_reshashtbl[hash], parent, name, namelen);
	read_unlock(&ls->ls_reshash_lock);

	if (r)
		goto out_set;
	if (!create) {
		*rp = NULL;
		goto out;
	}

	r = allocate_rsb(ls, namelen);
	if (!r)
		goto fail;

	INIT_LIST_HEAD(&r->res_subreslist);
	INIT_LIST_HEAD(&r->res_grantqueue);
	INIT_LIST_HEAD(&r->res_convertqueue);
	INIT_LIST_HEAD(&r->res_waitqueue);

	memcpy(r->res_name, name, namelen);
	r->res_length = namelen;
	r->res_ls = ls;
	init_rwsem(&r->res_lock);
	atomic_set(&r->res_ref, 1);

	if (parent) {
		r->res_parent = parent;
		r->res_depth = parent->res_depth + 1;
		r->res_root = parent->res_root;
		r->res_nodeid = parent->res_nodeid;
	} else {
		r->res_parent = NULL;
		r->res_depth = 1;
		r->res_root = r;
		r->res_nodeid = -1;
	}

	write_lock(&ls->ls_reshash_lock);
	tmp = search_hashchain(&ls->ls_reshashtbl[hash], parent, name, namelen);
	if (tmp) {
		write_unlock(&ls->ls_reshash_lock);
		free_rsb(r);
		r = tmp;
	} else {
		list_add(&r->res_hashchain, &ls->ls_reshashtbl[hash]);
		write_unlock(&ls->ls_reshash_lock);

		down_read(&ls->ls_gap_rsblist);
		if (parent)
			list_add_tail(&r->res_subreslist,
				      &r->res_root->res_subreslist);
		else
			list_add(&r->res_rootlist, &ls->ls_rootres);
		up_read(&ls->ls_gap_rsblist);
	}

      out_set:
	*rp = r;

      out:
	error = 0;

      fail:
	return error;
}

/*
 * Add a LKB to a resource's grant/convert/wait queue. in order
 */

void lkb_add_ordered(struct list_head *new, struct list_head *head, int mode)
{
	gd_lkb_t *lkb = NULL;

	list_for_each_entry(lkb, head, lkb_statequeue) {
		if (lkb->lkb_rqmode < mode)
			break;
	}

	if (!lkb) {
		/* No entries in the queue, we are alone */
	        list_add_tail(new, head);
	} else {
	        __list_add(new, lkb->lkb_statequeue.prev, &lkb->lkb_statequeue);
	}
}

/*
 * The rsb res_lock must be held in write when this function is called.
 */

void lkb_enqueue(gd_res_t *r, gd_lkb_t *lkb, int type)
{

	GDLM_ASSERT(!lkb->lkb_status, printk("status=%u\n", lkb->lkb_status););

	lkb->lkb_status = type;

	switch (type) {
	case GDLM_LKSTS_WAITING:
		list_add_tail(&lkb->lkb_statequeue, &r->res_waitqueue);
		break;

	case GDLM_LKSTS_GRANTED:
		lkb_add_ordered(&lkb->lkb_statequeue, &r->res_grantqueue,
				lkb->lkb_grmode);
		break;

	case GDLM_LKSTS_CONVERT:
	        if (lkb->lkb_lockqueue_flags & DLM_LKF_EXPEDITE)
		        list_add(&lkb->lkb_statequeue, &r->res_convertqueue);

		else
		        if (lkb->lkb_lockqueue_flags & DLM_LKF_QUECVT)
			        list_add_tail(&lkb->lkb_statequeue,
					      &r->res_convertqueue);
			else
			        lkb_add_ordered(&lkb->lkb_statequeue,
						&r->res_convertqueue, lkb->lkb_rqmode);
		break;

	default:
		GDLM_ASSERT(0,);
	}
}

void res_lkb_enqueue(gd_res_t *r, gd_lkb_t *lkb, int type)
{
	down_write(&r->res_lock);
	lkb_enqueue(r, lkb, type);
	up_write(&r->res_lock);
}

/*
 * The rsb res_lock must be held in write when this function is called.
 */

int lkb_dequeue(gd_lkb_t *lkb)
{
	int status = lkb->lkb_status;

	if (!status)
		goto out;

	lkb->lkb_status = 0;
	list_del(&lkb->lkb_statequeue);

      out:
	return status;
}

int res_lkb_dequeue(gd_lkb_t *lkb)
{
	int status;

	down_write(&lkb->lkb_resource->res_lock);
	status = lkb_dequeue(lkb);
	up_write(&lkb->lkb_resource->res_lock);

	return status;
}

/*
 * The rsb res_lock must be held in write when this function is called.
 */

int lkb_swqueue(gd_res_t *r, gd_lkb_t *lkb, int type)
{
	int status;

	status = lkb_dequeue(lkb);
	lkb_enqueue(r, lkb, type);

	return status;
}

int res_lkb_swqueue(gd_res_t *r, gd_lkb_t *lkb, int type)
{
	int status;

	down_write(&r->res_lock);
	status = lkb_swqueue(r, lkb, type);
	up_write(&r->res_lock);

	return status;
}
