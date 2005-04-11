/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
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


static void set_rc_id(struct dlm_rsb *r, struct dlm_rcom *rc)
{
#if BITS_PER_LONG == 32
	rc->rc_id = (uint32_t) r;
#elif BITS_PER_LONG == 64
	rc->rc_id = (uint64_t) r;
#else
#error BITS_PER_LONG not defined
#endif
}

static int rcom_response(struct dlm_ls *ls)
{
	return test_bit(LSFL_RCOM_READY, &ls->ls_flags);
}

int create_rcom(struct dlm_ls *ls, int to_nodeid, int type, int len,
		struct dlm_rcom **rc_ret, struct dlm_mhandle **mh_ret)
{
	struct dlm_rcom *rc;
	struct dlm_mhandle *mh;
	char *mb;
	int mb_len = sizeof(struct dlm_rcom) + len;

	mh = lowcomms_get_buffer(to_nodeid, mb_len, GFP_KERNEL, &mb);
	if (!mh)
		return -ENOBUFS;
	memset(mb, 0, mb_len);

	rc = (struct dlm_rcom *) mb;

	rc->rc_header.h_version = (DLM_HEADER_MAJOR | DLM_HEADER_MINOR);
	rc->rc_header.h_lockspace = ls->ls_global_id;
	rc->rc_header.h_nodeid = dlm_our_nodeid();
	rc->rc_header.h_length = mb_len;
	rc->rc_header.h_cmd = DLM_RCOM;

	rc->rc_type = type;

	*mh_ret = mh;
	*rc_ret = rc;
	return 0;
}

int send_rcom(struct dlm_ls *ls, struct dlm_mhandle *mh, struct dlm_rcom *rc)
{
	struct dlm_header *hd = (struct dlm_header *) rc;

	/* FIXME: do byte swapping here */
	hd->h_length = cpu_to_le16(hd->h_length);

	log_print("send_rcom type %d result %d", rc->rc_type, rc->rc_result);

	lowcomms_commit_buffer(mh);
	return 0;
}

int make_status(struct dlm_ls *ls)
{
	int status = 0;

	if (test_bit(LSFL_DIR_VALID, &ls->ls_flags))
		status |= DIR_VALID;

	if (test_bit(LSFL_ALL_DIR_VALID, &ls->ls_flags))
		status |= DIR_ALL_VALID;

	if (test_bit(LSFL_NODES_VALID, &ls->ls_flags))
		status |= NODES_VALID;

	if (test_bit(LSFL_ALL_NODES_VALID, &ls->ls_flags))
		status |= NODES_ALL_VALID;

	return status;
}

int dlm_rcom_status(struct dlm_ls *ls, int nodeid)
{
	struct dlm_rcom *rc;
	struct dlm_mhandle *mh;
	int error;

	memset(ls->ls_rcom, 0, dlm_config.buffer_size);

	if (nodeid == dlm_our_nodeid()) {
		ls->ls_rcom->rc_result = make_status(ls);
		return 0;
	}

	error = create_rcom(ls, nodeid, DLM_RCOM_STATUS, 0, &rc, &mh);

	error = send_rcom(ls, mh, rc);

	error = dlm_wait_function(ls, &rcom_response);

	clear_bit(LSFL_RCOM_READY, &ls->ls_flags);

	return error;
}

void receive_rcom_status(struct dlm_ls *ls, struct dlm_rcom *rc_in)
{
	struct dlm_rcom *rc;
	struct dlm_mhandle *mh;
	int error, nodeid = rc_in->rc_header.h_nodeid;

	error = create_rcom(ls, nodeid, DLM_RCOM_STATUS_REPLY, 0, &rc, &mh);
	rc->rc_result = make_status(ls);

	error = send_rcom(ls, mh, rc);
}

void receive_rcom_status_reply(struct dlm_ls *ls, struct dlm_rcom *rc_in)
{
	memcpy(ls->ls_rcom, rc_in, rc_in->rc_header.h_length);
	set_bit(LSFL_RCOM_READY, &ls->ls_flags);
	wake_up(&ls->ls_wait_general);
}

int dlm_rcom_names(struct dlm_ls *ls, int nodeid, char *last_name, int last_len)
{
	struct dlm_rcom *rc;
	struct dlm_mhandle *mh;
	int error;

	memset(ls->ls_rcom, 0, dlm_config.buffer_size);

	if (nodeid == dlm_our_nodeid()) {
		int len = dlm_config.buffer_size - sizeof(struct dlm_rcom);
		dlm_copy_master_names(ls, last_name, last_len,
				      ls->ls_rcom->rc_buf, len, nodeid);
		return 0;
	}

	error = create_rcom(ls, nodeid, DLM_RCOM_NAMES, last_len, &rc, &mh);
	memcpy(rc->rc_buf, last_name, last_len);

	error = send_rcom(ls, mh, rc);

	error = dlm_wait_function(ls, &rcom_response);

	clear_bit(LSFL_RCOM_READY, &ls->ls_flags);

	return error;
}

void receive_rcom_names(struct dlm_ls *ls, struct dlm_rcom *rc_in)
{
	struct dlm_rcom *rc;
	struct dlm_mhandle *mh;
	int error, inlen, outlen;
	int nodeid = rc_in->rc_header.h_nodeid;

	/*
	 * We can't run dlm_dir_rebuild_send (which uses ls_nodes) while
	 * dlm_recoverd is running ls_nodes_reconfig (which changes ls_nodes).
	 * It could only happen in rare cases where we get a late NAMES 
	 * message from a previous instance of recovery.
	 */

	if (!test_bit(LSFL_NODES_VALID, &ls->ls_flags)) {
		log_debug(ls, "ignoring RCOM_NAMES from %u", nodeid);
		return;
	}
		
	nodeid = rc_in->rc_header.h_nodeid;
	inlen = rc_in->rc_header.h_length - sizeof(struct dlm_rcom);
	outlen = dlm_config.buffer_size - sizeof(struct dlm_rcom);

	error = create_rcom(ls, nodeid, DLM_RCOM_NAMES_REPLY, outlen, &rc, &mh);

	error = dlm_copy_master_names(ls, rc_in->rc_buf, inlen, rc->rc_buf,
				      outlen, nodeid);

	error = send_rcom(ls, mh, rc);
}

void receive_rcom_names_reply(struct dlm_ls *ls, struct dlm_rcom *rc_in)
{
	memcpy(ls->ls_rcom, rc_in, rc_in->rc_header.h_length);
	set_bit(LSFL_RCOM_READY, &ls->ls_flags);
	wake_up(&ls->ls_wait_general);
}

int dlm_send_rcom_lookup(struct dlm_rsb *r, int dir_nodeid)
{
	struct dlm_rcom *rc;
	struct dlm_mhandle *mh;
	struct dlm_ls *ls = r->res_ls;
	int error;

	error = create_rcom(ls, dir_nodeid, DLM_RCOM_LOOKUP, r->res_length,
			    &rc, &mh);
	memcpy(rc->rc_buf, r->res_name, r->res_length);
	set_rc_id(r, rc);

	error = send_rcom(ls, mh, rc);
	return 0;
}

void receive_rcom_lookup(struct dlm_ls *ls, struct dlm_rcom *rc_in)
{
	struct dlm_rcom *rc;
	struct dlm_mhandle *mh;
	int error, ret_nodeid, nodeid = rc_in->rc_header.h_nodeid;
	int len = rc_in->rc_header.h_length - sizeof(struct dlm_rcom);

	error = dlm_dir_lookup(ls, nodeid, rc_in->rc_buf, len, &ret_nodeid);

	error = create_rcom(ls, nodeid, DLM_RCOM_LOOKUP_REPLY, 0, &rc, &mh);
	rc->rc_result = ret_nodeid;
	rc->rc_id = rc_in->rc_id;

	error = send_rcom(ls, mh, rc);
}

void receive_rcom_lookup_reply(struct dlm_ls *ls, struct dlm_rcom *rc_in)
{
	dlm_recover_master_reply(ls, rc_in);
}

#if 0
struct rcom_lock {
	char			name[MAX_RESNAME_LEN];
	uint16_t		namelen;
};

void make_rcom_lock(struct dlm_rsb *r, struct dlm_lkb *lkb,
		    struct rcom_lock *rl)
{
}

int dlm_send_rcom_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	struct dlm_rcom *rc;
	struct dlm_mhandle *mh;
	struct dlm_ls *ls = r->res_ls;

	error = create_rcom(ls, r->res_nodeid, DLM_RCOM_LOCK,
			    sizeof(struct rcom_lock), &rc, &mh);

	rl = (struct rcom_lock *) rc->rc_buf;
	make_rcom_lock(r, lkb, rl);
	rc->rc_id = r;

	error = send_rcom(ls, mh, rc);
}
#endif

void receive_rcom_lock(struct dlm_ls *ls, struct dlm_rcom *rc_in)
{
#if 0
	struct dlm_rcom *rc;
	struct dlm_mhandle *mh;
	int error, nodeid = rc_in->rc_header.h_nodeid;

	rl = (struct rcom_lock *) rc_in->rc_buf;

	dlm_recover_lock(ls, nodeid, rl);

	error = create_rcom(ls, nodeid, DLM_RCOM_LOCK_REPLY,
			    sizeof(struct rcom_lock), &rc, &mh);

	memcpy(rc->rc_buf, rc_in->rc_buf, sizeof(struct rcom_lock));
	rc->rc_id = rc_in->rc_id;

	error = send_rcom(ls, mh, rc);
#endif
}

void receive_rcom_lock_reply(struct dlm_ls *ls, struct dlm_rcom *rc_in)
{
#if 0
	if (!test_bit(LSFL_DIR_VALID, &ls->ls_flags)) {
		log_debug(ls, "ignoring RCOM_LOCK_REPLY from %u", nodeid);
		return;
	}

	dlm_recover_lock_reply(ls, rc_in);
#endif
}

static int send_ls_not_ready(int nodeid, struct dlm_rcom *rc_in)
{
	struct dlm_rcom *rc;
	struct dlm_mhandle *mh;
	char *mb;
	int mb_len = sizeof(struct dlm_rcom);

	mh = lowcomms_get_buffer(nodeid, mb_len, GFP_KERNEL, &mb);
	if (!mh)
		return -ENOBUFS;
	memset(mb, 0, mb_len);

	rc = (struct dlm_rcom *) mb;

	rc->rc_header.h_version = (DLM_HEADER_MAJOR | DLM_HEADER_MINOR);
	rc->rc_header.h_lockspace = rc_in->rc_header.h_lockspace;
	rc->rc_header.h_nodeid = dlm_our_nodeid();
	rc->rc_header.h_length = mb_len;
	rc->rc_header.h_cmd = DLM_RCOM;

	rc->rc_type = DLM_RCOM_STATUS_REPLY;
	rc->rc_result = 0;

	/* FIXME: do byte swapping here */
	rc->rc_header.h_length = cpu_to_le16(rc->rc_header.h_length);

	lowcomms_commit_buffer(mh);

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
		send_ls_not_ready(nodeid, rc);
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

	log_print("recv_rcom type %d result %d from %d",
		  rc->rc_type, rc->rc_result, nodeid);

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

	case DLM_RCOM_LOCK:
		receive_rcom_lock(ls, rc);
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

	case DLM_RCOM_LOCK_REPLY:
		receive_rcom_lock_reply(ls, rc);
		break;

	default:
		DLM_ASSERT(0, printk("rc_type=%x\n", rc->rc_type););
	}

	put_lockspace(ls);
}

