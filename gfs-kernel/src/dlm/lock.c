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

static char junk_lvb[DLM_LVB_SIZE];


/*
 * Run in DLM thread
 */

static void queue_complete(dlm_lock_t *lp)
{
	dlm_t *dlm = lp->dlm;

	if (!test_bit(LFL_WAIT_COMPLETE, &lp->flags)) {
		log_all("extra completion %x,%"PRIx64" %d %d,%d id %x flags %lx",
		        lp->lockname.ln_type, lp->lockname.ln_number,
		        lp->lksb.sb_status, lp->cur, lp->req, lp->lksb.sb_lkid, lp->flags);
		dump_stack();
		return;
	}

	clear_bit(LFL_WAIT_COMPLETE, &lp->flags);

	log_debug("qc %x,%"PRIx64" %d,%d id %x sts %d %x",
		  lp->lockname.ln_type, lp->lockname.ln_number,
		  lp->cur, lp->req, lp->lksb.sb_lkid, lp->lksb.sb_status,
		  lp->lksb.sb_flags);

	spin_lock(&dlm->async_lock);
	list_add_tail(&lp->clist, &dlm->complete);
	set_bit(LFL_CLIST, &lp->flags);
	spin_unlock(&dlm->async_lock);
	wake_up(&dlm->wait);
}

static void queue_blocking(dlm_lock_t *lp, int mode)
{
	dlm_t *dlm = lp->dlm;

	/* We often get basts for EX while we're promoting from SH to EX */
	/*
	if (test_bit(LFL_WAIT_COMPLETE, &lp->flags)) {
		log_debug("bast during wait %x,%"PRIx64" %d-%d %x bmode %d",
			  lp->lockname.ln_type, lp->lockname.ln_number,
			  lp->cur, lp->req, lp->lkf, mode);
	}
	*/

	if (!mode) {
		printk("lock_dlm: bast mode zero %x,%"PRIx64"\n",
			lp->lockname.ln_type, lp->lockname.ln_number);
		return;
	}

	spin_lock(&dlm->async_lock);

	if (!lp->bast_mode) {
		list_add_tail(&lp->blist, &dlm->blocking);
		set_bit(LFL_BLIST, &lp->flags);
		lp->bast_mode = mode;
	} else if (lp->bast_mode < mode)
		lp->bast_mode = mode;

	spin_unlock(&dlm->async_lock);
	wake_up(&dlm->wait);
}

static __inline__ void lock_ast(void *astarg)
{
	queue_complete((dlm_lock_t *) astarg);
}

static __inline__ void lock_bast(void *astarg, int mode)
{
	queue_blocking((dlm_lock_t *) astarg, mode);
}

/*
 * Run in GFS or user thread
 */

/**
 * queue_delayed - add request to queue to be submitted later
 * @lp: DLM lock
 * @type: the reason the lock is blocked
 *
 * Queue of locks which need submitting sometime later.  Locks here
 * due to BLOCKED_LOCKS are moved to request queue when recovery is
 * done.  Locks here due to an ERROR are moved to request queue after
 * some delay.  This could also be called from dlm_async thread.
 */

void queue_delayed(dlm_lock_t *lp, int type)
{
	dlm_t *dlm = lp->dlm;

	lp->type = type;

	spin_lock(&dlm->async_lock);
	list_add_tail(&lp->dlist, &dlm->delayed);
	set_bit(LFL_DLIST, &lp->flags);
	spin_unlock(&dlm->async_lock);
}

/**
 * make_mode - convert to DLM_LOCK_
 * @lmstate: GFS lock state
 *
 * Returns: DLM lock mode
 */

static int16_t make_mode(int16_t lmstate)
{
	switch (lmstate) {
	case LM_ST_UNLOCKED:
		return DLM_LOCK_NL;
	case LM_ST_EXCLUSIVE:
		return DLM_LOCK_EX;
	case LM_ST_DEFERRED:
		return DLM_LOCK_CW;
	case LM_ST_SHARED:
		return DLM_LOCK_PR;
	default:
		DLM_ASSERT(0, printk("unknown LM state %d\n", lmstate););
	}
}

/**
 * make_lmstate - convert to LM_ST_
 * @dlmmode: DLM lock mode 
 *
 * Returns: GFS lock state 
 */

int16_t make_lmstate(int16_t dlmmode)
{
	switch (dlmmode) {
	case DLM_LOCK_IV:
	case DLM_LOCK_NL:
		return LM_ST_UNLOCKED;
	case DLM_LOCK_EX:
		return LM_ST_EXCLUSIVE;
	case DLM_LOCK_CW:
		return LM_ST_DEFERRED;
	case DLM_LOCK_PR:
		return LM_ST_SHARED;
	default:
		DLM_ASSERT(0, printk("unknown DLM mode %d\n", dlmmode););
	}
}

/**
 * check_cur_state - verify agreement with GFS on the current lock state
 * @lp: the DLM lock 
 * @cur_state: the current lock state from GFS
 *
 * NB: DLM_LOCK_NL and DLM_LOCK_IV are both considered 
 * LM_ST_UNLOCKED by GFS.
 *
 */

static void check_cur_state(dlm_lock_t *lp, unsigned int cur_state)
{
	int16_t cur = make_mode(cur_state);
	if (lp->cur != DLM_LOCK_IV)
		DLM_ASSERT(lp->cur == cur, printk("%d, %d\n", lp->cur, cur););
}

/**
 * make_flags - put together necessary DLM flags
 * @lp: DLM lock
 * @gfs_flags: GFS flags
 * @cur: current DLM lock mode
 * @req: requested DLM lock mode
 *
 * Returns: DLM flags
 */

static unsigned int make_flags(dlm_lock_t *lp, unsigned int gfs_flags,
			       int16_t cur, int16_t req)
{
	unsigned int lkf = 0;

	if (gfs_flags & LM_FLAG_TRY)
		lkf |= DLM_LKF_NOQUEUE;

	if (gfs_flags & LM_FLAG_TRY_1CB) {
		lkf |= DLM_LKF_NOQUEUE;
		lkf |= DLM_LKF_NOQUEUEBAST;
	}

	if (gfs_flags & LM_FLAG_PRIORITY) {
		lkf |= DLM_LKF_NOORDER;
		lkf |= DLM_LKF_HEADQUE;
	}

	if (gfs_flags & LM_FLAG_ANY) {
		if (req == DLM_LOCK_PR)
			lkf |= DLM_LKF_ALTCW;
		else if (req == DLM_LOCK_CW)
			lkf |= DLM_LKF_ALTPR;
	}

	if (lp->lksb.sb_lkid != 0) {
		lkf |= DLM_LKF_CONVERT;

		/* Conversion deadlock avoidance by DLM */

		if (!test_bit(LFL_FORCE_PROMOTE, &lp->flags) &&
		    !(lkf & DLM_LKF_NOQUEUE) &&
		    cur > DLM_LOCK_NL && req > DLM_LOCK_NL && cur != req)
			lkf |= DLM_LKF_CONVDEADLK;
	}

	if (lp->lvb)
		lkf |= DLM_LKF_VALBLK;

	return lkf;
}

/**
 * make_strname - convert GFS lock numbers to string
 * @lockname: the lock type/number 
 * @str: the lock string/length
 *
 */

static __inline__ void make_strname(struct lm_lockname *lockname,
				    strname_t *str)
{
	sprintf(str->name, "%8x%16"PRIx64, lockname->ln_type,
		lockname->ln_number);
	str->namelen = LOCK_DLM_STRNAME_BYTES;
}

int create_lp(dlm_t *dlm, struct lm_lockname *name, dlm_lock_t **lpp)
{
	dlm_lock_t *lp;

	lp = kmalloc(sizeof(dlm_lock_t), GFP_KERNEL);
	if (!lp)
		return -ENOMEM;

	memset(lp, 0, sizeof(dlm_lock_t));
	lp->lockname = *name;
	lp->dlm = dlm;
	lp->cur = DLM_LOCK_IV;
	lp->lvb = NULL;
	lp->hold_null = NULL;
	init_completion(&lp->uast_wait);

	spin_lock(&dlm->async_lock);
	list_add(&lp->all_list, &dlm->all_locks);
	dlm->all_locks_count++;
	spin_unlock(&dlm->async_lock);

	*lpp = lp;
	return 0;
}

void delete_lp(dlm_lock_t *lp)
{
	dlm_t *dlm = lp->dlm;

	spin_lock(&dlm->async_lock);
	if (test_bit(LFL_CLIST, &lp->flags)) {
		printk("lock_dlm: dlm_put_lock lp on clist num=%x,%"PRIx64"\n",
		       lp->lockname.ln_type, lp->lockname.ln_number);
		list_del(&lp->clist);
	}
	if (test_bit(LFL_BLIST, &lp->flags)) {
		/*
		printk("lock_dlm: dlm_put_lock lp on blist num=%x,%"PRIx64"\n",
		       lp->lockname.ln_type, lp->lockname.ln_number);
		*/
		list_del(&lp->blist);
	}
	if (test_bit(LFL_DLIST, &lp->flags)) {
		printk("lock_dlm: dlm_put_lock lp on dlist num=%x,%"PRIx64"\n",
		       lp->lockname.ln_type, lp->lockname.ln_number);
		list_del(&lp->dlist);
	}
	if (test_bit(LFL_SLIST, &lp->flags)) {
		printk("lock_dlm: dlm_put_lock lp on slist num=%x,%"PRIx64"\n",
		       lp->lockname.ln_type, lp->lockname.ln_number);
		list_del(&lp->slist);
	}

	list_del(&lp->all_list);
	dlm->all_locks_count--;
	spin_unlock(&dlm->async_lock);

	kfree(lp);
}

/**
 * dlm_get_lock - get a lm_lock_t given a descripton of the lock
 * @lockspace: the lockspace the lock lives in
 * @name: the name of the lock
 * @lockp: return the lm_lock_t here
 *
 * Returns: 0 on success, -EXXX on failure
 */

int lm_dlm_get_lock(lm_lockspace_t *lockspace, struct lm_lockname *name,
		    lm_lock_t **lockp)
{
	dlm_lock_t *lp;
	int error;

	error = create_lp((dlm_t *) lockspace, name, &lp);

	*lockp = (lm_lock_t *) lp;
	return error;
}

/**
 * dlm_put_lock - get rid of a lock structure
 * @lock: the lock to throw away
 *
 */

void lm_dlm_put_lock(lm_lock_t *lock)
{
	dlm_lock_t *lp = (dlm_lock_t *) lock;
	DLM_ASSERT(!lp->lvb,);
	delete_lp(lp);
}

void do_dlm_unlock(dlm_lock_t *lp)
{
	unsigned int lkf = 0;
	int error;

	set_bit(LFL_DLM_UNLOCK, &lp->flags);
	set_bit(LFL_WAIT_COMPLETE, &lp->flags);

	if (lp->lvb)
		lkf = DLM_LKF_VALBLK;

	log_debug("un %x,%"PRIx64" %x %d %x", lp->lockname.ln_type,
		  lp->lockname.ln_number, lp->lksb.sb_lkid, lp->cur, lkf);

	error = dlm_unlock(lp->dlm->gdlm_lsp, lp->lksb.sb_lkid, lkf, NULL, lp);

	DLM_ASSERT(!error,
		   printk("%s: error=%d num=%x,%"PRIx64" lkf=%x flags=%lx\n",
			  lp->dlm->fsname, error, lp->lockname.ln_type,
			  lp->lockname.ln_number, lkf, lp->flags););
}

void do_dlm_unlock_sync(dlm_lock_t *lp)
{
	set_bit(LFL_UNLOCK_SYNC, &lp->flags);
	init_completion(&lp->uast_wait);
	do_dlm_unlock(lp);
	wait_for_completion(&lp->uast_wait);
}

void do_dlm_lock(dlm_lock_t *lp, struct dlm_range *range)
{
	dlm_t *dlm = lp->dlm;
	strname_t str;
	int error, bast = 1;

	/*
	 * When recovery is in progress, delay lock requests for submission
	 * once recovery is done.  Requests for recovery (NOEXP) and unlocks
	 * can pass.
	 */

	if (test_bit(DFL_BLOCK_LOCKS, &dlm->flags) &&
	    !test_bit(LFL_NOBLOCK, &lp->flags) && lp->req != DLM_LOCK_NL) {
		queue_delayed(lp, QUEUE_LOCKS_BLOCKED);
		return;
	}

	/*
	 * Submit the actual lock request.
	 */

	if (lp->posix || test_bit(LFL_NOBAST, &lp->flags))
		bast = 0;

	make_strname(&lp->lockname, &str);

	set_bit(LFL_WAIT_COMPLETE, &lp->flags);

	log_debug("lk %x,%"PRIx64" id %x %d,%d %x", lp->lockname.ln_type,
		  lp->lockname.ln_number, lp->lksb.sb_lkid,
		  lp->cur, lp->req, lp->lkf);

	error = dlm_lock(dlm->gdlm_lsp, lp->req, &lp->lksb, lp->lkf, str.name,
			  str.namelen, 0, lock_ast, (void *) lp,
			  bast ? lock_bast : NULL, range);

	if ((error == -EAGAIN) && (lp->lkf & DLM_LKF_NOQUEUE)) {
		lp->lksb.sb_status = -EAGAIN;
		queue_complete(lp);
		error = 0;
	}

	DLM_ASSERT(!error,
		   printk("%s: num=%x,%"PRIx64" err=%d cur=%d req=%d lkf=%x\n",
			  dlm->fsname, lp->lockname.ln_type,
			  lp->lockname.ln_number, error, lp->cur, lp->req,
			  lp->lkf););
}

int do_dlm_lock_sync(dlm_lock_t *lp, struct dlm_range *range)
{
	init_completion(&lp->uast_wait);
	do_dlm_lock(lp, range);
	wait_for_completion(&lp->uast_wait);

	return lp->lksb.sb_status;
}

/**
 * lm_dlm_lock - acquire a lock
 * @lock: the lock to manipulate
 * @cur_state: the current state
 * @req_state: the requested state
 * @flags: modifier flags
 *
 * Returns: A bitmap of LM_OUT_* on success, -EXXX on failure
 */

unsigned int lm_dlm_lock(lm_lock_t *lock, unsigned int cur_state,
			 unsigned int req_state, unsigned int flags)
{
	dlm_lock_t *lp = (dlm_lock_t *) lock;

	clear_bit(LFL_DLM_CANCEL, &lp->flags);
	if (flags & LM_FLAG_NOEXP)
		set_bit(LFL_NOBLOCK, &lp->flags);

	check_cur_state(lp, cur_state);
	lp->req = make_mode(req_state);
	lp->lkf = make_flags(lp, flags, lp->cur, lp->req);

	do_dlm_lock(lp, NULL);
	return LM_OUT_ASYNC;
}

int lm_dlm_lock_sync(lm_lock_t *lock, unsigned int cur_state,
		     unsigned int req_state, unsigned int flags)
{
	dlm_lock_t *lp = (dlm_lock_t *) lock;

	init_completion(&lp->uast_wait);
	lm_dlm_lock(lock, cur_state, req_state, flags);
	wait_for_completion(&lp->uast_wait);

	return lp->lksb.sb_status;
}

/**
 * lm_dlm_unlock - unlock a lock
 * @lock: the lock to manipulate
 * @cur_state: the current state
 *
 * Returns: 0 on success, -EXXX on failure
 */

unsigned int lm_dlm_unlock(lm_lock_t *lock, unsigned int cur_state)
{
	dlm_lock_t *lp = (dlm_lock_t *) lock;

	clear_bit(LFL_DLM_CANCEL, &lp->flags);
	if (lp->cur == DLM_LOCK_IV)
		return 0;
	do_dlm_unlock(lp);
	return LM_OUT_ASYNC;
}

void lm_dlm_unlock_sync(lm_lock_t *lock, unsigned int cur_state)
{
	dlm_lock_t *lp = (dlm_lock_t *) lock;

	init_completion(&lp->uast_wait);
	lm_dlm_unlock(lock, cur_state);
	wait_for_completion(&lp->uast_wait);
}

/**
 * dlm_cancel - cancel a request that is blocked due to DFL_BLOCK_LOCKS
 * @lock: the lock to cancel request for
 *
 */

void lm_dlm_cancel(lm_lock_t *lock)
{
	dlm_lock_t *lp = (dlm_lock_t *) lock;
	int error, dlist = FALSE;

	if (test_bit(LFL_DLM_CANCEL, &lp->flags))
		return;

	log_all("lm_dlm_cancel %x,%"PRIx64" flags %lx",
		lp->lockname.ln_type, lp->lockname.ln_number, lp->flags);

	spin_lock(&lp->dlm->async_lock);
	if (test_and_clear_bit(LFL_DLIST, &lp->flags)) {
		list_del(&lp->dlist);
		lp->type = 0;
		dlist = TRUE;
	}
	spin_unlock(&lp->dlm->async_lock);

	if (dlist) {
		set_bit(LFL_CANCEL, &lp->flags);
		set_bit(LFL_WAIT_COMPLETE, &lp->flags);
		queue_complete(lp);
		return;
	}

	if (!test_bit(LFL_WAIT_COMPLETE, &lp->flags) ||
	    test_bit(LFL_DLM_UNLOCK, &lp->flags))	{
		log_all("lm_dlm_cancel skip %x,%"PRIx64" flags %lx",
			lp->lockname.ln_type, lp->lockname.ln_number,
			lp->flags);
		return;
	}

	/* the lock is blocked in the dlm */

	set_bit(LFL_DLM_CANCEL, &lp->flags);
	set_bit(LFL_WAIT_COMPLETE, &lp->flags);

	error = dlm_unlock(lp->dlm->gdlm_lsp, lp->lksb.sb_lkid, DLM_LKF_CANCEL,
			   NULL, lp);

	log_all("lm_dlm_cancel rv %d %x,%"PRIx64" flags %lx", error,
	        lp->lockname.ln_type, lp->lockname.ln_number, lp->flags);

	if (error == -EBUSY)
		clear_bit(LFL_DLM_CANCEL, &lp->flags);
}

int dlm_add_lvb(dlm_lock_t *lp)
{
	char *lvb;

	lvb = kmalloc(DLM_LVB_SIZE, GFP_KERNEL);
	if (!lvb)
		return -ENOMEM;

	memset(lvb, 0, DLM_LVB_SIZE);

	lp->lksb.sb_lvbptr = lvb;
	lp->lvb = lvb;
	return 0;
}

void dlm_del_lvb(dlm_lock_t *lp)
{
	kfree(lp->lvb);
	lp->lvb = NULL;
	lp->lksb.sb_lvbptr = NULL;
}

/**
 * hold_null_lock - add a NL lock to the resource
 * @lp: represents the resource
 *
 * This can do a synchronous dlm request (requiring a lock_dlm thread to
 * get the completion) because gfs won't call hold_lvb() during a
 * callback (from the context of a lock_dlm thread).
 *
 * Returns: 0 on success, -EXXX on failure
 */

static int hold_null_lock(dlm_lock_t *lp)
{
	dlm_lock_t *lpn = NULL;
	int error;

	if (lp->hold_null) {
		printk("lock_dlm: lvb already held\n");
		return 0;
	}

	error = create_lp(lp->dlm, &lp->lockname, &lpn);
	if (error)
		goto out;

	lpn->lksb.sb_lvbptr = junk_lvb;
	lpn->lvb = junk_lvb;

	lpn->req = DLM_LOCK_NL;
	lpn->lkf = DLM_LKF_VALBLK | DLM_LKF_EXPEDITE;
	set_bit(LFL_NOBAST, &lpn->flags);
	set_bit(LFL_INLOCK, &lpn->flags);

	error = do_dlm_lock_sync(lpn, NULL);
	if (error) {
		delete_lp(lpn);
		lpn = NULL;
	}

 out:
	lp->hold_null = lpn;
	return error;
}

/**
 * unhold_null_lock - remove the NL lock from the resource
 * @lp: represents the resource
 *
 * This cannot do a synchronous dlm request (requiring a lock_dlm thread to
 * get the completion) because gfs may call unhold_lvb() during a
 * callback (from the context of a lock_dlm thread) which could cause a
 * deadlock since the other lock_dlm thread could be engaged in recovery.
 *
 * Returns: 0 on success, -EXXX on failure
 */

static void unhold_null_lock(dlm_lock_t *lp)
{
	dlm_lock_t *lpn = lp->hold_null;

	lpn->lksb.sb_lvbptr = NULL;
	lpn->lvb = NULL;
	set_bit(LFL_UNLOCK_DELETE, &lpn->flags);
	do_dlm_unlock(lpn);
	lp->hold_null = NULL;
}

/**
 * dlm_hold_lvb - hold on to a lock value block
 * @lock: the lock the LVB is associated with
 * @lvbp: return the lvb memory here
 *
 * Returns: 0 on success, -EXXX on failure
 */

int lm_dlm_hold_lvb(lm_lock_t *lock, char **lvbp)
{
	dlm_lock_t *lp = (dlm_lock_t *) lock;
	int error;

	error = dlm_add_lvb(lp);
	if (error)
		return error;

	*lvbp = lp->lvb;

	/* Acquire a NL lock because gfs requires the value block to remain
	   intact on the resource while the lvb is "held" even if it's holding
	   no locks on the resource. */
      
	error = hold_null_lock(lp);
	if (error)
		dlm_del_lvb(lp);

	return error;
}

/**
 * dlm_unhold_lvb - release a LVB
 * @lock: the lock the LVB is associated with
 * @lvb: the lock value block
 *
 */

void lm_dlm_unhold_lvb(lm_lock_t *lock, char *lvb)
{
	dlm_lock_t *lp = (dlm_lock_t *) lock;

	unhold_null_lock(lp);
	dlm_del_lvb(lp);
}

/**
 * dlm_sync_lvb - sync out the value of a lvb
 * @lock: the lock the LVB is associated with
 * @lvb: the lock value block
 *
 */

void lm_dlm_sync_lvb(lm_lock_t *lock, char *lvb)
{
	dlm_lock_t *lp = (dlm_lock_t *) lock;

	if (lp->cur != DLM_LOCK_EX)
		return;

	init_completion(&lp->uast_wait);
	set_bit(LFL_SYNC_LVB, &lp->flags);

	lp->req = DLM_LOCK_EX;
	lp->lkf = make_flags(lp, 0, lp->cur, lp->req);

	do_dlm_lock(lp, NULL);
	wait_for_completion(&lp->uast_wait);
}

/**
 * process_submit - make DLM lock requests from dlm_async thread
 * @lp: DLM Lock
 *
 */

void process_submit(dlm_lock_t *lp)
{
	struct dlm_range range, *r = NULL;

	if (lp->posix) {
		range.ra_start = lp->posix->start;
		range.ra_end = lp->posix->end;
		r = &range;
	}

	do_dlm_lock(lp, r);
}

void lm_dlm_submit_delayed(dlm_t *dlm)
{
	dlm_lock_t *lp, *safe;

	spin_lock(&dlm->async_lock);

	list_for_each_entry_safe(lp, safe, &dlm->delayed, dlist) {
		if (lp->type != QUEUE_LOCKS_BLOCKED)
			continue;

		lp->type = 0;
		list_del(&lp->dlist);
		list_add_tail(&lp->slist, &dlm->submit);

		clear_bit(LFL_DLIST, &lp->flags);
		set_bit(LFL_SLIST, &lp->flags);
	}
	spin_unlock(&dlm->async_lock);
	wake_up(&dlm->wait);
}

int release_all_locks(dlm_t *dlm)
{
	dlm_lock_t *lp, *safe;
	int count = 0;

	spin_lock(&dlm->async_lock);
	list_for_each_entry_safe(lp, safe, &dlm->all_locks, all_list) {
		list_del(&lp->all_list);

		if (lp->lvb && lp->lvb != junk_lvb)
			kfree(lp->lvb);
		kfree(lp);
		count++;
	}
	spin_unlock(&dlm->async_lock);

	return count;
}

