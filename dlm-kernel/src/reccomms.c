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

static void rcom_process_message(gd_ls_t * ls, uint32_t nodeid, gd_rcom_t * rc);

/*
 * Track per-node progress/stats during recovery to help debugging.
 */

void rcom_log(gd_ls_t *ls, int nodeid, gd_rcom_t *rc, int send)
{
	gd_csb_t *csb;
	int found = 0;
 
	list_for_each_entry(csb, &ls->ls_nodes, csb_list) {
		if (csb->csb_node->gn_nodeid == nodeid) {
			found = TRUE;
			break;
		}
	}

	if (!found)
		return;

	if (rc->rc_subcmd == RECCOMM_RECOVERNAMES) {
		if (send) {
			csb->csb_names_send_count++;
			csb->csb_names_send_msgid = rc->rc_msgid;
		} else {
			csb->csb_names_recv_count++;
			csb->csb_names_recv_msgid = rc->rc_msgid;
		}
	} else if (rc->rc_subcmd == RECCOMM_NEWLOCKS) {
		if (send) {
			csb->csb_locks_send_count++;
			csb->csb_locks_send_msgid = rc->rc_msgid;
		} else {
			csb->csb_locks_recv_count++;
			csb->csb_locks_recv_msgid = rc->rc_msgid;
		}
	}
}

void rcom_log_clear(gd_ls_t *ls)
{
	gd_csb_t *csb;
 
	list_for_each_entry(csb, &ls->ls_nodes, csb_list) {
		csb->csb_names_send_count = 0;
		csb->csb_names_send_msgid = 0;
		csb->csb_names_recv_count = 0;
		csb->csb_names_recv_msgid = 0;
		csb->csb_locks_send_count = 0;
		csb->csb_locks_send_msgid = 0;
		csb->csb_locks_recv_count = 0;
		csb->csb_locks_recv_msgid = 0;
	}
}

static int rcom_response(gd_ls_t *ls)
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
 * The max value of "mylen" is dlm_config.buffer_size - sizeof(gd_rcom_t).  If
 * more data must be passed in one send, use rcom_expand_buffer() which
 * incrementally increases the size of the rc buffer by dlm_config.buffer_size
 * bytes.
 *
 * Any data returned for the message (when need_reply is set) will saved in
 * rc->rc_buf when this function returns and rc->rc_datalen will be set to the
 * number of bytes copied into rc->rc_buf.
 *
 * Returns: 0 on success, -EXXX on failure
 */

int rcom_send_message(gd_ls_t *ls, uint32_t nodeid, int type, gd_rcom_t *rc,
		      int need_reply)
{
	int error = 0;

	if (!rc->rc_datalen)
		rc->rc_datalen = 1;

	/* 
	 * Fill in the header.
	 */

	rc->rc_header.rh_cmd = GDLM_REMCMD_RECOVERMESSAGE;
	rc->rc_header.rh_lockspace = ls->ls_global_id;
	rc->rc_header.rh_length = sizeof(gd_rcom_t) + rc->rc_datalen - 1;
	rc->rc_subcmd = type;
	rc->rc_msgid = ++ls->ls_rcom_msgid;

	rcom_log(ls, nodeid, rc, 1);

	/* 
	 * When a reply is received, the reply data goes back into this buffer.
	 * Synchronous rcom requests (need_reply=1) are serialised because of
	 * the single ls_rcom.
	 */

	if (need_reply) {
		down(&ls->ls_rcom_lock);
		ls->ls_rcom = rc;
	}

	/* 
	 * After sending the message we'll wait at the end of this function to
	 * get a reply.  The READY flag will be set when the reply has been
	 * received and requested data has been copied into
	 * ls->ls_rcom->rc_buf;
	 */

	GDLM_ASSERT(!test_bit(LSFL_RECCOMM_READY, &ls->ls_flags),);

	/* 
	 * The WAIT bit indicates that we're waiting for and willing to accept a
	 * reply.  Any replies are ignored unless this bit is set.
	 */

	set_bit(LSFL_RECCOMM_WAIT, &ls->ls_flags);

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

	log_debug(ls, "rcom send %d to %u id %u", type, nodeid, rc->rc_msgid);

	error = midcomms_send_message(nodeid, (struct gd_req_header *) rc,
				      GFP_KERNEL);
	GDLM_ASSERT(error >= 0, printk("error = %d\n", error););
	error = 0;

	/* 
	 * Wait for a reply.  Once a reply is processed from midcomms, the
	 * READY bit will be set and we'll be awoken (gdlm_wait_function will
	 * return 0).
	 */

	if (need_reply) {
		error = gdlm_wait_function(ls, &rcom_response);
		if (error)
			log_debug(ls, "rcom wait error %d", error);
	}

      out:
	clear_bit(LSFL_RECCOMM_WAIT, &ls->ls_flags);
	clear_bit(LSFL_RECCOMM_READY, &ls->ls_flags);

	if (need_reply)
		up(&ls->ls_rcom_lock);

	return error;
}

/* 
 * Runs in same context as midcomms.
 */

static void rcom_process_message(gd_ls_t *ls, uint32_t nodeid, gd_rcom_t *rc)
{
	gd_rcom_t rc_stack;
	gd_rcom_t *reply = NULL;
	gd_resdata_t *rd;
	int status, datalen, maxlen;
	uint32_t be_nodeid;

	if (!ls)
		return;

	rcom_log(ls, nodeid, rc, 0);

	if (gdlm_recovery_stopped(ls) && (rc->rc_subcmd != RECCOMM_STATUS)) {
		log_error(ls, "ignoring recovery message %x from %u",
			  rc->rc_subcmd, nodeid);
		return;
	}

	switch (rc->rc_subcmd) {

	case RECCOMM_STATUS:

		memset(&rc_stack, 0, sizeof(gd_rcom_t));
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
			sizeof(gd_rcom_t) + reply->rc_datalen - 1;

		log_debug(ls, "rcom status %x to %u", reply->rc_buf[0], nodeid);
		break;

	case RECCOMM_RECOVERNAMES:

		reply = allocate_rcom_buffer(ls);
		GDLM_ASSERT(reply,);
		maxlen = dlm_config.buffer_size - sizeof(gd_rcom_t);

		reply->rc_header.rh_cmd = GDLM_REMCMD_RECOVERREPLY;
		reply->rc_header.rh_lockspace = rc->rc_header.rh_lockspace;
		reply->rc_subcmd = rc->rc_subcmd;
		reply->rc_msgid = rc->rc_msgid;

		/* 
		 * The other node wants a bunch of resource names.  The name of
		 * the resource to begin with is in rc->rc_buf.
		 */

		datalen = resdir_rebuild_send(ls, rc->rc_buf, rc->rc_datalen,
					      reply->rc_buf, maxlen, nodeid);

		reply->rc_datalen = datalen;
		reply->rc_header.rh_length =
		    sizeof(gd_rcom_t) + reply->rc_datalen - 1;

		log_debug(ls, "rcom names len %d to %u id %u", datalen, nodeid,
			  reply->rc_msgid);
		break;

	case RECCOMM_GETMASTER:

		reply = allocate_rcom_buffer(ls);
		GDLM_ASSERT(reply,);

		reply->rc_header.rh_cmd = GDLM_REMCMD_RECOVERREPLY;
		reply->rc_header.rh_lockspace = rc->rc_header.rh_lockspace;
		reply->rc_subcmd = rc->rc_subcmd;
		reply->rc_msgid = rc->rc_msgid;

		/* 
		 * The other node wants to know the master of a named resource.
		 */

		status = get_resdata(ls, nodeid, rc->rc_buf, rc->rc_datalen,
				     &rd, 1);
		if (status != 0) {
			free_rcom_buffer(reply);
			reply = NULL;
			return;
		}
		be_nodeid = cpu_to_be32(rd->rd_master_nodeid);
		memcpy(reply->rc_buf, &be_nodeid, sizeof(uint32_t));
		reply->rc_datalen = sizeof(uint32_t);
		reply->rc_header.rh_length =
		    sizeof(gd_rcom_t) + reply->rc_datalen - 1;
		break;

	case RECCOMM_BULKLOOKUP:

		reply = allocate_rcom_buffer(ls);
		GDLM_ASSERT(reply,);

		reply->rc_header.rh_cmd = GDLM_REMCMD_RECOVERREPLY;
		reply->rc_header.rh_lockspace = rc->rc_header.rh_lockspace;
		reply->rc_subcmd = rc->rc_subcmd;
		reply->rc_msgid = rc->rc_msgid;

		/* 
		 * This is a bulk version of the above and just returns a
		 * buffer full of node ids to match the resources
		 */

		datalen = bulk_master_lookup(ls, nodeid, rc->rc_buf,
				             rc->rc_datalen, reply->rc_buf);
		if (datalen < 0) {
			free_rcom_buffer(reply);
			reply = NULL;
			return;
		}

		reply->rc_datalen = datalen;
		reply->rc_header.rh_length =
		    sizeof(gd_rcom_t) + reply->rc_datalen - 1;
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
		remove_resdata(ls, nodeid, rc->rc_buf, rc->rc_datalen, 1);
		break;

	default:
		GDLM_ASSERT(0, printk("cmd=%x\n", rc->rc_subcmd););
	}

	if (reply) {
		if (nodeid == our_nodeid()) {
			GDLM_ASSERT(rc == ls->ls_rcom,);
			memcpy(rc->rc_buf, reply->rc_buf, reply->rc_datalen);
			rc->rc_datalen = reply->rc_datalen;
		} else {
			midcomms_send_message(nodeid,
					      (struct gd_req_header *) reply,
					      GFP_KERNEL);
		}

		if (reply != &rc_stack)
			free_rcom_buffer(reply);
	}
}

static void process_reply_sync(gd_ls_t *ls, uint32_t nodeid, gd_rcom_t *reply)
{
	gd_rcom_t *rc = ls->ls_rcom;

	if (!test_bit(LSFL_RECCOMM_WAIT, &ls->ls_flags)) {
		log_error(ls, "unexpected rcom reply nodeid=%u", nodeid);
		return;
	}

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

static void process_reply_async(gd_ls_t *ls, uint32_t nodeid, gd_rcom_t *reply)
{
	restbl_rsb_update_recv(ls, nodeid, reply->rc_buf, reply->rc_datalen,
			       reply->rc_msgid);
}

/* 
 * Runs in same context as midcomms.
 */

static void rcom_process_reply(gd_ls_t *ls, uint32_t nodeid, gd_rcom_t *reply)
{
	if (gdlm_recovery_stopped(ls)) {
		log_error(ls, "ignoring recovery reply %x from %u",
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


static int send_ls_not_ready(uint32_t nodeid, struct gd_req_header *header)
{
	struct writequeue_entry *wq;
	gd_rcom_t *rc = (gd_rcom_t *) header;
	gd_rcom_t *reply;

	wq = lowcomms_get_buffer(nodeid, sizeof(gd_rcom_t), GFP_KERNEL,
			         (char **)&reply);
	if (!wq)
		return -ENOMEM;

	reply->rc_header.rh_cmd = GDLM_REMCMD_RECOVERREPLY;
	reply->rc_header.rh_lockspace = rc->rc_header.rh_lockspace;
	reply->rc_subcmd = rc->rc_subcmd;
	reply->rc_msgid = rc->rc_msgid;
	reply->rc_buf[0] = 0;

	reply->rc_datalen = 1;
	reply->rc_header.rh_length = sizeof(gd_rcom_t) + reply->rc_datalen - 1;

	midcomms_send_buffer((struct gd_req_header *)reply, wq);
	return 0;
}


/* 
 * Runs in same context as midcomms.  Both recovery requests and recovery
 * replies come through this function.
 */

void process_recovery_comm(uint32_t nodeid, struct gd_req_header *header)
{
	gd_ls_t *ls = find_lockspace_by_global_id(header->rh_lockspace);
	gd_rcom_t *rc = (gd_rcom_t *) header;

	/* If the lockspace doesn't exist then still send a status message
	   back, it's possible that it just doesn't have it's global_id
  	   yet. */
	if (!ls) {
	      send_ls_not_ready(nodeid, header);
	      return;
	}

	switch (header->rh_cmd) {
	case GDLM_REMCMD_RECOVERMESSAGE:
		down_read(&ls->ls_rec_rsblist);
		rcom_process_message(ls, nodeid, rc);
		up_read(&ls->ls_rec_rsblist);
		break;

	case GDLM_REMCMD_RECOVERREPLY:
		rcom_process_reply(ls, nodeid, rc);
		break;

	default:
		GDLM_ASSERT(0, printk("cmd=%x\n", header->rh_cmd););
	}
}

