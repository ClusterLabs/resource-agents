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

/*
 * locking.c
 *
 * This is where the main work of the DLM goes on
 *
 */

#include "dlm_internal.h"
#include "lockqueue.h"
#include "locking.h"
#include "lockspace.h"
#include "lkb.h"
#include "nodes.h"
#include "dir.h"
#include "ast.h"
#include "memory.h"
#include "rsb.h"
#include "util.h"
#include "lowcomms.h"

extern struct list_head lslist;

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

/*
 * Lock compatibilty matrix - thanks Steve
 * UN = Unlocked state. Not really a state, used as a flag
 * PD = Padding. Used to make the matrix a nice power of two in size
 * Other states are the same as the VMS DLM.
 * Usage: matrix[grmode+1][rqmode+1]  (although m[rq+1][gr+1] is the same)
 */

#define modes_compat(gr, rq) \
	__dlm_compat_matrix[(gr)->lkb_grmode + 1][(rq)->lkb_rqmode + 1]

const int __dlm_compat_matrix[8][8] = {
      /* UN NL CR CW PR PW EX PD */
	{1, 1, 1, 1, 1, 1, 1, 0},	/* UN */
	{1, 1, 1, 1, 1, 1, 1, 0},	/* NL */
	{1, 1, 1, 1, 1, 1, 0, 0},	/* CR */
	{1, 1, 1, 1, 0, 0, 0, 0},	/* CW */
	{1, 1, 1, 0, 1, 0, 0, 0},	/* PR */
	{1, 1, 1, 0, 0, 0, 0, 0},	/* PW */
	{1, 1, 0, 0, 0, 0, 0, 0},	/* EX */
	{0, 0, 0, 0, 0, 0, 0, 0}	/* PD */
};

/*
 * Compatibility matrix for conversions with QUECVT set.
 * Granted mode is the row; requested mode is the column.
 * Usage: matrix[grmode+1][rqmode+1]
 */

const int __quecvt_compat_matrix[8][8] = {
      /* UN NL CR CW PR PW EX PD */
	{0, 0, 0, 0, 0, 0, 0, 0},	/* UN */
	{0, 0, 1, 1, 1, 1, 1, 0},	/* NL */
	{0, 0, 0, 1, 1, 1, 1, 0},	/* CR */
	{0, 0, 0, 0, 1, 1, 1, 0},	/* CW */
	{0, 0, 0, 1, 0, 1, 1, 0},	/* PR */
	{0, 0, 0, 0, 0, 0, 1, 0},	/* PW */
	{0, 0, 0, 0, 0, 0, 0, 0},	/* EX */
	{0, 0, 0, 0, 0, 0, 0, 0}	/* PD */
};

/*
 * This defines the direction of transfer of LVB data.
 * Granted mode is the row; requested mode is the column.
 * Usage: matrix[grmode+1][rqmode+1]
 * 1 = LVB is returned to the caller
 * 0 = LVB is written to the resource
 * -1 = nothing happens to the LVB
 */

const int __lvb_operations[8][8] = {
	/* UN   NL  CR  CW  PR  PW  EX  PD*/
	{  -1,  1,  1,  1,  1,  1,  1, -1 }, /* UN */
	{  -1,  1,  1,  1,  1,  1,  1,  0 }, /* NL */
	{  -1, -1,  1,  1,  1,  1,  1,  0 }, /* CR */
	{  -1, -1, -1,  1,  1,  1,  1,  0 }, /* CW */
	{  -1, -1, -1, -1,  1,  1,  1,  0 }, /* PR */
	{  -1,  0,  0,  0,  0,  0,  1,  0 }, /* PW */
	{  -1,  0,  0,  0,  0,  0,  0,  0 }, /* EX */
	{  -1,  0,  0,  0,  0,  0,  0,  0 }  /* PD */
};

static void grant_lock(struct dlm_lkb *lkb, int send_remote);
static void send_blocking_asts(struct dlm_rsb *rsb, struct dlm_lkb *lkb);
static void send_blocking_asts_all(struct dlm_rsb *rsb, struct dlm_lkb *lkb);
static int convert_lock(struct dlm_ls *ls, int mode, struct dlm_lksb *lksb,
			uint32_t flags, void *ast, void *astarg, void *bast,
			struct dlm_range *range);
static int dlm_lock_stage1(struct dlm_ls *ls, struct dlm_lkb *lkb,
			   uint32_t flags, char *name, int namelen);


inline int dlm_modes_compat(int mode1, int mode2)
{
	return __dlm_compat_matrix[mode1 + 1][mode2 + 1];
}

static inline int first_in_list(struct dlm_lkb *lkb, struct list_head *head)
{
	struct dlm_lkb *first = list_entry(head->next, struct dlm_lkb, lkb_statequeue);

	if (lkb->lkb_id == first->lkb_id)
		return 1;

	return 0;
}

/*
 * Return 1 if the locks' ranges overlap
 * If the lkb has no range then it is assumed to cover 0-ffffffff.ffffffff
 */

static inline int ranges_overlap(struct dlm_lkb *lkb1, struct dlm_lkb *lkb2)
{
	if (!lkb1->lkb_range || !lkb2->lkb_range)
		return 1;

	if (lkb1->lkb_range[RQ_RANGE_END] < lkb2->lkb_range[GR_RANGE_START] ||
	    lkb1->lkb_range[RQ_RANGE_START] > lkb2->lkb_range[GR_RANGE_END])
		return 0;

	return 1;
}

/*
 * Check if the given lkb conflicts with another lkb on the queue.
 */

static int queue_conflict(struct list_head *head, struct dlm_lkb *lkb)
{
	struct dlm_lkb *this;

	list_for_each_entry(this, head, lkb_statequeue) {
		if (this == lkb)
			continue;
		if (ranges_overlap(lkb, this) && !modes_compat(this, lkb))
			return TRUE;
	}
	return FALSE;
}

/*
 * "A conversion deadlock arises with a pair of lock requests in the converting
 * queue for one resource.  The granted mode of each lock blocks the requested
 * mode of the other lock."
 *
 * Part 2: if the granted mode of lkb is preventing the first lkb in the
 * convert queue from being granted, then demote lkb (set grmode to NL).
 * This second form requires that we check for conv-deadlk even when
 * now == 0 in _can_be_granted().
 *
 * Example:
 * Granted Queue: empty
 * Convert Queue: NL->EX (first lock)
 *                PR->EX (second lock)
 *
 * The first lock can't be granted because of the granted mode of the second
 * lock and the second lock can't be granted because it's not first in the
 * list.  We demote the granted mode of the second lock (the lkb passed to this
 * function).
 *
 * After the resolution, the "grant pending" function needs to go back and try
 * to grant locks on the convert queue again since the first lock can now be
 * granted.
 */

static int conversion_deadlock_detect(struct dlm_rsb *rsb, struct dlm_lkb *lkb)
{
	struct dlm_lkb *this, *first = NULL, *self = NULL;

	list_for_each_entry(this, &rsb->res_convertqueue, lkb_statequeue) {
		if (!first)
			first = this;
		if (this == lkb) {
			self = lkb;
			continue;
		}

		if (!ranges_overlap(lkb, this))
			continue;

		if (!modes_compat(this, lkb) && !modes_compat(lkb, this))
			return TRUE;
	}

	/* if lkb is on the convert queue and is preventing the first
	   from being granted, then there's deadlock and we demote lkb.
	   multiple converting locks may need to do this before the first
	   converting lock can be granted. */

	if (self && self != first) {
		if (!modes_compat(lkb, first) &&
		    !queue_conflict(&rsb->res_grantqueue, first))
			return TRUE;
	}

	return FALSE;
}

/*
 * Return 1 if the lock can be granted, 0 otherwise.
 * Also detect and resolve conversion deadlocks.
 *
 * lkb is the lock to be granted
 *
 * now is 1 if the function is being called in the context of the
 * immediate request, it is 0 if called later, after the lock has been
 * queued.
 *
 * References are from chapter 6 of "VAXcluster Principles" by Roy Davis
 */

static int _can_be_granted(struct dlm_rsb *r, struct dlm_lkb *lkb, int now)
{
	int8_t conv = (lkb->lkb_grmode != DLM_LOCK_IV);

	/*
	 * 6-10: Version 5.4 introduced an option to address the phenomenon of
	 * a new request for a NL mode lock being blocked.
	 *
	 * 6-11: If the optional EXPEDITE flag is used with the new NL mode
	 * request, then it would be granted.  In essence, the use of this flag
	 * tells the Lock Manager to expedite theis request by not considering
	 * what may be in the CONVERTING or WAITING queues...  As of this
	 * writing, the EXPEDITE flag can be used only with new requests for NL
	 * mode locks.  This flag is not valid for conversion requests.
	 *
	 * A shortcut.  Earlier checks return an error if EXPEDITE is used in a
	 * conversion or used with a non-NL requested mode.  We also know an
	 * EXPEDITE request is always granted immediately, so now must always
	 * be 1.  The full condition to grant an expedite request: (now &&
	 * !conv && lkb->rqmode == DLM_LOCK_NL && (flags & EXPEDITE)) can
	 * therefore be shortened to just checking the flag.
	 */

	if (lkb->lkb_lockqueue_flags & DLM_LKF_EXPEDITE)
		return TRUE;

	/*
	 * A shortcut. Without this, !queue_conflict(grantqueue, lkb) would be
	 * added to the remaining conditions.
	 */

	if (queue_conflict(&r->res_grantqueue, lkb))
		goto out;

	/*
	 * 6-3: By default, a conversion request is immediately granted if the
	 * requested mode is compatible with the modes of all other granted
	 * locks
	 */

	if (queue_conflict(&r->res_convertqueue, lkb))
		goto out;

	/*
	 * 6-5: But the default algorithm for deciding whether to grant or
	 * queue conversion requests does not by itself guarantee that such
	 * requests are serviced on a "first come first serve" basis.  This, in
	 * turn, can lead to a phenomenon known as "indefinate postponement".
	 *
	 * 6-7: This issue is dealt with by using the optional QUECVT flag with
	 * the system service employed to request a lock conversion.  This flag
	 * forces certain conversion requests to be queued, even if they are
	 * compatible with the granted modes of other locks on the same
	 * resource.  Thus, the use of this flag results in conversion requests
	 * being ordered on a "first come first servce" basis.
	 *
	 * DCT: This condition is all about new conversions being able to occur
	 * "in place" while the lock remains on the granted queue (assuming
	 * nothing else conflicts.)  IOW if QUECVT isn't set, a conversion
	 * doesn't _have_ to go onto the convert queue where it's processed in
	 * order.  The "now" variable is necessary to distinguish converts
	 * being received and processed for the first time now, because once a
	 * convert is moved to the conversion queue the condition below applies
	 * requiring fifo granting.
	 */

	if (now && conv && !(lkb->lkb_lockqueue_flags & DLM_LKF_QUECVT))
		return TRUE;

	/*
	 * When using range locks the NOORDER flag is set to avoid the standard
	 * vms rules on grant order.
	 */

	if (lkb->lkb_lockqueue_flags & DLM_LKF_NOORDER)
		return TRUE;

	/*
	 * 6-3: Once in that queue [CONVERTING], a conversion request cannot be
	 * granted until all other conversion requests ahead of it are granted
	 * and/or canceled.
	 */

	if (!now && conv && first_in_list(lkb, &r->res_convertqueue))
		return TRUE;

	/*
	 * 6-4: By default, a new request is immediately granted only if all
	 * three of the following conditions are satisfied when the request is
	 * issued:
	 * - The queue of ungranted conversion requests for the resource is
	 *   empty.
	 * - The queue of ungranted new requests for the resource is empty.
	 * - The mode of the new request is compatible with the most
	 *   restrictive mode of all granted locks on the resource.
	 */

	if (now && !conv && list_empty(&r->res_convertqueue) &&
	    list_empty(&r->res_waitqueue))
		return TRUE;

	/*
	 * 6-4: Once a lock request is in the queue of ungranted new requests,
	 * it cannot be granted until the queue of ungranted conversion
	 * requests is empty, all ungranted new requests ahead of it are
	 * granted and/or canceled, and it is compatible with the granted mode
	 * of the most restrictive lock granted on the resource.
	 */

	if (!now && !conv && list_empty(&r->res_convertqueue) &&
	    first_in_list(lkb, &r->res_waitqueue))
		return TRUE;

 out:
	/*
	 * The following, enabled by CONVDEADLK, departs from VMS.
	 */

	if (conv && (lkb->lkb_lockqueue_flags & DLM_LKF_CONVDEADLK) &&
	    conversion_deadlock_detect(r, lkb)) {
		lkb->lkb_grmode = DLM_LOCK_NL;
		lkb->lkb_flags |= GDLM_LKFLG_DEMOTED;
	}

	return FALSE;
}

static int can_be_granted(struct dlm_rsb *r, struct dlm_lkb *lkb, int now)
{
	uint32_t flags = lkb->lkb_lockqueue_flags;
	int rv;
	int8_t alt = 0, rqmode = lkb->lkb_rqmode;

	rv = _can_be_granted(r, lkb, now);
	if (rv)
		goto out;

	if (lkb->lkb_flags & GDLM_LKFLG_DEMOTED)
		goto out;

	if (rqmode != DLM_LOCK_PR && flags & DLM_LKF_ALTPR)
		alt = DLM_LOCK_PR;
	else if (rqmode != DLM_LOCK_CW && flags & DLM_LKF_ALTCW)
		alt = DLM_LOCK_CW;

	if (alt) {
		lkb->lkb_rqmode = alt;
		rv = _can_be_granted(r, lkb, now);
		if (rv)
			lkb->lkb_flags |= GDLM_LKFLG_ALTMODE;
		else
			lkb->lkb_rqmode = rqmode;
	}
 out:
	return rv;
}

int dlm_lock(void *lockspace,
	     uint32_t mode,
	     struct dlm_lksb *lksb,
	     uint32_t flags,
	     void *name,
	     unsigned int namelen,
	     uint32_t parent,
	     void (*ast) (void *astarg),
	     void *astarg,
	     void (*bast) (void *astarg, int mode),
	     struct dlm_range *range)
{
	struct dlm_ls *lspace;
	struct dlm_lkb *lkb = NULL, *parent_lkb = NULL;
	int ret = -EINVAL;

	lspace = find_lockspace_by_local_id(lockspace);
	if (!lspace)
		return ret;

	if (mode < 0 || mode > DLM_LOCK_EX)
		goto out;

	if (!(flags & DLM_LKF_CONVERT) && (namelen > DLM_RESNAME_MAXLEN))
		goto out;

	if (flags & DLM_LKF_CANCEL)
		goto out;

	if (flags & DLM_LKF_QUECVT && !(flags & DLM_LKF_CONVERT))
		goto out;

	if (flags & DLM_LKF_CONVDEADLK && !(flags & DLM_LKF_CONVERT))
		goto out;

	if (flags & DLM_LKF_CONVDEADLK && flags & DLM_LKF_NOQUEUE)
		goto out;

	if (flags & DLM_LKF_EXPEDITE && flags & DLM_LKF_CONVERT)
		goto out;

	if (flags & DLM_LKF_EXPEDITE && flags & DLM_LKF_QUECVT)
		goto out;

	if (flags & DLM_LKF_EXPEDITE && flags & DLM_LKF_NOQUEUE)
		goto out;

	if (flags & DLM_LKF_EXPEDITE && (mode != DLM_LOCK_NL))
		goto out;

	if (!ast || !lksb)
		goto out;

	if ((flags & DLM_LKF_VALBLK) && !lksb->sb_lvbptr)
		goto out;

	/*
	 * Take conversion path.
	 */

	if (flags & DLM_LKF_CONVERT) {
		ret = convert_lock(lspace, mode, lksb, flags, ast, astarg,
				   bast, range);
		goto out;
	}

#ifdef CONFIG_DLM_STATS
	dlm_stats.lockops++;
#endif
	/*
	 * Take new lock path.
	 */

	if (parent) {
		down_read(&lspace->ls_unlock_sem);

		parent_lkb = find_lock_by_id(lspace, parent);

		if (!parent_lkb ||
		    parent_lkb->lkb_flags & GDLM_LKFLG_DELETED ||
		    parent_lkb->lkb_flags & GDLM_LKFLG_MSTCPY ||
		    parent_lkb->lkb_status != GDLM_LKSTS_GRANTED) {
			up_read(&lspace->ls_unlock_sem);
			goto out;
		}

		atomic_inc(&parent_lkb->lkb_childcnt);
		up_read(&lspace->ls_unlock_sem);
	}

	down_read(&lspace->ls_in_recovery);

	ret = -ENOMEM;

	lkb = create_lkb(lspace);
	if (!lkb)
		goto fail_dec;
	lkb->lkb_astaddr = ast;
	lkb->lkb_astparam = (long) astarg;
	lkb->lkb_bastaddr = bast;
	lkb->lkb_rqmode = mode;
	lkb->lkb_grmode = DLM_LOCK_IV;
	lkb->lkb_nodeid = -1;
	lkb->lkb_lksb = lksb;
	lkb->lkb_parent = parent_lkb;
	lkb->lkb_lockqueue_flags = flags;
	lkb->lkb_lvbptr = lksb->sb_lvbptr;

	if (!in_interrupt() && current)
		lkb->lkb_ownpid = (int) current->pid;
	else
		lkb->lkb_ownpid = 0;

	if (range) {
		if (range->ra_start > range->ra_end) {
			ret = -EINVAL;
			goto fail_free;
		}

		if (lkb_set_range(lspace, lkb, range->ra_start, range->ra_end))
			goto fail_free;
	}

	/* Convert relevant flags to internal numbers */
	if (flags & DLM_LKF_VALBLK)
		lkb->lkb_flags |= GDLM_LKFLG_VALBLK;
	if (flags & DLM_LKF_PERSISTENT)
		lkb->lkb_flags |= GDLM_LKFLG_PERSISTENT;
	if (flags & DLM_LKF_NODLCKWT)
		lkb->lkb_flags |= GDLM_LKFLG_NODLCKWT;

	lksb->sb_lkid = lkb->lkb_id;

	ret = dlm_lock_stage1(lspace, lkb, flags, name, namelen);
	if (ret)
		goto fail_free;

	up_read(&lspace->ls_in_recovery);

	wake_astd();

	put_lockspace(lspace);
	return 0;

      fail_free:
	release_lkb(lspace, lkb);
	goto fail_unlock;

      fail_dec:
	if (parent_lkb)
		atomic_dec(&parent_lkb->lkb_childcnt);

      fail_unlock:
	up_read(&lspace->ls_in_recovery);

      out:
	put_lockspace(lspace);
	return ret;
}

int dlm_lock_stage1(struct dlm_ls *ls, struct dlm_lkb *lkb, uint32_t flags,
		    char *name, int namelen)
{
	struct dlm_rsb *rsb, *parent_rsb = NULL;
	struct dlm_lkb *parent_lkb = lkb->lkb_parent;
	uint32_t nodeid;
	int error, dir_error = 0;

	if (parent_lkb)
		parent_rsb = parent_lkb->lkb_resource;

	error = find_rsb(ls, parent_rsb, name, namelen, CREATE, &rsb);
	if (error)
		return error;
	lkb->lkb_resource = rsb;
	down_write(&rsb->res_lock);

	log_debug1(ls, "(%d) rq %u %x \"%s\"", lkb->lkb_ownpid, lkb->lkb_rqmode,
		   lkb->lkb_id, rsb->res_name);
	/*
	 * Next stage, do we need to find the master or can
	 * we get on with the real locking work ?
	 */

 retry:
	if (rsb->res_nodeid == -1) {
		if (get_directory_nodeid(rsb) != our_nodeid()) {
			up_write(&rsb->res_lock);
			remote_stage(lkb, GDLM_LQSTATE_WAIT_RSB);
			return 0;
		}

		error = dlm_dir_lookup(ls, our_nodeid(), rsb->res_name,
				       rsb->res_length, &nodeid);
		if (error) {
			DLM_ASSERT(error == -EEXIST,);
			msleep(500);
			dir_error = error;
			goto retry;
		}

		if (nodeid == our_nodeid()) {
			set_bit(RESFL_MASTER, &rsb->res_flags);
			rsb->res_nodeid = 0;
		} else {
			clear_bit(RESFL_MASTER, &rsb->res_flags);
			rsb->res_nodeid = nodeid;
		}

		if (dir_error) {
			log_debug(ls, "dir lookup retry %x %u", lkb->lkb_id,
				  nodeid);
		}
	}

	lkb->lkb_nodeid = rsb->res_nodeid;
	up_write(&rsb->res_lock);

	error = dlm_lock_stage2(ls, lkb, rsb, flags);

	return error;
}

/*
 * Locking routine called after we have an RSB, either a copy of a remote one
 * or a local one, or perhaps a shiny new one all of our very own
 */

int dlm_lock_stage2(struct dlm_ls *ls, struct dlm_lkb *lkb, struct dlm_rsb *rsb,
		    uint32_t flags)
{
	int error = 0;

	DLM_ASSERT(rsb->res_nodeid != -1, print_lkb(lkb); print_rsb(rsb););

	if (rsb->res_nodeid) {
		res_lkb_enqueue(rsb, lkb, GDLM_LKSTS_WAITING);
		error = remote_stage(lkb, GDLM_LQSTATE_WAIT_CONDGRANT);
	} else {
		dlm_lock_stage3(lkb);
	}

	return error;
}

/*
 * Called on an RSB's master node to do stage2 locking for a remote lock
 * request.  Returns a proper lkb with rsb ready for lock processing.
 * This is analagous to sections of dlm_lock() and dlm_lock_stage1().
 */

struct dlm_lkb *remote_stage2(int remote_nodeid, struct dlm_ls *ls,
			      struct dlm_request *freq)
{
	struct dlm_rsb *rsb = NULL, *parent_rsb = NULL;
	struct dlm_lkb *lkb = NULL, *parent_lkb = NULL;
	int error, namelen;

	if (freq->rr_remparid) {
		parent_lkb = find_lock_by_id(ls, freq->rr_remparid);
		if (!parent_lkb)
			goto fail;

		atomic_inc(&parent_lkb->lkb_childcnt);
		parent_rsb = parent_lkb->lkb_resource;
	}

	/*
	 * A new MSTCPY lkb.  Initialize lkb fields including the real lkid and
	 * node actually holding the (non-MSTCPY) lkb.  AST address are just
	 * flags in the master copy.
	 */

	lkb = create_lkb(ls);
	if (!lkb)
		goto fail_dec;
	lkb->lkb_grmode = DLM_LOCK_IV;
	lkb->lkb_rqmode = freq->rr_rqmode;
	lkb->lkb_parent = parent_lkb;
	lkb->lkb_astaddr = (void *) (long) (freq->rr_asts & AST_COMP);
	lkb->lkb_bastaddr = (void *) (long) (freq->rr_asts & AST_BAST);
	lkb->lkb_nodeid = remote_nodeid;
	lkb->lkb_remid = freq->rr_header.rh_lkid;
	lkb->lkb_flags = GDLM_LKFLG_MSTCPY;
	lkb->lkb_lockqueue_flags = freq->rr_flags;

	if (lkb->lkb_lockqueue_flags & DLM_LKF_VALBLK) {
		lkb->lkb_flags |= GDLM_LKFLG_VALBLK;
		allocate_and_copy_lvb(ls, &lkb->lkb_lvbptr, freq->rr_lvb);
		if (!lkb->lkb_lvbptr)
			goto fail_free;
	}

	if (lkb->lkb_lockqueue_flags & GDLM_LKFLG_RANGE) {
		error = lkb_set_range(ls, lkb, freq->rr_range_start,
				      freq->rr_range_end);
		if (error)
			goto fail_free;
	}

	/*
	 * Get the RSB which this lock is for.  Create a new RSB if this is a
	 * new lock on a new resource.  We must be the master of any new rsb.
	 */

	namelen = freq->rr_header.rh_length - sizeof(*freq) + 1;

	error = find_rsb(ls, parent_rsb, freq->rr_name, namelen, MASTER, &rsb);
	if (error)
		goto fail_free;

	if (!rsb) {
		log_debug(ls, "send einval to %u", remote_nodeid);
		/* print_name(freq->rr_name, namelen); */
		lkb->lkb_retstatus = -EINVAL;
		goto out;
	}

	lkb->lkb_resource = rsb;

	log_debug1(ls, "(%d) rq %u from %u %x \"%s\"",
		   lkb->lkb_ownpid, lkb->lkb_rqmode, remote_nodeid,
		   lkb->lkb_id, rsb->res_name);

      out:
	return lkb;

      fail_free:
	/* release_lkb handles parent */
	release_lkb(ls, lkb);
	parent_lkb = NULL;

      fail_dec:
	if (parent_lkb)
		atomic_dec(&parent_lkb->lkb_childcnt);
      fail:
	return NULL;
}

/*
 * The final bit of lock request processing on the master node.  Here the lock
 * is granted and the completion ast is queued, or the lock is put on the
 * waitqueue and blocking asts are sent.
 */

void dlm_lock_stage3(struct dlm_lkb *lkb)
{
	struct dlm_rsb *rsb = lkb->lkb_resource;

	/*
	 * This is a locally mastered lock on a resource that already exists,
	 * see if it can be  granted or if it must wait.  When this function is
	 * called for a remote lock request (process_cluster_request,
	 * REMCMD_LOCKREQUEST), the result from grant_lock is returned to the
	 * requesting node at the end of process_cluster_request, not at the
	 * end of grant_lock.
	 */

	down_write(&rsb->res_lock);

	if (can_be_granted(rsb, lkb, TRUE)) {
		grant_lock(lkb, 0);
		goto out;
	}

	/*
	 * This request is not a conversion, so the lkb didn't exist other than
	 * for this request and should be freed after EAGAIN is returned in the
	 * ast.
	 */

	if (lkb->lkb_lockqueue_flags & DLM_LKF_NOQUEUE) {
		lkb->lkb_retstatus = -EAGAIN;
		if (lkb->lkb_lockqueue_flags & DLM_LKF_NOQUEUEBAST)
			send_blocking_asts_all(rsb, lkb);
		/*
		 * up the res_lock before queueing ast, since the AST_DEL will
		 * cause the rsb to be released and that can happen anytime.
		 */
		up_write(&rsb->res_lock);
		queue_ast(lkb, AST_COMP | AST_DEL, 0);
		return;
	}

	/*
	 * The requested lkb must wait.  Because the rsb of the requested lkb
	 * is mastered here, send blocking asts for the lkb's blocking the
	 * request.
	 */

	log_debug2("w %x %d %x %d,%d %d %s", lkb->lkb_id, lkb->lkb_nodeid,
		   lkb->lkb_remid, lkb->lkb_grmode, lkb->lkb_rqmode,
		   lkb->lkb_status, rsb->res_name);

	lkb->lkb_retstatus = 0;
	lkb_enqueue(rsb, lkb, GDLM_LKSTS_WAITING);

	send_blocking_asts(rsb, lkb);

      out:
	up_write(&rsb->res_lock);
}

int dlm_unlock(void *lockspace,
	       uint32_t lkid,
	       uint32_t flags,
	       struct dlm_lksb *lksb,
	       void *astarg)
{
	struct dlm_ls *ls = find_lockspace_by_local_id(lockspace);
	struct dlm_lkb *lkb;
	struct dlm_rsb *rsb;
	int ret = -EINVAL;

	if (!ls) {
		log_print("dlm_unlock: lkid %x lockspace not found", lkid);
		return ret;
	}

	lkb = find_lock_by_id(ls, lkid);
	if (!lkb) {
		log_debug(ls, "unlock %x no id", lkid);
		goto out;
	}

	/* Can't dequeue a master copy (a remote node's mastered lock) */
	if (lkb->lkb_flags & GDLM_LKFLG_MSTCPY) {
		log_debug(ls, "(%d) unlock %x lkb_flags %x",
			  lkb->lkb_ownpid, lkid, lkb->lkb_flags);
		goto out;
	}

	/* Already waiting for a remote lock operation */
	if (lkb->lkb_lockqueue_state) {
		log_debug(ls, "(%d) unlock %x lq%d",
			  lkb->lkb_ownpid, lkid, lkb->lkb_lockqueue_state);
		ret = -EBUSY;
		goto out;
	}

#ifdef CONFIG_DLM_STATS
	dlm_stats.unlockops++;
#endif
	/* Can only cancel WAITING or CONVERTing locks.
	 * This is just a quick check - it is also checked in unlock_stage2()
	 * (which may be on the master) under the semaphore.
	 */
	if ((flags & DLM_LKF_CANCEL) &&
	    (lkb->lkb_status == GDLM_LKSTS_GRANTED)) {
		log_debug(ls, "(%d) unlock %x %x %d",
			  lkb->lkb_ownpid, lkid, flags, lkb->lkb_status);
		goto out;
	}

	/* "Normal" unlocks must operate on a granted lock */
	if (!(flags & DLM_LKF_CANCEL) &&
	    (lkb->lkb_status != GDLM_LKSTS_GRANTED)) {
		log_debug(ls, "(%d) unlock %x %x %d",
			  lkb->lkb_ownpid, lkid, flags, lkb->lkb_status);
		goto out;
	}

	if (lkb->lkb_flags & GDLM_LKFLG_DELETED) {
		log_debug(ls, "(%d) unlock deleted %x %x %d",
			  lkb->lkb_ownpid, lkid, flags, lkb->lkb_status);
		goto out;
	}

	down_write(&ls->ls_unlock_sem);
	/* Can't dequeue a lock with sublocks */
	if (atomic_read(&lkb->lkb_childcnt)) {
		up_write(&ls->ls_unlock_sem);
		ret = -ENOTEMPTY;
		goto out;
	}
	/* Mark it as deleted so we can't use it as a parent in dlm_lock() */
	if (!(flags & DLM_LKF_CANCEL))
		lkb->lkb_flags |= GDLM_LKFLG_DELETED;
	up_write(&ls->ls_unlock_sem);

	down_read(&ls->ls_in_recovery);
	rsb = find_rsb_to_unlock(ls, lkb);

	log_debug1(ls, "(%d) un %x %x %d %d \"%s\"",
		   lkb->lkb_ownpid,
		   lkb->lkb_id,
		   lkb->lkb_flags,
		   lkb->lkb_nodeid,
		   rsb->res_nodeid,
		   rsb->res_name);

	/* Save any new params */
	if (lksb)
		lkb->lkb_lksb = lksb;
	lkb->lkb_astparam = (long) astarg;
	lkb->lkb_lockqueue_flags = flags;

	if (lkb->lkb_nodeid)
		ret = remote_stage(lkb, GDLM_LQSTATE_WAIT_UNLOCK);
	else {
		dlm_unlock_stage2(lkb, rsb, flags);
		ret = 0;
	}
	up_read(&ls->ls_in_recovery);

	wake_astd();

      out:
	put_lockspace(ls);
	return ret;
}

int dlm_unlock_stage2(struct dlm_lkb *lkb, struct dlm_rsb *rsb, uint32_t flags)
{
	int remote = lkb->lkb_flags & GDLM_LKFLG_MSTCPY;
	int old_status, rv = 0;

	down_write(&rsb->res_lock);

	if ((flags & DLM_LKF_CANCEL) && lkb->lkb_status == GDLM_LKSTS_GRANTED) {
		log_print("cancel granted %x", lkb->lkb_id);
	        rv = lkb->lkb_retstatus = -EINVAL;
		up_write(&rsb->res_lock);
		if (!remote)
			queue_ast(lkb, AST_COMP, 0);
	        goto out;
	}

	log_debug2("u %x %d %x %d,%d %d %s", lkb->lkb_id, lkb->lkb_nodeid,
		   lkb->lkb_remid, lkb->lkb_grmode, lkb->lkb_rqmode,
		   lkb->lkb_status, rsb->res_name);

	old_status = lkb_dequeue(lkb);

	if (flags & DLM_LKF_CANCEL) {
		if (old_status == GDLM_LKSTS_CONVERT) {
			/* VMS semantics say we should send blocking ASTs
			   again here */
			send_blocking_asts(rsb, lkb);

			/* Remove from deadlock detection */
			if (lkb->lkb_duetime)
				remove_from_deadlockqueue(lkb);

			lkb_enqueue(rsb, lkb, GDLM_LKSTS_GRANTED);
			lkb->lkb_rqmode = DLM_LOCK_IV;

			/* Was it blocking any other locks? */
			if (first_in_list(lkb, &rsb->res_convertqueue))
				grant_pending_locks(rsb);

			rv = lkb->lkb_retstatus = -DLM_ECANCEL;
			up_write(&rsb->res_lock);
			if (!remote)
				queue_ast(lkb, AST_COMP, 0);
		} else if (old_status == GDLM_LKSTS_WAITING) {
			lkb->lkb_rqmode = DLM_LOCK_IV;
			rv = lkb->lkb_retstatus = -DLM_ECANCEL;
			up_write(&rsb->res_lock);
			if (!remote)
				queue_ast(lkb, AST_COMP | AST_DEL, 0);
			else {
				/* frees the lkb */
				release_lkb(rsb->res_ls, lkb);
				release_rsb(rsb);
			}
		} else
			log_print("unlock cancel status %d", old_status);
	} else {
		if (old_status != GDLM_LKSTS_GRANTED) {
			log_print("unlock ungranted %d", old_status);
			rv = lkb->lkb_retstatus = -EINVAL;
			up_write(&rsb->res_lock);
			if (!remote)
				queue_ast(lkb, AST_COMP, 0);
		} else {
			if (lkb->lkb_grmode >= DLM_LOCK_PW) {
				if (!rsb->res_lvbptr)
					rsb->res_lvbptr = allocate_lvb(rsb->res_ls);
				if ((flags & DLM_LKF_VALBLK) && lkb->lkb_lvbptr) {
					memcpy(rsb->res_lvbptr, lkb->lkb_lvbptr,
						DLM_LVB_LEN);
					rsb->res_lvbseq++;
					clear_bit(RESFL_VALNOTVALID, &rsb->res_flags);
				}
				if (flags & DLM_LKF_IVVALBLK)
					set_bit(RESFL_VALNOTVALID, &rsb->res_flags);
			}
			grant_pending_locks(rsb);
			rv = lkb->lkb_retstatus = -DLM_EUNLOCK;
			up_write(&rsb->res_lock);
			if (!remote)
				queue_ast(lkb, AST_COMP | AST_DEL, 0);
			else {
				/* frees the lkb */
				release_lkb(rsb->res_ls, lkb);
				release_rsb(rsb);
			}
		}
	}

 out:
	if (!remote)
		wake_astd();
	return rv;
}

/*
 * Lock conversion
 */

static int convert_lock(struct dlm_ls *ls, int mode, struct dlm_lksb *lksb,
			uint32_t flags, void *ast, void *astarg, void *bast,
			struct dlm_range *range)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *rsb;
	int ret = -EINVAL;

	lkb = find_lock_by_id(ls, lksb->sb_lkid);
	if (!lkb) {
		goto out;
	}

	if (lkb->lkb_status != GDLM_LKSTS_GRANTED) {
		ret = -EBUSY;
		goto out;
	}

	if (lkb->lkb_flags & GDLM_LKFLG_MSTCPY) {
		goto out;
	}

	if ((flags & DLM_LKF_QUECVT) &&
	    !__quecvt_compat_matrix[lkb->lkb_grmode + 1][mode + 1]) {
		goto out;
	}

	if (!lksb->sb_lvbptr && (flags & DLM_LKF_VALBLK)) {
	        goto out;
	}

#ifdef CONFIG_DLM_STATS
	dlm_stats.convertops++;
#endif
	/* Set up the ranges as appropriate */
	if (range) {
		if (range->ra_start > range->ra_end)
			goto out;

		if (lkb_set_range(ls, lkb, range->ra_start, range->ra_end)) {
			ret = -ENOMEM;
			goto out;
		}
	}

	rsb = lkb->lkb_resource;
	down_read(&ls->ls_in_recovery);

	log_debug1(ls, "(%d) cv %u %x \"%s\"", lkb->lkb_ownpid, mode,
		   lkb->lkb_id, rsb->res_name);

	lkb->lkb_flags &= ~(GDLM_LKFLG_VALBLK | GDLM_LKFLG_DEMOTED |
			    GDLM_LKFLG_RETURNLVB | GDLM_LKFLG_VALNOTVALID |
			    GDLM_LKFLG_ALTMODE);

	if (flags & DLM_LKF_NODLCKWT)
		lkb->lkb_flags |= GDLM_LKFLG_NODLCKWT;
	if (flags & DLM_LKF_VALBLK)
		lkb->lkb_flags |= GDLM_LKFLG_VALBLK;
	lkb->lkb_astaddr = ast;
	lkb->lkb_astparam = (long) astarg;
	lkb->lkb_bastaddr = bast;
	lkb->lkb_rqmode = mode;
	lkb->lkb_lockqueue_flags = flags;
	lkb->lkb_lvbptr = lksb->sb_lvbptr;

	if (rsb->res_nodeid) {
		res_lkb_swqueue(rsb, lkb, GDLM_LKSTS_CONVERT);
		ret = remote_stage(lkb, GDLM_LQSTATE_WAIT_CONVERT);
	} else {
		ret = dlm_convert_stage2(lkb, FALSE);
	}

	up_read(&ls->ls_in_recovery);

	wake_astd();

      out:
	return ret;
}

/*
 * For local conversion requests on locally mastered locks this is called
 * directly from dlm_lock/convert_lock.  This function is also called for
 * remote conversion requests of MSTCPY locks (from process_cluster_request).
 */

int dlm_convert_stage2(struct dlm_lkb *lkb, int do_ast)
{
	struct dlm_rsb *rsb = lkb->lkb_resource;
	int ret = 0;

	down_write(&rsb->res_lock);

	if (can_be_granted(rsb, lkb, TRUE)) {
		grant_lock(lkb, 0);
		grant_pending_locks(rsb);
		goto out;
	}

	if (lkb->lkb_lockqueue_flags & DLM_LKF_NOQUEUE) {
		ret = lkb->lkb_retstatus = -EAGAIN;
		if (do_ast)
			queue_ast(lkb, AST_COMP, 0);
		if (lkb->lkb_lockqueue_flags & DLM_LKF_NOQUEUEBAST)
			send_blocking_asts_all(rsb, lkb);
		goto out;
	}

	log_debug2("c %x %d %x %d,%d %d %s", lkb->lkb_id, lkb->lkb_nodeid,
		   lkb->lkb_remid, lkb->lkb_grmode, lkb->lkb_rqmode,
		   lkb->lkb_status, rsb->res_name);

	lkb->lkb_retstatus = 0;
	lkb_swqueue(rsb, lkb, GDLM_LKSTS_CONVERT);

	/*
	 * The granted mode may have been reduced to NL by conversion deadlock
	 * avoidance in can_be_granted().  If so, try to grant other locks.
	 */

	if (lkb->lkb_flags & GDLM_LKFLG_DEMOTED)
		grant_pending_locks(rsb);

	send_blocking_asts(rsb, lkb);

	if (!(lkb->lkb_flags & GDLM_LKFLG_NODLCKWT))
	        add_to_deadlockqueue(lkb);

      out:
	up_write(&rsb->res_lock);
	return ret;
}

/*
 * Remove lkb from any queue it's on, add it to the granted queue, and queue a
 * completion ast.  rsb res_lock must be held in write when this is called.
 */

static void grant_lock(struct dlm_lkb *lkb, int send_remote)
{
	struct dlm_rsb *rsb = lkb->lkb_resource;

	if (lkb->lkb_duetime)
		remove_from_deadlockqueue(lkb);

	if (lkb->lkb_flags & GDLM_LKFLG_VALBLK) {
		int b;
		DLM_ASSERT(lkb->lkb_lvbptr,);

		if (!rsb->res_lvbptr)
			rsb->res_lvbptr = allocate_lvb(rsb->res_ls);

		lkb->lkb_flags &= ~GDLM_LKFLG_RETURNLVB;

		b = __lvb_operations[lkb->lkb_grmode + 1][lkb->lkb_rqmode + 1];
		if (b == 1) {
			memcpy(lkb->lkb_lvbptr, rsb->res_lvbptr, DLM_LVB_LEN);
			lkb->lkb_flags |= GDLM_LKFLG_RETURNLVB;
			lkb->lkb_lvbseq = rsb->res_lvbseq;
		}
		if (b == 0) {
			memcpy(rsb->res_lvbptr, lkb->lkb_lvbptr, DLM_LVB_LEN);
			clear_bit(RESFL_VALNOTVALID, &rsb->res_flags);
			rsb->res_lvbseq++;
			lkb->lkb_lvbseq = rsb->res_lvbseq;
		}
	}

	if (test_bit(RESFL_VALNOTVALID, &rsb->res_flags))
		lkb->lkb_flags |= GDLM_LKFLG_VALNOTVALID;

	if (lkb->lkb_range) {
		lkb->lkb_range[GR_RANGE_START] = lkb->lkb_range[RQ_RANGE_START];
		lkb->lkb_range[GR_RANGE_END] = lkb->lkb_range[RQ_RANGE_END];
	}

	log_debug2("g %x %d %x %d,%d %d %x %s", lkb->lkb_id, lkb->lkb_nodeid,
	           lkb->lkb_remid, lkb->lkb_grmode, lkb->lkb_rqmode,
		   lkb->lkb_status, lkb->lkb_flags, rsb->res_name);

	if (lkb->lkb_grmode != lkb->lkb_rqmode) {
		lkb->lkb_grmode = lkb->lkb_rqmode;
		lkb_swqueue(rsb, lkb, GDLM_LKSTS_GRANTED);
	}
	lkb->lkb_rqmode = DLM_LOCK_IV;
	lkb->lkb_highbast = 0;
	lkb->lkb_retstatus = 0;
	queue_ast(lkb, AST_COMP, 0);

	/*
	 * A remote conversion request has been granted, either immediately
	 * upon being requested or after waiting a bit.  In the former case,
	 * reply_and_grant() is called.  In the later case send_remote is 1 and
	 * remote_grant() is called.
	 *
	 * The "send_remote" flag is set only for locks which are granted "out
	 * of band" - ie by another lock being converted or unlocked.
	 *
	 * The second case occurs when this lkb is granted right away as part
	 * of processing the initial request.  In that case, we send a single
	 * message in reply_and_grant which combines the request reply with the
	 * grant message.
	 */

	if ((lkb->lkb_flags & GDLM_LKFLG_MSTCPY) && lkb->lkb_nodeid) {
		if (send_remote)
			remote_grant(lkb);
		else if (lkb->lkb_request)
			reply_and_grant(lkb);
	}

}

static void send_bast_queue(struct list_head *head, struct dlm_lkb *lkb)
{
	struct dlm_lkb *gr;

	list_for_each_entry(gr, head, lkb_statequeue) {
		if (gr->lkb_bastaddr &&
		    gr->lkb_highbast < lkb->lkb_rqmode &&
		    ranges_overlap(lkb, gr) && !modes_compat(gr, lkb)) {
			queue_ast(gr, AST_BAST, lkb->lkb_rqmode);
			gr->lkb_highbast = lkb->lkb_rqmode;
		}
	}
}

/*
 * Notify granted locks if they are blocking a newly forced-to-wait lock.
 */

static void send_blocking_asts(struct dlm_rsb *rsb, struct dlm_lkb *lkb)
{
	send_bast_queue(&rsb->res_grantqueue, lkb);
	/* check if the following improves performance */
	/* send_bast_queue(&rsb->res_convertqueue, lkb); */
}

static void send_blocking_asts_all(struct dlm_rsb *rsb, struct dlm_lkb *lkb)
{
	send_bast_queue(&rsb->res_grantqueue, lkb);
	send_bast_queue(&rsb->res_convertqueue, lkb);
}

/*
 * When we go through the convert queue trying to grant locks, we may grant or
 * demote some lkb's later in the list that would allow lkb's earlier in the
 * list to be granted when they weren't before.  When this happens we need to
 * go through the list again.
 */

static int grant_pending_convert(struct dlm_rsb *r, int high)
{
	struct dlm_lkb *lkb, *s;
	int hi, demoted, quit, grant_restart, demote_restart;

	quit = 0;
 restart:
	grant_restart = 0;
	demote_restart = 0;
	hi = DLM_LOCK_IV;

	list_for_each_entry_safe(lkb, s, &r->res_convertqueue, lkb_statequeue) {
		demoted = lkb->lkb_flags & GDLM_LKFLG_DEMOTED;
		if (can_be_granted(r, lkb, FALSE)) {
			grant_lock(lkb, 1);
			grant_restart = 1;
		} else {
			hi = MAX(lkb->lkb_rqmode, hi);
			if (!demoted && lkb->lkb_flags & GDLM_LKFLG_DEMOTED)
				demote_restart = 1;
		}
	}

	if (grant_restart)
		goto restart;
	if (demote_restart && !quit) {
		quit = 1;
		goto restart;
	}

	return MAX(high, hi);
}

static int grant_pending_wait(struct dlm_rsb *r, int high)
{
	struct dlm_lkb *lkb, *s;

	list_for_each_entry_safe(lkb, s, &r->res_waitqueue, lkb_statequeue) {
		if (lkb->lkb_lockqueue_state)
			continue;

		if (can_be_granted(r, lkb, FALSE))
			grant_lock(lkb, 1);
		else
			high = MAX(lkb->lkb_rqmode, high);
	}

	return high;
}

/*
 * Called when a lock has been dequeued. Look for any locks to grant that are
 * waiting for conversion or waiting to be granted.
 * The rsb res_lock must be held in write when this function is called.
 */

int grant_pending_locks(struct dlm_rsb *r)
{
	struct dlm_lkb *lkb, *s;
	int high = DLM_LOCK_IV;

	high = grant_pending_convert(r, high);
	high = grant_pending_wait(r, high);

	if (high == DLM_LOCK_IV)
		return 0;

	/*
	 * If there are locks left on the wait/convert queue then send blocking
	 * ASTs to granted locks that are blocking.  FIXME: This might generate
	 * some spurious blocking ASTs for range locks.
	 */

	list_for_each_entry_safe(lkb, s, &r->res_grantqueue, lkb_statequeue) {
		if (lkb->lkb_bastaddr && (lkb->lkb_highbast < high) &&
		    !__dlm_compat_matrix[lkb->lkb_grmode+1][high+1]) {
			queue_ast(lkb, AST_BAST, high);
			lkb->lkb_highbast = high;
		}
	}

	return 0;
}

/*
 * Called to cancel a locking operation that failed due to some internal
 * reason.
 *
 * Waiting locks will be removed, converting locks will be reverted to their
 * granted status, unlocks will be left where they are.
 *
 * A completion AST will be delivered to the caller.
 */

int cancel_lockop(struct dlm_lkb *lkb, int status)
{
	int state = lkb->lkb_lockqueue_state;
	uint16_t astflags = AST_COMP;

	lkb->lkb_lockqueue_state = 0;

	switch (state) {
	case GDLM_LQSTATE_WAIT_RSB:
		astflags |= AST_DEL;
		break;

	case GDLM_LQSTATE_WAIT_CONDGRANT:
		res_lkb_dequeue(lkb);
		astflags |= AST_DEL;
		break;

	case GDLM_LQSTATE_WAIT_CONVERT:
		res_lkb_swqueue(lkb->lkb_resource, lkb, GDLM_LKSTS_GRANTED);

		/* Remove from deadlock detection */
		if (lkb->lkb_duetime)
			remove_from_deadlockqueue(lkb);
		break;

	case GDLM_LQSTATE_WAIT_UNLOCK:
		/* We can leave this. I think.... */
		break;
	}

	lkb->lkb_retstatus = status;
	queue_ast(lkb, astflags, 0);

	return 0;
}

/*
 * Check for conversion deadlock. If a deadlock was found
 * return lkb to kill, else return NULL
 */

struct dlm_lkb *conversion_deadlock_check(struct dlm_lkb *lkb)
{
	struct dlm_rsb *rsb = lkb->lkb_resource;
	struct list_head *entry;

	DLM_ASSERT(lkb->lkb_status == GDLM_LKSTS_CONVERT,);

	/* Work our way up to the head of the queue looking for locks that
	 * conflict with us */

	down_read(&rsb->res_lock);

	entry = lkb->lkb_statequeue.prev;
	while (entry != &rsb->res_convertqueue) {
		struct dlm_lkb *lkb2 = list_entry(entry, struct dlm_lkb, lkb_statequeue);

		if (ranges_overlap(lkb, lkb2) && !modes_compat(lkb2, lkb)) {
			up_read(&rsb->res_lock);
			return lkb;
		}
		entry = entry->prev;
	}
	up_read(&rsb->res_lock);

	return 0;
}

/*
 * Conversion operation was cancelled by us (not the user).
 * ret contains the return code to pass onto the user
 */

void cancel_conversion(struct dlm_lkb *lkb, int ret)
{
	struct dlm_rsb *rsb = lkb->lkb_resource;

	/* Stick it back on the granted queue */
	res_lkb_swqueue(rsb, lkb, GDLM_LKSTS_GRANTED);
	lkb->lkb_rqmode = lkb->lkb_grmode;

	remove_from_deadlockqueue(lkb);

	lkb->lkb_retstatus = ret;
	queue_ast(lkb, AST_COMP, 0);
	wake_astd();
}

/*
 * As new master of the rsb for this lkb, we need to handle these requests
 * removed from the lockqueue and originating from local processes:
 * GDLM_LQSTATE_WAIT_RSB, GDLM_LQSTATE_WAIT_CONDGRANT,
 * GDLM_LQSTATE_WAIT_UNLOCK, GDLM_LQSTATE_WAIT_CONVERT.
 */

void process_remastered_lkb(struct dlm_ls *ls, struct dlm_lkb *lkb, int state)
{
	struct dlm_rsb *rsb;

	switch (state) {
	case GDLM_LQSTATE_WAIT_RSB:
		dlm_lock_stage1(lkb->lkb_resource->res_ls, lkb,
				lkb->lkb_lockqueue_flags,
				lkb->lkb_resource->res_name,
				lkb->lkb_resource->res_length);
		break;

	case GDLM_LQSTATE_WAIT_CONDGRANT:
		res_lkb_dequeue(lkb);
		dlm_lock_stage3(lkb);
		break;

	case GDLM_LQSTATE_WAIT_UNLOCK:
		rsb = find_rsb_to_unlock(ls, lkb);
		dlm_unlock_stage2(lkb, rsb, lkb->lkb_lockqueue_flags);
		break;

	case GDLM_LQSTATE_WAIT_CONVERT:
		/* The lkb is on the local convert queue while waiting for
		   the remote conversion.  dlm_convert_stage2() assumes
		   the lkb is still on the grant queue. */
		res_lkb_swqueue(lkb->lkb_resource, lkb, GDLM_LKSTS_GRANTED);
		dlm_convert_stage2(lkb, TRUE);
		break;

	default:
		DLM_ASSERT(0,);
	}
}

static void dump_queue(struct list_head *head, char *qname)
{
	struct dlm_lkb *lkb;

	list_for_each_entry(lkb, head, lkb_statequeue) {
		printk("%s %08x gr %d rq %d flg %x sts %u node %u remid %x "
		       "lq %d,%x\n",
		       qname,
		       lkb->lkb_id,
		       lkb->lkb_grmode,
		       lkb->lkb_rqmode,
		       lkb->lkb_flags,
		       lkb->lkb_status,
		       lkb->lkb_nodeid,
		       lkb->lkb_remid,
		       lkb->lkb_lockqueue_state,
		       lkb->lkb_lockqueue_flags);
	}
}

static void dump_rsb(struct dlm_rsb *rsb)
{
	printk("name \"%s\" flags %lx nodeid %d ref %u\n",
	       rsb->res_name, rsb->res_flags, rsb->res_nodeid,
	       atomic_read(&rsb->res_ref));

	if (!list_empty(&rsb->res_grantqueue))
		dump_queue(&rsb->res_grantqueue, "G");

	if (!list_empty(&rsb->res_convertqueue))
		dump_queue(&rsb->res_convertqueue, "C");

	if (!list_empty(&rsb->res_waitqueue))
		dump_queue(&rsb->res_waitqueue, "W");
}

/* This is only called from DLM_ASSERT */
void dlm_locks_dump(void)
{
	struct dlm_ls *ls;
	struct dlm_rsb *rsb;
	struct list_head *head;
	int i;

	lowcomms_stop_accept();

	list_for_each_entry(ls, &lslist, ls_list) {
		for (i = 0; i < ls->ls_rsbtbl_size; i++) {
			head = &ls->ls_rsbtbl[i].list;
			list_for_each_entry(rsb, head, res_hashchain)
				dump_rsb(rsb);
		}
	}
}

