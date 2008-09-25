#include "dlm_daemon.h"
#include "config.h"

uint32_t cpgname_to_crc(const char *data, int len);

struct protocol_version {
	uint16_t major;
	uint16_t minor;
	uint16_t patch;
	uint16_t flags;
};

struct protocol {
	union {
		struct protocol_version dm_ver;
		uint16_t                daemon_max[4];
	};
	union {
		struct protocol_version km_ver;
		uint16_t                kernel_max[4];
	};
	union {
		struct protocol_version dr_ver;
		uint16_t                daemon_run[4];
	};
	union {
		struct protocol_version kr_ver;
		uint16_t                kernel_run[4];
	};
};

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
	int fs_notified;
	uint64_t add_time;
	uint32_t added_seq;	/* for queries */
	uint32_t removed_seq;	/* for queries */
	int failed_reason;	/* for queries */

	struct protocol proto;
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
	uint32_t seq; /* used as a reference for debugging, and for queries */
	uint32_t combined_seq; /* for queries */
};

struct ls_info {
	uint32_t ls_info_size;
	uint32_t id_info_size;
	uint32_t id_info_count;

	uint32_t started_count;

	int member_count;
	int joined_count;
	int remove_count;
	int failed_count;
};

struct id_info {
	int nodeid;
};

int message_flow_control_on;
static int daemon_cpg_fd;
static cpg_handle_t daemon_cpg_handle;
static struct protocol our_protocol;
static struct list_head daemon_nodes;
static struct cpg_address daemon_member[MAX_NODES];
static int daemon_member_count;

static void ls_info_in(struct ls_info *li)
{
	li->ls_info_size  = le32_to_cpu(li->ls_info_size);
	li->id_info_size  = le32_to_cpu(li->id_info_size);
	li->id_info_count = le32_to_cpu(li->id_info_count);
	li->started_count = le32_to_cpu(li->started_count);
	li->member_count  = le32_to_cpu(li->member_count);
	li->joined_count  = le32_to_cpu(li->joined_count);
	li->remove_count  = le32_to_cpu(li->remove_count);
	li->failed_count  = le32_to_cpu(li->failed_count);
}

static void id_info_in(struct id_info *id)
{
	id->nodeid = le32_to_cpu(id->nodeid);
}

static void ids_in(struct ls_info *li, struct id_info *ids)
{
	struct id_info *id;
	int i;

	id = ids;
	for (i = 0; i < li->id_info_count; i++) {
		id_info_in(id);
		id = (struct id_info *)((char *)id + li->id_info_size);
	}
}

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

	hd->version[0]  = cpu_to_le16(our_protocol.daemon_run[0]);
	hd->version[1]  = cpu_to_le16(our_protocol.daemon_run[1]);
	hd->version[2]  = cpu_to_le16(our_protocol.daemon_run[2]);
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

static void node_history_init(struct lockspace *ls, int nodeid,
			      struct change *cg)
{
	struct node *node;

	node = get_node_history(ls, nodeid);
	if (node)
		goto out;

	node = malloc(sizeof(struct node));
	if (!node)
		return;
	memset(node, 0, sizeof(struct node));

	node->nodeid = nodeid;
	node->add_time = 0;
	list_add_tail(&node->list, &ls->node_history);
 out:
	node->added_seq = cg->seq;	/* for queries */
}

static void node_history_start(struct lockspace *ls, int nodeid)
{
	struct node *node;
	
	node = get_node_history(ls, nodeid);
	if (!node) {
		log_error("node_history_start no nodeid %d", nodeid);
		return;
	}

	node->add_time = time(NULL);
}

static void node_history_left(struct lockspace *ls, int nodeid,
			      struct change *cg)
{
	struct node *node;

	node = get_node_history(ls, nodeid);
	if (!node) {
		log_error("node_history_left no nodeid %d", nodeid);
		return;
	}

	node->add_time = 0;
	node->removed_seq = cg->seq;	/* for queries */
}

static void node_history_fail(struct lockspace *ls, int nodeid,
			      struct change *cg, int reason)
{
	struct node *node;

	node = get_node_history(ls, nodeid);
	if (!node) {
		log_error("node_history_fail no nodeid %d", nodeid);
		return;
	}

	if (cfgd_enable_fencing && !node->add_time)
		node->check_fencing = 1;

	/* fenced will take care of making sure the quorum value
	   is adjusted for all the failures */

	if (cfgd_enable_quorum && !cfgd_enable_fencing)
		node->check_quorum = 1;

	node->check_fs = 1;

	node->removed_seq = cg->seq;	/* for queries */
	node->failed_reason = reason;	/* for queries */
}

static int check_fencing_done(struct lockspace *ls)
{
	struct node *node;
	uint64_t last_fenced_time;
	int in_progress, wait_count = 0;
	int rv;

	if (!cfgd_enable_fencing)
		return 1;

	list_for_each_entry(node, &ls->node_history, list) {
		if (!node->check_fencing)
			continue;

		/* check with fenced to see if the node has been
		   fenced since node->add_time */

		rv = fence_node_time(node->nodeid, &last_fenced_time);
		if (rv < 0)
			log_error("fenced_node_info error %d", rv);

		if (last_fenced_time > node->add_time) {
			node->check_fencing = 0;
			node->add_time = 0;
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

	rv = fence_in_progress(&in_progress);
	if (rv < 0) {
		log_error("fenced_domain_info error %d", rv);
		return 0;
	}

	if (in_progress)
		return 0;
	return 1;
}

static int check_quorum_done(struct lockspace *ls)
{
	struct node *node;
	int wait_count = 0;

	if (!cfgd_enable_quorum)
		return 1;

	/* wait for quorum system (cman) to see all the same nodes failed, so
	   we know that cluster_quorate is adjusted for the same failures we've
	   seen (see comment in fenced about the assumption here) */

	list_for_each_entry(node, &ls->node_history, list) {
		if (!node->check_quorum)
			continue;

		if (!is_cluster_member(node->nodeid)) {
			node->check_quorum = 0;
		} else {
			log_group(ls, "check_quorum nodeid %d is_cluster_member",
				  node->nodeid);
			wait_count++;
		}
	}

	if (wait_count)
		return 0;

	if (!cluster_quorate) {
		log_group(ls, "check_quorum not quorate");
		return 0;
	}

	log_group(ls, "check_quorum done");
	return 1;
}

/* wait for local fs_controld to ack each failed node */

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

		if (node->fs_notified) {
			node->check_fs = 0;
		} else {
			log_group(ls, "check_fs nodeid %d needs fs notify",
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
		log_error("start_kernel cg %u not stopped", cg->seq);
		return;
	}

	log_group(ls, "start_kernel cg %u member_count %d",
		  cg->seq, cg->member_count);

	/* needs to happen before setting control which starts recovery */
	if (ls->joining)
		set_sysfs_id(ls->name, ls->global_id);

	format_member_ids(ls);
	format_renew_ids(ls);
	set_configfs_members(ls->name, member_count, member_ids,
			     renew_count, renew_ids);
	set_sysfs_control(ls->name, 1);
	ls->kernel_stopped = 0;

	if (ls->joining) {
		set_sysfs_event_done(ls->name, 0);
		ls->joining = 0;
	}
}

static void stop_kernel(struct lockspace *ls, uint32_t seq)
{
	if (!ls->kernel_stopped) {
		log_group(ls, "stop_kernel cg %u", seq);
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
		log_group(ls, "wait_messages need %d of %d", need, total);
		return 0;
	}

	log_group(ls, "wait_messages got all %d", total);
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

	ls->started_count++;
	if (!ls->started_count)
		ls->started_count++;

	cg->combined_seq = cg->seq; /* for queries */

	list_for_each_entry_safe(cg, safe, &ls->changes, list) {
		ls->started_change->combined_seq = cg->seq; /* for queries */
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

/* do the change details in the message match the details of the given change */

static int match_change(struct lockspace *ls, struct change *cg,
			struct dlm_header *hd, struct ls_info *li,
			struct id_info *ids)
{
	struct id_info *id;
	struct member *memb;
	uint32_t seq = hd->msgdata;
	int i, members_mismatch;

	/* We can ignore messages if we're not in the list of members.
	   The one known time this will happen is after we've joined
	   the cpg, we can get messages for changes prior to the change
	   in which we're added. */

	id = get_id_struct(ids, li->id_info_count, li->id_info_size,our_nodeid);

	if (!id) {
		log_group(ls, "match_change %d:%u skip %u we are not in members",
			  hd->nodeid, seq, cg->seq);
                return 0;
	}

	memb = find_memb(cg, hd->nodeid);
	if (!memb) {
		log_group(ls, "match_change %d:%u skip %u sender not member",
			  hd->nodeid, seq, cg->seq);
		return 0;
	}

	/* verify this is the right change by matching the counts
	   and the nodeids of the current members */

	if (li->member_count != cg->member_count ||
	    li->joined_count != cg->joined_count ||
	    li->remove_count != cg->remove_count ||
	    li->failed_count != cg->failed_count) {
		log_group(ls, "match_change %d:%u skip %u expect counts "
			  "%d %d %d %d", hd->nodeid, seq, cg->seq,
			  cg->member_count, cg->joined_count,
			  cg->remove_count, cg->failed_count);
		return 0;
	}

	members_mismatch = 0;
	id = ids;

	for (i = 0; i < li->id_info_count; i++) {
		memb = find_memb(cg, id->nodeid);
		if (!memb) {
			log_group(ls, "match_change %d:%u skip %u no memb %d",
			  	  hd->nodeid, seq, cg->seq, id->nodeid);
			members_mismatch = 1;
			break;
		}
		id = (struct id_info *)((char *)id + li->id_info_size);
	}
	if (members_mismatch)
		return 0;

	log_group(ls, "match_change %d:%u matches cg %u", hd->nodeid, seq,
		  cg->seq);
	return 1;
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
				  struct ls_info *li, struct id_info *ids)
{
	struct change *cg;

	list_for_each_entry_reverse(cg, &ls->changes, list) {
		if (!match_change(ls, cg, hd, li, ids))
			continue;
		return cg;
	}

	log_group(ls, "find_change %d:%u no match", hd->nodeid, hd->msgdata);
	return NULL;
}

static int is_added(struct lockspace *ls, int nodeid)
{
	struct change *cg;
	struct member *memb;

	list_for_each_entry(cg, &ls->changes, list) {
		memb = find_memb(cg, nodeid);
		if (memb && memb->added)
			return 1;
	}
	return 0;
}

static void receive_start(struct lockspace *ls, struct dlm_header *hd, int len)
{
	struct change *cg;
	struct member *memb;
	struct ls_info *li;
	struct id_info *ids;
	uint32_t seq = hd->msgdata;
	int added;

	log_group(ls, "receive_start %d:%u len %d", hd->nodeid, seq, len);

	li = (struct ls_info *)((char *)hd + sizeof(struct dlm_header));
	ids = (struct id_info *)((char *)li + sizeof(struct ls_info));

	ls_info_in(li);
	ids_in(li, ids);

	cg = find_change(ls, hd, li, ids);
	if (!cg)
		return;

	memb = find_memb(cg, hd->nodeid);
	if (!memb) {
		/* this should never happen since match_change checks it */
		log_error("receive_start no member %d", hd->nodeid);
		return;
	}

	memb->start_flags = hd->flags;

	added = is_added(ls, hd->nodeid);

	if (added && li->started_count) {
		log_error("receive_start %d:%u add node with started_count %u",
			  hd->nodeid, seq, li->started_count);

		/* see comment in fence/fenced/cpg.c */
		memb->disallowed = 1;
		return;
	}

	node_history_start(ls, hd->nodeid);
	memb->start = 1;
}

static void receive_plocks_stored(struct lockspace *ls, struct dlm_header *hd,
				  int len)
{
	struct ls_info *li;
	struct id_info *ids;

	log_group(ls, "receive_plocks_stored %d:%u need_plocks %d",
		  hd->nodeid, hd->msgdata, ls->need_plocks);

	if (!ls->need_plocks)
		return;

	/* a confchg arrived between the last start and the plocks_stored msg,
	   so we ignore this plocks_stored msg and wait to read the ckpt until
	   the next plocks_stored msg following the current start */
   
	if (!list_empty(&ls->changes) || !ls->started_change) {
		log_group(ls, "receive_plocks_stored %d:%u ignore",
			  hd->nodeid, hd->msgdata);
		return;
	}

	li = (struct ls_info *)((char *)hd + sizeof(struct dlm_header));
	ids = (struct id_info *)((char *)li + sizeof(struct ls_info));
	ls_info_in(li);
	ids_in(li, ids);

	if (!match_change(ls, ls->started_change, hd, li, ids)) {
		log_group(ls, "receive_plocks_stored %d:%u ignore no match",
			  hd->nodeid, hd->msgdata);
		return;
	}

	retrieve_plocks(ls);
	process_saved_plocks(ls);
	ls->need_plocks = 0;
	ls->save_plocks = 0;
}

static void send_info(struct lockspace *ls, int type)
{
	struct change *cg;
	struct dlm_header *hd;
	struct ls_info *li;
	struct id_info *id;
	struct member *memb;
	char *buf;
	int len, id_count;

	cg = list_first_entry(&ls->changes, struct change, list);

	id_count = cg->member_count;

	len = sizeof(struct dlm_header) + sizeof(struct ls_info) +
	      id_count * sizeof(struct id_info);

	buf = malloc(len);
	if (!buf) {
		log_error("send_info len %d no mem", len);
		return;
	}
	memset(buf, 0, len);

	hd = (struct dlm_header *)buf;
	li = (struct ls_info *)(buf + sizeof(*hd));
	id = (struct id_info *)(buf + sizeof(*hd) + sizeof(*li));

	/* fill in header (dlm_send_message handles part of header) */

	hd->type = type;
	hd->msgdata = cg->seq;
	if (ls->joining)
		hd->flags |= DLM_MFLG_JOINING;
	if (!ls->need_plocks)
		hd->flags |= DLM_MFLG_HAVEPLOCK;

	/* fill in ls_info */

	li->ls_info_size  = cpu_to_le32(sizeof(struct ls_info));
	li->id_info_size  = cpu_to_le32(sizeof(struct id_info));
	li->id_info_count = cpu_to_le32(id_count);
	li->started_count = cpu_to_le32(ls->started_count);
	li->member_count  = cpu_to_le32(cg->member_count);
	li->joined_count  = cpu_to_le32(cg->joined_count);
	li->remove_count  = cpu_to_le32(cg->remove_count);
	li->failed_count  = cpu_to_le32(cg->failed_count);

	/* fill in id_info entries */

	list_for_each_entry(memb, &cg->members, list) {
		id->nodeid = cpu_to_le32(memb->nodeid);
		id++;
	}

	log_group(ls, "send_%s cg %u flags %x counts %u %d %d %d %d",
		  type == DLM_MSG_START ? "start" : "plocks_stored",
		  cg->seq, hd->flags, ls->started_count, cg->member_count,
		  cg->joined_count, cg->remove_count, cg->failed_count);

	dlm_send_message(ls, buf, len);

	free(buf);
}

static void send_start(struct lockspace *ls)
{
	send_info(ls, DLM_MSG_START);
}

static void send_plocks_stored(struct lockspace *ls)
{
	send_info(ls, DLM_MSG_PLOCKS_STORED);
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
	cg->state = CGST_WAIT_CONDITIONS;
	cg->seq = ++ls->change_seq;
	if (!cg->seq)
		cg->seq = ++ls->change_seq;

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
			node_history_fail(ls, memb->nodeid, cg,
					  left_list[i].reason);
		else
			node_history_left(ls, memb->nodeid, cg);

		log_group(ls, "add_change cg %u remove nodeid %d reason %d",
			  cg->seq, memb->nodeid, left_list[i].reason);

		if (left_list[i].reason == CPG_REASON_PROCDOWN)
			kick_node_from_cluster(memb->nodeid);
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
			node_history_init(ls, memb->nodeid, cg);

		log_group(ls, "add_change cg %u joined nodeid %d", cg->seq,
			  memb->nodeid);
	}

	if (cg->we_joined) {
		log_group(ls, "add_change cg %u we joined", cg->seq);
		list_for_each_entry(memb, &cg->members, list)
			node_history_init(ls, memb->nodeid, cg);
	}

	log_group(ls, "add_change cg %u counts member %d joined %d remove %d "
		  "failed %d", cg->seq, cg->member_count, cg->joined_count,
		  cg->remove_count, cg->failed_count);

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

static void dlm_header_in(struct dlm_header *hd)
{
	hd->version[0]  = le16_to_cpu(hd->version[0]);
	hd->version[1]  = le16_to_cpu(hd->version[1]);
	hd->version[2]  = le16_to_cpu(hd->version[2]);
	hd->type        = le16_to_cpu(hd->type);
	hd->nodeid      = le32_to_cpu(hd->nodeid);
	hd->to_nodeid   = le32_to_cpu(hd->to_nodeid);
	hd->global_id   = le32_to_cpu(hd->global_id);
	hd->flags       = le32_to_cpu(hd->flags);
	hd->msgdata     = le32_to_cpu(hd->msgdata);
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

	if (len < sizeof(*hd)) {
		log_error("deliver_cb short message %d", len);
		return;
	}

	hd = (struct dlm_header *)data;
	dlm_header_in(hd);

	if (hd->version[0] != our_protocol.daemon_run[0] ||
	    hd->version[1] != our_protocol.daemon_run[1]) {
		log_error("reject message from %d version %u.%u.%u vs %u.%u.%u",
			  nodeid, hd->version[0], hd->version[1],
			  hd->version[2], our_protocol.daemon_run[0],
			  our_protocol.daemon_run[1],
			  our_protocol.daemon_run[2]);
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

	error = cpg_flow_control_state_get(daemon_cpg_handle,
					   &flow_control_state);
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
	sprintf(name.value, "dlm:ls:%s", ls->name);
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
	sprintf(name.value, "dlm:ls:%s", ls->name);
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

static struct node *get_node_daemon(int nodeid)
{
	struct node *node;

	list_for_each_entry(node, &daemon_nodes, list) {
		if (node->nodeid == nodeid)
			return node;
	}
	return NULL;
}

static void add_node_daemon(int nodeid)
{
	struct node *node;

	if (get_node_daemon(nodeid))
		return;

	node = malloc(sizeof(struct node));
	if (!node) {
		log_error("add_node_daemon no mem");
		return;
	}
	memset(node, 0, sizeof(struct node));
	node->nodeid = nodeid;
	list_add_tail(&node->list, &daemon_nodes);
}

static void pv_in(struct protocol_version *pv)
{
	pv->major = le16_to_cpu(pv->major);
	pv->minor = le16_to_cpu(pv->minor);
	pv->patch = le16_to_cpu(pv->patch);
	pv->flags = le16_to_cpu(pv->flags);
}

static void pv_out(struct protocol_version *pv)
{
	pv->major = cpu_to_le16(pv->major);
	pv->minor = cpu_to_le16(pv->minor);
	pv->patch = cpu_to_le16(pv->patch);
	pv->flags = cpu_to_le16(pv->flags);
}

static void protocol_in(struct protocol *proto)
{
	pv_in(&proto->dm_ver);
	pv_in(&proto->km_ver);
	pv_in(&proto->dr_ver);
	pv_in(&proto->kr_ver);
}

static void protocol_out(struct protocol *proto)
{
	pv_out(&proto->dm_ver);
	pv_out(&proto->km_ver);
	pv_out(&proto->dr_ver);
	pv_out(&proto->kr_ver);
}

/* go through member list saved in last confchg, see if we have received a
   proto message from each */

static int all_protocol_messages(void)
{
	struct node *node;
	int i;

	if (!daemon_member_count)
		return 0;

	for (i = 0; i < daemon_member_count; i++) {
		node = get_node_daemon(daemon_member[i].nodeid);
		if (!node) {
			log_error("all_protocol_messages no node %d",
				  daemon_member[i].nodeid);
			return 0;
		}

		if (!node->proto.daemon_max[0])
			return 0;
	}
	return 1;
}

static int pick_min_protocol(struct protocol *proto)
{
	uint16_t mind[4];
	uint16_t mink[4];
	struct node *node;
	int i;

	memset(&mind, 0, sizeof(mind));
	memset(&mink, 0, sizeof(mink));

	/* first choose the minimum major */

	for (i = 0; i < daemon_member_count; i++) {
		node = get_node_daemon(daemon_member[i].nodeid);
		if (!node) {
			log_error("pick_min_protocol no node %d",
				  daemon_member[i].nodeid);
			return -1;
		}

		if (!mind[0] || node->proto.daemon_max[0] < mind[0])
			mind[0] = node->proto.daemon_max[0];

		if (!mink[0] || node->proto.kernel_max[0] < mink[0])
			mink[0] = node->proto.kernel_max[0];
	}

	if (!mind[0] || !mink[0]) {
		log_error("pick_min_protocol zero major number");
		return -1;
	}

	/* second pick the minimum minor with the chosen major */

	for (i = 0; i < daemon_member_count; i++) {
		node = get_node_daemon(daemon_member[i].nodeid);
		if (!node)
			continue;

		if (mind[0] == node->proto.daemon_max[0]) {
			if (!mind[1] || node->proto.daemon_max[1] < mind[1])
				mind[1] = node->proto.daemon_max[1];
		}

		if (mink[0] == node->proto.kernel_max[0]) {
			if (!mink[1] || node->proto.kernel_max[1] < mink[1])
				mink[1] = node->proto.kernel_max[1];
		}
	}

	if (!mind[1] || !mink[1]) {
		log_error("pick_min_protocol zero minor number");
		return -1;
	}

	/* third pick the minimum patch with the chosen major.minor */

	for (i = 0; i < daemon_member_count; i++) {
		node = get_node_daemon(daemon_member[i].nodeid);
		if (!node)
			continue;

		if (mind[0] == node->proto.daemon_max[0] &&
		    mind[1] == node->proto.daemon_max[1]) {
			if (!mind[2] || node->proto.daemon_max[2] < mind[2])
				mind[2] = node->proto.daemon_max[2];
		}

		if (mink[0] == node->proto.kernel_max[0] &&
		    mink[1] == node->proto.kernel_max[1]) {
			if (!mink[2] || node->proto.kernel_max[2] < mink[2])
				mink[2] = node->proto.kernel_max[2];
		}
	}

	if (!mind[2] || !mink[2]) {
		log_error("pick_min_protocol zero patch number");
		return -1;
	}

	memcpy(&proto->daemon_run, &mind, sizeof(mind));
	memcpy(&proto->kernel_run, &mink, sizeof(mink));
	return 0;
}

static void receive_protocol(struct dlm_header *hd, int len)
{
	struct protocol *p;
	struct node *node;

	p = (struct protocol *)((char *)hd + sizeof(struct dlm_header));
	protocol_in(p);

	if (len < sizeof(struct dlm_header) + sizeof(struct protocol)) {
		log_error("receive_protocol invalid len %d from %d",
			  len, hd->nodeid);
		return;
	}

	/* zero is an invalid version value */

	if (!p->daemon_max[0] || !p->daemon_max[1] || !p->daemon_max[2] ||
	    !p->kernel_max[0] || !p->kernel_max[1] || !p->kernel_max[2]) {
		log_error("receive_protocol invalid max value from %d "
			  "daemon %u.%u.%u kernel %u.%u.%u", hd->nodeid,
			  p->daemon_max[0], p->daemon_max[1], p->daemon_max[2],
			  p->kernel_max[0], p->kernel_max[1], p->kernel_max[2]);
		return;
	}

	/* the run values will be zero until a version is set, after
	   which none of the run values can be zero */

	if (p->daemon_run[0] && (!p->daemon_run[1] || !p->daemon_run[2] ||
	    !p->kernel_run[0] || !p->kernel_run[1] || !p->kernel_run[2])) {
		log_error("receive_protocol invalid run value from %d "
			  "daemon %u.%u.%u kernel %u.%u.%u", hd->nodeid,
			  p->daemon_run[0], p->daemon_run[1], p->daemon_run[2],
			  p->kernel_run[0], p->kernel_run[1], p->kernel_run[2]);
		return;
	}

	/* if we have zero run values, and this msg has non-zero run values,
	   then adopt them as ours; otherwise save this proto message */

	if (our_protocol.daemon_run[0])
		return;

	if (p->daemon_run[0]) {
		memcpy(&our_protocol.daemon_run, &p->daemon_run,
		       sizeof(struct protocol_version));
		memcpy(&our_protocol.kernel_run, &p->kernel_run,
		       sizeof(struct protocol_version));
		log_debug("run protocol from nodeid %d", hd->nodeid);
		return;
	}

	/* save this node's proto so we can tell when we've got all, and
	   use it to select a minimum protocol from all */

	node = get_node_daemon(hd->nodeid);
	if (!node) {
		log_error("receive_protocol no node %d", hd->nodeid);
		return;
	}
	memcpy(&node->proto, p, sizeof(struct protocol));
}

static void send_protocol(struct protocol *proto)
{
	struct dlm_header *hd;
	struct protocol *pr;
	char *buf;
	int len;

	len = sizeof(struct dlm_header) + sizeof(struct protocol);
	buf = malloc(len);
	if (!buf) {
		log_error("send_protocol no mem %d", len);
		return;
	}
	memset(buf, 0, len);

	hd = (struct dlm_header *)buf;
	pr = (struct protocol *)(buf + sizeof(*hd));

	hd->type = cpu_to_le16(DLM_MSG_PROTOCOL);
	hd->nodeid = cpu_to_le32(our_nodeid);

	memcpy(pr, proto, sizeof(struct protocol));
	protocol_out(pr);

	_send_message(daemon_cpg_handle, buf, len, DLM_MSG_PROTOCOL);
}

int set_protocol(void)
{
	struct protocol proto;
	struct pollfd pollfd;
	int sent_proposal = 0;
	int rv;

	memset(&pollfd, 0, sizeof(pollfd));
	pollfd.fd = daemon_cpg_fd;
	pollfd.events = POLLIN;

	while (1) {
		if (our_protocol.daemon_run[0])
			break;

		if (!sent_proposal && all_protocol_messages()) {
			/* propose a protocol; look through info from all
			   nodes and pick the min for both daemon and kernel,
			   and propose that */

			sent_proposal = 1;

			/* copy our max values */
			memcpy(&proto, &our_protocol, sizeof(struct protocol));

			rv = pick_min_protocol(&proto);
			if (rv < 0)
				return rv;

			log_debug("set_protocol member_count %d propose "
				  "daemon %u.%u.%u kernel %u.%u.%u",
				  daemon_member_count,
				  proto.daemon_run[0], proto.daemon_run[1],
				  proto.daemon_run[2], proto.kernel_run[0],
				  proto.kernel_run[1], proto.kernel_run[2]);

			send_protocol(&proto);
		}

		/* only process messages/events from daemon cpg until protocol
		   is established */

		rv = poll(&pollfd, 1, -1);
		if (rv == -1 && errno == EINTR) {
			if (daemon_quit)
				return -1;
			continue;
		}
		if (rv < 0) {
			log_error("set_protocol poll errno %d", errno);
			return -1;
		}

		if (pollfd.revents & POLLIN)
			process_cpg(0);
		if (pollfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
			log_error("set_protocol poll revents %u",
				  pollfd.revents);
			return -1;
		}
	}

	if (our_protocol.daemon_run[0] != our_protocol.daemon_max[0] ||
	    our_protocol.daemon_run[1] > our_protocol.daemon_max[1]) {
		log_error("incompatible daemon protocol run %u.%u.%u max %u.%u.%u",
			our_protocol.daemon_run[0],
			our_protocol.daemon_run[1],
			our_protocol.daemon_run[2],
			our_protocol.daemon_max[0],
			our_protocol.daemon_max[1],
			our_protocol.daemon_max[2]);
		return -1;
	}

	if (our_protocol.kernel_run[0] != our_protocol.kernel_max[0] ||
	    our_protocol.kernel_run[1] > our_protocol.kernel_max[1]) {
		log_error("incompatible kernel protocol run %u.%u.%u max %u.%u.%u",
			our_protocol.kernel_run[0],
			our_protocol.kernel_run[1],
			our_protocol.kernel_run[2],
			our_protocol.kernel_max[0],
			our_protocol.kernel_max[1],
			our_protocol.kernel_max[2]);
		return -1;
	}

	log_debug("daemon run %u.%u.%u max %u.%u.%u "
		  "kernel run %u.%u.%u max %u.%u.%u",
		  our_protocol.daemon_run[0],
		  our_protocol.daemon_run[1],
		  our_protocol.daemon_run[2],
		  our_protocol.daemon_max[0],
		  our_protocol.daemon_max[1],
		  our_protocol.daemon_max[2],
		  our_protocol.kernel_run[0],
		  our_protocol.kernel_run[1],
		  our_protocol.kernel_run[2],
		  our_protocol.kernel_max[0],
		  our_protocol.kernel_max[1],
		  our_protocol.kernel_max[2]);
	return 0;
}

static void deliver_cb_daemon(cpg_handle_t handle, struct cpg_name *group_name,
		uint32_t nodeid, uint32_t pid, void *data, int len)
{
	struct dlm_header *hd;

	if (len < sizeof(*hd)) {
		log_error("deliver_cb short message %d", len);
		return;
	}

	hd = (struct dlm_header *)data;
	dlm_header_in(hd);

	switch (hd->type) {
	case DLM_MSG_PROTOCOL:
		receive_protocol(hd, len);
		break;
	default:
		log_error("deliver_cb_daemon unknown msg type %d", hd->type);
	}
}

static void confchg_cb_daemon(cpg_handle_t handle, struct cpg_name *group_name,
		struct cpg_address *member_list, int member_list_entries,
		struct cpg_address *left_list, int left_list_entries,
		struct cpg_address *joined_list, int joined_list_entries)
{
	int i;

	if (joined_list_entries)
		send_protocol(&our_protocol);

	memset(&daemon_member, 0, sizeof(daemon_member));
	daemon_member_count = member_list_entries;

	for (i = 0; i < member_list_entries; i++) {
		daemon_member[i] = member_list[i];
		add_node_daemon(member_list[i].nodeid);
	}
}

static cpg_callbacks_t cpg_callbacks_daemon = {
	.cpg_deliver_fn = deliver_cb_daemon,
	.cpg_confchg_fn = confchg_cb_daemon,
};

void process_cpg(int ci)
{
	cpg_error_t error;

	error = cpg_dispatch(daemon_cpg_handle, CPG_DISPATCH_ALL);
	if (error != CPG_OK)
		log_error("daemon cpg_dispatch error %d", error);
}

int setup_cpg(void)
{
	cpg_error_t error;
	struct cpg_name name;
	int i = 0;

	INIT_LIST_HEAD(&daemon_nodes);

	memset(&our_protocol, 0, sizeof(our_protocol));
	our_protocol.daemon_max[0] = 1;
	our_protocol.daemon_max[1] = 1;
	our_protocol.daemon_max[2] = 1;
	our_protocol.kernel_max[0] = 1;
	our_protocol.kernel_max[1] = 1;
	our_protocol.kernel_max[2] = 1;

	error = cpg_initialize(&daemon_cpg_handle, &cpg_callbacks_daemon);
	if (error != CPG_OK) {
		log_error("daemon cpg_initialize error %d", error);
		return -1;
	}

	cpg_fd_get(daemon_cpg_handle, &daemon_cpg_fd);

	memset(&name, 0, sizeof(name));
	sprintf(name.value, "dlm:controld");
	name.length = strlen(name.value) + 1;

 retry:
	error = cpg_join(daemon_cpg_handle, &name);
	if (error == CPG_ERR_TRY_AGAIN) {
		sleep(1);
		if (!(++i % 10))
			log_error("daemon cpg_join error retrying");
		goto retry;
	}
	if (error != CPG_OK) {
		log_error("daemon cpg_join error %d", error);
		goto fail;
	}

	log_debug("setup_cpg %d", daemon_cpg_fd);
	return daemon_cpg_fd;

 fail:
	cpg_finalize(daemon_cpg_handle);
	return -1;
}

/* fs_controld has seen nodedown for nodeid; it's now ok for dlm to do
   recovery for the failed node */

int set_fs_notified(struct lockspace *ls, int nodeid)
{
	struct node *node;

	/* this shouldn't happen */
	node = get_node_history(ls, nodeid);
	if (!node) {
		log_error("set_fs_notified no nodeid %d", nodeid);
		return -ESRCH;
	}

	/* this can happen, we haven't seen a nodedown for this node yet,
	   but we should soon */
	if (!node->check_fs) {
		log_group(ls, "set_fs_notified %d zero check_fs", nodeid);
		return -EAGAIN;
	}

	node->fs_notified = 1;
	return 0;
}

int set_lockspace_info(struct lockspace *ls, struct dlmc_lockspace *lockspace)
{
	struct change *cg, *last = NULL;

	strncpy(lockspace->name, ls->name, DLM_LOCKSPACE_LEN);
	lockspace->global_id = ls->global_id;

	if (ls->joining)
		lockspace->flags |= DLMC_LF_JOINING;
	if (ls->leaving)
		lockspace->flags |= DLMC_LF_LEAVING;
	if (ls->kernel_stopped)
		lockspace->flags |= DLMC_LF_KERNEL_STOPPED;
	if (ls->fs_registered)
		lockspace->flags |= DLMC_LF_FS_REGISTERED;
	if (ls->need_plocks)
		lockspace->flags |= DLMC_LF_NEED_PLOCKS;
	if (ls->save_plocks)
		lockspace->flags |= DLMC_LF_SAVE_PLOCKS;

	if (!ls->started_change)
		goto next;

	cg = ls->started_change;

	lockspace->cg_prev.member_count = cg->member_count;
	lockspace->cg_prev.joined_count = cg->joined_count;
	lockspace->cg_prev.remove_count = cg->remove_count;
	lockspace->cg_prev.failed_count = cg->failed_count;
	lockspace->cg_prev.combined_seq = cg->combined_seq;
	lockspace->cg_prev.seq = cg->seq;

 next:
	if (list_empty(&ls->changes))
		goto out;

	list_for_each_entry(cg, &ls->changes, list)
		last = cg;

	cg = list_first_entry(&ls->changes, struct change, list);

	lockspace->cg_next.member_count = cg->member_count;
	lockspace->cg_next.joined_count = cg->joined_count;
	lockspace->cg_next.remove_count = cg->remove_count;
	lockspace->cg_next.failed_count = cg->failed_count;
	lockspace->cg_next.combined_seq = last->seq;
	lockspace->cg_next.seq = cg->seq;

	if (cg->state == CGST_WAIT_CONDITIONS)
		lockspace->cg_next.wait_condition = 4;
	if (poll_fencing)
		lockspace->cg_next.wait_condition = 1;
	else if (poll_quorum)
		lockspace->cg_next.wait_condition = 2;
	else if (poll_fs)
		lockspace->cg_next.wait_condition = 3;

	if (cg->state == CGST_WAIT_MESSAGES)
		lockspace->cg_next.wait_messages = 1;
 out:
	return 0;
}

static int _set_node_info(struct lockspace *ls, struct change *cg, int nodeid,
			  struct dlmc_node *node)
{
	struct member *m = NULL;
	struct node *n;

	node->nodeid = nodeid;

	if (cg)
		m = find_memb(cg, nodeid);
	if (!m)
		goto history;

	node->flags |= DLMC_NF_MEMBER;

	if (m->start)
		node->flags |= DLMC_NF_START;
	if (m->disallowed)
		node->flags |= DLMC_NF_DISALLOWED;

 history:
	n = get_node_history(ls, nodeid);
	if (!n)
		goto out;

	if (n->check_fencing)
		node->flags |= DLMC_NF_CHECK_FENCING;
	if (n->check_quorum)
		node->flags |= DLMC_NF_CHECK_QUORUM;
	if (n->check_fs)
		node->flags |= DLMC_NF_CHECK_FS;
	if (n->fs_notified)
		node->flags |= DLMC_NF_FS_NOTIFIED;

	node->added_seq = n->added_seq;
	node->removed_seq = n->removed_seq;
	node->failed_reason = n->failed_reason;
 out:
	return 0;
}

int set_node_info(struct lockspace *ls, int nodeid, struct dlmc_node *node)
{
	struct change *cg;

	if (!list_empty(&ls->changes)) {
		cg = list_first_entry(&ls->changes, struct change, list);
		return _set_node_info(ls, cg, nodeid, node);
	}

	return _set_node_info(ls, ls->started_change, nodeid, node);
}

int set_lockspaces(int *count, struct dlmc_lockspace **lss_out)
{
	struct lockspace *ls;
	struct dlmc_lockspace *lss, *lsp;
	int ls_count = 0;

	list_for_each_entry(ls, &lockspaces, list)
		ls_count++;

	lss = malloc(ls_count * sizeof(struct dlmc_lockspace));
	if (!lss)
		return -ENOMEM;
	memset(lss, 0, ls_count * sizeof(struct dlmc_lockspace));

	lsp = lss;
	list_for_each_entry(ls, &lockspaces, list) {
		set_lockspace_info(ls, lsp++);
	}

	*count = ls_count;
	*lss_out = lss;
	return 0;
}

int set_lockspace_nodes(struct lockspace *ls, int option, int *node_count,
                        struct dlmc_node **nodes_out)
{
	struct change *cg;
	struct node *n;
	struct dlmc_node *nodes = NULL, *nodep;
	struct member *memb;
	int count = 0;

	if (option == DLMC_NODES_ALL) {
		if (!list_empty(&ls->changes))
			cg = list_first_entry(&ls->changes, struct change,list);
		else
			cg = ls->started_change;

		list_for_each_entry(n, &ls->node_history, list)
			count++;

	} else if (option == DLMC_NODES_MEMBERS) {
		if (!ls->started_change)
			goto out;
		cg = ls->started_change;
		count = cg->member_count;

	} else if (option == DLMC_NODES_NEXT) {
		if (list_empty(&ls->changes))
			goto out;
		cg = list_first_entry(&ls->changes, struct change, list);
		count = cg->member_count;
	} else
		goto out;

	nodes = malloc(count * sizeof(struct dlmc_node));
	if (!nodes)
		return -ENOMEM;
	memset(nodes, 0, count * sizeof(struct dlmc_node));
	nodep = nodes;

	if (option == DLMC_NODES_ALL) {
		list_for_each_entry(n, &ls->node_history, list)
			_set_node_info(ls, cg, n->nodeid, nodep++);
	} else {
		list_for_each_entry(memb, &cg->members, list)
			_set_node_info(ls, cg, memb->nodeid, nodep++);
	}
 out:
	*node_count = count;
	*nodes_out = nodes;
	return 0;
}

