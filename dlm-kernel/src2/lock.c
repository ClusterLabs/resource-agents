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

/* DLM locking routines to replace locking.c/lockqueue.c
   This file should be split up. */


/* Central locking logic has four stages:

   dlm_lock()
   dlm_unlock()

   request_lock(ls, lkb)
   convert_lock(ls, lkb)
   unlock_lock(ls, lkb)
   cancel_lock(ls, lkb)

   _request_lock(r, lkb)
   _convert_lock(r, lkb)
   _unlock_lock(r, lkb)
   _cancel_lock(r, lkb)

   do_request(r, lkb)
   do_convert(r, lkb)
   do_unlock(r, lkb)
   do_cancel(r, lkb)


   Stage 1 (lock, unlock) is mainly about checking input args and
   splitting into one of the four main operations:

       dlm_lock          = request_lock
       dlm_lock+CONVERT  = convert_lock
       dlm_unlock        = unlock_lock
       dlm_unlock+CANCEL = cancel_lock

   Stage 2, xxxx_lock(), just finds and locks the relevant rsb which is
   provided to the next stage.

   Stage 3, _xxxx_lock(), determines if the operation is local or remote.
   When remote, it calls send_xxxx(), when local it calls do_xxxx().

   Stage 4, do_xxxx(), is the guts of the operation.  It manipulates the
   given rsb and lkb and queues callbacks.


   For remote operations, the send_xxxx() results in the corresponding
   do_xxxx() function being executed on the remote node.  The connecting
   send/receive calls on local (L) and remote (R) nodes:

   L: send_xxxx()              ->  R: receive_xxxx()
                                   R: do_xxxx()
   L: receive_xxxx_reply()     <-  R: send_xxxx_reply()
*/


/*
 * Threads cannot use the lockspace while it's being recovered
 */

void lock_recovery(struct dlm_ls *ls)
{
	down_read(&ls->ls_in_recovery);
}

void unlock_recovery(struct dlm_ls *ls)
{
	up_read(&ls->ls_in_recovery);
}

int lock_recovery_try(struct dlm_ls *ls)
{
	return down_read_trylock(&ls->ls_in_recovery);
}


/*
 * Basic operations on rsb's and lkb's
 */

/*
 * Find rsb in rsbtbl (hash table) and potentially create/add one.
 *
 * ref++
 * MASTER: only return a master rsb (released or not)
 * REUSE:  re-use an rsb that's been released
 * CREATE: create/add rsb if one isn't found, ref = 1
 */

int find_rsb(struct dlm_ls *ls, struct dlm_lkb *lkb, char *name, int namelen,
	     unsigned int flags, struct dlm_rsb **r_ret);
{
}

/*
 * Remove from rsbtbl (hash table), send a dir remove message if master, free
 */

void release_rsb(struct dlm_rsb *r)
{
}

/*
 * ref--, when ref reaches 0, flag and set time for release
 */

void put_rsb(struct dlm_rsb *r)
{
}

/*
 * ref++
 */

void hold_rsb(struct dlm_rsb *r)
{
}

/*
 * Called periodically to release rsb's that are due
 * Probably requires a new thread to wake up and do this.
 */

void shrink_rsb_cache(struct dlm_ls *ls)
{
}

/*
 * exclusive access to rsb and all its locks
 */

void lock_rsb(struct dlm_rsb *r)
{
	down(&r->res_sema);
}

void unlock_rsb(struct dlm_rsb *r)
{
	up(&r->res_sema);
}

/*
 * Allocate lkb, assign it a lkid, add to lkbtbl (hash table), ref = 1
 */

int create_lkb(struct dlm_ls *ls, struct dlm_lkb **lkb_ret)
{
}

/*
 * ref++
 */

int find_lkb(struct dlm_ls *ls, uint32_t lkid, struct dlm_lkb **lkb_ret)
{
}

/*
 * ref--, when ref reaches 0, remove from lkbtbl, free
 */

void put_lkb(struct dlm_lkb *lkb)
{
}

/*
 * ref++, add lkb to rsb's grant/convert/wait queue
 */

void add_lkb(struct dlm_rsb *r, struct dlm_lkb *lkb, int qtype)
{
}

/*
 * ref--, remove from rsb's grant/convert/wait queue
 */

void del_lkb(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
}

/*
 * ref++, add lkb to lockspace ast list, set lkb_ast_type
 */

void add_ast_list(struct dlm_lkb *lkb, int type)
{
}

/*
 * ref++, add lkb to lockspace's list of lkb's waiting for remote replies
 *        set lkb_wait_type to mstype
 */

void add_to_waiters(struct dlm_lkb *lkb, int mstype)
{
}

/*
 * ref--, remove lkb from lockspace's list of lkb's waiting for remote replies
 *        clear lkb_wait_type, return an error if lkb is not on waiters list
 */

int remove_from_waiters(dlm_lkb *lkb)
{
}


/*
 * check and set input args
 * FIXME: is it safe to check lkb fields without a lock_rsb() ?
 */

int set_lock_args(struct dlm_ls *ls, struct dlm_lkb *lkb, int mode,
		  struct dlm_lksb *lksb, uint32_t flags, int namelen,
		  uint32_t parent_lkid, void *ast, void *astarg, void *bast,
		  struct dlm_range *range)
{
	int rv = -EINVAL;

	/* check for invalid arg usage */

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

	if (flags & DLM_LKF_EXPEDITE && mode != DLM_LOCK_NL)
		goto out;

	if (!ast || !lksb)
		goto out;

	if (flags & DLM_LKF_VALBLK && !lksb->sb_lvbptr)
		goto out;

	/* FIXME: parent/child locks not yet supported */
	if (parent_lkid)
		goto out;

	/* checks specific to conversions */

	if (flags & DLM_LKF_CONVERT) {

		if (!lksb->sb_lkid)
			goto out;

		if (lkb->lkb_flags & DLM_IFL_MSTCPY)
			goto out;

		if (flags & DLM_LKF_QUECVT &&
		    !__quecvt_compat_matrix[lkb->lkb_grmode + 1][mode + 1])
			goto out;

		rv = -EBUSY;
		if (lkb->lkb_status != DLM_LKSTS_GRANTED)
			goto out;
	}

	/* copy input values into lkb */

	lkb->lkb_exflags = flags;
	lkb->lkb_sbflags = 0;
	lkb->lkb_astaddr = ast;
	lkb->lkb_astparam = (long) astarg;
	lkb->lkb_bastaddr = bast;
	lkb->lkb_rqmode = mode;
	lkb->lkb_lksb = lksb;
	lkb->lkb_lvbptr = lksb->sb_lvbptr;
	lkb->lkb_ownpid = (int) current->pid;

	if (range) {
		if (!lkb->lkb_range) {
			rv = -ENOMEM;
			lkb->lkb_range = allocate_range(ls);
			if (!lkb->lkb_range)
				goto out;

			/* This is needed for conversions that contain ranges
			   where the original lock didn't but it's harmless for
			   new locks too. */

			lkb->lkb_range[GR_RANGE_START] = 0LL;
			lkb->lkb_range[GR_RANGE_END] = 0xffffffffffffffffULL;
		}

		lkb->lkb_range[RQ_RANGE_START] = start;
		lkb->lkb_range[RQ_RANGE_END] = end;
	}

	/* return lkid in lksb for new locks */

	if (!lksb->sb_lkid)
		lksb->sb_lkid = lkb->lkb_id;
	rv = 0;
 out:
	return rv;
}

int set_unlock_args(struct dlm_ls *ls, struct dlm_lks *lkb, uint32_t flags,
		    struct dlm_lksb *lksb, void *astarg)	
{
	int rv = -EINVAL;

	if (lkb->lkb_flags & DLM_IFL_MSTCPY)
		goto out;

	if (flags & DLM_LKF_CANCEL && lkb->lkb_status == DLM_LKSTS_GRANTED)
		goto out;

	if (!(flags & DLM_LKF_CANCEL) && lkb->lkb_status != DLM_LKSTS_GRANTED)
		goto out;

	rv = -EBUSY;
	if (lkb->lkb_wait_type)
		goto out;

	lkb->lkb_exflags = flags;
	lkb->lkb_sbflags = 0;

	rv = 0;
 out:
	return rv;
}


/*
 * Two stage 1 varieties:  dlm_lock() and dlm_unlock()
 * These are called on the node making the request.
 */

int dlm_lock(void *lockspace,
	     int mode,
	     struct dlm_lksb *lksb,
	     uint32_t flags,
	     void *name,
	     unsigned int namelen,
	     uint32_t parent_lkid,
	     void (*ast) (void *astarg),
	     void *astarg,
	     void (*bast) (void *astarg, int mode),
	     struct dlm_range *range)
{
	struct dlm_ls *ls;

	ls = find_lockspace_local(lockspace);
	if (!ls)
		return -EINVAL;

	lock_recovery(ls);

	if (flags & DLM_LKF_CONVERT)
		error = find_lkb(ls, lksb->sb_id, &lkb);
	else
		error = create_lkb(ls, &lkb);

	if (error)
		goto out;

	error = set_lock_args(ls, lkb, mode, lksb, flags, namelen, parent_lkid,
			      ast, astarg, bast, range);
	if (error)
		goto out_put;

	if (flags & DLM_LKF_CONVERT)
		error = convert_lock(ls, lkb);
	else
		error = request_lock(ls, lkb, name, namelen);

	/* the lock request is queued */
	if (error == -EBUSY)
		error = 0;

 out_put:
	put_lkb(lkb);
 out:
	unlock_recovery(ls);
	put_lockspace(ls);
	return error;
}

int dlm_unlock(void *lockspace,
	       uint32_t lkid,
	       uint32_t flags,
	       struct dlm_lksb *lksb,
	       void *astarg)
{
	ls = find_lockspace_local(lockspace);
	if (!ls)
		return -EINVAL;

	lock_recovery(ls);

	error = find_lkb(ls, lkid, &lkb);
	if (error)
		goto out;

	error = set_unlock_args(ls, lkb, flags, lksb, astarg);
	if (error)
		goto out_put;

	if (flags & DLM_LKF_CANCEL)
		error = cancel_lock(ls, lkb);
	else
		error = unlock_lock(ls, lkb);

 out_put:
	put_lkb(lkb);
 out:
	unlock_recovery(ls);
	put_lockspace(ls);
	return error;
}


/* set_master(r, lkb) -- set the master nodeid of a resource

   _request_lock calls this in the context of an original lock request,
   or in the context of dlm_recvd after receiving a master lookup reply
   (which may be positive or negative).

   The purpose of this function is to set the nodeid field in the given
   lkb using the nodeid field in the given rsb.  If the rsb's nodeid is
   known, it can just be copied to the lkb and the function will return
   0.  If the rsb's nodeid is _not_ known, it needs to be looked up
   before it can be copied to the lkb.  If the rsb's nodeid is known,
   but is uncertain, then we may wait to copy it to the lkb until we
   know for certain.

   [Note: an rsb's nodeid may change (and is uncertain) after we
    look it up but before we are granted a lock on it.]
   
   When the rsb nodeid is being looked up, the lkb is kept on the
   lockqueue waiting for the lookup reply and other lkb's are kept
   on the wait queue until the master is certain.  When the rsb
   nodeid is being "tried out", the trial lkb is pointed to by
   res_trial_lkb, is kept on the lockqueue waiting for the request
   reply and other lkb's are kept on the wait queue until the
   master is certain.

   In the case where the rsb's nodeid is uncertain, we allow the first
   lkb calling this function to go ahead and "try it out".  Other lkb's
   will have to wait on the wait queue until the rsb nodeid is confirmed.
   When the rsb's uncertain nodeid is copied to the trial lkb's nodeid,
   that lkb request may end up failing if the nodeid is wrong.  In this
   failure case, the rsb nodeid will need to be looked up again in the
   context of this lkb.

   Cases:
   1. we've no idea who master is
   2. we know who master /was/ in the past (r was tossed and remote)
   3. we're certain of who master is (we hold granted locks)
   4. we're in the process of looking up master from dir
   5. we've been told who the master is (from dir) but it could
      change by the time we send it a lock request (only if we
      were told the master is remote)
   6. 5 and we have a request outstanding to this uncertain master

   How do we track these states?
   a. res_nodeid == 0   we are master
   b. res_nodeid > 0    remote node is master
   c. res_nodeid == -1  no idea who master is
   d. lkb_nodeid == 0   we are master, res_nodeid is certain
   e. lkb_nodeid != 0
   f. res_flags MASTER            we are master (a. should be true)
   g. res_flags MASTER_UNCERTAIN  (b. should be true in this case)
   h. res_flags MASTER_WAIT

   1. c
   2. b, g
   3. b or a
   4. c, h
   5. b, g
   6. b, g, h

   Notes:
   - A new rsb should have res_nodeid set to -1 and clear
     WAIT, UNCERTAIN, MASTER flags.

   - A released rsb returned by find() should have res_nodeid of
     0 or > 0.  If 0, MASTER should be set, WAIT, UNCERTAIN clear.
     If > 0, MASTER and WAIT should be clear, UNCERTAIN set.

   - The UNCERTAIN and WAIT flags are cleared when we are notified
     that our request was queued on the remote master (granted or waiting).

   - When a trial lkb is requested again it means the trial failed and
     the uncertain rsb master was wrong.  The rsb nodeid goes from being
     uncertain to unknown, and the lkb goes from being a trial to just
     waiting for the lookup reply.  The lkb will become a trial again
     when the lookup reply is received and the rsb nodeid is tried again.

   Return values:
   0: nodeid is set in rsb/lkb and the caller should go ahead and use it
   1: the rsb master is not available and the lkb has been placed on
      a wait queue
   -EXXX: there was some error in processing
*/
 
int set_master(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	if (r->res_trial_lkb == lkb) {
		r->res_nodeid = -1;
		lkb->lkb_nodeid = -1;
		r->res_trial_lkb = NULL;
		clear_bit(RESFL_MASTER_WAIT, &r->res_flags);
		clear_bit(RESFL_MASTER_UNCERTAIN, &r->res_flags);
	}

	if (r->res_nodeid == -1) {
		if (!test_bit(RESFL_MASTER_WAIT, &r->res_flags)) {
			error = send_lookup(r, lkb);
			if (error)
				return error;
			set_bit(RESFL_MASTER_WAIT, &r->res_flags);
		} else
			list_add_tail(&lkb->lkb_rsb_lookup, &r->res_lookup);
		return 1;
	}

	if (r->res_nodeid == 0) {
		lkb->lkb_nodeid = 0;
		return 0;
	}

	if (r->res_nodeid > 0) {
		if (!test_bit(RESFL_MASTER_UNCERTAIN, &r->res_flags)) {
			lkb->lkb_nodeid = r->res_nodeid;
			return 0;
		}

		/* When the rsb nodeid is uncertain we let the first
		   lkb try it out while any others wait.  If the trial
		   fails we'll end up back in this function in the first
		   condition. */

		if (!test_bit(RESFL_MASTER_WAIT, &r->res_flags)) {
			set_bit(RESFL_MASTER_WAIT, &r->res_flags);
			lkb->lkb_nodeid = r->res_nodeid;
			r->res_trial_lkb = lkb;
			return 0;
		} else {
			list_add_tail(&lkb->lkb_rsb_lookup, &r->res_lookup);
			return 1;
		}
	}

	return -1;
}

/* confirm_master -- confirm an rsb's master nodeid

   This is called when a master node sends a non-EINVAL reply to a
   request.  It means that we have the rsb nodeid correct, which is
   interesting if we're waiting to confirm an uncertain rsb nodeid.
 
   The "rv" return value is the result the master sent back for the
   request.  It could be -EAGAIN (the lkb would block), -EBUSY (the
   lkb was put on the wait queue), or 0 (the lkb was granted.)
 
   If the rsb nodeid is uncertain and the rv is -EAGAIN, we still
   can't be certain so we need to do another trial lkb.  If the
   rsb is uncertain and the rv is -EBUSY or 0, we are now certain
   and can send any lkb requests waiting on res_lookup.
*/

void confirm_master(struct dlm_rsb *r, int rv)
{
	if (!test_bit((RESFL_MASTER_UNCERTAIN, &r->res_flags)))
		return;

	if (rv == 0 || rv == -EBUSY) {
		clear_bit(RESFL_MASTER_UNCERTAIN, &r->res_flags);
		clear_bit(RESFL_MASTER_WAIT, &r->res_flags);

		list_for_each_entry_safe(lkb, safe, &r->res_lookup,
					 lkb_rsb_lookup) {
			list_del(&lkb->lkb_rsb_lookup);
			_request_lock(r, lkb);
			schedule();
		}
		return;
	}

	if (rv == -EAGAIN) {
		/* unsure about this case where lookup list is empty */
		if (list_empty(&r->res_lookup)) {
			printk("confirm_master: lookup list empty\n");
			return;
		}

		/* clearing MASTER_WAIT allows this lkb to become the
		   trial lkb when _request_lock calls set_master */

		lkb = list_entry(r->res_lookup.next);
		list_del(&lkb->lkb_rsb_lookup);
		clear_bit(RESFL_MASTER_WAIT, &r->res_flags);
		_request_lock(r, lkb);
	}
}

/*
 * Four stage 2 varieties:
 * request_lock(), convert_lock(), unlock_lock(), cancel_lock()
 * These are only called on the node making the request.
 */

int request_lock(struct dlm_ls *ls, struct dlm_lkb *lkb, char *name, int len)
{
	struct dlm_rsb *r;
	int error;

	error = find_rsb(ls, lkb, name, len, CREATE | REUSE, &r);
	if (error)
		return error;

	lock_rsb(r);

	lkb->lkb_resource = r;
	error = _request_lock(r, lkb);

	unlock_rsb(r);
	put_rsb(r);

	lkb->lkb_lksb->sb_id = lkb->lkb_id;
	return error;
}

int convert_lock(struct dlm_ls *ls, struct dlm_lkb *lkb)
{
	struct dlm_rsb *r;
	int error;

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	error = _convert_lock(r, lkb);

	unlock_rsb(r);
	put_rsb(r);
	return error;
}

int unlock_lock(struct dlm_ls *ls, struct dlm_lkb *lkb)
{
	struct dlm_rsb *r;
	int error;

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	error = _unlock_lock(r, lkb);

	unlock_rsb(r);
	put_rsb(r);
	return error;
}

int cancel_lock(struct dlm_ls *ls, struct dlm_lkb *lkb)
{
	struct dlm_rsb *r;
	int error;

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	error = _cancel_lock(r, lkb);

	unlock_rsb(r);
	put_rsb(r);
	return error;
}

/*
 * Four stage 3 varieties:
 * _request_lock(), _convert_lock(), _unlock_lock(), _cancel_lock()
 * These are called on the node making the request.
 */

/* add a new lkb to a possibly new rsb, called by requesting process */

int _request_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error;

	/* set_master: sets lkb nodeid from r */
	   
	error = set_master(r, lkb);
	if (error < 0) 
		goto out;
	if (error) {
		error = 0;
		goto out;
	}

	if (is_remote(r))
		/* receive_request() calls do_request() on remote node */
		error = send_request(r, lkb);
	else
		error = do_request(r, lkb);
 out:
	return error;
}

/* change some property of an existing lkb, e.g. mode, range */

int _convert_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error;

	if (is_remote(r))
		/* receive_convert() calls do_convert() on remote node */
		error = send_convert(r, lkb);
	else
		error = do_convert(r, lkb);

	return error;
}

/* remove an existing lkb from the granted queue */

int _unlock_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error;

	if (is_remote(r))
		/* receive_unlock() calls call do_unlock() on remote node */
		error = send_unlock(r, lkb);
	else
		error = do_unlock(r, lkb);

	return error;
}

/* remove an existing lkb from the convert or wait queue */

int _cancel_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error;

	if (is_remote(r))
		/* receive_cancel() calls do_cancel() on remote node */
		error = send_cancel(r, lkb);
	else
		error = do_cancel(r, lkb);

	return error;
}


/*
 * Special-purpose routines called by the core locking functions.
 * Deal with "details" whereas primary routines deal with locking "logic".
 */

void queue_cast(struct dlm_rsb *r, struct dlm_lkb *lkb, int rv)
{
	if (is_master_copy(lkb))
		return;

	lkb->lkb_lksb.sb_status = rv;
	lkb->lkb_lksb.sb_flags = lkb->lkb_sbflags;

	add_ast_list(lkb, CAST);
}

void queue_bast(struct dlm_rsb *r, struct dlm_lkb *lkb, int rqmode)
{
	if (is_master_copy(lkb))
		send_bast(lkb, rqmode);
	else {
		lkb->lkb_bastmode = rqmode;
		add_ast_list(lkb, BAST);
	}
}

/* only called on master node, lkb can be local or remote */

void set_lvb_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int b;

	if (!r->res_lvbptr)
		r->res_lvbptr = allocate_lvb(r->res_ls);

	/* b=1 lvb returned to caller
	   b=0 lvb written to rsb or invalidated
	   b=-1 do nothing */

	b =  __lvb_operations[lkb->lkb_grmode + 1][lkb->lkb_rqmode + 1];

	if (b == 1) {
		if (!lkb->lkb_lvbptr)
			return;

		if (!(lkb->lkb_exflags & DLM_LKF_VALBLK))
			return;

		memcpy(lkb->lkb_lvbptr, r->res_lvbptr, DLM_LVB_LEN);
		lkb->lkb_lvbseq = r->res_lvbseq;

	} else if (b == 0) {
		if (lkb->lkb_exflags & DLM_LKF_IVVALBLK) {
			set_bit(RESFL_VALNOTVALID, &r->res_flags);
			return;
		}

		if (!lkb->lkb_lvbptr)
			return;

		if (!(lkb->lkb_exflags & DLM_LKF_VALBLK))
			return;

		memcpy(r->res_lvbptr, lkb->lkb_lvbptr, DLM_LVB_LEN);
		r->res_lvbseq++;
		clear_bit(RESFL_VALNOTVALID, &r->res_flags);
	}

	if (test_bit(RESFL_VALNOTVALID, &r->res_flags))
		lkb->lkb_sbflags |= DLM_SBF_VALNOTVALID;
}

void set_lvb_unlock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	if (lkb->lkb_grmode < DLM_LOCK_PW)
		return;

	if (lkb->lkb_exflags & DLM_LKF_IVVALBLK) {
		set_bit(RESFL_VALNOTVALID, &r->res_flags);
		return;
	}

	if (!lkb->lkb_lvbptr)
		return;

	if (!(lkb->lkb_exflags & DLM_LKF_VALBLK))
		return;

	if (!r->res_lvbptr)
		r->res_lvbptr = allocate_lvb(r->res_ls);

	memcpy(r->res_lvbptr, lkb->lkb_lvbptr, DLM_LVB_LEN);
	r->res_lvbseq++;
	clear_bit(RESFL_VALNOTVALID, &r->res_flags);
}

/* called on non-master node for a remotely mastered lock */

void set_lvb_lock_local(struct dlm_rsb *r, struct dlm_lkb *lkb,
			struct dlm_message *ms)
{
	if (!lkb->lkb_lvbptr)
		return;

	if (!(lkb->lkb_exflags & DLM_LKF_VALBLK))
		return;

	if (!(lkb->lkb_flags & DLM_IFL_RETURNLVB))
		return;

	memcpy(lkb->lkb_lvb, ms->m_lvb, DLM_LVB_SIZE);
	lkb->lkb_lvbseq = ms->m_lvbseq;
}

/* "remove_lock" varieties used for unlock */

void _remove_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	del_lkb(r, lkb);
	lkb->lkb_grmode = DLM_LOCK_IV;
}

void remove_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	set_lvb_unlock(r, lkb);
	_remove_lock(r, lkb);
}

void remove_lock_local(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	_remove_lock(r, lkb);
}

/* "revert_lock" varieties used for cancel */

void revert_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	del_lkb(r, lkb);
	lkb->lkb_rqmode = DLM_LOCK_IV;
	add_lkb(r, lkb, GRANTED);
}

void revert_lock_local(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	revert_lock(r, lkb);
}

/* "grant_lock" varieties used for request and convert */

void _grant_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	if (lkb->lkb_grmode != lkb->lkb_rqmode) {
		lkb->lkb_grmode = lkb->lkb_rqmode;
		del_lkb(r, lkb);
		add_lkb(r, lkb, GRANTED);
	}

	lkb->lkb_rqmode = DLM_LOCK_IV;

	if (lkb->lkb_range) {
		lkb->lkb_range[GR_RANGE_START] = lkb->lkb_range[RQ_RANGE_START];
		lkb->lkb_range[GR_RANGE_END] = lkb->lkb_range[RQ_RANGE_END];
	}
}

/* called on master node when processing a local or remote request/convert */

void grant_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	set_lvb_lock(r, lkb);
	_grant_lock(r, lkb);
	lkb->lkb_highbast = 0;
}

/* called on local node after a lock has been granted on the master node */

void grant_lock_local(struct dlm_rsb *r, struct dlm_lkb *lkb,
		      struct dlm_message *ms)
{
	set_lvb_lock_local(r, lkb, ms);
	_grant_lock(r, lkb);
}

/* called by grant_pending_locks() which means an async grant message must
   be sent to the requesting node in addition to granting the lock.
   i.e. it's not granted in the context of the initial request/convert */

void grant_lock_pending(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	grant_lock(r, lkb);
	if (is_master_copy(lkb))
		send_grant(r, lkb);
}

int grant_pending_convert(struct dlm_rsb *r, int high)
{
	struct dlm_lkb *lkb, *s;
	int hi, demoted, quit, grant_restart, demote_restart;

	quit = 0;
 restart:
	grant_restart = 0;
	demote_restart = 0;
	hi = DLM_LOCK_IV;

	list_for_each_entry_safe(lkb, s, &r->res_convertqueue, lkb_statequeue) {
		demoted = is_demoted(lkb);
		if (can_be_granted(r, lkb, FALSE)) {
			grant_lock_pending(r, lkb);
			grant_restart = 1;
		} else {
			hi = MAX(lkb->lkb_rqmode, hi);
			if (!demoted && is_demoted(lkb))
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

int grant_pending_wait(struct dlm_rsb *r, int high)
{
	struct dlm_lkb *lkb, *s;

	list_for_each_entry_safe(lkb, s, &r->res_waitqueue, lkb_statequeue) {
		if (can_be_granted(r, lkb, FALSE))
			grant_lock_pending(r, lkb);
                else
			high = MAX(lkb->lkb_rqmode, high);
	}

	return high;
}

int grant_pending_locks(struct dlm_rsb *r)
{
	struct dlm_lkb *lkb, *s;
	int high = DLM_LOCK_IV;

	high = grant_pending_queue(&r->res_convertqueue, high);
	high = grant_pending_queue(&r->res_waitqueue, high);

	if (high == DLM_LOCK_IV)
		return 0;

	/*
	 * If there are locks left on the wait/convert queue then send blocking
	 * ASTs to granted locks that are blocking
	 *
	 * FIXME: This might generate some spurious blocking ASTs for range
	 * locks.
	 *
	 * FIXME: the highbast < high comparison is not always valid.
	 */

	list_for_each_entry_safe(lkb, s, &r->res_grantqueue, lkb_statequeue) {
		if (lkb->lkb_bastaddr && (lkb->lkb_highbast < high) &&
		    !__dlm_compat_matrix[lkb->lkb_grmode+1][high+1]) {
			queue_bast(r, lkb, high);
			lkb->lkb_highbast = high;
		}
	}

	return 0;
}

int can_be_granted(struct dlm_rsb *r, struct dlm_lkb *lkb, int now)
{
	/* same as current, but DEMOTED set in lkb_sbflags and
	   check DLM_LKF flags in lkb->lkb_exflags */
}

int can_be_queued(struct dlm_lkb *lkb)
{
	return (!(lkb->lkb_exflags & DLM_LKF_NOQUEUE));
}

int force_blocking_asts(struct dlm_lkb *lkb)
{
	return (lkb->lkb_exflags & DLM_LKF_NOQUEUEBAST);
}

int is_demoted(struct dlm_lkb *lkb)
{
	return (lkb->lkb_sbflags & DLM_SBF_DEMOTED);
}

int is_remote(struct dlm_rsb *r)
{
}


/*
 * Four stage 4 varieties:
 * do_request(), do_convert(), do_unlock(), do_cancel()
 * These are called on the master node for the given lock and
 * from the central locking logic.
 */

int do_request(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error = 0;

	if (can_be_granted(r, lkb, TRUE))
		grant_lock(r, lkb);
		queue_cast(r, lkb, 0);
		goto out;
	}

	if (can_be_queued(lkb)) {
		error = -EBUSY;
		add_lkb(r, lkb, WAITING);
		send_blocking_asts(r, lkb);
		goto out;
	}

	error = -EAGAIN;
	if (force_blocking_asts(lkb))
		send_blocking_asts_all(r, lkb);
	queue_cast(r, lkb, -EAGAIN);

 out:
	return error;
}

int do_convert(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error = 0;

	/* changing an existing lock may allow others to be granted */

	if (can_be_granted(r, lkb, TRUE))
		grant_lock(r, lkb);
		queue_cast(r, lkb, 0);
		grant_pending_locks(r);
		goto out;
	}

	if (can_be_queued(lkb)) {
		if (is_demoted(lkb))
			grant_pending_locks(r, lkb);
		error = -EBUSY;
		add_lkb(r, lkb, CONVERT);
		send_blocking_asts(r, lkb);
		goto out;
	}

	error = -EAGAIN;
	if (force_blocking_asts(lkb))
		send_blocking_asts_all(r, lkb);
	queue_cast(r, lkb, -EAGAIN);

 out:
	return error;
}

int do_unlock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	remove_lock(r, lkb);
	grant_pending_locks(r);
	queue_cast(r, lkb, -DLM_EUNLOCK);
	return -DLM_EUNLOCK;
}

int do_cancel(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	revert_lock(r, lkb);
	grant_pending_locks(r);
	queue_cast(r, lkb, -DLM_ECANCEL);
	return -DLM_ECANCEL;
}



void send_args(struct dlm_rsb *r, struct dlm_lkb *lkb,
	       struct dlm_message *ms, int mstype)
{
	ms->m_type    = mstype;
	ms->m_lkid    = lkb->lkb_id;
	ms->m_remlkid = lkb->lkb_remid;
	ms->m_flags   = lkb->lkb_flags;
	ms->m_exflags = lkb->lkb_exflags;
	ms->m_sbflags = lkb->lkb_sbflags;
	ms->m_nodeid  = lkb->lkb_nodeid;
	ms->m_pid     = lkb->lkb_ownpid;
	ms->m_grmode  = lkb->lkb_grmode;
	ms->m_rqmode  = lkb->lkb_rqmode;
	ms->m_lvbseq  = lkb->lkb_lvbseq;

	if (lkb->lkb_bastaddr)
		ms->m_asts |= AST_BAST;
	if (lkb->lkb_astaddr)
		ms->m_asts |= AST_COMP;

	if (lkb->lkb_parent) {
		ms->m_lkid_parent = lkb->lkb_parent->lkb_id;
		ms->m_remlkid_parent = lkb->lkb_parent->lkb_remid;
	}

	if (lkb->lkb_range) {
		ms->m_range[0] = lkb->lkb_range[RQ_RANGE_START];
		ms->m_range[1] = lkb->lkb_range[RQ_RANGE_END];
	}

	memcpy(ms->ms_lvb, lkb->lkb_lvbptr, DLM_LVB_SIZE);
	
	if (type == DLM_MSG_REQUEST || type == DLM_MSG_LOOKUP)
		memcpy(ms->m_name, r->res_name, r->res_length);
}

int send_common(struct dlm_rsb *r, struct dlm_lkb *lkb, int mstype)
{
	struct dlm_message *ms;
	int to_nodeid, error;

	add_to_waiters(lkb, mstype);

	to_nodeid = r->res_nodeid;

	error = create_message(to_nodeid, &ms);
	if (error)
		goto fail;

	send_args(r, lkb, ms, mstype);

	error = send_message(ms);
	if (error)
		goto fail;
	kfree(ms);
	return 0;

 fail:
	kfree(ms);
	remove_from_waiters(lkb);
	return error;
}

int send_request(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	return send_common(r, lkb, DLM_MSG_REQUEST);
}

int send_convert(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	return send_common(r, lkb, DLM_MSG_CONVERT);
}

int send_unlock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	return send_common(r, lkb, DLM_MSG_UNLOCK);
}

int send_cancel(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	return send_common(r, lkb, DLM_MSG_CANCEL);
}

int send_lookup(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	struct dlm_message *ms;
	int to_nodeid, error;

	add_to_waiters(lkb, DLM_MSG_LOOKUP);

	to_nodeid = dlm_dir_nodeid(r);

	error = create_message(to_nodeid, &ms);
	if (error)
		goto fail;

	send_args(r, lkb, ms, DLM_MSG_LOOKUP);

	error = send_message(ms);
	if (error)
		goto fail;
	kfree(ms);
	return 0;

 fail:
	kfree(ms);
	remove_from_waiters(lkb);
	return error;
}

int send_grant(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	struct dlm_message *ms;
	int to_nodeid, error;

	to_nodeid = lkb->lkb_nodeid;

	error = create_message(to_nodeid, &ms);
	if (error)
		goto out;

	send_args(r, lkb, ms, DLM_MSG_GRANT);

	ms->m_result = 0;

	error = send_message(ms);
 out:
	kfree(ms);
	return error;
}

void send_request_reply(struct dlm_rsb *r, struct dlm_lkb *lkb, int rv)
{
	struct dlm_message *ms;
	int to_nodeid, error;

	to_nodeid = lkb->lkb_nodeid;

	error = create_message(to_nodeid, &ms);
	if (error)
		goto out;

	ms->m_result = rv;

	error = send_message(ms);
 out:
	kfree(ms);
	return error;
}

int send_convert_reply(struct dlm_rsb *r, struct dlm_lkb *lkb, int rv)
{
}

int send_unlock_reply(struct dlm_rsb *r, struct dlm_lkb *lkb, int rv)
{
}

int send_cancel_reply(struct dlm_rsb *r, struct dlm_lkb *lkb, int rv)
{
}

int send_lookup_reply(int to_nodeid, int ret_nodeid, int rv)
{
	struct dlm_message *ms;
	int error;

	error = create_message(to_nodeid, &ms);
	if (error)
		goto out;

	ms->m_result = rv;
	ms->m_nodeid = ret_nodeid;

	error = send_message(ms);
 out:
	kfree(ms);
	return error;
}



/* which args we save from a received message depends heavily on the type
   of message, unlike the send side where we can just send everything
   for every type of message */

void receive_flags(lkb, ms)
{
	lkb->lkb_exflags = ms->m_exflags;
	lkb->lkb_flags = (lkb->lkb_flags & 0xFFFF0000) |
		         (ms->m_flags & 0x0000FFFF);
}

void receive_flags_reply(lkb, ms)
{
	lkb->lkb_sbflags = ms->m_sbflags;
	lkb->lkb_flags = (lkb->lkb_flags & 0xFFFF0000) |
		         (ms->m_flags & 0x0000FFFF);
}

int receive_request_args(lkb, ms, nodeid)
{
	/* needed for receive_request where we need to fill in
	   a bunch of the remote lkb's values into our mstcpy lkb */

	lkb->lkb_grmode = DLM_LOCK_IV;
	lkb->lkb_rqmode = ms->m_rqmode;
	lkb->lkb_astaddr = (void *) (long) (ms->m_asts & AST_COMP);
	lkb->lkb_bastaddr = (void *) (long) (ms->m_asts & AST_BAST);
	lkb->lkb_nodeid = nodeid;
	lkb->lkb_remid = ms->m_lkid;

	if () {
		/* allocate and copy the lvb */
	}

	if () {
		/* set range */
	}

	return 0;
}

int receive_convert_args(lkb, ms, nodeid)
{
}

int receive_unlock_args(lkb, ms, nodeid)
{
}

int receive_cancel_args(lkb, ms, nodeid)
{
}

int receive_request_reply_args(lkb, ms, nodeid)
{
	lkb->lkb_remid = ms->m_lkid;
	return 0;
}

void receive_request(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error, namelen;

	error = create_lkb(ls, &lkb);
	if (error)
		goto out;

	receive_request_args(lkb, ms);
	receive_flags(lkb, ms);
	namelen = receive_namelen(ms);

	error = find_rsb(ls, lkb, ms->m_name, namelen, MASTER | REUSE, &r);
	if (error)
		goto out;

	lock_rsb(r);

	lkb->lkb_resource = r;
	error = do_request(r, lkb);

	/* the queue_cast called by do_request is null for remote locks */
 out:
	send_request_reply(r, lkb, error);

	unlock_rsb(r);
	put_rsb(r);
	put_lkb(lkb);
}

void receive_convert(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remlkid, &lkb);
	if (error)
		goto out;

	receive_convert_args(lkb, ms);
	receive_flags(lkb, ms);

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	error = do_convert(r, lkb);
 out:
	send_convert_reply(r, lkb, error);

	unlock_rsb(r);
	put_rsb(r);
	put_lkb(lkb);
}

void receive_unlock(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remlkid, &lkb);
	if (error)
		goto out;

	receive_unlock_args(lkb, ms);
	receive_flags(lkb, ms);

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	error = do_unlock(r, lkb);
 out:
	send_unlock_reply(r, lkb, error);

	unlock_rsb(r);
	put_rsb(r);
	put_lkb(lkb);
}

void receive_cancel(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remlkid, &lkb);
	if (error)
		goto out;

	receive_cancel_args(lkb, ms);
	receive_flags(lkb, ms);

	r = lkb->resource;

	hold_rsb(r);
	lock_rsb(r);

	error = do_cancel(r, lkb);
 out:
	send_cancel_reply(r, lkb, error);

	unlock_rsb(r);
	put_rsb(r);
	put_lkb(lkb);
}

void receive_request_reply(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remlkid, &lkb);
	if (error) {
		log_error(ls, "receive_request_reply no lkb");
		return;
	}

	error = remove_from_waiters(lkb);
	if (error) {
		log_error(ls, "receive_request_reply not on lockqueue");
		goto out;
	}

	/* this is the value returned from do_request() on the master */
	error = ms->m_result;

	/* now follow pattern of xxx_lock() functions */

	r = lkb->lkb_resource;
	hold_rsb(r);
	lock_rsb(r);

	switch (error) {
	case -EINVAL:
		/* the remote node wasn't the master as we thought */
		_request_lock(r, lkb);
		break;

	case -EAGAIN:
		/* request would block (be queued) on remote master */
		queue_cast(r, lkb, -EAGAIN);
		confirm_master(r, -EAGAIN);
		break;

	case -EBUSY:
		/* request was queued on remote master */
		receive_request_reply_args(lkb, ms);
		add_lkb(r, lkb, WAITING);
		confirm_master(r, -EBUSY);
		break;

	case 0:
		/* request was granted on remote master */
		receive_request_reply_args(lkb, ms);
		receive_flags_reply(lkb, ms);
		grant_lock_local(r, lkb, ms);
		queue_cast(r, lkb, 0);
		confirm_master(r, 0);
		break;

	default:
		log_error(ls, "receive_request_reply unknown error %d", error);
	}

	unlock_rsb(r);
	put_rsb(r);
 out:
	put_lkb(lkb);
}

void receive_convert_reply(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remlkid, &lkb);
	if (error) {
		log_error(ls, "receive_convert_reply no lkb");
		return;
	}

	error = remove_from_waiters(lkb);
	if (error) {
		log_error(ls, "receive_convert_reply not on lockqueue");
		goto out;
	}

	/* this is the value returned from do_convert() on the master */
	error = ms->m_result;

	/* now follow pattern of xxx_lock() functions */

	r = lkb->lkb_resource;
	hold_rsb(r);
	lock_rsb(r);

	switch (error) {
	case -EAGAIN:
		/* convert would block (be queued) on remote master */
		queue_cast(r, lkb, -EAGAIN);
		break;

	case -EBUSY:
		/* convert was queued on remote master */
		del_lkb(r, lkb);
		add_lkb(r, lkb, CONVERT);
		break;

	case 0:
		/* convert was granted on remote master */
		receive_flags_reply(lkb, ms);
		grant_lock_local(r, lkb, ms);
		queue_cast(r, lkb, 0);
		break;

	default:
		log_error(ls, "receive_convert_reply unknown error %d", error);
	}

	unlock_rsb(r);
	put_rsb(r);
 out:
	put_lkb(lkb);
}

void receive_unlock_reply(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remlkid, &lkb);
	if (error) {
		log_error(ls, "receive_unlock_reply no lkb");
		return;
	}

	error = remove_from_waiters(lkb);
	if (error) {
		log_error(ls, "receive_unlock_reply not on lockqueue");
		goto out;
	}

	/* this is the value returned from do_unlock() on the master */
	error = ms->m_result;

	/* now follow pattern of xxx_lock() functions */

	r = lkb->lkb_resource;
	hold_rsb(r);
	lock_rsb(r);

	switch (error) {
	case -DLM_EUNLOCK:
		receive_flags_reply(lkb, ms);
		remove_lock_local(r, lkb);
		queue_cast(r, lkb, -DLM_EUNLOCK);
		break;

	default:
		log_error(ls, "receive_unlock_reply unknown error %d", error);
	}

	unlock_rsb(r);
	put_rsb(r);
 out:
	put_lkb(lkb);
}

void receive_cancel_reply(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remlkid, &lkb);
	if (error) {
		log_error(ls, "receive_cancel_reply no lkb");
		return;
	}

	error = remove_from_waiters(lkb);
	if (error) {
		log_error(ls, "receive_cancel_reply not on lockqueue");
		goto out;
	}

	/* this is the value returned from do_cancel() on the master */
	error = ms->m_result;

	/* now follow pattern of xxx_lock() functions */

	r = lkb->lkb_resource;
	hold_rsb(r);
	lock_rsb(r);

	switch (error) {
	case -DLM_ECANCEL:
		receive_flags_reply(lkb, ms);
		revert_lock_local(r, lkb);
		queue_cast(r, lkb, -DLM_ECANCEL);
		break;

	default:
		log_error(ls, "receive_cancel_reply unknown error %d", error);
	}

	unlock_rsb(r);
	put_rsb(r);
 out:
	put_lkb(lkb);
}

void receive_grant(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remlkid, &lkb);
	if (error) {
		log_error(ls, "receive_grant no lkb");
		return;
	}

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	receive_flags_reply(lkb, ms);
	grant_lock_local(r, lkb, ms);
	queue_cast(r, lkb, 0);

	unlock_rsb(r);
	put_rsb(r);
	put_lkb(lkb);
}

void receive_lookup(struct dlm_ls *ls, struct dlm_message *ms)
{
	int len, error, ret_nodeid, dir_nodeid, from_nodeid;

	from_nodeid = ms->m_header.h_nodeid;

	len = receive_namelen(ms);

	dir_nodeid = name_to_directory_nodeid(ls, ms->m_name, namelen);
	if (dir_nodeid != dlm_our_nodeid()) {
		error = -EINVAL;
		ret_nodeid = -1;
		goto out;
	}

	error = dlm_dir_lookup(ls, from_nodeid, ms->m_name, len, &ret_nodeid);
 out:
	send_lookup_reply(from_nodeid, ret_nodeid, error);
}

void receive_lookup_reply(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remlkid, &lkb);
	if (error) {
		log_error(ls, "receive_lookup_reply no lkb");
		return;
	}

	error = remove_from_waiters(lkb);
	if (error) {
		log_error(ls, "receive_lookup_reply not on lockqueue");
		goto out;
	}

	/* this is the value returned by dlm_dir_lookup on dir node */
	error = ms->m_result;

	/* now follow pattern of xxx_lock() functions.
	   this is basically request_lock() again */

	r = lkb->resource;
	hold_rsb(r);
	lock_rsb(r);

	/* set nodeid as master in r; _request_lock calls
	   set_master(r, lkb) to propagate the master to lkb */

	r->res_nodeid = ms->m_nodeid;
	if (r->res_nodeid != 0)
		set_bit(RESFL_MASTER_UNCERTAIN, &r->res_flags);

	/* We ignore the return value of _request_lock because there's
	   no user context to return it to.  The user is left to getting the
	   result from the ast. */

	_request_lock(r, lkb);

	unlock_rsb(r);
	put_rsb(r);
 out:
	put_lkb(lkb);
}

int send_bast()
{
}

void receive_bast()
{
}

int dlm_send_message()
{
}

int dlm_receive_message(struct dlm_header *hd, int nodeid, int recovery)
{
	struct dlm_message *ms = (struct dlm_message *) hd;
	struct dlm_ls *ls;

	ls = find_lockspace_global(hd->h_lockspace);
	if (!ls)
		return -EINVAL;

	/* recovery may have just ended leaving a bunch of backed-up requests
	   in the requestqueue; wait while dlm_recoverd clears them */

	if (!recovery)
		wait_requestqueue(ls);

	/* recovery may have just started while there were a bunch of
	   in-flight requests -- save them in requestqueue to be processed
	   after recovery.  we can't let dlm_recvd block on the recovery
	   lock.  if dlm_recoverd is calling this function to clear the
	   requestqueue, it needs to be interrupted (-EINTR) if another
	   recovery operation is starting. */

	while (1) {
		if (!test_bit(LSFL_LS_RUN, &ls->ls_flags)) {
			if (!recovery)
				add_to_requestqueue(ls, nodeid, hd);
			error = -EINTR;
			goto out;
		}

		error = lock_recovery_try(ls);
		if (!error)
			break;
		schedule();
	}

	switch (ms->m_type) {

	/* messages sent to a master node */

	DLM_MSG_REQUEST:
		receive_request(ls, ms);
		break;

	DLM_MSG_CONVERT:
		receive_convert(ls, ms);
		break;

	DLM_MSG_UNLOCK:
		receive_unlock(ls, ms);
		break;

	DLM_MSG_CANCEL:
		receive_cancel(ls, ms);
		break;

	/* messages sent from a master node (replies to above) */

	DLM_MSG_REQUEST_REPLY:
		receive_request_reply(ls, ms);
		break;

	DLM_MSG_CONVERT_REPLY:
		receive_convert_reply(ls, ms);
		break;

	DLM_MSG_UNLOCK_REPLY:
		receive_unlock_reply(ls, ms);
		break;

	DLM_MSG_CANCEL_REPLY:
		receive_cancel_reply(ls, ms);
		break;

	/* messages sent from a master node (only two types of async msg) */

	DLM_MSG_GRANT:
		receive_grant(ls, ms);
		break;

	DLM_MSG_BAST:
		receive_bast(ls, ms);
		break;

	/* messages sent to a dir node */

	DLM_MSG_LOOKUP:
		receive_lookup(ls, ms, nodeid);
		break;

	DLM_MSG_REMOVE:
		receive_remove(ls, ms, nodeid);
		break;

	/* messages sent from a dir node */

	DLM_MSG_LOOKUP_REPLY:
		receive_lookup_reply(ls, ms, nodeid);
		break;

	default:
		log_error("unknown message type %d", ms->m_type);
	}

	unlock_recovery(ls);
	put_lockspace(ls);
	wake_astd();
	return 0;
}


struct dlm_lkb {
	struct dlm_rsb *	lkb_resource;	/* the rsb */
	struct kref		lkb_ref;
	int			lkb_nodeid;	/* copied from rsb */
	int			lkb_ownpid;	/* pid of lock owner */
	uint32_t		lkb_id;		/* our lock ID */
	uint32_t		lkb_remid;	/* lock ID on remote partner */
	uint32_t		lkb_exflags;	/* external flags from caller */
	uint32_t		lkb_sbflags;	/* lksb flags */
	uint32_t		lkb_flags;	/* internal flags */
	uint32_t		lkb_lvbseq;	/* lvb sequence number */

	int8_t			lkb_status;     /* granted, waiting, convert */
	int8_t			lkb_rqmode;	/* requested lock mode */
	int8_t			lkb_grmode;	/* granted lock mode */
	int8_t			lkb_bastmode;	/* requested mode */
	int8_t			lkb_highbast;	/* highest mode bast sent for */

	int8_t			lkb_wait_type;	/* type of reply waiting for */
	int8_t			lkb_ast_type;	/* type of ast queued for */

	struct list_head	lkb_idtbl_list;	/* lockspace lkbtbl */
	struct list_head	lkb_statequeue;	/* rsb g/c/w list */
	struct list_head	lkb_rsb_lookup;	/* waiting for rsb lookup */
	struct list_head	lkb_wait_reply;	/* waiting for remote reply */
	struct list_head	lkb_astqueue;	/* need ast to be sent */

	uint64_t *		lkb_range;	/* array of gr/rq ranges */
	char *			lkb_lvbptr;
	struct dlm_lksb *       lkb_lksb;       /* caller's status block */
	void *			lkb_astaddr;	/* caller's ast function */
	void *			lkb_bastaddr;	/* caller's bast function */
	long			lkb_astparam;	/* caller's ast arg */

	/* parent/child locks not yet implemented */
#if 0
	struct dlm_lkb *	lkb_parent;	/* parent lkid */
	int			lkb_childcnt;	/* number of children */
#endif
};


/* dlm_header is first element of all structs sent between nodes */

struct dlm_header {
	uint32_t		h_lockspace;
	uint32_t		h_nodeid;	/* nodeid of sender */
	uint16_t		h_length;
	uint8_t			h_cmd;		/* DLM_MSG, DLM_RCOM */
	uint8_t			h_pad;
};

struct dlm_message {
	struct dlm_header	m_header;
	uint32_t		m_type;		/* DLM_MSG_ */
	uint32_t		m_lkid;		/* lkid on sender */
	uint32_t		m_remlkid;	/* lkid on receiver */
	uint32_t		m_lkid_parent;
	uint32_t		m_remlkid_parent;
	uint32_t		m_flags;
	uint32_t		m_exflags;
	uint32_t		m_sbflags;
	uint32_t		m_nodeid;
	uint32_t		m_pid;
	int			m_grmode;
	int			m_rqmode;
	int			m_result;	/* 0 or -EXXX */
	uint16_t		m_queue;	/* CONVERT, WAIT, GRANT */
	uint16_t		m_asts;
	uint32_t		m_lvbseq;
	char			m_lvb[DLM_LVB_LEN];
	uint64_t		m_range[2];
	char			m_name[0];
};

struct dlm_rcom {
	struct dlm_header	m_header;

};

struct dlm_query_request {
	struct dlm_header	m_header;

}

