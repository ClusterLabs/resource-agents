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
 * queries.c
 *
 * This file provides the kernel query interface to the DLM.
 *
 */

#define EXPORT_SYMTAB
#include <linux/module.h>

#include "dlm_internal.h"
#include "lockspace.h"
#include "lockqueue.h"
#include "locking.h"
#include "lkb.h"
#include "nodes.h"
#include "dir.h"
#include "ast.h"
#include "memory.h"
#include "lowcomms.h"
#include "midcomms.h"
#include "rsb.h"

static int query_resource(struct dlm_rsb *rsb, struct dlm_resinfo *resinfo);
static int query_locks(int query, struct dlm_lkb *lkb, struct dlm_queryinfo *qinfo);

/*
 * API entry point.
 */
int dlm_query(void *lockspace,
	      struct dlm_lksb *lksb,
	      int query,
	      struct dlm_queryinfo *qinfo,
	      void (ast_routine(void *)),
	      void *astarg)
{
	int status = -EINVAL;
	struct dlm_lkb *target_lkb;
	struct dlm_lkb *query_lkb = NULL;	/* Our temporary LKB */
	struct dlm_ls  *ls = find_lockspace_by_local_id(lockspace);

	if (!ls)
		return -EINVAL;
	if (!qinfo)
		goto out;
	if (!ast_routine)
	        goto out;
	if (!lksb)
	        goto out;

	if (!qinfo->gqi_lockinfo)
		qinfo->gqi_locksize = 0;

        /* Find the lkid */
	target_lkb = find_lock_by_id(ls, lksb->sb_lkid);
	if (!target_lkb)
		goto out;

	/* If the user wants a list of locks that are blocking or
	   not blocking this lock, then it must be waiting
	   for something
	*/
	if (((query & DLM_QUERY_MASK) == DLM_QUERY_LOCKS_BLOCKING ||
	     (query & DLM_QUERY_MASK) == DLM_QUERY_LOCKS_NOTBLOCK) &&
	    target_lkb->lkb_status == GDLM_LKSTS_GRANTED)
		goto out;

	/* We now allocate an LKB for our own use (so we can hang
	 * things like the AST routine and the lksb from it) */
	lksb->sb_status = -EBUSY;
	query_lkb = create_lkb(ls);
	if (!query_lkb) {
	        status = -ENOMEM;
		goto out;
	}
	query_lkb->lkb_astaddr  = ast_routine;
	query_lkb->lkb_astparam = (long)astarg;
	query_lkb->lkb_resource = target_lkb->lkb_resource;
	query_lkb->lkb_lksb     = lksb;

	/* Don't free the resource while we are querying it. This ref
	 * will be dropped when the LKB is freed */
	hold_rsb(query_lkb->lkb_resource);

	/* Fill in the stuff that's always local */
	if (qinfo->gqi_resinfo) {
		if (target_lkb->lkb_resource->res_nodeid)
			qinfo->gqi_resinfo->rsi_masternode =
				target_lkb->lkb_resource->res_nodeid;
		else
			qinfo->gqi_resinfo->rsi_masternode = our_nodeid();
		qinfo->gqi_resinfo->rsi_length =
			target_lkb->lkb_resource->res_length;
		memcpy(qinfo->gqi_resinfo->rsi_name,
		       target_lkb->lkb_resource->res_name,
		       qinfo->gqi_resinfo->rsi_length);
	}

	/* If the master is local (or the user doesn't want the overhead of a
	 * remote call) - fill in the details here */
	if (target_lkb->lkb_resource->res_nodeid == 0 ||
	    (query & DLM_QUERY_LOCAL)) {

		status = 0;
		/* Resource info */
		if (qinfo->gqi_resinfo) {
			query_resource(target_lkb->lkb_resource,
				       qinfo->gqi_resinfo);
		}

		/* Lock lists */
		if (qinfo->gqi_lockinfo) {
			status = query_locks(query, target_lkb, qinfo);
		}

		query_lkb->lkb_retstatus = status;
		queue_ast(query_lkb, AST_COMP | AST_DEL, 0);
		wake_astd();

		/* An AST will be delivered so we must return success here */
		status = 0;
		goto out;
	}

	/* Remote master */
	if (target_lkb->lkb_resource->res_nodeid != 0)
	{
		struct dlm_query_request *remquery;
		struct writequeue_entry *e;

		/* Clear this cos the receiving end adds to it with
		   each incoming packet */
		qinfo->gqi_lockcount = 0;

		/* Squirrel a pointer to the query info struct
		   somewhere illegal */
		query_lkb->lkb_request = (struct dlm_request *) qinfo;

		e = lowcomms_get_buffer(query_lkb->lkb_resource->res_nodeid,
					sizeof(struct dlm_query_request),
					ls->ls_allocation,
					(char **) &remquery);
		if (!e) {
			status = -ENOBUFS;
			goto out;
		}

		/* Build remote packet */
		memset(remquery, 0, sizeof(struct dlm_query_request));

		remquery->rq_maxlocks  = qinfo->gqi_locksize;
		remquery->rq_query     = query;
		remquery->rq_mstlkid   = target_lkb->lkb_remid;
		if (qinfo->gqi_lockinfo)
			remquery->rq_maxlocks = qinfo->gqi_locksize;

		remquery->rq_header.rh_cmd       = GDLM_REMCMD_QUERY;
		remquery->rq_header.rh_flags     = 0;
		remquery->rq_header.rh_length    = sizeof(struct dlm_query_request);
		remquery->rq_header.rh_lkid      = query_lkb->lkb_id;
		remquery->rq_header.rh_lockspace = ls->ls_global_id;

		midcomms_send_buffer(&remquery->rq_header, e);
		status = 0;
	}

      out:
	put_lockspace(ls);
	return status;
}

static inline int valid_range(struct dlm_range *r)
{
    if (r->ra_start != 0ULL ||
	r->ra_end != 0xFFFFFFFFFFFFFFFFULL)
	return 1;
    else
	return 0;
}

static void put_int(int x, char *buf, int *offp)
{
        x = cpu_to_le32(x);
        memcpy(buf + *offp, &x, sizeof(int));
        *offp += sizeof(int);
}

static void put_int64(uint64_t x, char *buf, int *offp)
{
        x = cpu_to_le64(x);
        memcpy(buf + *offp, &x, sizeof(uint64_t));
        *offp += sizeof(uint64_t);
}

static int get_int(char *buf, int *offp)
{
        int value;
        memcpy(&value, buf + *offp, sizeof(int));
        *offp += sizeof(int);
        return le32_to_cpu(value);
}

static uint64_t get_int64(char *buf, int *offp)
{
        uint64_t value;

        memcpy(&value, buf + *offp, sizeof(uint64_t));
        *offp += sizeof(uint64_t);
        return le64_to_cpu(value);
}

#define LOCK_LEN (sizeof(int)*4 + sizeof(uint8_t)*4)

/* Called from recvd to get lock info for a remote node */
int remote_query(int nodeid, struct dlm_ls *ls, struct dlm_header *msg)
{
        struct dlm_query_request *query = (struct dlm_query_request *) msg;
	struct dlm_query_reply *reply;
	struct dlm_resinfo resinfo;
	struct dlm_queryinfo qinfo;
	struct writequeue_entry *e;
	char *buf;
	struct dlm_lkb *lkb;
	int status = 0;
	int bufidx;
	int finished = 0;
	int cur_lock = 0;
	int start_lock = 0;

	lkb = find_lock_by_id(ls, query->rq_mstlkid);
	if (!lkb) {
		status = -EINVAL;
		goto send_error;
	}

	qinfo.gqi_resinfo = &resinfo;
	qinfo.gqi_locksize = query->rq_maxlocks;

	/* Get the resource bits */
	query_resource(lkb->lkb_resource, &resinfo);

	/* Now get the locks if wanted */
	if (query->rq_maxlocks) {
	        qinfo.gqi_lockinfo = kmalloc(sizeof(struct dlm_lockinfo) * query->rq_maxlocks,
					     GFP_KERNEL);
		if (!qinfo.gqi_lockinfo) {
		        status = -ENOMEM;
			goto send_error;
		}

		status = query_locks(query->rq_query, lkb, &qinfo);
		if (status && status != -E2BIG) {
			kfree(qinfo.gqi_lockinfo);
			goto send_error;
		}
	}
	else {
	        qinfo.gqi_lockinfo = NULL;
		qinfo.gqi_lockcount = 0;
	}

	/* Send as many blocks as needed for all the locks */
	do {
		int i;
		int msg_len = sizeof(struct dlm_query_reply);
		int last_msg_len = msg_len; /* keeps compiler quiet */
		int last_lock;

		/* First work out how many locks we can fit into a block */
		for (i=cur_lock; i < qinfo.gqi_lockcount && msg_len < PAGE_SIZE; i++) {

			last_msg_len = msg_len;

			msg_len += LOCK_LEN;
			if (valid_range(&qinfo.gqi_lockinfo[i].lki_grrange) ||
			    valid_range(&qinfo.gqi_lockinfo[i].lki_rqrange)) {

				msg_len += sizeof(uint64_t) * 4;
			}
		}

		/* There must be a neater way of doing this... */
		if (msg_len > PAGE_SIZE) {
			last_lock = i-1;
			msg_len = last_msg_len;
		}
		else {
			last_lock = i;
		}

		e = lowcomms_get_buffer(nodeid,
					msg_len,
					ls->ls_allocation,
					(char **) &reply);
		if (!e) {
			kfree(qinfo.gqi_lockinfo);
			status = -ENOBUFS;
			goto out;
		}

		reply->rq_header.rh_cmd       = GDLM_REMCMD_QUERYREPLY;
		reply->rq_header.rh_length    = msg_len;
		reply->rq_header.rh_lkid      = msg->rh_lkid;
		reply->rq_header.rh_lockspace = msg->rh_lockspace;

		reply->rq_status     = status;
		reply->rq_startlock  = cur_lock;
		reply->rq_grantcount = qinfo.gqi_resinfo->rsi_grantcount;
		reply->rq_convcount  = qinfo.gqi_resinfo->rsi_convcount;
		reply->rq_waitcount  = qinfo.gqi_resinfo->rsi_waitcount;
		memcpy(reply->rq_valblk, qinfo.gqi_resinfo->rsi_valblk, DLM_LVB_LEN);

		buf = (char *)reply;
		bufidx = sizeof(struct dlm_query_reply);

		for (; cur_lock < last_lock; cur_lock++) {

			buf[bufidx++] = qinfo.gqi_lockinfo[cur_lock].lki_state;
			buf[bufidx++] = qinfo.gqi_lockinfo[cur_lock].lki_grmode;
			buf[bufidx++] = qinfo.gqi_lockinfo[cur_lock].lki_rqmode;
			put_int(qinfo.gqi_lockinfo[cur_lock].lki_lkid, buf, &bufidx);
			put_int(qinfo.gqi_lockinfo[cur_lock].lki_mstlkid, buf, &bufidx);
			put_int(qinfo.gqi_lockinfo[cur_lock].lki_parent, buf, &bufidx);
			put_int(qinfo.gqi_lockinfo[cur_lock].lki_node, buf, &bufidx);
			put_int(qinfo.gqi_lockinfo[cur_lock].lki_ownpid, buf, &bufidx);

			if (valid_range(&qinfo.gqi_lockinfo[cur_lock].lki_grrange) ||
			    valid_range(&qinfo.gqi_lockinfo[cur_lock].lki_rqrange)) {

				buf[bufidx++] = 1;
				put_int64(qinfo.gqi_lockinfo[cur_lock].lki_grrange.ra_start, buf, &bufidx);
				put_int64(qinfo.gqi_lockinfo[cur_lock].lki_grrange.ra_end, buf, &bufidx);
				put_int64(qinfo.gqi_lockinfo[cur_lock].lki_rqrange.ra_start, buf, &bufidx);
				put_int64(qinfo.gqi_lockinfo[cur_lock].lki_rqrange.ra_end, buf, &bufidx);
			}
			else {
				buf[bufidx++] = 0;
			}
		}

		if (cur_lock == qinfo.gqi_lockcount) {
			reply->rq_header.rh_flags = GDLM_REMFLAG_ENDQUERY;
			finished = 1;
		}
		else {
			reply->rq_header.rh_flags = 0;
		}

		reply->rq_numlocks = cur_lock - start_lock;
		start_lock = cur_lock;

		midcomms_send_buffer(&reply->rq_header, e);
	} while (!finished);

	kfree(qinfo.gqi_lockinfo);
 out:
	return status;

 send_error:
	e = lowcomms_get_buffer(nodeid,
				sizeof(struct dlm_query_reply),
				ls->ls_allocation,
				(char **) &reply);
	if (!e) {
		status =  -ENOBUFS;
		goto out;
	}
	reply->rq_header.rh_cmd = GDLM_REMCMD_QUERYREPLY;
	reply->rq_header.rh_flags = GDLM_REMFLAG_ENDQUERY;
	reply->rq_header.rh_length = sizeof(struct dlm_query_reply);
	reply->rq_header.rh_lkid = msg->rh_lkid;
	reply->rq_header.rh_lockspace = msg->rh_lockspace;
	reply->rq_status     = status;
	reply->rq_numlocks   = 0;
	reply->rq_startlock  = 0;
	reply->rq_grantcount = 0;
	reply->rq_convcount  = 0;
	reply->rq_waitcount  = 0;

	midcomms_send_buffer(&reply->rq_header, e);

	return status;
}

/* Reply to a remote query */
int remote_query_reply(int nodeid, struct dlm_ls *ls, struct dlm_header *msg)
{
	struct dlm_lkb *query_lkb;
	struct dlm_queryinfo *qinfo;
	struct dlm_query_reply *reply;
	char *buf;
	int i;
	int bufidx;

	query_lkb = find_lock_by_id(ls, msg->rh_lkid);
	if (!query_lkb)
		return -EINVAL;

	qinfo = (struct dlm_queryinfo *) query_lkb->lkb_request;
	reply = (struct dlm_query_reply *) msg;

	/* Copy the easy bits first */
	qinfo->gqi_lockcount += reply->rq_numlocks;
	if (qinfo->gqi_resinfo) {
		qinfo->gqi_resinfo->rsi_grantcount = reply->rq_grantcount;
		qinfo->gqi_resinfo->rsi_convcount = reply->rq_convcount;
		qinfo->gqi_resinfo->rsi_waitcount = reply->rq_waitcount;
		memcpy(qinfo->gqi_resinfo->rsi_valblk, reply->rq_valblk,
			DLM_LVB_LEN);
	}

	/* Now unpack the locks */
	bufidx = sizeof(struct dlm_query_reply);
	buf = (char *) msg;

	DLM_ASSERT(reply->rq_startlock + reply->rq_numlocks <= qinfo->gqi_locksize,
		    printk("start = %d, num + %d. Max=  %d\n",
			   reply->rq_startlock, reply->rq_numlocks, qinfo->gqi_locksize););

	for (i = reply->rq_startlock;
	     i < reply->rq_startlock + reply->rq_numlocks; i++) {
		qinfo->gqi_lockinfo[i].lki_state = buf[bufidx++];
		qinfo->gqi_lockinfo[i].lki_grmode = buf[bufidx++];
		qinfo->gqi_lockinfo[i].lki_rqmode = buf[bufidx++];
		qinfo->gqi_lockinfo[i].lki_lkid = get_int(buf, &bufidx);
		qinfo->gqi_lockinfo[i].lki_mstlkid = get_int(buf, &bufidx);
		qinfo->gqi_lockinfo[i].lki_parent = get_int(buf, &bufidx);
		qinfo->gqi_lockinfo[i].lki_node = get_int(buf, &bufidx);
		qinfo->gqi_lockinfo[i].lki_ownpid = get_int(buf, &bufidx);
		if (buf[bufidx++]) {
			qinfo->gqi_lockinfo[i].lki_grrange.ra_start = get_int64(buf, &bufidx);
			qinfo->gqi_lockinfo[i].lki_grrange.ra_end   = get_int64(buf, &bufidx);
			qinfo->gqi_lockinfo[i].lki_rqrange.ra_start = get_int64(buf, &bufidx);
			qinfo->gqi_lockinfo[i].lki_rqrange.ra_end   = get_int64(buf, &bufidx);
		}
		else {
			qinfo->gqi_lockinfo[i].lki_grrange.ra_start = 0ULL;
			qinfo->gqi_lockinfo[i].lki_grrange.ra_end   = 0xFFFFFFFFFFFFFFFFULL;
			qinfo->gqi_lockinfo[i].lki_rqrange.ra_start = 0ULL;
			qinfo->gqi_lockinfo[i].lki_rqrange.ra_end   = 0xFFFFFFFFFFFFFFFFULL;
		}
	}

	/* If this was the last block then now tell the user */
	if (msg->rh_flags & GDLM_REMFLAG_ENDQUERY) {
	        query_lkb->lkb_retstatus = reply->rq_status;
		queue_ast(query_lkb, AST_COMP | AST_DEL, 0);
		wake_astd();
	}

	return 0;
}

/* Aggregate resource information */
static int query_resource(struct dlm_rsb *rsb, struct dlm_resinfo *resinfo)
{
	struct list_head *tmp;


	if (rsb->res_lvbptr)
		memcpy(resinfo->rsi_valblk, rsb->res_lvbptr, DLM_LVB_LEN);

	resinfo->rsi_grantcount = 0;
	list_for_each(tmp, &rsb->res_grantqueue) {
		resinfo->rsi_grantcount++;
	}

	resinfo->rsi_waitcount = 0;
	list_for_each(tmp, &rsb->res_waitqueue) {
		resinfo->rsi_waitcount++;
	}

	resinfo->rsi_convcount = 0;
	list_for_each(tmp, &rsb->res_convertqueue) {
		resinfo->rsi_convcount++;
	}

	return 0;
}

static int add_lock(struct dlm_lkb *lkb, struct dlm_queryinfo *qinfo)
{
	int entry;

	/* Don't fill it in if the buffer is full */
	if (qinfo->gqi_lockcount == qinfo->gqi_locksize)
		return -E2BIG;

	/* gqi_lockcount contains the number of locks we have returned */
	entry = qinfo->gqi_lockcount++;

	/* Fun with master copies */
	if (lkb->lkb_flags & GDLM_LKFLG_MSTCPY) {
	        qinfo->gqi_lockinfo[entry].lki_lkid = lkb->lkb_remid;
		qinfo->gqi_lockinfo[entry].lki_mstlkid = lkb->lkb_id;
	}
	else {
	        qinfo->gqi_lockinfo[entry].lki_lkid = lkb->lkb_id;
		qinfo->gqi_lockinfo[entry].lki_mstlkid = lkb->lkb_remid;
	}

	/* Also make sure we always have a valid nodeid in there, the
	   calling end may not know which node "0" is */
	if (lkb->lkb_nodeid)
	    qinfo->gqi_lockinfo[entry].lki_node = lkb->lkb_nodeid;
	else
	    qinfo->gqi_lockinfo[entry].lki_node = our_nodeid();

	if (lkb->lkb_parent)
		qinfo->gqi_lockinfo[entry].lki_parent = lkb->lkb_parent->lkb_id;
	else
		qinfo->gqi_lockinfo[entry].lki_parent = 0;

	qinfo->gqi_lockinfo[entry].lki_state  = lkb->lkb_status;
	qinfo->gqi_lockinfo[entry].lki_rqmode = lkb->lkb_rqmode;
	qinfo->gqi_lockinfo[entry].lki_grmode = lkb->lkb_grmode;
	qinfo->gqi_lockinfo[entry].lki_ownpid = lkb->lkb_ownpid;

	if (lkb->lkb_range) {
		qinfo->gqi_lockinfo[entry].lki_grrange.ra_start =
			lkb->lkb_range[GR_RANGE_START];
		qinfo->gqi_lockinfo[entry].lki_grrange.ra_end =
			lkb->lkb_range[GR_RANGE_END];
		qinfo->gqi_lockinfo[entry].lki_rqrange.ra_start =
			lkb->lkb_range[RQ_RANGE_START];
		qinfo->gqi_lockinfo[entry].lki_rqrange.ra_end =
			lkb->lkb_range[RQ_RANGE_END];
	} else {
		qinfo->gqi_lockinfo[entry].lki_grrange.ra_start = 0ULL;
		qinfo->gqi_lockinfo[entry].lki_grrange.ra_start = 0xffffffffffffffffULL;
		qinfo->gqi_lockinfo[entry].lki_rqrange.ra_start = 0ULL;
		qinfo->gqi_lockinfo[entry].lki_rqrange.ra_start = 0xffffffffffffffffULL;
	}
	return 0;
}

static int query_lkb_queue(struct list_head *queue, int query,
			   struct dlm_queryinfo *qinfo)
{
	struct list_head *tmp;
	int status = 0;
	int mode = query & DLM_QUERY_MODE_MASK;

	list_for_each(tmp, queue) {
		struct dlm_lkb *lkb = list_entry(tmp, struct dlm_lkb, lkb_statequeue);
		int lkmode;

		if (query & DLM_QUERY_RQMODE)
			lkmode = lkb->lkb_rqmode;
		else
			lkmode = lkb->lkb_grmode;

		/* Add the LKB info to the list if it matches the criteria in
		 * the query bitmap */
		switch (query & DLM_QUERY_MASK) {
		case DLM_QUERY_LOCKS_ALL:
			status = add_lock(lkb, qinfo);
			break;

		case DLM_QUERY_LOCKS_HIGHER:
			if (lkmode > mode)
				status = add_lock(lkb, qinfo);
			break;

		case DLM_QUERY_LOCKS_EQUAL:
			if (lkmode == mode)
				status = add_lock(lkb, qinfo);
			break;

		case DLM_QUERY_LOCKS_LOWER:
			if (lkmode < mode)
				status = add_lock(lkb, qinfo);
			break;
		}
	}
	return status;
}

/*
 * Return 1 if the locks' ranges overlap
 * If the lkb has no range then it is assumed to cover 0-ffffffff.ffffffff
 */
static inline int ranges_overlap(struct dlm_lkb *lkb1, struct dlm_lkb *lkb2)
{
	if (!lkb1->lkb_range || !lkb2->lkb_range)
		return 1;

	if (lkb1->lkb_range[RQ_RANGE_END] <= lkb2->lkb_range[GR_RANGE_START] ||
	    lkb1->lkb_range[RQ_RANGE_START] >= lkb2->lkb_range[GR_RANGE_END])
		return 0;

	return 1;
}
extern const int __dlm_compat_matrix[8][8];


static int get_blocking_locks(struct dlm_lkb *qlkb, struct dlm_queryinfo *qinfo)
{
	struct list_head *tmp;
	int status = 0;

	list_for_each(tmp, &qlkb->lkb_resource->res_grantqueue) {
		struct dlm_lkb *lkb = list_entry(tmp, struct dlm_lkb, lkb_statequeue);

		if (ranges_overlap(lkb, qlkb) &&
		    !__dlm_compat_matrix[lkb->lkb_grmode + 1][qlkb->lkb_rqmode + 1])
			status = add_lock(lkb, qinfo);
	}

	return status;
}

static int get_nonblocking_locks(struct dlm_lkb *qlkb, struct dlm_queryinfo *qinfo)
{
	struct list_head *tmp;
	int status = 0;

	list_for_each(tmp, &qlkb->lkb_resource->res_grantqueue) {
		struct dlm_lkb *lkb = list_entry(tmp, struct dlm_lkb, lkb_statequeue);

		if (!(ranges_overlap(lkb, qlkb) &&
		      !__dlm_compat_matrix[lkb->lkb_grmode + 1][qlkb->lkb_rqmode + 1]))
			status = add_lock(lkb, qinfo);
	}

	return status;
}

/* Gather a list of appropriate locks */
static int query_locks(int query, struct dlm_lkb *lkb, struct dlm_queryinfo *qinfo)
{
	int status = 0;


	/* Mask in the actual granted/requsted mode of the lock if LOCK_THIS
	 * was requested as the mode
	 */
	if ((query & DLM_QUERY_MODE_MASK) == DLM_LOCK_THIS) {
		query &= ~DLM_QUERY_MODE_MASK;
		if (query & DLM_QUERY_RQMODE)
			query |= lkb->lkb_rqmode;
		else
			query |= lkb->lkb_grmode;
	}

	qinfo->gqi_lockcount = 0;

	/* BLOCKING/NOTBLOCK only look at the granted queue */
	if ((query & DLM_QUERY_MASK) == DLM_QUERY_LOCKS_BLOCKING)
		return get_blocking_locks(lkb, qinfo);

	if ((query & DLM_QUERY_MASK) == DLM_QUERY_LOCKS_NOTBLOCK)
		return get_nonblocking_locks(lkb, qinfo);

        /* Do the lock queues that were requested */
	if (query & DLM_QUERY_QUEUE_GRANT) {
		status = query_lkb_queue(&lkb->lkb_resource->res_grantqueue,
					 query,	qinfo);
	}

	if (!status && (query & DLM_QUERY_QUEUE_CONVERT)) {
		status = query_lkb_queue(&lkb->lkb_resource->res_convertqueue,
					 query, qinfo);
	}

	if (!status && (query & DLM_QUERY_QUEUE_WAIT)) {
		status = query_lkb_queue(&lkb->lkb_resource->res_waitqueue,
					 query, qinfo);
	}


	return status;
}

EXPORT_SYMBOL(dlm_query);
/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
