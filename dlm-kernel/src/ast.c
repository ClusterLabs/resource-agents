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
 * This delivers ASTs and checks for dead remote requests and deadlocks.
 */

#include <linux/timer.h>

#include "dlm_internal.h"
#include "rsb.h"
#include "lockqueue.h"
#include "dir.h"
#include "locking.h"
#include "lkb.h"
#include "lowcomms.h"
#include "midcomms.h"
#include "ast.h"
#include "nodes.h"
#include "config.h"

/* Wake up flags for astd */
#define GDLMD_WAKE_ASTS  1
#define GDLMD_WAKE_TIMER 2

static struct list_head _deadlockqueue;
static struct semaphore _deadlockqueue_lock;
static struct list_head _lockqueue;
static struct semaphore _lockqueue_lock;
static struct timer_list _lockqueue_timer;
static struct list_head _ast_queue;
static struct semaphore _ast_queue_lock;
static wait_queue_head_t _astd_waitchan;
static atomic_t _astd_running;
static long _astd_pid;
static unsigned long _astd_wakeflags;
static struct completion _astd_done;

void add_to_lockqueue(gd_lkb_t *lkb)
{
	/* Time stamp the entry so we know if it's been waiting too long */
	lkb->lkb_lockqueue_time = jiffies;

	down(&_lockqueue_lock);
	list_add(&lkb->lkb_lockqueue, &_lockqueue);
	up(&_lockqueue_lock);
}

void remove_from_lockqueue(gd_lkb_t *lkb)
{
	down(&_lockqueue_lock);
	list_del(&lkb->lkb_lockqueue);
	up(&_lockqueue_lock);
}

void add_to_deadlockqueue(gd_lkb_t *lkb)
{
	if (test_bit(LSFL_NOTIMERS, &lkb->lkb_resource->res_ls->ls_flags))
		return;
	lkb->lkb_duetime = jiffies;
	down(&_deadlockqueue_lock);
	list_add(&lkb->lkb_deadlockq, &_deadlockqueue);
	up(&_deadlockqueue_lock);
}

void remove_from_deadlockqueue(gd_lkb_t *lkb)
{
	if (test_bit(LSFL_NOTIMERS, &lkb->lkb_resource->res_ls->ls_flags))
		return;

	down(&_deadlockqueue_lock);
	list_del(&lkb->lkb_deadlockq);
	up(&_deadlockqueue_lock);

	/* Invalidate the due time */
	memset(&lkb->lkb_duetime, 0, sizeof(lkb->lkb_duetime));
}

void remove_from_astqueue(gd_lkb_t *lkb)
{
	down(&_ast_queue_lock);
	if (lkb->lkb_asts_to_deliver)
		list_del(&lkb->lkb_astqueue);
	lkb->lkb_asts_to_deliver = 0;
	up(&_ast_queue_lock);
}

/* 
 * Actually deliver an AST to a user. The caller MUST hold the ast queue lock
 * and we unlock it for the duration of the user call, otherwise things can
 * deadlock.
 */

static void deliver_ast(gd_lkb_t *lkb, gd_ast_type_t astt)
{
	void (*cast) (long param) = lkb->lkb_astaddr;
	void (*bast) (long param, int mode) = lkb->lkb_bastaddr;

	up(&_ast_queue_lock);

	if (cast && (astt == GDLM_QUEUE_COMPAST))
		cast(lkb->lkb_astparam);

	else if (bast && (astt == GDLM_QUEUE_BLKAST)
		 && (lkb->lkb_status == GDLM_LKSTS_GRANTED))
		bast(lkb->lkb_astparam, (int) lkb->lkb_bastmode);

	/* 
	 * Remove LKB if requested.  It is up to the caller to remove the LKB
	 * from any resource queue it may be on.
	 *
	 * NOTE: we check lkb_asts_to_deliver here in case an ast for us was
	 * queued during the AST delivery itself (eg a user called dlm_unlock
	 * in the AST routine!
	 */

	if (lkb->lkb_flags & GDLM_LKFLG_DELAST && astt == GDLM_QUEUE_COMPAST &&
	    lkb->lkb_asts_to_deliver == 0) {
		gd_res_t *rsb = lkb->lkb_resource;
		struct rw_semaphore *in_recovery = &rsb->res_ls->ls_in_recovery;

		down_read(in_recovery);
		release_lkb(rsb->res_ls, lkb);
		release_rsb(rsb);
		up_read(in_recovery);
	}

	/* This queue can get very big so we schedule here to give the rest of
	 * the cluster chance to do some work. */
	schedule();

	down(&_ast_queue_lock);
}

/* 
 * Queue an AST for delivery, this will only deal with
 * kernel ASTs, usermode API will piggyback on top of this.
 *
 * This can be called in either the user or DLM context.
 * ASTs are queued EVEN IF we are already running in gdlm_astd
 * context as we don't know what other locks are held (eg we could
 * be being called from a lock operation that was called from
 * another AST!
 * If the AST is to be queued remotely then a message is sent to
 * the target system via midcomms.
 */

void queue_ast(gd_lkb_t *lkb, gd_ast_type_t astt, uint8_t rqmode)
{
	struct gd_remlockrequest req;

	if (lkb->lkb_flags & GDLM_LKFLG_MSTCPY) {
		/* 
		 * Send a message to have an ast queued remotely.  Note: we do
		 * not send remote completion asts, they are handled as part of
		 * remote lock granting.
		 */

		if (astt == GDLM_QUEUE_BLKAST) {
			req.rr_header.rh_cmd = GDLM_REMCMD_SENDBAST;
			req.rr_header.rh_length = sizeof(req);
			req.rr_header.rh_flags = 0;
			req.rr_header.rh_lkid = lkb->lkb_id;
			req.rr_header.rh_lockspace =
			    lkb->lkb_resource->res_ls->ls_global_id;
			req.rr_status = lkb->lkb_retstatus;
			req.rr_remlkid = lkb->lkb_remid;
			req.rr_rqmode = rqmode;

			midcomms_send_message(lkb->lkb_nodeid, &req.rr_header,
					      lkb->lkb_resource->res_ls->ls_allocation);

		} else if (lkb->lkb_retstatus == -EDEADLOCK) {
			/* 
			 * We only queue remote Completion ASTs here for error
			 * completions that happen out of band.
			 * DEADLOCK is one such.
			 */

			req.rr_header.rh_cmd = GDLM_REMCMD_SENDCAST;
			req.rr_header.rh_length = sizeof(req);
			req.rr_header.rh_flags = 0;
			req.rr_header.rh_lkid = lkb->lkb_id;
			req.rr_header.rh_lockspace =
			    lkb->lkb_resource->res_ls->ls_global_id;
			req.rr_status = lkb->lkb_retstatus;
			req.rr_remlkid = lkb->lkb_remid;
			req.rr_rqmode = rqmode;

			midcomms_send_message(lkb->lkb_nodeid, &req.rr_header,
					      lkb->lkb_resource->res_ls->ls_allocation);
		}
	} else {
		/* 
		 * Prepare info which will be returned in ast/bast.
		 */

		if (astt == GDLM_QUEUE_BLKAST) {
			lkb->lkb_bastmode = rqmode;
		} else {
			lkb->lkb_lksb->sb_status = lkb->lkb_retstatus;

			if (lkb->lkb_flags & GDLM_LKFLG_DEMOTED)
				lkb->lkb_lksb->sb_flags = DLM_SBF_DEMOTED;
			else
				lkb->lkb_lksb->sb_flags = 0;
		}

		/* 
		 * Queue ast/bast or deliver directly.  astd can deliver ASTs
		 * during deadlock detection or lock timeouts.
		 */

		down(&_ast_queue_lock);

		if (!lkb->lkb_asts_to_deliver)
			list_add_tail(&lkb->lkb_astqueue, &_ast_queue);
		lkb->lkb_asts_to_deliver |= astt;

		up(&_ast_queue_lock);

		/* It is the responsibility of the caller to call wake_astd()
		 * after it has finished other locking operations that request
		 * the ASTs to be delivered after */
	}
}

/* 
 * Process any LKBs on the AST queue.  The were queued in queue_ast().
 */

static void process_asts(void)
{
	gd_lkb_t *lkb, *safe;
	uint32_t to_deliver;

	down(&_ast_queue_lock);

	list_for_each_entry_safe(lkb, safe, &_ast_queue, lkb_astqueue) {

		/* The lkb can be placed back on _ast_queue as soon as
		 * _ast_queue_lock is released. */

		to_deliver = lkb->lkb_asts_to_deliver;
		lkb->lkb_asts_to_deliver = 0;
		list_del(&lkb->lkb_astqueue);

		if ((to_deliver & GDLM_QUEUE_COMPAST))
			deliver_ast(lkb, GDLM_QUEUE_COMPAST);

		if ((to_deliver & GDLM_QUEUE_BLKAST))
			deliver_ast(lkb, GDLM_QUEUE_BLKAST);
	}
	up(&_ast_queue_lock);
}

void lockqueue_lkb_mark(gd_ls_t *ls)
{
	gd_lkb_t *lkb, *safe;
	int count = 0;

	log_all(ls, "mark waiting requests");

	down(&_lockqueue_lock);

	list_for_each_entry_safe(lkb, safe, &_lockqueue, lkb_lockqueue) {

		if (lkb->lkb_resource->res_ls != ls)
			continue;

		/* 
		 * These lkb's are new and the master is being looked up.  Mark
		 * the lkb request to be resent.  Even if the destination node
		 * for the request is still living and has our request, it will
		 * purge all resdir requests in purge_requestqueue.  If there's
		 * a reply to the LOOKUP request in our requestqueue (the reply
		 * arrived after ls_stop), it is invalid and will be discarded
		 * in purge_requestqueue, too.
		 */

		if (lkb->lkb_lockqueue_state == GDLM_LQSTATE_WAIT_RSB) {
			GDLM_ASSERT(lkb->lkb_nodeid == -1,
				    log_error(ls, "nodeid=%d\n",
					      lkb->lkb_nodeid););

			lkb->lkb_flags |= GDLM_LKFLG_LQRESEND;
			count++;
			continue;
		}

		/* 
		 * These lkb's have an outstanding request to a bygone node.
		 * The request will be redirected to the new master node in
		 * resend_cluster_requests().  Don't mark the request for
		 * resending if there's a reply for it saved in the
		 * requestqueue.
		 */

		if (in_nodes_gone(ls, lkb->lkb_nodeid) &&
		    !reply_in_requestqueue(ls, lkb->lkb_id)) {

			lkb->lkb_flags |= GDLM_LKFLG_LQRESEND;

			/* 
			 * Don't rebuild this lkb on a new rsb in
			 * rebuild_rsbs_send().
			 */

			if (lkb->lkb_lockqueue_state ==
			    GDLM_LQSTATE_WAIT_CONDGRANT) {
				GDLM_ASSERT(lkb->lkb_status ==
					    GDLM_LKSTS_WAITING, );
				lkb->lkb_flags |= GDLM_LKFLG_NOREBUILD;
			}

			/* 
			 * This flag indicates to the new master that his lkb
			 * is in the midst of a convert request and should be
			 * placed on the granted queue rather than the convert
			 * queue.  We will resend this convert request to the
			 * new master.
			 */

			else if (lkb->lkb_lockqueue_state ==
				 GDLM_LQSTATE_WAIT_CONVERT) {
				GDLM_ASSERT(lkb->lkb_status ==
					    GDLM_LKSTS_CONVERT, );
				lkb->lkb_flags |= GDLM_LKFLG_LQCONVERT;
			}

			count++;
		}
	}
	up(&_lockqueue_lock);

	log_all(ls, "marked %d requests", count);
}

int resend_cluster_requests(gd_ls_t *ls)
{
	gd_lkb_t *lkb, *safe;
	int error = 0, state, count = 0;

	log_all(ls, "resend marked requests");

	down(&_lockqueue_lock);

	list_for_each_entry_safe(lkb, safe, &_lockqueue, lkb_lockqueue) {

		if (!test_bit(LSFL_LS_RUN, &ls->ls_flags)) {
			log_debug(ls, "resend_cluster_requests: aborted");
			error = -EINTR;
			break;
		}

		if (lkb->lkb_resource->res_ls != ls)
			continue;

		log_debug(ls, "resend_cluster_requests id=%x nodeid=%d "
		          "lqstate=%u flags=%x", lkb->lkb_id, lkb->lkb_nodeid,
			  lkb->lkb_lockqueue_state, lkb->lkb_flags);

		/* 
		 * Resend/process the lockqueue lkb's (in-progres requests)
		 * that were flagged at the start of recovery in
		 * lockqueue_lkb_mark().
		 */

		if (lkb->lkb_flags & GDLM_LKFLG_LQRESEND) {
			lkb->lkb_flags &= ~GDLM_LKFLG_LQRESEND;
			lkb->lkb_flags &= ~GDLM_LKFLG_NOREBUILD;
			lkb->lkb_flags &= ~GDLM_LKFLG_LQCONVERT;

			if (lkb->lkb_nodeid == -1) {
				/* 
				 * Send lookup to new resdir node.
				 */
				lkb->lkb_lockqueue_time = jiffies;
				send_cluster_request(lkb,
						     lkb->lkb_lockqueue_state);
			}

			else if (lkb->lkb_nodeid != 0) {
				/* 
				 * There's a new RSB master (that's not us.)
				 */
				lkb->lkb_lockqueue_time = jiffies;
				send_cluster_request(lkb,
						     lkb->lkb_lockqueue_state);
			}

			else {
				/* 
				 * We are the new RSB master for this lkb
				 * request.
				 */
				state = lkb->lkb_lockqueue_state;
				lkb->lkb_lockqueue_state = 0;
				/* list_del equals remove_from_lockqueue() */
				list_del(&lkb->lkb_lockqueue);
				process_remastered_lkb(lkb, state);
			}

			count++;
		}
	}
	up(&_lockqueue_lock);

	log_all(ls, "resent %d requests", count);
	return error;
}

/* 
 * Process any LKBs on the Lock queue, this
 * just looks at the entries to see if they have been
 * on the queue too long and fails the requests if so.
 */

static void process_lockqueue(void)
{
	gd_lkb_t *lkb, *safe;
	gd_ls_t *ls;
	int count = 0;

	down(&_lockqueue_lock);

	list_for_each_entry_safe(lkb, safe, &_lockqueue, lkb_lockqueue) {
		ls = lkb->lkb_resource->res_ls;

		if (test_bit(LSFL_NOTIMERS, &ls->ls_flags))
			continue;

		/* Don't time out locks that are in transition */
		if (!test_bit(LSFL_LS_RUN, &ls->ls_flags))
			continue;

		if (check_timeout(lkb->lkb_lockqueue_time,
				  dlm_config.lock_timeout)) {
			count++;
			list_del(&lkb->lkb_lockqueue);
			up(&_lockqueue_lock);
			cancel_lockop(lkb, -ETIMEDOUT);
			down(&_lockqueue_lock);
		}
	}
	up(&_lockqueue_lock);

	if (count)
		wake_astd();

	if (atomic_read(&_astd_running))
		mod_timer(&_lockqueue_timer,
			  jiffies + ((dlm_config.lock_timeout >> 1) * HZ));
}

/* Look for deadlocks */
static void process_deadlockqueue(void)
{
	gd_lkb_t *lkb, *safe;

	down(&_deadlockqueue_lock);

	list_for_each_entry_safe(lkb, safe, &_deadlockqueue, lkb_deadlockq) {
		gd_lkb_t *kill_lkb;

		/* Only look at "due" locks */
		if (!check_timeout(lkb->lkb_duetime, dlm_config.deadlocktime))
			break;

		/* Don't look at locks that are in transition */
		if (!test_bit(LSFL_LS_RUN,
			      &lkb->lkb_resource->res_ls->ls_flags))
			continue;

		up(&_deadlockqueue_lock);

		/* Lock has hit due time, check for conversion deadlock */
		kill_lkb = conversion_deadlock_check(lkb);
		if (kill_lkb)
			cancel_conversion(kill_lkb, -EDEADLOCK);

		down(&_deadlockqueue_lock);
	}
	up(&_deadlockqueue_lock);
}

static __inline__ int no_asts(void)
{
	int ret;

	down(&_ast_queue_lock);
	ret = list_empty(&_ast_queue);
	up(&_ast_queue_lock);
	return ret;
}

static void lockqueue_timer_fn(unsigned long arg)
{
	set_bit(GDLMD_WAKE_TIMER, &_astd_wakeflags);
	wake_up(&_astd_waitchan);
}

/* 
 * DLM daemon which delivers asts.
 */

static int dlm_astd(void *data)
{
	daemonize("dlm_astd");

	INIT_LIST_HEAD(&_lockqueue);
	init_MUTEX(&_lockqueue_lock);
	INIT_LIST_HEAD(&_deadlockqueue);
	init_MUTEX(&_deadlockqueue_lock);
	INIT_LIST_HEAD(&_ast_queue);
	init_MUTEX(&_ast_queue_lock);
	init_waitqueue_head(&_astd_waitchan);
	complete(&_astd_done);

	/* 
	 * Set a timer to check the lockqueue for dead locks (and deadlocks).
	 */

	init_timer(&_lockqueue_timer);
	_lockqueue_timer.function = lockqueue_timer_fn;
	_lockqueue_timer.data = 0;
	mod_timer(&_lockqueue_timer,
		  jiffies + ((dlm_config.lock_timeout >> 1) * HZ));

	while (atomic_read(&_astd_running)) {
		wchan_cond_sleep_intr(_astd_waitchan, no_asts());

		if (test_and_clear_bit(GDLMD_WAKE_ASTS, &_astd_wakeflags))
			process_asts();

		if (test_and_clear_bit(GDLMD_WAKE_TIMER, &_astd_wakeflags)) {
			process_lockqueue();
			if (dlm_config.deadlocktime)
				process_deadlockqueue();
		}
	}

	if (timer_pending(&_lockqueue_timer))
		del_timer(&_lockqueue_timer);

	complete(&_astd_done);

	return 0;
}

void wake_astd(void)
{
	set_bit(GDLMD_WAKE_ASTS, &_astd_wakeflags);
	wake_up(&_astd_waitchan);
}

int astd_start()
{
	init_completion(&_astd_done);
	atomic_set(&_astd_running, 1);
	_astd_pid = kernel_thread(dlm_astd, NULL, 0);
	wait_for_completion(&_astd_done);
	return 0;
}

void astd_stop()
{
	atomic_set(&_astd_running, 0);
	wake_astd();
	wait_for_completion(&_astd_done);
}
