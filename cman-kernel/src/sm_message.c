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

#include "sm.h"

#define SMSG_BUF_SIZE (sizeof(sm_msg_t) + MAX_SERVICE_NAME_LEN + 1)

extern struct socket *	sm_socket;
extern uint32_t 	sm_our_nodeid;
static uint32_t 	global_last_id;
static struct list_head messages;
static spinlock_t 	message_lock;
static char		smsg_buf[SMSG_BUF_SIZE];

int send_nodeid_message(char *msg, int len, uint32_t nodeid);

struct rq_entry {
	struct list_head list;
	char *msg;
	int len;
	uint32_t nodeid;
};
typedef struct rq_entry rq_entry_t;

void init_messages(void)
{
	global_last_id = 1;
	INIT_LIST_HEAD(&messages);
	spin_lock_init(&message_lock);
}

uint32_t sm_new_global_id(int level)
{
	uint32_t id = global_last_id++;
	uint8_t l = (uint8_t) level;

	if (level > 255)
		return 0;

	if (id > 0x00FFFFFF)
		return 0;

	id |= (l << 24);
	return id;
}

static void smsg_copy_in(char *msg, sm_msg_t *smsg)
{
	sm_msg_t *in = (sm_msg_t *) msg;

	smsg->ms_type = in->ms_type;
	smsg->ms_status = in->ms_status;
	smsg->ms_sevent_id = le16_to_cpu(in->ms_sevent_id);
	smsg->ms_global_sgid = le32_to_cpu(in->ms_global_sgid);
	smsg->ms_global_lastid = le32_to_cpu(in->ms_global_lastid);
	smsg->ms_sglevel = le16_to_cpu(in->ms_sglevel);
	smsg->ms_length = le16_to_cpu(in->ms_length);
}

/* swapping bytes in place is an easy source of errors - be careful not to
 * access the fields after calling this */

void smsg_bswap_out(sm_msg_t *smsg)
{
	smsg->ms_sevent_id = cpu_to_le16(smsg->ms_sevent_id);
	smsg->ms_global_sgid = cpu_to_le32(smsg->ms_global_sgid);
	smsg->ms_global_lastid = cpu_to_le32(smsg->ms_global_lastid);
	smsg->ms_sglevel = cpu_to_le16(smsg->ms_sglevel);
	smsg->ms_length = cpu_to_le16(smsg->ms_length);
}

char *create_smsg(sm_group_t *sg, int type, int datalen, int *msglen,
		  sm_sevent_t *sev)
{
	char *msg;
	sm_msg_t *smsg;
	int fulllen = sizeof(sm_msg_t) + datalen;

	msg = smsg_buf;
	memset(smsg_buf, 0, SMSG_BUF_SIZE);
	SM_ASSERT(fulllen <= SMSG_BUF_SIZE,);

	smsg = (sm_msg_t *) msg;
	smsg->ms_type = type;
	smsg->ms_global_sgid = sg->global_id;
	smsg->ms_sglevel = sg->level;
	smsg->ms_length = datalen;
	smsg->ms_sevent_id = sev ? sev->se_id : 0;

	smsg_bswap_out(smsg);
	*msglen = fulllen;
	return msg;
}

static unsigned int msgtype_to_flag(int type)
{
	unsigned int flag;

	switch (type) {
	case SMSG_JOIN_REP:
	case SMSG_JOIN_REQ:
		flag = SEFL_ALLOW_JOIN;
		break;

	case SMSG_JSTOP_REP:
	case SMSG_JSTOP_REQ:
		flag = SEFL_ALLOW_JSTOP;
		break;

	case SMSG_LEAVE_REP:
	case SMSG_LEAVE_REQ:
		flag = SEFL_ALLOW_LEAVE;
		break;

	case SMSG_LSTOP_REP:
	case SMSG_LSTOP_REQ:
		flag = SEFL_ALLOW_LSTOP;
		break;

	default:
		SM_ASSERT(0, printk("msgtype_to_flag bad type %d\n", type););
	}
	return flag;
}

static int test_allowed_msgtype(sm_sevent_t * sev, int type)
{
	unsigned int flag = msgtype_to_flag(type);

	return test_bit(flag, &sev->se_flags);
}

static void clear_allowed_msgtype(sm_sevent_t * sev, int type)
{
	unsigned int flag = msgtype_to_flag(type);

	clear_bit(flag, &sev->se_flags);
}

static void set_allowed_msgtype(sm_sevent_t * sev, int type)
{
	unsigned int flag = msgtype_to_flag(type);

	set_bit(flag, &sev->se_flags);
}

static int save_global_id(sm_sevent_t * sev, sm_msg_t * smsg)
{
	sm_group_t *sg = sev->se_sg;

	if (!smsg->ms_global_sgid) {
		log_error(sg, "save_global_id: zero sg id");
		return -1;
	}

	if (!sg->global_id)
		sg->global_id = smsg->ms_global_sgid;

	if (sg->global_id != smsg->ms_global_sgid) {
		log_error(sg, "save_global_id: id %x", smsg->ms_global_sgid);
		return -1;
	}
	return 0;
}

static void save_lastid(sm_msg_t * smsg)
{
	uint32_t gid = smsg->ms_global_lastid & 0x00FFFFFF;

	/*
	 * Keep track of the highst SG id which has been used
	 * in the cluster in case we need to choose a new SG id.
	 */

	if (gid > global_last_id)
		global_last_id = gid;
}

static int next_sev_state(int msg_type, int cur_state)
{
	int next = 0;

	switch (msg_type) {
	case SMSG_JOIN_REP:
		SM_ASSERT(cur_state == SEST_JOIN_ACKWAIT,);
		next = SEST_JOIN_ACKED;
		break;

	case SMSG_JSTOP_REP:
		SM_ASSERT(cur_state == SEST_JSTOP_ACKWAIT,);
		next = SEST_JSTOP_ACKED;
		break;

	case SMSG_LEAVE_REP:
		SM_ASSERT(cur_state == SEST_LEAVE_ACKWAIT,);
		next = SEST_LEAVE_ACKED;
		break;

	case SMSG_LSTOP_REP:
		SM_ASSERT(cur_state == SEST_LSTOP_ACKWAIT,);
		next = SEST_LSTOP_ACKED;
		break;
	}
	return next;
}

/*
 * Functions in sevent.c send messages to other nodes and then expect replies.
 * This function collects the replies for the sevent messages and moves the
 * sevent to the next stage when all the expected replies have been received.
 */

static void process_reply(sm_msg_t * smsg, uint32_t nodeid)
{
	sm_sevent_t *sev;
	int i, expected, type = smsg->ms_type;

	/*
	 * Find the relevant sevent.
	 */

	sev = find_sevent(smsg->ms_sevent_id);
	if (!sev) {
		log_print("process_reply invalid id=%u nodeid=%u",
			  smsg->ms_sevent_id, nodeid);
		goto out;
	}

	/*
	 * Check if this message type is what this sevent is waiting for.
	 */

	if (!test_allowed_msgtype(sev, type)) {
		log_debug(sev->se_sg, "process_reply ignored type=%u nodeid=%u "			  "id=%u", type, nodeid, sev->se_id);
		goto out;
	}

	expected =
	    (type == SMSG_JOIN_REP) ? sev->se_node_count : sev->se_memb_count;

	SM_ASSERT(expected * sizeof(uint32_t) <= sev->se_len_ids,
		  printk("type=%d expected=%d len_ids=%d node_count=%d "
			 "memb_count=%d\n", type, expected, sev->se_len_ids,
			 sev->se_node_count, sev->se_memb_count););

	SM_ASSERT(expected * sizeof(char) <= sev->se_len_status,
		  printk("type=%d expected=%d len_status=%d node_count=%d "
			 "memb_count=%d\n", type, expected, sev->se_len_status,
			 sev->se_node_count, sev->se_memb_count););

	for (i = 0; i < expected; i++) {
		if (sev->se_node_ids[i] == nodeid) {
			/*
			 * Save the status from the replying node
			 */

			if (!sev->se_node_status[i])
				sev->se_node_status[i] = smsg->ms_status;
			else {
				log_error(sev->se_sg, "process_reply duplicate"
					  "id=%u nodeid=%u %u/%u",
					  sev->se_id, nodeid,
					  sev->se_node_status[i],
					  smsg->ms_status);
				goto out;
			}

			if (type == SMSG_JOIN_REP) {
				save_lastid(smsg);

				if (smsg->ms_status == STATUS_POS)
					save_global_id(sev, smsg);
			}

			/*
			 * Signal sm if we have all replies
			 */

			if (++sev->se_reply_count == expected) {
				clear_allowed_msgtype(sev, type);
				sev->se_state = next_sev_state(type,
						 	       sev->se_state);
				set_bit(SEFL_CHECK, &sev->se_flags);
				wake_serviced(DO_JOINLEAVE);
			}

			break;
		}
	}

      out:
	return;
}

/*
 * A node wants to join an SG and has run send_join_notice.  If we know nothing
 * about the SG , then we have no objection - send back STATUS_POS.  If we're a
 * member of the SG, then send back STATUS_POS (go ahead and join) if there's
 * no sevent or uevent of higher priority in progress (only a single join or
 * leave is permitted for the SG at once).  If there happens to be a higher
 * priority sevent/uevent in progress, send back STATUS_WAIT to defer the
 * requested join for a bit.
 */

static void process_join_request(sm_msg_t *smsg, uint32_t nodeid, char *name)
{
	sm_group_t *sg = NULL;
	sm_sevent_t *sev = NULL;
	sm_node_t *node;
	int found = FALSE;
	int level = smsg->ms_sglevel;
	sm_msg_t reply;

	memset(&reply, 0, sizeof(reply));

	down(&sm_sglock);

	if (nodeid == sm_our_nodeid)
		goto next;

	/*
	 * search SG list for an SG with given name/len
	 */

	list_for_each_entry(sg, &sm_sg[level], list) {
		if ((sg->namelen != smsg->ms_length) ||
		    memcmp(sg->name, name, sg->namelen))
			continue;
		found = TRUE;
		break;
	}

	/*
	 * build reply message
	 */

      next:

	if (!found) {
		reply.ms_type = SMSG_JOIN_REP;
		reply.ms_status = STATUS_NEG;
		reply.ms_global_lastid = global_last_id;
		reply.ms_sevent_id = smsg->ms_sevent_id;
	} else {
		reply.ms_type = SMSG_JOIN_REP;
		reply.ms_status = STATUS_POS;
		reply.ms_sevent_id = smsg->ms_sevent_id;
		reply.ms_global_sgid = sg->global_id;
		reply.ms_global_lastid = global_last_id;

		/*
		 * The node trying to join should wait and try again until
		 * we're done with recovery.
		 */

		if (sg->state == SGST_RECOVER) {
			reply.ms_status = STATUS_WAIT;
			goto send;
		}

		/*
		 * An sevent node trying to join may have gotten as far as
		 * creating a uevent with us and then backed out.  That node
		 * will retry joining from the beginning so we should not turn
		 * them away.  If we're handling a uevent for another node,
		 * tell the joining node to wait.
		 */

		if (test_bit(SGFL_UEVENT, &sg->flags)) {
			if (sg->uevent.ue_nodeid != nodeid)
				reply.ms_status = STATUS_WAIT;
			goto send;
		}

		/*
		 * We're trying to join or leave the SG at the moment.
		 */

		if (test_bit(SGFL_SEVENT, &sg->flags)) {
			sev = sg->sevent;

			/*
			 * We're trying to leave.  Make the join wait until
			 * we've left if we're beyond LEAVE_ACKWAIT.
			 */

			if (test_bit(SEFL_LEAVE, &sev->se_flags)) {
				if (sev->se_state > SEST_LEAVE_ACKED)
					reply.ms_status = STATUS_WAIT;
				else {
					reply.ms_status = STATUS_POS;
					clear_bit(SEFL_ALLOW_LEAVE,
						  &sev->se_flags);
					set_bit(SEFL_CANCEL, &sev->se_flags);
				}
			}

			/*
			 * We're trying to join.  Making the other join wait
			 * until we're joined if we're beyond JOIN_ACKWAIT or
			 * if we have a lower id.  (Send NEG to allow the other
			 * node to go ahead because we're not in the SG.)
			 */

			else {
				if (sev->se_state > SEST_JOIN_ACKED)
					reply.ms_status = STATUS_WAIT;
				else if (sm_our_nodeid < nodeid)
					reply.ms_status = STATUS_WAIT;
				else {
					reply.ms_status = STATUS_NEG;
					clear_bit(SEFL_ALLOW_JOIN,
						  &sev->se_flags);
					set_bit(SEFL_CANCEL, &sev->se_flags);
				}
			}

			if (test_bit(SEFL_CANCEL, &sev->se_flags)) {
				set_bit(SEFL_CHECK, &sev->se_flags);
				wake_serviced(DO_JOINLEAVE);
			}
			goto send;
		}

		/* no r,u,s event, stick with STATUS_POS */
	}

      send:

	if (reply.ms_status == STATUS_POS) {
		node = sm_find_joiner(sg, nodeid);
		if (!node) {
			node = sm_new_node(nodeid);
			list_add_tail(&node->list, &sg->joining);
		}
	}

	up(&sm_sglock);
	smsg_bswap_out(&reply);
	send_nodeid_message((char *) &reply, sizeof(reply), nodeid);
}

/*
 * Another node wants us to stop a service so it can join or leave the SG.  We
 * do this by saving the request info in a uevent and having the sm thread do
 * the processing and then replying.
 */

static void process_stop_request(sm_msg_t * smsg, uint32_t nodeid,
				 uint32_t * msgbuf)
{
	sm_group_t *sg;
	sm_uevent_t *uev;
	sm_msg_t reply;
	int type = smsg->ms_type;

	if (nodeid == sm_our_nodeid)
		goto agree;

	sg = sm_global_id_to_sg(smsg->ms_global_sgid);
	if (!sg) {
		log_print("process_stop_request: unknown sg id %x",
			  smsg->ms_global_sgid);
		return;
	}

	/*
	 * We shouldn't get here with uevent already set.
	 */

	if (test_and_set_bit(SGFL_UEVENT, &sg->flags)) {
		log_error(sg, "process_stop_request: uevent already set");
		return;
	}

	uev = &sg->uevent;
	uev->ue_nodeid = nodeid;
	uev->ue_remote_seid = smsg->ms_sevent_id;
	uev->ue_state = (type == SMSG_JSTOP_REQ) ? UEST_JSTOP : UEST_LSTOP;

	if (type == SMSG_JSTOP_REQ)
		uev->ue_num_nodes = be32_to_cpu(*msgbuf);
	else
		set_bit(UEFL_LEAVE, &uev->ue_flags);

	/*
	 * Do process_join_stop() or process_leave_stop().
	 */

	set_bit(UEFL_CHECK, &uev->ue_flags);
	wake_serviced(DO_MEMBERSHIP);
	return;

      agree:
	reply.ms_status = STATUS_POS;
	reply.ms_type =
	    (type == SMSG_JSTOP_REQ) ? SMSG_JSTOP_REP : SMSG_LSTOP_REP;
	reply.ms_sevent_id = smsg->ms_sevent_id;
	smsg_bswap_out(&reply);
	send_nodeid_message((char *) &reply, sizeof(reply), nodeid);
}

static void process_start_request(sm_msg_t * smsg, uint32_t nodeid)
{
	sm_group_t *sg;
	sm_uevent_t *uev;
	int type = smsg->ms_type;

	if (nodeid == sm_our_nodeid)
		return;

	sg = sm_global_id_to_sg(smsg->ms_global_sgid);
	if (!sg) {
		log_print("process_start_request: unknown sg id %x",
			  smsg->ms_global_sgid);
		return;
	}

	if (!test_bit(SGFL_UEVENT, &sg->flags)) {
		log_error(sg, "process_start_request: no uevent");
		return;
	}

	uev = &sg->uevent;

	if (type == SMSG_JSTART_CMD)
		uev->ue_state = UEST_JSTART;
	else
		uev->ue_state = UEST_LSTART;

	set_bit(UEFL_CHECK, &uev->ue_flags);
	wake_serviced(DO_MEMBERSHIP);
}

static void process_leave_request(sm_msg_t * smsg, uint32_t nodeid)
{
	sm_group_t *sg;
	sm_node_t *node;
	sm_msg_t reply;
	sm_sevent_t *sev;
	int found = FALSE;

	sg = sm_global_id_to_sg(smsg->ms_global_sgid);
	if (sg) {
		if (nodeid == sm_our_nodeid)
			found = TRUE;
		else {
			list_for_each_entry(node, &sg->memb, list) {
				if (node->id != nodeid)
					continue;
				set_bit(SNFL_LEAVING, &node->flags);
				found = TRUE;
				break;
			}
		}
	}

	if (!found) {
		reply.ms_type = SMSG_LEAVE_REP;
		reply.ms_status = STATUS_NEG;
		reply.ms_sevent_id = smsg->ms_sevent_id;
	} else {
		reply.ms_type = SMSG_LEAVE_REP;
		reply.ms_status = STATUS_POS;
		reply.ms_sevent_id = smsg->ms_sevent_id;

		if (sg->state == SGST_RECOVER)
			reply.ms_status = STATUS_WAIT;

		else if (test_bit(SGFL_SEVENT, &sg->flags) &&
			 nodeid != sm_our_nodeid) {
			sev = sg->sevent;

			/*
			 * We're trying to join or leave at the moment.  If
			 * we're past JOIN/LEAVE_ACKWAIT, we make the requestor
			 * wait.  Otherwise, if joining we'll cancel to let the
			 * leave happen first, or if we're leaving allow the
			 * lower nodeid to leave first.
			 */

			if (test_bit(SEFL_LEAVE, &sev->se_flags)) {
				if (sev->se_state > SEST_LEAVE_ACKWAIT)
					reply.ms_status = STATUS_WAIT;
				else if (sm_our_nodeid < nodeid)
					reply.ms_status = STATUS_WAIT;
				else {
					reply.ms_status = STATUS_POS;
					clear_bit(SEFL_ALLOW_LEAVE,
						  &sev->se_flags);
					set_bit(SEFL_CANCEL, &sev->se_flags);
				}
			} else {
				if (sev->se_state > SEST_JOIN_ACKWAIT)
					reply.ms_status = STATUS_WAIT;
				else {
					reply.ms_status = STATUS_NEG;
					clear_bit(SEFL_ALLOW_JOIN,
						  &sev->se_flags);
					set_bit(SEFL_CANCEL, &sev->se_flags);
				}
			}

			if (test_bit(SEFL_CANCEL, &sev->se_flags)) {
				set_bit(SEFL_CHECK, &sev->se_flags);
				wake_serviced(DO_JOINLEAVE);
			}
		}

		else if (test_bit(SGFL_UEVENT, &sg->flags)) {
			if (sg->uevent.ue_nodeid != nodeid)
				reply.ms_status = STATUS_WAIT;
		}

	}

	smsg_bswap_out(&reply);
	send_nodeid_message((char *) &reply, sizeof(reply), nodeid);
}

/*
 * Each remaining node will send us a done message.  We quit when we get the
 * first.  The subsequent done messages for the finished sevent get here and
 * are ignored.
 */

static void process_lstart_done(sm_msg_t *smsg, uint32_t nodeid)
{
	sm_sevent_t *sev;

	sev = find_sevent(smsg->ms_sevent_id);
	if (!sev)
		return;

	if (sev->se_state != SEST_LSTART_WAITREMOTE)
		return;

	sev->se_state = SEST_LSTART_REMOTEDONE;
	set_bit(SEFL_CHECK, &sev->se_flags);
	wake_serviced(DO_JOINLEAVE);
}

/*
 * This function and everything it calls always runs in sm context.
 */

static void process_message(char *msg, uint32_t nodeid)
{
	sm_msg_t smsg;

	smsg_copy_in(msg, &smsg);

	switch (smsg.ms_type) {
	case SMSG_JOIN_REQ:
		process_join_request(&smsg, nodeid, msg + sizeof(sm_msg_t));
		break;

	case SMSG_JSTOP_REQ:
		process_stop_request(&smsg, nodeid,
				     (uint32_t *) (msg + sizeof(sm_msg_t)));
		break;

	case SMSG_LEAVE_REQ:
		process_leave_request(&smsg, nodeid);
		break;

	case SMSG_LSTOP_REQ:
		process_stop_request(&smsg, nodeid, NULL);
		break;

	case SMSG_JSTART_CMD:
	case SMSG_LSTART_CMD:
		process_start_request(&smsg, nodeid);
		break;

	case SMSG_LSTART_DONE:
		process_lstart_done(&smsg, nodeid);
		break;

	case SMSG_JOIN_REP:
	case SMSG_JSTOP_REP:
	case SMSG_LEAVE_REP:
	case SMSG_LSTOP_REP:
		process_reply(&smsg, nodeid);
		break;

	case SMSG_RECOVER:
		process_recover_msg(&smsg, nodeid);
		break;

	default:
		log_print("process_message: unknown type %u nodeid %u",
			  smsg.ms_type, nodeid);
	}
}

/*
 * Always called from sm context.
 */

void process_messages(void)
{
	rq_entry_t *re;

	while (1) {
		re = NULL;

		spin_lock(&message_lock);
		if (!list_empty(&messages)) {
			re = list_entry(messages.next, rq_entry_t, list);
			list_del(&re->list);
		}
		spin_unlock(&message_lock);

		if (!re)
			break;
		process_message(re->msg, re->nodeid);
		kfree(re->msg);
		kfree(re);
		schedule();
	}
}

/*
 * Context: cnxman and sm
 */

static int add_to_recvqueue(char *msg, int len, uint32_t nodeid)
{
	rq_entry_t *re;

	SM_RETRY(re = (rq_entry_t *) kmalloc(sizeof(rq_entry_t), GFP_KERNEL),
		 re);
	SM_RETRY(re->msg = (char *) kmalloc(len, GFP_KERNEL), re->msg);

	memcpy(re->msg, msg, len);
	re->len = len;
	re->nodeid = nodeid;

	spin_lock(&message_lock);
	list_add_tail(&re->list, &messages);
	spin_unlock(&message_lock);

	wake_serviced(DO_MESSAGES);
	return 0;
}

/*
 * Context: cnxman
 * Called by cnxman when a service manager message arrives.
 */

int sm_cluster_message(char *msg, int len, char *addr, int addr_len,
		       unsigned int node_id)
{
        if (!node_id)
	        return -EINVAL;
        return add_to_recvqueue(msg, len, node_id);
}

/*
 * These send routines are used by sm and are always called from sm context.
 */

int send_nodeid_message(char *msg, int len, uint32_t nodeid)
{
	int error = 0;
	struct sockaddr_cl saddr;

	if (nodeid == sm_our_nodeid) {
		add_to_recvqueue(msg, len, nodeid);
		goto out;
	}

	saddr.scl_family = AF_CLUSTER;
	saddr.scl_port = CLUSTER_PORT_SERVICES;
	saddr.scl_nodeid = nodeid;
	error = kcl_sendmsg(sm_socket, msg, len, &saddr,
			    sizeof(saddr), 0);
	if (error > 0)
		error = 0;

	if (error)
		log_print("send_nodeid_message error %d to %u", error, nodeid);
      out:
	return error;
}

int send_broadcast_message(char *msg, int len)
{
	int error;

	error = kcl_sendmsg(sm_socket, msg, len, NULL, 0, 0);
	if (error > 0)
		error = 0;

	add_to_recvqueue(msg, len, sm_our_nodeid);

	if (error)
		log_print("send_broadcast_message error %d", error);

	return error;
}

int send_members_message(sm_group_t *sg, char *msg, int len)
{
	sm_node_t *node;
	int error = 0;

	list_for_each_entry(node, &sg->memb, list) {
		error = send_nodeid_message(msg, len, node->id);
		if (error < 0)
			break;
	}
	return error;
}

int send_members_message_sev(sm_group_t *sg, char *msg, int len,
			     sm_sevent_t * sev)
{
	int error;
	sm_msg_t *smsg = (sm_msg_t *) msg;

	set_allowed_msgtype(sev, smsg->ms_type);
	sev->se_reply_count = 0;

	error = send_members_message(sg, msg, len);
	if (error < 0)
		clear_allowed_msgtype(sev, smsg->ms_type);

	return error;
}

int send_broadcast_message_sev(char *msg, int len, sm_sevent_t * sev)
{
	int error;
	sm_msg_t *smsg = (sm_msg_t *) msg;

	set_allowed_msgtype(sev, smsg->ms_type);
	sev->se_reply_count = 0;

	error = send_broadcast_message(msg, len);
	if (error < 0)
		clear_allowed_msgtype(sev, smsg->ms_type);

	return error;
}
