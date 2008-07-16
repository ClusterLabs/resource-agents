/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2008 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "gfs_daemon.h"
#include "config.h"
#include "libdlmcontrol.h"

#define MAX_JOURNALS 256

uint32_t cpgname_to_crc(const char *data, int len);

static unsigned int protocol_active[3] = {1, 0, 0};
static int dlmcontrol_fd;

/* gfs_header types */
enum {
	GFS_MSG_START = 1,
	GFS_MSG_MOUNT_DONE = 2,
	GFS_MSG_FIRST_RECOVERY_DONE = 3,
	GFS_MSG_RECOVERY_RESULT = 4,
};

/* gfs_header flags */
#define GFS_MFLG_JOINING   1  /* accompanies start, we are joining */

struct gfs_header {
	uint16_t version[3];
	uint16_t type;          /* GFS_MSG_ */
	uint32_t nodeid;        /* sender */
	uint32_t to_nodeid;     /* recipient, 0 for all */
	uint32_t global_id;     /* global unique id for this lockspace */
	uint32_t flags;         /* GFS_MFLG_ */
	uint32_t msgdata;       /* in-header payload depends on MSG type */
	uint32_t pad1;
	uint64_t pad2;
};

/* mg_info and id_info: for syncing state in start message */

struct mg_info {
	uint32_t mg_info_size;
	uint32_t id_info_size;
	uint32_t id_info_count;

	uint32_t started_count;

	int member_count;
	int joined_count;
	int remove_count;
	int failed_count;

	int first_recovery_needed;
	int first_recovery_master;
};

#define IDI_NODEID_IS_MEMBER	0x00000001
#define IDI_JID_NEEDS_RECOVERY	0x00000002
#define IDI_MOUNT_DONE		0x00000008
#define IDI_MOUNT_ERROR		0x00000010
#define IDI_MOUNT_RO		0x00000020
#define IDI_MOUNT_SPECTATOR	0x00000040

struct id_info {
	int nodeid;
	int jid;
	uint32_t flags;
};

#define JID_NONE -1

struct journal {
	struct list_head list;
	int jid;
	int nodeid;
	int failed_nodeid;
	int needs_recovery;

	int local_recovery_busy;
	int local_recovery_done;
	int local_recovery_result;
	int failed_recovery_count;
};

struct node {
	struct list_head list;
	int nodeid;
	int jid;
	int ro;
	int spectator;
	int kernel_mount_done;
	int kernel_mount_error;

	int check_dlm;
	int dlm_notify_callback;
	int dlm_notify_result;

	int failed_reason;
	uint32_t added_seq;
	uint32_t removed_seq;
	uint64_t add_time;
};

struct member {
	struct list_head list;
	int nodeid;
	int start;   /* 1 if we received a start message for this change */
	int added;   /* 1 if added by this change */
	int failed;  /* 1 if failed in this change */
	int disallowed;
	char *start_msg; /* full copy of the start message from this node */
	struct mg_info *mg_info; /* shortcut into started_msg */
};

/* One of these change structs is created for every confchg a cpg gets. */

#define CGST_WAIT_CONDITIONS 1
#define CGST_WAIT_MESSAGES   2

struct change {
	struct list_head list;
	struct list_head members;
	struct list_head removed; /* nodes removed by this change */
	struct list_head saved_messages; /* saved messages */
	int member_count;
	int joined_count;
	int remove_count;
	int failed_count;
	int state;
	int we_joined;
	uint32_t seq; /* used as a reference for debugging, and for queries */
	uint32_t combined_seq; /* for queries */
};

struct save_msg {
	struct list_head list;
	int len;
	char buf[0];
};

/* 
   cpg confchg's arrive telling us that mountgroup members have
   joined/left/failed.  A "change" struct is created for each confchg,
   and added to the mg->changes list.

   apply_changes()
   ---------------

   <a new node won't know whether first_recovery_needed or not, but it also
    won't have any conditions to wait for, so a new node will go directly to
    sending out start message regardless>

   if first_recovery_needed,
   (or new, where new is not having completed a start barrier yet)
   all nodes: skip wait conditions
   all nodes: send start message

   else !first_recovery_needed,
   all nodes: if failures in changes, wait for conditions:
              local mount to complete if in progress, stop_kernel, dlm_notified
   all nodes: send start message

   <new changes that arrive result in going back to beginning; start messages
    from this aborted start cycle will be ignored>

   all nodes: wait for all start messages

   <once all start messages are received, new changes will be handled in a
    new batch after all current changes are cleared at end of sync_state>

   if start cycle / start barrier completes (start messages received from
   all nodes without being interrupted by a change), go on to sync_state
   which puts all members (as defined by the most recent change) in sync.

   "old nodes" are nodes that have completed a start cycle before (have
   a non-zero started_count), and "new nodes" are nodes that have not
   completed a start cycle before (they are being added by one of the
   changes in this start cycle)

   sync_state()
   ------------

   if old nodes have first_recovery_needed, or all nodes are new
   all nodes: mg->first_recovery_needed = 1
   all nodes: mg->first_recovery_master = prev or new low nodeid
   new nodes: instantiate existing state to match old nodes
   old nodes: update state per the changes in the completed start cycle
   all nodes: assign jids to new members
   all nodes: clear all change structs

   else !first_recovery_needed,
   new nodes: instantiate existing state to match old nodes
   old nodes: update state per the changes in the completed start cycle
   all nodes: assign jids to new members
   all nodes: clear all change structs

   <new changes that arrive from here on result in going back to the top>

   recover_and_start()
   ------------------- 

   if first_recovery_needed,
   master:    tells mount to run with first=1 (if not already)
   all nodes: wait for first_recovery_done message
   master:    sends first_recovery_done message when mount is done
   all nodes: mg->first_recovery_needed = 0
   all nodes: start kernel / tell mount.gfs to mount(2) (master already did)
   all nodes: send message with result of kernel mount

   else !first_recovery_needed,
   all nodes: if there are no journals to recover, goto start kernel
   old nodes: tell kernel to recover jids, send message with each result
   all nodes: wait for all recoveries to be done
   all nodes: start kernel
   new nodes: tell mount.gfs to mount(2)
   new nodes: send message with result of kernel mount

   [If no one can recover some journal(s), all will be left waiting, unstarted.
    A new change from a new mount will result in things going back to the top,
    and hopefully the new node will be successful at doing the journal
    recoveries when it comes through the recover_and_start() section, which
    would let everyone start again.]
*/

static void process_mountgroup(struct mountgroup *mg);

static char *msg_name(int type)
{
	switch (type) {
	case GFS_MSG_START:
		return "start";
	case GFS_MSG_MOUNT_DONE:
		return "mount_done";
	case GFS_MSG_FIRST_RECOVERY_DONE:
		return "first_recovery_done";
	case GFS_MSG_RECOVERY_RESULT:
		return "recovery_result";
	default:
		return "unknown";
	}
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

/* header fields caller needs to set: type, to_nodeid, flags, msgdata */

static void gfs_send_message(struct mountgroup *mg, char *buf, int len)
{
	struct gfs_header *hd = (struct gfs_header *) buf;
	int type = hd->type;

	hd->version[0]  = cpu_to_le16(protocol_active[0]);
	hd->version[1]  = cpu_to_le16(protocol_active[1]);
	hd->version[2]  = cpu_to_le16(protocol_active[2]);
	hd->type	= cpu_to_le16(hd->type);
	hd->nodeid      = cpu_to_le32(our_nodeid);
	hd->to_nodeid   = cpu_to_le32(hd->to_nodeid);
	hd->global_id   = cpu_to_le32(mg->id);
	hd->flags       = cpu_to_le32(hd->flags);
	hd->msgdata     = cpu_to_le32(hd->msgdata);

	_send_message(mg->cpg_handle, buf, len, type);
}

static struct member *find_memb(struct change *cg, int nodeid)
{
	struct member *memb;

	list_for_each_entry(memb, &cg->members, list) {
		if (memb->nodeid == nodeid)
			return memb;
	}
	return NULL;
}

static struct mountgroup *find_mg_handle(cpg_handle_t h)
{
	struct mountgroup *mg;

	list_for_each_entry(mg, &mountgroups, list) {
		if (mg->cpg_handle == h)
			return mg;
	}
	return NULL;
}

static struct mountgroup *find_mg_ci(int ci)
{
	struct mountgroup *mg;

	list_for_each_entry(mg, &mountgroups, list) {
		if (mg->cpg_client == ci)
			return mg;
	}
	return NULL;
}

static struct journal *find_journal(struct mountgroup *mg, int jid)
{
	struct journal *j;

	list_for_each_entry(j, &mg->journals, list) {
		if (j->jid == jid)
			return j;
	}
	return NULL;
}

static struct journal *find_journal_by_nodeid(struct mountgroup *mg, int nodeid)
{
	struct journal *j;

	list_for_each_entry(j, &mg->journals, list) {
		if (j->nodeid == nodeid)
			return j;
	}
	return NULL;
}

static void free_cg(struct change *cg)
{
	struct member *memb, *safe;
	struct save_msg *sm, *sm2;

	list_for_each_entry_safe(memb, safe, &cg->members, list) {
		list_del(&memb->list);
		if (memb->start_msg)
			free(memb->start_msg);
		free(memb);
	}
	list_for_each_entry_safe(memb, safe, &cg->removed, list) {
		list_del(&memb->list);
		if (memb->start_msg)
			free(memb->start_msg);
		free(memb);
	}
	list_for_each_entry_safe(sm, sm2, &cg->saved_messages, list) {
		list_del(&sm->list);
		free(sm);
	}
	free(cg);
}

static void free_mg(struct mountgroup *mg)
{
	struct change *cg, *cg_safe;
	struct node *node, *node_safe;

	list_for_each_entry_safe(cg, cg_safe, &mg->changes, list) {
		list_del(&cg->list);
		free_cg(cg);
	}

	if (mg->started_change)
		free_cg(mg->started_change);

	list_for_each_entry_safe(node, node_safe, &mg->node_history, list) {
		list_del(&node->list);
		free(node);
	}

	free(mg);
}

static struct node *get_node_history(struct mountgroup *mg, int nodeid)
{
	struct node *node;

	list_for_each_entry(node, &mg->node_history, list) {
		if (node->nodeid == nodeid)
			return node;
	}
	return NULL;
}

static void node_history_init(struct mountgroup *mg, int nodeid,
			      struct change *cg)
{
	struct node *node;

	node = get_node_history(mg, nodeid);
	if (node)
		goto out;

	node = malloc(sizeof(struct node));
	if (!node)
		return;
	memset(node, 0, sizeof(struct node));

	node->nodeid = nodeid;
	node->add_time = 0;
	list_add_tail(&node->list, &mg->node_history);
 out:
	node->added_seq = cg->seq;	/* for queries */
}

static void node_history_start(struct mountgroup *mg, int nodeid)
{
	struct node *node;
	
	node = get_node_history(mg, nodeid);
	if (!node) {
		log_error("node_history_start no nodeid %d", nodeid);
		return;
	}

	node->add_time = time(NULL);
}

static void node_history_left(struct mountgroup *mg, int nodeid,
			      struct change *cg)
{
	struct node *node;

	node = get_node_history(mg, nodeid);
	if (!node) {
		log_error("node_history_left no nodeid %d", nodeid);
		return;
	}

	node->add_time = 0;
	node->removed_seq = cg->seq;	/* for queries */
}

static void node_history_fail(struct mountgroup *mg, int nodeid,
			      struct change *cg, int reason)
{
	struct node *node;

	node = get_node_history(mg, nodeid);
	if (!node) {
		log_error("node_history_fail no nodeid %d", nodeid);
		return;
	}

	node->check_dlm = 1;

	node->removed_seq = cg->seq;	/* for queries */
	node->failed_reason = reason;	/* for queries */
}

static int is_added(struct mountgroup *mg, int nodeid)
{
	struct change *cg;
	struct member *memb;

	list_for_each_entry(cg, &mg->changes, list) {
		memb = find_memb(cg, nodeid);
		if (memb && memb->added)
			return 1;
	}
	return 0;
}

static int nodes_failed(struct mountgroup *mg)
{
	struct change *cg;

	list_for_each_entry(cg, &mg->changes, list) {
		if (cg->failed_count)
			return 1;
	}
	return 0;
}

/* find a start message from an old node to use; it doesn't matter which old
   node we take the start message from, they should all be the same */

static int get_id_list(struct mountgroup *mg, struct id_info **ids,
		       int *count, int *size)
{
	struct change *cg;
	struct member *memb;

	cg = list_first_entry(&mg->changes, struct change, list);

	list_for_each_entry(memb, &cg->members, list) {
		if (!memb->mg_info->started_count)
			continue;

		*count = memb->mg_info->id_info_count;
		*size = memb->mg_info->id_info_size;
		*ids = (struct id_info *)(memb->start_msg +
					  sizeof(struct gfs_header) +
					  memb->mg_info->mg_info_size);
		return 0;
	}
	return -1;
}

static struct id_info *get_id_struct(struct id_info *ids, int count, int size,
				     int nodeid)
{
	struct id_info *id = ids;
	int i;

	for (i = 0; i < count; i++) {
		if (id->nodeid == nodeid)
			return id;
		id = (struct id_info *)((char *)id + size);
	}
	return NULL;
}

static void start_kernel(struct mountgroup *mg)
{
	struct change *cg = mg->started_change;

	if (!mg->kernel_stopped) {
		log_error("start_kernel %u not stopped", cg->seq);
		return;
	}

	log_group(mg, "start_kernel %u member_count %d",
		  cg->seq, cg->member_count);

	set_sysfs(mg, "block", 0);
	mg->kernel_stopped = 0;

	if (mg->joining) {
		client_reply_join_full(mg, 0);
		mg->joining = 0;
		mg->mount_client_notified = 1;
	}
}

static void stop_kernel(struct mountgroup *mg)
{
	if (!mg->kernel_stopped) {
		log_group(mg, "stop_kernel");
		set_sysfs(mg, "block", 1);
		mg->kernel_stopped = 1;
	}
}

void process_dlmcontrol(int ci)
{
	struct mountgroup *mg;
	struct node *node;
	char name[GFS_MOUNTGROUP_LEN+1];
	int rv, type, nodeid, result;

	memset(name, 0, sizeof(name));

	rv = dlmc_fs_result(dlmcontrol_fd, name, &type, &nodeid, &result);
	if (rv) {
		log_error("process_dlmcontrol dlmc_fs_result %d", rv);
		return;
	}

	mg = find_mg(name);
	if (!mg) {
		log_error("process_dlmcontrol no mg %s", name);
		return;
	}

	if (type == DLMC_RESULT_NOTIFIED) {
		log_group(mg, "process_dlmcontrol notified nodeid %d result %d",
			  nodeid, result);

		node = get_node_history(mg, nodeid);
		if (!node) {
			/* shouldn't happen */
			log_error("process_dlmcontrol no nodeid %d", nodeid);
			return;
		}

		if (mg->dlm_notify_nodeid != nodeid) {
			/* shouldn't happen */
			log_error("process_dlmcontrol node %d expected %d",
				  nodeid, mg->dlm_notify_nodeid);
			return;
		}

		mg->dlm_notify_nodeid = 0;
		node->dlm_notify_callback = 1;
		node->dlm_notify_result = result;

	} else if (type == DLMC_RESULT_REGISTER) {
		log_group(mg, "process_dlmcontrol register nodeid %d result %d",
			  nodeid, result);
	} else {
		log_group(mg, "process_dlmcontrol unknown type %d", type);
	}

	poll_dlm = 0;
}

static int check_dlm_notify_done(struct mountgroup *mg)
{
	struct node *node;
	int rv;

	/* we're waiting for a notify result from the dlm (could we fire off
	   all dlmc_fs_notified() calls at once instead of serially?) */

	if (mg->dlm_notify_nodeid)
		return 0;

	list_for_each_entry(node, &mg->node_history, list) {

		/* check_dlm is set when we see a node fail, and is cleared
		   below when we find that the dlm has also seen it fail */

		if (!node->check_dlm)
			continue;

		/* we're in sync with the dlm for this nodeid, i.e. we've
		   both seen this node fail */

		if (node->dlm_notify_callback && !node->dlm_notify_result) {
			node->dlm_notify_callback = 0;
			node->check_dlm = 0;
			continue;
		}

		/* we're not in sync with the dlm for this nodeid, i.e.
		   the dlm hasn't seen this node fail yet; try calling
		   dlmc_fs_notified() again in a bit */

		if (node->dlm_notify_callback && node->dlm_notify_result) {
			log_group(mg, "check_dlm_notify will retry nodeid %d",
				  node->nodeid);
			node->dlm_notify_callback = 0;
			poll_dlm = 1;
			return 0;
		}

		/* check if the dlm has seen this nodeid fail, we get the
		   answer asynchronously in process_dlmcontrol */

		log_group(mg, "check_dlm_notify %d begin", node->nodeid);

		rv = dlmc_fs_notified(dlmcontrol_fd, mg->name, node->nodeid);
		if (rv) {
			log_error("dlmc_fs_notified error %d", rv);
			return 0;
		}

		mg->dlm_notify_nodeid = node->nodeid;
		return 0;
	}

	log_group(mg, "check_dlm_notify done");
	return 1;
}

static int wait_conditions_done(struct mountgroup *mg)
{
	if (mg->first_recovery_needed) {
		log_group(mg, "wait_conditions skip for first_recovery_needed");
		return 1;
	}

	if (!mg->started_count) {
		log_group(mg, "wait_conditions skip for zero started_count");
		return 1;
	}

	if (!nodes_failed(mg)) {
		log_group(mg, "wait_conditions skip for zero nodes_failed");
		return 1;
	}

	if (!mg->mount_client_notified) {
		log_group(mg, "wait_conditions skip mount client not notified");
		return 1;
	}

	if (mg->kernel_mount_done && mg->kernel_mount_error) {
		log_group(mg, "wait_conditions skip for kernel_mount_error");
		return 1;
	}

	if (!mg->kernel_mount_done) {
		log_group(mg, "wait_conditions need mount_done");
		return 0;
	}

	stop_kernel(mg);

	if (!check_dlm_notify_done(mg))
		return 0;

	return 1;
}

static int wait_messages_done(struct mountgroup *mg)
{
	struct change *cg = list_first_entry(&mg->changes, struct change, list);
	struct member *memb;
	int need = 0, total = 0;

	list_for_each_entry(memb, &cg->members, list) {
		if (!memb->start)
			need++;
		total++;
	}

	if (need) {
		log_group(mg, "wait_messages_done need %d of %d", need, total);
		return 0;
	}

	log_group(mg, "wait_messages_done got all %d", total);
	return 1;
}

static void cleanup_changes(struct mountgroup *mg)
{
	struct change *cg = list_first_entry(&mg->changes, struct change, list);
	struct change *safe;

	list_del(&cg->list);
	if (mg->started_change)
		free_cg(mg->started_change);
	mg->started_change = cg;

	/* zero started_count means "never started" */

	mg->started_count++;
	if (!mg->started_count)
		mg->started_count++;

	cg->combined_seq = cg->seq; /* for queries */

	list_for_each_entry_safe(cg, safe, &mg->changes, list) {
		mg->started_change->combined_seq = cg->seq; /* for queries */
		list_del(&cg->list);
		free_cg(cg);
	}
}

/* do the change details in the message match the details of the given change */

static int match_change(struct mountgroup *mg, struct change *cg,
			struct gfs_header *hd, int len, struct mg_info *mi)
{
	struct id_info *ids, *id;
	struct member *memb;
	uint32_t seq = hd->msgdata;
	int i, members_mismatch;

	ids = (struct id_info *)((char *)hd +
				 sizeof(struct gfs_header) +
				 mi->mg_info_size);

	/* We can ignore messages if we're not in the list of members.
	   The one known time this will happen is after we've joined
	   the cpg, we can get messages for changes prior to the change
	   in which we're added. */

	id = get_id_struct(ids, mi->id_info_count, mi->id_info_size, our_nodeid);

	if (!id || !(id->flags & IDI_NODEID_IS_MEMBER)) {
		log_group(mg, "match_change fail %d:%u we are not in members",
			  hd->nodeid, seq);
		return 0;
	}

	memb = find_memb(cg, hd->nodeid);
	if (!memb) {
		log_group(mg, "match_change fail %d:%u sender not member",
			  hd->nodeid, seq);
		return 0;
	}

	/* verify this is the right change by matching the counts
	   and the nodeids of the current members */

	if (mi->member_count != cg->member_count ||
	    mi->joined_count != cg->joined_count ||
	    mi->remove_count != cg->remove_count ||
	    mi->failed_count != cg->failed_count) {
		log_group(mg, "match_change fail %d:%u expect counts "
			  "%d %d %d %d", hd->nodeid, seq,
			  cg->member_count, cg->joined_count,
			  cg->remove_count, cg->failed_count);
		return 0;
	}

	members_mismatch = 0;
	id = ids;

	for (i = 0; i < mi->id_info_count; i++) {
		if (id->flags & IDI_NODEID_IS_MEMBER) {
			memb = find_memb(cg, id->nodeid);
			if (!memb) {
				log_group(mg, "match_change fail %d:%u memb %d",
					  hd->nodeid, seq, id->nodeid);
				members_mismatch = 1;
				break;
			}
		}
		id = (struct id_info *)((char *)id + mi->id_info_size);
	}

	if (members_mismatch)
		return 0;

	log_group(mg, "match_change done %d:%u", hd->nodeid, seq);
	return 1;
}

/* Unfortunately, there's no really simple way to match a message with the
   specific change that it was sent for.  We hope that by passing all the
   details of the change in the message, we will be able to uniquely match
   it to the correct change. */

/* A start message will usually be for the first (current) change on our list.
   In some cases it will be for a non-current change, and we can ignore it:

   1. A,B,C get confchg1 adding C
   2. C sends start for confchg1
   3. A,B,C get confchg2 adding D
   4. A,B,C,D recv start from C for confchg1 - ignored
   5. C,D send start for confchg2
   6. A,B send start for confchg2
   7. A,B,C,D recv all start messages for confchg2; start barrier/cycle done
 
   In step 4, how do the nodes know whether the start message from C is
   for confchg1 or confchg2?  Hopefully by comparing the counts and members. */

static struct change *find_change(struct mountgroup *mg, struct gfs_header *hd,
				  int len, struct mg_info *mi)
{
	struct change *cg;

	list_for_each_entry_reverse(cg, &mg->changes, list) {
		if (!match_change(mg, cg, hd, len, mi))
			continue;
		return cg;
	}

	log_group(mg, "find_change %d:%u no match", hd->nodeid, hd->msgdata);
	return NULL;
}

static void mg_info_in(struct mg_info *mi)
{
	mi->mg_info_size  = le32_to_cpu(mi->mg_info_size);
	mi->id_info_size  = le32_to_cpu(mi->id_info_size);
	mi->id_info_count = le32_to_cpu(mi->id_info_count);
	mi->started_count = le32_to_cpu(mi->started_count);
	mi->member_count  = le32_to_cpu(mi->member_count);
	mi->joined_count  = le32_to_cpu(mi->joined_count);
	mi->remove_count  = le32_to_cpu(mi->remove_count);
	mi->failed_count  = le32_to_cpu(mi->failed_count);
	mi->first_recovery_needed = le32_to_cpu(mi->first_recovery_needed);
	mi->first_recovery_master = le32_to_cpu(mi->first_recovery_master);
}

static void id_info_in(struct id_info *id)
{
	id->nodeid = le32_to_cpu(id->nodeid);
	id->jid    = le32_to_cpu(id->jid);
	id->flags  = le32_to_cpu(id->flags);
}

static void ids_in(struct mg_info *mi, struct id_info *ids)
{
	struct id_info *id;
	int i;

	id = ids;
	for (i = 0; i < mi->id_info_count; i++) {
		id_info_in(id);
		id = (struct id_info *)((char *)id + mi->id_info_size);
	}
}

static void receive_start(struct mountgroup *mg, struct gfs_header *hd, int len)
{
	struct change *cg;
	struct member *memb;
	struct mg_info *mi;
	struct id_info *ids;
	uint32_t seq = hd->msgdata;
	int added;

	log_group(mg, "receive_start %d:%u len %d", hd->nodeid, seq, len);

	/* header endian conv in deliver_cb, mg_info endian conv here,
	   id_info endian conv below */

	mi = (struct mg_info *)((char *)hd + sizeof(struct gfs_header));
	mg_info_in(mi);

	cg = find_change(mg, hd, len, mi);
	if (!cg)
		return;

	memb = find_memb(cg, hd->nodeid);
	if (!memb) {
		/* this should never happen since match_change checks it */
		log_error("receive_start no member %d", hd->nodeid);
		return;
	}

	added = is_added(mg, hd->nodeid);

	if (added && mi->started_count) {
		log_error("receive_start %d:%u add node with started_count %u",
			  hd->nodeid, seq, mi->started_count);

		/* observe this scheme working before using it; I'm not sure
		   that a joining node won't ever see an existing node as added
		   under normal circumstances */
		/*
		memb->disallowed = 1;
		return;
		*/
	}

	node_history_start(mg, hd->nodeid);
	memb->start = 1;

	if (memb->start_msg) {
		/* shouldn't happen */
		log_error("receive_start %d:%u dup start msg", hd->nodeid, seq);
		return;
	}

	/* save a copy of each start message */
	memb->start_msg = malloc(len);
	if (!memb->start_msg) {
		log_error("receive_start len %d no mem", len);
		return;
	}
	memcpy(memb->start_msg, hd, len);

	/* a shortcut to the saved mg_info */
	memb->mg_info = (struct mg_info *)(memb->start_msg +
					   sizeof(struct gfs_header));
	/* endian swap saved id_info entries */
	ids = (struct id_info *)(memb->start_msg +
				 sizeof(struct gfs_header) +
				 memb->mg_info->mg_info_size);
	ids_in(mi, ids);
}

/* start messages are associated with a specific change and use the
   find_change/match_change routines to make sure all start messages
   are matched with the same change on all nodes.  The current set of
   changes are cleared after a completed start cycle.  Other messages
   happen outside the context of changes.  An "incomplete" start cycle
   is when a confchg arrives (adding a new change struct) before all
   start messages have been received for the current change.  In this
   case, all members send a new start message for the latest change,
   and any start messages received for the previous change(s) are ignored.

   To sync state with start messages, we need to include:
   - the state before applying any of the current set of queued changes
     (new nodes will initialize with this)
   - the essential info from changes in the set that's being started,
     so nodes added by one of the queued changes can apply the same changes
     to the init state that the existing nodes do. */ 

/* recovery_result and mount_done messages may arrive between the time
   that an old node sends start and the time a new node receives it.
   two old nodes may also send start before/after a recovery_result or
   mount_done message, creating inconsistent data in their start messages.

   Soln: a new node saves recovery_result/mount_done messages between
   last confchg and final start.  the new node knows that a start message
   from an old node may or may not include the effects from rr/md messages
   since the last confchg, but *will* include all effects from prior to
   the last confchg.  The saved rr/md messages can be applied on top of
   the state from an old node's start message; applying them a second time
   should not change anything, producing the same result. */

static int count_ids(struct mountgroup *mg)
{
	struct change *cg;
	struct member *memb;
	struct journal *j;
	int count = 0;

	cg = list_first_entry(&mg->changes, struct change, list);

	list_for_each_entry(memb, &cg->members, list)
		count++;

	list_for_each_entry(j, &mg->journals, list)
		if (j->needs_recovery)
			count++;

	list_for_each_entry(cg, &mg->changes, list) {
		list_for_each_entry(memb, &cg->removed, list) {
			j = find_journal_by_nodeid(mg, memb->nodeid);
			if (j)
				count++;
		}
	}

	return count;
}

/* old member: current member that has completed a start cycle
   new member: current member that has not yet completed a start cycle */

static void send_start(struct mountgroup *mg)
{
	struct change *cg, *c;
	struct gfs_header *hd;
	struct mg_info *mi;
	struct id_info *id;
	struct member *memb;
	struct node *node;
	struct journal *j;
	char *buf;
	uint32_t flags;
	int len, id_count, jid;
	int old_memb = 0, new_memb = 0, old_journal = 0, new_journal = 0;

	cg = list_first_entry(&mg->changes, struct change, list);

	id_count = count_ids(mg);

	/* sanity check */

	if (!mg->started_count && id_count != cg->member_count) {
		log_error("send_start bad counts id_count %d member_count %d",
			  cg->member_count, id_count);
		return;
	}

	len = sizeof(struct gfs_header) + sizeof(struct mg_info) +
	      id_count * sizeof(struct id_info);

	buf = malloc(len);
	if (!buf) {
		log_error("send_start len %d no mem", len);
		return;
	}
	memset(buf, 0, len);

	hd = (struct gfs_header *)buf;
	mi = (struct mg_info *)(buf + sizeof(*hd));
	id = (struct id_info *)(buf + sizeof(*hd) + sizeof(*mi));

	/* fill in header (gfs_send_message handles part of header) */

	hd->type = GFS_MSG_START;
	hd->msgdata = cg->seq;
	hd->flags |= mg->joining ? GFS_MFLG_JOINING : 0;

	/* fill in mg_info */

	mi->mg_info_size  = cpu_to_le32(sizeof(struct mg_info));
	mi->id_info_size  = cpu_to_le32(sizeof(struct id_info));
	mi->id_info_count = cpu_to_le32(id_count);
	mi->started_count = cpu_to_le32(mg->started_count);
	mi->member_count  = cpu_to_le32(cg->member_count);
	mi->joined_count  = cpu_to_le32(cg->joined_count);
	mi->remove_count  = cpu_to_le32(cg->remove_count);
	mi->failed_count  = cpu_to_le32(cg->failed_count);
	mi->first_recovery_needed = cpu_to_le32(mg->first_recovery_needed);
	mi->first_recovery_master = cpu_to_le32(mg->first_recovery_master);

	/* fill in id_info entries */

	/* New members send info about themselves, and empty id_info slots for
	   all other members.  Old members send full info about all old
	   members, and empty id_info slots about new members.  The union of
	   start messages from a single old node and all new nodes give a
	   complete picture of state for all members.  In sync_state, all nodes
	   (old and new) make this union, and then assign jid's to new nodes. */

	list_for_each_entry(memb, &cg->members, list) {

		if (!mg->started_count || is_added(mg, memb->nodeid)) {
			/* send empty slot for new member */
			jid = JID_NONE;
			flags = IDI_NODEID_IS_MEMBER;

			/* include our own info which no one knows yet */
			if (!mg->started_count && memb->nodeid == our_nodeid) {
				flags |= mg->ro ? IDI_MOUNT_RO : 0;
				flags |= mg->spectator ? IDI_MOUNT_SPECTATOR : 0;
			}
			new_memb++;

		} else {
			/* send full info for old member */
			node = get_node_history(mg, memb->nodeid);
			if (!node) {
				log_error("send_start no node %d",memb->nodeid);
				continue;
			}

			jid = node->jid;
			flags = IDI_NODEID_IS_MEMBER;
			flags |= node->ro ? IDI_MOUNT_RO : 0;
			flags |= node->spectator ? IDI_MOUNT_SPECTATOR : 0;
			flags |= node->kernel_mount_done ? IDI_MOUNT_DONE : 0;
			flags |= node->kernel_mount_error ? IDI_MOUNT_ERROR : 0;
			old_memb++;
		}

		id->nodeid = cpu_to_le32(memb->nodeid);
		id->jid    = cpu_to_le32(jid);
		id->flags  = cpu_to_le32(flags);
		id++;
	}

	/* journals needing recovery from previous start cycles */

	list_for_each_entry(j, &mg->journals, list) {
		if (j->needs_recovery) {
			id->jid = cpu_to_le32(j->jid);
			id->flags = cpu_to_le32(IDI_JID_NEEDS_RECOVERY);
			id++;
			old_journal++;
		}
	}

	/* journals needing recovery from the current start cycle */

	list_for_each_entry(c, &mg->changes, list) {
		list_for_each_entry(memb, &c->removed, list) {
			j = find_journal_by_nodeid(mg, memb->nodeid);
			if (j) {
				id->jid = cpu_to_le32(j->jid);
				id->flags = cpu_to_le32(IDI_JID_NEEDS_RECOVERY);
				id++;
				new_journal++;
			}
		}
	}

	/* sanity check */

	if (!mg->started_count && (old_memb || old_journal || new_journal)) {
		log_error("send_start %u bad counts om %d nm %d oj %d nj %d",
			  cg->seq, old_memb, new_memb, old_journal, new_journal);
		return;
	}

	log_group(mg, "send_start %u id_count %d om %d nm %d oj %d nj %d",
		  cg->seq, id_count, old_memb, new_memb, old_journal,
		  new_journal);

	gfs_send_message(mg, buf, len);

	free(buf);
}

static void send_mount_done(struct mountgroup *mg, int result)
{
	struct gfs_header h;

	memset(&h, 0, sizeof(h));

	h.type = GFS_MSG_MOUNT_DONE;
	h.msgdata = result;

	gfs_send_message(mg, (char *)&h, sizeof(h));
}

static void send_first_recovery_done(struct mountgroup *mg)
{
	struct gfs_header h;

	memset(&h, 0, sizeof(h));

	h.type = GFS_MSG_FIRST_RECOVERY_DONE;

	gfs_send_message(mg, (char *)&h, sizeof(h));
}

static void send_recovery_result(struct mountgroup *mg, int jid, int result)
{
	struct gfs_header *hd;
	char *buf;
	int len, *p;

	len = sizeof(struct gfs_header) + 2 * sizeof(int);

	buf = malloc(len);
	if (!buf) {
		return;
	}
	memset(buf, 0, len);

	hd = (struct gfs_header *)buf;
	hd->type = GFS_MSG_RECOVERY_RESULT;

	p = (int *)(buf + sizeof(struct gfs_header));

	p[0] = cpu_to_le32(jid);
	p[1] = cpu_to_le32(result);

	gfs_send_message(mg, buf, len);

	free(buf);
}

static void save_message(struct mountgroup *mg, struct gfs_header *hd, int len)
{
	struct change *cg;
	struct save_msg *sm;

	cg = list_first_entry(&mg->changes, struct change, list);

	sm = malloc(sizeof(struct save_msg) + len);
	if (!sm) {
		log_error("save_message len %d no mem", len);
		return;
	}

	sm->len = len;
	memcpy(sm->buf, hd, len);

	list_add_tail(&sm->list, &cg->saved_messages);
}

/* We can't register with dlm_controld until dlm_controld knows about this
   lockspace.  We know that it will when the kernel mount completes.  */

void gfs_mount_done(struct mountgroup *mg)
{
	int rv;

	if (!mg->kernel_mount_error) {
		rv = dlmc_fs_register(dlmcontrol_fd, mg->name);
		if (rv)
			log_error("dlmc_fs_register %s error %d", mg->name, rv);
	}

	send_mount_done(mg, mg->kernel_mount_error);
}

static void receive_mount_done(struct mountgroup *mg, struct gfs_header *hd,
			       int len)
{
	struct node *node;

	log_group(mg, "receive_mount_done %d result %d",
		  hd->nodeid, hd->msgdata);

	node = get_node_history(mg, hd->nodeid);
	if (!node) {
		log_error("receive_mount_done no nodeid %d", hd->nodeid);
		return;
	}

	node->kernel_mount_done = 1;
	node->kernel_mount_error = hd->msgdata;
}

static void receive_recovery_result(struct mountgroup *mg,
				    struct gfs_header *hd, int len)
{
	struct journal *j;
	int jid, result, *p;

	p = (int *)((char *)hd + sizeof(struct gfs_header));
	jid = le32_to_cpu(p[0]);
	result = le32_to_cpu(p[1]);

	log_group(mg, "receive_recovery_result %d jid %d result %d",
		  hd->nodeid, jid, result);

	j = find_journal(mg, jid);
	if (!j) {
		log_error("receive_recovery_result %d no jid %d",
			  hd->nodeid, jid);
		return;
	}

	if (!j->needs_recovery)
		return;

	if (result == LM_RD_SUCCESS)
		j->needs_recovery = 0;
	else {
		j->failed_recovery_count++;
		log_group(mg, "jid %d failed_recovery_count %d", jid,
			  j->failed_recovery_count);
	}
}

static void receive_first_recovery_done(struct mountgroup *mg,
					struct gfs_header *hd, int len)
{
	int master = mg->first_recovery_master;

	log_group(mg, "receive_first_recovery_done %d master %d "
		  "mount_client_notified %d",
		  hd->nodeid, master, mg->mount_client_notified);

	if (master != hd->nodeid)
		log_error("receive_first_recovery_done %d master %d",
			  hd->nodeid, master);

	if (list_empty(&mg->changes)) {
		/* everything is idle, no changes in progress */

		mg->first_recovery_needed = 0;
		mg->first_recovery_master = 0;
		mg->first_recovery_msg = 1;

		if (master != our_nodeid)
			start_kernel(mg);
	} else {
		/* Everyone will receive this message in the same sequence
		   wrt other start messages and confchgs:

		   - If a new confchg arrives after this message (and before
		     the final start message in the current start cycle),
		     a new start cycle will begin.  All nodes before the
		     confchg will have frn=0 due to receiving this message,
		     and nodes added by the confchg will see frn=0 in all
		     start messages (in any_nodes_first_recovery() which
		     returns 0).

		   - If the final start message arrives after this message,
		     the start cycle will complete, running sync_state(), on
		     all current nodes with all having seen this message.
		     Old and new nodes in the current start cycle will see
		     this msg and use it (first_recovery_msg) instead of the
		     first_recovery_needed/master data in the start messages
		     (which may be inconsistent due to members sending their
		     start messages either before or after receiving this
		     message). */

		mg->first_recovery_needed = 0;
		mg->first_recovery_master = 0;
		mg->first_recovery_msg = 1;
	}
}

/* start message from all nodes shows zero started_count */

static int all_nodes_new(struct mountgroup *mg)
{
	struct change *cg;
	struct member *memb;

	cg = list_first_entry(&mg->changes, struct change, list);

	list_for_each_entry(memb, &cg->members, list) {
		if (memb->mg_info->started_count)
			return 0;
	}
	return 1;
}

/* does start message from any node with non-zero started_count have
   first_recovery_needed set?  (verify that all started nodes agree on
   first_recovery_needed) */

static int any_nodes_first_recovery(struct mountgroup *mg)
{
	struct change *cg;
	struct member *memb;
	int yes = 0, no = 0, master = 0;

	cg = list_first_entry(&mg->changes, struct change, list);

	list_for_each_entry(memb, &cg->members, list) {
		if (!memb->mg_info->started_count)
			continue;
		if (memb->mg_info->first_recovery_needed)
			yes++;
		else
			no++;
	}

	if (no && yes) {
		/* disagreement on first_recovery_needed, shouldn't happen */
		log_error("any_nodes_first_recovery no %d yes %d", no, yes);
		return 1;
	}

	if (no)
		return 0;

	/* sanity check: verify agreement on the master */

	list_for_each_entry(memb, &cg->members, list) {
		if (!memb->mg_info->started_count)
			continue;
		if (!master) {
			master = memb->mg_info->first_recovery_master;
			continue;
		}
		if (master == memb->mg_info->first_recovery_master)
			continue;

		/* disagreement on master, shouldn't happen */
		log_error("any_nodes_first_recovery master %d vs %d",
			  master, memb->mg_info->first_recovery_master);
	}

	return 1;
}

/* If all nodes new, there's no previous master, pick low nodeid;
   if not all nodes new, there will be a previous master, use that one unless
   it's no longer a member; if master is no longer a member pick low nodeid.
   The current master will already be set in mg->first_recovery_master for old
   nodes, but new nodes will need to look in the start messages to find it. */

static int pick_first_recovery_master(struct mountgroup *mg, int all_new)
{
	struct change *cg;
	struct member *memb;
	int old = 0, low = 0;

	cg = list_first_entry(&mg->changes, struct change, list);

	list_for_each_entry(memb, &cg->members, list) {
		if (memb->mg_info->started_count)
			old = memb->mg_info->first_recovery_master;

		if (!low)
			low = memb->nodeid;
		else if (memb->nodeid < low)
			low = memb->nodeid;
	}

	memb = find_memb(cg, old);

	if (!memb || all_new) {
		log_group(mg, "pick_first_recovery_master low %d old %d",
			  low, old);
		return low;
	}

	log_group(mg, "pick_first_recovery_master old %d", old);
	return old;
}

/* use a start message from an old node to create node info for each old node */

static void create_old_nodes(struct mountgroup *mg)
{
	struct change *cg;
	struct member *memb;
	struct node *node;
	struct journal *j;
	struct id_info *ids, *id;
	int id_count, id_size, rv;

	/* get ids from a start message of an old node */

	rv = get_id_list(mg, &ids, &id_count, &id_size);
	if (rv) {
		/* all new nodes, no old nodes */
		log_group(mg, "create_old_nodes all new");
		return;
	}

	/* use id list to set info for all old nodes */

	cg = list_first_entry(&mg->changes, struct change, list);

	list_for_each_entry(memb, &cg->members, list) {
		if (!memb->mg_info->started_count)
			continue;

		node = get_node_history(mg, memb->nodeid);
		id = get_id_struct(ids, id_count, id_size, memb->nodeid);

		if (!node || !id) {
			/* shouldn't happen */
			log_error("create_old_nodes %d node %d id %d",
				  memb->nodeid, !!node, !!id);
			return;
		}

		if (!(id->flags & IDI_NODEID_IS_MEMBER) ||
		     (id->flags & IDI_JID_NEEDS_RECOVERY)) {
			/* shouldn't happen */
			log_error("create_old_nodes %d bad flags %x",
				  memb->nodeid, id->flags);
			return;
		}

		node->jid                = id->jid;
		node->kernel_mount_done  = !!(id->flags & IDI_MOUNT_DONE);
		node->kernel_mount_error = !!(id->flags & IDI_MOUNT_ERROR);
		node->ro                 = !!(id->flags & IDI_MOUNT_RO);
		node->spectator          = !!(id->flags & IDI_MOUNT_SPECTATOR);

		j = malloc(sizeof(struct journal));
		if (!j) {
			log_error("create_old_nodes no mem");
			return;
		}
		memset(j, 0, sizeof(struct journal));

		j->nodeid = node->nodeid;
		j->jid = node->jid;
		list_add(&j->list, &mg->journals);

		log_group(mg, "create_old_nodes %d jid %d ro %d spect %d "
			  "kernel_mount_done %d error %d",
			  node->nodeid, node->jid, node->ro, node->spectator,
			  node->kernel_mount_done, node->kernel_mount_error);
	}
}

/* use start messages from new nodes to create node info for each new node */

static void create_new_nodes(struct mountgroup *mg)
{
	struct change *cg;
	struct member *memb;
	struct id_info *ids, *id;
	struct node *node;

	cg = list_first_entry(&mg->changes, struct change, list);

	list_for_each_entry(memb, &cg->members, list) {
		if (memb->mg_info->started_count)
			continue;

		node = get_node_history(mg, memb->nodeid);
		if (!node) {
			/* shouldn't happen */
			log_error("create_new_nodes %d no node", memb->nodeid);
			return;
		}

		ids = (struct id_info *)(memb->start_msg +
					 sizeof(struct gfs_header) +
					 memb->mg_info->mg_info_size);

		id = get_id_struct(ids, memb->mg_info->id_info_count,
				   memb->mg_info->id_info_size, memb->nodeid);

		if (!(id->flags & IDI_NODEID_IS_MEMBER) ||
		     (id->flags & IDI_JID_NEEDS_RECOVERY)) {
			/* shouldn't happen */
			log_error("create_new_nodes %d bad flags %x",
				  memb->nodeid, id->flags);
			return;
		}

		node->jid       = JID_NONE;
		node->ro        = !!(id->flags & IDI_MOUNT_RO);
		node->spectator = !!(id->flags & IDI_MOUNT_SPECTATOR);

		log_group(mg, "create_new_nodes %d ro %d spect %d",
			  node->nodeid, node->ro, node->spectator);
	}
}

static void create_failed_journals(struct mountgroup *mg)
{
	struct journal *j;
	struct id_info *ids, *id;
	int id_count, id_size;
	int rv, i;

	rv = get_id_list(mg, &ids, &id_count, &id_size);
	if (rv) {
		/* all new nodes, no old nodes */
		return;
	}

	id = ids;

	for (i = 0; i < id_count; i++) {
		if (!(id->flags & IDI_JID_NEEDS_RECOVERY))
			continue;

		j = malloc(sizeof(struct journal));
		if (!j) {
			log_error("create_failed_journals no mem");
			return;
		}
		memset(j, 0, sizeof(struct journal));

		j->jid = id->jid;
		j->needs_recovery = 1;
		list_add(&j->list, &mg->journals);

		id = (struct id_info *)((char *)id + id_size);

		log_group(mg, "create_failed_journals jid %d", j->jid);
	}
}

static void set_failed_journals(struct mountgroup *mg)
{
	struct change *cg;
	struct member *memb;
	struct journal *j;

	cg = list_first_entry(&mg->changes, struct change, list);

	list_for_each_entry(cg, &mg->changes, list) {
		list_for_each_entry(memb, &cg->removed, list) {
			if (!memb->failed)
				continue;

			j = find_journal_by_nodeid(mg, memb->nodeid);
			if (j) {
				j->needs_recovery = 1;
				j->failed_nodeid = j->nodeid;
				j->nodeid = 0;
				log_group(mg, "set_failed_journals jid %d "
					  "nodeid %d", j->jid, memb->nodeid);
			} else {
				log_group(mg, "set_failed_journals no journal "
					  "for nodeid %d ", memb->nodeid);
			}
		}
	}
}

/* returns nodeid of new member with the next highest nodeid */

static int next_new_nodeid(struct mountgroup *mg, int prev)
{
	struct change *cg;
	struct member *memb;
	int low = 0;

	cg = list_first_entry(&mg->changes, struct change, list);

	list_for_each_entry(memb, &cg->members, list) {
		if (memb->mg_info->started_count)
			continue;
		if (memb->nodeid <= prev)
			continue;
		if (!low)
			low = memb->nodeid;
		else if (memb->nodeid < low)
			low = memb->nodeid;
	}

	return low;
}

/* returns lowest unused jid */

static int next_free_jid(struct mountgroup *mg)
{
	int i;

	for (i = 0; i < MAX_JOURNALS; i++) {
		if (!find_journal(mg, i))
			return i;
	}
	return -1;
}

static void create_new_journals(struct mountgroup *mg)
{
	struct journal *j, *safe;
	struct change *cg;
	struct node *node;
	int nodeid = 0;

	cg = list_first_entry(&mg->changes, struct change, list);

	/* first get rid of journal structs that are no longer used
	   or dirty, i.e. from nodes that have unmounted/left, or
	   journals that have been recovered */

	list_for_each_entry_safe(j, safe, &mg->journals, list) {
		if (j->needs_recovery)
			continue;

		if (find_memb(cg, j->nodeid))
			continue;

		list_del(&j->list);
		free(j);
	}

	while (1) {
		nodeid = next_new_nodeid(mg, nodeid);
		if (!nodeid)
			break;

		node = get_node_history(mg, nodeid);
		if (!node) {
			/* shouldn't happen */
			log_error("create_new_journals no nodeid %d", nodeid);
			continue;
		}

		if (node->spectator)
			node->jid = JID_NONE;
		else
			node->jid = next_free_jid(mg);

		if (node->nodeid == our_nodeid)
			mg->our_jid = node->jid;

		log_group(mg, "create_new_journals %d gets jid %d",
			  node->nodeid, node->jid);

		if (node->jid == JID_NONE)
			continue;

		j = malloc(sizeof(struct journal));
		if (!j) {
			log_error("create_new_journals no mem");
			continue;
		}
		memset(j, 0, sizeof(struct journal));

		j->nodeid = nodeid;
		j->jid = node->jid;
		list_add(&j->list, &mg->journals);
	}
}

/* recovery_result and mount_done messages are saved by new members until
   they've completed the start cycle and have member state to apply them to.
   The start messages from old nodes may not reflect the rr/md updates. */

static void apply_saved_messages(struct mountgroup *mg)
{
	struct change *cg;
	struct save_msg *sm, *safe;
	struct gfs_header *hd;

	cg = list_first_entry(&mg->changes, struct change, list);

	list_for_each_entry_safe(sm, safe, &cg->saved_messages, list) {
		hd = (struct gfs_header *)sm->buf;

		switch (hd->type) {
		case GFS_MSG_MOUNT_DONE:
			receive_mount_done(mg, hd, sm->len);
			break;
		case GFS_MSG_RECOVERY_RESULT:
			receive_recovery_result(mg, hd, sm->len);
			break;
		}

		list_del(&sm->list);
		free(sm);
	}
}

/* this is run immediately after receiving the final start message in a start
   cycle, so all nodes will run this in the same sequence wrt other messages
   and confchgs */

static void sync_state(struct mountgroup *mg)
{
	/* This is needed for the case where the first_recovery_done message
	   arrives while a change/start cycle is in progress.  The
	   first_recovery data in the start messages (used by new nodes in this
	   cycle to determine the first_recovery state) may be inconsistent in
	   different start messages (because nodes sent their start messages at
	   different times wrt the first_recovery_done message.)  But, in the
	   case where the new nodes received the first_recovery_done message,
	   they can just use that and don't need the (possibly inconsistent)
	   first recovery data in the start messages. */

	if (mg->first_recovery_msg) {
		if (mg->first_recovery_needed || mg->first_recovery_master) {
			/* shouldn't happen */
			log_error("sync_state first_recovery_msg needed %d "
				  "master %d", mg->first_recovery_needed,
				  mg->first_recovery_master);
		}

		log_group(mg, "sync_state first_recovery_msg");
		goto out;
	}

	/* This is the path the initial start cycle for the group always
	   follows.  It's the case where one or more nodes are all starting up
	   for the first time.  No one has completed a start cycle yet because
	   everyone is joining, and one node needs to do first recovery. */

	if (all_nodes_new(mg)) {
		if (mg->first_recovery_needed || mg->first_recovery_master) {
			/* shouldn't happen */
			log_error("sync_state all_nodes_new first_recovery "
				  "needed %d master %d",
				  mg->first_recovery_needed,
				  mg->first_recovery_master);
		}
		mg->first_recovery_needed = 1;
		mg->first_recovery_master = pick_first_recovery_master(mg, 1);

		log_group(mg, "sync_state all_nodes_new first_recovery_needed "
			  "master %d", mg->first_recovery_master);
		goto out;
	}

	/* This is for the case where new nodes are added to existing members
	   that have first_recovery_needed set. */

	if (any_nodes_first_recovery(mg)) {
		mg->first_recovery_needed = 1;
		mg->first_recovery_master = pick_first_recovery_master(mg, 0);

		log_group(mg, "sync_state first_recovery_needed master %d",
			  mg->first_recovery_master);
		goto out;
	}

	/* Normal case where nodes join an established group that completed
	   first recovery sometime in the past.  Existing nodes that weren't
	   around during first recovery come through here, and new nodes
           being added in this cycle come through here. */

	if (mg->first_recovery_needed) {
		/* shouldn't happen */
		log_error("sync_state frn should not be set");
	}

 out:
	if (!mg->started_count) {
		create_old_nodes(mg);
		create_new_nodes(mg);
		create_failed_journals(mg);
		apply_saved_messages(mg);
		create_new_journals(mg);
	} else {
		create_new_nodes(mg);
		set_failed_journals(mg);
		create_new_journals(mg);
	}
}

static void apply_changes(struct mountgroup *mg)
{
	struct change *cg;

	cg = list_first_entry(&mg->changes, struct change, list);

	switch (cg->state) {

	case CGST_WAIT_CONDITIONS:
		if (wait_conditions_done(mg)) {
			send_start(mg);
			cg->state = CGST_WAIT_MESSAGES;
		}
		break;

	case CGST_WAIT_MESSAGES:
		if (wait_messages_done(mg)) {
			sync_state(mg);
			cleanup_changes(mg);
		}
		break;

	default:
		log_error("apply_changes invalid state %d", cg->state);
	}
}

/* We send messages with the info from kernel uevents or mount.gfs ipc,
   and then process the uevent/ipc upon receiving the message for it, so
   that it can be processed in the same order by all nodes. */

void process_recovery_uevent(char *table)
{
	struct mountgroup *mg;
	struct journal *j;
	char *name = strstr(table, ":") + 1;
	int jid, recover_status, first_done;
	int rv;

	mg = find_mg(name);
	if (!mg) {
		log_error("process_recovery_uevent mg not found %s", table);
		return;
	}

	rv = read_sysfs_int(mg, "recover_done", &jid);
	if (rv < 0) {
		log_error("process_recovery_uevent recover_done read %d", rv);
		return;
	}

	rv = read_sysfs_int(mg, "recover_status", &recover_status);
	if (rv < 0) {
		log_error("process_recovery_uevent recover_status read %d", rv);
		return;
	}

	if (!mg->first_recovery_needed) {
		if (!mg->local_recovery_busy) {
			/* This will happen in two known situations:
			   - we get a recovery_done uevent for our own journal
			     when we mount  (jid == mg->our_jid)
			   - the first mounter will read first_done and clear
			     first_recovery_needed before seeing the change
			     uevent from others_may_mount */
			log_group(mg, "process_recovery_uevent jid %d ignore",
				  jid);
			return;
		}

		mg->local_recovery_busy = 0;

		if (mg->local_recovery_jid != jid) {
			log_error("process_recovery_uevent jid %d exp %d",
				  jid, mg->local_recovery_jid);
			return;
		}

		j = find_journal(mg, jid);
		if (!j) {
			log_error("process_recovery_uevent no journal %d", jid);
			return;
		}

		log_group(mg, "process_recovery_uevent jid %d status %d",
			  jid, recover_status);

		j->local_recovery_done = 1;
		j->local_recovery_result = recover_status;

		/* j->needs_recovery will be cleared when we receive this
		   recovery_result message */

		send_recovery_result(mg, jid, recover_status);
	} else {

		/*
		 * Assumption here is that only the first mounter will get
		 * uevents when first_recovery_needed is set.
		 */

		/* make a local record of jid and recover_status; we may want
		   to check below that we've seen uevents for all jids
		   during first recovery before sending first_recovery_done. */

		log_group(mg, "process_recovery_uevent jid %d status %d "
			  "ignore during first_recovery", jid, recover_status);

		rv = read_sysfs_int(mg, "first_done", &first_done);
		if (rv < 0) {
			log_error("process_recovery_uevent first_done read %d",
				  rv);
			return;
		}

		if (first_done) {
			log_group(mg, "process_recovery_uevent first_done");
			send_first_recovery_done(mg);
		}
	}

	process_mountgroup(mg);
}

static void start_journal_recovery(struct mountgroup *mg, int jid)
{
	int rv;

	log_group(mg, "start_journal_recovery jid %d", jid);

	rv = set_sysfs(mg, "recover", jid);
	if (rv < 0) {
		log_error("start_journal_recovery %d error %d", jid, rv);
		return;
	}

	mg->local_recovery_busy = 1;
	mg->local_recovery_jid = jid;
}

static int wait_recoveries_done(struct mountgroup *mg)
{
	struct journal *j;
	int wait_count = 0;

	list_for_each_entry(j, &mg->journals, list) {
		if (j->needs_recovery) {
			log_group(mg, "wait_recoveries jid %d unrecovered",
				  j->jid);
			wait_count++;
		}
	}

	if (wait_count)
		return 0;

	log_group(mg, "wait_recoveries done");
	return 1;
}

/* pick a jid that has not been successfully recovered by someone else
   (received recovery_result success message) and hasn't been recovered
   by us (local record); if nothing to recover, return 0 */

static int pick_journal_to_recover(struct mountgroup *mg, int *jid)
{
	struct journal *j;

	list_for_each_entry(j, &mg->journals, list) {
		if (j->needs_recovery && !j->local_recovery_done) {
			*jid = j->jid;
			return 1;
		}
	}
	return 0;
}

/* processing that happens after all changes have been dealt with */

static void recover_and_start(struct mountgroup *mg)
{
	int jid;

	if (mg->first_recovery_needed) {
		if (mg->first_recovery_master == our_nodeid &&
		    !mg->mount_client_notified) {
			log_group(mg, "recover_and_start first start_kernel");
			mg->first_mounter = 1; /* adds first=1 to hostdata */
			start_kernel(mg);      /* includes reply to mount.gfs */
		}
		return;
	}

	/* The normal non-first-recovery mode.  When a recovery_done message
	   is received, check whether any more journals need recovery.  If
	   so, start recovery on the next one, if not, start the kernel. */

	if (!wait_recoveries_done(mg)) {
		if (!mg->kernel_mount_done || mg->kernel_mount_error)
			return;
		if (mg->spectator)
			return;
		if (mg->local_recovery_busy)
			return;
		if (pick_journal_to_recover(mg, &jid))
			start_journal_recovery(mg, jid);
	} else {
		if (!mg->kernel_stopped)
			return;
		log_group(mg, "recover_and_start start_kernel");
		start_kernel(mg);
	}
}

static void process_mountgroup(struct mountgroup *mg)
{
	if (!list_empty(&mg->changes))
		apply_changes(mg);
	
	if (list_empty(&mg->changes))
		recover_and_start(mg);
}

void process_mountgroups(void)
{
	struct mountgroup *mg, *safe;

	list_for_each_entry_safe(mg, safe, &mountgroups, list)
		process_mountgroup(mg);
}

static int add_change(struct mountgroup *mg,
		      struct cpg_address *member_list, int member_list_entries,
		      struct cpg_address *left_list, int left_list_entries,
		      struct cpg_address *joined_list, int joined_list_entries,
		      struct change **cg_out)
{
	struct change *cg;
	struct member *memb;
	int i, error;

	cg = malloc(sizeof(struct change));
	if (!cg)
		goto fail_nomem;
	memset(cg, 0, sizeof(struct change));
	INIT_LIST_HEAD(&cg->members);
	INIT_LIST_HEAD(&cg->removed);
	INIT_LIST_HEAD(&cg->saved_messages);
	cg->state = CGST_WAIT_CONDITIONS;
	cg->seq = ++mg->change_seq;
	if (!cg->seq)
		cg->seq = ++mg->change_seq;

	cg->member_count = member_list_entries;
	cg->joined_count = joined_list_entries;
	cg->remove_count = left_list_entries;

	for (i = 0; i < member_list_entries; i++) {
		memb = malloc(sizeof(struct member));
		if (!memb)
			goto fail_nomem;
		memset(memb, 0, sizeof(struct member));
		memb->nodeid = member_list[i].nodeid;
		list_add_tail(&memb->list, &cg->members);
	}

	for (i = 0; i < left_list_entries; i++) {
		memb = malloc(sizeof(struct member));
		if (!memb)
			goto fail_nomem;
		memset(memb, 0, sizeof(struct member));
		memb->nodeid = left_list[i].nodeid;
		if (left_list[i].reason == CPG_REASON_NODEDOWN ||
		    left_list[i].reason == CPG_REASON_PROCDOWN) {
			memb->failed = 1;
			cg->failed_count++;
		}
		list_add_tail(&memb->list, &cg->removed);

		if (memb->failed)
			node_history_fail(mg, memb->nodeid, cg,
					  left_list[i].reason);
		else
			node_history_left(mg, memb->nodeid, cg);

		log_group(mg, "add_change %u nodeid %d remove reason %d",
			  cg->seq, memb->nodeid, left_list[i].reason);
	}

	for (i = 0; i < joined_list_entries; i++) {
		memb = find_memb(cg, joined_list[i].nodeid);
		if (!memb) {
			log_error("no member %d", joined_list[i].nodeid);
			error = -ENOENT;
			goto fail;
		}
		memb->added = 1;

		if (memb->nodeid == our_nodeid)
			cg->we_joined = 1;
		else
			node_history_init(mg, memb->nodeid, cg);

		log_group(mg, "add_change %u nodeid %d joined", cg->seq,
			  memb->nodeid);
	}

	if (cg->we_joined) {
		log_group(mg, "add_change %u we joined", cg->seq);
		list_for_each_entry(memb, &cg->members, list)
			node_history_init(mg, memb->nodeid, cg);
	}

	log_group(mg, "add_change %u member %d joined %d remove %d failed %d",
		  cg->seq, cg->member_count, cg->joined_count, cg->remove_count,
		  cg->failed_count);

	list_add(&cg->list, &mg->changes);
	*cg_out = cg;
	return 0;

 fail_nomem:
	log_error("no memory");
	error = -ENOMEM;
 fail:
	free_cg(cg);
	return error;
}

static int we_left(struct cpg_address *left_list, int left_list_entries)
{
	int i;

	for (i = 0; i < left_list_entries; i++) {
		if (left_list[i].nodeid == our_nodeid)
			return 1;
	}
	return 0;
}

static void confchg_cb(cpg_handle_t handle, struct cpg_name *group_name,
		       struct cpg_address *member_list, int member_list_entries,
		       struct cpg_address *left_list, int left_list_entries,
		       struct cpg_address *joined_list, int joined_list_entries)
{
	struct mountgroup *mg;
	struct change *cg;
	int rv;

	mg = find_mg_handle(handle);
	if (!mg) {
		log_error("confchg_cb no mountgroup for cpg %s",
			  group_name->value);
		return;
	}

	if (mg->leaving && we_left(left_list, left_list_entries)) {
		/* we called cpg_leave(), and this should be the final
		   cpg callback we receive */
		log_group(mg, "confchg for our leave");
		cpg_finalize(mg->cpg_handle);
		client_dead(mg->cpg_client);
		list_del(&mg->list);
		free_mg(mg);
		return;
	}

	rv = add_change(mg, member_list, member_list_entries,
			left_list, left_list_entries,
			joined_list, joined_list_entries, &cg);
	if (rv)
		return;

	process_mountgroup(mg);
}

static void deliver_cb(cpg_handle_t handle, struct cpg_name *group_name,
		       uint32_t nodeid, uint32_t pid, void *data, int len)
{
	struct mountgroup *mg;
	struct gfs_header *hd;

	mg = find_mg_handle(handle);
	if (!mg) {
		log_error("deliver_cb no mg for cpg %s", group_name->value);
		return;
	}

	hd = (struct gfs_header *)data;

	hd->version[0]  = le16_to_cpu(hd->version[0]);
	hd->version[1]  = le16_to_cpu(hd->version[1]);
	hd->version[2]  = le16_to_cpu(hd->version[2]);
	hd->type        = le16_to_cpu(hd->type);
	hd->nodeid      = le32_to_cpu(hd->nodeid);
	hd->to_nodeid   = le32_to_cpu(hd->to_nodeid);
	hd->global_id   = le32_to_cpu(hd->global_id);
	hd->flags       = le32_to_cpu(hd->flags);
	hd->msgdata     = le32_to_cpu(hd->msgdata);

	if (hd->version[0] != protocol_active[0]) {
		log_error("reject message from %d version %u.%u.%u vs %u.%u.%u",
			  nodeid, hd->version[0], hd->version[1],
			  hd->version[2], protocol_active[0],
			  protocol_active[1], protocol_active[2]);
		return;
	}

	if (hd->nodeid != nodeid) {
		log_error("bad msg nodeid %d %d", hd->nodeid, nodeid);
		return;
	}

	switch (hd->type) {
	case GFS_MSG_START:
		receive_start(mg, hd, len);
		break;
	case GFS_MSG_MOUNT_DONE:
		if (!mg->started_count)
			save_message(mg, hd, len);
		else
			receive_mount_done(mg, hd, len);
		break;
	case GFS_MSG_FIRST_RECOVERY_DONE:
		receive_first_recovery_done(mg, hd, len);
		break;
	case GFS_MSG_RECOVERY_RESULT:
		if (!mg->started_count)
			save_message(mg, hd, len);
		else
			receive_recovery_result(mg, hd, len);
		break;
	default:
		log_error("unknown msg type %d", hd->type);
	}

	process_mountgroup(mg);
}

static cpg_callbacks_t cpg_callbacks = {
	.cpg_deliver_fn = deliver_cb,
	.cpg_confchg_fn = confchg_cb,
};

static void process_mountgroup_cpg(int ci)
{
	struct mountgroup *mg;
	cpg_error_t error;

	mg = find_mg_ci(ci);
	if (!mg) {
		log_error("process_mountgroup_cpg no mountgroup for ci %d", ci);
		return;
	}

	error = cpg_dispatch(mg->cpg_handle, CPG_DISPATCH_ALL);
	if (error != CPG_OK) {
		log_error("cpg_dispatch error %d", error);
		return;
	}

	update_flow_control_status();
}

int gfs_join_mountgroup(struct mountgroup *mg)
{
	cpg_error_t error;
	cpg_handle_t h;
	struct cpg_name name;
	int i = 0, fd, ci;

	error = cpg_initialize(&h, &cpg_callbacks);
	if (error != CPG_OK) {
		log_error("cpg_initialize error %d", error);
		goto fail;
	}

	cpg_fd_get(h, &fd);

	ci = client_add(fd, process_mountgroup_cpg, NULL);

	mg->cpg_handle = h;
	mg->cpg_client = ci;
	mg->cpg_fd = fd;
	mg->kernel_stopped = 1;
	mg->joining = 1;

	memset(&name, 0, sizeof(name));
	sprintf(name.value, "gfs:%s", mg->name);
	name.length = strlen(name.value) + 1;

	/* TODO: allow global_id to be set in cluster.conf? */
	mg->id = cpgname_to_crc(name.value, name.length);

 retry:
	error = cpg_join(h, &name);
	if (error == CPG_ERR_TRY_AGAIN) {
		sleep(1);
		if (!(++i % 10))
			log_error("cpg_join error retrying");
		goto retry;
	}
	if (error != CPG_OK) {
		log_error("cpg_join error %d", error);
		cpg_finalize(h);
		goto fail_client;
	}

	return 0;

 fail_client:
	client_dead(ci);
	cpg_finalize(h);
 fail:
	return -ENOTCONN;
}

void gfs_leave_mountgroup(char *mgname, int mnterr)
{
	struct mountgroup *mg;
	cpg_error_t error;
	struct cpg_name name;
	int i = 0;

	mg = find_mg(mgname);
	if (!mg) {
		log_error("leave: %s not found", mgname);
		return;
	}

	mg->leaving = 1;

	memset(&name, 0, sizeof(name));
	sprintf(name.value, "gfs:%s", mg->name);
	name.length = strlen(name.value) + 1;

 retry:
	error = cpg_leave(mg->cpg_handle, &name);
	if (error == CPG_ERR_TRY_AGAIN) {
		sleep(1);
		if (!(++i % 10))
			log_error("cpg_leave error retrying");
		goto retry;
	}
	if (error != CPG_OK)
		log_error("cpg_leave error %d", error);
}

int setup_cpg(void)
{
	cpg_error_t error;

	error = cpg_initialize(&libcpg_handle, &cpg_callbacks);
	if (error != CPG_OK) {
		log_error("setup_cpg cpg_initialize error %d", error);
		return -1;
	}

	/* join "gfs_controld" cpg to interact with other daemons in
	   the cluster before we start processing uevents?  Could this
	   also help in handling transient partitions? */

	return 0;
}

int setup_dlmcontrol(void)
{
	dlmcontrol_fd = dlmc_fs_connect();
	return dlmcontrol_fd;
}

int set_mountgroup_info(struct mountgroup *mg, struct gfsc_mountgroup *out)
{
	struct change *cg, *last = NULL;

	strncpy(out->name, mg->name, GFS_MOUNTGROUP_LEN);
	out->global_id = mg->id;

	if (mg->joining)
		out->flags |= GFSC_MF_JOINING;
	if (mg->leaving)
		out->flags |= GFSC_MF_LEAVING;
	if (mg->kernel_stopped)
		out->flags |= GFSC_MF_KERNEL_STOPPED;
	if (mg->kernel_mount_done)
		out->flags |= GFSC_MF_KERNEL_MOUNT_DONE;
	if (mg->kernel_mount_error)
		out->flags |= GFSC_MF_KERNEL_MOUNT_ERROR;
	if (mg->first_recovery_needed)
		out->flags |= GFSC_MF_FIRST_RECOVERY_NEEDED;
	if (mg->first_recovery_msg)
		out->flags |= GFSC_MF_FIRST_RECOVERY_MSG;
	if (mg->local_recovery_busy)
		out->flags |= GFSC_MF_LOCAL_RECOVERY_BUSY;

	if (!mg->started_change)
		goto next;

	cg = mg->started_change;

	out->cg_prev.member_count = cg->member_count;
	out->cg_prev.joined_count = cg->joined_count;
	out->cg_prev.remove_count = cg->remove_count;
	out->cg_prev.failed_count = cg->failed_count;
	out->cg_prev.combined_seq = cg->combined_seq;
	out->cg_prev.seq = cg->seq;

 next:
	if (list_empty(&mg->changes))
		goto out;

	list_for_each_entry(cg, &mg->changes, list)
		last = cg;

	cg = list_first_entry(&mg->changes, struct change, list);

	out->cg_next.member_count = cg->member_count;
	out->cg_next.joined_count = cg->joined_count;
	out->cg_next.remove_count = cg->remove_count;
	out->cg_next.failed_count = cg->failed_count;
	out->cg_next.combined_seq = last->seq;
	out->cg_next.seq = cg->seq;

	/* FIXME: use real definitions for these conditions
	   (also in dlm_controld) */

	if (cg->state == CGST_WAIT_CONDITIONS)
		out->cg_next.wait_condition = 4;
	if (!mg->kernel_mount_done)
		out->cg_next.wait_condition = 1;
	if (mg->dlm_notify_nodeid)
		out->cg_next.wait_condition = 2;
	if (poll_dlm)
		out->cg_next.wait_condition = 3;

	if (cg->state == CGST_WAIT_MESSAGES)
		out->cg_next.wait_messages = 1;
 out:
	return 0;
}

static int _set_node_info(struct mountgroup *mg, struct change *cg, int nodeid,
			  struct gfsc_node *node)
{
	struct member *m = NULL;
	struct node *n;

	node->nodeid = nodeid;

	if (cg)
		m = find_memb(cg, nodeid);
	if (!m)
		goto history;

	node->flags |= GFSC_NF_MEMBER;

	if (m->start)
		node->flags |= GFSC_NF_START;
	if (m->disallowed)
		node->flags |= GFSC_NF_DISALLOWED;

 history:
	n = get_node_history(mg, nodeid);
	if (!n)
		goto out;

	node->jid = n->jid;

	if (n->kernel_mount_done)
		node->flags |= GFSC_NF_KERNEL_MOUNT_DONE;
	if (n->kernel_mount_error)
		node->flags |= GFSC_NF_KERNEL_MOUNT_ERROR;
	if (n->check_dlm)
		node->flags |= GFSC_NF_CHECK_DLM;
	if (n->ro)
		node->flags |= GFSC_NF_READONLY;
	if (n->spectator)
		node->flags |= GFSC_NF_SPECTATOR;

	node->added_seq = n->added_seq;
	node->removed_seq = n->removed_seq;
	node->failed_reason = n->failed_reason;
 out:
	return 0;
}

int set_node_info(struct mountgroup *mg, int nodeid, struct gfsc_node *node)
{
	struct change *cg;

	if (!list_empty(&mg->changes)) {
		cg = list_first_entry(&mg->changes, struct change, list);
		return _set_node_info(mg, cg, nodeid, node);
	}

	return _set_node_info(mg, mg->started_change, nodeid, node);
}

int set_mountgroups(int *count, struct gfsc_mountgroup **mgs_out)
{
	struct mountgroup *mg;
	struct gfsc_mountgroup *mgs, *mgp;
	int mg_count = 0;

	list_for_each_entry(mg, &mountgroups, list)
		mg_count++;

	mgs = malloc(mg_count * sizeof(struct gfsc_mountgroup));
	if (!mgs)
		return -ENOMEM;
	memset(mgs, 0, mg_count * sizeof(struct gfsc_mountgroup));

	mgp = mgs;
	list_for_each_entry(mg, &mountgroups, list) {
		set_mountgroup_info(mg, mgp++);
	}

	*count = mg_count;
	*mgs_out = mgs;
	return 0;
}

int set_mountgroup_nodes(struct mountgroup *mg, int option, int *node_count,
                        struct gfsc_node **nodes_out)
{
	struct change *cg;
	struct node *n;
	struct gfsc_node *nodes = NULL, *nodep;
	struct member *memb;
	int count = 0;

	if (option == GFSC_NODES_ALL) {
		if (!list_empty(&mg->changes))
			cg = list_first_entry(&mg->changes, struct change,list);
		else
			cg = mg->started_change;

		list_for_each_entry(n, &mg->node_history, list)
			count++;

	} else if (option == GFSC_NODES_MEMBERS) {
		if (!mg->started_change)
			goto out;
		cg = mg->started_change;
		count = cg->member_count;

	} else if (option == GFSC_NODES_NEXT) {
		if (list_empty(&mg->changes))
			goto out;
		cg = list_first_entry(&mg->changes, struct change, list);
		count = cg->member_count;
	} else
		goto out;

	nodes = malloc(count * sizeof(struct gfsc_node));
	if (!nodes)
		return -ENOMEM;
	memset(nodes, 0, count * sizeof(struct gfsc_node));
	nodep = nodes;

	if (option == GFSC_NODES_ALL) {
		list_for_each_entry(n, &mg->node_history, list)
			_set_node_info(mg, cg, n->nodeid, nodep++);
	} else {
		list_for_each_entry(memb, &cg->members, list)
			_set_node_info(mg, cg, memb->nodeid, nodep++);
	}
 out:
	*node_count = count;
	*nodes_out = nodes;
	return 0;
}

