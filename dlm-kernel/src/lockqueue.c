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
 * lockqueue.c
 *
 * This controls the lock queue, which is where locks
 * come when they need to wait for a remote operation
 * to complete.
 *
 * This could also be thought of as the "high-level" comms
 * layer.
 *
 */

#include "dlm_internal.h"
#include "lockqueue.h"
#include "dir.h"
#include "locking.h"
#include "lkb.h"
#include "lowcomms.h"
#include "midcomms.h"
#include "reccomms.h"
#include "nodes.h"
#include "lockspace.h"
#include "ast.h"
#include "memory.h"
#include "rsb.h"
#include "queries.h"
#include "util.h"

static void add_reply_lvb(struct dlm_lkb * lkb, struct dlm_reply *reply);
static void add_request_lvb(struct dlm_lkb * lkb, struct dlm_request *req);

/*
 * format of an entry on the request queue
 */
struct rq_entry {
	struct list_head rqe_list;
	uint32_t rqe_nodeid;
	char rqe_request[1];
};

/*
 * Add a new request (if appropriate) to the request queue and send the remote
 * request out.  - runs in the context of the locking caller
 *
 * Recovery of a remote_stage request if the remote end fails while the lkb
 * is still on the lockqueue:
 *
 * o lkbs on the lockqueue are flagged with GDLM_LKFLG_LQRESEND in
 *   lockqueue_lkb_mark() at the start of recovery.
 *
 * o Some lkb's will be rebuilt on new master rsb's during recovery.
 *   (depends on the type of request, see below).
 *
 * o At the end of recovery, resend_cluster_requests() looks at these
 *   LQRESEND lkb's and either:
 *
 *   i) resends the request to the new master for the rsb where the
 *      request is processed as usual.  The lkb remains on the lockqueue until
 *      the new master replies and we run process_lockqueue_reply().
 *
 *   ii) if we've become the rsb master, remove the lkb from the lockqueue
 *       and processes the request locally via process_remastered_lkb().
 *
 * GDLM_LQSTATE_WAIT_RSB (1) - these lockqueue lkb's are not on any rsb queue
 * and the request should be resent if dest node is failed.
 *
 * GDLM_LQSTATE_WAIT_CONDGRANT (3) - this lockqueue lkb is on a local rsb's
 * wait queue.  Don't rebuild this lkb on a new master rsb (the NOREBUILD flag
 * makes send_lkb_queue() skip it).  Resend this request to the new master.
 *
 * GDLM_LQSTATE_WAIT_UNLOCK (4) - this lkb is on a local rsb's queue.  It will
 * be rebuilt on the rsb on the new master (restbl_lkb_send/send_lkb_queue).
 * Resend this request to the new master.
 *
 * GDLM_LQSTATE_WAIT_CONVERT (2) - this lkb is on a local rsb convert queue.
 * It will be rebuilt on the new master rsb's granted queue.  Resend this
 * request to the new master.
 */

int remote_stage(struct dlm_lkb *lkb, int state)
{
	int error;

	lkb->lkb_lockqueue_state = state;
	add_to_lockqueue(lkb);

	error = send_cluster_request(lkb, state);
	if (error < 0) {
		log_print("remote_stage error sending request %d", error);

		/* Leave on lockqueue, it will be resent to correct node during
		 * recovery. */

		 /*
		 lkb->lkb_lockqueue_state = 0;
		 remove_from_lockqueue(lkb);
		 return -ENOTCONN;
		 */
	}
	return 0;
}

/*
 * Requests received while the lockspace is in recovery get added to the
 * request queue and processed when recovery is complete.
 */

void add_to_requestqueue(struct dlm_ls *ls, int nodeid, struct dlm_header *hd)
{
	struct rq_entry *entry;
	int length = hd->rh_length;

	if (test_bit(LSFL_REQUEST_WARN, &ls->ls_flags))
		log_error(ls, "request during recovery from %u", nodeid);

	if (in_nodes_gone(ls, nodeid))
		return;

	entry = kmalloc(sizeof(struct rq_entry) + length, GFP_KERNEL);
	if (!entry) {
		// TODO something better
		printk("dlm: add_to_requestqueue: out of memory\n");
		return;
	}

	log_debug(ls, "add_to_requestqueue cmd %d from %d", hd->rh_cmd, nodeid);
	entry->rqe_nodeid = nodeid;
	memcpy(entry->rqe_request, hd, length);
	list_add_tail(&entry->rqe_list, &ls->ls_requestqueue);
}

int process_requestqueue(struct dlm_ls *ls)
{
	int error = 0, count = 0;
	struct rq_entry *entry, *safe;
	struct dlm_header *req;

	log_all(ls, "process held requests");

	list_for_each_entry_safe(entry, safe, &ls->ls_requestqueue, rqe_list) {
		req = (struct dlm_header *) entry->rqe_request;
		log_debug(ls, "process_requestqueue %u", entry->rqe_nodeid);

		if (!test_bit(LSFL_LS_RUN, &ls->ls_flags)) {
			log_debug(ls, "process_requestqueue aborted");
			error = -EINTR;
			break;
		}

		error = process_cluster_request(entry->rqe_nodeid, req, TRUE);
		if (error == -EINTR) {
			log_debug(ls, "process_requestqueue interrupted");
			break;
		}

		list_del(&entry->rqe_list);
		kfree(entry);
		count++;
		error = 0;
	}

	log_all(ls, "processed %d requests", count);
	return error;
}

void wait_requestqueue(struct dlm_ls *ls)
{
	while (!list_empty(&ls->ls_requestqueue) &&
		test_bit(LSFL_LS_RUN, &ls->ls_flags))
		schedule();
}

/*
 * Resdir requests (lookup or remove) and replies from before recovery are
 * invalid since the resdir was rebuilt.  Clear them.  Requests from nodes now
 * gone are also invalid.
 */

void purge_requestqueue(struct dlm_ls *ls)
{
	int count = 0;
	struct rq_entry *entry, *safe;
	struct dlm_header *req;
	struct dlm_request *freq;
	struct dlm_lkb *lkb;

	log_all(ls, "purge requests");

	list_for_each_entry_safe(entry, safe, &ls->ls_requestqueue, rqe_list) {
		req = (struct dlm_header *) entry->rqe_request;
		freq = (struct dlm_request *) req;

		if (req->rh_cmd == GDLM_REMCMD_REM_RESDATA ||
		    req->rh_cmd == GDLM_REMCMD_LOOKUP ||
		    in_nodes_gone(ls, entry->rqe_nodeid)) {

			list_del(&entry->rqe_list);
			kfree(entry);
			count++;

		} else if (req->rh_cmd == GDLM_REMCMD_LOCKREPLY) {

			/*
			 * Replies to resdir lookups are invalid and must be
			 * purged.  The lookup requests are marked in
			 * lockqueue_lkb_mark and will be resent in
			 * resend_cluster_requests.  The only way to check if
			 * this is a lookup reply is to look at the
			 * lockqueue_state of the lkb.
			 */

			lkb = find_lock_by_id(ls, freq->rr_header.rh_lkid);
			DLM_ASSERT(lkb,);
			if (lkb->lkb_lockqueue_state == GDLM_LQSTATE_WAIT_RSB) {
				list_del(&entry->rqe_list);
				kfree(entry);
				count++;
			}
		}
	}

	log_all(ls, "purged %d requests", count);
}

/*
 * Check if there's a reply for the given lkid in the requestqueue.
 */

int reply_in_requestqueue(struct dlm_ls *ls, int lkid)
{
	int rv = FALSE;
	struct rq_entry *entry, *safe;
	struct dlm_header *req;
	struct dlm_request *freq;

	list_for_each_entry_safe(entry, safe, &ls->ls_requestqueue, rqe_list) {
		req = (struct dlm_header *) entry->rqe_request;
		freq = (struct dlm_request *) req;

		if (req->rh_cmd == GDLM_REMCMD_LOCKREPLY &&
		    freq->rr_header.rh_lkid == lkid) {
			rv = TRUE;
			break;
		}
	}

	return rv;
}

void allocate_and_copy_lvb(struct dlm_ls *ls, char **lvbptr, char *src)
{
	if (!*lvbptr)
		*lvbptr = allocate_lvb(ls);
	if (*lvbptr)
		memcpy(*lvbptr, src, DLM_LVB_LEN);
}

/*
 * Process a lockqueue LKB after it has had it's remote processing complete and
 * been pulled from the lockqueue.  Runs in the context of the DLM recvd thread
 * on the machine that requested the lock.
 */

static void process_lockqueue_reply(struct dlm_lkb *lkb,
				    struct dlm_reply *reply,
				    uint32_t nodeid)
{
	struct dlm_rsb *rsb = lkb->lkb_resource;
	struct dlm_ls *ls = rsb->res_ls;
	int oldstate, state = lkb->lkb_lockqueue_state;

	lkb->lkb_lockqueue_state = 0;
	if (state)
		remove_from_lockqueue(lkb);

	switch (state) {
	case GDLM_LQSTATE_WAIT_RSB:

		DLM_ASSERT(reply->rl_status == 0,
			   print_lkb(lkb);
			   print_rsb(rsb);
			   print_reply(reply););

		DLM_ASSERT(rsb->res_nodeid == -1 ||
			   rsb->res_nodeid == 0,
			   print_lkb(lkb);
			   print_rsb(rsb);
			   print_reply(reply););

		if (reply->rl_nodeid == our_nodeid()) {
			if (rsb->res_nodeid == -1) {
				set_bit(RESFL_MASTER, &rsb->res_flags);
				rsb->res_nodeid = 0;
			} else {
				log_all(ls, "ignore master reply %x %u",
					lkb->lkb_id, nodeid);
			}
		} else {
			DLM_ASSERT(rsb->res_nodeid == -1,
				   print_lkb(lkb);
				   print_rsb(rsb);
				   print_reply(reply););

			clear_bit(RESFL_MASTER, &rsb->res_flags);
			rsb->res_nodeid = reply->rl_nodeid;
		}

		log_debug(ls, "lu rep %x fr %u %u", lkb->lkb_id, nodeid,
			  rsb->res_nodeid);

		lkb->lkb_nodeid = rsb->res_nodeid;
		dlm_lock_stage2(ls, lkb, rsb, lkb->lkb_lockqueue_flags);
		break;

	case GDLM_LQSTATE_WAIT_CONVERT:
	case GDLM_LQSTATE_WAIT_CONDGRANT:

		/*
		 * After a remote lock/conversion/grant request we put the lock
		 * on the right queue and send an AST if appropriate.  Any lock
		 * shuffling (eg newly granted locks because this one was
		 * converted downwards) will be dealt with in seperate messages
		 * (which may be in the same network message)
		 */


		/* the destination wasn't the master */
		if (reply->rl_status == -EINVAL) {
			int master_nodeid;

			log_debug(ls, "rq rep %x fr %u einval",
				  lkb->lkb_id, nodeid);

			schedule();
			lkb_dequeue(lkb);
			rsb->res_nodeid = -1;
			lkb->lkb_nodeid = -1;
			if (get_directory_nodeid(rsb) != our_nodeid())
				remote_stage(lkb, GDLM_LQSTATE_WAIT_RSB);
			else {
			    	dlm_dir_lookup(ls, our_nodeid(), rsb->res_name,
					       rsb->res_length, &master_nodeid);
					       
			    	if (master_nodeid == our_nodeid()) {
					set_bit(RESFL_MASTER, &rsb->res_flags);
					master_nodeid = 0;
			        } 
				else
					clear_bit(RESFL_MASTER,&rsb->res_flags);
			        rsb->res_nodeid = master_nodeid;
			        lkb->lkb_nodeid = master_nodeid;
			        dlm_lock_stage2(ls, lkb, rsb,
			 			lkb->lkb_lockqueue_flags);
			}
			break;
		}

		if (!lkb->lkb_remid)
			lkb->lkb_remid = reply->rl_lkid;

		/*
		 * The remote request failed (we assume because of NOQUEUE).
		 * If this is a new request (non-conv) the lkb was created just
		 * for it so the lkb should be freed.  If this was a
		 * conversion, the lkb already existed so we should put it back
		 * on the grant queue.
		 */

		if (reply->rl_status != 0) {
			DLM_ASSERT(reply->rl_status == -EAGAIN,);

			if (state == GDLM_LQSTATE_WAIT_CONDGRANT) {
				res_lkb_dequeue(lkb);
				lkb->lkb_retstatus = reply->rl_status;
				queue_ast(lkb, AST_COMP | AST_DEL, 0);
			} else {
				res_lkb_swqueue(rsb, lkb, GDLM_LKSTS_GRANTED);
				lkb->lkb_retstatus = reply->rl_status;
				queue_ast(lkb, AST_COMP, 0);
			}
			break;
		}

		/*
		 * The remote request was successful in granting the request or
		 * queuing it to be granted later.  Add the lkb to the
		 * appropriate rsb queue.
		 */

		switch (reply->rl_lockstate) {
		case GDLM_LKSTS_GRANTED:

			/* Compact version of grant_lock(). */

			down_write(&rsb->res_lock);
			if (lkb->lkb_flags & GDLM_LKFLG_VALBLK)
				memcpy(lkb->lkb_lvbptr, reply->rl_lvb,
				       DLM_LVB_LEN);

			lkb->lkb_grmode = lkb->lkb_rqmode;
			lkb->lkb_rqmode = DLM_LOCK_IV;
			lkb_swqueue(rsb, lkb, GDLM_LKSTS_GRANTED);

			if (lkb->lkb_range) {
				lkb->lkb_range[GR_RANGE_START] =
				    lkb->lkb_range[RQ_RANGE_START];
				lkb->lkb_range[GR_RANGE_END] =
				    lkb->lkb_range[RQ_RANGE_END];
			}
			up_write(&rsb->res_lock);

			lkb->lkb_retstatus = 0;
			queue_ast(lkb, AST_COMP, 0);
			break;

		case GDLM_LKSTS_WAITING:

			if (lkb->lkb_status != GDLM_LKSTS_GRANTED)
				res_lkb_swqueue(rsb, lkb, GDLM_LKSTS_WAITING);
			else
				log_error(ls, "wait reply for granted %x %u",
					  lkb->lkb_id, lkb->lkb_nodeid);
			break;

		case GDLM_LKSTS_CONVERT:

			if (lkb->lkb_status != GDLM_LKSTS_GRANTED)
				res_lkb_swqueue(rsb, lkb, GDLM_LKSTS_CONVERT);
			else
				log_error(ls, "convert reply for granted %x %u",
					  lkb->lkb_id, lkb->lkb_nodeid);
			break;

		default:
			log_error(ls, "process_lockqueue_reply state %d",
				  reply->rl_lockstate);
		}

		break;

	case GDLM_LQSTATE_WAIT_UNLOCK:

		/*
		 * Unlocks should never fail.  Update local lock info.  This
		 * always sends completion AST with status in lksb
		 */

		DLM_ASSERT(reply->rl_status == 0,);
		oldstate = res_lkb_dequeue(lkb);

		/* Differentiate between unlocks and conversion cancellations */
		if (lkb->lkb_lockqueue_flags & DLM_LKF_CANCEL &&
		    oldstate == GDLM_LKSTS_CONVERT) {
			res_lkb_enqueue(lkb->lkb_resource, lkb,
					GDLM_LKSTS_GRANTED);
			lkb->lkb_retstatus = -DLM_ECANCEL;
			queue_ast(lkb, AST_COMP, 0);
		} else {
			lkb->lkb_retstatus = -DLM_EUNLOCK;
			queue_ast(lkb, AST_COMP | AST_DEL, 0);
		}
		break;

	default:
		log_error(ls, "process_lockqueue_reply id %x state %d",
		          lkb->lkb_id, state);
	}
}

/*
 * Tell a remote node to grant a lock.  This happens when we are the master
 * copy for a lock that is actually held on a remote node.  The remote end is
 * also responsible for sending the completion AST.
 */

void remote_grant(struct dlm_lkb *lkb)
{
	struct writequeue_entry *e;
	struct dlm_request *req;

	// TODO Error handling
	e = lowcomms_get_buffer(lkb->lkb_nodeid,
				sizeof(struct dlm_request),
				lkb->lkb_resource->res_ls->ls_allocation,
				(char **) &req);
	if (!e)
		return;

	req->rr_header.rh_cmd = GDLM_REMCMD_LOCKGRANT;
	req->rr_header.rh_length = sizeof(struct dlm_request);
	req->rr_header.rh_flags = 0;
	req->rr_header.rh_lkid = lkb->lkb_id;
	req->rr_header.rh_lockspace = lkb->lkb_resource->res_ls->ls_global_id;
	req->rr_remlkid = lkb->lkb_remid;
	req->rr_flags = 0;

	if (lkb->lkb_flags & GDLM_LKFLG_DEMOTED) {
		/* This is a confusing non-standard use of rr_flags which is
		 * usually used to pass lockqueue_flags. */
		req->rr_flags |= GDLM_LKFLG_DEMOTED;
	}

	add_request_lvb(lkb, req);
	midcomms_send_buffer(&req->rr_header, e);
}

void reply_and_grant(struct dlm_lkb *lkb)
{
	struct dlm_request *req = lkb->lkb_request;
	struct dlm_reply *reply;
	struct writequeue_entry *e;

	// TODO Error handling
	e = lowcomms_get_buffer(lkb->lkb_nodeid,
				sizeof(struct dlm_reply),
				lkb->lkb_resource->res_ls->ls_allocation,
				(char **) &reply);
	if (!e)
		return;

	reply->rl_header.rh_cmd = GDLM_REMCMD_LOCKREPLY;
	reply->rl_header.rh_flags = 0;
	reply->rl_header.rh_length = sizeof(struct dlm_reply);
	reply->rl_header.rh_lkid = req->rr_header.rh_lkid;
	reply->rl_header.rh_lockspace = req->rr_header.rh_lockspace;

	reply->rl_status = lkb->lkb_retstatus;
	reply->rl_lockstate = lkb->lkb_status;
	reply->rl_lkid = lkb->lkb_id;

	DLM_ASSERT(!(lkb->lkb_flags & GDLM_LKFLG_DEMOTED),);

	lkb->lkb_request = NULL;

	add_reply_lvb(lkb, reply);
	midcomms_send_buffer(&reply->rl_header, e);
}

/*
 * Request removal of a dead entry in the resource directory
 */

void remote_remove_direntry(struct dlm_ls *ls, int nodeid, char *name,
			    int namelen)
{
	struct writequeue_entry *e;
	struct dlm_request *req;

	if (!test_bit(LSFL_LS_RUN, &ls->ls_flags)) {
		struct dlm_rcom *rc = allocate_rcom_buffer(ls);

		memcpy(rc->rc_buf, name, namelen);
		rc->rc_datalen = namelen;

		rcom_send_message(ls, nodeid, RECCOMM_REMRESDATA, rc, 0);

		free_rcom_buffer(rc);
		return;
	}
	// TODO Error handling
	e = lowcomms_get_buffer(nodeid,
				sizeof(struct dlm_request) + namelen - 1,
				ls->ls_allocation, (char **) &req);
	if (!e)
		return;

	memset(req, 0, sizeof(struct dlm_request) + namelen - 1);
	req->rr_header.rh_cmd = GDLM_REMCMD_REM_RESDATA;
	req->rr_header.rh_length =
	    sizeof(struct dlm_request) + namelen - 1;
	req->rr_header.rh_flags = 0;
	req->rr_header.rh_lkid = 0;
	req->rr_header.rh_lockspace = ls->ls_global_id;
	req->rr_remlkid = 0;
	memcpy(req->rr_name, name, namelen);

	midcomms_send_buffer(&req->rr_header, e);
}

/*
 * Send remote cluster request to directory or master node before the request
 * is put on the lock queue.  Runs in the context of the locking caller.
 */

int send_cluster_request(struct dlm_lkb *lkb, int state)
{
	uint32_t target_nodeid;
	struct dlm_rsb *rsb = lkb->lkb_resource;
	struct dlm_ls *ls = rsb->res_ls;
	struct dlm_request *req;
	struct writequeue_entry *e;

	if (state == GDLM_LQSTATE_WAIT_RSB)
		target_nodeid = get_directory_nodeid(rsb);
	else
		target_nodeid = lkb->lkb_nodeid;

	/* during recovery it's valid for target_nodeid to equal our own;
	   resend_cluster_requests does this to get requests back on track */

	DLM_ASSERT(target_nodeid && target_nodeid != -1,
		   print_lkb(lkb);
		   print_rsb(rsb);
		   printk("target_nodeid %u\n", target_nodeid););

	if (!test_bit(LSFL_LS_RUN, &ls->ls_flags)) {
		/* this may happen when called by resend_cluster_request */
		log_error(ls, "send_cluster_request to %u state %d recovery",
			  target_nodeid, state);
	}

	e = lowcomms_get_buffer(target_nodeid,
				sizeof(struct dlm_request) +
				rsb->res_length - 1, ls->ls_allocation,
				(char **) &req);
	if (!e)
		return -ENOBUFS;
	memset(req, 0, sizeof(struct dlm_request) + rsb->res_length - 1);

	/* Common stuff, some are just defaults */

	if (lkb->lkb_bastaddr)
		req->rr_asts = AST_BAST;
	if (lkb->lkb_astaddr)
		req->rr_asts |= AST_COMP;
	if (lkb->lkb_parent)
		req->rr_remparid = lkb->lkb_parent->lkb_remid;

	req->rr_flags = lkb->lkb_lockqueue_flags;
	req->rr_rqmode = lkb->lkb_rqmode;
	req->rr_remlkid = lkb->lkb_remid;
	req->rr_header.rh_length =
	    sizeof(struct dlm_request) + rsb->res_length - 1;
	req->rr_header.rh_flags = 0;
	req->rr_header.rh_lkid = lkb->lkb_id;
	req->rr_header.rh_lockspace = ls->ls_global_id;

	switch (state) {

	case GDLM_LQSTATE_WAIT_RSB:

		DLM_ASSERT(!lkb->lkb_parent,
			   print_lkb(lkb);
			   print_rsb(rsb););

		DLM_ASSERT(rsb->res_nodeid == -1,
			   print_lkb(lkb);
			   print_rsb(rsb););

		log_debug(ls, "send lu %x to %u", lkb->lkb_id, target_nodeid);

		req->rr_header.rh_cmd = GDLM_REMCMD_LOOKUP;
		memcpy(req->rr_name, rsb->res_name, rsb->res_length);
		break;

	case GDLM_LQSTATE_WAIT_CONVERT:

		DLM_ASSERT(lkb->lkb_nodeid == rsb->res_nodeid,
			   print_lkb(lkb);
			   print_rsb(rsb););

		log_debug(ls, "send cv %x to %u", lkb->lkb_id, target_nodeid);

		req->rr_header.rh_cmd = GDLM_REMCMD_CONVREQUEST;
		if (lkb->lkb_range) {
			req->rr_flags |= GDLM_LKFLG_RANGE;
			req->rr_range_start = lkb->lkb_range[RQ_RANGE_START];
			req->rr_range_end = lkb->lkb_range[RQ_RANGE_END];
		}
		break;

	case GDLM_LQSTATE_WAIT_CONDGRANT:

		DLM_ASSERT(lkb->lkb_nodeid == rsb->res_nodeid,
			   print_lkb(lkb);
			   print_rsb(rsb););

		log_debug(ls, "send rq %x to %u", lkb->lkb_id, target_nodeid);

		req->rr_header.rh_cmd = GDLM_REMCMD_LOCKREQUEST;
		memcpy(req->rr_name, rsb->res_name, rsb->res_length);
		if (lkb->lkb_range) {
			req->rr_flags |= GDLM_LKFLG_RANGE;
			req->rr_range_start = lkb->lkb_range[RQ_RANGE_START];
			req->rr_range_end = lkb->lkb_range[RQ_RANGE_END];
		}
		break;

	case GDLM_LQSTATE_WAIT_UNLOCK:

		log_debug(ls, "send un %x to %u", lkb->lkb_id, target_nodeid);

		if (rsb->res_nodeid != -1)
			log_all(ls, "un %x to %u rsb nodeid %u", lkb->lkb_id,
				target_nodeid, rsb->res_nodeid);

		req->rr_header.rh_cmd = GDLM_REMCMD_UNLOCKREQUEST;
		break;

	default:
		DLM_ASSERT(0, printk("Unknown cluster request\n"););
	}

	add_request_lvb(lkb, req);
	midcomms_send_buffer(&req->rr_header, e);

	return 0;
}

/*
 * We got a request from another cluster node, process it and return an info
 * structure with the lock state/LVB etc as required.  Executes in the DLM's
 * recvd thread.
 */

int process_cluster_request(int nodeid, struct dlm_header *req, int recovery)
{
	struct dlm_ls *lspace;
	struct dlm_lkb *lkb = NULL;
	struct dlm_rsb *rsb;
	int send_reply = 0, status = 0, namelen;
	struct dlm_request *freq = (struct dlm_request *) req;
	struct dlm_reply *rp = (struct dlm_reply *) req;
	struct dlm_reply reply;

	lspace = find_lockspace_by_global_id(req->rh_lockspace);

	if (!lspace) {
		log_print("process_cluster_request invalid lockspace %x "
			  "from %d req %u", req->rh_lockspace, nodeid,
			  req->rh_cmd);
		status = -EINVAL;
		goto out;
	}

	/* wait for recoverd to drain requestqueue */
	if (!recovery)
		wait_requestqueue(lspace);

	/*
	 * If we're in recovery then queue the request for later.  Otherwise,
	 * we still need to get the "in_recovery" lock to make sure the
	 * recovery itself doesn't start until we are done.
	 */
 retry:
	if (!test_bit(LSFL_LS_RUN, &lspace->ls_flags)) {
		add_to_requestqueue(lspace, nodeid, req);
		status = -EINTR;
		goto out;
	}
	if (!down_read_trylock(&lspace->ls_in_recovery)) {
		schedule();
		goto retry;
	}


	/*
	 * Process the request.
	 */

	switch (req->rh_cmd) {

	case GDLM_REMCMD_LOOKUP:
		{
			uint32_t dir_nodeid, r_nodeid;
			int status;

			namelen = freq->rr_header.rh_length - sizeof(*freq) + 1;

			dir_nodeid = name_to_directory_nodeid(lspace,
							      freq->rr_name,
							      namelen);
			if (dir_nodeid != our_nodeid())
				log_debug(lspace, "ignoring directory lookup");

			status = dlm_dir_lookup(lspace, nodeid, freq->rr_name,
					        namelen, &r_nodeid);
			if (status)
				status = -ENOMEM;

			reply.rl_status = status;
			reply.rl_lockstate = 0;
			reply.rl_nodeid = r_nodeid;
		}
		send_reply = 1;
		break;

	case GDLM_REMCMD_REM_RESDATA:

		namelen = freq->rr_header.rh_length - sizeof(*freq) + 1;
		dlm_dir_remove(lspace, nodeid, freq->rr_name, namelen);
		break;

	case GDLM_REMCMD_LOCKREQUEST:

		lkb = remote_stage2(nodeid, lspace, freq);
		if (lkb) {
			lkb->lkb_request = freq;
			if (lkb->lkb_retstatus != -EINVAL)
				dlm_lock_stage3(lkb);

			/*
			 * If the request was granted in lock_stage3, then a
			 * reply message was already sent in combination with
			 * the grant message and lkb_request is NULL.
			 */

			if (lkb->lkb_request) {
				lkb->lkb_request = NULL;
				send_reply = 1;
				reply.rl_status = lkb->lkb_retstatus;
				reply.rl_lockstate = lkb->lkb_status;
				reply.rl_lkid = lkb->lkb_id;

				/*
				 * If the request could not be granted and the
				 * user won't wait, then free up the LKB
				 */

				if (lkb->lkb_retstatus == -EAGAIN) {
					rsb = lkb->lkb_resource;
					release_lkb(lspace, lkb);
					release_rsb(rsb);
					lkb = NULL;
				}
				else if (lkb->lkb_retstatus == -EINVAL) {
					release_lkb(lspace, lkb);
					lkb = NULL;
				}
			}
		} else {
			reply.rl_status = -ENOMEM;
			send_reply = 1;
		}
		break;

	case GDLM_REMCMD_CONVREQUEST:

		lkb = find_lock_by_id(lspace, freq->rr_remlkid);

		DLM_ASSERT(lkb,
			   print_request(freq);
			   printk("nodeid %u\n", nodeid););

		rsb = lkb->lkb_resource;

		DLM_ASSERT(rsb,
			   print_lkb(lkb);
			   print_request(freq);
			   printk("nodeid %u\n", nodeid););

		DLM_ASSERT(!rsb->res_nodeid,
			   print_lkb(lkb);
			   print_rsb(rsb);
			   print_request(freq);
			   printk("nodeid %u\n", nodeid););

		DLM_ASSERT(lkb->lkb_flags & GDLM_LKFLG_MSTCPY,
			   print_lkb(lkb);
			   print_rsb(rsb);
			   print_request(freq);
			   printk("nodeid %u\n", nodeid););

		DLM_ASSERT(lkb->lkb_status == GDLM_LKSTS_GRANTED,
			   print_lkb(lkb);
			   print_rsb(rsb);
			   print_request(freq);
			   printk("nodeid %u\n", nodeid););

		lkb->lkb_rqmode = freq->rr_rqmode;
		lkb->lkb_lockqueue_flags = freq->rr_flags;
		lkb->lkb_request = freq;
		lkb->lkb_flags &= ~GDLM_LKFLG_DEMOTED;

		if (lkb->lkb_flags & GDLM_LKFLG_VALBLK ||
		    freq->rr_flags & DLM_LKF_VALBLK) {
			lkb->lkb_flags |= GDLM_LKFLG_VALBLK;
			allocate_and_copy_lvb(lspace, &lkb->lkb_lvbptr,
					      freq->rr_lvb);
		}

		if (freq->rr_flags & GDLM_LKFLG_RANGE) {
			if (lkb_set_range(lspace, lkb, freq->rr_range_start,
			                  freq->rr_range_end)) {
				reply.rl_status = -ENOMEM;
				send_reply = 1;
				goto out;
			}
		}

		log_debug(lspace, "cv %u from %u %x \"%s\"", lkb->lkb_rqmode,
			  nodeid, lkb->lkb_id, rsb->res_name);

		dlm_convert_stage2(lkb, FALSE);

		/*
		 * If the conv request was granted in stage2, then a reply
		 * message was already sent in combination with the grant
		 * message.
		 */

		if (lkb->lkb_request) {
			lkb->lkb_request = NULL;
			send_reply = 1;
			reply.rl_status = lkb->lkb_retstatus;
			reply.rl_lockstate = lkb->lkb_status;
			reply.rl_lkid = lkb->lkb_id;
		}
		break;

	case GDLM_REMCMD_LOCKREPLY:

		lkb = find_lock_by_id(lspace, req->rh_lkid);

		DLM_ASSERT(lkb,
			   print_reply(rp);
			   printk("nodeid %u\n", nodeid););

		DLM_ASSERT(!(lkb->lkb_flags & GDLM_LKFLG_MSTCPY),
			   print_lkb(lkb);
			   print_reply(rp);
			   printk("nodeid %u\n", nodeid););

		process_lockqueue_reply(lkb, rp, nodeid);
		break;

	case GDLM_REMCMD_LOCKGRANT:

		/*
		 * Remote lock has been granted asynchronously.  Do a compact
		 * version of what grant_lock() does.
		 */

		lkb = find_lock_by_id(lspace, freq->rr_remlkid);

		DLM_ASSERT(lkb,
			   print_request(freq);
			   printk("nodeid %u\n", nodeid););

		rsb = lkb->lkb_resource;

		DLM_ASSERT(rsb,
			   print_lkb(lkb);
			   print_request(freq);
			   printk("nodeid %u\n", nodeid););

		DLM_ASSERT(rsb->res_nodeid,
			   print_lkb(lkb);
			   print_rsb(rsb);
			   print_request(freq);
			   printk("nodeid %u\n", nodeid););

		DLM_ASSERT(!(lkb->lkb_flags & GDLM_LKFLG_MSTCPY),
			   print_lkb(lkb);
			   print_rsb(rsb);
			   print_request(freq);
			   printk("nodeid %u\n", nodeid););

		if (lkb->lkb_lockqueue_state) {
			log_error(rsb->res_ls, "granting lock on lockqueue");
			print_lkb(lkb);
			print_request(freq);
			lkb->lkb_lockqueue_state = 0;
			remove_from_lockqueue(lkb);
			if (!lkb->lkb_remid)
				lkb->lkb_remid = req->rh_lkid;
		}

		down_write(&rsb->res_lock);

		if (lkb->lkb_flags & GDLM_LKFLG_VALBLK)
			memcpy(lkb->lkb_lvbptr, freq->rr_lvb, DLM_LVB_LEN);

		lkb->lkb_grmode = lkb->lkb_rqmode;
		lkb->lkb_rqmode = DLM_LOCK_IV;

		if (lkb->lkb_range) {
			lkb->lkb_range[GR_RANGE_START] =
			    lkb->lkb_range[RQ_RANGE_START];
			lkb->lkb_range[GR_RANGE_END] =
			    lkb->lkb_range[RQ_RANGE_END];
		}

		lkb_swqueue(rsb, lkb, GDLM_LKSTS_GRANTED);
		up_write(&rsb->res_lock);

		if (freq->rr_flags & GDLM_LKFLG_DEMOTED)
			lkb->lkb_flags |= GDLM_LKFLG_DEMOTED;

		lkb->lkb_retstatus = 0;
		queue_ast(lkb, AST_COMP, 0);
		break;

	case GDLM_REMCMD_SENDBAST:

		lkb = find_lock_by_id(lspace, freq->rr_remlkid);

		DLM_ASSERT(lkb,
			   print_request(freq);
			   printk("nodeid %u\n", nodeid););

		if (lkb->lkb_status == GDLM_LKSTS_GRANTED)
			queue_ast(lkb, AST_BAST, freq->rr_rqmode);
		break;

	case GDLM_REMCMD_SENDCAST:

		/* This is only used for some error completion ASTs */

		lkb = find_lock_by_id(lspace, freq->rr_remlkid);

		DLM_ASSERT(lkb,
			   print_request(freq);
			   printk("nodeid %u\n", nodeid););

		/* Return the lock to granted status */
		res_lkb_swqueue(lkb->lkb_resource, lkb, GDLM_LKSTS_GRANTED);
		lkb->lkb_retstatus = freq->rr_status;
		queue_ast(lkb, AST_COMP, 0);
		break;

	case GDLM_REMCMD_UNLOCKREQUEST:

		lkb = find_lock_by_id(lspace, freq->rr_remlkid);

		DLM_ASSERT(lkb,
			   print_request(freq);
			   printk("nodeid %u\n", nodeid););

		DLM_ASSERT(lkb->lkb_flags & GDLM_LKFLG_MSTCPY,
			   print_lkb(lkb);
			   print_request(freq);
			   printk("nodeid %u\n", nodeid););

		rsb = find_rsb_to_unlock(lspace, lkb);

		log_debug(lspace, "un from %u %x \"%s\"", nodeid, lkb->lkb_id,
			  rsb->res_name);

		reply.rl_status = dlm_unlock_stage2(lkb, rsb, freq->rr_flags);
		send_reply = 1;
		break;

	case GDLM_REMCMD_QUERY:
	        remote_query(nodeid, lspace, req);
		break;

	case GDLM_REMCMD_QUERYREPLY:
	        remote_query_reply(nodeid, lspace, req);
		break;

	default:
		log_error(lspace, "process_cluster_request cmd %d",req->rh_cmd);
	}

	up_read(&lspace->ls_in_recovery);

      out:
	if (send_reply) {
		reply.rl_header.rh_cmd = GDLM_REMCMD_LOCKREPLY;
		reply.rl_header.rh_flags = 0;
		reply.rl_header.rh_length = sizeof(reply);
		reply.rl_header.rh_lkid = freq->rr_header.rh_lkid;
		reply.rl_header.rh_lockspace = freq->rr_header.rh_lockspace;

		status = midcomms_send_message(nodeid, &reply.rl_header,
			                       GFP_KERNEL);
	}

	wake_astd();
	put_lockspace(lspace);
	return status;
}

static void add_reply_lvb(struct dlm_lkb *lkb, struct dlm_reply *reply)
{
	if (lkb->lkb_flags & GDLM_LKFLG_VALBLK)
		memcpy(reply->rl_lvb, lkb->lkb_lvbptr, DLM_LVB_LEN);
}

static void add_request_lvb(struct dlm_lkb *lkb, struct dlm_request *req)
{
	if (lkb->lkb_flags & GDLM_LKFLG_VALBLK)
		memcpy(req->rr_lvb, lkb->lkb_lvbptr, DLM_LVB_LEN);
}
