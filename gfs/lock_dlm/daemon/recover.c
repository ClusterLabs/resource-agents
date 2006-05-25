/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "lock_dlm.h"

struct save_msg {
	struct list_head list;
	int nodeid;
	int len;
	char buf[MAX_MSGLEN];
};

#define SYSFS_DIR	"/sys/fs"

extern char *clustername;
extern int our_nodeid;
extern group_handle_t gh;

struct list_head mounts;

int hold_withdraw_locks(struct mountgroup *mg);
void release_withdraw_lock(struct mountgroup *mg, struct mg_member *memb);
void release_withdraw_locks(struct mountgroup *mg);

void start_participant_init_2(struct mountgroup *mg);
void start_participant_2(struct mountgroup *mg);
void start_spectator_init_2(struct mountgroup *mg);
void start_spectator_2(struct mountgroup *mg);

int set_sysfs(struct mountgroup *mg, char *field, int val)
{
	char fname[512];
	char out[16];
	int rv, fd;

	snprintf(fname, 512, "%s/%s/%s/lock_module/%s",
		 SYSFS_DIR, mg->type, mg->table, field);

	log_group(mg, "set %s to %d", fname, val);

	fd = open(fname, O_RDWR);
	if (fd < 0) {
		log_error("open %s error %d %d", fname, fd, errno);
		return -1;
	}

	memset(out, 0, 16);
	sprintf(out, "%d", val);
	rv = write(fd, out, strlen(out));

	if (rv != strlen(out)) {
		log_error("write %s error %d %d", fname, fd, errno);
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}

int get_sysfs(struct mountgroup *mg, char *field, char *buf, int len)
{
	char fname[512], *p;
	int fd, rv;

	snprintf(fname, 512, "%s/%s/%s/lock_module/%s",
		 SYSFS_DIR, mg->type, mg->table, field);

	fd = open(fname, O_RDONLY);
	if (fd < 0) {
		log_error("open %s error %d %d", fname, fd, errno);
		return -1;
	}

	rv = read(fd, buf, len);
	if (rv < 0)
		log_error("read %s error %d %d", fname, rv, errno);
	else {
		rv = 0;
		p = strchr(buf, '\n');
		if (p)
			*p = '\0';
	}

	close(fd);
	return rv;
}

struct mg_member *find_memb_nodeid(struct mountgroup *mg, int nodeid)
{
	struct mg_member *memb;

	list_for_each_entry(memb, &mg->members, list) {
		if (memb->nodeid == nodeid)
			return memb;
	}
	return NULL;
}

struct mg_member *find_memb_jid(struct mountgroup *mg, int jid)
{
	struct mg_member *memb;

	list_for_each_entry(memb, &mg->members, list) {
		if (memb->jid == jid)
			return memb;
	}
	return NULL;
}

void clear_new(struct mountgroup *mg)
{
	struct mg_member *memb;
	list_for_each_entry(memb, &mg->members, list)
		memb->new = 0;
}

static void start_done(struct mountgroup *mg)
{
	log_group(mg, "start_done %d", mg->start_event_nr);
	group_start_done(gh, mg->name, mg->start_event_nr);
}

void notify_remount_client(struct mountgroup *mg, char *msg)
{
	char buf[MAXLINE];
	int rv;

	memset(buf, 0, MAXLINE);
	snprintf(buf, MAXLINE, "%s", msg);

	log_debug("notify_remount_client: %s", buf);

	rv = client_send(mg->remount_client, buf, MAXLINE);
	if (rv < 0)
		log_error("notify_remount_client: send failed %d", rv);

	mg->remount_client = 0;
}

#define SEND_RS_INTS 3

void send_recovery_status(struct mountgroup *mg)
{
	struct gdlm_header *hd;
	struct mg_member *memb;
	int len, *p, i, n = 0;
	char *buf;

	list_for_each_entry(memb, &mg->members_gone, list) {
		if (memb->local_recovery_status == RS_SUCCESS)
			n++;
	}

	len = sizeof(struct gdlm_header) + (n * SEND_RS_INTS * sizeof(int));

	buf = malloc(len);
	if (!buf)
		return;
	memset(buf, 0, len);

	hd = (struct gdlm_header *)buf;
	hd->type = MSG_RECOVERY_STATUS;
	hd->nodeid = our_nodeid;
	hd->to_nodeid = 0;
	p = (int *) (buf + sizeof(struct gdlm_header));

	i = 0;
	list_for_each_entry(memb, &mg->members_gone, list) {
		if (memb->local_recovery_status != RS_SUCCESS)
			continue;
		p[i] = cpu_to_le32(memb->nodeid);
		i++;
		p[i] = cpu_to_le32(memb->jid);
		i++;
		p[i] = cpu_to_le32(memb->local_recovery_status);
		i++;
	}

	log_group(mg, "send_recovery_status for %d nodes len %d", n, len);

	send_group_message(mg, len, buf);

	free(buf);
}

/* Note: we can get more than one node reporting success in recovering
   the journal for a failed node.  The first has really recovered it,
   the rest have found the fs clean and report success. */

void _receive_recovery_status(struct mountgroup *mg, char *buf, int len,
			      int from)
{
	struct mg_member *memb;
	int *p, n, i, nodeid, jid, status, found = 0;

	n = (len - sizeof(struct gdlm_header)) / (SEND_RS_INTS * sizeof(int));

	p = (int *) (buf + sizeof(struct gdlm_header));

	for (i = 0; i < n; i++) {
		nodeid = le32_to_cpu(p[i * SEND_RS_INTS]);
		jid    = le32_to_cpu(p[i * SEND_RS_INTS + 1]);
		status = le32_to_cpu(p[i * SEND_RS_INTS + 2]);

		ASSERT(status == RS_SUCCESS);

		found = 0;
		list_for_each_entry(memb, &mg->members_gone, list) {
			if (memb->nodeid != nodeid)
				continue;
			ASSERT(memb->jid == jid);
			ASSERT(memb->recovery_status == RS_NEED_RECOVERY ||
			       memb->recovery_status == RS_SUCCESS);
			memb->recovery_status = status;
			found = 1;
			break;
		}

		log_group(mg, "receive_recovery_status from %d len %d "
			  "nodeid %d jid %d status %d found %d",
			  from, len, nodeid, jid, status, found);
	}

	if (from == our_nodeid)
		start_done(mg);
}

void process_saved_recovery_status(struct mountgroup *mg)
{
	struct save_msg *sm, *sm2;

	if (list_empty(&mg->saved_recovery_status))
		return;

	log_group(mg, "process_saved_recovery_status");

	list_for_each_entry_safe(sm, sm2, &mg->saved_recovery_status, list) {
		_receive_recovery_status(mg, sm->buf, sm->len, sm->nodeid);
		list_del(&sm->list);
		free(sm);
	}
}

/* we can receive recovery_status messages from other nodes doing start before
   we actually process the corresponding start callback ourselves */

void save_recovery_status(struct mountgroup *mg, char *buf, int len, int from)
{
	struct save_msg *sm;

	sm = malloc(sizeof(struct save_msg));
	if (!sm)
		return;
	memset(sm, 0, sizeof(struct save_msg));

	memcpy(&sm->buf, buf, len);
	sm->len = len;
	sm->nodeid = from;

	list_add_tail(&sm->list, &mg->saved_recovery_status);

	log_group(mg, "save_recovery_status from %d len %d", from, len);
}

void receive_recovery_status(struct mountgroup *mg, char *buf, int len,
			     int from)
{
	switch (mg->last_callback) {
	case DO_STOP:
		save_recovery_status(mg, buf, len, from);
		break;
	case DO_START:
		_receive_recovery_status(mg, buf, len, from);
		break;
	default:
		log_group(mg, "receive_recovery_status %d last_callback %d",
			  from, mg->last_callback);
	}
}

/* tell others that all journals are recovered; they should clear
   memb's from members_gone, clear needs_recovery and unblock locks */

void send_recovery_done(struct mountgroup *mg)
{
	struct gdlm_header *hd;
	int len;
	char *buf;

	len = sizeof(struct gdlm_header);

	buf = malloc(len);
	if (!buf)
		return;
	memset(buf, 0, len);

	hd = (struct gdlm_header *)buf;
	hd->type = MSG_RECOVERY_DONE;
	hd->nodeid = our_nodeid;
	hd->to_nodeid = 0;

	send_group_message(mg, len, buf);

	free(buf);
}

void receive_recovery_done(struct mountgroup *mg, char *buf, int len, int from)
{
	struct mg_member *memb, *safe;

	log_group(mg, "receive_recovery_done from %d needs_recovery %d",
		  from, mg->needs_recovery);

	list_for_each_entry_safe(memb, safe, &mg->members_gone, list) {
		log_group(mg, "receive_recovery_done clear jid %d nodeid %d",
			  memb->jid, memb->nodeid);
		list_del(&memb->list);
		free(memb);
	}

	mg->needs_recovery = 0;
	set_sysfs(mg, "block", 0);
}

void send_remount(struct mountgroup *mg, int ro)
{
	struct gdlm_header *hd;
	int len;
	char *buf;

	len = sizeof(struct gdlm_header) + MAX_OPTIONS_LEN;

	buf = malloc(len);
	if (!buf)
		return;
	memset(buf, 0, len);

	hd = (struct gdlm_header *)buf;
	hd->type = MSG_REMOUNT;
	hd->nodeid = our_nodeid;
	hd->to_nodeid = 0;

	strcpy(buf+sizeof(struct gdlm_header), ro ? "ro" : "rw");

	log_group(mg, "send_remount len %d \"%s\"", len,
		  buf+sizeof(struct gdlm_header));

	send_group_message(mg, len, buf);

	free(buf);
}

void receive_remount(struct mountgroup *mg, char *buf, int len, int from)
{
	struct mg_member *memb;
	char *options, *msg = "ok";
	int rw = 0, ro = 0, error = 0;

	options = (char *) (buf + sizeof(struct gdlm_header));

	memb = find_memb_nodeid(mg, from);
	if (!memb) {
		log_error("receive_remount: unknown nodeid %d", from);
		return;
	}

	if (strstr(options, "rw"))
		rw = 1;
	else if (strstr(options, "ro"))
		ro = 1;
	else {
		msg = "error: invalid option";
		error = -1;
		goto out;
	}

	if (mg->needs_recovery) {
		log_group(mg, "receive_remount from %d needs_recovery", from);
		msg = "error: needs recovery";
		error = -1;
		goto out;
	}

	memb->readonly = ro;
	memb->rw = !ro;

	if (ro) {
		memb->opts &= ~MEMB_OPT_RW;
		memb->opts |= MEMB_OPT_RO;
	} else {
		memb->opts &= ~MEMB_OPT_RO;
		memb->opts |= MEMB_OPT_RW;
	}
 out:
	if (from == our_nodeid) {
		if (!error) {
			mg->rw = memb->rw;
			mg->readonly = memb->readonly;
		}
		notify_remount_client(mg, msg);
	}

	log_group(mg, "receive_remount from %d error %d rw=%d ro=%d opts=%x",
		  from, error, memb->rw, memb->readonly, memb->opts);
}

void set_our_memb_options(struct mountgroup *mg)
{
	struct mg_member *memb;
	memb = find_memb_nodeid(mg, our_nodeid);
	ASSERT(memb);

	if (mg->readonly) {
		memb->readonly = 1;
		memb->opts |= MEMB_OPT_RO;
	} else if (mg->spectator) {
		memb->spectator = 1;
		memb->opts |= MEMB_OPT_SPECT;
	} else if (mg->rw) {
		memb->rw = 1;
		memb->opts |= MEMB_OPT_RW;
	}
}

void send_options(struct mountgroup *mg)
{
	struct gdlm_header *hd;
	int len;
	char *buf;

	len = sizeof(struct gdlm_header) + MAX_OPTIONS_LEN;

	buf = malloc(len);
	if (!buf)
		return;
	memset(buf, 0, len);

	hd = (struct gdlm_header *)buf;
	hd->type = MSG_OPTIONS;
	hd->nodeid = our_nodeid;
	hd->to_nodeid = 0;

	strncpy(buf+sizeof(struct gdlm_header), mg->options, MAX_OPTIONS_LEN-1);

	log_group(mg, "send_options len %d \"%s\"", len,
		  buf+sizeof(struct gdlm_header));

	send_group_message(mg, len, buf);

	free(buf);
}

void _receive_options(struct mountgroup *mg, char *buf, int len, int from)
{
	struct mg_member *memb;
	struct gdlm_header *hd;
	char *options;

	hd = (struct gdlm_header *)buf;
	options = (char *) (buf + sizeof(struct gdlm_header));

	memb = find_memb_nodeid(mg, from);
	if (!memb) {
		log_error("unknown nodeid %d for options message", from);
		return;
	}

	if (from == our_nodeid)
		return;

	if (strstr(options, "spectator")) {
		memb->spectator = 1;
		memb->opts |= MEMB_OPT_SPECT;
	} else if (strstr(options, "rw")) {
		memb->rw = 1;
		memb->opts |= MEMB_OPT_RW;
	} else if (strstr(options, "ro")) {
		memb->readonly = 1;
		memb->opts |= MEMB_OPT_RO;
	}

	log_group(mg, "receive_options from %d rw=%d ro=%d spect=%d opts=%x",
		  from, memb->rw, memb->readonly, memb->spectator, memb->opts);
}

void receive_options(struct mountgroup *mg, char *buf, int len, int from)
{
	struct gdlm_header *hd = (struct gdlm_header *)buf;

	if (hd->nodeid == our_nodeid)
		return;

	log_group(mg, "receive_options from %d len %d last_cb %d",
		  from, len, mg->last_callback);

	/* If last_callback isn't DO_START it means we've not gotten
	   the start callback for the new node addition yet, and we need to
	   save this message to be processed after we get our first start. */

	if (mg->last_callback != DO_START) {
		mg->options_msg = malloc(len);
		mg->options_msg_len = len;
		mg->options_msg_from = from;
		memcpy(mg->options_msg, buf, len);
	} else {
		void (*start2)(struct mountgroup *mg) = mg->start2_fn;
		_receive_options(mg, buf, len, from);
		start2(mg);
		mg->start2_fn = NULL;
	}
}

#define NUM 3

/* send nodeid/jid/opts of every member to nodeid */

void send_journals(struct mountgroup *mg, int nodeid)
{
	struct mg_member *memb;
	struct gdlm_header *hd;
	int i, len;
	char *buf;
	int *ids;

	len = sizeof(struct gdlm_header) + (mg->memb_count * NUM * sizeof(int));

	buf = malloc(len);
	if (!buf)
		return;
	memset(buf, 0, len);

	hd = (struct gdlm_header *)buf;
	hd->type = MSG_JOURNAL;
	hd->nodeid = our_nodeid;
	hd->to_nodeid = nodeid;
	ids = (int *) (buf + sizeof(struct gdlm_header));

	i = 0;
	list_for_each_entry(memb, &mg->members, list) {
		ids[i] = cpu_to_le32(memb->nodeid);
		i++;
		ids[i] = cpu_to_le32(memb->jid);
		i++;
		ids[i] = cpu_to_le32(memb->opts);
		i++;
	}

	log_group(mg, "send_journals to %d len %d count %d", nodeid, len, i);

	send_group_message(mg, len, buf);

	free(buf);
}

void _receive_journals(struct mountgroup *mg, char *buf, int len, int from)
{
	struct mg_member *memb, *memb2;
	struct gdlm_header *hd;
	int *ids, count, i, nodeid, jid, opts;

	hd = (struct gdlm_header *)buf;

	count = (len - sizeof(struct gdlm_header)) / (NUM * sizeof(int));

	if (count != mg->memb_count) {
		log_error("invalid journals message len %d counts %d %d",
			  len, count, mg->memb_count);
		return;
	}

	ids = (int *) (buf + sizeof(struct gdlm_header));

	for (i = 0; i < count; i++) {
		nodeid = le32_to_cpu(ids[i * NUM]);
		jid    = le32_to_cpu(ids[i * NUM + 1]);
		opts   = le32_to_cpu(ids[i * NUM + 2]);

		log_debug("receive nodeid %d jid %d opts %x",
			  nodeid, jid, opts);

		memb = find_memb_nodeid(mg, nodeid);
		memb2 = find_memb_jid(mg, jid);

		if (!memb || memb2) {
			log_error("invalid journals message "
				  "nodeid %d jid %d opts %x",
				  nodeid, jid, opts);
			continue;
		}

		memb->jid = jid;

		if (nodeid == our_nodeid) {
			mg->our_jid = jid;
			/* set_our_memb_options() sets rest */
			if (opts & MEMB_OPT_RECOVER)
				memb->opts |= MEMB_OPT_RECOVER;
		} else {
			memb->opts = opts;
			if (opts & MEMB_OPT_RO)
				memb->readonly = 1;
			else if (opts & MEMB_OPT_RW)
				memb->rw = 1;
			else if (opts & MEMB_OPT_SPECT)
				memb->spectator = 1;
		}
	}
}

void receive_journals(struct mountgroup *mg, char *buf, int len, int from)
{
	struct gdlm_header *hd = (struct gdlm_header *)buf;
	int count;

	if (hd->to_nodeid && hd->to_nodeid != our_nodeid)
		return;

	count = (len - sizeof(struct gdlm_header)) / (NUM * sizeof(int));

	log_group(mg, "receive_journals from %d len %d count %d last_cb %d",
		  from, len, count, mg->last_callback);

	/* If init is still 1 it means we've not run do_start()
	   for our join yet, and we need to save this message to be
	   processed after we get our first start. */


	/* it should now be impossible to receive a journals message prior to
	   our start because the node sending journals won't do so until
	   receiving our options message
	if (mg->init) {
		mg->journals_msg = malloc(len);
		mg->journals_msg_len = len;
		mg->journals_msg_from = from;
		memcpy(mg->journals_msg, buf, len);
	} else {
	*******/

	ASSERT(mg->last_callback == DO_START);

	{
		void (*start2)(struct mountgroup *mg) = mg->start2_fn;
		_receive_journals(mg, buf, len, from);
		start2(mg);
		mg->start2_fn = NULL;
	}
}

/* We set the new member's jid to the lowest unused jid.
   If we're the lowest existing member (by nodeid), then
   send jid info to the new node. */

/* Look at rw/ro/spectator status of all existing mounters and whether
   we need to do recovery.  Based on that, decide if the current mount
   mode (ro/spectator) is permitted; if not, set jid = -2.  If spectator
   mount and it's ok, set jid = -1.  If ro or rw mount and it's ok, set
   real jid. */

int discover_journals(struct mountgroup *mg)
{
	struct mg_member *memb, *new = NULL;
	int i, total, rw_count, ro_count, spect_count, invalid_count;

	total = rw_count = ro_count = spect_count = invalid_count = 0;

	list_for_each_entry(memb, &mg->members, list) {
		if (memb->new && new) {
			log_error("more than one new member %d %d",
				  new->nodeid, memb->nodeid);
			return -1;
		} else if (memb->new) {
			new = memb;
		} else {
			total++;
			if (memb->jid == -2)
				invalid_count++;
			else if (memb->spectator)
				spect_count++;
			else if (memb->rw)
				rw_count++;
			else if (memb->readonly)
				ro_count++;
		}
	}

	if (!new) {
		log_group(mg, "discover_journals: no new member");
		return 0;
	}

	log_group(mg, "discover_journals: total %d iv %d rw %d ro %d spect %d",
		  total, invalid_count, rw_count, ro_count, spect_count);

	log_group(mg, "discover_journals: new member %d rw=%d ro=%d spect=%d",
		  new->nodeid, new->rw, new->readonly, new->spectator);

	/* do we let the new member mount? jid=-2 means no.
	   - we only allow an rw mount when the fs needs recovery
	   - we only allow a single rw mount when the fs needs recovery */

	if (mg->needs_recovery) {
		if (!new->rw || rw_count)
			new->jid = -2;
	}

	if (new->jid == -2) {
		log_group(mg, "discover_journals: fail - needs_recovery %d",
			  mg->needs_recovery);
		goto out;
	}

	if (new->spectator) {
		log_group(mg, "discover_journals: new spectator allowed");
		new->jid = -1;
		goto out;
	}

	for (i = 0; i < 1024; i++) {
		memb = find_memb_jid(mg, i);
		if (!memb) {
			new->jid = i;
			break;
		}
	}

	/* Currently the fs needs recovery, i.e. none of the current
	   mounters (ro/spectators) can recover journals.  So, this new rw
	   mounter is told to do first-mounter recovery of all the journals. */

	if (mg->needs_recovery) {
		log_group(mg, "discover_journals: new member OPT_RECOVER");
		new->opts |= MEMB_OPT_RECOVER;
	}

 out:
	log_group(mg, "discover_journals: new member %d got jid %d",
		  new->nodeid, new->jid);

	if (mg->low_finished_nodeid == our_nodeid)
		send_journals(mg, new->nodeid);
	return 0;
}

static void add_ordered_member(struct mountgroup *mg, struct mg_member *new)
{
	struct mg_member *memb = NULL;
	struct list_head *tmp;
	struct list_head *newlist = &new->list;
	struct list_head *head = &mg->members;

	list_for_each(tmp, head) {
		memb = list_entry(tmp, struct mg_member, list);
		if (new->nodeid < memb->nodeid)
			break;
	}

	if (!memb)
		list_add_tail(newlist, head);
	else {
		/* FIXME: can use list macro here */
		newlist->prev = tmp->prev;
		newlist->next = tmp;
		tmp->prev->next = newlist;
		tmp->prev = newlist;
	}
}

int add_member(struct mountgroup *mg, int nodeid)
{
	struct mg_member *memb;

	memb = malloc(sizeof(struct mg_member));
	if (!memb)
		return -ENOMEM;

	memset(memb, 0, sizeof(*memb));

	memb->nodeid = nodeid;
	memb->jid = -9;
	memb->new = 1;
	add_ordered_member(mg, memb);
	mg->memb_count++;
	return 0;
}

int is_member(struct mountgroup *mg, int nodeid)
{
	struct mg_member *memb;

	list_for_each_entry(memb, &mg->members, list) {
		if (memb->nodeid == nodeid)
			return TRUE;
	}
	return FALSE;
}

int is_removed(struct mountgroup *mg, int nodeid)
{
	struct mg_member *memb;

	list_for_each_entry(memb, &mg->members_gone, list) {
		if (memb->nodeid == nodeid)
			return TRUE;
	}
	return FALSE;
}

static void clear_memb_list(struct list_head *head)
{
	struct mg_member *memb;

	while (!list_empty(head)) {
		memb = list_entry(head->next, struct mg_member, list);
		list_del(&memb->list);
		free(memb);
	}
}

void clear_members(struct mountgroup *mg)
{
	clear_memb_list(&mg->members);
	mg->memb_count = 0;
}

void clear_members_gone(struct mountgroup *mg)
{
	clear_memb_list(&mg->members_gone);
}

void recover_members(struct mountgroup *mg, int num_nodes,
 		     int *nodeids, int *pos_out, int *neg_out)
{
	struct mg_member *memb, *safe;
	int i, found, id, pos = 0, neg = 0, low = -1;

	/* move departed nodes from members list to members_gone */

	list_for_each_entry_safe(memb, safe, &mg->members, list) {
		found = FALSE;
		for (i = 0; i < num_nodes; i++) {
			if (memb->nodeid == nodeids[i]) {
				found = TRUE;
				break;
			}
		}

		if (!found) {
			neg++;

			list_move(&memb->list, &mg->members_gone);
			memb->gone_event = mg->start_event_nr;
			memb->gone_type = mg->start_type;
			mg->memb_count--;

			memb->tell_gfs_to_recover = 0;
			memb->recovery_status = 0;
			memb->local_recovery_status = 0;

			/* - journal cb for failed or withdrawing nodes
			   - journal cb only if failed node finished joining
			   - no journal cb if failed node was spectator
			   - no journal cb if we've already done a journl cb */

			if ((memb->gone_type == GROUP_NODE_FAILED ||
			    memb->withdraw) &&
			    memb->mount_finished &&
			    !memb->spectator &&
			    !memb->wait_gfs_recover_done) {
				memb->tell_gfs_to_recover = 1;
				memb->recovery_status = RS_NEED_RECOVERY;
				memb->local_recovery_status = RS_NEED_RECOVERY;
			}

			log_group(mg, "remove member %d tell_gfs_to_recover %d "
				  "(%d,%d,%d,%d,%d,%d)",
				  memb->nodeid, memb->tell_gfs_to_recover,
				  mg->spectator,
				  mg->start_type,
				  memb->withdraw,
				  memb->mount_finished,
				  memb->spectator,
				  memb->wait_gfs_recover_done);
		}
	}	

	/* add new nodes to members list */

	for (i = 0; i < num_nodes; i++) {
		id = nodeids[i];
		if (is_member(mg, id))
			continue;
		add_member(mg, id);
		pos++;
		log_group(mg, "add member %d", id);
	}

	list_for_each_entry(memb, &mg->members, list) {
		if (!memb->mount_finished)
			continue;
		if (low == -1 || memb->nodeid < low)
			low = memb->nodeid;
	}
	mg->low_finished_nodeid = low;

	*pos_out = pos;
	*neg_out = neg;

	log_group(mg, "total members %d low_finished_nodeid %d",
		  mg->memb_count, low);
}

struct mountgroup *create_mg(char *name)
{
	struct mountgroup *mg;

	mg = malloc(sizeof(struct mountgroup));
	memset(mg, 0, sizeof(struct mountgroup));

	INIT_LIST_HEAD(&mg->members);
	INIT_LIST_HEAD(&mg->members_gone);
	INIT_LIST_HEAD(&mg->resources);
	INIT_LIST_HEAD(&mg->saved_recovery_status);
	mg->init = 1;
	mg->init2 = 1;

	strncpy(mg->name, name, MAXNAME);

	return mg;
}

struct mountgroup *find_mg(char *name)
{
	struct mountgroup *mg;

	list_for_each_entry(mg, &mounts, list) {
		if ((strlen(mg->name) == strlen(name)) &&
		    !strncmp(mg->name, name, strlen(name)))
			return mg;
	}
	return NULL;
}

struct mountgroup *find_mg_id(uint32_t id)
{
	struct mountgroup *mg;

	list_for_each_entry(mg, &mounts, list) {
		if (mg->id == id)
			return mg;
	}
	return NULL;
}

struct mountgroup *find_mg_dir(char *dir)
{
	struct mountgroup *mg;

	list_for_each_entry(mg, &mounts, list) {
		if (!strcmp(mg->dir, dir))
			return mg;
	}
	return NULL;
}

static int we_are_in_fence_domain(void)
{
	group_data_t data;
	int i, rv;

	memset(&data, 0, sizeof(data));

	rv = group_get_group(0, "default", &data);

	if (rv || strcmp(data.client_name, "fence"))
		return 0;

	for (i = 0; i < data.member_count; i++) {
		if (data.members[i] == our_nodeid)
			return 1;
	}

	return 0;
}

int do_mount(int ci, char *dir, char *type, char *proto, char *table,
	     char *options)
{
	struct mountgroup *mg;
	char table2[MAXLINE];
	char *cluster = NULL, *name = NULL;
	int rv;

	log_debug("mount: %s %s %s %s %s",
		  dir, type, proto, table, options);

	if (strcmp(proto, "lock_dlm")) {
		log_error("mount: lockproto %s not supported", proto);
		rv = -EINVAL;
		goto fail;
	}

	if (strstr(options, "jid=") ||
	    strstr(options, "first=") ||
	    strstr(options, "id=")) {
		log_error("mount: jid, first and id are reserved options");
		rv = -EINVAL;
		goto fail;
	}

	/* table is <cluster>:<name> */

	memset(&table2, 0, MAXLINE);
	strncpy(table2, table, MAXLINE);

	name = strstr(table2, ":");
	if (!name) {
		rv = -EINVAL;
		goto fail;
	}

	*name = '\0';
	name++;
	cluster = table2;

	if (strlen(name) > MAXNAME) {
		rv = -ENAMETOOLONG;
		goto fail;
	}

	mg = find_mg(name);
	if (mg) {
		rv = -EEXIST;
		goto fail;
	}

	mg = create_mg(name);
	if (!mg) {
		rv = -ENOMEM;
		goto fail;
	}

	mg->mount_client = ci;
	strncpy(mg->dir, dir, sizeof(mg->dir));
	strncpy(mg->type, type, sizeof(mg->type));
	strncpy(mg->table, table, sizeof(mg->table));
	strncpy(mg->options, options, sizeof(mg->options));

	if (strlen(cluster) != strlen(clustername) ||
	    strlen(cluster) == 0 || strcmp(cluster, clustername)) {
		rv = -1;
		log_error("mount: fs requires cluster=\"%s\" current=\"%s\"",
			  cluster, clustername);
		goto fail;
	} else
		log_group(mg, "cluster name matches: %s", clustername);

	if (strstr(options, "spectator")) {
		log_group(mg, "spectator mount");
		mg->spectator = 1;
	} else {
		if (!we_are_in_fence_domain()) {
			rv = -EINVAL;
			log_error("mount: not in default fence domain");
			goto fail;
		}
	}

	if (!mg->spectator && strstr(options, "rw"))
		mg->rw = 1;
	else if (strstr(options, "ro")) {
		if (mg->spectator) {
			rv = -EINVAL;
			log_error("mount: readonly invalid with spectator");
			goto fail;
		}
		mg->readonly = 1;
	}

	if (strlen(options) > MAX_OPTIONS_LEN-1) {
		rv = -EINVAL;
		log_error("mount: options too long %d", strlen(options));
		goto fail;
	}

	list_add(&mg->list, &mounts);

	group_join(gh, name);
	return 0;

 fail:
	log_error("mount: failed %d", rv);
	return rv;
}

/* When we're the first mounter, gfs does recovery on all the journals
   and does "recovery_done" callbacks when it finishes each.  We ignore
   these and wait for gfs to be finished with all at which point it calls
   others_may_mount() and first_done is set. */

int kernel_recovery_done_first(struct mountgroup *mg)
{
	char buf[MAXLINE];
	int rv, first_done;

	memset(buf, 0, sizeof(buf));

	rv = get_sysfs(mg, "first_done", buf, sizeof(buf));
	if (rv < 0)
		return rv;

	first_done = atoi(buf);

	log_group(mg, "recovery_done_first first_done %d wait_first_done %d",
		  first_done, mg->wait_first_done);

	if (first_done) {
		mg->first_mounter_done = 1;

		/* If a second node was added before we got first_done,
		   we delayed calling start_done() (to complete adding
		   the second node) until here. */

		if (mg->wait_first_done) {
			clear_new(mg);
			start_done(mg);
		}
	}
	return 0;
}

/* recover_members() discovers which nodes need journal recovery
   and moves the memb structs for those nodes into members_gone
   and sets memb->tell_gfs_to_recover on them */

void recover_journals(struct mountgroup *mg)
{
	struct mg_member *memb;
	int rv;

	if (mg->spectator || mg->readonly) {
		list_for_each_entry(memb, &mg->members_gone, list) {
			if (!memb->tell_gfs_to_recover)
				continue;

			log_group(mg, "recover journal %d nodeid %d skip ro",
				  memb->jid, memb->nodeid);
			memb->tell_gfs_to_recover = 0;
			memb->local_recovery_status = RS_READONLY;
		}
		start_done(mg);
		return;
	}

	/* we feed one jid into the kernel for recovery instead of all
	   at once because we need to get the result of each independently
	   through the single recovery_done sysfs file */

	list_for_each_entry(memb, &mg->members_gone, list) {
		if (!memb->tell_gfs_to_recover)
			continue;

		log_group(mg, "recover journal %d nodeid %d",
			  memb->jid, memb->nodeid);

		rv = set_sysfs(mg, "recover", memb->jid);
		if (rv < 0) {
			memb->local_recovery_status = RS_NOFS;
			continue;
		}
		memb->tell_gfs_to_recover = 0;
		memb->wait_gfs_recover_done = 1;
		return;
	}

	/* no more journals to attempt to recover, if we've been successful
	   recovering any then send out status, if not then start_done...
	   receiving no status message from us before start_done means we
	   didn't successfully recover any journals.  If we send out status,
	   then delay start_done until we get our own message (so all nodes
	   will get the status before finish) */

	list_for_each_entry(memb, &mg->members_gone, list) {
		if (memb->local_recovery_status == RS_SUCCESS) {
			send_recovery_status(mg);
			log_group(mg, "delay start_done until status recvd");
			return;
		}
	}

	start_done(mg);
}

/* In some cases, we may be joining a mountgroup with needs_recovery
   set (there are journals that need recovery and current members can't
   recover them because they're ro).  In this case, we're told to act
   like the first mounter to cause gfs to try to recovery all journals
   when it mounts.  When gfs does this, we'll get recovery_done's for
   the individual journals it recovers (ignored) and finally, if all
   journals are ok, an others_may_mount/first_done.
 
   This all happens outside the context of a start event.  The last start
   event was used to add the new emulate_first mounter to the group.

   Only a single rw node is allowed to mount while the fs needs_recovery. */

/* When gfs does first-mount recovery, the mount(2) fails if it can't
   recover one of the journals.  If we get o_m_m, then we know it was
   able to successfully recover all the journals. */

int kernel_recovery_done_emulate_first(struct mountgroup *mg)
{
	char buf[MAXLINE];
	int rv;

	memset(buf, 0, sizeof(buf));

	rv = get_sysfs(mg, "first_done", buf, sizeof(buf));
	if (rv < 0)
		return rv;

	if (atoi(buf)) {
		log_group(mg, "kernel_recovery_done_emulate_first");
		mg->first_mounter_done = 1;
		send_recovery_done(mg);
	}
	return 0;
}

/* Note: when a readonly node fails we do consider its journal (and the
   fs) to need recovery... not sure this is really necessary, but
   the readonly node did "own" a journal so it seems proper to recover
   it even if the node wasn't writing to it.  So, if there are 3 ro
   nodes mounting the fs and one fails, gfs on the remaining 2 will
   remain blocked until an rw node mounts, and the next mounter must
   be rw. */

int kernel_recovery_done(char *table)
{
	struct mountgroup *mg;
	struct mg_member *memb;
	char buf[MAXLINE];
	char *ss, *name = strstr(table, ":") + 1;
	int rv, jid_done, found = 0;

	mg = find_mg(name);
	if (!mg) {
		log_error("recovery_done: unknown mount group %s", table);
		return -1;
	}

	if (mg->first_mounter && !mg->first_mounter_done &&
	    mg->emulate_first_mounter)
		return kernel_recovery_done_emulate_first(mg);

	if (mg->first_mounter && !mg->first_mounter_done)
		return kernel_recovery_done_first(mg);

	memset(buf, 0, sizeof(buf));

	rv = get_sysfs(mg, "recover_done", buf, sizeof(buf));
	if (rv < 0)
		return rv;
	jid_done = atoi(buf);

	list_for_each_entry(memb, &mg->members_gone, list) {
		if (memb->jid == jid_done) {
			if (memb->wait_gfs_recover_done) {
				memb->wait_gfs_recover_done = 0;
				found = 1;
			}
			break;
		}
	}

	/* We need to ignore recovery_done callbacks in the case where there
	   are a bunch of recovery_done callbacks for the first mounter, but
	   we detect "first_done" before we've processed all the
	   recovery_done's. */

	if (!found) {
		log_group(mg, "recovery_done jid %d ignored, first %d,%d",
			  jid_done, mg->first_mounter, mg->first_mounter_done);
		return 0;
	}

	memset(buf, 0, sizeof(buf));

	rv = get_sysfs(mg, "recover_status", buf, sizeof(buf));
	if (rv < 0) {
		log_group(mg, "recovery_done jid %d nodeid %d sysfs error %d",
			  memb->jid, memb->nodeid, rv);
		memb->local_recovery_status = RS_NOFS;
		goto out;
	}

	switch (atoi(buf)) {
	case LM_RD_GAVEUP:
		memb->local_recovery_status = RS_GAVEUP;
		ss = "gaveup";
		break;
	case LM_RD_SUCCESS:
		memb->local_recovery_status = RS_SUCCESS;
		ss = "success";
		break;
	default:
		log_error("recovery_done: jid %d nodeid %d unknown status %d",
			  memb->jid, memb->nodeid, atoi(buf));
		ss = "unknown";
	}

	log_group(mg, "recovery_done jid %d nodeid %d %s",
		  memb->jid, memb->nodeid, ss);
 out:
	recover_journals(mg);
	return 0;
}

int need_kernel_recovery_done(struct mountgroup *mg)
{
	struct mg_member *memb;

	list_for_each_entry(memb, &mg->members_gone, list) {
		if (memb->wait_gfs_recover_done)
			return 1;
	}
	return 0;
}

int do_remount(int ci, char *dir, char *mode)
{
	struct mountgroup *mg;
	int ro = 0, rw = 0;;

	if (!strncmp(mode, "ro", 2))
		ro = 1;
	else
		rw = 1;

	mg = find_mg_dir(dir);
	if (!mg) {
		log_error("do_remount: remount mount dir %s", dir);
		return -1;
	}

	/* no change */
	if ((mg->readonly && ro) || (mg->rw && rw))
		return 1;

	mg->remount_client = ci;
	send_remount(mg, ro);
	return 0;
}

int do_unmount(int ci, char *dir)
{
	struct mountgroup *mg;

	mg = find_mg_dir(dir);
	if (!mg) {
		log_error("do_unmount: unknown mount dir %s", dir);
		return -1;
	}
	
	/* Check to see if we're waiting for a kernel recovery_done to do a
	   start_done().  If so, call the start_done() here because we won't be
	   getting anything else from gfs-kernel which is now gone. */

	if (need_kernel_recovery_done(mg)) {
		log_group(mg, "do_unmount: fill in start_done");
		start_done(mg);
	}

	group_leave(gh, mg->name);
	return 0;
}

void notify_mount_client(struct mountgroup *mg)
{
	char buf[MAXLINE];
	int rv, error = 0;

	memset(buf, 0, MAXLINE);

	if (mg->error_msg[0]) {
		strncpy(buf, mg->error_msg, MAXLINE);
		error = 1;
	} else {
		if (mg->our_jid < 0)
			snprintf(buf, MAXLINE, "hostdata=id=%u:first=%d",
		 		 mg->id, mg->first_mounter);
		else
			snprintf(buf, MAXLINE, "hostdata=jid=%d:id=%u:first=%d",
		 		 mg->our_jid, mg->id, mg->first_mounter);
	}

	log_debug("notify_mount_client: %s", buf);

	rv = client_send(mg->mount_client, buf, MAXLINE);
	if (rv < 0)
		log_error("notify_mount_client: send failed %d", rv);

	mg->mount_client = 0;

	if (error) {
		log_group(mg, "leaving due to mount error: %s", mg->error_msg);
		group_leave(gh, mg->name);
	}
}

/* When mounting a fs, we first join the mountgroup, then tell mount.gfs
   to procede with the kernel mount.  Once we're in the mountgroup, we
   can get a stop callback at any time, which requires us to block the
   fs by setting a sysfs file.  If the kernel mount is slow, we can get
   a stop callback and try to set the sysfs file before the kernel mount
   has actually created the sysfs files for the fs.  This function delays
   any further processing until the sysfs files exist. */

void wait_for_kernel_mount(struct mountgroup *mg)
{
	char buf[MAXLINE];
	int rv;

	while (1) {
		rv = get_sysfs(mg, "id", buf, sizeof(buf));
		if (!rv)
			break;
		usleep(100000);
	}
}

int do_stop(struct mountgroup *mg)
{
	set_sysfs(mg, "block", 1);
	group_stop_done(gh, mg->name);
	return 0;
}

int do_finish(struct mountgroup *mg)
{
	struct mg_member *memb, *safe;
	int leave_blocked = 0;

	/* members_gone list are the members that were removed from the
	   members list when processing a start.  members are removed
	   from members_gone if their journals has been recovered */

	list_for_each_entry_safe(memb, safe, &mg->members_gone, list) {
		if (!memb->withdraw)
			release_withdraw_lock(mg, memb);

		if (memb->recovery_status == RS_SUCCESS) {
			ASSERT(memb->gone_event <= mg->last_finish);
			log_group(mg, "finish: recovered jid %d nodeid %d",
				  memb->jid, memb->nodeid);
			list_del(&memb->list);
			free(memb);
		} else {
			mg->needs_recovery = 1;
			log_group(mg, "finish: needs recovery "
				  "jid %d nodeid %d status %d",
				  memb->jid, memb->nodeid,
				  memb->recovery_status);
		}
	}

	list_for_each_entry(memb, &mg->members, list) {
		memb->mount_finished = 1;

		/* If there are still withdrawing nodes that haven't left
		   the group, we need to keep lock requests blocked */

		if (memb->withdraw) {
			log_group(mg, "finish: leave locks blocked for "
				  "withdrawing node %d", memb->nodeid);
			leave_blocked = 1;
		}
	}

	if (mg->needs_recovery) {
		log_group(mg, "finish: leave locks blocked for needs_recovery");
		leave_blocked = 1;
	}

	if (mg->mount_client) {
		notify_mount_client(mg);
		wait_for_kernel_mount(mg);
	} else if (!leave_blocked)
		set_sysfs(mg, "block", 0);

	return 0;
}

int first_mounter_recovery(struct mountgroup *mg)
{
	struct mg_member *memb;

	memb = find_memb_nodeid(mg, our_nodeid);
	ASSERT(memb);

	if (memb->opts & MEMB_OPT_RECOVER)
		return 1;
	return 0;
}

/*
 * - require the first mounter to be rw, not ro or spectator.
 *
 * - if rw mounter fails, leaving only spectator mounters,
 * require the next mounter to be rw, more ro/spectator mounts should
 * fail until the fs is mounted rw.
 *
 * - if last rw mounter fails and ro mounters are left (possibly with
 * some spectators), disallow any ro->rw remounts, leave gfs blocked,
 * require next mounter to be rw, have next mounter do first mount
 * gfs/journal recovery.
 */

/* called for the initial start on the node that's first to mount the fs.
   (it should be ok to let the first mounter be a spectator, gfs should do
   first recovery and bail out if there are any dirty journals) */

/* FIXME: if journal recovery fails on any of the journals, we should
   fail the mount */

void start_first_mounter(struct mountgroup *mg)
{
	struct mg_member *memb;

	log_group(mg, "start_first_mounter");

	set_our_memb_options(mg);

	memb = find_memb_nodeid(mg, our_nodeid);
	ASSERT(memb);

	if (mg->readonly || mg->spectator) {
		memb->jid = -2;
		mg->our_jid = -2;
		log_group(mg, "start_first_mounter not rw ro=%d spect=%d",
			  mg->readonly, mg->spectator);
		strcpy(mg->error_msg, "error: first mounter must be read-write");
	} else {
		memb->jid = 0;
		mg->our_jid = 0;
		mg->first_mounter = 1;
		mg->first_mounter_done = 0;
		hold_withdraw_locks(mg);
	}
	clear_new(mg);
	start_done(mg);
	mg->init = 0;
}

/* called for the initial start on a rw/ro mounter;
   the existing mounters are running start_participant() */

void start_participant_init(struct mountgroup *mg)
{
	log_group(mg, "start_participant_init");

	set_our_memb_options(mg);
	send_options(mg);
	hold_withdraw_locks(mg);

	if (mg->journals_msg) {
		_receive_journals(mg,
				  mg->journals_msg,
				  mg->journals_msg_len,
				  mg->journals_msg_from);
		free(mg->journals_msg);
		mg->journals_msg = NULL;

		start_participant_init_2(mg);
	} else {
		/* will be called in receive_journals() */
		mg->start2_fn = start_participant_init_2;
	}
	mg->init = 0;
}

/* called for the initial start on a rw/ro mounter after _receive_journals() */

void start_participant_init_2(struct mountgroup *mg)
{
	log_group(mg, "start_participant_init_2 our_jid=%d", mg->our_jid);

	/* we've been given jid of -2 which means we're not permitted
	   to mount the fs; probably because we're trying to mount readonly
	   but the next mounter is required to be rw */

	if (mg->our_jid == -2) {
		strcpy(mg->error_msg, "error: jid is -2, try rw");
		goto out;
	}

	/* fs needs recovery and existing mounters can't recover it,
	   i.e. they're spectator/readonly, so we're told to do
	   first-mounter recovery on the fs. */
	 
	if (first_mounter_recovery(mg)) {
		log_group(mg, "first_mounter_recovery");
		mg->emulate_first_mounter = 1;
		mg->first_mounter = 1;
		mg->first_mounter_done = 0;
	}
 out:
	clear_new(mg);
	start_done(mg);
	mg->init2 = 0;
}

/* called for a non-initial start on a normal mounter */

void start_participant(struct mountgroup *mg, int pos, int neg)
{
	log_group(mg, "start_participant pos=%d neg=%d", pos, neg);

	if (pos) {
		hold_withdraw_locks(mg);

		if (mg->options_msg) {
			_receive_options(mg,
				  	 mg->options_msg,
				  	 mg->options_msg_len,
				  	 mg->options_msg_from);
			free(mg->options_msg);
			mg->options_msg = NULL;

			start_participant_2(mg);
		} else {
			/* will be called in receive_options() */
			mg->start2_fn = start_participant_2;
		}
	} else if (neg) {
		recover_journals(mg);
		process_saved_recovery_status(mg);
	}
}

/* called for a non-initial start on a normal mounter when adding a node,
   after _receive_options().  we need to know if the new node is a spectator
   or not (from options) before deciding if it should be given a journal
   in discover_journals() */

void start_participant_2(struct mountgroup *mg)
{
	log_group(mg, "start_participant_2");

	discover_journals(mg);

	/* If we're the first mounter, and we're adding a second
	   node here, but haven't gotten first_done (others_may_mount) from gfs
	   yet, then don't do the start_done() to complete adding the
	   second node.  Set wait_first_done=1 to have first_recovery_done()
	   call start_done().
	   This also requires that we unblock locking on the first
	   mounter if gfs hasn't done others_may_mount yet. */

	if (mg->init2 && mg->first_mounter && !mg->first_mounter_done) {
		mg->wait_first_done = 1;
		set_sysfs(mg, "block", 0);
		log_group(mg, "delay start_done until others_may_mount");
	} else {
		clear_new(mg);
		start_done(mg);
	}

	mg->init2 = 0;
}

/* called for the initial start on a spectator mounter */

void start_spectator_init(struct mountgroup *mg)
{
	log_group(mg, "start_spectator_init");

	set_our_memb_options(mg);
	send_options(mg);
	hold_withdraw_locks(mg);

	if (mg->journals_msg) {
		_receive_journals(mg,
				  mg->journals_msg,
				  mg->journals_msg_len,
				  mg->journals_msg_from);
		free(mg->journals_msg);
		mg->journals_msg = NULL;

		start_spectator_init_2(mg);
	} else {
		/* will be called in receive_journals() */
		mg->start2_fn = start_spectator_init_2;
	}
	mg->init = 0;
}

/* called for the initial start on a spectator mounter,
   after _receive_journals() */

void start_spectator_init_2(struct mountgroup *mg)
{
	log_group(mg, "start_spectator_init_2 our_jid=%d", mg->our_jid);

	/* we've been given jid of -2 which means we're not permitted
	   to mount the fs; probably because the next mounter must be rw */

	if (mg->our_jid == -2)
		strcpy(mg->error_msg, "error: spectator mount not allowed");
	else
		ASSERT(mg->our_jid == -1);

	clear_new(mg);
	start_done(mg);
	mg->init2 = 0;
}

/* called for a non-initial start on a spectator mounter */

void start_spectator(struct mountgroup *mg, int pos, int neg)
{
	log_group(mg, "start_spectator pos=%d neg=%d", pos, neg);

	if (pos) {
		hold_withdraw_locks(mg);

		if (mg->options_msg) {
			_receive_options(mg,
				  	 mg->options_msg,
					 mg->options_msg_len,
					 mg->options_msg_from);
			free(mg->options_msg);
			mg->options_msg = NULL;

			start_spectator_2(mg);
		} else {
			/* will be called in receive_options() */
			mg->start2_fn = start_spectator_2;
		}
	} else if (neg) {
		recover_journals(mg);
		process_saved_recovery_status(mg);
	}
}

/* called for a non-initial start on a spectator mounter when adding a
   node, after _receive_options() */

void start_spectator_2(struct mountgroup *mg)
{
	log_group(mg, "start_spectator_2");
	discover_journals(mg);
	clear_new(mg);
	start_done(mg);
}

/* If nodeA fails, nodeB is recovering journalA and nodeB fails before
   finishing, then nodeC needs to tell gfs to recover both journalA and
   journalB.  We do this by setting tell_gfs_to_recover back to 1 for
   any nodes that are still on the members_gone list. */

void reset_unfinished_recoveries(struct mountgroup *mg)
{
	struct mg_member *memb;

	list_for_each_entry(memb, &mg->members_gone, list) {
		if (memb->recovery_status &&
		    memb->recovery_status != RS_NEED_RECOVERY) {
			log_group(mg, "retry unfinished recovery "
				  "jid %d nodeid %d",
				  memb->jid, memb->nodeid);
			memb->tell_gfs_to_recover = 1;
			memb->recovery_status = RS_NEED_RECOVERY;
			memb->local_recovery_status = RS_NEED_RECOVERY;
		}
	}
}

/*
   A is rw mount, B mounts rw

   do_start		do_start
   start_participant	start_participant_init
   			send_options
   receive_options
   start_participant_2
   discover_journals
   assign B a jid
   send_journals
   group_start_done
   			receive_journals
			start_participant_init_2
			group_start_done
   do_finish		do_finish
*/

void do_start(struct mountgroup *mg, int type, int member_count, int *nodeids)
{
	int pos = 0, neg = 0;

	mg->start_event_nr = mg->last_start;
	mg->start_type = type;

	log_group(mg, "start %d init %d type %d member_count %d",
		  mg->last_start, mg->init, type, member_count);

	recover_members(mg, member_count, nodeids, &pos, &neg);

	reset_unfinished_recoveries(mg);

	if (mg->init) {
		if (member_count == 1)
			start_first_mounter(mg);
		else if (mg->spectator)
			start_spectator_init(mg);
		else
			start_participant_init(mg);
	} else {
		if (mg->spectator)
			start_spectator(mg, pos, neg);
		else
			start_participant(mg, pos, neg);
	}
}

/* FIXME:

  What repurcussions are there from umount shutting down gfs in the
  kernel before we leave the mountgroup?  We can no longer participate
  in recovery even though we're in the group -- what are the end cases
  that we need to deal with where this causes a problem?  i.e. there
  is a period of time where the mountgroup=A,B,C but the kernel fs
  is only active on A,B, not C.  The mountgroup on A,B can't depend
  on the mg on C to necessarily be able to do some things (recovery).

  At least in part, it means that after we do an umount and have
  removed the instance of this fs in the kernel, we'll still get
  stop/start/finish callbacks from groupd for which we'll attempt
  and fail to: block/unblock gfs kernel activity, initiate gfs
  journal recovery, get recovery-done signals fromt eh kernel.
  
  We don't want to hang groupd event processing by failing to send
  an ack (stop_done/start_done) back to groupd when it needs one
  to procede.  In the case where we get a start for a failed node
  that needs journal recovery, we have a problem because we wait to
  call group_start_done() until gfs in the kernel to signal that
  the journal recovery is done.  If we've unmounted gfs isn't there
  any more to give us this signal and we'll never call start_done. */

int do_terminate(struct mountgroup *mg)
{
	log_group(mg, "termination of our unmount leave");
	release_withdraw_locks(mg);
	return 0;
}

