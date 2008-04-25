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

#include "fd.h"

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

static void fd_send_message(struct fd *fd, char *buf, int len)
{
	struct fd_header *hd = (struct fd_header *) buf;
	int type = hd->type;

	hd->version[0]  = cpu_to_le16(protocol_active[0]);
	hd->version[1]  = cpu_to_le16(protocol_active[1]);
	hd->version[2]  = cpu_to_le16(protocol_active[2]);
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

void node_history_fence(struct fd *fd, int nodeid, int master, int how)
{
	struct node_history *node;

	node = get_node_history(fd, nodeid);
	if (!node) {
		log_error("node_history_fence no nodeid %d", nodeid);
		return;
	}

	node->fence_time = time(NULL);
	node->fence_master = master;
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

/* call this from libfenced:fenced_external() */

void send_external(struct fd *fd, int victim)
{
	struct fd_header *hd;
	char *buf;
	int len;

	len = sizeof(struct fd_header);

	buf = malloc(len);
	if (!buf) {
		return;
	}
	memset(buf, 0, len);

	hd = (struct fd_header *)buf;
	hd->type = FD_MSG_EXTERNAL;
	hd->msgdata = victim;

	log_debug("send_external %u", victim);

	fd_send_message(fd, buf, len);

	free(buf);
}

/* now, if the victim dies and the fence domain sees it fail,
   it will be added as an fd victim, but fence_victims() will
   call is_fenced_external() which will see that it's already
   fenced and bypass fencing it again */

static void receive_external(struct fd *fd, struct fd_header *hd, int len)
{
	log_debug("receive_external from %d len %d victim %d",
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

void send_victim_done(struct fd *fd, int victim, int how)
{
	struct change *cg = list_first_entry(&fd->changes, struct change, list);
	struct fd_header *hd;
	int n_ints, len, *p;
	int remaining = list_count(&fd->victims);
	char *buf;

	n_ints = 3;
	len = sizeof(struct fd_header) + (n_ints * sizeof(int));

	buf = malloc(len);
	if (!buf) {
		return;
	}
	memset(buf, 0, len);

	hd = (struct fd_header *)buf;
	hd->type = FD_MSG_VICTIM_DONE;
	hd->msgdata = cg->seq;

	if (fd->init_complete)
		hd->flags |= FD_MFLG_COMPLETE;

	p = (int *)(buf + sizeof(struct fd_header));

	p[0] = cpu_to_le32(victim);
	p[1] = cpu_to_le32(how);
	p[2] = cpu_to_le32(remaining);

	log_debug("send_victim_done %u flags %x victim %d how %d remaining %d",
		  cg->seq, hd->flags, victim, how, remaining);

	fd_send_message(fd, buf, len);

	free(buf);
}

static void receive_victim_done(struct fd *fd, struct fd_header *hd, int len)
{
	struct node *node;
	uint32_t seq = hd->msgdata;
	int victim, how, remaining, found;
	int *nums;

	log_debug("receive_victim_done %d:%u flags %x len %d", hd->nodeid, seq,
		  hd->flags, len);

	/* check that hd->nodeids is fd->master ? */

	/* I don't think there's any problem with the master removing the
	   victim when it's done instead of waiting to remove it when it
	   receives its own victim_done message, like the other nodes do */

	if (hd->nodeid == our_nodeid)
		return;

	nums = (int *)((char *)hd + sizeof(struct fd_header));

	victim = le32_to_cpu(nums[0]);
	how = le32_to_cpu(nums[1]);
	remaining = le32_to_cpu(nums[2]);

	found = 0;

	list_for_each_entry(node, &fd->victims, list) {
		if (node->nodeid == victim) {
			log_debug("receive_victim_done remove %d how %d rem %d",
				  victim, how, remaining);
			node_history_fence(fd, victim, hd->nodeid, how);
			list_del(&node->list);
			free(node);
			found = 1;
			break;
		}
	}

	if (!found)
		log_debug("receive_victim_done victim %d not found from %d",
			  victim, hd->nodeid);
}

/* same content as a start message, a new (incomplete) node will look for
   a complete message that shows it as a member, when it sees one it can
   clear any init_victims and set init_complete for future cycles */

static void send_complete(struct fd *fd)
{
	struct change *cg = list_first_entry(&fd->changes, struct change, list);
	struct fd_header *hd;
	struct member *memb;
	int n_ints, len, *p, i;
	char *buf;

	n_ints = 4 + cg->member_count;
	len = sizeof(struct fd_header) + (n_ints * sizeof(int));

	buf = malloc(len);
	if (!buf) {
		return;
	}
	memset(buf, 0, len);

	hd = (struct fd_header *)buf;
	hd->type = FD_MSG_COMPLETE;
	hd->msgdata = cg->seq;

	p = (int *)(buf + sizeof(struct fd_header));

	p[0] = cpu_to_le32(cg->member_count);
	p[1] = cpu_to_le32(cg->joined_count);
	p[2] = cpu_to_le32(cg->remove_count);
	p[3] = cpu_to_le32(cg->failed_count);

	i = 4;
	list_for_each_entry(memb, &cg->members, list)
		p[i++] = cpu_to_le32(memb->nodeid);

	log_debug("send_complete %u counts %d %d %d %d", cg->seq,
		  cg->member_count, cg->joined_count,
		  cg->remove_count, cg->failed_count);

	fd_send_message(fd, buf, len);

	free(buf);
}

static void receive_complete(struct fd *fd, struct fd_header *hd, int len)
{
	int member_count, joined_count, remove_count, failed_count;
	int i, n_ints, *nums;
	uint32_t seq = hd->msgdata;
	struct node *node, *safe;

	log_debug("receive_complete %d:%u len %d", hd->nodeid, seq, len);

	if (fd->init_complete)
		return;

	nums = (int *)((char *)hd + sizeof(struct fd_header));

	member_count = le32_to_cpu(nums[0]);
	joined_count = le32_to_cpu(nums[1]);
	remove_count = le32_to_cpu(nums[2]);
	failed_count = le32_to_cpu(nums[3]);

	n_ints = 4 + member_count;
	if (len < (sizeof(struct fd_header) + (n_ints * sizeof(int)))) {
		log_debug("receive_complete %d:%u bad len %d nums %s",
			  hd->nodeid, seq, len, str_nums(nums, n_ints));
		return;
	}

	for (i = 0; i < member_count; i++) {
		if (our_nodeid == le32_to_cpu(nums[4+i]))
			break;
	}
	if (i == member_count) {
		log_debug("receive_complete %d:%u we are not in members",
			  hd->nodeid, seq);
		return;
	}

	fd->init_complete = 1;

	/* we may have victims from init which we can clear now */
	list_for_each_entry_safe(node, safe, &fd->victims, list) {
		log_debug("receive_complete clear victim %d init %d",
			  node->nodeid, node->init_victim);
		list_del(&node->list);
		free(node);
	}
}

static int check_quorum_done(struct fd *fd)
{
	struct node_history *node;
	int wait_count = 0;

	if (!cman_quorate) {
		log_debug("check_quorum %d", cman_quorate);
		return 0;
	}

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
		log_debug("wait_messages_done need %d of %d", need, total);
		return 0;
	}

	log_debug("wait_messages_done got all %d", total);
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

/* do the change details in the message match the details of the given change */

static int match_change(struct fd *fd, struct change *cg,
			struct fd_header *hd, int len)
{
	struct member *memb;
	int member_count, joined_count, remove_count, failed_count;
	int i, n_ints, *nums, nodeid, members_mismatch;
	uint32_t seq = hd->msgdata;

	nums = (int *)((char *)hd + sizeof(struct fd_header));

	member_count = le32_to_cpu(nums[0]);
	joined_count = le32_to_cpu(nums[1]);
	remove_count = le32_to_cpu(nums[2]);
	failed_count = le32_to_cpu(nums[3]);

	n_ints = 4 + member_count;
	if (len < (sizeof(struct fd_header) + (n_ints * sizeof(int)))) {
		log_debug("match_change fail %d:%u bad len %d nums %s",
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
		log_debug("match_change fail %d:%u we are not in members",
			  hd->nodeid, seq);
		return 0;
	}

	memb = find_memb(cg, hd->nodeid);
	if (!memb) {
		log_debug("match_change fail %d:%u sender not member",
			  hd->nodeid, seq);
		return 0;
	}

	/* verify this is the right change by matching the counts
	   and the nodeids of the current members */

	if (member_count != cg->member_count ||
	    joined_count != cg->joined_count ||
	    remove_count != cg->remove_count ||
	    failed_count != cg->failed_count) {
		log_debug("match_change fail %d:%u expect counts "
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
		log_debug("match_change fail %d:%u no memb %d",
			  hd->nodeid, seq, nodeid);
		members_mismatch = 1;
	}
	if (members_mismatch)
		return 0;

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

static struct change *find_change(struct fd *fd, struct fd_header *hd, int len)
{
	struct change *cg;

	list_for_each_entry_reverse(cg, &fd->changes, list) {
		if (!match_change(fd, cg, hd, len))
			continue;
		return cg;
	}

	log_debug("find_change %d:%u no match", hd->nodeid, hd->msgdata);
	return NULL;
}

/* We require new members (memb->added) to be joining the domain
   (memb->joining).  New members that are not joining the domain can happen
   when the cpg partitions and is then merged back together (shouldn't happen
   in general, but is possible).  We label these new members that are not
   joining as "disallowed", and ignore their start message. */

/* Handle spurious joins by ignoring this start message if the node says it's
   not joining (i.e. it's already a member), but we see it being added (i.e.
   it's not already a member) */

static void receive_start(struct fd *fd, struct fd_header *hd, int len)
{
	struct change *cg;
	struct member *memb;
	int joining = 0;
	uint32_t seq = hd->msgdata;

	log_debug("receive_start %d:%u flags %x len %d", hd->nodeid, seq,
		  hd->flags, len);

	cg = find_change(fd, hd, len);
	if (!cg)
		return;

	memb = find_memb(cg, hd->nodeid);
	if (!memb) {
		/* this should never happen since match_change checks it */
		log_error("receive_start no member %d", hd->nodeid);
		return;
	}

	memb->start_flags = hd->flags;

	if (memb->start_flags & FD_MFLG_JOINING)
		joining = 1;

	if ((memb->added && !joining) || (!memb->added && joining)) {
		log_error("receive_start %d:%u disallowed added %d joining %d",
			  hd->nodeid, seq, memb->added, joining);
		memb->disallowed = 1;
	} else {
		node_history_start(fd, hd->nodeid);
		memb->start = 1;
	}
}

static void send_start(struct fd *fd)
{
	struct change *cg = list_first_entry(&fd->changes, struct change, list);
	struct fd_header *hd;
	struct member *memb;
	int n_ints, len, *p, i;
	char *buf;

	n_ints = 4 + cg->member_count;
	len = sizeof(struct fd_header) + (n_ints * sizeof(int));

	buf = malloc(len);
	if (!buf) {
		return;
	}
	memset(buf, 0, len);

	hd = (struct fd_header *)buf;
	hd->type = FD_MSG_START;
	hd->msgdata = cg->seq;

	if (cg->we_joined)
		hd->flags |= FD_MFLG_JOINING;
	if (fd->init_complete)
		hd->flags |= FD_MFLG_COMPLETE;

	p = (int *)(buf + sizeof(struct fd_header));

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

	log_debug("send_start %u flags %x counts %d %d %d %d", cg->seq,
		  hd->flags, cg->member_count, cg->joined_count,
		  cg->remove_count, cg->failed_count);

	fd_send_message(fd, buf, len);

	free(buf);
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
		node = get_new_node(fd, memb->nodeid);
		if (!node)
			return;
		list_add(&node->list, &fd->victims);
		log_debug("add node %d to victims", node->nodeid);
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
   WAIT_MESSAGES.  if the master fails, the others will no longer wait for it. */

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

		log_debug("add_change %u nodeid %d remove reason %d",
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
			node_history_init(fd, memb->nodeid);

		log_debug("add_change %u nodeid %d joined", cg->seq,
			  memb->nodeid);
	}

	if (cg->we_joined)
		list_for_each_entry(memb, &cg->members, list)
			node_history_init(fd, memb->nodeid);

	log_debug("add_change %u member %d joined %d remove %d failed %d",
		  cg->seq, cg->member_count, cg->joined_count, cg->remove_count,
		  cg->failed_count);

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
			log_debug("add_victims_init %d", node->nodeid);
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

	hd = (struct fd_header *)data;

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

	apply_changes(fd);
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
		cpg_finalize(h);
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

int set_node_info(struct fd *fd, int nodeid, struct fenced_node *nodeinfo)
{
	struct node_history *node;
	struct member *memb;

	nodeinfo->nodeid = nodeid;
	nodeinfo->victim = is_victim(fd, nodeid);

	if (!fd->started_change)
		goto history;

	memb = find_memb(fd->started_change, nodeid);
	if (memb)
		nodeinfo->member = memb->disallowed ? 0 : 1;

 history:
	node = get_node_history(fd, nodeid);
	if (!node)
		return 0;

	nodeinfo->last_fenced_master = node->fence_master;
	nodeinfo->last_fenced_how = node->fence_how;
	nodeinfo->last_fenced_time = node->fence_time;

	return 0;
}

int set_domain_info(struct fd *fd, struct fenced_domain *domain)
{
	struct change *cg = fd->started_change;

	if (cg) {
		domain->member_count = cg->member_count;
		domain->state = cg->state;
	}
	domain->master_nodeid = fd->master;
	domain->victim_count = list_count(&fd->victims);

	return 0;
}

int set_domain_members(struct fd *fd, int *member_count,
		       struct fenced_node **members)
{
	struct change *cg = fd->started_change;
	struct fenced_node *nodes, *nodep;
	struct member *memb;

	if (!cg) {
		*member_count = 0;
		return 0;
	}

	nodes = malloc(cg->member_count * sizeof(struct fenced_node));
	if (!nodes)
		return -ENOMEM;

	nodep = nodes;
	list_for_each_entry(memb, &cg->members, list) {
		set_node_info(fd, memb->nodeid, nodep++);
	}

	*member_count = cg->member_count;
	*members = nodes;
	return 0;
}

