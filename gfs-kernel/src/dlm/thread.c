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

#include "lock_dlm.h"

/* 
 * Run in dlm_async thread 
 */

/**
 * queue_submit - add lock request to queue for dlm_async thread
 * @lp: DLM lock
 *
 * A lock placed on this queue is re-submitted to DLM as soon as
 * dlm_async thread gets to it.  
 */

static void queue_submit(dlm_lock_t *lp)
{
	dlm_t *dlm = lp->dlm;

	spin_lock(&dlm->async_lock);
	list_add_tail(&lp->slist, &dlm->submit);
	set_bit(LFL_SLIST, &lp->flags);
	spin_unlock(&dlm->async_lock);
	wake_up(&dlm->wait);
}

/**
 * process_blocking - processing of blocking callback
 * @lp: DLM lock
 *
 */

static void process_blocking(dlm_lock_t *lp, int bast_mode)
{
	dlm_t *dlm = lp->dlm;
	unsigned int cb;

	switch (make_lmstate(bast_mode)) {
	case LM_ST_EXCLUSIVE:
		cb = LM_CB_NEED_E;
		break;
	case LM_ST_DEFERRED:
		cb = LM_CB_NEED_D;
		break;
	case LM_ST_SHARED:
		cb = LM_CB_NEED_S;
		break;
	default:
		DLM_ASSERT(0, printk("unknown bast mode %u\n", lp->bast_mode););
	}

	dlm->fscb(dlm->fsdata, cb, &lp->lockname);
}

/**
 * process_complete - processing of completion callback for a lock request
 * @lp: DLM lock
 *
 */

static void process_complete(dlm_lock_t *lp)
{
	dlm_t *dlm = lp->dlm;
	struct lm_async_cb acb;
	int16_t prev_mode = lp->cur;

	memset(&acb, 0, sizeof(acb));

	/*
	 * This is an AST for an unlock.
	 */

	if (test_and_clear_bit(LFL_DLM_UNLOCK, &lp->flags)) {

		/* FIXME: Add an assertion to catch NOFAIL promotions from
		 * non-NL modes? */

		if (lp->lksb.sb_status == -DLM_ECANCEL) {

			/* lp->cur remains the same, is there anything to clear
			 * or reset to put this lp into an "ordinary" state? */

			printk("lock_dlm: -DLM_ECANCEL num=%x,%"PRIx64"\n",
			       lp->lockname.ln_type, lp->lockname.ln_number);
		} else {
			DLM_ASSERT(lp->lksb.sb_status == -DLM_EUNLOCK,
				   printk("num=%x,%"PRIx64" status=%d\n",
					  lp->lockname.ln_type,
					  lp->lockname.ln_number,
					  lp->lksb.sb_status););
			lp->cur = DLM_LOCK_IV;
		}

		complete(&lp->uast_wait);
		return;
	}

	/*
	 * A canceled lock request.  The lock was just taken off the delayed
	 * list and was never even submitted to dlm.
	 */

	if (test_and_clear_bit(LFL_CANCEL, &lp->flags)) {
		lp->req = lp->cur;
		acb.lc_ret |= LM_OUT_CANCELED;
		goto out;
	}

	/*
	 * An error occured.
	 */

	if (lp->lksb.sb_status) {
		lp->req = lp->cur;
		if (lp->cur == DLM_LOCK_IV)
			lp->lksb.sb_lkid = 0;

		if ((lp->lksb.sb_status == -EAGAIN) &&
		    (lp->lkf & DLM_LKF_NOQUEUE)) {
			/* a "normal" error */
		} else
			printk("lock_dlm: process_complete error id=%x "
			       "status=%d\n", lp->lksb.sb_lkid,
			       lp->lksb.sb_status);
		goto out;
	}

	/*
	 * This is an AST for an EX->EX conversion for sync_lvb from GFS.
	 */

	if (test_and_clear_bit(LFL_SYNC_LVB, &lp->flags)) {
		complete(&lp->uast_wait);
		return;
	}

	/*
	 * A lock has been demoted to NL because it initially completed during
	 * BLOCK_LOCKS.  Now it must be requested in the originally requested
	 * mode.
	 */

	if (test_and_clear_bit(LFL_REREQUEST, &lp->flags)) {

		DLM_ASSERT(lp->req == DLM_LOCK_NL,);
		DLM_ASSERT(lp->prev_req > DLM_LOCK_NL,);

		lp->cur = DLM_LOCK_NL;
		lp->req = lp->prev_req;
		lp->prev_req = DLM_LOCK_IV;
		lp->lkf &= ~DLM_LKF_CONVDEADLK;
		lp->lkf |= DLM_LKF_QUECVT;

		set_bit(LFL_NOCACHE, &lp->flags);

		if (test_bit(DFL_BLOCK_LOCKS, &dlm->flags) &&
		    !test_bit(LFL_NOBLOCK, &lp->flags))
			queue_delayed(lp, QUEUE_LOCKS_BLOCKED);
		else
			queue_submit(lp);
		return;
	}

	/* 
	 * A request is granted during dlm recovery.  It may be granted
	 * because the locks of a failed node were cleared.  In that case,
	 * there may be inconsistent data beneath this lock and we must wait
	 * for recovery to complete to use it.  When gfs recovery is done this
	 * granted lock will be converted to NL and then reacquired in this
	 * granted state.
	 */

	if (test_bit(DFL_BLOCK_LOCKS, &dlm->flags) &&
	    !test_bit(LFL_NOBLOCK, &lp->flags) &&
	    lp->req != DLM_LOCK_NL) {

		lp->cur = lp->req;
		lp->prev_req = lp->req;
		lp->req = DLM_LOCK_NL;
		lp->lkf |= DLM_LKF_CONVERT;
		lp->lkf &= ~DLM_LKF_CONVDEADLK;
		lp->lkf &= ~DLM_LKF_QUECVT;

		set_bit(LFL_REREQUEST, &lp->flags);
		queue_submit(lp);
		return;
	}

	/*
	 * DLM demoted the lock to NL before it was granted so GFS must be
	 * told it cannot cache data for this lock.
	 */

	if (lp->lksb.sb_flags == DLM_SBF_DEMOTED)
		set_bit(LFL_NOCACHE, &lp->flags);

      out:

	/*
	 * This is an internal lock_dlm lock used for managing JIDs.
	 */

	if (test_bit(LFL_IDLOCK, &lp->flags)) {
		clear_bit(LFL_NOBLOCK, &lp->flags);
		lp->cur = lp->req;
		complete(&lp->uast_wait);
		return;
	}

	/*
	 * Normal completion of a lock request.  Tell GFS it now has the lock.
	 */

	clear_bit(LFL_NOBLOCK, &lp->flags);
	lp->cur = lp->req;

	acb.lc_name = lp->lockname;
	acb.lc_ret |= make_lmstate(lp->cur);

	if (!test_and_clear_bit(LFL_NOCACHE, &lp->flags) &&
	    (lp->cur > DLM_LOCK_NL) && (prev_mode > DLM_LOCK_NL))
		acb.lc_ret |= LM_OUT_CACHEABLE;

	dlm->fscb(dlm->fsdata, LM_CB_ASYNC, &acb);
}

/**
 * no_work - determine if there's work for the dlm_async thread
 * @dlm:
 *
 * Returns: 1 if no work, 0 otherwise
 */

static __inline__ int no_work(dlm_t * dlm)
{
	int ret;

	spin_lock(&dlm->async_lock);

	ret = list_empty(&dlm->complete) &&
	    list_empty(&dlm->blocking) &&
	    list_empty(&dlm->submit) &&
	    list_empty(&dlm->starts) && !test_bit(DFL_MG_FINISH, &dlm->flags);

	spin_unlock(&dlm->async_lock);

	return ret;
}

/**
 * dlm_async - thread for a variety of asynchronous processing
 * @data:
 *
 * Returns: 0 on success, -EXXX on failure
 */

static int dlm_async(void *data)
{
	dlm_t *dlm = (dlm_t *) data;
	dlm_lock_t *lp = NULL;
	dlm_start_t *ds = NULL;
	uint8_t complete, blocking, submit, start, finish;
	DECLARE_WAITQUEUE(wait, current);

	daemonize("lock_dlm");
	atomic_inc(&dlm->threads);

	do {
		current->state = TASK_INTERRUPTIBLE;
		add_wait_queue(&dlm->wait, &wait);
		if (no_work(dlm))
			schedule();
		remove_wait_queue(&dlm->wait, &wait);
		current->state = TASK_RUNNING;

		complete = blocking = submit = start = finish = 0;

		spin_lock(&dlm->async_lock);

		if (!list_empty(&dlm->complete)) {
			lp = list_entry(dlm->complete.next, dlm_lock_t, clist);
			list_del(&lp->clist);
			clear_bit(LFL_CLIST, &lp->flags);
			complete = 1;
		} else if (!list_empty(&dlm->blocking)) {
			lp = list_entry(dlm->blocking.next, dlm_lock_t, blist);
			list_del(&lp->blist);
			clear_bit(LFL_BLIST, &lp->flags);
			blocking = lp->bast_mode;
			lp->bast_mode = 0;
		} else if (!list_empty(&dlm->submit)) {
			lp = list_entry(dlm->submit.next, dlm_lock_t, slist);
			list_del(&lp->slist);
			clear_bit(LFL_SLIST, &lp->flags);
			submit = 1;
		} else if (!list_empty(&dlm->starts)) {
			ds = list_entry(dlm->starts.next, dlm_start_t, list);
			list_del(&ds->list);
			start = 1;
		} else if (test_and_clear_bit(DFL_MG_FINISH, &dlm->flags)) {
			finish = 1;
		}

		spin_unlock(&dlm->async_lock);

		if (complete)
			process_complete(lp);

		else if (blocking)
			process_blocking(lp, blocking);

		else if (submit)
			process_submit(lp);

		else if (start)
			process_start(dlm, ds);

		else if (finish)
			process_finish(dlm);

		schedule();
	}
	while (!test_bit(DFL_THREAD_STOP, &dlm->flags));

	atomic_dec(&dlm->threads);
	return 0;
}

/**
 * init_async_thread
 * @dlm:
 *
 * Returns: 0 on success, -EXXX on failure
 */

int init_async_thread(dlm_t * dlm)
{
	int error;

	clear_bit(DFL_THREAD_STOP, &dlm->flags);
	atomic_set(&dlm->threads, 0);

	error = kernel_thread(dlm_async, dlm, 0);
	if (error < 0)
		goto out;

	error = kernel_thread(dlm_async, dlm, 0);
	if (error < 0) {
		release_async_thread(dlm);
		goto out;
	}

	while (atomic_read(&dlm->threads) != 2)
		schedule();
	error = 0;

      out:
	if (error)
		printk("lock_dlm: can't start async thread %d\n", error);
	return error;
}

/**
 * release_async_thread
 * @dlm:
 *
 */

void release_async_thread(dlm_t * dlm)
{
	set_bit(DFL_THREAD_STOP, &dlm->flags);
	while (atomic_read(&dlm->threads)) {
		wake_up(&dlm->wait);
		schedule();
	}
}
