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
#include "reccomms.h"
#include "dir.h"
#include "locking.h"
#include "rsb.h"
#include "lockspace.h"
#include "lkb.h"
#include "nodes.h"
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

		wchan_cond_sleep_intr(ls->ls_wait_general,
				      !testfn(ls) &&
				      !test_bit(LSFL_LS_STOP, &ls->ls_flags));

		if (timer_pending(&timer))
			del_timer(&timer);

		if (testfn(ls))
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
	struct dlm_csb *csb;
	int status;
	int error = 0;

	memset(&rc_stack, 0, sizeof(struct dlm_rcom));
	rc = &rc_stack;
	rc->rc_datalen = 0;

	list_for_each_entry(csb, &ls->ls_nodes, list) {
		for (;;) {
			error = dlm_recovery_stopped(ls);
			if (error)
				goto out;

			error = rcom_send_message(ls, csb->node->nodeid,
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

		if (in_nodes_gone(ls, lkb->lkb_nodeid)) {
			list_del(&lkb->lkb_statequeue);

			rsb = lkb->lkb_resource;
			lkb->lkb_status = 0;

			if (lkb->lkb_status == GDLM_LKSTS_CONVERT
			    && &lkb->lkb_duetime)
				remove_from_deadlockqueue(lkb);

			release_lkb(ls, lkb);
			release_rsb(rsb);
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

	log_all(ls, "purge locks of departed nodes");

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
			release_rsb(rsb);
		}
		count += purge_queue(ls, &rootrsb->res_grantqueue);
		count += purge_queue(ls, &rootrsb->res_convertqueue);
		count += purge_queue(ls, &rootrsb->res_waitqueue);

		up_write(&rootrsb->res_lock);
		release_rsb(rootrsb);
	}

	log_all(ls, "purged %d locks", count);

	return 0;
}

/*
 * Grant any locks that have become grantable after a purge
 */

int restbl_grant_after_purge(struct dlm_ls *ls)
{
	struct dlm_rsb *root, *rsb, *safe;
	int error = 0;

	down_write(&ls->ls_gap_rsblist);

	list_for_each_entry_safe(root, safe, &ls->ls_rootres, res_rootlist) {
		/* only the rsb master grants locks */
		if (root->res_nodeid)
			continue;

		if (!test_bit(LSFL_LS_RUN, &ls->ls_flags)) {
			log_debug(ls, "restbl_grant_after_purge aborted");
			error = -EINTR;
			up_write(&ls->ls_gap_rsblist);
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
	up_write(&ls->ls_gap_rsblist);
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
 * This rsb struct is now the master so it is responsible for keeping the
 * latest rsb.  Find if any current lkb's have an up to date copy of the lvb to
 * be used as the rsb copy.  An equivalent step occurs as new lkb's arrive for
 * this rsb in deserialise_lkb.
 */

static void set_rsb_lvb(struct dlm_rsb *rsb)
{
	struct dlm_lkb *lkb;

	list_for_each_entry(lkb, &rsb->res_grantqueue, lkb_statequeue) {

		if (!(lkb->lkb_flags & GDLM_LKFLG_DELETED) &&
		    (lkb->lkb_flags & GDLM_LKFLG_VALBLK) &&
		    (lkb->lkb_grmode > DLM_LOCK_NL))
		{
			if (!rsb->res_lvbptr)
				rsb->res_lvbptr = allocate_lvb(rsb->res_ls);

			memcpy(rsb->res_lvbptr, lkb->lkb_lvbptr, DLM_LVB_LEN);
			return;
		}
	}

	list_for_each_entry(lkb, &rsb->res_convertqueue, lkb_statequeue) {

		if (!(lkb->lkb_flags & GDLM_LKFLG_DELETED) &&
		    (lkb->lkb_flags & GDLM_LKFLG_VALBLK) &&
		    (lkb->lkb_grmode > DLM_LOCK_NL))
		{
			if (!rsb->res_lvbptr)
				rsb->res_lvbptr = allocate_lvb(rsb->res_ls);

			memcpy(rsb->res_lvbptr, lkb->lkb_lvbptr, DLM_LVB_LEN);
			return;
		}
	}
}

/*
 * Propogate the new master nodeid to locks, subrsbs, sublocks.
 * The NEW_MASTER flag tells rebuild_rsbs_send() which rsb's to consider.
 */

static void set_new_master(struct dlm_rsb *rsb)
{
	struct dlm_rsb *subrsb;

	down_write(&rsb->res_lock);

	if (rsb->res_nodeid == our_nodeid()) {
		rsb->res_nodeid = 0;
		set_rsb_lvb(rsb);
	}

	set_master_lkbs(rsb);

	list_for_each_entry(subrsb, &rsb->res_subreslist, res_subreslist) {
		subrsb->res_nodeid = rsb->res_nodeid;
		set_master_lkbs(subrsb);
	}

	up_write(&rsb->res_lock);

	set_bit(RESFL_NEW_MASTER, &rsb->res_flags);
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

#if 0
static void recover_list_clear(struct dlm_ls *ls)
{
	struct dlm_rsb *rsb;


	spin_lock(&ls->ls_recover_list_lock);

	while (!list_empty(&ls->ls_recover_list)) {
		rsb = list_entry(ls->ls_recover_list.next, struct dlm_rsb,
			         res_recover_list);
		list_del(&rsb->res_recover_list);
		ls->ls_recover_list_count--;
	}
	spin_unlock(&ls->ls_recover_list_lock);

}
#endif

static int rsb_master_lookup(struct dlm_rsb *rsb, struct dlm_rcom *rc)
{
	struct dlm_ls *ls = rsb->res_ls;
	uint32_t dir_nodeid, r_nodeid;
	int error;

	dir_nodeid = get_directory_nodeid(rsb);

	if (dir_nodeid == our_nodeid()) {
		error = dlm_dir_lookup_recovery(ls, dir_nodeid, rsb->res_name,
				    	        rsb->res_length, &r_nodeid);
		if (error)
			goto fail;

		rsb->res_nodeid = r_nodeid;
		set_new_master(rsb);
	} else {
		/* As we are the only thread doing recovery this 
		   should be safe. if not then we need to use a different
		   ID somehow. We must set it in the RSB before rcom_send_msg
		   completes cos we may get a reply quite quickly.
		*/
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

	log_all(ls, "update remastered resources");

	rc = allocate_rcom_buffer(ls);
	if (!rc)
		goto out;

	list_for_each_entry_safe(rsb, safe, &ls->ls_rootres, res_rootlist) {
		if (!rsb->res_nodeid)
			continue;

		error = dlm_recovery_stopped(ls);
		if (error)
			goto out_free;

		if (in_nodes_gone(ls, rsb->res_nodeid)) {
			error = rsb_master_lookup(rsb, rc);
			if (error)
				goto out_free;
			count++;
		}
	}

	error = dlm_wait_function(ls, &recover_list_empty);

	log_all(ls, "updated %d resources", count);

      out_free:
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
	rsb->res_nodeid = be32_to_cpu(be_nodeid);
	set_new_master(rsb);
	recover_list_del(rsb);

	if (recover_list_empty(ls))
		wake_up(&ls->ls_wait_general);

      out:
	return 0;
}

/*
 * This function not used any longer.
 */

int bulk_master_lookup(struct dlm_ls *ls, int nodeid, char *inbuf, int inlen,
		       char *outbuf)
{
	char *inbufptr, *outbufptr;

	/*
	 * The other node wants nodeids matching the resource names in inbuf.
	 * The resource names are packed into inbuf as
	 * [len1][name1][len2][name2]...  where lenX is 1 byte and nameX is
	 * lenX bytes.  Matching nodeids are packed into outbuf in order
	 * [nodeid1][nodeid2]...
	 */

	inbufptr = inbuf;
	outbufptr = outbuf;

	while (inbufptr < inbuf + inlen) {
		uint32_t r_nodeid, be_nodeid;
		int status;

		status = dlm_dir_lookup_recovery(ls, nodeid, inbufptr + 1,
						 *inbufptr, &r_nodeid);
		if (status != 0)
			goto fail;

		inbufptr += *inbufptr + 1;

		be_nodeid = cpu_to_be32(r_nodeid);
		memcpy(outbufptr, &be_nodeid, sizeof(uint32_t));
		outbufptr += sizeof(uint32_t);

		/* add assertion that outbufptr - outbuf is not > than ... */
	}

	return (outbufptr - outbuf);

      fail:
	return -1;
}
