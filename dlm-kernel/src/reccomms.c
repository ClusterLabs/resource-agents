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
#include "lowcomms.h"
#include "midcomms.h"
#include "reccomms.h"
#include "nodes.h"
#include "lockspace.h"
#include "recover.h"
#include "dir.h"
#include "config.h"
#include "rebuild.h"
#include "memory.h"

/* Running on the basis that only a single recovery communication will be done
 * at a time per lockspace */

static void rcom_process_message(struct dlm_ls *ls, uint32_t nodeid, struct dlm_rcom *rc);

static int rcom_response(struct dlm_ls *ls)
{
	return test_bit(LSFL_RECCOMM_READY, &ls->ls_flags);
}

/**
 * rcom_send_message - send or request recovery data
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
 *          rcom_send_message(rc);
 *
 * The max value of "mylen" is dlm_config.buffer_size - sizeof(struct
 * dlm_rcom).  If more data must be passed in one send, use
 * rcom_expand_buffer() which incrementally increases the size of the rc buffer
 * by dlm_config.buffer_size bytes.
 *
 * Any data returned for the message (when need_reply is set) will saved in
 * rc->rc_buf when this function returns and rc->rc_datalen will be set to the
 * number of bytes copied into rc->rc_buf.
 *
 * Returns: 0 on success, -EXXX on failure
 */

int rcom_send_message(struct dlm_ls *ls, uint32_t nodeid, int type,
		      struct dlm_rcom *rc, int need_reply)
{
	int error = 0;

	if (!rc->rc_datalen)
		rc->rc_datalen = 1;

	/* 
	 * Fill in the header.
	 * FIXME: set msgid's differently, rsb_master_lookup needs to
	 * set and remember the msgid before calling here.
	 */

	rc->rc_header.rh_cmd = GDLM_REMCMD_RECOVERMESSAGE;
	rc->rc_header.rh_lockspace = ls->ls_global_id;
	rc->rc_header.rh_length = sizeof(struct dlm_rcom) + rc->rc_datalen - 1;
	rc->rc_subcmd = type;
	rc->rc_msgid = ++ls->ls_rcom_msgid;

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
		DLM_ASSERT(!test_bit(LSFL_RECCOMM_READY, &ls->ls_flags),);
	}

	/* 
	 * Process the message locally.
	 */

	if (nodeid == our_nodeid()) {
		rcom_process_message(ls, nodeid, rc);
		goto out;
	}

	/* 
	 * Send the message.
	 */

	error = midcomms_send_message(nodeid, (struct dlm_header *) rc,
				      GFP_KERNEL);
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
		clear_bit(LSFL_RECCOMM_READY, &ls->ls_flags);
		up(&ls->ls_rcom_lock);
	}

	return error;
}

/* 
 * Runs in same context as midcomms.
 */

static void rcom_process_message(struct dlm_ls *ls, uint32_t nodeid, struct dlm_rcom *rc)
{
	struct dlm_rcom rc_stack;
	struct dlm_rcom *reply = NULL;
	int status, datalen, maxlen;
	uint32_t r_nodeid, be_nodeid;

	if (!ls)
		return;

	if (dlm_recovery_stopped(ls) && (rc->rc_subcmd != RECCOMM_STATUS)) {
		log_debug(ls, "ignoring recovery message %x from %u",
			  rc->rc_subcmd, nodeid);
		return;
	}

	switch (rc->rc_subcmd) {

	case RECCOMM_STATUS:

		memset(&rc_stack, 0, sizeof(struct dlm_rcom));
		reply = &rc_stack;

		reply->rc_header.rh_cmd = GDLM_REMCMD_RECOVERREPLY;
		reply->rc_header.rh_lockspace = rc->rc_header.rh_lockspace;
		reply->rc_subcmd = rc->rc_subcmd;
		reply->rc_msgid = rc->rc_msgid;
		reply->rc_buf[0] = 0;

		if (test_bit(LSFL_RESDIR_VALID, &ls->ls_flags))
			reply->rc_buf[0] |= RESDIR_VALID;

		if (test_bit(LSFL_ALL_RESDIR_VALID, &ls->ls_flags))
			reply->rc_buf[0] |= RESDIR_ALL_VALID;

		if (test_bit(LSFL_NODES_VALID, &ls->ls_flags))
			reply->rc_buf[0] |= NODES_VALID;

		if (test_bit(LSFL_ALL_NODES_VALID, &ls->ls_flags))
			reply->rc_buf[0] |= NODES_ALL_VALID;

		reply->rc_datalen = 1;
		reply->rc_header.rh_length =
			sizeof(struct dlm_rcom) + reply->rc_datalen - 1;
		break;

	case RECCOMM_RECOVERNAMES:

		/*
		 * We can't run dlm_dir_rebuild_send (which uses ls_nodes)
		 * while dlm_recoverd is running ls_nodes_reconfig (which
		 * changes ls_nodes).  It could only happen in rare cases where
		 * we get a late RECOVERNAMES message from a previous instance
		 * of recovery.
		 */
		if (!test_bit(LSFL_NODES_VALID, &ls->ls_flags)) {
			log_debug(ls, "ignoring RECOVERNAMES from %u", nodeid);
			return;
		}

		reply = allocate_rcom_buffer(ls);
		DLM_ASSERT(reply,);
		maxlen = dlm_config.buffer_size - sizeof(struct dlm_rcom);

		reply->rc_header.rh_cmd = GDLM_REMCMD_RECOVERREPLY;
		reply->rc_header.rh_lockspace = rc->rc_header.rh_lockspace;
		reply->rc_subcmd = rc->rc_subcmd;
		reply->rc_msgid = rc->rc_msgid;

		/* 
		 * The other node wants a bunch of resource names.  The name of
		 * the resource to begin with is in rc->rc_buf.
		 */

		datalen = dlm_dir_rebuild_send(ls, rc->rc_buf, rc->rc_datalen,
					       reply->rc_buf, maxlen, nodeid);

		reply->rc_datalen = datalen;
		reply->rc_header.rh_length =
		    sizeof(struct dlm_rcom) + reply->rc_datalen - 1;
		break;

	case RECCOMM_GETMASTER:

		reply = allocate_rcom_buffer(ls);
		DLM_ASSERT(reply,);

		reply->rc_header.rh_cmd = GDLM_REMCMD_RECOVERREPLY;
		reply->rc_header.rh_lockspace = rc->rc_header.rh_lockspace;
		reply->rc_subcmd = rc->rc_subcmd;
		reply->rc_msgid = rc->rc_msgid;

		/* 
		 * The other node wants to know the master of a named resource.
		 */

		status = dlm_dir_lookup(ls, nodeid, rc->rc_buf, rc->rc_datalen,
					&r_nodeid);
		if (status != 0) {
			log_error(ls, "rcom lookup error %d", status);
			free_rcom_buffer(reply);
			reply = NULL;
			return;
		}
		be_nodeid = cpu_to_be32(r_nodeid);
		memcpy(reply->rc_buf, &be_nodeid, sizeof(uint32_t));
		reply->rc_datalen = sizeof(uint32_t);
		reply->rc_header.rh_length =
		    sizeof(struct dlm_rcom) + reply->rc_datalen - 1;
		break;

		/* 
		 * These RECCOMM messages don't need replies.
		 */

	case RECCOMM_NEWLOCKS:
		rebuild_rsbs_recv(ls, nodeid, rc->rc_buf, rc->rc_datalen);
		break;

	case RECCOMM_NEWLOCKIDS:
		rebuild_rsbs_lkids_recv(ls, nodeid, rc->rc_buf, rc->rc_datalen);
		break;

	case RECCOMM_REMRESDATA:
		dlm_dir_remove(ls, nodeid, rc->rc_buf, rc->rc_datalen);
		break;

	default:
		DLM_ASSERT(0, printk("cmd=%x\n", rc->rc_subcmd););
	}

	if (reply) {
		if (nodeid == our_nodeid()) {
			DLM_ASSERT(rc == ls->ls_rcom,);
			memcpy(rc->rc_buf, reply->rc_buf, reply->rc_datalen);
			rc->rc_datalen = reply->rc_datalen;
		} else {
			midcomms_send_message(nodeid,
					      (struct dlm_header *) reply,
					      GFP_KERNEL);
		}

		if (reply != &rc_stack)
			free_rcom_buffer(reply);
	}
}

static void process_reply_sync(struct dlm_ls *ls, uint32_t nodeid,
			       struct dlm_rcom *reply)
{
	struct dlm_rcom *rc = ls->ls_rcom;

	if (reply->rc_msgid != le32_to_cpu(rc->rc_msgid)) {
		log_error(ls, "unexpected rcom msgid %x/%x nodeid=%u",
		          reply->rc_msgid, le32_to_cpu(rc->rc_msgid), nodeid);
		return;
	}

	memcpy(rc->rc_buf, reply->rc_buf, reply->rc_datalen);
	rc->rc_datalen = reply->rc_datalen;

	/* 
	 * Tell the thread waiting in rcom_send_message() that it can go ahead.
	 */

	set_bit(LSFL_RECCOMM_READY, &ls->ls_flags);
	wake_up(&ls->ls_wait_general);
}

static void process_reply_async(struct dlm_ls *ls, uint32_t nodeid,
				struct dlm_rcom *reply)
{
	restbl_rsb_update_recv(ls, nodeid, reply->rc_buf, reply->rc_datalen,
			       reply->rc_msgid);
}

/* 
 * Runs in same context as midcomms.
 */

static void rcom_process_reply(struct dlm_ls *ls, uint32_t nodeid,
			       struct dlm_rcom *reply)
{
	if (dlm_recovery_stopped(ls)) {
		log_debug(ls, "ignoring recovery reply %x from %u",
			  reply->rc_subcmd, nodeid);
		return;
	}

	switch (reply->rc_subcmd) {
	case RECCOMM_GETMASTER:
		process_reply_async(ls, nodeid, reply);
		break;
	case RECCOMM_STATUS:
	case RECCOMM_NEWLOCKS:
	case RECCOMM_NEWLOCKIDS:
	case RECCOMM_RECOVERNAMES:
		process_reply_sync(ls, nodeid, reply);
		break;
	default:
		log_error(ls, "unknown rcom reply subcmd=%x nodeid=%u",
		          reply->rc_subcmd, nodeid);
	}
}


static int send_ls_not_ready(uint32_t nodeid, struct dlm_header *header)
{
	struct writequeue_entry *wq;
	struct dlm_rcom *rc = (struct dlm_rcom *) header;
	struct dlm_rcom *reply;

	wq = lowcomms_get_buffer(nodeid, sizeof(struct dlm_rcom), GFP_KERNEL,
			         (char **)&reply);
	if (!wq)
		return -ENOMEM;

	reply->rc_header.rh_cmd = GDLM_REMCMD_RECOVERREPLY;
	reply->rc_header.rh_lockspace = rc->rc_header.rh_lockspace;
	reply->rc_subcmd = rc->rc_subcmd;
	reply->rc_msgid = rc->rc_msgid;
	reply->rc_buf[0] = 0;

	reply->rc_datalen = 1;
	reply->rc_header.rh_length = sizeof(struct dlm_rcom) + reply->rc_datalen - 1;

	midcomms_send_buffer((struct dlm_header *)reply, wq);
	return 0;
}


/* 
 * Runs in same context as midcomms.  Both recovery requests and recovery
 * replies come through this function.
 */

void process_recovery_comm(uint32_t nodeid, struct dlm_header *header)
{
	struct dlm_ls *ls = find_lockspace_by_global_id(header->rh_lockspace);
	struct dlm_rcom *rc = (struct dlm_rcom *) header;

	/* If the lockspace doesn't exist then still send a status message
	   back; it's possible that it just doesn't have its global_id yet. */

	if (!ls) {
	      send_ls_not_ready(nodeid, header);
	      return;
	}

	switch (header->rh_cmd) {
	case GDLM_REMCMD_RECOVERMESSAGE:
		rcom_process_message(ls, nodeid, rc);
		break;

	case GDLM_REMCMD_RECOVERREPLY:
		rcom_process_reply(ls, nodeid, rc);
		break;

	default:
		DLM_ASSERT(0, printk("cmd=%x\n", header->rh_cmd););
	}

	put_lockspace(ls);
}

