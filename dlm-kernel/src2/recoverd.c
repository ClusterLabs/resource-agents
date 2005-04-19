/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "dlm_internal.h"
#include "lockspace.h"
#include "member.h"
#include "dir.h"
#include "ast.h"
#include "recover.h"
#include "lowcomms.h"
#include "lock.h"
#include "requestqueue.h"

/*
 * next_move actions
 */

#define DO_STOP             (1)
#define DO_START            (2)
#define DO_FINISH           (3)
#define DO_FINISH_STOP      (4)
#define DO_FINISH_START     (5)

static void set_start_done(struct dlm_ls *ls, int event_id)
{
	int error;

	spin_lock(&ls->ls_recover_lock);
	ls->ls_startdone = event_id;
	spin_unlock(&ls->ls_recover_lock);

	error = kobject_uevent(&ls->ls_kobj, KOBJ_CHANGE, NULL);
	if (error)
		log_error(ls, "set_start_done kobject_uevent %d", error);
}

static int enable_locking(struct dlm_ls *ls, int event_id)
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

static int ls_first_start(struct dlm_ls *ls, struct dlm_recover *rv)
{
	int error;

	log_debug(ls, "recover event %u (first)", rv->event_id);

	down(&ls->ls_recoverd_active);

	error = dlm_recover_members_first(ls, rv);
	if (error) {
		log_error(ls, "recover_members first failed %d", error);
		goto out;
	}

	error = dlm_recover_directory(ls);
	if (error) {
		log_error(ls, "recover_directory failed %d", error);
		goto out;
	}

	error = dlm_dir_rebuild_wait(ls);
	if (error) {
		log_error(ls, "dir_rebuild_wait failed %d", error);
		goto out;
	}

	log_debug(ls, "recover event %u done", rv->event_id);
	set_start_done(ls, rv->event_id);

 out:
	up(&ls->ls_recoverd_active);
	return error;
}

/*
 * We are given here a new group of nodes which are in the lockspace.  We first
 * figure out the differences in ls membership from when we were last running.
 * If nodes from before are gone, then there will be some lock recovery to do.
 * If there are only nodes which have joined, then there's no lock recovery.
 *
 * Lockspace recovery for failed nodes must be completed before any nodes are
 * allowed to join or leave the lockspace.
 */

static int ls_reconfig(struct dlm_ls *ls, struct dlm_recover *rv)
{
	int error, neg = 0;

	log_debug(ls, "recover event %u", rv->event_id);

	down(&ls->ls_recoverd_active);

	/*
	 * Suspending and resuming dlm_astd ensures that no lkb's from this ls
	 * will be processed by dlm_astd during recovery.
	 */

	dlm_astd_suspend();
	dlm_astd_resume();

	/*
	 * This list of root rsb's will be the basis of most of the recovery
	 * routines.
	 */

	dlm_create_root_list(ls);

	/*
	 * Add or remove nodes from the lockspace's ls_nodes list.
	 * Also waits for all nodes to complete dlm_recover_members.
	 */

	error = dlm_recover_members(ls, rv, &neg);
	if (error) {
		log_error(ls, "recover_members failed %d", error);
		goto fail;
	}

	/*
	 * Rebuild our own share of the directory by collecting from all other
	 * nodes their master rsb names that hash to us.
	 */

	error = dlm_recover_directory(ls);
	if (error) {
		log_error(ls, "recover_directory failed %d", error);
		goto fail;
	}

	/*
	 * Purge directory-related requests that are saved in requestqueue.
	 * All dir requests from before recovery are invalid now due to the dir
	 * rebuild and will be resent by the requesting nodes.
	 */

	dlm_purge_requestqueue(ls);

	/*
	 * Wait for all nodes to complete directory rebuild.
	 */

	error = dlm_dir_rebuild_wait(ls);
	if (error) {
		log_error(ls, "dir_rebuild_wait failed %d", error);
		goto fail;
	}

	/*
	 * We may have outstanding operations that are waiting for a reply from
	 * a failed node.  Mark these to be resent after recovery.  Unlock and
	 * cancel ops can just be completed.
	 */

	dlm_recover_waiters_pre(ls);

	error = dlm_recovery_stopped(ls);
	if (error)
		goto fail;

	if (neg) {
		/*
		 * Clear lkb's for departed nodes.
		 */

		dlm_purge_locks(ls);

		/*
		 * Get new master nodeid's for rsb's that were mastered on
		 * departed nodes.
		 */

		error = dlm_recover_masters(ls);
		if (error) {
			log_error(ls, "recover_masters failed %d", error);
			goto fail;
		}

		/*
		 * Send our locks on remastered rsb's to the new masters.
		 */

		error = dlm_recover_locks(ls);
		if (error) {
			log_error(ls, "recover_locks failed %d", error);
			goto fail;
		}

		dlm_recover_lvbs(ls);
	}
	dlm_release_root_list(ls);

	log_debug(ls, "recover event %u done", rv->event_id);

	set_start_done(ls, rv->event_id);
	up(&ls->ls_recoverd_active);
	return 0;

 fail:
	log_debug(ls, "recover event %d error %d", rv->event_id, error);
	up(&ls->ls_recoverd_active);
	return error;
}

/*
 * Between calls to this routine for a ls, there can be multiple stop/start
 * events from cman where every start but the latest is cancelled by stops.
 * There can only be a single finish from cman because every finish requires us
 * to call start_done.  A single finish event could be followed by multiple
 * stop/start events.  This routine takes any combination of events from cman
 * and boils them down to one course of action.
 */

static int next_move(struct dlm_ls *ls, struct dlm_recover **rv_out,
		     int *finish_out)
{
	LIST_HEAD(events);
	unsigned int cmd = 0, stop, start, finish;
	unsigned int last_stop, last_start, last_finish;
	struct dlm_recover *rv = NULL, *start_rv = NULL;

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
		rv = list_entry(ls->ls_recover.next, struct dlm_recover, list);
		list_del(&rv->list);
		list_add_tail(&rv->list, &events);
	}

	/*
	 * There are two cases where we need to adjust these event values:
	 * 1. - we get a first start
	 *    - we get a stop
	 *    - we process the start + stop here and notice this special case
	 * 
	 * 2. - we get a first start
	 *    - we process the start
	 *    - we get a stop
	 *    - we process the stop here and notice this special case
	 *
	 * In both cases, the first start we received was aborted by a
	 * stop before we received a finish.  last_finish being zero is the
	 * indication that this is the "first" start, i.e. we've not yet
	 * finished a start; if we had, last_finish would be non-zero.
	 * Part of the problem arises from the fact that when we initially
	 * get start/stop/start, SM uses the same event id for both starts
	 * (since the first was cancelled).
	 *
	 * In both cases, last_start and last_stop will be equal.
	 * In both cases, finish=0.
	 * In the first case start=1 && stop=1.
	 * In the second case start=0 && stop=1.
	 *
	 * In both cases, we need to make adjustments to values so:
	 * - we process the current event (now) as a normal stop
	 * - the next start we receive will be processed normally
	 *   (taking into account the assertions below)
	 *
	 * In the first case, dlm_ls_start() will have printed the
	 * "repeated start" warning.
	 *
	 * In the first case we need to get rid of the recover event struct.
	 *
	 * - set stop=1, start=0, finish=0 for case 4 below
	 * - last_stop and last_start must be set equal per the case 4 assert
	 * - ls_last_stop = 0 so the next start will be larger
	 * - ls_last_start = 0 not really necessary (avoids dlm_ls_start print)
	 */

	if (!last_finish && (last_start == last_stop)) {
		log_debug(ls, "move reset %u,%u,%u ids %u,%u,%u", stop,
			  start, finish, last_stop, last_start, last_finish);
		stop = 1;
		start = 0;
		finish = 0;
		last_stop = 0;
		last_start = 0;
		ls->ls_last_stop = 0;
		ls->ls_last_start = 0;

		while (!list_empty(&events)) {
			rv = list_entry(events.next, struct dlm_recover, list);
			list_del(&rv->list);
			kfree(rv->nodeids);
			kfree(rv);
		}
	}
	spin_unlock(&ls->ls_recover_lock);

	log_debug(ls, "move flags %u,%u,%u ids %u,%u,%u", stop, start, finish,
		  last_stop, last_start, last_finish);

	/*
	 * Toss start events which have since been cancelled.
	 */

	while (!list_empty(&events)) {
		DLM_ASSERT(start,);
		rv = list_entry(events.next, struct dlm_recover, list);
		list_del(&rv->list);

		if (rv->event_id <= last_stop) {
			log_debug(ls, "move skip event %u", rv->event_id);
			kfree(rv->nodeids);
			kfree(rv);
			rv = NULL;
		} else {
			log_debug(ls, "move use event %u", rv->event_id);
			DLM_ASSERT(!start_rv,);
			start_rv = rv;
		}
	}

	/*
	 * Eight possible combinations of events.
	 */

	/* 0 */
	if (!stop && !start && !finish) {
		DLM_ASSERT(!start_rv,);
		cmd = 0;
		goto out;
	}

	/* 1 */
	if (!stop && !start && finish) {
		DLM_ASSERT(!start_rv,);
		DLM_ASSERT(last_start > last_stop,);
		DLM_ASSERT(last_finish == last_start,);
		cmd = DO_FINISH;
		*finish_out = last_finish;
		goto out;
	}

	/* 2 */
	if (!stop && start && !finish) {
		DLM_ASSERT(start_rv,);
		DLM_ASSERT(last_start > last_stop,);
		cmd = DO_START;
		*rv_out = start_rv;
		goto out;
	}

	/* 3 */
	if (!stop && start && finish) {
		DLM_ASSERT(0, printk("finish and start with no stop\n"););
	}

	/* 4 */
	if (stop && !start && !finish) {
		DLM_ASSERT(!start_rv,);
		DLM_ASSERT(last_start == last_stop,);
		cmd = DO_STOP;
		goto out;
	}

	/* 5 */
	if (stop && !start && finish) {
		DLM_ASSERT(!start_rv,);
		DLM_ASSERT(last_finish == last_start,);
		DLM_ASSERT(last_stop == last_start,);
		cmd = DO_FINISH_STOP;
		*finish_out = last_finish;
		goto out;
	}

	/* 6 */
	if (stop && start && !finish) {
		if (start_rv) {
			DLM_ASSERT(last_start > last_stop,);
			cmd = DO_START;
			*rv_out = start_rv;
		} else {
			DLM_ASSERT(last_stop == last_start,);
			cmd = DO_STOP;
		}
		goto out;
	}

	/* 7 */
	if (stop && start && finish) {
		if (start_rv) {
			DLM_ASSERT(last_start > last_stop,);
			DLM_ASSERT(last_start > last_finish,);
			cmd = DO_FINISH_START;
			*finish_out = last_finish;
			*rv_out = start_rv;
		} else {
			DLM_ASSERT(last_start == last_stop,);
			DLM_ASSERT(last_start > last_finish,);
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

static void do_ls_recovery(struct dlm_ls *ls)
{
	struct dlm_recover *rv = NULL;
	int error, cur_state, next_state = 0, do_now, finish_event = 0;

	do_now = next_move(ls, &rv, &finish_event);
	if (!do_now)
		goto out;

	cur_state = ls->ls_state;
	next_state = 0;

	DLM_ASSERT(!test_bit(LSFL_LS_RUN, &ls->ls_flags),
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
			error = ls_reconfig(ls, rv);
			if (error)
				next_state = LSST_WAIT_START;
			else
				next_state = LSST_RECONFIG_DONE;
			break;

		case DO_FINISH:	/* invalid */
		case DO_FINISH_STOP:	/* invalid */
		case DO_FINISH_START:	/* invalid */
		default:
			DLM_ASSERT(0,);
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
			error = ls_reconfig(ls, rv);
			if (error)
				next_state = LSST_WAIT_START;
			else
				next_state = LSST_RECONFIG_DONE;
			break;

		case DO_FINISH:	/* invalid */
		case DO_FINISH_STOP:	/* invalid */
		case DO_FINISH_START:	/* invalid */
		default:
			DLM_ASSERT(0,);
		}
		goto out;
	}

	/*
	 * LSST_RECONFIG_DONE - we entered this state after successfully
	 * completing ls_reconfig and calling set_start_done.  We expect to get
	 * a finish if everything goes ok.  A finish could be followed by stop
	 * or stop/start before we get here to check it.  Or a finish may never
	 * happen, only stop or stop/start.
	 */

	if (cur_state == LSST_RECONFIG_DONE) {
		switch (do_now) {
		case DO_FINISH:
			dlm_clear_members_finish(ls, finish_event);
			next_state = LSST_CLEAR;

			error = enable_locking(ls, finish_event);
			if (error)
				break;

			error = dlm_process_requestqueue(ls);
			if (error)
				break;

			error = dlm_recover_waiters_post(ls);
			if (error)
				break;

			dlm_grant_after_purge(ls);

			dlm_astd_wake();

			log_debug(ls, "recover event %u finished", finish_event);
			break;

		case DO_STOP:
			next_state = LSST_WAIT_START;
			break;

		case DO_FINISH_STOP:
			dlm_clear_members_finish(ls, finish_event);
			next_state = LSST_WAIT_START;
			break;

		case DO_FINISH_START:
			dlm_clear_members_finish(ls, finish_event);
			/* fall into DO_START */

		case DO_START:
			error = ls_reconfig(ls, rv);
			if (error)
				next_state = LSST_WAIT_START;
			else
				next_state = LSST_RECONFIG_DONE;
			break;

		default:
			DLM_ASSERT(0,);
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
			error = ls_first_start(ls, rv);
			if (!error)
				next_state = LSST_INIT_DONE;
			break;

		case DO_STOP:
			break;

		case DO_FINISH:	/* invalid */
		case DO_FINISH_STOP:	/* invalid */
		case DO_FINISH_START:	/* invalid */
		default:
			DLM_ASSERT(0,);
		}
		goto out;
	}

	/*
	 * LSST_INIT_DONE - after the first start operation is completed
	 * successfully and set_start_done() called.  If there are no errors, a
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
			error = ls_reconfig(ls, rv);
			if (error)
				next_state = LSST_WAIT_START;
			else
				next_state = LSST_RECONFIG_DONE;
			break;

		case DO_FINISH:
			next_state = LSST_CLEAR;

			enable_locking(ls, finish_event);

			dlm_process_requestqueue(ls);

			dlm_astd_wake();

			log_debug(ls, "recover event %u finished", finish_event);
			break;

		default:
			DLM_ASSERT(0,);
		}
		goto out;
	}

 out:
	if (next_state)
		ls->ls_state = next_state;

	if (rv) {
		kfree(rv->nodeids);
		kfree(rv);
	}
}

int dlm_recoverd(void *arg)
{
	struct dlm_ls *ls = arg;

	dlm_hold_lockspace(ls);

	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (!test_bit(LSFL_WORK, &ls->ls_flags))
			schedule();
		set_current_state(TASK_RUNNING);

		if (test_and_clear_bit(LSFL_WORK, &ls->ls_flags))
			do_ls_recovery(ls);
	}

	dlm_put_lockspace(ls);
	return 0;
}

void dlm_recoverd_kick(struct dlm_ls *ls)
{
	set_bit(LSFL_WORK, &ls->ls_flags);
	wake_up_process(ls->ls_recoverd_task);
}

int dlm_recoverd_start(struct dlm_ls *ls)
{
	struct task_struct *p;
	int error = 0;

	p = kthread_run(dlm_recoverd, ls, "dlm_recoverd");
	if (IS_ERR(p))
		error = PTR_ERR(p);
	else
                ls->ls_recoverd_task = p;
	return error;
}

void dlm_recoverd_stop(struct dlm_ls *ls)
{
	kthread_stop(ls->ls_recoverd_task);
}

void dlm_recoverd_suspend(struct dlm_ls *ls)
{
	down(&ls->ls_recoverd_active);
}

void dlm_recoverd_resume(struct dlm_ls *ls)
{
	up(&ls->ls_recoverd_active);
}

