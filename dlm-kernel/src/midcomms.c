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
 * midcomms.c
 *
 * This is the appallingly named "mid-level" comms layer.
 *
 * Its purpose is to take packets from the "real" comms layer,
 * split them up into packets and pass them to the interested
 * part of the locking mechanism.
 *
 * It also takes messages from the locking layer, formats them
 * into packets and sends them to the comms layer.
 *
 * It knows the format of the mid-level messages used and nodeidss
 * but it does not know how to resolve a nodeid into an IP address
 * or any of the comms channel details
 *
 */

#include "dlm_internal.h"
#include "lowcomms.h"
#include "midcomms.h"
#include "lockqueue.h"
#include "nodes.h"
#include "reccomms.h"
#include "config.h"

/* Byteorder routines */

static void host_to_network(void *msg)
{
	struct dlm_header *head = msg;
	struct dlm_request *req = msg;
	struct dlm_reply *rep = msg;
	struct dlm_query_request *qreq = msg;
	struct dlm_query_reply *qrep= msg;
	struct dlm_rcom *rc = msg;

	/* Force into network byte order */

	/*
	 * Do the common header first
	 */

	head->rh_length = cpu_to_le16(head->rh_length);
	head->rh_lockspace = cpu_to_le32(head->rh_lockspace);
	/* Leave the lkid alone as it is transparent at the remote end */

	/*
	 * Do the fields in the remlockrequest or remlockreply structs
	 */

	switch (req->rr_header.rh_cmd) {

	case GDLM_REMCMD_LOCKREQUEST:
	case GDLM_REMCMD_CONVREQUEST:
		req->rr_range_start = cpu_to_le64(req->rr_range_start);
		req->rr_range_end = cpu_to_le64(req->rr_range_end);
		/* Deliberate fall through */
	case GDLM_REMCMD_UNLOCKREQUEST:
	case GDLM_REMCMD_LOOKUP:
	case GDLM_REMCMD_LOCKGRANT:
	case GDLM_REMCMD_SENDBAST:
	case GDLM_REMCMD_SENDCAST:
	case GDLM_REMCMD_REM_RESDATA:
		req->rr_flags = cpu_to_le32(req->rr_flags);
		req->rr_status = cpu_to_le32(req->rr_status);
		break;

	case GDLM_REMCMD_LOCKREPLY:
		rep->rl_lockstate = cpu_to_le32(rep->rl_lockstate);
		rep->rl_nodeid = cpu_to_le32(rep->rl_nodeid);
		rep->rl_status = cpu_to_le32(rep->rl_status);
		break;

	case GDLM_REMCMD_RECOVERMESSAGE:
	case GDLM_REMCMD_RECOVERREPLY:
		rc->rc_msgid = cpu_to_le32(rc->rc_msgid);
		rc->rc_datalen = cpu_to_le16(rc->rc_datalen);
		break;

	case GDLM_REMCMD_QUERY:
	        qreq->rq_mstlkid = cpu_to_le32(qreq->rq_mstlkid);
		qreq->rq_query = cpu_to_le32(qreq->rq_query);
		qreq->rq_maxlocks = cpu_to_le32(qreq->rq_maxlocks);
		break;

	case GDLM_REMCMD_QUERYREPLY:
	        qrep->rq_numlocks = cpu_to_le32(qrep->rq_numlocks);
		qrep->rq_status = cpu_to_le32(qrep->rq_status);
		qrep->rq_grantcount = cpu_to_le32(qrep->rq_grantcount);
		qrep->rq_waitcount = cpu_to_le32(qrep->rq_waitcount);
		qrep->rq_convcount = cpu_to_le32(qrep->rq_convcount);
		break;

	default:
		printk("dlm: warning, unknown REMCMD type %u\n",
		       req->rr_header.rh_cmd);
	}
}

static void network_to_host(void *msg)
{
	struct dlm_header *head = msg;
	struct dlm_request *req = msg;
	struct dlm_reply *rep = msg;
	struct dlm_query_request *qreq = msg;
	struct dlm_query_reply *qrep = msg;
	struct dlm_rcom *rc = msg;

	/* Force into host byte order */

	/*
	 * Do the common header first
	 */

	head->rh_length = le16_to_cpu(head->rh_length);
	head->rh_lockspace = le32_to_cpu(head->rh_lockspace);
	/* Leave the lkid alone as it is transparent at the remote end */

	/*
	 * Do the fields in the remlockrequest or remlockreply structs
	 */

	switch (req->rr_header.rh_cmd) {

	case GDLM_REMCMD_LOCKREQUEST:
	case GDLM_REMCMD_CONVREQUEST:
		req->rr_range_start = le64_to_cpu(req->rr_range_start);
		req->rr_range_end = le64_to_cpu(req->rr_range_end);
	case GDLM_REMCMD_LOOKUP:
	case GDLM_REMCMD_UNLOCKREQUEST:
	case GDLM_REMCMD_LOCKGRANT:
	case GDLM_REMCMD_SENDBAST:
	case GDLM_REMCMD_SENDCAST:
	case GDLM_REMCMD_REM_RESDATA:
		/* Actually, not much to do here as the remote lock IDs are
		 * transparent too */
		req->rr_flags = le32_to_cpu(req->rr_flags);
		req->rr_status = le32_to_cpu(req->rr_status);
		break;

	case GDLM_REMCMD_LOCKREPLY:
		rep->rl_lockstate = le32_to_cpu(rep->rl_lockstate);
		rep->rl_nodeid = le32_to_cpu(rep->rl_nodeid);
		rep->rl_status = le32_to_cpu(rep->rl_status);
		break;

	case GDLM_REMCMD_RECOVERMESSAGE:
	case GDLM_REMCMD_RECOVERREPLY:
		rc->rc_msgid = le32_to_cpu(rc->rc_msgid);
		rc->rc_datalen = le16_to_cpu(rc->rc_datalen);
		break;


	case GDLM_REMCMD_QUERY:
	        qreq->rq_mstlkid = le32_to_cpu(qreq->rq_mstlkid);
		qreq->rq_query = le32_to_cpu(qreq->rq_query);
		qreq->rq_maxlocks = le32_to_cpu(qreq->rq_maxlocks);
		break;

	case GDLM_REMCMD_QUERYREPLY:
	        qrep->rq_numlocks = le32_to_cpu(qrep->rq_numlocks);
		qrep->rq_status = le32_to_cpu(qrep->rq_status);
		qrep->rq_grantcount = le32_to_cpu(qrep->rq_grantcount);
		qrep->rq_waitcount = le32_to_cpu(qrep->rq_waitcount);
		qrep->rq_convcount = le32_to_cpu(qrep->rq_convcount);
		break;

	default:
		printk("dlm: warning, unknown REMCMD type %u\n",
		       req->rr_header.rh_cmd);
	}
}

static void copy_from_cb(void *dst, const void *base, unsigned offset,
			 unsigned len, unsigned limit)
{
	unsigned copy = len;

	if ((copy + offset) > limit)
		copy = limit - offset;
	memcpy(dst, base + offset, copy);
	len -= copy;
	if (len)
		memcpy(dst + copy, base, len);
}

static void khexdump(const unsigned char *c, int len)
{
	while (len > 16) {
		printk(KERN_INFO
		       "%02x %02x %02x %02x %02x %02x %02x %02x-%02x %02x %02x %02x %02x %02x %02x %02x\n",
		       c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8],
		       c[9], c[10], c[11], c[12], c[13], c[14], c[15]);
		len -= 16;
		c += 16;
	}
	while (len > 4) {
		printk(KERN_INFO "%02x %02x %02x %02x\n", c[0], c[1], c[2],
		       c[3]);
		len -= 4;
		c += 4;
	}
	while (len > 0) {
		printk(KERN_INFO "%02x\n", c[0]);
		len--;
		c++;
	}
}

/*
 * Called from the low-level comms layer to process a buffer of
 * commands.
 *
 * Only complete messages are processed here, any "spare" bytes from
 * the end of a buffer are saved and tacked onto the front of the next
 * message that comes in. I doubt this will happen very often but we
 * need to be able to cope with it and I don't want the task to be waiting
 * for packets to come in when there is useful work to be done.
 *
 */
int midcomms_process_incoming_buffer(int nodeid, const void *base,
				     unsigned offset, unsigned len,
				     unsigned limit)
{
	unsigned char __tmp[sizeof(struct dlm_header) + 64];
	struct dlm_header *msg = (struct dlm_header *) __tmp;
	int ret = 0;
	int err = 0;
	unsigned msglen;
	__u32 id, space;

	while (len > sizeof(struct dlm_header)) {
		/* Get message header and check it over */
		copy_from_cb(msg, base, offset, sizeof(struct dlm_header),
			     limit);
		msglen = le16_to_cpu(msg->rh_length);
		id = msg->rh_lkid;
		space = msg->rh_lockspace;

		/* Check message size */
		err = -EINVAL;
		if (msglen < sizeof(struct dlm_header))
			break;
		err = -E2BIG;
		if (msglen > dlm_config.buffer_size) {
			printk("dlm: message size from %d too big %d(pkt len=%d)\n", nodeid, msglen, len);
			khexdump((const unsigned char *) msg, len);
			break;
		}
		err = 0;

		/* Not enough in buffer yet? wait for some more */
		if (msglen > len)
			break;

		/* Make sure our temp buffer is large enough */
		if (msglen > sizeof(__tmp) &&
		    msg == (struct dlm_header *) __tmp) {
			msg = kmalloc(dlm_config.buffer_size, GFP_KERNEL);
			if (msg == NULL)
				return ret;
		}

		copy_from_cb(msg, base, offset, msglen, limit);
		BUG_ON(id != msg->rh_lkid);
		BUG_ON(space != msg->rh_lockspace);
		ret += msglen;
		offset += msglen;
		offset &= (limit - 1);
		len -= msglen;
		network_to_host(msg);

		if ((msg->rh_cmd > 32) ||
		    (msg->rh_cmd == 0) ||
		    (msg->rh_length < sizeof(struct dlm_header)) ||
		    (msg->rh_length > dlm_config.buffer_size)) {

			printk("dlm: midcomms: cmd=%u, flags=%u, length=%hu, "
			       "lkid=%u, lockspace=%u\n",
			       msg->rh_cmd, msg->rh_flags, msg->rh_length,
			       msg->rh_lkid, msg->rh_lockspace);

			printk("dlm: midcomms: base=%p, offset=%u, len=%u, "
			       "ret=%u, limit=%08x newbuf=%d\n",
			       base, offset, len, ret, limit,
			       ((struct dlm_header *) __tmp == msg));

			khexdump((const unsigned char *) msg, msg->rh_length);

			return -EBADMSG;
		}

		switch (msg->rh_cmd) {
		case GDLM_REMCMD_RECOVERMESSAGE:
		case GDLM_REMCMD_RECOVERREPLY:
			process_recovery_comm(nodeid, msg);
			break;
		default:
			process_cluster_request(nodeid, msg, FALSE);
		}
	}

	if (msg != (struct dlm_header *) __tmp)
		kfree(msg);

	return err ? err : ret;
}

/*
 * Send a lowcomms buffer
 */

void midcomms_send_buffer(struct dlm_header *msg, struct writequeue_entry *e)
{
	host_to_network(msg);
	lowcomms_commit_buffer(e);
}

/*
 * Make the message into network byte order and send it
 */

int midcomms_send_message(uint32_t nodeid, struct dlm_header *msg,
			  int allocation)
{
	int len = msg->rh_length;

	host_to_network(msg);

	/*
	 * Loopback.  In fact, the locking code pretty much prevents this from
	 * being needed but it can happen when the directory node is also the
	 * local node.
	 */

	if (nodeid == our_nodeid())
		return midcomms_process_incoming_buffer(nodeid, (char *) msg, 0,
							len, len);

	return lowcomms_send_message(nodeid, (char *) msg, len, allocation);
}
