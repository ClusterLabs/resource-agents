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
#include "nodes.h"
#include "dir.h"
#include "ast.h"
#include "recover.h"
#include "lockspace.h"
#include "lowcomms.h"
#include "lockqueue.h"
#include "lkb.h"
#include "rebuild.h"

/* 
 * next_move actions
 */

#define DO_STOP             (1)
#define DO_START            (2)
#define DO_FINISH           (3)
#define DO_FINISH_STOP      (4)
#define DO_FINISH_START     (5)

/* 
 * recoverd_flags for thread
 */

#define THREAD_STOP         (0)

/* 
 * local thread variables
 */

static unsigned long recoverd_flags;
static struct completion recoverd_run;
static wait_queue_head_t recoverd_wait;
static struct task_struct *recoverd_task;

/* 
 * Queue of lockspaces (gr_recover_t structs) which need to be
 * started/recovered
 */

static struct list_head recoverd_start_queue;
static atomic_t recoverd_start_count;

extern struct list_head lslist;
extern spinlock_t lslist_lock;

void dlm_recoverd_init(void)
{
	INIT_LIST_HEAD(&recoverd_start_queue);
	atomic_set(&recoverd_start_count, 0);

	init_completion(&recoverd_run);
	init_waitqueue_head(&recoverd_wait);
	memset(&recoverd_flags, 0, sizeof(unsigned long));
}

static int enable_locking(gd_ls_t *ls, int event_id)
{
	int error = 0;

	spin_lock(&ls->ls_recover_lock);
	if (ls->ls_last_stop < event_id) {
		set_bit(LSFL_LS_RUN, &ls->ls_flags);
		up_write(&ls->ls_in_recovery);
	} else {
		error = -EINTR;
		log_debug(ls, "enable_locking: abort %d", event_id);
	}
	spin_unlock(&ls->ls_recover_lock);
	return error;
}

static int ls_first_start(gd_ls_t *ls, gd_recover_t *gr)
{
	int error;

	log_all(ls, "recover event %u (first)", gr->gr_event_id);

	kcl_global_service_id(ls->ls_local_id, &ls->ls_global_id);

	error = ls_nodes_init(ls, gr);
	if (error) {
		log_error(ls, "nodes_init failed %d", error);
		goto out;
	}

	error = resdir_rebuild_local(ls);
	if (error) {
		log_error(ls, "resdir_rebuild_local failed %d", error);
		goto out;
	}

	error = resdir_rebuild_wait(ls);
	if (error) {
		log_error(ls, "resdir_rebuild_wait failed %d", error);
		goto out;
	}

	log_all(ls, "recover event %u done", gr->gr_event_id);
	kcl_start_done(ls->ls_local_id, gr->gr_event_id);

      out:
	return error;
}

/* 
 * We are given here a new group of nodes which are in the lockspace.  We first
 * figure out the differences in ls membership from when we were last running.
 * If nodes from before are gone, then there will be some lock recovery to do.
 * If there are only nodes which have joined, then there's no lock recovery.
 *
 * note: cman requires an rc to finish starting on an revent (where nodes die)
 * before it allows an sevent (where nodes join) to be processed.  This means
 * that we won't get start1 with nodeA gone, stop/cancel, start2 with nodeA
 * joined.
 */

static int ls_reconfig(gd_ls_t *ls, gd_recover_t *gr)
{
	int error, neg = 0;

	log_all(ls, "recover event %u", gr->gr_event_id);

	/* 
	 * Add or remove nodes from the lockspace's ls_nodes list.
	 */

	error = ls_nodes_reconfig(ls, gr, &neg);
	if (error) {
		log_error(ls, "nodes_reconfig failed %d", error);
		goto fail;
	}

	/* 
	 * Rebuild our own share of the resdir by collecting from all other
	 * nodes rsb name/master pairs for which the name hashes to us.
	 */

	error = resdir_rebuild_local(ls);
	if (error) {
		log_error(ls, "resdir_rebuild_local failed %d", error);
		goto fail;
	}

	/* 
	 * Purge resdir-related requests that are being held in requestqueue.
	 * All resdir requests from before recovery started are invalid now due
	 * to the resdir rebuild and will be resent by the requesting nodes.
	 */

	purge_requestqueue(ls);
	set_bit(LSFL_REQUEST_WARN, &ls->ls_flags);

	/* 
	 * Wait for all nodes to complete resdir rebuild.
	 */

	error = resdir_rebuild_wait(ls);
	if (error) {
		log_error(ls, "resdir_rebuild_wait failed %d", error);
		goto fail;
	}

	/* 
	 * Mark our own lkb's waiting in the lockqueue for remote replies from
	 * nodes that are now departed.  These will be resent to the new
	 * masters in resend_cluster_requests.  Also mark resdir lookup
	 * requests for resending.
	 */

	lockqueue_lkb_mark(ls);

	error = gdlm_recovery_stopped(ls);
	if (error)
		goto fail;

	if (neg) {
		/* 
		 * Clear lkb's for departed nodes.  This can't fail since it
		 * doesn't involve communicating with other nodes.
		 */

		down_write(&ls->ls_rec_rsblist);
		restbl_lkb_purge(ls);
		up_write(&ls->ls_rec_rsblist);

		down_read(&ls->ls_rec_rsblist);

		/* 
		 * Get new master id's for rsb's of departed nodes.  This fails
		 * if we can't communicate with other nodes.
		 */

		error = restbl_rsb_update(ls);
		if (error) {
			log_error(ls, "restbl_rsb_update failed %d", error);
			goto fail_up;
		}

		/* 
		 * Send our lkb info to new masters.  This fails if we can't
		 * communicate with a node.
		 */

		error = rebuild_rsbs_send(ls);
		if (error) {
			log_error(ls, "rebuild_rsbs_send failed %d", error);
			goto fail_up;
		}
		up_read(&ls->ls_rec_rsblist);
	}

	clear_bit(LSFL_REQUEST_WARN, &ls->ls_flags);

	log_all(ls, "recover event %u done", gr->gr_event_id);
	kcl_start_done(ls->ls_local_id, gr->gr_event_id);
	return 0;

 fail_up:
	up_read(&ls->ls_rec_rsblist);
 fail:
	log_all(ls, "recover event %d error %d", gr->gr_event_id, error);
	return error;
}

static void clear_finished_nodes(gd_ls_t *ls, int finish_event)
{
	gd_csb_t *csb, *safe;

	list_for_each_entry_safe(csb, safe, &ls->ls_nodes_gone, csb_list) {
		if (csb->csb_gone_event <= finish_event) {
			list_del(&csb->csb_list);
			release_csb(csb);
		}
	}
}

/* 
 * Between calls to this routine for a ls, there can be multiple stop/start
 * events from cman where every start but the latest is cancelled by stops.
 * There can only be a single finish from cman because every finish requires us
 * to call start_done.  A single finish event could be followed by multiple
 * stop/start events.  This routine takes any combination of events from cman
 * and boils them down to one course of action.
 */

int next_move(gd_ls_t *ls, gd_recover_t **gr_out, int *finish_out)
{
	LIST_HEAD(events);
	unsigned int cmd = 0, stop, start, finish;
	unsigned int last_stop, last_start, last_finish;
	gd_recover_t *gr = NULL, *start_gr = NULL;

	/* 
	 * Grab the current state of cman/sm events.
	 */

	spin_lock(&ls->ls_recover_lock);

	stop = test_and_clear_bit(LSFL_LS_STOP, &ls->ls_flags) ? 1 : 0;
	start = test_and_clear_bit(LSFL_LS_START, &ls->ls_flags) ? 1 : 0;
	finish = test_and_clear_bit(LSFL_LS_FINISH, &ls->ls_flags) ? 1 : 0;

	last_stop = ls->ls_last_stop;
	last_start = ls->ls_last_start;
	last_finish = ls->ls_last_finish;

	while (!list_empty(&ls->ls_recover)) {
		gr = list_entry(ls->ls_recover.next, gd_recover_t, gr_list);
		list_del(&gr->gr_list);
		list_add_tail(&gr->gr_list, &events);
	}
	spin_unlock(&ls->ls_recover_lock);

	log_debug(ls, "move flags %u,%u,%u ids %u,%u,%u", stop, start, finish,
		  last_stop, last_start, last_finish);

	/* 
	 * Toss start events which have since been cancelled.
	 */

	while (!list_empty(&events)) {
		GDLM_ASSERT(start,);
		gr = list_entry(events.next, gd_recover_t, gr_list);
		list_del(&gr->gr_list);

		if (gr->gr_event_id <= last_stop) {
			log_debug(ls, "move skip event %u", gr->gr_event_id);
			kfree(gr->gr_nodeids);
			free_dlm_recover(gr);
			gr = NULL;
		} else {
			log_debug(ls, "move use event %u", gr->gr_event_id);
			GDLM_ASSERT(!start_gr,);
			start_gr = gr;
		}
	}

	/* 
	 * Eight possible combinations of events.
	 */

	/* 0 */
	if (!stop && !start && !finish) {
		GDLM_ASSERT(!start_gr,);
		cmd = 0;
		goto out;
	}

	/* 1 */
	if (!stop && !start && finish) {
		GDLM_ASSERT(!start_gr,);
		GDLM_ASSERT(last_start > last_stop,);
		GDLM_ASSERT(last_finish == last_start,);
		cmd = DO_FINISH;
		*finish_out = last_finish;
		goto out;
	}

	/* 2 */
	if (!stop && start && !finish) {
		GDLM_ASSERT(start_gr,);
		GDLM_ASSERT(last_start > last_stop,);
		cmd = DO_START;
		*gr_out = start_gr;
		goto out;
	}

	/* 3 */
	if (!stop && start && finish) {
		GDLM_ASSERT(0, printk("finish and start with no stop\n"););
	}

	/* 4 */
	if (stop && !start && !finish) {
		GDLM_ASSERT(!start_gr,);
		GDLM_ASSERT(last_start == last_stop,);
		cmd = DO_STOP;
		goto out;
	}

	/* 5 */
	if (stop && !start && finish) {
		GDLM_ASSERT(!start_gr,);
		GDLM_ASSERT(last_finish == last_start,);
		GDLM_ASSERT(last_stop == last_start,);
		cmd = DO_FINISH_STOP;
		*finish_out = last_finish;
		goto out;
	}

	/* 6 */
	if (stop && start && !finish) {
		if (start_gr) {
			GDLM_ASSERT(last_start > last_stop,);
			cmd = DO_START;
			*gr_out = start_gr;
		} else {
			GDLM_ASSERT(last_stop == last_start,);
			cmd = DO_STOP;
		}
		goto out;
	}

	/* 7 */
	if (stop && start && finish) {
		if (start_gr) {
			GDLM_ASSERT(last_start > last_stop,);
			GDLM_ASSERT(last_start > last_finish,);
			cmd = DO_FINISH_START;
			*finish_out = last_finish;
			*gr_out = start_gr;
		} else {
			GDLM_ASSERT(last_start == last_stop,);
			GDLM_ASSERT(last_start > last_finish,);
			cmd = DO_FINISH_STOP;
			*finish_out = last_finish;
		}
		goto out;
	}

      out:
	return cmd;
}

/* 
 * This function decides what to do given every combination of current
 * lockspace state and next lockspace state.
 */

static void do_ls_recovery(gd_ls_t *ls)
{
	gd_recover_t *gr = NULL;
	int error, cur_state, next_state = 0, do_now, finish_event = 0;

	do_now = next_move(ls, &gr, &finish_event);
	if (!do_now)
		goto out;

	cur_state = ls->ls_state;
	next_state = 0;

	GDLM_ASSERT(!test_bit(LSFL_LS_RUN, &ls->ls_flags),
		    log_error(ls, "curstate=%d donow=%d", cur_state, do_now););

	/* 
	 * LSST_CLEAR - we're not in any recovery state.  We can get a stop or
	 * a stop and start which equates with a START.
	 */

	if (cur_state == LSST_CLEAR) {
		switch (do_now) {
		case DO_STOP:
			next_state = LSST_WAIT_START;
			break;

		case DO_START:
			error = ls_reconfig(ls, gr);
			if (error)
				next_state = LSST_WAIT_START;
			else
				next_state = LSST_RECONFIG_DONE;
			break;

		case DO_FINISH:	/* invalid */
		case DO_FINISH_STOP:	/* invalid */
		case DO_FINISH_START:	/* invalid */
		default:
			GDLM_ASSERT(0,);
		}
		goto out;
	}

	/* 
	 * LSST_WAIT_START - we're not running because of getting a stop or
	 * failing a start.  We wait in this state for another stop/start or
	 * just the next start to begin another reconfig attempt.
	 */

	if (cur_state == LSST_WAIT_START) {
		switch (do_now) {
		case DO_STOP:
			break;

		case DO_START:
			error = ls_reconfig(ls, gr);
			if (error)
				next_state = LSST_WAIT_START;
			else
				next_state = LSST_RECONFIG_DONE;
			break;

		case DO_FINISH:	/* invalid */
		case DO_FINISH_STOP:	/* invalid */
		case DO_FINISH_START:	/* invalid */
		default:
			GDLM_ASSERT(0,);
		}
		goto out;
	}

	/* 
	 * LSST_RECONFIG_DONE - we entered this state after successfully
	 * completing ls_reconfig and calling kcl_start_done.  We expect to get
	 * a finish if everything goes ok.  A finish could be followed by stop
	 * or stop/start before we get here to check it.  Or a finish may never
	 * happen, only stop or stop/start.
	 */

	if (cur_state == LSST_RECONFIG_DONE) {
		switch (do_now) {
		case DO_FINISH:
			clear_finished_nodes(ls, finish_event);
			next_state = LSST_CLEAR;

			error = enable_locking(ls, finish_event);
			if (error)
				break;

			error = process_requestqueue(ls);
			if (error)
				break;

			error = resend_cluster_requests(ls);
			if (error)
				break;

			restbl_grant_after_purge(ls);

			log_all(ls, "recover event %u finished", finish_event);
			break;

		case DO_STOP:
			next_state = LSST_WAIT_START;
			break;

		case DO_FINISH_STOP:
			clear_finished_nodes(ls, finish_event);
			next_state = LSST_WAIT_START;
			break;

		case DO_FINISH_START:
			clear_finished_nodes(ls, finish_event);
			/* fall into DO_START */

		case DO_START:
			error = ls_reconfig(ls, gr);
			if (error)
				next_state = LSST_WAIT_START;
			else
				next_state = LSST_RECONFIG_DONE;
			break;

		default:
			GDLM_ASSERT(0,);
		}
		goto out;
	}

	/* 
	 * LSST_INIT - state after ls is created and before it has been
	 * started.  A start operation will cause the ls to be started for the
	 * first time.  A failed start will cause to just wait in INIT for
	 * another stop/start.
	 */

	if (cur_state == LSST_INIT) {
		switch (do_now) {
		case DO_START:
			error = ls_first_start(ls, gr);
			if (!error)
				next_state = LSST_INIT_DONE;
			break;

		case DO_STOP:
			break;

		case DO_FINISH:	/* invalid */
		case DO_FINISH_STOP:	/* invalid */
		case DO_FINISH_START:	/* invalid */
		default:
			GDLM_ASSERT(0,);
		}
		goto out;
	}

	/* 
	 * LSST_INIT_DONE - after the first start operation is completed
	 * successfully and kcl_start_done() called.  If there are no errors, a
	 * finish will arrive next and we'll move to LSST_CLEAR.
	 */

	if (cur_state == LSST_INIT_DONE) {
		switch (do_now) {
		case DO_STOP:
		case DO_FINISH_STOP:
			next_state = LSST_WAIT_START;
			break;

		case DO_START:
		case DO_FINISH_START:
			error = ls_reconfig(ls, gr);
			if (error)
				next_state = LSST_WAIT_START;
			else
				next_state = LSST_RECONFIG_DONE;
			break;

		case DO_FINISH:
			next_state = LSST_CLEAR;
			enable_locking(ls, finish_event);
			log_all(ls, "recover event %u finished", finish_event);
			break;

		default:
			GDLM_ASSERT(0,);
		}
		goto out;
	}

      out:
	if (next_state)
		ls->ls_state = next_state;

	if (gr) {
		kfree(gr->gr_nodeids);
		free_dlm_recover(gr);
	}
}

static __inline__ gd_ls_t *get_work(int clear)
{
	gd_ls_t *ls;

	spin_lock(&lslist_lock);

	list_for_each_entry(ls, &lslist, ls_list) {
		if (clear) {
			if (test_and_clear_bit(LSFL_WORK, &ls->ls_flags))
			        goto got_work;

		} else {
			if (test_bit(LSFL_WORK, &ls->ls_flags))
			        goto got_work;
		}
	}
	ls = NULL;

 got_work:
	spin_unlock(&lslist_lock);

	return ls;
}

/* 
 * Thread which does recovery for all lockspaces.
 */

static int dlm_recoverd(void *arg)
{
	gd_ls_t *ls;

	daemonize("dlm_recoverd");
	recoverd_task = current;
	complete(&recoverd_run);

	while (!test_bit(THREAD_STOP, &recoverd_flags)) {
		wchan_cond_sleep_intr(recoverd_wait, !get_work(0));
		if ((ls = get_work(1)))
			do_ls_recovery(ls);
	}

	complete(&recoverd_run);
	return 0;
}

/* 
 * Mark a specific lockspace as needing work and wake up the thread to do it.
 */

void recoverd_kick(gd_ls_t *ls)
{
	set_bit(LSFL_WORK, &ls->ls_flags);
	wake_up(&recoverd_wait);
}

/* 
 * Start the recoverd thread when gdlm is started (before any lockspaces).
 */

int recoverd_start(void)
{
	int error;

	clear_bit(THREAD_STOP, &recoverd_flags);
	error = kernel_thread(dlm_recoverd, NULL, 0);
	if (error < 0)
		goto out;

	error = 0;
	wait_for_completion(&recoverd_run);

      out:
	return error;
}

/* 
 * Stop the recoverd thread when gdlm is shut down (all lockspaces are gone).
 */

int recoverd_stop(void)
{
	set_bit(THREAD_STOP, &recoverd_flags);
	wake_up(&recoverd_wait);
	wait_for_completion(&recoverd_run);

	return 0;
}
