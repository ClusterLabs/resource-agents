/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2007-2008 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "dlm_daemon.h"
#include "config.h"

uint32_t cpgname_to_crc(const char *data, int len);

int message_flow_control_on;
static cpg_handle_t daemon_handle;
static unsigned int protocol_active[3] = {1, 0, 0};

struct member {
	struct list_head list;
	int nodeid;
	int start;   /* 1 if we received a start message for this change */
	int added;   /* 1 if added by this change */
	int failed;  /* 1 if failed in this change */
	int disallowed;
	uint32_t start_flags;
};

struct node {
	struct list_head list;
	int nodeid;
	int check_fencing;
	int check_quorum;
	int check_fs;
	int fs_notify;
	struct timeval add_time;
};

/* One of these change structs is created for every confchg a cpg gets. */

#define CGST_WAIT_CONDITIONS 1
#define CGST_WAIT_MESSAGES   2

struct change {
	struct list_head list;
	struct list_head members;
	struct list_head removed; /* nodes removed by this change */
	int member_count;
	int joined_count;
	int remove_count;
	int failed_count;
	int state;
	int we_joined;
	uint32_t seq; /* just used as a reference when debugging */
};

char *msg_name(int type)
{
	switch (type) {
	case DLM_MSG_START:
		return "start";
	case DLM_MSG_PLOCK:
		return "plock";
	case DLM_MSG_PLOCK_OWN:
		return "plock_own";
	case DLM_MSG_PLOCK_DROP:
		return "plock_drop";
	case DLM_MSG_PLOCK_SYNC_LOCK:
		return "plock_sync_lock";
	case DLM_MSG_PLOCK_SYNC_WAITER:
		return "plock_sync_waiter";
	case DLM_MSG_PLOCKS_STORED:
		return "plocks_stored";
	case DLM_MSG_DEADLK_CYCLE_START:
		return "deadlk_cycle_start";
	case DLM_MSG_DEADLK_CYCLE_END:
		return "deadlk_cycle_end";
	case DLM_MSG_DEADLK_CHECKPOINT_READY:
		return "deadlk_checkpoint_ready";
	case DLM_MSG_DEADLK_CANCEL_LOCK:
		return "deadlk_cancel_lock";
	default:
		return "unknown";
	}
}

static char *str_nums(int *nums, int n_ints)
{
	static char buf[128];
	int i, len, ret, pos = 0;

	len = sizeof(buf);
	memset(buf, 0, len);

	for (i = 0; i < n_ints; i++) {
		ret = snprintf(buf + pos, len - pos, "%d ",
			       le32_to_cpu(nums[i]));
		if (ret >= len - pos)
			break;
		pos += ret;
	}

	return buf;
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

void dlm_send_message(struct lockspace *ls, char *buf, int len)
{
	struct dlm_header *hd = (struct dlm_header *) buf;
	int type = hd->type;

	hd->version[0]  = cpu_to_le16(protocol_active[0]);
	hd->version[1]  = cpu_to_le16(protocol_active[1]);
	hd->version[2]  = cpu_to_le16(protocol_active[2]);
	hd->type	= cpu_to_le16(hd->type);
	hd->nodeid      = cpu_to_le32(our_nodeid);
	hd->to_nodeid   = cpu_to_le32(hd->to_nodeid);
	hd->global_id   = cpu_to_le32(ls->global_id);
	hd->flags       = cpu_to_le32(hd->flags);
	hd->msgdata     = cpu_to_le32(hd->msgdata);

	_send_message(ls->cpg_handle, buf, len, type);
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

static struct lockspace *find_ls_handle(cpg_handle_t h)
{
	struct lockspace *ls;

	list_for_each_entry(ls, &lockspaces, list) {
		if (ls->cpg_handle == h)
			return ls;
	}
	return NULL;
}

static struct lockspace *find_ls_ci(int ci)
{
	struct lockspace *ls;

	list_for_each_entry(ls, &lockspaces, list) {
		if (ls->cpg_client == ci)
			return ls;
	}
	return NULL;
}

static void free_cg(struct change *cg)
{
	struct member *memb, *safe;

	list_for_each_entry_safe(memb, safe, &cg->members, list) {
		list_del(&memb->list);
		free(memb);
	}
	list_for_each_entry_safe(memb, safe, &cg->removed, list) {
		list_del(&memb->list);
		free(memb);
	}
	free(cg);
}

static void free_ls(struct lockspace *ls)
{
	struct change *cg, *cg_safe;
	struct node *node, *node_safe;

	list_for_each_entry_safe(cg, cg_safe, &ls->changes, list) {
		list_del(&cg->list);
		free_cg(cg);
	}

	if (ls->started_change)
		free_cg(ls->started_change);

	list_for_each_entry_safe(node, node_safe, &ls->node_history, list) {
		list_del(&node->list);
		free(node);
	}

	free(ls);
}


/* Problem scenario:
   nodes A,B,C are in fence domain
   node C has gfs foo mounted
   node C fails
   nodes A,B begin fencing C (slow, not completed)
   node B mounts gfs foo

   We may end up having gfs foo mounted and being used on B before
   C has been fenced.  C could wake up corrupt fs.

   So, we need to prevent any new gfs mounts while there are any
   outstanding, incomplete fencing operations.

   We also need to check that the specific failed nodes we know about have
   been fenced (since fenced may not even have been notified that the node
   has failed yet).

   So, check that:
   1. has fenced fenced the node after it joined this lockspace?
   2. fenced has no outstanding fencing ops

   For 1:
   - record the time of the first good start message we see from node X
   - node X fails
   - wait for X to be removed from all dlm cpg's  (probably not necessary)
   - check that the fencing time is later than the recorded time above

   Tracking fencing state when there are spurious partitions/merges...

   from a spurious leave/join of node X, a lockspace will see:
   - node X is a lockspace member
   - node X fails, may be waiting for all cpgs to see failure or for fencing to
     complete
   - node X joins the lockspace - we want to process the change as usual, but
     don't want to disrupt the code waiting for the fencing, and we want to
     continue running properly once the remerged node is properly reset

   ls->node_history
   when we see a node not in this list, add entry for it with zero add_time
   record the time we get a good start message from the node, add_time
   clear add_time if the node leaves
   if node fails with non-zero add_time, set check_fencing
   when a node is fenced, clear add_time and clear check_fencing
   if a node remerges after this, no good start message, no new add_time set
   if a node fails with zero add_time, it doesn't need fencing
   if a node remerges before it's been fenced, no good start message, no new
   add_time set 
*/

static struct node *get_node_history(struct lockspace *ls, int nodeid)
{
	struct node *node;

	list_for_each_entry(node, &ls->node_history, list) {
		if (node->nodeid == nodeid)
			return node;
	}
	return NULL;
}

static void node_history_init(struct lockspace *ls, int nodeid)
{
	struct node *node;

	node = get_node_history(ls, nodeid);
	if (node)
		return;

	node = malloc(sizeof(struct node));
	if (!node)
		return;
	memset(node, 0, sizeof(struct node));

	node->nodeid = nodeid;
	timerclear(&node->add_time);
	list_add_tail(&node->list, &ls->node_history);
}

static void node_history_start(struct lockspace *ls, int nodeid)
{
	struct node *node;
	
	node = get_node_history(ls, nodeid);
	if (!node) {
		log_error("node_history_start no nodeid %d", nodeid);
		return;
	}

	gettimeofday(&node->add_time, NULL);
}

static void node_history_left(struct lockspace *ls, int nodeid)
{
	struct node *node;

	node = get_node_history(ls, nodeid);
	if (!node) {
		log_error("node_history_left no nodeid %d", nodeid);
		return;
	}

	timerclear(&node->add_time);
}

static void node_history_fail(struct lockspace *ls, int nodeid)
{
	struct node *node;

	node = get_node_history(ls, nodeid);
	if (!node) {
		log_error("node_history_fail no nodeid %d", nodeid);
		return;
	}

	if (!timerisset(&node->add_time))
		node->check_fencing = 1;

	node->check_quorum = 1;
	node->check_fs = 1;
}

static int check_fencing_done(struct lockspace *ls)
{
	struct node *node;
	struct timeval last_fenced;
	int wait_count = 0;

	list_for_each_entry(node, &ls->node_history, list) {
		if (!node->check_fencing)
			continue;

		/* check with fenced to see if the node has been
		   fenced since node->add_time */

		/* fenced_last_success(node->nodeid, &last_fenced); */
		gettimeofday(&last_fenced, NULL);

		if (timercmp(&last_fenced, &node->add_time, >)) {
			node->check_fencing = 0;
			timerclear(&node->add_time);
		} else {
			log_group(ls, "check_fencing %d needs fencing",
				  node->nodeid);
			wait_count++;
		}
	}

	if (wait_count)
		return 0;

	/* now check if there are any outstanding fencing ops (for nodes
	   we may not have seen in any lockspace), and return 0 if there
	   are any */

	/*
	fenced_pending_count(&pending);
	if (pending)
		return 0;
	*/
	return 1;
}

static int check_quorum_done(struct lockspace *ls)
{
	struct node *node;
	int wait_count = 0;

	if (!cman_quorate) {
		log_group(ls, "check_quorum %d", cman_quorate);
		return 0;
	}

	list_for_each_entry(node, &ls->node_history, list) {
		if (!node->check_quorum)
			continue;

		if (!is_cman_member(node->nodeid)) {
			node->check_quorum = 0;
		} else {
			log_group(ls, "check_quorum %d is_cman_member",
				  node->nodeid);
			wait_count++;
		}
	}

	if (wait_count)
		return 0;

	log_group(ls, "check_quorum done");
	return 1;
}

static int check_fs_done(struct lockspace *ls)
{
	struct node *node;
	int wait_count = 0;

	/* no corresponding fs for this lockspace */
	if (!ls->fs_registered)
		return 1;

	list_for_each_entry(node, &ls->node_history, list) {
		if (!node->check_fs)
			continue;

		if (node->fs_notify) {
			node->check_fs = 0;
		} else {
			log_group(ls, "check_fs %d needs fs notify",
				  node->nodeid);
			wait_count++;
		}
	}

	if (wait_count)
		return 0;

	log_group(ls, "check_fs done");
	return 1;
}

static int member_ids[MAX_NODES];
static int member_count;
static int renew_ids[MAX_NODES];
static int renew_count;

static void format_member_ids(struct lockspace *ls)
{
	struct change *cg = list_first_entry(&ls->changes, struct change, list);
	struct member *memb;

	memset(member_ids, 0, sizeof(member_ids));
	member_count = 0;

	list_for_each_entry(memb, &cg->members, list)
		member_ids[member_count++] = memb->nodeid;
}

/* list of nodeids that have left and rejoined since last start_kernel;
   is any member of startcg in the left list of any other cg's?
   (if it is, then it presumably must be flagged added in another) */

static void format_renew_ids(struct lockspace *ls)
{
	struct change *cg, *startcg;
	struct member *memb, *leftmemb;

	startcg = list_first_entry(&ls->changes, struct change, list);

	memset(renew_ids, 0, sizeof(renew_ids));
	renew_count = 0;

	list_for_each_entry(memb, &startcg->members, list) {
		list_for_each_entry(cg, &ls->changes, list) {
			if (cg == startcg)
				continue;
			list_for_each_entry(leftmemb, &cg->removed, list) {
				if (memb->nodeid == leftmemb->nodeid) {
					renew_ids[renew_count++] = memb->nodeid;
				}
			}
		}
	}

}

static void start_kernel(struct lockspace *ls)
{
	struct change *cg = list_first_entry(&ls->changes, struct change, list);

	if (!ls->kernel_stopped) {
		log_error("start_kernel %u not stopped", cg->seq);
		return;
	}

	log_group(ls, "start_kernel %u member_count %d",
		  cg->seq, cg->member_count);

	format_member_ids(ls);
	format_renew_ids(ls);
	set_configfs_members(ls->name, member_count, member_ids,
			     renew_count, renew_ids);
	set_sysfs_control(ls->name, 1);
	ls->kernel_stopped = 0;

	if (ls->joining) {
		set_sysfs_id(ls->name, ls->global_id);
		set_sysfs_event_done(ls->name, 0);
		ls->joining = 0;
	}
}

static void stop_kernel(struct lockspace *ls, uint32_t seq)
{
	if (!ls->kernel_stopped) {
		log_group(ls, "stop_kernel %u", seq);
		set_sysfs_control(ls->name, 0);
		ls->kernel_stopped = 1;
	}
}

/* the first condition is that the local lockspace is stopped which we
   don't need to check for because stop_kernel(), which is synchronous,
   was done when the change was created */

static int wait_conditions_done(struct lockspace *ls)
{
	/* the fencing/quorum/fs conditions need to account for all the changes
	   that have occured since the last change applied to dlm-kernel, not
	   just the latest change */

	if (!check_fencing_done(ls)) {
		poll_fencing = 1;
		return 0;
	}
	poll_fencing = 0;

	/* even though fencing also waits for quorum, checking fencing isn't
	   sufficient because we don't want to start new lockspaces in an
	   inquorate cluster */

	if (!check_quorum_done(ls)) {
		poll_quorum = 1;
		return 0;
	}
	poll_quorum = 0;

	if (!check_fs_done(ls)) {
		poll_fs = 1;
		return 0;
	}
	poll_fs = 0;

	return 1;
}

static int wait_messages_done(struct lockspace *ls)
{
	struct change *cg = list_first_entry(&ls->changes, struct change, list);
	struct member *memb;
	int need = 0, total = 0;

	list_for_each_entry(memb, &cg->members, list) {
		if (!memb->start)
			need++;
		total++;
	}

	if (need) {
		log_group(ls, "wait_messages_done need %d of %d", need, total);
		return 0;
	}

	log_group(ls, "wait_messages_done got all %d", total);
	return 1;
}

static void cleanup_changes(struct lockspace *ls)
{
	struct change *cg = list_first_entry(&ls->changes, struct change, list);
	struct change *safe;

	list_del(&cg->list);
	if (ls->started_change)
		free_cg(ls->started_change);
	ls->started_change = cg;

	list_for_each_entry_safe(cg, safe, &ls->changes, list) {
		list_del(&cg->list);
		free_cg(cg);
	}
}

/* There's a stream of confchg and messages. At one of these
   messages, the low node needs to store plocks and new nodes
   need to begin saving plock messages.  A second message is
   needed to say that the plocks are ready to be read.

   When the last start message is recvd for a change, the low node
   stores plocks and the new nodes begin saving messages.  When the
   store is done, low node sends plocks_stored message.  When
   new nodes recv this, they read the plocks and their saved messages.
   plocks_stored message should identify a specific change, like start
   messages do; if it doesn't match ls->started_change, then it's ignored.

   If a confchg adding a new node arrives after plocks are stored but
   before plocks_stored msg recvd, then the message is ignored.  The low
   node will send another plocks_stored message for the latest change
   (although it may be able to reuse the ckpt if no plock state has changed).
*/

static void set_plock_ckpt_node(struct lockspace *ls)
{
	struct change *cg = list_first_entry(&ls->changes, struct change, list);
	struct member *memb;
	int low = 0;

	list_for_each_entry(memb, &cg->members, list) {
		if (!(memb->start_flags & DLM_MFLG_HAVEPLOCK))
			continue;

		if (!low || memb->nodeid < low)
			low = memb->nodeid;
	}

	log_group(ls, "set_plock_ckpt_node from %d to %d",
		  ls->plock_ckpt_node, low);

	if (ls->plock_ckpt_node == our_nodeid && low != our_nodeid) {
		/* Close ckpt so it will go away when the new ckpt_node
		   unlinks it prior to creating a new one; if we fail
		   our open ckpts are automatically closed.  At this point
		   the ckpt has not been unlinked, but won't be held open by
		   anyone.  We use the max "retentionDuration" to stop the
		   system from cleaning up ckpts that are open by no one. */
		close_plock_checkpoint(ls);
	}

	ls->plock_ckpt_node = low;
}

/* do the change details in the message match the details of the given change */

static int match_change(struct lockspace *ls, struct change *cg,
			struct dlm_header *hd, int len)
{
	struct member *memb;
	int member_count, joined_count, remove_count, failed_count;
	int i, n_ints, *nums, nodeid, members_mismatch;
	uint32_t seq = hd->msgdata;

	nums = (int *)((char *)hd + sizeof(struct dlm_header));

	member_count = le32_to_cpu(nums[0]);
	joined_count = le32_to_cpu(nums[1]);
	remove_count = le32_to_cpu(nums[2]);
	failed_count = le32_to_cpu(nums[3]);

	n_ints = 4 + member_count;
	if (len < (sizeof(struct dlm_header) + (n_ints * sizeof(int)))) {
		log_group(ls, "match_change fail %d:%u bad len %d nums %s",
			  hd->nodeid, seq, len, str_nums(nums, n_ints));
		return 0;
	}

	/* We can ignore messages if we're not in the list of members.  The one
	   known time this will happen is after we've joined the cpg, we can
	   get messages for changes prior to the change in which we're added. */

	for (i = 0; i < member_count; i++) {
		if (our_nodeid == le32_to_cpu(nums[4+i]))
			break;
	}
	if (i == member_count) {
		log_group(ls, "match_change fail %d:%u we are not in members",
			  hd->nodeid, seq);
		return 0;
	}

	memb = find_memb(cg, hd->nodeid);
	if (!memb) {
		log_group(ls, "match_change fail %d:%u sender not member",
			  hd->nodeid, seq);
		return 0;
	}

	/* verify this is the right change by matching the counts
	   and the nodeids of the current members */

	if (member_count != cg->member_count ||
	    joined_count != cg->joined_count ||
	    remove_count != cg->remove_count ||
	    failed_count != cg->failed_count) {
		log_group(ls, "match_change fail %d:%u expect counts "
			  "%d %d %d %d nums %s",
			  hd->nodeid, seq,
			  cg->member_count, cg->joined_count,
			  cg->remove_count, cg->failed_count,
			  str_nums(nums, n_ints));
		return 0;
	}

	members_mismatch = 0;
	for (i = 0; i < member_count; i++) {
		nodeid = le32_to_cpu(nums[4+i]);
		memb = find_memb(cg, nodeid);
		if (memb)
			continue;
		log_group(ls, "match_change fail %d:%u no memb %d",
			  hd->nodeid, seq, nodeid);
		members_mismatch = 1;
	}
	if (members_mismatch)
		return 0;

	return 1;
}

static void send_plocks_stored(struct lockspace *ls)
{
	struct change *cg = list_first_entry(&ls->changes, struct change, list);
	struct dlm_header *hd;
	struct member *memb;
	int n_ints, len, *p, i;
	char *buf;

	n_ints = 4 + cg->member_count;
	len = sizeof(struct dlm_header) + (n_ints * sizeof(uint32_t));

	buf = malloc(len);
	if (!buf) {
		return;
	}
	memset(buf, 0, len);

	hd = (struct dlm_header *)buf;
	hd->type = DLM_MSG_PLOCKS_STORED;
	hd->msgdata = cg->seq;

	p = (int *)(buf + sizeof(struct dlm_header));

	/* sending all this stuff is probably unnecessary, but gives
	   us more certainty in matching stopped messages to the correct
	   change that they are for */

	p[0] = cpu_to_le32(cg->member_count);
	p[1] = cpu_to_le32(cg->joined_count);
	p[2] = cpu_to_le32(cg->remove_count);
	p[3] = cpu_to_le32(cg->failed_count);

	i = 4;
	list_for_each_entry(memb, &cg->members, list)
		p[i++] = cpu_to_le32(memb->nodeid);

	dlm_send_message(ls, buf, len);

	free(buf);
}

static void receive_plocks_stored(struct lockspace *ls, struct dlm_header *hd,
				  int len)
{
	log_group(ls, "receive_plocks_stored %d:%u need_plocks %d",
		  hd->nodeid, hd->msgdata, ls->need_plocks);

	if (!ls->need_plocks)
		return;

	/* a confchg arrived between the last start and the plocks_stored msg,
	   so we ignore this plocks_stored msg and wait to read the ckpt until
	   the next plocks_stored msg following the current start */
	   
	if (!list_empty(&ls->changes) || !ls->started_change ||
	    !match_change(ls, ls->started_change, hd, len)) {
		log_group(ls, "receive_plocks_stored %d:%u ignore",
			  hd->nodeid, hd->msgdata);
		return;
	}

	retrieve_plocks(ls);
	process_saved_plocks(ls);
	ls->need_plocks = 0;
	ls->save_plocks = 0;
}

/* Unfortunately, there's no really simple way to match a message with the
   specific change that it was sent for.  We hope that by passing all the
   details of the change in the message, we will be able to uniquely match the
   it to the correct change. */

/* A start message will usually be for the first (current) change on our list.
   In some cases it will be for a non-current change, and we can ignore it:

   1. A,B,C get confchg1 adding C
   2. C sends start for confchg1
   3. A,B,C get confchg2 adding D
   4. A,B,C,D recv start from C for confchg1 - ignored
   5. C,D send start for confchg2
   6. A,B send start for confchg2
   7. A,B,C,D recv all start messages for confchg2, and start kernel
 
   In step 4, how do the nodes know whether the start message from C is
   for confchg1 or confchg2?  Hopefully by comparing the counts and members. */

static struct change *find_change(struct lockspace *ls, struct dlm_header *hd,
				  int len)
{
	struct change *cg;

	list_for_each_entry_reverse(cg, &ls->changes, list) {
		if (!match_change(ls, cg, hd, len))
			continue;
		return cg;
	}

	log_group(ls, "find_change %d:%u no match", hd->nodeid, hd->msgdata);
	return NULL;
}

/* We require new members (memb->added) to be joining the lockspace
   (memb->joining).  New members that are not joining the lockspace can happen
   when the cpg partitions and is then merged back together (shouldn't happen
   in general, but is possible).  We label these new members that are not
   joining as "disallowed", and ignore their start message. */

/* Handle spurious joins by ignoring this start message if the node says it's
   not joining (i.e. it's already a member), but we see it being added (i.e.
   it's not already a member) */

static void receive_start(struct lockspace *ls, struct dlm_header *hd, int len)
{
	struct change *cg;
	struct member *memb;
	int joining = 0;
	uint32_t seq = hd->msgdata;

	log_group(ls, "receive_start %d:%u flags %x len %d", hd->nodeid, seq,
		  hd->flags, len);

	cg = find_change(ls, hd, len);
	if (!cg)
		return;

	memb = find_memb(cg, hd->nodeid);
	if (!memb) {
		/* this should never happen since match_change checks it */
		log_error("receive_start no member %d", hd->nodeid);
		return;
	}

	memb->start_flags = hd->flags;

	if (memb->start_flags & DLM_MFLG_JOINING)
		joining = 1;

	if ((memb->added && !joining) || (!memb->added && joining)) {
		log_error("receive_start %d:%u disallowed added %d joining %d",
			  hd->nodeid, seq, memb->added, joining);
		memb->disallowed = 1;
	} else {
		node_history_start(ls, hd->nodeid);
		memb->start = 1;
	}
}

static void send_start(struct lockspace *ls)
{
	struct change *cg = list_first_entry(&ls->changes, struct change, list);
	struct dlm_header *hd;
	struct member *memb;
	int n_ints, len, *p, i;
	char *buf;

	n_ints = 4 + cg->member_count;
	len = sizeof(struct dlm_header) + (n_ints * sizeof(int));

	buf = malloc(len);
	if (!buf) {
		return;
	}
	memset(buf, 0, len);

	hd = (struct dlm_header *)buf;
	hd->type = DLM_MSG_START;
	hd->msgdata = cg->seq;

	if (cg->we_joined)
		hd->flags |= DLM_MFLG_JOINING;

	if (!ls->need_plocks)
		hd->flags |= DLM_MFLG_HAVEPLOCK;

	p = (int *)(buf + sizeof(struct dlm_header));

	/* sending all this stuff is probably unnecessary, but gives
	   us more certainty in matching stopped messages to the correct
	   change that they are for */

	p[0] = cpu_to_le32(cg->member_count);
	p[1] = cpu_to_le32(cg->joined_count);
	p[2] = cpu_to_le32(cg->remove_count);
	p[3] = cpu_to_le32(cg->failed_count);

	i = 4;
	list_for_each_entry(memb, &cg->members, list)
		p[i++] = cpu_to_le32(memb->nodeid);

	log_group(ls, "send_start %u flags %x counts %d %d %d %d", cg->seq,
		  hd->flags, cg->member_count, cg->joined_count,
		  cg->remove_count, cg->failed_count);

	dlm_send_message(ls, buf, len);

	free(buf);
}

static int nodes_added(struct lockspace *ls)
{
	struct change *cg;

	list_for_each_entry(cg, &ls->changes, list) {
		if (cg->joined_count)
			return 1;
	}
	return 0;
}

static void prepare_plocks(struct lockspace *ls)
{
	struct change *cg = list_first_entry(&ls->changes, struct change, list);
	struct member *memb;

	if (!cfgd_enable_plock)
		return;

	/* if we're the only node in the lockspace, then we are the ckpt_node
	   and we don't need plocks */

	if (cg->member_count == 1) {
		list_for_each_entry(memb, &cg->members, list) {
			if (memb->nodeid != our_nodeid) {
				log_error("prepare_plocks other member %d",
					  memb->nodeid);
			}
		}
		ls->plock_ckpt_node = our_nodeid;
		ls->need_plocks = 0;
		return;
	}

	/* the low node that indicated it had plock state in its last
	   start message is the ckpt_node */

	set_plock_ckpt_node(ls);

	/* We save all plock messages from the time that the low node saves
	   existing plock state in the ckpt to the time that we read that state
	   from the ckpt. */

	if (ls->need_plocks) {
		ls->save_plocks = 1;
		return;
	}

	if (ls->plock_ckpt_node != our_nodeid)
		return;

	/* At each start, a ckpt is written if there have been nodes added
	   since the last start/ckpt.  If no nodes have been added, no one
	   does anything with ckpts.  If the node that wrote the last ckpt
	   is no longer the ckpt_node, the new ckpt_node will unlink and
	   write a new one.  If the node that wrote the last ckpt is still
	   the ckpt_node and no plock state has changed since the last ckpt,
	   it will just leave the old ckpt and not write a new one.
	 
	   A new ckpt_node will send a stored message even if it doesn't
	   write a ckpt because new nodes in the previous start may be
	   waiting to read the ckpt from the previous ckpt_node after ignoring
	   the previous stored message.  They will read the ckpt from the
	   previous ckpt_node upon receiving the stored message from us. */

	if (nodes_added(ls))
		store_plocks(ls);
	send_plocks_stored(ls);
}

static void apply_changes(struct lockspace *ls)
{
	struct change *cg;

	if (list_empty(&ls->changes))
		return;
	cg = list_first_entry(&ls->changes, struct change, list);

	switch (cg->state) {

	case CGST_WAIT_CONDITIONS:
		if (wait_conditions_done(ls)) {
			send_start(ls);
			cg->state = CGST_WAIT_MESSAGES;
		}
		break;

	case CGST_WAIT_MESSAGES:
		if (wait_messages_done(ls)) {
			start_kernel(ls);
			prepare_plocks(ls);
			cleanup_changes(ls);
		}
		break;

	default:
		log_error("apply_changes invalid state %d", cg->state);
	}
}

void process_lockspace_changes(void)
{
	struct lockspace *ls, *safe;

	list_for_each_entry_safe(ls, safe, &lockspaces, list) {
		if (!list_empty(&ls->changes))
			apply_changes(ls);
	}
}

static int add_change(struct lockspace *ls,
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
	cg->seq = ++ls->change_seq;
	cg->state = CGST_WAIT_CONDITIONS;

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
			node_history_fail(ls, memb->nodeid);
		else
			node_history_left(ls, memb->nodeid);

		log_group(ls, "add_change %u nodeid %d remove reason %d",
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
			node_history_init(ls, memb->nodeid);

		log_group(ls, "add_change %u nodeid %d joined", cg->seq,
			  memb->nodeid);
	}

	if (cg->we_joined)
		list_for_each_entry(memb, &cg->members, list)
			node_history_init(ls, memb->nodeid);

	log_group(ls, "add_change %u member %d joined %d remove %d failed %d",
		  cg->seq, cg->member_count, cg->joined_count, cg->remove_count,
		  cg->failed_count);

	list_add(&cg->list, &ls->changes);
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
	struct lockspace *ls;
	struct change *cg;
	struct member *memb;
	int rv;

	ls = find_ls_handle(handle);
	if (!ls) {
		log_error("confchg_cb no lockspace for cpg %s",
			  group_name->value);
		return;
	}

	if (ls->leaving && we_left(left_list, left_list_entries)) {
		/* we called cpg_leave(), and this should be the final
		   cpg callback we receive */
		log_group(ls, "confchg for our leave");
		stop_kernel(ls, 0);
		set_configfs_members(ls->name, 0, NULL, 0, NULL);
		set_sysfs_event_done(ls->name, 0);
		cpg_finalize(ls->cpg_handle);
		client_dead(ls->cpg_client);
		purge_plocks(ls, our_nodeid, 1);
		list_del(&ls->list);
		free_ls(ls);
		return;
	}

	rv = add_change(ls, member_list, member_list_entries,
			left_list, left_list_entries,
			joined_list, joined_list_entries, &cg);
	if (rv)
		return;

	stop_kernel(ls, cg->seq);

	list_for_each_entry(memb, &cg->removed, list)
		purge_plocks(ls, memb->nodeid, 0);

#if 0
	/* deadlock code needs to adjust per a confchg, is this the right
	   way/place for this? */

	deadlk_confchg(ls, member_list, member_list_entries,
		       left_list, left_list_entries,
		       joined_list, joined_list_entries);
#endif
}

static void deliver_cb(cpg_handle_t handle, struct cpg_name *group_name,
		       uint32_t nodeid, uint32_t pid, void *data, int len)
{
	struct lockspace *ls;
	struct dlm_header *hd;

	ls = find_ls_handle(handle);
	if (!ls) {
		log_error("deliver_cb no ls for cpg %s", group_name->value);
		return;
	}

	hd = (struct dlm_header *)data;

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
	case DLM_MSG_START:
		receive_start(ls, hd, len);
		break;

	case DLM_MSG_PLOCK:
		receive_plock(ls, hd, len);
		break;

	case DLM_MSG_PLOCK_OWN:
		receive_own(ls, hd, len);
		break;

	case DLM_MSG_PLOCK_DROP:
		receive_drop(ls, hd, len);
		break;

	case DLM_MSG_PLOCK_SYNC_LOCK:
	case DLM_MSG_PLOCK_SYNC_WAITER:
		receive_sync(ls, hd, len);
		break;

	case DLM_MSG_PLOCKS_STORED:
		receive_plocks_stored(ls, hd, len);
		break;

	case DLM_MSG_DEADLK_CYCLE_START:
		receive_cycle_start(ls, hd, len);
		break;

	case DLM_MSG_DEADLK_CYCLE_END:
		receive_cycle_end(ls, hd, len);
		break;

	case DLM_MSG_DEADLK_CHECKPOINT_READY:
		receive_checkpoint_ready(ls, hd, len);
		break;

	case DLM_MSG_DEADLK_CANCEL_LOCK:
		receive_cancel_lock(ls, hd, len);
		break;

	default:
		log_error("unknown msg type %d", hd->type);
	}
}

static cpg_callbacks_t cpg_callbacks = {
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

static void process_lockspace_cpg(int ci)
{
	struct lockspace *ls;
	cpg_error_t error;

	ls = find_ls_ci(ci);
	if (!ls) {
		log_error("process_lockspace_cpg no lockspace for ci %d", ci);
		return;
	}

	error = cpg_dispatch(ls->cpg_handle, CPG_DISPATCH_ALL);
	if (error != CPG_OK) {
		log_error("cpg_dispatch error %d", error);
		return;
	}

	apply_changes(ls);

	update_flow_control_status();
}

/* received an "online" uevent from dlm-kernel */

int dlm_join_lockspace(struct lockspace *ls)
{
	cpg_error_t error;
	cpg_handle_t h;
	struct cpg_name name;
	int i = 0, fd, ci;

	error = cpg_initialize(&h, &cpg_callbacks);
	if (error != CPG_OK) {
		log_error("cpg_initialize error %d", error);
		goto fail_free;
	}

	cpg_fd_get(h, &fd);

	ci = client_add(fd, process_lockspace_cpg, NULL);

	list_add(&ls->list, &lockspaces);

	ls->cpg_handle = h;
	ls->cpg_client = ci;
	ls->cpg_fd = fd;
	ls->kernel_stopped = 1;
	ls->need_plocks = 1;
	ls->joining = 1;

	memset(&name, 0, sizeof(name));
	sprintf(name.value, "dlm:%s", ls->name);
	name.length = strlen(name.value) + 1;

	/* TODO: allow global_id to be set in cluster.conf? */
	ls->global_id = cpgname_to_crc(name.value, name.length);

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
		goto fail;
	}

	return 0;

 fail:
	list_del(&ls->list);
	client_dead(ci);
	cpg_finalize(h);
 fail_free:
	free_ls(ls);
	return error;
}

/* received an "offline" uevent from dlm-kernel */

int dlm_leave_lockspace(struct lockspace *ls)
{
	cpg_error_t error;
	struct cpg_name name;
	int i = 0;

	ls->leaving = 1;

	memset(&name, 0, sizeof(name));
	sprintf(name.value, "dlm:%s", ls->name);
	name.length = strlen(name.value) + 1;

 retry:
	error = cpg_leave(ls->cpg_handle, &name);
	if (error == CPG_ERR_TRY_AGAIN) {
		sleep(1);
		if (!(++i % 10))
			log_error("cpg_leave error retrying");
		goto retry;
	}
	if (error != CPG_OK)
		log_error("cpg_leave error %d", error);

	return 0;
}

int setup_cpg(void)
{
	cpg_error_t error;

	error = cpg_initialize(&daemon_handle, &cpg_callbacks);
	if (error != CPG_OK) {
		log_error("setup_cpg cpg_initialize error %d", error);
		return -1;
	}

	/* join "dlm_controld" cpg to interact with other daemons in
	   the cluster before we start processing uevents?  Could this
	   also help in handling transient partitions? */

	return 0;
}

