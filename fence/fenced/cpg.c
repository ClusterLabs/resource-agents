#include "fd.h"
#include "config.h"

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
		struct protocol_version dr_ver;
		uint16_t                daemon_run[4];
	};
};

struct node_daemon {
	struct list_head list;
	int nodeid;
	struct protocol proto;
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

/* fd_info and id_info: for syncing state in start message */

struct fd_info {
	uint32_t fd_info_size;
	uint32_t id_info_size;
	uint32_t id_info_count;

	uint32_t started_count;

	int member_count;
	int joined_count;
	int remove_count;
	int failed_count;
};

#define IDI_NODEID_IS_MEMBER    0x00000001

struct id_info {
	int nodeid;
	uint32_t flags;

	/* the following syncs info to make queries useful from all nodes */

	int fence_external_node;
	int fence_master;
	int fence_how;
	uint64_t fence_time;
	uint64_t fence_external_time;
};

static cpg_handle_t cpg_handle_daemon;
static int daemon_cpg_fd;
static struct protocol our_protocol;
static struct list_head daemon_nodes;
static struct cpg_address daemon_member[MAX_NODES];
static int daemon_member_count;

static void fd_info_in(struct fd_info *fi)
{
	fi->fd_info_size  = le32_to_cpu(fi->fd_info_size);
	fi->id_info_size  = le32_to_cpu(fi->id_info_size);
	fi->id_info_count = le32_to_cpu(fi->id_info_count);
	fi->started_count = le32_to_cpu(fi->started_count);
	fi->member_count  = le32_to_cpu(fi->member_count);
	fi->joined_count  = le32_to_cpu(fi->joined_count);
	fi->remove_count  = le32_to_cpu(fi->remove_count);
	fi->failed_count  = le32_to_cpu(fi->failed_count);
}

static void id_info_in(struct id_info *id)
{
	id->nodeid              = le32_to_cpu(id->nodeid);
	id->flags               = le32_to_cpu(id->flags);
	id->fence_external_node = le32_to_cpu(id->fence_external_node);
	id->fence_master        = le32_to_cpu(id->fence_master);
	id->fence_how           = le32_to_cpu(id->fence_how);
	id->fence_time          = le64_to_cpu(id->fence_time);
	id->fence_external_time = le64_to_cpu(id->fence_external_time);
}

static void ids_in(struct fd_info *fi, struct id_info *ids)
{
	struct id_info *id;
	int i;

	id = ids;
	for (i = 0; i < fi->id_info_count; i++) {
		id_info_in(id);
		id = (struct id_info *)((char *)id + fi->id_info_size);
        }
}

static char *msg_name(int type)
{
	switch (type) {
	case FD_MSG_START:
		return "start";
	case FD_MSG_VICTIM_DONE:
		return "victim_done";
	case FD_MSG_COMPLETE:
		return "complete";
	case FD_MSG_EXTERNAL:
		return "external";
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

static void fd_send_message(struct fd *fd, char *buf, int len)
{
	struct fd_header *hd = (struct fd_header *) buf;
	int type = hd->type;

	hd->version[0]  = cpu_to_le16(our_protocol.daemon_run[0]);
	hd->version[1]  = cpu_to_le16(our_protocol.daemon_run[1]);
	hd->version[2]  = cpu_to_le16(our_protocol.daemon_run[2]);
	hd->type	= cpu_to_le16(hd->type);
	hd->nodeid      = cpu_to_le32(our_nodeid);
	hd->to_nodeid   = cpu_to_le32(hd->to_nodeid);
	hd->flags       = cpu_to_le32(hd->flags);
	hd->msgdata     = cpu_to_le32(hd->msgdata);

	_send_message(fd->cpg_handle, buf, len, type);
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

static struct fd *find_fd_handle(cpg_handle_t h)
{
	struct fd *fd;

	list_for_each_entry(fd, &domains, list) {
		if (fd->cpg_handle == h)
			return fd;
	}
	return NULL;
}

static struct fd *find_fd_ci(int ci)
{
	struct fd *fd;

	list_for_each_entry(fd, &domains, list) {
		if (fd->cpg_client == ci)
			return fd;
	}
	return NULL;
}

void free_cg(struct change *cg)
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

static struct node_history *get_node_history(struct fd *fd, int nodeid)
{
	struct node_history *node;

	list_for_each_entry(node, &fd->node_history, list) {
		if (node->nodeid == nodeid)
			return node;
	}
	return NULL;
}

static void node_history_init(struct fd *fd, int nodeid)
{
	struct node_history *node;

	node = get_node_history(fd, nodeid);
	if (node)
		return;

	node = malloc(sizeof(struct node_history));
	if (!node)
		return;
	memset(node, 0, sizeof(struct node_history));

	node->nodeid = nodeid;
	list_add_tail(&node->list, &fd->node_history);
}

static void node_history_start(struct fd *fd, int nodeid)
{
	struct node_history *node;
	
	node = get_node_history(fd, nodeid);
	if (!node) {
		log_error("node_history_start no nodeid %d", nodeid);
		return;
	}

	node->add_time = time(NULL);
}

static void node_history_left(struct fd *fd, int nodeid)
{
	struct node_history *node;

	node = get_node_history(fd, nodeid);
	if (!node) {
		log_error("node_history_left no nodeid %d", nodeid);
		return;
	}

	node->left_time = time(NULL);
}

static void node_history_fail(struct fd *fd, int nodeid)
{
	struct node_history *node;

	node = get_node_history(fd, nodeid);
	if (!node) {
		log_error("node_history_fail no nodeid %d", nodeid);
		return;
	}

	node->fail_time = time(NULL);

	node->check_quorum = 1;
}

/* The master node updates this info when it fences the victim, the other
   domain members update it when they receive the status message from the
   master. */

void node_history_fence(struct fd *fd, int victim, int master, int how,
			uint64_t mastertime)
{
	struct node_history *node;

	node = get_node_history(fd, victim);
	if (!node) {
		log_error("node_history_fence no nodeid %d", victim);
		return;
	}

	node->fence_master = master;
	node->fence_time = mastertime;
	node->fence_how = how;
}

/* When the fence_node command is run on a machine, it will first call
   libfence:fence_node(victim) to do the fencing.  Afterward, it should call
   libfenced:fence_external(victim) to tell fenced what it's done, so fenced
   can avoid fencing the node a second time.  This will result in a message
   being sent to all domain members which will update their node_history entry
   for the victim.  The recover.c:fence_victims() code can check whether
   a victim has been externally fenced since the last add_time, and if so
   skip the fencing.  This won't always work perfectly; a node might in some
   circumstances be fenced a second time by fenced. */

static void node_history_fence_external(struct fd *fd, int nodeid, int from)
{
	struct node_history *node;

	node = get_node_history(fd, nodeid);
	if (!node) {
		log_error("node_history_fence_external no nodeid %d", nodeid);
		return;
	}

	node->fence_external_time = time(NULL);
	node->fence_external_node = from;
}

static void save_history(struct fd *fd, struct fd_info *fi, struct id_info *ids)
{
	struct node_history *node;
	struct id_info *id;
	int i;

	id = ids;

	for (i = 0; i < fi->id_info_count; i++) {
		/* create history entries for nodes that were domain members
		   prior to our joining the domain */
		node_history_init(fd, id->nodeid);

		node = get_node_history(fd, id->nodeid);
		if (!node) {
			log_error("save_history no nodeid %d", id->nodeid);
			goto next;
		}

		if (!node->fence_time && id->fence_time) {
			node->fence_master = id->fence_master;
			node->fence_time = id->fence_time;
			node->fence_how = id->fence_how;
			log_debug("save_history %d master %d time %llu how %d",
				  node->nodeid, node->fence_master,
				  (unsigned long long)node->fence_time,
				  node->fence_how);
		}

		if (!node->fence_external_time && id->fence_external_time) {
			node->fence_external_time = id->fence_external_time;
			node->fence_external_node = id->fence_external_node;
			log_debug("save_history %d ext node %d ext time %llu",
				  node->nodeid, node->fence_external_node,
				  (unsigned long long)node->fence_external_time);
		}
 next:
		id = (struct id_info *)((char *)id + fi->id_info_size);
	}
}

/* call this from libfenced:fenced_external() */

void send_external(struct fd *fd, int victim)
{
	struct fd_header *hd;
	char *buf;
	int len;

	len = sizeof(struct fd_header);

	buf = malloc(len);
	if (!buf) {
		log_error("send_external no mem len %d", len);
		return;
	}
	memset(buf, 0, len);

	hd = (struct fd_header *)buf;
	hd->type = FD_MSG_EXTERNAL;
	hd->msgdata = victim;

	log_debug("send_external victim nodeid %u", victim);

	fd_send_message(fd, buf, len);

	free(buf);
}

/* now, if the victim dies and the fence domain sees it fail,
   it will be added as an fd victim, but fence_victims() will
   call is_fenced_external() which will see that it's already
   fenced and bypass fencing it again */

static void receive_external(struct fd *fd, struct fd_header *hd, int len)
{
	log_debug("receive_external from %d len %d victim nodeid %d",
		  hd->nodeid, len, hd->msgdata);

	node_history_fence_external(fd, hd->msgdata, hd->nodeid);
}

int is_fenced_external(struct fd *fd, int nodeid)
{
	struct node_history *node;

	node = get_node_history(fd, nodeid);
	if (!node) {
		log_error("is_fenced_external no nodeid %d", nodeid);
		return 0;
	}

	if (node->fence_external_time > node->add_time)
		return 1;
	return 0;
}

/* completed victim must be removed from victims list before calling this
   because we count the number of entries on the victims list for remaining */

void send_victim_done(struct fd *fd, int victim)
{
	struct change *cg = list_first_entry(&fd->changes, struct change, list);
	struct fd_header *hd;
	struct id_info *id;
	struct node_history *node;
	char *buf;
	int len;

	len = sizeof(struct fd_header) + sizeof(struct id_info);

	buf = malloc(len);
	if (!buf) {
		log_error("send_victim_done no mem len %d", len);
		return;
	}
	memset(buf, 0, len);

	hd = (struct fd_header *)buf;
	hd->type = FD_MSG_VICTIM_DONE;
	hd->msgdata = cg->seq;

	if (fd->init_complete)
		hd->flags |= FD_MFLG_COMPLETE;

	node = get_node_history(fd, victim);
	if (!node) {
		log_error("send_victim_done no nodeid %d", victim);
		return;
	}

	id = (struct id_info *)(buf + sizeof(struct fd_header));
	id->nodeid       = cpu_to_le32(victim);
	id->fence_master = cpu_to_le32(our_nodeid);
	id->fence_time   = cpu_to_le64(node->fence_time);
	id->fence_how    = cpu_to_le32(node->fence_how);

	log_debug("send_victim_done cg %u flags %x victim nodeid %d",
		  cg->seq, hd->flags, victim);

	fd_send_message(fd, buf, len);

	free(buf);
}

static void receive_victim_done(struct fd *fd, struct fd_header *hd, int len)
{
	struct node *node;
	uint32_t seq = hd->msgdata;
	int found;
	struct id_info *id;

	log_debug("receive_victim_done %d:%u flags %x len %d", hd->nodeid, seq,
		  hd->flags, len);

	/* check that hd->nodeids is fd->master ? */

	/* I don't think there's any problem with the master removing the
	   victim when it's done instead of waiting to remove it when it
	   receives its own victim_done message, like the other nodes do */

	if (hd->nodeid == our_nodeid)
		return;

	id = (struct id_info *)((char *)hd + sizeof(struct fd_header));
	id_info_in(id);

	found = 0;
	list_for_each_entry(node, &fd->victims, list) {
		if (node->nodeid == id->nodeid) {
			log_debug("receive_victim_done remove nodeid %d how %d",
				  id->nodeid, id->fence_how);
			node_history_fence(fd, id->nodeid, id->fence_master,
					   id->fence_how, id->fence_time);
			list_del(&node->list);
			free(node);
			found = 1;
			break;
		}
	}

	if (!found)
		log_debug("receive_victim_done no nodeid %d from %d",
			  id->nodeid, hd->nodeid);
}

static int check_quorum_done(struct fd *fd)
{
	struct node_history *node;
	int wait_count = 0;

	/* We don't want to trust the cman_quorate value until we know
	   that cman has seen the same nodes fail that we have.  So, we
	   first make sure that all nodes we've seen fail are also
	   failed in cman, then we can just check cman_quorate.  This
	   assumes that we'll get to this function to do all the checks
	   before any of the failed nodes can actually rejoin and become
	   cman members again (if that assumption doesn't hold, perhaps
	   do something with timestamps of join/fail). */

	list_for_each_entry(node, &fd->node_history, list) {
		if (!node->check_quorum)
			continue;

		if (!is_cman_member(node->nodeid)) {
			node->check_quorum = 0;
		} else {
			log_debug("check_quorum %d is_cman_member",
				  node->nodeid);
			wait_count++;
		}
	}

	if (wait_count)
		return 0;

	if (!cman_quorate) {
		log_debug("check_quorum not quorate");
		return 0;
	}

	log_debug("check_quorum done");
	return 1;
}

static int wait_conditions_done(struct fd *fd)
{
	if (!check_quorum_done(fd))
		return 0;
	return 1;
}

static int wait_messages_done(struct fd *fd)
{
	struct change *cg = list_first_entry(&fd->changes, struct change, list);
	struct member *memb;
	int need = 0, total = 0;

	list_for_each_entry(memb, &cg->members, list) {
		if (!memb->start)
			need++;
		total++;
	}

	if (need) {
		log_debug("wait_messages need %d of %d", need, total);
		return 0;
	}

	log_debug("wait_messages got all %d", total);
	return 1;
}

static void cleanup_changes(struct fd *fd)
{
	struct change *cg = list_first_entry(&fd->changes, struct change, list);
	struct change *safe;

	list_del(&cg->list);
	if (fd->started_change)
		free_cg(fd->started_change);
	fd->started_change = cg;

	/* zero started_count means "never started" */

	fd->started_count++;
	if (!fd->started_count)
		fd->started_count++;

	list_for_each_entry_safe(cg, safe, &fd->changes, list) {
		list_del(&cg->list);
		free_cg(cg);
	}
}

static void set_master(struct fd *fd)
{
	struct change *cg = list_first_entry(&fd->changes, struct change, list);
	struct member *memb;
	int low = 0, complete = 0;

	list_for_each_entry(memb, &cg->members, list) {
		if (!low || memb->nodeid < low)
			low = memb->nodeid;

		if (!(memb->start_flags & FD_MFLG_COMPLETE))
			continue;

		if (!complete || memb->nodeid < complete)
			complete = memb->nodeid;
	}

	log_debug("set_master from %d to %s node %d", fd->master,
		  complete ? "complete" : "low",
		  complete ? complete : low);

	fd->master = complete ? complete : low;
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

static int match_change(struct fd *fd, struct change *cg, struct fd_header *hd,
			struct fd_info *fi, struct id_info *ids)
{
	struct id_info *id;
	struct member *memb;
	uint32_t seq = hd->msgdata;
	int i, members_mismatch;

	/* We can ignore messages if we're not in the list of members.
	   The one known time this will happen is after we've joined
	   the cpg, we can get messages for changes prior to the change
	   in which we're added. */

	id = get_id_struct(ids, fi->id_info_count, fi->id_info_size,our_nodeid);

	if (!id || !(id->flags & IDI_NODEID_IS_MEMBER)) {
		log_debug("match_change %d:%u skip cg %u we are not in members",
			  hd->nodeid, seq, cg->seq);
		return 0;
	}

	memb = find_memb(cg, hd->nodeid);
	if (!memb) {
		log_debug("match_change %d:%u skip cg %u sender not member",
			  hd->nodeid, seq, cg->seq);
		return 0;
	}

	/* verify this is the right change by matching the counts
	   and the nodeids of the current members */

	if (fi->member_count != cg->member_count ||
	    fi->joined_count != cg->joined_count ||
	    fi->remove_count != cg->remove_count ||
	    fi->failed_count != cg->failed_count) {
		log_debug("match_change %d:%u skip cg %u expect counts "
			  "%d %d %d %d", hd->nodeid, seq, cg->seq,
			  cg->member_count, cg->joined_count,
			  cg->remove_count, cg->failed_count);
		return 0;
	}

	members_mismatch = 0;
	id = ids;

	for (i = 0; i < fi->id_info_count; i++) {
		if (id->flags & IDI_NODEID_IS_MEMBER) {
			memb = find_memb(cg, id->nodeid);
			if (!memb) {
				log_debug("match_change %d:%u skip cg %u "
					  "no memb %d", hd->nodeid, seq,
					  cg->seq, id->nodeid);
				members_mismatch = 1;
				break;
			}
		}
		id = (struct id_info *)((char *)id + fi->id_info_size);
	}

	if (members_mismatch)
		return 0;

	log_debug("match_change %d:%u matches cg %u", hd->nodeid, seq, cg->seq);
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

static struct change *find_change(struct fd *fd, struct fd_header *hd,
				  struct fd_info *fi, struct id_info *ids)
{
	struct change *cg;

	list_for_each_entry_reverse(cg, &fd->changes, list) {
		if (!match_change(fd, cg, hd, fi, ids))
			continue;
		return cg;
	}

	log_debug("find_change %d:%u no match", hd->nodeid, hd->msgdata);
	return NULL;
}

static int is_added(struct fd *fd, int nodeid)
{
	struct change *cg;
	struct member *memb;

	list_for_each_entry(cg, &fd->changes, list) {
		memb = find_memb(cg, nodeid);
		if (memb && memb->added)
			return 1;
	}
	return 0;
}

static void receive_start(struct fd *fd, struct fd_header *hd, int len)
{
	struct change *cg;
	struct member *memb;
	struct fd_info *fi;
	struct id_info *ids;
	uint32_t seq = hd->msgdata;
	int added;

	log_debug("receive_start %d:%u len %d", hd->nodeid, seq, len);

	fi = (struct fd_info *)((char *)hd + sizeof(struct fd_header));
	ids = (struct id_info *)((char *)fi + sizeof(struct fd_info));

	fd_info_in(fi);
	ids_in(fi, ids);

	cg = find_change(fd, hd, fi, ids);
	if (!cg)
		return;

	memb = find_memb(cg, hd->nodeid);
	if (!memb) {
		/* this should never happen since match_change checks it */
		log_error("receive_start no member %d", hd->nodeid);
		return;
	}

	memb->start_flags = hd->flags;

	added = is_added(fd, hd->nodeid);

	if (added && fi->started_count) {
		log_error("receive_start %d:%u add node with started_count %u",
			  hd->nodeid, seq, fi->started_count);

		/* This is how we deal with cpg's that are partitioned and
		   then merge back together.  When the merge happens, the
		   cpg on each side will see nodes from the other side being
		   added, and neither side will have zero started_count.  So,
		   both sides will ignore start messages from the other side.
		   This causes the the domain on each side to continue waiting
		   for the missing start messages indefinately.  To unblock
		   things, all nodes from one side of the former partition
		   need to fail. */

		/* This method of detecting a merge of a partitioned cpg
		   assumes a joining node won't ever see an existing node
		   as "added" under normal circumstances. */

		memb->disallowed = 1;
		return;
	}

	node_history_start(fd, hd->nodeid);
	memb->start = 1;

	/* save any fencing history from this message that we don't have */
	save_history(fd, fi, ids);
}

static void receive_complete(struct fd *fd, struct fd_header *hd, int len)
{
	struct fd_info *fi;
	struct id_info *ids, *id;
	uint32_t seq = hd->msgdata;
	struct node *node, *safe;

	log_debug("receive_complete %d:%u len %d", hd->nodeid, seq, len);

	if (fd->init_complete)
		return;

	fi = (struct fd_info *)((char *)hd + sizeof(struct fd_header));
	ids = (struct id_info *)((char *)fi + sizeof(struct fd_info));

	fd_info_in(fi);
	ids_in(fi, ids);

	id = get_id_struct(ids, fi->id_info_count, fi->id_info_size,our_nodeid);

	if (!id || !(id->flags & IDI_NODEID_IS_MEMBER)) {
		log_debug("receive_complete %d:%u we are not in members",
			  hd->nodeid, seq);
		return;
	}

	fd->init_complete = 1;

	/* we may have victims from init which we can clear now */
	list_for_each_entry_safe(node, safe, &fd->victims, list) {
		log_debug("receive_complete clear victim nodeid %d init %d",
			  node->nodeid, node->init_victim);
		list_del(&node->list);
		free(node);
	}
}

static int count_ids(struct fd *fd)
{
	struct node_history *node;
	int count = 0;

	list_for_each_entry(node, &fd->node_history, list)
		count++;

	return count;
}

static void send_info(struct fd *fd, int type)
{
	struct change *cg;
	struct fd_header *hd;
	struct fd_info *fi;
	struct id_info *id;
	struct node_history *node;
	char *buf;
	uint32_t flags;
	int len, id_count;

	cg = list_first_entry(&fd->changes, struct change, list);

	id_count = count_ids(fd);

	len = sizeof(struct fd_header) + sizeof(struct fd_info) +
	      id_count * sizeof(struct id_info);

	buf = malloc(len);
	if (!buf) {
		log_error("send_info len %d no mem", len);
		return;
	}
	memset(buf, 0, len);

	hd = (struct fd_header *)buf;
	fi = (struct fd_info *)(buf + sizeof(*hd));
	id = (struct id_info *)(buf + sizeof(*hd) + sizeof(*fi));

	/* fill in header (fd_send_message handles part of header) */

	hd->type = type;
	hd->msgdata = cg->seq;
	if (cg->we_joined)
		hd->flags |= FD_MFLG_JOINING;
	if (fd->init_complete)
		hd->flags |= FD_MFLG_COMPLETE;

	/* fill in fd_info */

	fi->fd_info_size  = cpu_to_le32(sizeof(struct fd_info));
	fi->id_info_size  = cpu_to_le32(sizeof(struct id_info));
	fi->id_info_count = cpu_to_le32(id_count);
	fi->started_count = cpu_to_le32(fd->started_count);
	fi->member_count  = cpu_to_le32(cg->member_count);
	fi->joined_count  = cpu_to_le32(cg->joined_count);
	fi->remove_count  = cpu_to_le32(cg->remove_count);
	fi->failed_count  = cpu_to_le32(cg->failed_count);

	/* fill in id_info entries */

	list_for_each_entry(node, &fd->node_history, list) {
		flags = 0;
		if (find_memb(cg, node->nodeid))
			flags = IDI_NODEID_IS_MEMBER;

		id->flags              = cpu_to_le32(flags);
		id->nodeid             = cpu_to_le32(node->nodeid);
		id->fence_external_node= cpu_to_le32(node->fence_external_node);
		id->fence_master       = cpu_to_le32(node->fence_master);
		id->fence_how          = cpu_to_le32(node->fence_how);
		id->fence_time         = cpu_to_le64(node->fence_time);
		id->fence_external_time= cpu_to_le64(node->fence_external_time);
		id++;
	}

	log_debug("send_%s cg %u flags %x counts %u %d %d %d %d",
		  type == FD_MSG_START ? "start" : "complete",
		  cg->seq, hd->flags, fd->started_count, cg->member_count,
		  cg->joined_count, cg->remove_count, cg->failed_count);

	fd_send_message(fd, buf, len);

	free(buf);
}

static void send_start(struct fd *fd)
{
	send_info(fd, FD_MSG_START);
}

/* same content as a start message, a new (incomplete) node will look for
   a complete message that shows it as a member, when it sees one it can
   clear any init_victims and set init_complete for future cycles */

static void send_complete(struct fd *fd)
{
	send_info(fd, FD_MSG_COMPLETE);
}

/* FIXME: better to just look in victims list for any nodes with init_victim? */

static int nodes_added(struct fd *fd)
{
	struct change *cg;

	list_for_each_entry(cg, &fd->changes, list) {
		if (cg->joined_count)
			return 1;
	}
	return 0;
}

/* If we're being added by the current change, we'll have an empty victims
   list, while other previous members may already have nodes in their
   victims list.  So, we need to assume that any node in cluster.conf that's
   not a cluster member when we're added to the fd is already a victim.
   We can go back on that assumption, and clear out any presumed victims, when
   we see a message from a previous member saying that are no current victims. */

static void add_victims(struct fd *fd, struct change *cg)
{
	struct member *memb;
	struct node *node;

	list_for_each_entry(memb, &cg->removed, list) {
		if (!memb->failed)
			continue;
		if (is_victim(fd, memb->nodeid)) {
			/* Only one scenario I know of where this happens:
			   when a partitioned cpg merges and then the
			   disallowed node is killed.  The original
			   partition makes the node a victim, and killing
			   it after a merge will find it already a victim. */
			log_debug("add_victims nodeid %d already victim",
				  memb->nodeid);
			continue;
		}
		node = get_new_node(fd, memb->nodeid);
		if (!node)
			return;
		list_add(&node->list, &fd->victims);
		log_debug("add nodeid %d to victims", node->nodeid);
	}
}

/* with start messages from all members, we can pick which one should be master
   and do the fencing (low nodeid with state, "COMPLETE").  as the master
   successfully fences each victim, it sends a status message such that all
   members remove the node from their victims list.

   after all victims have been dealt following a change (or set of changes),
   the master sends a complete message that indicates the members of the group
   for the change it has completed processing.  when a joining node sees this
   complete message and sees itself as a member, it knows it can clear all
   init_victims from startup init, and it sets init_complete so it will
   volunteer to be master in the next round by setting COMPLETE flag.

   once the master begins fencing victims, it won't process any new changes
   until it's done.  the non-master members will process changes while the
   master is fencing, but will wait for the master to catch up in
   WAIT_MESSAGES.  if the master fails, the others will no longer wait for it.*/

static void apply_changes(struct fd *fd)
{
	struct change *cg;

	if (list_empty(&fd->changes))
		return;
	cg = list_first_entry(&fd->changes, struct change, list);

	switch (cg->state) {

	case CGST_WAIT_CONDITIONS:
		if (wait_conditions_done(fd)) {
			send_start(fd);
			cg->state = CGST_WAIT_MESSAGES;
		}
		break;

	case CGST_WAIT_MESSAGES:
		if (wait_messages_done(fd)) {
			set_master(fd);
			cg->state = CGST_WAIT_FENCING;  /* for queries */

			if (fd->master == our_nodeid) {
				delay_fencing(fd, nodes_added(fd));
				fence_victims(fd);
				send_complete(fd);
			} else {
				defer_fencing(fd);
			}

			cleanup_changes(fd);
			fd->joining_group = 0;
		}
		break;

	default:
		log_error("apply_changes invalid state %d", cg->state);
	}
}

void process_fd_changes(void)
{
	struct fd *fd, *safe;

	list_for_each_entry_safe(fd, safe, &domains, list) {
		if (!list_empty(&fd->changes))
			apply_changes(fd);
	}
}

static int add_change(struct fd *fd,
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
	cg->seq = ++fd->change_seq;
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
			node_history_fail(fd, memb->nodeid);
		else
			node_history_left(fd, memb->nodeid);

		log_debug("add_change cg %u remove nodeid %d reason %d",
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
			node_history_init(fd, memb->nodeid);

		log_debug("add_change cg %u joined nodeid %d", cg->seq,
			  memb->nodeid);
	}

	if (cg->we_joined)
		list_for_each_entry(memb, &cg->members, list)
			node_history_init(fd, memb->nodeid);

	log_debug("add_change cg %u counts member %d joined %d remove %d "
		  "failed %d", cg->seq, cg->member_count, cg->joined_count,
		  cg->remove_count, cg->failed_count);

	list_add(&cg->list, &fd->changes);
	*cg_out = cg;
	return 0;

 fail_nomem:
	log_error("no memory");
	error = -ENOMEM;
 fail:
	free_cg(cg);
	return error;
}

/* add a victim for each node in complete list (represents all nodes in
   cluster.conf) that is not a cman member (and not already a victim) */

static void add_victims_init(struct fd *fd, struct change *cg)
{
	struct node *node, *safe;

	list_for_each_entry_safe(node, safe, &fd->complete, list) {
		list_del(&node->list);

		if (!is_cman_member(node->nodeid) &&
		    !find_memb(cg, node->nodeid) &&
		    !is_victim(fd, node->nodeid)) {
			node->init_victim = 1;
			list_add(&node->list, &fd->victims);
			log_debug("add_victims_init nodeid %d", node->nodeid);
		} else {
			free(node);
		}
	}
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
	struct fd *fd;
	struct change *cg;
	int rv;

	fd = find_fd_handle(handle);
	if (!fd) {
		log_error("confchg_cb no fence domain for cpg %s",
			  group_name->value);
		return;
	}

	if (fd->leaving_group && we_left(left_list, left_list_entries)) {
		/* we called cpg_leave(), and this should be the final
		   cpg callback we receive */
		log_debug("confchg for our leave");
		cpg_finalize(fd->cpg_handle);
		client_dead(fd->cpg_client);
		list_del(&fd->list);
		free_fd(fd);
		return;
	}

	rv = add_change(fd, member_list, member_list_entries,
			left_list, left_list_entries,
			joined_list, joined_list_entries, &cg);
	if (rv)
		return;

	/* failed nodes in this change become victims */

	add_victims(fd, cg);

	/* As a joining domain member with no previous state, we need to
	   assume non-member nodes are already victims; these initial victims
	   are cleared if we get a "complete" message from the master.
	   But, if we're the master, we do end up fencing these init nodes. */

	if (cg->we_joined)
		add_victims_init(fd, cg);

	apply_changes(fd);
}

static void fd_header_in(struct fd_header *hd)
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
	struct fd *fd;
	struct fd_header *hd;

	fd = find_fd_handle(handle);
	if (!fd) {
		log_error("deliver_cb no fd for cpg %s", group_name->value);
		return;
	}

	if (len < sizeof(*hd)) {
		log_error("deliver_cb short message %d", len);
		return;
	}

	hd = (struct fd_header *)data;
	fd_header_in(hd);

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
	case FD_MSG_START:
		receive_start(fd, hd, len);
		break;
	case FD_MSG_VICTIM_DONE:
		receive_victim_done(fd, hd, len);
		break;
	case FD_MSG_COMPLETE:
		receive_complete(fd, hd, len);
		break;
	case FD_MSG_EXTERNAL:
		receive_external(fd, hd, len);
		break;
	default:
		log_error("unknown msg type %d", hd->type);
	}

	apply_changes(fd);
}

static cpg_callbacks_t cpg_callbacks = {
	.cpg_deliver_fn = deliver_cb,
	.cpg_confchg_fn = confchg_cb,
};

static void process_fd_cpg(int ci)
{
	struct fd *fd;
	cpg_error_t error;

	fd = find_fd_ci(ci);
	if (!fd) {
		log_error("process_fd_cpg no fence domain for ci %d", ci);
		return;
	}

	error = cpg_dispatch(fd->cpg_handle, CPG_DISPATCH_ALL);
	if (error != CPG_OK) {
		log_error("cpg_dispatch error %d", error);
		return;
	}
}

int fd_join(struct fd *fd)
{
	cpg_error_t error;
	cpg_handle_t h;
	struct cpg_name name;
	int i = 0, f, ci;

	error = cpg_initialize(&h, &cpg_callbacks);
	if (error != CPG_OK) {
		log_error("cpg_initialize error %d", error);
		goto fail_free;
	}

	cpg_fd_get(h, &f);

	ci = client_add(f, process_fd_cpg, NULL);

	list_add(&fd->list, &domains);
	fd->cpg_handle = h;
	fd->cpg_client = ci;
	fd->cpg_fd = f;
	fd->joining_group = 1;

	memset(&name, 0, sizeof(name));
	sprintf(name.value, "fenced:%s", fd->name);
	name.length = strlen(name.value) + 1;

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
		goto fail;
	}

	return 0;

 fail:
	list_del(&fd->list);
	client_dead(ci);
	cpg_finalize(h);
 fail_free:
	free(fd);
	return error;
}

int fd_leave(struct fd *fd)
{
	cpg_error_t error;
	struct cpg_name name;
	int i = 0;

	fd->leaving_group = 1;

	memset(&name, 0, sizeof(name));
	sprintf(name.value, "fenced:%s", fd->name);
	name.length = strlen(name.value) + 1;

 retry:
	error = cpg_leave(fd->cpg_handle, &name);
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

static struct node_daemon *get_node_daemon(int nodeid)
{
	struct node_daemon *node;

	list_for_each_entry(node, &daemon_nodes, list) {
		if (node->nodeid == nodeid)
			return node;
	}
	return NULL;
}

static void add_node_daemon(int nodeid)
{
	struct node_daemon *node;

	if (get_node_daemon(nodeid))
		return;

	node = malloc(sizeof(struct node_daemon));
	if (!node) {
		log_error("add_node_daemon no mem");
		return;
	}
	memset(node, 0, sizeof(struct node_daemon));
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
	pv_in(&proto->dr_ver);
}

static void protocol_out(struct protocol *proto)
{
	pv_out(&proto->dm_ver);
	pv_out(&proto->dr_ver);
}

/* go through member list saved in last confchg, see if we have received a
   proto message from each */

static int all_protocol_messages(void)
{
	struct node_daemon *node;
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
	struct node_daemon *node;
	int i;

	memset(&mind, 0, sizeof(mind));

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
	}

	if (!mind[0]) {
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
	}

	if (!mind[1]) {
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
	}

	if (!mind[2]) {
		log_error("pick_min_protocol zero patch number");
		return -1;
	}

	memcpy(&proto->daemon_run, &mind, sizeof(mind));
	return 0;
}

static void receive_protocol(struct fd_header *hd, int len)
{
	struct protocol *p;
	struct node_daemon *node;

	p = (struct protocol *)((char *)hd + sizeof(struct fd_header));
	protocol_in(p);

	if (len < sizeof(struct fd_header) + sizeof(struct protocol)) {
		log_error("receive_protocol invalid len %d from %d",
			  len, hd->nodeid);
		return;
	}

	/* zero is an invalid version value */

	if (!p->daemon_max[0] || !p->daemon_max[1] || !p->daemon_max[2]) {
		log_error("receive_protocol invalid max value from %d "
			  "daemon %u.%u.%u", hd->nodeid,
			  p->daemon_max[0], p->daemon_max[1], p->daemon_max[2]);
		return;
	}

	/* the run values will be zero until a version is set, after
	   which none of the run values can be zero */

	if (p->daemon_run[0] && (!p->daemon_run[1] || !p->daemon_run[2])) {
		log_error("receive_protocol invalid run value from %d "
			  "daemon %u.%u.%u", hd->nodeid,
			  p->daemon_run[0], p->daemon_run[1], p->daemon_run[2]);
		return;
	}

	/* if we have zero run values, and this msg has non-zero run values,
	   then adopt them as ours; otherwise save this proto message */

	if (our_protocol.daemon_run[0])
		return;

	if (p->daemon_run[0]) {
		memcpy(&our_protocol.daemon_run, &p->daemon_run,
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
	struct fd_header *hd;
	struct protocol *pr;
	char *buf;
	int len;

	len = sizeof(struct fd_header) + sizeof(struct protocol);
	buf = malloc(len);
	if (!buf) {
		log_error("send_protocol no mem %d", len);
		return;
	}
	memset(buf, 0, len);

	hd = (struct fd_header *)buf;
	pr = (struct protocol *)(buf + sizeof(*hd));

	hd->type = cpu_to_le16(FD_MSG_PROTOCOL);
	hd->nodeid = cpu_to_le32(our_nodeid);

	memcpy(pr, proto, sizeof(struct protocol));
	protocol_out(pr);

	_send_message(cpg_handle_daemon, buf, len, FD_MSG_PROTOCOL);
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
			   nodes and pick the min and propose that */

			sent_proposal = 1;

			/* copy our max values */
			memcpy(&proto, &our_protocol, sizeof(struct protocol));

			rv = pick_min_protocol(&proto);
			if (rv < 0)
				return rv;

			log_debug("set_protocol member_count %d propose "
				  "daemon %u.%u.%u", daemon_member_count,
				  proto.daemon_run[0], proto.daemon_run[1],
				  proto.daemon_run[2]);

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

	log_debug("daemon run %u.%u.%u max %u.%u.%u",
		  our_protocol.daemon_run[0],
		  our_protocol.daemon_run[1],
		  our_protocol.daemon_run[2],
		  our_protocol.daemon_max[0],
		  our_protocol.daemon_max[1],
		  our_protocol.daemon_max[2]);
	return 0;
}

/* process_cpg(), setup_cpg(), close_cpg() are for the "daemon" cpg which
   tracks the presence of other daemons; it's not the fenced domain cpg.
   Joining this cpg tells others that we don't have uncontrolled dlm/gfs
   kernel state and they can skip fencing us if we're a victim.  (We have
   to check for that uncontrolled state before calling setup_cpg, obviously.) */

static void deliver_cb_daemon(cpg_handle_t handle, struct cpg_name *group_name,
		uint32_t nodeid, uint32_t pid, void *data, int len)
{
	struct fd_header *hd;

	if (len < sizeof(*hd)) {
		log_error("deliver_cb short message %d", len);
		return;
	}

	hd = (struct fd_header *)data;
	fd_header_in(hd);

	switch (hd->type) {
	case FD_MSG_PROTOCOL:
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

	error = cpg_dispatch(cpg_handle_daemon, CPG_DISPATCH_ALL);
	if (error != CPG_OK)
		log_error("daemon cpg_dispatch error %d", error);
}

int in_daemon_member_list(int nodeid)
{
	int i;

	cpg_dispatch(cpg_handle_daemon, CPG_DISPATCH_ALL);

	for (i = 0; i < daemon_member_count; i++) {
		if (daemon_member[i].nodeid == nodeid)
			return 1;
	}
	return 0;
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

	error = cpg_initialize(&cpg_handle_daemon, &cpg_callbacks_daemon);
	if (error != CPG_OK) {
		log_error("daemon cpg_initialize error %d", error);
		goto fail;
	}

	cpg_fd_get(cpg_handle_daemon, &daemon_cpg_fd);

	memset(&name, 0, sizeof(name));
	sprintf(name.value, "fenced:daemon");
	name.length = strlen(name.value) + 1;

 retry:
	error = cpg_join(cpg_handle_daemon, &name);
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
	cpg_finalize(cpg_handle_daemon);
	return -1;
}

void close_cpg(void)
{
	cpg_error_t error;
	struct cpg_name name;
	int i = 0;

	if (!cpg_handle_daemon || cluster_down)
		return;

	memset(&name, 0, sizeof(name));
	sprintf(name.value, "fenced:daemon");
	name.length = strlen(name.value) + 1;

 retry:
	error = cpg_leave(cpg_handle_daemon, &name);
	if (error == CPG_ERR_TRY_AGAIN) {
		sleep(1);
		if (!(++i % 10))
			log_error("daemon cpg_leave error retrying");
		goto retry;
	}
	if (error != CPG_OK)
		log_error("daemon cpg_leave error %d", error);
}

int set_node_info(struct fd *fd, int nodeid, struct fenced_node *nodeinfo)
{
	struct node_history *node;
	struct member *memb;
	struct change *cg;

	nodeinfo->nodeid = nodeid;
	nodeinfo->victim = is_victim(fd, nodeid);

	if (list_empty(&fd->changes))
		cg = fd->started_change;
	else
		cg = list_first_entry(&fd->changes, struct change, list);

	if (cg) {
		memb = find_memb(cg, nodeid);
		if (memb)
			nodeinfo->member = memb->disallowed ? -1 : 1;
	}

	node = get_node_history(fd, nodeid);
	if (node) {
		nodeinfo->last_fenced_master = node->fence_master;
		nodeinfo->last_fenced_how = node->fence_how;
		nodeinfo->last_fenced_time = node->fence_time;
	}

	return 0;
}

int set_domain_info(struct fd *fd, struct fenced_domain *domain)
{
	struct change *cg;

	if (list_empty(&fd->changes)) {
		if (fd->started_change)
			domain->member_count = fd->started_change->member_count;
	} else {
		cg = list_first_entry(&fd->changes, struct change, list);
		domain->member_count = cg->member_count;
		domain->state = cg->state;
	}

	domain->master_nodeid = fd->master;
	domain->victim_count = list_count(&fd->victims);
	domain->current_victim = fd->current_victim;

	return 0;
}

int set_domain_nodes(struct fd *fd, int option, int *node_count,
		     struct fenced_node **nodes_out)
{
	struct change *cg = fd->started_change;
	struct fenced_node *nodes = NULL, *n;
	struct node_history *nh;
	struct member *memb;
	int count = 0;

	if (option == FENCED_NODES_MEMBERS) {
		if (!cg)
			goto out;
		count = cg->member_count;

		nodes = malloc(count * sizeof(struct fenced_node));
		if (!nodes)
			return -ENOMEM;
		memset(nodes, 0, count * sizeof(struct fenced_node));

		n = nodes;
		list_for_each_entry(memb, &cg->members, list)
			set_node_info(fd, memb->nodeid, n++);
	}

	else if (option == FENCED_NODES_ALL) {
		list_for_each_entry(nh, &fd->node_history, list)
			count++;

		nodes = malloc(count * sizeof(struct fenced_node));
		if (!nodes)
			return -ENOMEM;
		memset(nodes, 0, count * sizeof(struct fenced_node));

		n = nodes;
		list_for_each_entry(nh, &fd->node_history, list)
			set_node_info(fd, nh->nodeid, n++);
	}
 out:
	*node_count = count;
	*nodes_out = nodes;
	return 0;
}

