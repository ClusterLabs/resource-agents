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

/*
 * Called in recovery routines to check whether the recovery process has been
 * interrupted/stopped by another transition.  A recovery in-process will abort
 * if the lockspace is "stopped" so that a new recovery process can start from
 * the beginning when the lockspace is "started" again.
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

int dlm_wait_status_all(struct dlm_ls *ls, unsigned int wait_status)
{
	struct dlm_rcom rc_stack, *rc;
	struct dlm_member *memb;
	int status;
	int error = 0;

	memset(&rc_stack, 0, sizeof(struct dlm_rcom));
	rc = &rc_stack;
	rc->rc_datalen = 0;

	list_for_each_entry(memb, &ls->ls_nodes, list) {
		for (;;) {
			error = dlm_recovery_stopped(ls);
			if (error)
				goto out;

			error = dlm_send_rcom(ls, memb->node->nodeid,
					      DLM_RCOM_STATUS, rc, 1);
			if (error)
				goto out;

			status = rc->rc_buf[0];
			if (status & wait_status)
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
	struct dlm_rcom rc_stack, *rc;
	int nodeid = ls->ls_low_nodeid;
	int status;
	int error = 0;

	memset(&rc_stack, 0, sizeof(struct dlm_rcom));
	rc = &rc_stack;
	rc->rc_datalen = 0;

	for (;;) {
		error = dlm_recovery_stopped(ls);
		if (error)
			goto out;

		error = dlm_send_rcom(ls, nodeid, DLM_RCOM_STATUS, rc, 1);
		if (error)
			break;

		status = rc->rc_buf[0];
		if (status & wait_status)
			break;
		else {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(HZ >> 1);
		}
	}

 out:
	return error;
}

#if 0
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
 * The NEW_MASTER flag tells rebuild_rsbs_send() which rsb's to consider.
 * The NEW_MASTER2 flag tells rsb_lvb_recovery() which rsb's to consider.
 */

static void set_new_master(struct dlm_rsb *r, int nodeid)
{
	lock_rsb(r);

	if (nodeid == dlm_our_nodeid())
		r->res_nodeid = 0;
	else
		r->res_nodeid = nodeid;

	set_master_lkbs(r);

#if 0
	list_for_each_entry(subrsb, &r->res_subreslist, res_subreslist) {
		subrsb->res_nodeid = r->res_nodeid;
		set_master_lkbs(subrsb);
	}
#endif

	set_bit(RESFL_NEW_MASTER, &r->res_flags);
	set_bit(RESFL_NEW_MASTER2, &r->res_flags);
	unlock_rsb(r);
}

/*
 * The recover_list contains all the rsb's for which we've requested the new
 * master nodeid.  As replies are returned from the resource directories the
 * rsb's are removed from the list.  When the list is empty we're done.
 *
 * The recover_list is later similarly used for all rsb's for which we've sent
 * new lkb's and need to receive new corresponding lkid's.
 */

int recover_list_empty(struct dlm_ls *ls)
{
	int empty;

	spin_lock(&ls->ls_recover_list_lock);
	empty = list_empty(&ls->ls_recover_list);
	spin_unlock(&ls->ls_recover_list_lock);

	return empty;
}

int recover_list_count(struct dlm_ls *ls)
{
	int count;

	spin_lock(&ls->ls_recover_list_lock);
	count = ls->ls_recover_list_count;
	spin_unlock(&ls->ls_recover_list_lock);

	return count;
}

void recover_list_add(struct dlm_rsb *rsb)
{
	struct dlm_ls *ls = rsb->res_ls;

	spin_lock(&ls->ls_recover_list_lock);
	if (!test_and_set_bit(RESFL_RECOVER_LIST, &rsb->res_flags)) {
		list_add_tail(&rsb->res_recover_list, &ls->ls_recover_list);
		ls->ls_recover_list_count++;
		hold_rsb(rsb);
	}
	spin_unlock(&ls->ls_recover_list_lock);
}

void recover_list_del(struct dlm_rsb *rsb)
{
	struct dlm_ls *ls = rsb->res_ls;

	spin_lock(&ls->ls_recover_list_lock);
	clear_bit(RESFL_RECOVER_LIST, &rsb->res_flags);
	list_del(&rsb->res_recover_list);
	ls->ls_recover_list_count--;
	spin_unlock(&ls->ls_recover_list_lock);

	release_rsb(rsb);
}

static struct dlm_rsb *recover_list_find(struct dlm_ls *ls, int msgid)
{
	struct dlm_rsb *rsb = NULL;

	spin_lock(&ls->ls_recover_list_lock);

	list_for_each_entry(rsb, &ls->ls_recover_list, res_recover_list) {
		if (rsb->res_recover_msgid == msgid)
		        goto rec_found;
	}
	rsb = NULL;

 rec_found:
	spin_unlock(&ls->ls_recover_list_lock);
	return rsb;
}

void recover_list_clear(struct dlm_ls *ls)
{
	struct dlm_rsb *r, *s;

	spin_lock(&ls->ls_recover_list_lock);
	list_for_each_entry_safe(r, s, &ls->ls_recover_list, res_recover_list) {
		list_del(&r->res_recover_list);
		clear_bit(RESFL_RECOVER_LIST, &r->res_flags);
		release_rsb(r);
		ls->ls_recover_list_count--;
	}

	if (ls->ls_recover_list_count != 0) {
		log_error(ls, "warning: recover_list_count %d",
			  ls->ls_recover_list_count);
		ls->ls_recover_list_count = 0;
	}
	spin_unlock(&ls->ls_recover_list_lock);
}

/* We do async lookups on rsb's that need new masters.  The rsb's
   waiting for a lookup reply are kept on the recover_list. */

static int recover_master(struct dlm_rsb *r, struct dlm_rcom *rc)
{
	struct dlm_ls *ls = r->res_ls;
	int error, dir_nodeid, ret_nodeid, our_nodeid = dlm_our_nodeid();

	/* very similar to set_master() */

	dir_nodeid = dlm_dir_nodeid(r);

	if (dir_nodeid == our_nodeid) {
		error = dlm_dir_lookup(ls, our_nodeid, r->res_name,
				       r->res_length, &ret_nodeid);

		/* FIXME: is -EEXIST ever a valid error here? */
		if (error)
			log_error(ls, "recover dir lookup error %d", error);

		set_new_master(r, ret_nodeid);
	} else {
		/* FIXME: set msgid's differently (see dlm_send_rcom) */
		r->res_recover_msgid = ls->ls_rcom_msgid + 1;

		recover_list_add(r);

		memcpy(rc->rc_buf, r->res_name, r->res_length);
		rc->rc_datalen = r->res_length;

		error = dlm_send_rcom(ls, dir_nodeid, DLM_RCOM_LOOKUP, rc, 0);
		if (error)
			goto out;
	}
 out:
	return error;
}

/*
 * Go through local root resources and for each rsb which has a master which
 * has departed, get the new master nodeid from the resdir.  The resdir will
 * assign mastery to the first node to look up the new master.  That means
 * we'll discover in this lookup if we're the new master of any rsb's.
 *
 * We fire off all the resdir requests individually and asynchronously to the
 * correct resdir node.  The replies are processed in rsb_master_recv().
 */

int dlm_recover_masters(struct dlm_ls *ls)
{
	struct dlm_rsb *r;
	struct dlm_rcom *rc;
	int error = -ENOMEM;

	log_debug(ls, "dlm_recover_masters");

	rc = allocate_rcom_buffer(ls);
	if (!rc)
		goto out;

	down_read(&ls->ls_root_lock);
	list_for_each_entry(r, &ls->ls_rootres, res_rootlist) {
		error = dlm_recovery_stopped(ls);
		if (error) {
			up_read(&ls->ls_root_lock);
			goto out_free;
		}

		clear_bit(RESFL_VALNOTVALID_PREV, &r->res_flags);
		if (test_bit(RESFL_VALNOTVALID, &r->res_flags))
			set_bit(RESFL_VALNOTVALID_PREV, &r->res_flags);

		if (dlm_is_removed(ls, r->res_nodeid))
			recover_master(r, rc);

		schedule();
	}
	up_read(&ls->ls_root_lock);

	error = dlm_wait_function(ls, &recover_list_empty);

 out_free:
	if (error)
		recover_list_clear(ls);
	free_rcom_buffer(rc);
 out:
	return error;
}

int dlm_recover_master_reply(struct dlm_ls *ls, struct dlm_rcom *rc)
{
	struct dlm_rsb *r;
	uint32_t be_nodeid;

	r = recover_list_find(ls, rc->rc_msgid);
	if (!r) {
		log_error(ls, "dlm_recover_master_reply no msgid %d",
			  rc->rc_msgid);
		goto out;
	}

	memcpy(&be_nodeid, rc->rc_buf, sizeof(uint32_t));

	set_new_master(r, be32_to_cpu(be_nodeid));

	recover_list_del(r);

	if (recover_list_empty(ls))
		wake_up(&ls->ls_wait_general);
 out:
	return 0;
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
 * from the lkb with the largest lvb sequence nubmer.
 */

void rsb_lvb_recovery(struct dlm_rsb *r)
{
	struct dlm_lkb *lkb, *high_lkb = NULL;
	uint32_t high_seq = 0;
	int lock_lvb_exists = FALSE;
	int big_lock_exists = FALSE;

	down_write(&r->res_lock);

	list_for_each_entry(lkb, &r->res_grantqueue, lkb_statequeue) {
		if (!(lkb->lkb_flags & GDLM_LKFLG_VALBLK))
			continue;

		if (lkb->lkb_flags & GDLM_LKFLG_DELETED)
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
		if (!(lkb->lkb_flags & GDLM_LKFLG_VALBLK))
			continue;

		if (lkb->lkb_flags & GDLM_LKFLG_DELETED)
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
	up_write(&r->res_lock);
}

int dlm_lvb_recovery(struct dlm_ls *ls)
{
	struct dlm_rsb *root;
	struct dlm_rsb *subrsb;

	down_read(&ls->ls_root_lock);
	list_for_each_entry(root, &ls->ls_rootres, res_rootlist) {
		if (root->res_nodeid)
			continue;

		rsb_lvb_recovery(root);
		list_for_each_entry(subrsb, &root->res_subreslist, res_subreslist) {
			rsb_lvb_recovery(subrsb);
		}
	}
	up_read(&ls->ls_root_lock);
	return 0;
}
#endif

