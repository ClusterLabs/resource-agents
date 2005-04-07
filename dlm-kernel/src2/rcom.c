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
#include "lowcomms.h"
#include "midcomms.h"
#include "rcom.h"
#include "recover.h"
#include "dir.h"
#include "config.h"
#include "memory.h"


/* Running on the basis that only a single synchronous recovery communication
 * will be done at a time per lockspace */


static int rcom_response(struct dlm_ls *ls)
{
	return test_bit(LSFL_RCOM_READY, &ls->ls_flags);
}

/**
 * dlm_send_rcom - send or request recovery data
 * @ls: the lockspace
 * @nodeid: node to which the message is sent
 * @type: type of recovery message
 * @rc: the rc buffer to send
 * @need_reply: wait for reply if this is set
 *
 * Using this interface
 * i)   Allocate an rc buffer:  
 *          rc = allocate_rcom_buffer(ls);
 * ii)  Copy data to send beginning at rc->rc_buf:
 *          memcpy(rc->rc_buf, mybuf, mylen);
 * iii) Set rc->rc_datalen to the number of bytes copied in (ii): 
 *          rc->rc_datalen = mylen
 * iv)  Submit the rc to this function:
 *          dlm_send_rcom(rc);
 *
 * The max value of "mylen" is dlm_config.buffer_size - sizeof struct dlm_rcom.
 *
 * Any data returned for the message (when need_reply is set) will saved in
 * rc->rc_buf when this function returns and rc->rc_datalen will be set to the
 * number of bytes copied into rc->rc_buf.
 *
 * Returns: 0 on success, -EXXX on failure
 */

int dlm_send_rcom(struct dlm_ls *ls, int nodeid, int type, struct dlm_rcom *rc,
		  int need_reply)
{
	struct dlm_header *hd = (struct dlm_header *) rc;
	int error = 0;

	log_debug(ls, "dlm_send_rcom to %d type %d", nodeid, type);

	if (!rc->rc_datalen)
		rc->rc_datalen = 1;

	rc->rc_header.h_lockspace = ls->ls_global_id;
	rc->rc_header.h_nodeid = dlm_our_nodeid();
	rc->rc_header.h_length = sizeof(struct dlm_rcom) + rc->rc_datalen - 1;
	rc->rc_header.h_cmd = DLM_RCOM;
	rc->rc_type = type;

	/* 
	 * When a reply is received, the reply data goes back into this buffer.
	 * Synchronous rcom requests (need_reply=1) are serialised because of
	 * the single ls_rcom.  After sending the message we'll wait at the end
	 * of this function to get a reply.  The READY flag will be set when
	 * the reply has been received and requested data has been copied into
	 * ls->ls_rcom->rc_buf;
	 */

	if (need_reply) {
		down(&ls->ls_rcom_lock);
		ls->ls_rcom = rc;
		DLM_ASSERT(!test_bit(LSFL_RCOM_READY, &ls->ls_flags),);
	}

	/* FIXME: do byte swapping here */
	hd->h_length = cpu_to_le16(hd->h_length);

	/*
	 * Process the message locally.
	 */

	if (nodeid == dlm_our_nodeid()) {
		dlm_receive_rcom((struct dlm_header *) rc, nodeid);
		goto out;
	}

	/*
	 * Send the message.
	 */

	error = lowcomms_send_message(nodeid, (char *) rc,
				      rc->rc_header.h_length, GFP_KERNEL);

	if (error > 0)
		error = 0;
	else if (error < 0) {
		log_debug(ls, "send message %d to %u failed %d",
			  type, nodeid, error);
		goto out;
	}

	/* 
	 * Wait for a reply.  Once a reply is processed from midcomms, the
	 * READY bit will be set and we'll be awoken (dlm_wait_function will
	 * return 0).
	 */

	if (need_reply) {
		error = dlm_wait_function(ls, &rcom_response);
		if (error)
			log_debug(ls, "wait message %d to %u failed %d",
				  type, nodeid, error);
	}

 out:
	if (need_reply) {
		clear_bit(LSFL_RCOM_READY, &ls->ls_flags);
		up(&ls->ls_rcom_lock);
	}

	return error;
}

void rcom_reply_args(struct dlm_ls *ls, struct dlm_rcom *rc,
		     struct dlm_rcom *rc_in, int type, int len)
{
	rc->rc_header.h_lockspace = ls->ls_global_id;
	rc->rc_header.h_nodeid = dlm_our_nodeid();
	rc->rc_header.h_cmd = DLM_RCOM;
	rc->rc_type = type;
	rc->rc_id = rc_in->rc_id;
	rc->rc_datalen = len;
	rc->rc_header.h_length = sizeof(struct dlm_rcom) + len - 1;
}

void send_rcom_reply(struct dlm_ls *ls, int nodeid, struct dlm_rcom *rc)
{
	struct dlm_rcom *rc_local = ls->ls_rcom;

	if (nodeid == dlm_our_nodeid()) {
		memcpy(rc_local->rc_buf, rc->rc_buf, rc->rc_datalen);
		rc_local->rc_datalen = rc->rc_datalen;
	} else
		lowcomms_send_message(nodeid, (char *) rc,
				      rc->rc_header.h_length, GFP_KERNEL);
}

void receive_rcom_status(struct dlm_ls *ls, struct dlm_rcom *rc_in)
{
	struct dlm_rcom rc_stack, *rc;
	int nodeid = rc_in->rc_header.h_nodeid;
	uint8_t status = 0;

	rc = &rc_stack;
	memset(rc, 0, sizeof(struct dlm_rcom));

	if (test_bit(LSFL_DIR_VALID, &ls->ls_flags))
		status |= DIR_VALID;

	if (test_bit(LSFL_ALL_DIR_VALID, &ls->ls_flags))
		status |= DIR_ALL_VALID;

	if (test_bit(LSFL_NODES_VALID, &ls->ls_flags))
		status |= NODES_VALID;

	if (test_bit(LSFL_ALL_NODES_VALID, &ls->ls_flags))
		status |= NODES_ALL_VALID;

	rc->rc_buf[0] = status;

	rcom_reply_args(ls, rc, rc_in, DLM_RCOM_STATUS_REPLY, sizeof(status));
	send_rcom_reply(ls, nodeid, rc);
}

/*
 * The other node wants a bunch of rsb names.  The name of the rsb to begin
 * with is in rc_in->rc_buf.
 */

void receive_rcom_names(struct dlm_ls *ls, struct dlm_rcom *rc_in)
{
	struct dlm_rcom *rc;
	int nodeid = rc_in->rc_header.h_nodeid;
	int datalen, maxlen;

	/*
	 * We can't run dlm_dir_rebuild_send (which uses ls_nodes) while
	 * dlm_recoverd is running ls_nodes_reconfig (which changes ls_nodes).
	 * It could only happen in rare cases where we get a late RECOVERNAMES
	 * message from a previous instance of recovery.
	 */

	if (!test_bit(LSFL_NODES_VALID, &ls->ls_flags)) {
		log_debug(ls, "ignoring RCOM_NAMES from %u", nodeid);
		return;
	}

	rc = allocate_rcom_buffer(ls);
	DLM_ASSERT(rc,);

	maxlen = dlm_config.buffer_size - sizeof(struct dlm_rcom);

	datalen = dlm_copy_master_names(ls, rc_in->rc_buf, rc_in->rc_datalen,
				        rc->rc_buf, maxlen, nodeid);

	rcom_reply_args(ls, rc, rc_in, DLM_RCOM_NAMES_REPLY, datalen);
	send_rcom_reply(ls, nodeid, rc);
	free_rcom_buffer(rc);
}

void receive_rcom_lookup(struct dlm_ls *ls, struct dlm_rcom *rc_in)
{
	struct dlm_rcom *rc;
	int error, nodeid = rc_in->rc_header.h_nodeid;
	uint32_t r_nodeid, be_nodeid;

	rc = allocate_rcom_buffer(ls);
	DLM_ASSERT(rc,);

	error = dlm_dir_lookup(ls, nodeid, rc_in->rc_buf, rc_in->rc_datalen,
			       &r_nodeid);
	if (error != 0) {
		log_error(ls, "rcom lookup error %d", error);
		free_rcom_buffer(rc);
		return;
	}

	be_nodeid = cpu_to_be32(r_nodeid);
	memcpy(rc->rc_buf, &be_nodeid, sizeof(uint32_t));

	rcom_reply_args(ls, rc, rc_in, DLM_RCOM_LOOKUP_REPLY, sizeof(uint32_t));
	send_rcom_reply(ls, nodeid, rc);
	free_rcom_buffer(rc);
}

/*
 * The other node has found that we're the new master of an rsb and is sending
 * us the locks they hold on it.  We send back the lkid's we've given to their
 * locks.
 */

void receive_rcom_locks(struct dlm_ls *ls, struct dlm_rcom *rc_in)
{
	struct dlm_rcom *rc;
	int nodeid = rc_in->rc_header.h_nodeid;
	int datalen = 0;

	/*
	 * This should prevent us from processing any RCOM_LOCKS messages sent
	 * during a previous recovery instance.
	 */

	if (!test_bit(LSFL_DIR_VALID, &ls->ls_flags)) {
		log_debug(ls, "ignoring RCOM_LOCKS from %u", nodeid);
		return;
	}

	rc = allocate_rcom_buffer(ls);
	DLM_ASSERT(rc,);

#if 0
	datalen = rebuild_rsbs_recv(ls, nodeid, rc_in->rc_buf,
				    rc_in->rc_datalen, rc->rc_buf);
#endif
	rcom_reply_args(ls, rc, rc_in, DLM_RCOM_LOCKS_REPLY, datalen);
	send_rcom_reply(ls, nodeid, rc);
	free_rcom_buffer(rc);
}

void receive_rcom_sync_reply(struct dlm_ls *ls, struct dlm_rcom *rc_in)
{
	int nodeid = rc_in->rc_header.h_nodeid;
	struct dlm_rcom *rc_sent = ls->ls_rcom;

	if (!rc_sent) {
		log_error(ls, "no rcom awaiting reply nodeid %d", nodeid);
		return;
	}

	if (rc_in->rc_id != le32_to_cpu(rc_sent->rc_id)) {
		log_error(ls, "expected rcom id %"PRIx64" got %"PRIx64" %d",
		          le64_to_cpu(rc_sent->rc_id), rc_in->rc_id, nodeid);
		return;
	}

	memcpy(rc_sent->rc_buf, rc_in->rc_buf, rc_in->rc_datalen);
	rc_sent->rc_datalen = rc_in->rc_datalen;

	/* 
	 * Tell the thread waiting for this reply in rcom_send_message() that
	 * it can go ahead.
	 */

	set_bit(LSFL_RCOM_READY, &ls->ls_flags);
	wake_up(&ls->ls_wait_general);
}

void receive_rcom_status_reply(struct dlm_ls *ls, struct dlm_rcom *rc_in)
{
	receive_rcom_sync_reply(ls, rc_in);
}

void receive_rcom_names_reply(struct dlm_ls *ls, struct dlm_rcom *rc_in)
{
	receive_rcom_sync_reply(ls, rc_in);
}

void receive_rcom_lookup_reply(struct dlm_ls *ls, struct dlm_rcom *rc_in)
{
	dlm_recover_master_reply(ls, rc_in);
}

void receive_rcom_locks_reply(struct dlm_ls *ls, struct dlm_rcom *rc_in)
{
#if 0
	int nodeid = rc_in->rc_header.h_nodeid;

	if (!test_bit(LSFL_DIR_VALID, &ls->ls_flags)) {
		log_debug(ls, "ignoring RCOM_LOCKS_REPLY from %u", nodeid);
		return;
	}

	rebuild_rsbs_lkids_recv(ls, nodeid, rc_in->rc_buf, rc_in->rc_datalen);
#endif
}

static int send_ls_not_ready(int nodeid, struct dlm_header *header)
{
	struct writequeue_entry *wq;
	struct dlm_rcom *rc = (struct dlm_rcom *) header;
	struct dlm_rcom *reply;

	wq = lowcomms_get_buffer(nodeid, sizeof(struct dlm_rcom), GFP_KERNEL,
			         (char **)&reply);
	if (!wq)
		return -ENOMEM;

	reply->rc_header.h_lockspace = rc->rc_header.h_lockspace;
	reply->rc_type = rc->rc_type;
	reply->rc_id = rc->rc_id;
	reply->rc_buf[0] = 0;

	reply->rc_datalen = 1;
	reply->rc_header.h_length = sizeof(struct dlm_rcom) +
				    reply->rc_datalen - 1;

	/* FIXME: do byte swapping */

	lowcomms_commit_buffer(wq);
	return 0;
}

/* Called by dlm_recvd; corresponds to dlm_receive_message() but special
   recovery-only comms are sent through here. */

void dlm_receive_rcom(struct dlm_header *hd, int nodeid)
{
	struct dlm_rcom *rc = (struct dlm_rcom *) hd;
	struct dlm_ls *ls;

	/* FIXME: do byte swapping here */
	hd->h_length = le16_to_cpu(hd->h_length);

	/* If the lockspace doesn't exist then still send a status message
	   back; it's possible that it just doesn't have its global_id yet. */

	ls = find_lockspace_global(hd->h_lockspace);
	if (!ls) {
	      send_ls_not_ready(nodeid, hd);
	      return;
	}

	if (dlm_recovery_stopped(ls) && (rc->rc_type != DLM_RCOM_STATUS)) {
		log_error(ls, "ignoring recovery message %x from %d",
			  rc->rc_type, nodeid);
		return;
	}

	if (nodeid != rc->rc_header.h_nodeid) {
		log_error(ls, "bad rcom nodeid %d from %d",
			  rc->rc_header.h_nodeid, nodeid);
		return;
	}

	switch (rc->rc_type) {
	case DLM_RCOM_STATUS:
		receive_rcom_status(ls, rc);
		break;

	case DLM_RCOM_NAMES:
		receive_rcom_names(ls, rc);
		break;

	case DLM_RCOM_LOOKUP:
		receive_rcom_lookup(ls, rc);
		break;

	case DLM_RCOM_LOCKS:
		receive_rcom_locks(ls, rc);
		break;

	case DLM_RCOM_STATUS_REPLY:
		receive_rcom_status_reply(ls, rc);
		break;

	case DLM_RCOM_NAMES_REPLY:
		receive_rcom_names_reply(ls, rc);
		break;

	case DLM_RCOM_LOOKUP_REPLY:
		receive_rcom_lookup_reply(ls, rc);
		break;

	case DLM_RCOM_LOCKS_REPLY:
		receive_rcom_locks_reply(ls, rc);
		break;

	default:
		DLM_ASSERT(0, printk("rc_type=%x\n", rc->rc_type););
	}

	put_lockspace(ls);
}

