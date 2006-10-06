/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2006 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <openais/cpg.h>
#include "lock_dlm.h"

static cpg_handle_t	daemon_handle;
static struct cpg_name	daemon_name;
static int		got_msg;
static int		saved_nodeid;
static int		saved_len;
static char		saved_data[MAX_MSGLEN];
int			message_flow_control_on;

void receive_journals(struct mountgroup *mg, char *buf, int len, int from);
void receive_options(struct mountgroup *mg, char *buf, int len, int from);
void receive_remount(struct mountgroup *mg, char *buf, int len, int from);
void receive_plock(struct mountgroup *mg, char *buf, int len, int from);
void receive_withdraw(struct mountgroup *mg, char *buf, int len, int from);
void receive_recovery_status(struct mountgroup *mg, char *buf, int len,
			     int from);
void receive_recovery_done(struct mountgroup *mg, char *buf, int len, int from);
char *msg_name(int type);

static void do_deliver(int nodeid, char *data, int len)
{
	struct mountgroup *mg;
	struct gdlm_header *hd;

	hd = (struct gdlm_header *) data;

	mg = find_mg(hd->name);
	if (!mg) {
		/*
		log_error("cpg message from %d len %d no group %s",
			  nodeid, len, hd->name);
		*/
		return;
	}

	hd->version[0]	= le16_to_cpu(hd->version[0]);
	hd->version[1]	= le16_to_cpu(hd->version[1]);
	hd->version[2]	= le16_to_cpu(hd->version[2]);
	hd->type	= le16_to_cpu(hd->type);
	hd->nodeid	= le32_to_cpu(hd->nodeid);
	hd->to_nodeid	= le32_to_cpu(hd->to_nodeid);

	if (hd->version[0] != GDLM_VER_MAJOR) {
		log_error("reject message version %u.%u.%u",
			  hd->version[0], hd->version[1], hd->version[2]);
		return;
	}

	/* If there are some group messages between a new node being added to
	   the cpg group and being added to the app group, the new node should
	   discard them since they're only relevant to the app group. */

	if (!mg->last_callback) {
		log_group(mg, "discard %s len %d from %d",
			  msg_name(hd->type), len, nodeid);
		return;
	}

	switch (hd->type) {
	case MSG_JOURNAL: 
		receive_journals(mg, data, len, nodeid);
		break;

	case MSG_OPTIONS:
		receive_options(mg, data, len, nodeid);
		break;

	case MSG_REMOUNT:
		receive_remount(mg, data, len, nodeid);
		break;

	case MSG_PLOCK:
		receive_plock(mg, data, len, nodeid);
		break;

	case MSG_RECOVERY_STATUS:
		receive_recovery_status(mg, data, len, nodeid);
		break;

	case MSG_RECOVERY_DONE:
		receive_recovery_done(mg, data, len, nodeid);
		break;

	case MSG_WITHDRAW:
		receive_withdraw(mg, data, len, nodeid);
		break;

	default:
		log_error("unknown message type %d from %d",
			  hd->type, hd->nodeid);
	}
}

void deliver_cb(cpg_handle_t handle, struct cpg_name *group_name,
		uint32_t nodeid, uint32_t pid, void *data, int data_len)
{
	saved_nodeid = nodeid;
	saved_len = data_len;
	memcpy(saved_data, data, data_len);
	got_msg = 1;
}

void confchg_cb(cpg_handle_t handle, struct cpg_name *group_name,
		struct cpg_address *member_list, int member_list_entries,
		struct cpg_address *left_list, int left_list_entries,
		struct cpg_address *joined_list, int joined_list_entries)
{
}

static cpg_callbacks_t callbacks = {
	.cpg_deliver_fn = deliver_cb,
	.cpg_confchg_fn = confchg_cb,
};

int process_cpg(void)
{
	cpg_flow_control_state_t flow_control_state;
	cpg_error_t error;
	
	got_msg = 0;
	saved_len = 0;
	saved_nodeid = 0;
	memset(saved_data, 0, sizeof(saved_data));

	error = cpg_dispatch(daemon_handle, CPG_DISPATCH_ONE);
	if (error != CPG_OK) {
		log_error("cpg_dispatch error %d", error);
		return -1;
	}

	if (got_msg)
		do_deliver(saved_nodeid, saved_data, saved_len);

	error = cpg_flow_control_state_get(daemon_handle, &flow_control_state);
	if (error != CPG_OK) {
		log_error("cpg_flow_control_state_get %d", error);
		return -1;
	}

	if (flow_control_state == CPG_FLOW_CONTROL_ENABLED) {
		message_flow_control_on = 1;
		log_debug("flow control on");
	} else {
		if (message_flow_control_on)
			log_debug("flow control off");
		message_flow_control_on = 0;
	}

	return 0;
}

int setup_cpg(void)
{
	cpg_error_t error;
	int fd = 0;

	error = cpg_initialize(&daemon_handle, &callbacks);
	if (error != CPG_OK) {
		log_error("cpg_initialize error %d", error);
		return -1;
	}

	cpg_fd_get(daemon_handle, &fd);
	if (fd < 0) {
		log_error("cpg_fd_get error %d", error);
		return -1;
	}

	memset(&daemon_name, 0, sizeof(daemon_name));
	strcpy(daemon_name.value, "gfs_controld");
	daemon_name.length = 12;

 retry:
	error = cpg_join(daemon_handle, &daemon_name);
	if (error == CPG_ERR_TRY_AGAIN) {
		log_debug("setup_cpg cpg_join retry");
		sleep(1);
		goto retry;
	}
	if (error != CPG_OK) {
		log_error("cpg_join error %d", error);
		cpg_finalize(daemon_handle);
		return -1;
	}

	log_debug("cpg %d", fd);
	return fd;
}

static int _send_message(cpg_handle_t h, void *buf, int len, int type)
{
	struct iovec iov;
	cpg_error_t error;
	int retries = 0;

	iov.iov_base = buf;
	iov.iov_len = len;

 retry:
	error = cpg_mcast_joined(h, CPG_TYPE_AGREED, &iov, 1);
	if (error == CPG_ERR_TRY_AGAIN) {
		retries++;
		usleep(1000);
		if (!(retries % 100))
			log_error("cpg_mcast_joined retry %d %s",
				   retries, msg_name(type));
		goto retry;
	}
	if (error != CPG_OK) {
		log_error("cpg_mcast_joined error %d handle %llx %s",
			  error, h, msg_name(type));
		return -1;
	}

	if (retries)
		log_debug("cpg_mcast_joined retried %d %s",
			  retries, msg_name(type));

	return 0;
}

int send_group_message(struct mountgroup *mg, int len, char *buf)
{
	struct gdlm_header *hd = (struct gdlm_header *) buf;
	int type = hd->type;

	hd->version[0]	= cpu_to_le16(GDLM_VER_MAJOR);
	hd->version[1]	= cpu_to_le16(GDLM_VER_MINOR);
	hd->version[2]	= cpu_to_le16(GDLM_VER_PATCH);
	hd->type	= cpu_to_le16(hd->type);
	hd->nodeid	= cpu_to_le32(hd->nodeid);
	hd->to_nodeid	= cpu_to_le32(hd->to_nodeid);
	memcpy(hd->name, mg->name, strlen(mg->name));
	
	return _send_message(daemon_handle, buf, len, type);
}

