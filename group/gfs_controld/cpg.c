/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2006-2007 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <openais/cpg.h>
#include "lock_dlm.h"

extern struct list_head mounts;
extern unsigned int     protocol_active[3];
static cpg_handle_t	daemon_handle;
static struct cpg_name	daemon_name;
int			message_flow_control_on;

void receive_journals(struct mountgroup *mg, char *buf, int len, int from);
void receive_options(struct mountgroup *mg, char *buf, int len, int from);
void receive_remount(struct mountgroup *mg, char *buf, int len, int from);
void receive_plock(struct mountgroup *mg, char *buf, int len, int from);
void receive_own(struct mountgroup *mg, char *buf, int len, int from);
void receive_drop(struct mountgroup *mg, char *buf, int len, int from);
void receive_sync(struct mountgroup *mg, char *buf, int len, int from);
void receive_withdraw(struct mountgroup *mg, char *buf, int len, int from);
void receive_mount_status(struct mountgroup *mg, char *buf, int len, int from);
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

	/* FIXME: we need to look at how to gracefully fail when we end up
	   with mixed incompat versions */

	if (hd->version[0] != protocol_active[0]) {
		log_error("reject message from %d version %u.%u.%u vs %u.%u.%u",
			  nodeid, hd->version[0], hd->version[1],
			  hd->version[2], protocol_active[0],
			  protocol_active[1], protocol_active[2]);
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

	case MSG_MOUNT_STATUS:
		receive_mount_status(mg, data, len, nodeid);
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

	case MSG_PLOCK_OWN:
		receive_own(mg, data, len, nodeid);
		break;

	case MSG_PLOCK_DROP:
		receive_drop(mg, data, len, nodeid);
		break;

	case MSG_PLOCK_SYNC_LOCK:
	case MSG_PLOCK_SYNC_WAITER:
		receive_sync(mg, data, len, nodeid);
		break;

	default:
		log_error("unknown message type %d from %d",
			  hd->type, hd->nodeid);
	}
}

void deliver_cb(cpg_handle_t handle, struct cpg_name *group_name,
		uint32_t nodeid, uint32_t pid, void *data, int data_len)
{
	do_deliver(nodeid, data, data_len);
}

/* Not sure if purging plocks (driven by confchg) needs to be synchronized with
   the other recovery steps (driven by libgroup) for a node, don't think so.
   Is it possible for a node to have been cleared from the members_gone list
   before this confchg is processed? */

void confchg_cb(cpg_handle_t handle, struct cpg_name *group_name,
		struct cpg_address *member_list, int member_list_entries,
		struct cpg_address *left_list, int left_list_entries,
		struct cpg_address *joined_list, int joined_list_entries)
{
	struct mountgroup *mg;
	int i, nodeid;

	for (i = 0; i < left_list_entries; i++) {
		nodeid = left_list[i].nodeid;
		list_for_each_entry(mg, &mounts, list) {
			if (is_member(mg, nodeid) || is_removed(mg, nodeid))
				purge_plocks(mg, left_list[i].nodeid, 0);
		}
	}
}

static cpg_callbacks_t callbacks = {
	.cpg_deliver_fn = deliver_cb,
	.cpg_confchg_fn = confchg_cb,
};

void update_flow_control_status(void)
{
	cpg_flow_control_state_t flow_control_state;
	cpg_error_t error;
	
	error = cpg_flow_control_state_get(daemon_handle, &flow_control_state);
	if (error != CPG_OK) {
		log_error("cpg_flow_control_state_get %d", error);
		return;
	}

	if (flow_control_state == CPG_FLOW_CONTROL_ENABLED) {
		if (message_flow_control_on == 0) {
			log_debug("flow control on");
		}
		message_flow_control_on = 1;
	} else {
		if (message_flow_control_on) {
			log_debug("flow control off");
		}
		message_flow_control_on = 0;
	}
}

int process_cpg(void)
{
	cpg_error_t error;

	error = cpg_dispatch(daemon_handle, CPG_DISPATCH_ALL);
	if (error != CPG_OK) {
		log_error("cpg_dispatch error %d", error);
		return -1;
	}

	update_flow_control_status();

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
			  error, (unsigned long long)h, msg_name(type));
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

	hd->version[0]	= cpu_to_le16(protocol_active[0]);
	hd->version[1]	= cpu_to_le16(protocol_active[1]);
	hd->version[2]	= cpu_to_le16(protocol_active[2]);
	hd->type	= cpu_to_le16(hd->type);
	hd->nodeid	= cpu_to_le32(hd->nodeid);
	hd->to_nodeid	= cpu_to_le32(hd->to_nodeid);
	memcpy(hd->name, mg->name, strlen(mg->name));
	
	return _send_message(daemon_handle, buf, len, type);
}

