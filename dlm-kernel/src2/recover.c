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
#include "reccomms.h"
#include "dir.h"
#include "locking.h"
#include "rsb.h"
#include "lkb.h"
#include "config.h"
#include "ast.h"
#include "memory.h"

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

static void dlm_wait_timer_fn(unsigned long data)
{
	struct dlm_ls *ls = (struct dlm_ls *) data;

	wake_up(&ls->ls_wait_general);
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
	struct timer_list timer;
	int error = 0;

	init_timer(&timer);
	timer.function = dlm_wait_timer_fn;
	timer.data = (long) ls;

	for (;;) {
		mod_timer(&timer, jiffies + (dlm_config.recover_timer * HZ));

		error = wait_event_interruptible(ls->ls_wait_general,
					!testfn(ls) &&
					!test_bit(LSFL_LS_STOP, &ls->ls_flags));

		if (timer_pending(&timer))
			del_timer(&timer);

		if (error || testfn(ls))
			break;

		if (test_bit(LSFL_LS_STOP, &ls->ls_flags)) {
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

			error = rcom_send_message(ls, memb->node->nodeid,
						  RECCOMM_STATUS, rc, 1);
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
	uint32_t nodeid = ls->ls_low_nodeid;
	int status;
	int error = 0;

	memset(&rc_stack, 0, sizeof(struct dlm_rcom));
	rc = &rc_stack;
	rc->rc_datalen = 0;

	for (;;) {
		error = dlm_recovery_stopped(ls);
		if (error)
			goto out;

		error = rcom_send_message(ls, nodeid, RECCOMM_STATUS, rc, 1);
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

static int purge_queue(struct dlm_ls *ls, struct list_head *queue)
{
	struct dlm_lkb *lkb, *safe;
	struct dlm_rsb *rsb;
	int count = 0;

	list_for_each_entry_safe(lkb, safe, queue, lkb_statequeue) {
		if (!lkb->lkb_nodeid)
			continue;

		DLM_ASSERT(lkb->lkb_flags & GDLM_LKFLG_MSTCPY,);

		if (dlm_is_removed(ls, lkb->lkb_nodeid)) {
			list_del(&lkb->lkb_statequeue);

			rsb = lkb->lkb_resource;
			lkb->lkb_status = 0;

			release_lkb(ls, lkb);
			release_rsb_locked(rsb);
			count++;
		}
	}

	return count;
}

/*
 * Go through local restbl and for each rsb we're master of, clear out any
 * lkb's held by departed nodes.
 */

int restbl_lkb_purge(struct dlm_ls *ls)
{
	struct list_head *tmp2, *safe2;
	int count = 0;
	struct dlm_rsb *rootrsb, *safe, *rsb;

	log_debug(ls, "purge locks of departed nodes");
	down_write(&ls->ls_root_lock);

	list_for_each_entry_safe(rootrsb, safe, &ls->ls_rootres, res_rootlist) {

		if (rootrsb->res_nodeid)
			continue;

		hold_rsb(rootrsb);
		down_write(&rootrsb->res_lock);

		/* This traverses the subreslist in reverse order so we purge
		 * the children before their parents. */

		for (tmp2 = rootrsb->res_subreslist.prev, safe2 = tmp2->prev;
		     tmp2 != &rootrsb->res_subreslist;
		     tmp2 = safe2, safe2 = safe2->prev) {
			rsb = list_entry(tmp2, struct dlm_rsb, res_subreslist);

			hold_rsb(rsb);
			purge_queue(ls, &rsb->res_grantqueue);
			purge_queue(ls, &rsb->res_convertqueue);
			purge_queue(ls, &rsb->res_waitqueue);
			release_rsb_locked(rsb);
		}
		count += purge_queue(ls, &rootrsb->res_grantqueue);
		count += purge_queue(ls, &rootrsb->res_convertqueue);
		count += purge_queue(ls, &rootrsb->res_waitqueue);

		up_write(&rootrsb->res_lock);
		release_rsb_locked(rootrsb);

		schedule();
	}

	up_write(&ls->ls_root_lock);
	log_debug(ls, "purged %d locks", count);

	return 0;
}

/*
 * Grant any locks that have become grantable after a purge
 */

int restbl_grant_after_purge(struct dlm_ls *ls)
{
	struct dlm_rsb *root, *rsb, *safe;
	int error = 0;

	down_read(&ls->ls_root_lock);

	list_for_each_entry_safe(root, safe, &ls->ls_rootres, res_rootlist) {
		/* only the rsb master grants locks */
		if (root->res_nodeid)
			continue;

		if (!test_bit(LSFL_LS_RUN, &ls->ls_flags)) {
			log_debug(ls, "restbl_grant_after_purge aborted");
			error = -EINTR;
			up_read(&ls->ls_root_lock);
			goto out;
		}

		down_write(&root->res_lock);
		grant_pending_locks(root);
		up_write(&root->res_lock);

		list_for_each_entry(rsb, &root->res_subreslist, res_subreslist){
			down_write(&rsb->res_lock);
			grant_pending_locks(rsb);
			up_write(&rsb->res_lock);
		}
	}
	up_read(&ls->ls_root_lock);
	wake_astd();
 out:
	return error;
}

/*
 * Set the lock master for all LKBs in a lock queue
 */

static void set_lock_master(struct list_head *queue, int nodeid)
{
	struct dlm_lkb *lkb;

	list_for_each_entry(lkb, queue, lkb_statequeue) {
		/* Don't muck around with pre-exising sublocks */
		if (!(lkb->lkb_flags & GDLM_LKFLG_MSTCPY))
			lkb->lkb_nodeid = nodeid;
	}
}

static void set_master_lkbs(struct dlm_rsb *rsb)
{
	set_lock_master(&rsb->res_grantqueue, rsb->res_nodeid);
	set_lock_master(&rsb->res_convertqueue, rsb->res_nodeid);
	set_lock_master(&rsb->res_waitqueue, rsb->res_nodeid);
}

/*
 * Propogate the new master nodeid to locks, subrsbs, sublocks.
 * The NEW_MASTER flag tells rebuild_rsbs_send() which rsb's to consider.
 * The NEW_MASTER2 flag tells rsb_lvb_recovery() which rsb's to consider.
 */

static void set_new_master(struct dlm_rsb *rsb, uint32_t nodeid)
{
	struct dlm_rsb *subrsb;

	down_write(&rsb->res_lock);

	if (nodeid == our_nodeid()) {
		set_bit(RESFL_MASTER, &rsb->res_flags);
		rsb->res_nodeid = 0;
	} else
		rsb->res_nodeid = nodeid;

	set_master_lkbs(rsb);

	list_for_each_entry(subrsb, &rsb->res_subreslist, res_subreslist) {
		subrsb->res_nodeid = rsb->res_nodeid;
		set_master_lkbs(subrsb);
	}

	up_write(&rsb->res_lock);

	set_bit(RESFL_NEW_MASTER, &rsb->res_flags);
	set_bit(RESFL_NEW_MASTER2, &rsb->res_flags);
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

static int rsb_master_lookup(struct dlm_rsb *rsb, struct dlm_rcom *rc)
{
	struct dlm_ls *ls = rsb->res_ls;
	uint32_t dir_nodeid, r_nodeid;
	int error;

	dir_nodeid = get_directory_nodeid(rsb);

	if (dir_nodeid == our_nodeid()) {
		error = dlm_dir_lookup(ls, dir_nodeid, rsb->res_name,
				       rsb->res_length, &r_nodeid);
		if (error == -EEXIST) {
			log_error(ls, "rsb_master_lookup %u EEXIST %s",
				  r_nodeid, rsb->res_name);
		} else if (error)
			goto fail;

		set_new_master(rsb, r_nodeid);
	} else {
		/* NEW_MASTER2 may have been set by set_new_master() in the
		   previous recovery cycle. */

		clear_bit(RESFL_NEW_MASTER2, &rsb->res_flags);

		/* As we are the only thread doing recovery this
		   should be safe. if not then we need to use a different
		   ID somehow. We must set it in the RSB before rcom_send_msg
		   completes cos we may get a reply quite quickly. */

		rsb->res_recover_msgid = ls->ls_rcom_msgid + 1;

		recover_list_add(rsb);

		memcpy(rc->rc_buf, rsb->res_name, rsb->res_length);
		rc->rc_datalen = rsb->res_length;

		error = rcom_send_message(ls, dir_nodeid, RECCOMM_GETMASTER,
				          rc, 0);
		if (error)
			goto fail;
	}

 fail:
	return error;
}

static int needs_update(struct dlm_ls *ls, struct dlm_rsb *r)
{
	if (!r->res_nodeid)
		return FALSE;

	if (r->res_nodeid == -1)
		return FALSE;

	if (dlm_is_removed(ls, r->res_nodeid))
		return TRUE;

	return FALSE;
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

int restbl_rsb_update(struct dlm_ls *ls)
{
	struct dlm_rsb *rsb, *safe;
	struct dlm_rcom *rc;
	int error = -ENOMEM;
	int count = 0;

	log_debug(ls, "update remastered resources");

	rc = allocate_rcom_buffer(ls);
	if (!rc)
		goto out;

	down_read(&ls->ls_root_lock);

	list_for_each_entry_safe(rsb, safe, &ls->ls_rootres, res_rootlist) {
		error = dlm_recovery_stopped(ls);
		if (error) {
			up_read(&ls->ls_root_lock);
			goto out_free;
		}

		if (test_bit(RESFL_VALNOTVALID, &rsb->res_flags))
			set_bit(RESFL_VALNOTVALID_PREV, &rsb->res_flags);
		else
			clear_bit(RESFL_VALNOTVALID_PREV, &rsb->res_flags);

		if (needs_update(ls, rsb)) {
			error = rsb_master_lookup(rsb, rc);
			if (error) {
				up_read(&ls->ls_root_lock);
				goto out_free;
			}
			count++;
		}
		schedule();
	}
	up_read(&ls->ls_root_lock);

	error = dlm_wait_function(ls, &recover_list_empty);

	log_debug(ls, "updated %d resources", count);
 out_free:
	if (error)
		recover_list_clear(ls);
	free_rcom_buffer(rc);
 out:
	return error;
}

int restbl_rsb_update_recv(struct dlm_ls *ls, uint32_t nodeid, char *buf,
			   int length, int msgid)
{
	struct dlm_rsb *rsb;
	uint32_t be_nodeid;

	rsb = recover_list_find(ls, msgid);
	if (!rsb) {
		log_error(ls, "restbl_rsb_update_recv rsb not found %d", msgid);
		goto out;
	}

	memcpy(&be_nodeid, buf, sizeof(uint32_t));
	set_new_master(rsb, be32_to_cpu(be_nodeid));
	recover_list_del(rsb);

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

