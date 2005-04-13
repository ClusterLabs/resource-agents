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
#include "lockspace.h"
#include "member.h"
#include "dir.h"
#include "config.h"
#include "ast.h"
#include "memory.h"
#include "rcom.h"
#include "lock.h"

void recover_rsb_lvb(struct dlm_rsb *r);


/*
 * Recovery waiting routines: these functions wait for a particular reply from
 * a remote node, or for the remote node to report a certain status.  They need
 * to abort if the lockspace is stopped indicating a node has failed (perhaps
 * the one being waited for).
 */

int dlm_recovery_stopped(struct dlm_ls *ls)
{
	return test_bit(LSFL_LS_STOP, &ls->ls_flags);
}

/*
 * Wait until given function returns non-zero or lockspace is stopped (LS_STOP
 * set due to failure of a node in ls_nodes).  When another function thinks it
 * could have completed the waited-on task, they should wake up ls_wait_general
 * to get an immediate response rather than waiting for the timer to detect the
 * result.  A timer wakes us up periodically while waiting to see if we should
 * abort due to a node failure.
 */

int dlm_wait_function(struct dlm_ls *ls, int (*testfn) (struct dlm_ls *ls))
{
	int error = 0;

	for (;;) {
		wait_event_interruptible_timeout(ls->ls_wait_general,
					testfn(ls) ||
					test_bit(LSFL_LS_STOP, &ls->ls_flags),
					(dlm_config.recover_timer * HZ));
		if (testfn(ls))
			break;
		if (dlm_recovery_stopped(ls)) {
			error = -1;
			break;
		}
	}

	return error;
}

/*
 * An efficient way for all nodes to wait for all others to have a certain
 * status.  The node with the lowest nodeid polls all the others for their
 * status (dlm_wait_status_all) and all the others poll the node with the low
 * id for its accumulated result (dlm_wait_status_low).
 */

int dlm_wait_status_all(struct dlm_ls *ls, unsigned int wait_status)
{
	struct dlm_rcom *rc = (struct dlm_rcom *) ls->ls_recover_buf;
	struct dlm_member *memb;
	int error = 0;

	list_for_each_entry(memb, &ls->ls_nodes, list) {
		for (;;) {
			error = dlm_recovery_stopped(ls);
			if (error)
				goto out;

			error = dlm_rcom_status(ls, memb->node->nodeid);
			if (error)
				goto out;

			if (rc->rc_result & wait_status)
				break;
			else {
				set_current_state(TASK_INTERRUPTIBLE);
				schedule_timeout(HZ >> 1);
			}
		}
	}
 out:
	return error;
}

int dlm_wait_status_low(struct dlm_ls *ls, unsigned int wait_status)
{
	struct dlm_rcom *rc = (struct dlm_rcom *) ls->ls_recover_buf;
	int error = 0, nodeid = ls->ls_low_nodeid;

	for (;;) {
		error = dlm_recovery_stopped(ls);
		if (error)
			goto out;

		error = dlm_rcom_status(ls, nodeid);
		if (error)
			break;

		if (rc->rc_result & wait_status)
			break;
		else {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(HZ >> 1);
		}
	}
 out:
	return error;
}

/*
 * The recover_list contains all the rsb's for which we've requested the new
 * master nodeid.  As replies are returned from the resource directories the
 * rsb's are removed from the list.  When the list is empty we're done.
 *
 * The recover_list is later similarly used for all rsb's for which we've sent
 * new lkb's and need to receive new corresponding lkid's.
 *
 * We use the address of the rsb struct as a simple local identifier for the
 * rsb so we can match an rcom reply with the rsb it was sent for.
 */

static int compare_rc_id(struct dlm_rsb *r, uint64_t id)
{
#if BITS_PER_LONG == 32
	if ((uint32_t) r == (uint32_t) id)
		return TRUE;
	return FALSE;
#elif BITS_PER_LONG == 64
	if ((uint64_t) r == id)
		return TRUE;
	return FALSE;
#else
#error BITS_PER_LONG not defined
#endif
}

int recover_list_empty(struct dlm_ls *ls)
{
	int empty;

	spin_lock(&ls->ls_recover_list_lock);
	empty = list_empty(&ls->ls_recover_list);
	spin_unlock(&ls->ls_recover_list_lock);

	return empty;
}

void recover_list_add(struct dlm_rsb *r)
{
	struct dlm_ls *ls = r->res_ls;

	spin_lock(&ls->ls_recover_list_lock);
	if (list_empty(&r->res_recover_list)) {
		list_add_tail(&r->res_recover_list, &ls->ls_recover_list);
		ls->ls_recover_list_count++;
		dlm_hold_rsb(r);
	}
	spin_unlock(&ls->ls_recover_list_lock);
}

void recover_list_del(struct dlm_rsb *r)
{
	struct dlm_ls *ls = r->res_ls;

	spin_lock(&ls->ls_recover_list_lock);
	list_del_init(&r->res_recover_list);
	ls->ls_recover_list_count--;
	spin_unlock(&ls->ls_recover_list_lock);

	dlm_put_rsb(r);
}

static struct dlm_rsb *recover_list_find(struct dlm_ls *ls, uint64_t id)
{
	struct dlm_rsb *r = NULL;

	spin_lock(&ls->ls_recover_list_lock);

	list_for_each_entry(r, &ls->ls_recover_list, res_recover_list) {
		if (compare_rc_id(r, id))
			goto out;
	}
	r = NULL;
 out:
	spin_unlock(&ls->ls_recover_list_lock);
	return r;
}

void recover_list_clear(struct dlm_ls *ls)
{
	struct dlm_rsb *r, *s;

	spin_lock(&ls->ls_recover_list_lock);
	list_for_each_entry_safe(r, s, &ls->ls_recover_list, res_recover_list) {
		list_del_init(&r->res_recover_list);
		dlm_put_rsb(r);
		ls->ls_recover_list_count--;
	}

	if (ls->ls_recover_list_count != 0) {
		log_error(ls, "warning: recover_list_count %d",
			  ls->ls_recover_list_count);
		ls->ls_recover_list_count = 0;
	}
	spin_unlock(&ls->ls_recover_list_lock);
}


/* Master recovery: find new master node for rsb's that were
   mastered on nodes that have been removed.

   dlm_recover_masters
   recover_master
   dlm_send_rcom_lookup            ->  receive_rcom_lookup
                                       dlm_dir_lookup
   receive_rcom_lookup_reply       <-
   dlm_recover_master_reply
   set_new_master
   set_master_lkbs
   set_lock_master
*/

/*
 * Set the lock master for all LKBs in a lock queue
 * If we are the new master of the rsb, we may have received new
 * MSTCPY locks from other nodes already which we need to ignore
 * when setting the new nodeid.
 */

static void set_lock_master(struct list_head *queue, int nodeid)
{
	struct dlm_lkb *lkb;

	list_for_each_entry(lkb, queue, lkb_statequeue)
		if (!(lkb->lkb_flags & DLM_IFL_MSTCPY))
			lkb->lkb_nodeid = nodeid;
}

static void set_master_lkbs(struct dlm_rsb *r)
{
	set_lock_master(&r->res_grantqueue, r->res_nodeid);
	set_lock_master(&r->res_convertqueue, r->res_nodeid);
	set_lock_master(&r->res_waitqueue, r->res_nodeid);
}

/*
 * Propogate the new master nodeid to locks, subrsbs, sublocks.
 * The NEW_MASTER flag tells dlm_recover_locks() which rsb's to consider.
 * The NEW_MASTER2 flag tells dlm_recover_lvbs() which rsb's to consider.
 */

static void set_new_master(struct dlm_rsb *r, int nodeid)
{
	dlm_lock_rsb(r);

	/* FIXME: what if there are lkb's waiting on res_lookup ? */

	if (nodeid == dlm_our_nodeid())
		r->res_nodeid = 0;
	else
		r->res_nodeid = nodeid;

	set_master_lkbs(r);

	/*
	list_for_each_entry(sr, &r->res_subreslist, res_subreslist) {
		sr->res_nodeid = r->res_nodeid;
		set_master_lkbs(sr);
	}
	*/

	set_bit(RESFL_NEW_MASTER, &r->res_flags);
	set_bit(RESFL_NEW_MASTER2, &r->res_flags);
	dlm_unlock_rsb(r);
}

/*
 * We do async lookups on rsb's that need new masters.  The rsb's
 * waiting for a lookup reply are kept on the recover_list.
 */

static int recover_master(struct dlm_rsb *r)
{
	struct dlm_ls *ls = r->res_ls;
	int error, dir_nodeid, ret_nodeid, our_nodeid = dlm_our_nodeid();

	dir_nodeid = dlm_dir_nodeid(r);

	if (dir_nodeid == our_nodeid) {
		error = dlm_dir_lookup(ls, our_nodeid, r->res_name,
				       r->res_length, &ret_nodeid);

		/* FIXME: is -EEXIST ever a valid error here? */
		if (error)
			log_error(ls, "recover dir lookup error %d", error);

		set_new_master(r, ret_nodeid);
	} else {
		recover_list_add(r);
		error = dlm_send_rcom_lookup(r, dir_nodeid);
	}

	return error;
}

/*
 * Go through local root resources and for each rsb which has a master which
 * has departed, get the new master nodeid from the directory.  The dir will
 * assign mastery to the first node to look up the new master.  That means
 * we'll discover in this lookup if we're the new master of any rsb's.
 *
 * We fire off all the dir lookup requests individually and asynchronously to
 * the correct dir node.
 */

int dlm_recover_masters(struct dlm_ls *ls)
{
	struct dlm_rsb *r;
	int error;

	log_debug(ls, "dlm_recover_masters");

	down_read(&ls->ls_root_lock);
	list_for_each_entry(r, &ls->ls_rootres, res_rootlist) {
		if (!r->res_nodeid)
			continue;

		error = dlm_recovery_stopped(ls);
		if (error) {
			up_read(&ls->ls_root_lock);
			goto out;
		}

		clear_bit(RESFL_VALNOTVALID_PREV, &r->res_flags);
		if (test_bit(RESFL_VALNOTVALID, &r->res_flags))
			set_bit(RESFL_VALNOTVALID_PREV, &r->res_flags);

		if (dlm_is_removed(ls, r->res_nodeid))
			recover_master(r);

		schedule();
	}
	up_read(&ls->ls_root_lock);

	error = dlm_wait_function(ls, &recover_list_empty);

 out:
	if (error)
		recover_list_clear(ls);
	return error;
}

int dlm_recover_master_reply(struct dlm_ls *ls, struct dlm_rcom *rc)
{
	struct dlm_rsb *r;

	r = recover_list_find(ls, rc->rc_id);
	if (!r) {
		log_error(ls, "dlm_recover_master_reply no id %"PRIx64"",
			  rc->rc_id);
		goto out;
	}

	set_new_master(r, rc->rc_result);
	recover_list_del(r);

	if (recover_list_empty(ls))
		wake_up(&ls->ls_wait_general);
 out:
	return 0;
}


/* Lock recovery: rebuild the process-copy locks we hold on a
   remastered rsb on the new rsb master.

   dlm_recover_locks
   recover_locks
   recover_locks_queue
   dlm_send_rcom_lock              ->  receive_rcom_lock
                                       dlm_recover_master_copy
   receive_rcom_lock_reply         <-
   dlm_recover_process_copy
*/


/*
 * keep a count of the number of lkb's we send to the new master; when we get
 * an equal number of replies then recovery for the rsb is done
 */

static int recover_locks_queue(struct dlm_rsb *r, struct list_head *head)
{
	struct dlm_lkb *lkb;
	int error = 0;

	list_for_each_entry(lkb, head, lkb_statequeue) {
	   	error = dlm_send_rcom_lock(r, lkb);
		if (error)
			break;
		r->res_recover_locks_count++;
	}

	return error;
}

static int all_queues_empty(struct dlm_rsb *r)
{
	if (!list_empty(&r->res_grantqueue) ||
	    !list_empty(&r->res_convertqueue) ||
	    !list_empty(&r->res_waitqueue))
		return FALSE;
	/*
	list_for_each_entry(sr, &r->res_subreslist, res_subreslist) {
		if (!list_empty(&sr->res_grantqueue) ||
	    	    !list_empty(&sr->res_convertqueue) ||
	    	    !list_empty(&sr->res_waitqueue))
			return FALSE;
	}
	*/
	return TRUE;
}

static int recover_locks(struct dlm_rsb *r)
{
	int error = 0;

	if (all_queues_empty(r))
		goto out;

	recover_list_add(r);

	error = recover_locks_queue(r, &r->res_grantqueue);
	if (error)
		goto out;
	error = recover_locks_queue(r, &r->res_convertqueue);
	if (error)
		goto out;
	error = recover_locks_queue(r, &r->res_waitqueue);
	if (error)
		goto out;

	/*
	list_for_each_entry(sr, &r->res_subreslist, res_subreslist) {
		error = recover_locks_queue(sr, &sr->res_grantqueue);
		if (error)
			goto out
		error = recover_locks_queue(sr, &sr->res_convertqueue);
		if (error)
			goto out
		error = recover_locks_queue(sr, &sr->res_waitqueue);
		if (error)
			goto out
	}
	*/
 out:
	return error;
}

int dlm_recover_locks(struct dlm_ls *ls)
{
	struct dlm_rsb *r;
	int error;

	log_debug(ls, "dlm_recover_locks");

	down_read(&ls->ls_root_lock);
	list_for_each_entry(r, &ls->ls_rootres, res_rootlist) {
		if (!r->res_nodeid)
			continue;

		error = dlm_recovery_stopped(ls);
		if (error) {
			up_read(&ls->ls_root_lock);
			goto out;
		}

		if (test_bit(RESFL_NEW_MASTER, &r->res_flags))
			recover_locks(r);
	}
	up_read(&ls->ls_root_lock);

	error = dlm_wait_function(ls, &recover_list_empty);

 out:
	if (error)
		recover_list_clear(ls);
	return error;
}

void dlm_recovered_lock(struct dlm_rsb *r)
{
	r->res_recover_locks_count--;
	if (!r->res_recover_locks_count) {
		clear_bit(RESFL_NEW_MASTER, &r->res_flags);
		recover_list_del(r);
	}

	if (recover_list_empty(r->res_ls))
		wake_up(&r->res_ls->ls_wait_general);

	recover_rsb_lvb(r);
}


/*
 * This routine is called on all master rsb's by dlm_recoverd.  It is also
 * called on an rsb when a new lkb is received during the rebuild recovery
 * stage (implying we are the new master for it.)  So, a newly mastered rsb
 * will often have this function called on it by dlm_recoverd and by dlm_recvd
 * when a new lkb is received.
 *
 * This function is in charge of making sure the rsb's VALNOTVALID flag is
 * set correctly and that the lvb contents are set correctly.
 *
 * RESFL_VALNOTVALID is set if:
 * - it was set prior to recovery, OR
 * - there are only NL/CR locks on the rsb
 *
 * RESFL_VALNOTVALID is cleared if:
 * - it was not set prior to recovery, AND
 * - there are locks > CR on the rsb
 *
 * (We'll only be clearing VALNOTVALID in this function if it
 *  was set in a prior call to this function when there were
 *  only NL/CR locks.)
 *
 * Whether this node is a new or old master of the rsb is not a factor
 * in the decision to set/clear VALNOTVALID.
 *
 * The LVB contents are only considered for changing when this is a new master
 * of the rsb (NEW_MASTER2).  Then, the rsb's lvb is taken from any lkb with
 * mode > CR.  If no lkb's exist with mode above CR, the lvb contents are taken
 * from the lkb with the largest lvb sequence number.
 */

void recover_rsb_lvb(struct dlm_rsb *r)
{
	struct dlm_lkb *lkb, *high_lkb = NULL;
	uint32_t high_seq = 0;
	int lock_lvb_exists = FALSE;
	int big_lock_exists = FALSE;

	list_for_each_entry(lkb, &r->res_grantqueue, lkb_statequeue) {
		if (!(lkb->lkb_exflags & DLM_LKF_VALBLK))
			continue;

		lock_lvb_exists = TRUE;

		if (lkb->lkb_grmode > DLM_LOCK_CR) {
			big_lock_exists = TRUE;
			goto setflag;
		}

		if (((int)lkb->lkb_lvbseq - (int)high_seq) >= 0) {
			high_lkb = lkb;
			high_seq = lkb->lkb_lvbseq;
		}
	}

	list_for_each_entry(lkb, &r->res_convertqueue, lkb_statequeue) {
		if (!(lkb->lkb_exflags & DLM_LKF_VALBLK))
			continue;

		lock_lvb_exists = TRUE;

		if (lkb->lkb_grmode > DLM_LOCK_CR) {
			big_lock_exists = TRUE;
			goto setflag;
		}

		if (((int)lkb->lkb_lvbseq - (int)high_seq) >= 0) {
			high_lkb = lkb;
			high_seq = lkb->lkb_lvbseq;
		}
	}

 setflag:
	/* there are no locks with lvb's */
	if (!lock_lvb_exists)
		goto out;

	/* don't clear valnotvalid if it was already set */
	if (test_bit(RESFL_VALNOTVALID_PREV, &r->res_flags))
		goto setlvb;

	if (big_lock_exists)
		clear_bit(RESFL_VALNOTVALID, &r->res_flags);
	else
		set_bit(RESFL_VALNOTVALID, &r->res_flags);

 setlvb:
	/* don't mess with the lvb unless we're the new master */
	if (!test_bit(RESFL_NEW_MASTER2, &r->res_flags))
		goto out;

	if (!r->res_lvbptr)
		r->res_lvbptr = allocate_lvb(r->res_ls);

	if (big_lock_exists) {
		r->res_lvbseq = lkb->lkb_lvbseq;
		memcpy(r->res_lvbptr, lkb->lkb_lvbptr, DLM_LVB_LEN);
	} else if (high_lkb) {
		r->res_lvbseq = high_lkb->lkb_lvbseq;
		memcpy(r->res_lvbptr, high_lkb->lkb_lvbptr, DLM_LVB_LEN);
	} else {
		r->res_lvbseq = 0;
		memset(r->res_lvbptr, 0, DLM_LVB_LEN);
	}
 out:
	return;
}

int dlm_recover_lvbs(struct dlm_ls *ls)
{
	struct dlm_rsb *r;

	down_read(&ls->ls_root_lock);
	list_for_each_entry(r, &ls->ls_rootres, res_rootlist) {
		if (r->res_nodeid)
			continue;

		dlm_lock_rsb(r);
		recover_rsb_lvb(r);
		dlm_unlock_rsb(r);

		/*
		list_for_each_entry(sr, &r->res_subreslist, res_subreslist)
			rsb_lvb_recovery(sr);
		*/
	}
	up_read(&ls->ls_root_lock);
	return 0;
}

