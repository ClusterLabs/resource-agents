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

static void grant_lock(struct dlm_lkb * lkb, int send_remote);
static void send_blocking_asts(struct dlm_rsb * rsb, struct dlm_lkb * lkb);
static void send_blocking_asts_all(struct dlm_rsb *rsb, struct dlm_lkb *lkb);
static int convert_lock(struct dlm_ls * ls, int mode, struct dlm_lksb *lksb,
			int flags, void *ast, void *astarg, void *bast,
			struct dlm_range *range);
static int dlm_lock_stage1(struct dlm_ls * lspace, struct dlm_lkb * lkb, int flags,
			   char *name, int namelen);


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
 * Resolve conversion deadlock by changing to NL the granted mode of deadlocked
 * locks on the convert queue.  One of the deadlocked locks is allowed to
 * retain its original granted state (we choose the lkb provided although it
 * shouldn't matter which.)  We do not change the granted mode on locks without
 * the CONVDEADLK flag.  If any of these exist (there shouldn't if the app uses
 * the flag consistently) the false return value is used.
 */

static int conversion_deadlock_resolve(struct dlm_rsb *rsb, struct dlm_lkb *lkb)
{
	struct dlm_lkb *this;
	int rv = TRUE;

	list_for_each_entry(this, &rsb->res_convertqueue, lkb_statequeue) {
		if (this == lkb)
			continue;

		if (!ranges_overlap(lkb, this))
			continue;

		if (!modes_compat(this, lkb) && !modes_compat(lkb, this)) {

			if (!(this->lkb_lockqueue_flags & DLM_LKF_CONVDEADLK)){
				rv = FALSE;
				continue;
			}
			this->lkb_grmode = DLM_LOCK_NL;
			this->lkb_flags |= GDLM_LKFLG_DEMOTED;
		}
	}
	return rv;
}

/*
 * "A conversion deadlock arises with a pair of lock requests in the converting
 * queue for one resource.  The granted mode of each lock blocks the requested
 * mode of the other lock."
 */

static int conversion_deadlock_detect(struct dlm_rsb *rsb, struct dlm_lkb *lkb)
{
	struct dlm_lkb *this;

	list_for_each_entry(this, &rsb->res_convertqueue, lkb_statequeue) {
		if (this == lkb)
			continue;

		if (!ranges_overlap(lkb, this))
			continue;

		if (!modes_compat(this, lkb) && !modes_compat(lkb, this))
			return TRUE;
	}
	return FALSE;
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
 * Deadlock can arise when using the QUECVT flag if the requested mode of the
 * first converting lock is incompatible with the granted mode of another
 * converting lock further down the queue.  To prevent this deadlock, a
 * requested QUEUECVT lock is granted immediately if adding it to the end of
 * the queue would prevent a lock ahead of it from being granted.
 */

static int queuecvt_deadlock_detect(struct dlm_rsb *rsb, struct dlm_lkb *lkb)
{
	struct dlm_lkb *this;

	list_for_each_entry(this, &rsb->res_convertqueue, lkb_statequeue) {
		if (this == lkb)
			break;

		if (ranges_overlap(lkb, this) && !modes_compat(lkb, this))
			return TRUE;
	}
	return FALSE;
}

/*
 * Return 1 if the lock can be granted, 0 otherwise.
 * Also detect and resolve conversion deadlocks.
 */

static int can_be_granted(struct dlm_rsb *rsb, struct dlm_lkb *lkb)
{
        if (test_bit(LSFL_NOCONVGRANT, &rsb->res_ls->ls_flags) &&
	    lkb->lkb_grmode == DLM_LOCK_IV &&
	    !list_empty(&rsb->res_convertqueue))
	        return FALSE;

        if (lkb->lkb_rqmode == DLM_LOCK_NL)
		return TRUE;

	if (lkb->lkb_rqmode == lkb->lkb_grmode)
		return TRUE;

	if (queue_conflict(&rsb->res_grantqueue, lkb))
		return FALSE;

	if (!queue_conflict(&rsb->res_convertqueue, lkb)) {
		if (!(lkb->lkb_lockqueue_flags & DLM_LKF_QUECVT))
			return TRUE;

		if (list_empty(&rsb->res_convertqueue) ||
		    first_in_list(lkb, &rsb->res_convertqueue) ||
		    queuecvt_deadlock_detect(rsb, lkb))
			return TRUE;
		else
			return FALSE;
	}

	/* there *is* a conflict between this lkb and a converting lock so
	   we return false unless conversion deadlock resolution is permitted
	   (only conversion requests will have the CONVDEADLK flag set) */

	if (!(lkb->lkb_lockqueue_flags & DLM_LKF_CONVDEADLK))
		return FALSE;

	if (!conversion_deadlock_detect(rsb, lkb))
		return FALSE;

	if (conversion_deadlock_resolve(rsb, lkb))
		return TRUE;

	return FALSE;
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
		goto out;

	if (mode < 0 || mode > DLM_LOCK_EX)
		goto out;

	if (namelen > DLM_RESNAME_MAXLEN)
		goto out;

	if (flags & DLM_LKF_CANCEL)
		goto out;

	if (flags & DLM_LKF_QUECVT && !(flags & DLM_LKF_CONVERT))
		goto out;

	if (flags & DLM_LKF_EXPEDITE && !(flags & DLM_LKF_CONVERT))
		goto out;

	if (flags & DLM_LKF_EXPEDITE && flags & DLM_LKF_QUECVT)
		goto out;

	if (flags & DLM_LKF_EXPEDITE && flags & DLM_LKF_NOQUEUE)
		goto out;

	if (!ast || !lksb)
		goto out;

	if (!lksb->sb_lvbptr && (flags & DLM_LKF_VALBLK))
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

	/* Copy the range if appropriate */
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
	return ret;
}

int dlm_lock_stage1(struct dlm_ls *ls, struct dlm_lkb *lkb, int flags,
		    char *name, int namelen)
{
	struct dlm_rsb *rsb, *parent_rsb = NULL;
	struct dlm_lkb *parent_lkb = lkb->lkb_parent;
	uint32_t nodeid;
	int error;

	if (parent_lkb)
		parent_rsb = parent_lkb->lkb_resource;

	error = find_or_create_rsb(ls, parent_rsb, name, namelen, 1, &rsb);
	if (error)
		goto out;
	lkb->lkb_resource = rsb;

	log_debug(ls, "rq %u %x \"%s\"", lkb->lkb_rqmode, lkb->lkb_id,
		  rsb->res_name);
	/*
	 * Next stage, do we need to find the master or can
	 * we get on with the real locking work ?
	 */

	if (rsb->res_nodeid == -1) {
		if (get_directory_nodeid(rsb) != our_nodeid()) {
			error = remote_stage(lkb, GDLM_LQSTATE_WAIT_RSB);
			goto out;
		}

		error = dlm_dir_lookup(ls, our_nodeid(), rsb->res_name,
				       rsb->res_length, &nodeid);
		if (error)
			goto out;

		if (nodeid == our_nodeid()) {
			set_bit(RESFL_MASTER, &rsb->res_flags);
			nodeid = 0;
		} else
			clear_bit(RESFL_MASTER, &rsb->res_flags);
		rsb->res_nodeid = nodeid;
	}

	lkb->lkb_nodeid = rsb->res_nodeid;

	error = dlm_lock_stage2(ls, lkb, rsb, flags);

      out:
	if (error)
		release_rsb(rsb);

	return error;
}

/*
 * Locking routine called after we have an RSB, either a copy of a remote one
 * or a local one, or perhaps a shiny new one all of our very own
 */

int dlm_lock_stage2(struct dlm_ls *ls, struct dlm_lkb *lkb, struct dlm_rsb *rsb,
		    int flags)
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

	error = find_or_create_rsb(ls, parent_rsb, freq->rr_name, namelen, 0,
				   &rsb);
	if (error)
		goto fail_free;

	if (!rsb || rsb->res_nodeid != 0) {
		log_debug(ls, "send einval to %u", remote_nodeid);
		lkb->lkb_retstatus = -EINVAL;
		if (rsb)
			release_rsb(rsb);
		goto out;
	}
	
	lkb->lkb_resource = rsb;

	log_debug(ls, "rq %u from %u %x \"%s\"", lkb->lkb_rqmode, remote_nodeid,
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

	if (can_be_granted(rsb, lkb)) {
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
		queue_ast(lkb, AST_COMP | AST_DEL, 0);
		goto out;
	}

	/*
	 * The requested lkb must wait.  Because the rsb of the requested lkb
	 * is mastered here, send blocking asts for the lkb's blocking the
	 * request.
	 */

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

	if (!ls)
		goto out;

	lkb = find_lock_by_id(ls, lkid);
	if (!lkb)
		goto out;

	/* Can't dequeue a master copy (a remote node's mastered lock) */
	if (lkb->lkb_flags & GDLM_LKFLG_MSTCPY)
		goto out;

	/* Already waiting for a remote lock operation */
	if (lkb->lkb_lockqueue_state) {
		ret = -EBUSY;
		goto out;
	}

	/* Can only cancel WAITING or CONVERTing locks.
	 * This is just a quick check - it is also checked in unlock_stage2()
	 * (which may be on the master) under the semaphore.
	 */
	if ((flags & DLM_LKF_CANCEL) &&
	    (lkb->lkb_status == GDLM_LKSTS_GRANTED))
		goto out;

	/* "Normal" unlocks must operate on a granted lock */
	if (!(flags & DLM_LKF_CANCEL) &&
	    (lkb->lkb_status != GDLM_LKSTS_GRANTED))
		goto out;

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

	log_debug(ls, "un %x ref %u flg %x nodeid %d/%d \"%s\"", lkb->lkb_id,
		  atomic_read(&rsb->res_ref), rsb->res_flags,
		  lkb->lkb_nodeid, rsb->res_nodeid, rsb->res_name);

	/* Save any new params */
	if (lksb)
		lkb->lkb_lksb = lksb;
	if (astarg)
		lkb->lkb_astparam = (long) astarg;
	lkb->lkb_lockqueue_flags = flags;

	if (lkb->lkb_nodeid)
		ret = remote_stage(lkb, GDLM_LQSTATE_WAIT_UNLOCK);
	else
		ret = dlm_unlock_stage2(lkb, rsb, flags);
	up_read(&ls->ls_in_recovery);

	wake_astd();

      out:
	return ret;
}

int dlm_unlock_stage2(struct dlm_lkb *lkb, struct dlm_rsb *rsb, uint32_t flags)
{
	int remote = lkb->lkb_flags & GDLM_LKFLG_MSTCPY;
	int old_status;

	down_write(&rsb->res_lock);

	/* Can only cancel WAITING or CONVERTing locks */
	if ((flags & DLM_LKF_CANCEL) &&
	    (lkb->lkb_status == GDLM_LKSTS_GRANTED)) {
	        lkb->lkb_retstatus = -EINVAL;
		queue_ast(lkb, AST_COMP, 0);
	        goto out;
	}

	old_status = lkb_dequeue(lkb);

	/*
	 * Cancelling a conversion
	 */

	if ((old_status == GDLM_LKSTS_CONVERT) && (flags & DLM_LKF_CANCEL)) {
		/* VMS semantics say we should send blocking ASTs again here */
		send_blocking_asts(rsb, lkb);

		/* Remove from deadlock detection */
		if (lkb->lkb_duetime)
			remove_from_deadlockqueue(lkb);

		/* Stick it back on the granted queue */
		lkb_enqueue(rsb, lkb, GDLM_LKSTS_GRANTED);
		lkb->lkb_rqmode = lkb->lkb_grmode;

		/* Was it blocking any other locks? */
		if (first_in_list(lkb, &rsb->res_convertqueue))
			grant_pending_locks(rsb);

		lkb->lkb_retstatus = -DLM_ECANCEL;
		queue_ast(lkb, AST_COMP, 0);
		goto out;
	}

	/*
	 * If was granted grant any converting or waiting locks
	 * and save or clear lvb
	 */

	if (old_status == GDLM_LKSTS_GRANTED) {
		if (rsb->res_lvbptr && (lkb->lkb_grmode >= DLM_LOCK_PW)) {
			if ((flags & DLM_LKF_VALBLK) && lkb->lkb_lvbptr)
				memcpy(rsb->res_lvbptr, lkb->lkb_lvbptr,
				       DLM_LVB_LEN);
			if (flags & DLM_LKF_IVVALBLK)
				memset(rsb->res_lvbptr, 0, DLM_LVB_LEN);
		}

		grant_pending_locks(rsb);
	}

	lkb->lkb_retstatus = flags & DLM_LKF_CANCEL ? -DLM_ECANCEL:-DLM_EUNLOCK;

	if (!remote) {
		queue_ast(lkb, AST_COMP | AST_DEL, 0);
	} else {
		up_write(&rsb->res_lock);
		release_lkb(rsb->res_ls, lkb);
		release_rsb(rsb);
		goto out2;
	}

 out:
	up_write(&rsb->res_lock);
 out2:
	wake_astd();
	return 0;
}

/*
 * Lock conversion
 */

static int convert_lock(struct dlm_ls *ls, int mode, struct dlm_lksb *lksb,
			int flags, void *ast, void *astarg, void *bast,
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

	if ((flags & DLM_LKF_VALBLK) && !lksb->sb_lvbptr) {
		goto out;
	}

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

	log_debug(ls, "cv %u %x \"%s\"", mode, lkb->lkb_id, rsb->res_name);

	lkb->lkb_flags &= ~GDLM_LKFLG_VALBLK;
	lkb->lkb_flags &= ~GDLM_LKFLG_DEMOTED;

	if (flags & DLM_LKF_NODLCKWT)
		lkb->lkb_flags |= GDLM_LKFLG_NODLCKWT;
	if (ast)
		lkb->lkb_astaddr = ast;
	if (astarg)
		lkb->lkb_astparam = (long) astarg;
	if (bast)
		lkb->lkb_bastaddr = bast;
	lkb->lkb_rqmode = mode;
	lkb->lkb_lockqueue_flags = flags;
	lkb->lkb_flags |= (flags & DLM_LKF_VALBLK) ? GDLM_LKFLG_VALBLK : 0;
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

	if (can_be_granted(rsb, lkb)) {
		grant_lock(lkb, 0);
		grant_pending_locks(rsb);
		goto out;
	}

	/*
	 * Remove lkb from granted queue.
	 */

	lkb_dequeue(lkb);

	/*
	 * The user won't wait so stick it back on the grant queue
	 */

	if (lkb->lkb_lockqueue_flags & DLM_LKF_NOQUEUE) {
		lkb_enqueue(rsb, lkb, GDLM_LKSTS_GRANTED);
		ret = lkb->lkb_retstatus = -EAGAIN;
		if (do_ast)
			queue_ast(lkb, AST_COMP, 0);
		if (lkb->lkb_lockqueue_flags & DLM_LKF_NOQUEUEBAST)
			send_blocking_asts_all(rsb, lkb);
		goto out;
	}

	/*
	 * The lkb's status tells which queue it's on.  Put back on convert
	 * queue.  (QUECVT requests added at end of the queue, all others in
	 * order.)
	 */

	lkb->lkb_retstatus = 0;
	lkb_enqueue(rsb, lkb, GDLM_LKSTS_CONVERT);

	/*
	 * If the request can't be granted
	 */

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

		b = __lvb_operations[lkb->lkb_grmode + 1][lkb->lkb_rqmode + 1];
		if (b)
			memcpy(lkb->lkb_lvbptr, rsb->res_lvbptr, DLM_LVB_LEN);
		else
			memcpy(rsb->res_lvbptr, lkb->lkb_lvbptr, DLM_LVB_LEN);
	}

	if (lkb->lkb_range) {
		lkb->lkb_range[GR_RANGE_START] = lkb->lkb_range[RQ_RANGE_START];
		lkb->lkb_range[GR_RANGE_END] = lkb->lkb_range[RQ_RANGE_END];
	}

	lkb->lkb_grmode = lkb->lkb_rqmode;
	lkb->lkb_rqmode = DLM_LOCK_IV;
	lkb_swqueue(rsb, lkb, GDLM_LKSTS_GRANTED);

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
 * Called when a lock has been dequeued. Look for any locks to grant that are
 * waiting for conversion or waiting to be granted.
 * The rsb res_lock must be held in write when this function is called.
 */

int grant_pending_locks(struct dlm_rsb *rsb)
{
	struct dlm_lkb *lkb;
	struct list_head *list;
	struct list_head *temp;
	int8_t high = DLM_LOCK_IV;

	list_for_each_safe(list, temp, &rsb->res_convertqueue) {
		lkb = list_entry(list, struct dlm_lkb, lkb_statequeue);

		if (can_be_granted(rsb, lkb))
			grant_lock(lkb, 1);
		else
			high = MAX(lkb->lkb_rqmode, high);
	}

	list_for_each_safe(list, temp, &rsb->res_waitqueue) {
		lkb = list_entry(list, struct dlm_lkb, lkb_statequeue);

		if (can_be_granted(rsb, lkb))
			grant_lock(lkb, 1);
		else
			high = MAX(lkb->lkb_rqmode, high);
	}

	/*
	 * If there are locks left on the wait/convert queue then send blocking
	 * ASTs to granted locks that are blocking
	 *
	 * FIXME: This might generate some spurious blocking ASTs for range
	 * locks.
	 */

	if (high > DLM_LOCK_IV) {
		list_for_each_safe(list, temp, &rsb->res_grantqueue) {
			lkb = list_entry(list, struct dlm_lkb, lkb_statequeue);

			if (lkb->lkb_bastaddr &&
			    (lkb->lkb_highbast < high) &&
			    !__dlm_compat_matrix[lkb->lkb_grmode+1][high+1]) {

				queue_ast(lkb, AST_BAST, high);
				lkb->lkb_highbast = high;
			}
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
		if (lkb->lkb_duetime) {
			remove_from_deadlockqueue(lkb);
		}
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
		dlm_convert_stage2(lkb, TRUE);
		break;

	default:
		DLM_ASSERT(0,);
	}
}

static void dump_queue(struct list_head *head)
{
	struct dlm_lkb *lkb;

	list_for_each_entry(lkb, head, lkb_statequeue) {
		printk("%08x gr %d rq %d flg %x sts %u node %u remid %x "
		       "lq %d,%x\n",
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
	printk("name \"%s\" flags %lx nodeid %u ref %u\n",
	       rsb->res_name, rsb->res_flags, rsb->res_nodeid,
	       atomic_read(&rsb->res_ref));

	if (!list_empty(&rsb->res_grantqueue)) {
		printk("grant queue\n");
		dump_queue(&rsb->res_grantqueue);
	}

	if (!list_empty(&rsb->res_convertqueue)) {
		printk("convert queue\n");
		dump_queue(&rsb->res_convertqueue);
	}

	if (!list_empty(&rsb->res_waitqueue)) {
		printk("wait queue\n");
		dump_queue(&rsb->res_waitqueue);
	}
}

void dlm_locks_dump(void)
{
	struct dlm_ls *ls;
	struct dlm_rsb *rsb;
	struct list_head *head;
	int i;

	list_for_each_entry(ls, &lslist, ls_list) {
		for (i = 0; i < ls->ls_rsbtbl_size; i++) {
			head = &ls->ls_rsbtbl[i].list;
			list_for_each_entry(rsb, head, res_hashchain)
				dump_rsb(rsb);
		}
	}
}

