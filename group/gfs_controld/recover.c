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

#define SYSFS_DIR	"/sys/fs"
#define JID_INIT	-9

extern char *clustername;
extern int our_nodeid;
extern group_handle_t gh;
extern int no_withdraw;

struct list_head mounts;
struct list_head withdrawn_mounts;

void send_journals(struct mountgroup *mg, int nodeid);
void start_participant_init_2(struct mountgroup *mg);
void start_spectator_init_2(struct mountgroup *mg);
void start_spectator_2(struct mountgroup *mg);
void notify_mount_client(struct mountgroup *mg);

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
		log_group(mg, "set open %s error %d %d", fname, fd, errno);
		return -1;
	}

	mg->got_kernel_mount = 1;

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
		log_group(mg, "get open %s error %d %d", fname, fd, errno);
		return -1;
	}

	mg->got_kernel_mount = 1;

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

void send_withdraw(struct mountgroup *mg)
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
	hd->type = MSG_WITHDRAW;
	hd->nodeid = our_nodeid;
	hd->to_nodeid = 0;

	log_group(mg, "send_withdraw");

	send_group_message(mg, len, buf);

	free(buf);
}

void receive_withdraw(struct mountgroup *mg, char *buf, int len, int from)
{
	struct mg_member *memb;

	memb = find_memb_nodeid(mg, from);
	if (!memb) {
		log_group(mg, "receive_withdraw no member %d", from);
		return;
	}
	log_group(mg, "receive_withdraw from %d", from);
	memb->withdrawing = 1;

	if (from == our_nodeid)
		group_leave(gh, mg->name);
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

	if (list_empty(&mg->saved_messages))
		return;

	log_group(mg, "process_saved_recovery_status");

	list_for_each_entry_safe(sm, sm2, &mg->saved_messages, list) {
		if (sm->type != MSG_RECOVERY_STATUS)
			continue;
		_receive_recovery_status(mg, sm->buf, sm->len, sm->nodeid);
		list_del(&sm->list);
		free(sm);
	}
}

char *msg_name(int type)
{
	switch (type) {
	case MSG_JOURNAL:
		return "MSG_JOURNAL";
	case MSG_OPTIONS:
		return "MSG_OPTIONS";
	case MSG_REMOUNT:
		return "MSG_REMOUNT";
	case MSG_PLOCK:
		return "MSG_PLOCK";
	case MSG_RECOVERY_STATUS:
		return "MSG_RECOVERY_STATUS";
	case MSG_RECOVERY_DONE:
		return "MSG_RECOVERY_DONE";
	case MSG_WITHDRAW:
		return "MSG_WITHDRAW";
	}
	return "unknown";
}

/* we can receive recovery_status messages from other nodes doing start before
   we actually process the corresponding start callback ourselves */

void save_message(struct mountgroup *mg, char *buf, int len, int from, int type)
{
	struct save_msg *sm;

	sm = malloc(sizeof(struct save_msg) + len);
	if (!sm)
		return;
	memset(sm, 0, sizeof(struct save_msg) + len);

	memcpy(&sm->buf, buf, len);
	sm->type = type;
	sm->len = len;
	sm->nodeid = from;

	log_group(mg, "save %s from %d len %d", msg_name(type), from, len);

	list_add_tail(&sm->list, &mg->saved_messages);
}

void receive_recovery_status(struct mountgroup *mg, char *buf, int len,
			     int from)
{
	switch (mg->last_callback) {
	case DO_STOP:
		save_message(mg, buf, len, from, MSG_RECOVERY_STATUS);
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

/* We set the new member's jid to the lowest unused jid.  If we're the lowest
   existing member (by nodeid), then send jid info to the new node. */

/* Look at rw/ro/spectator status of all existing mounters and whether
   we need to do recovery.  Based on that, decide if the current mount
   mode (ro/spectator) is permitted; if not, set jid = -2.  If spectator
   mount and it's ok, set jid = -1.  If ro or rw mount and it's ok, set
   real jid. */

int assign_journal(struct mountgroup *mg, struct mg_member *new)
{
	struct mg_member *memb;
	int i, total, rw_count, ro_count, spect_count, invalid_count;

	total = rw_count = ro_count = spect_count = invalid_count = 0;

	list_for_each_entry(memb, &mg->members, list) {
		if (memb->nodeid == new->nodeid)
			continue;
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

	log_group(mg, "assign_journal: total %d iv %d rw %d ro %d spect %d",
		  total, invalid_count, rw_count, ro_count, spect_count);

	/* do we let the new member mount? jid=-2 means no.
	   - we only allow an rw mount when the fs needs recovery
	   - we only allow a single rw mount when the fs needs recovery */

	if (mg->needs_recovery) {
		if (!new->rw || rw_count)
			new->jid = -2;
	}

	if (new->jid == -2) {
		log_group(mg, "assign_journal: fail - needs_recovery %d",
			  mg->needs_recovery);
		goto out;
	}

	if (new->spectator) {
		log_group(mg, "assign_journal: new spectator allowed");
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
		log_group(mg, "assign_journal: new member OPT_RECOVER");
		new->opts |= MEMB_OPT_RECOVER;
	}

 out:
	log_group(mg, "assign_journal: new member %d got jid %d",
		  new->nodeid, new->jid);

	if (mg->master_nodeid == our_nodeid) {
		store_plocks(mg, new->nodeid);

		/* if we're the first mounter and haven't gotten
		   others_may_mount yet, then don't send journals until
		   kernel_recovery_done_first so the second node won't mount
		   the fs until omm. */

		if (mg->first_mounter && !mg->first_mounter_done) {
			log_group(mg, "delay sending journals to %d",
				  new->nodeid);
			mg->delay_send_journals = new->nodeid;
		} else
			send_journals(mg, new->nodeid);
	}
	return 0;
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

	log_group(mg, "_receive_options from %d rw=%d ro=%d spect=%d opts=%x",
		  from, memb->rw, memb->readonly, memb->spectator, memb->opts);

	assign_journal(mg, memb);
}

void receive_options(struct mountgroup *mg, char *buf, int len, int from)
{
	struct gdlm_header *hd = (struct gdlm_header *)buf;
	struct mg_member *memb;

	log_group(mg, "receive_options from %d len %d last_cb %d",
		  from, len, mg->last_callback);

	if (hd->nodeid == our_nodeid) {
		mg->got_our_options = 1;
		mg->save_plocks = 1;
		return;
	}

	if (!mg->got_our_options) {
		log_group(mg, "ignore options from %d", from);
		return;
	}

	/* we can receive an options message before getting the start
	   that adds the mounting node that sent the options, or
	   we can receive options messages before we get the journals
	   message for out own mount */

	memb = find_memb_nodeid(mg, from);

	if (!memb || !mg->got_our_journals)
		save_message(mg, buf, len, from, MSG_OPTIONS);
	else
		_receive_options(mg, buf, len, from);
}

void process_saved_options(struct mountgroup *mg)
{
	struct save_msg *sm, *sm2;

	if (list_empty(&mg->saved_messages))
		return;

	log_group(mg, "process_saved_options");

	list_for_each_entry_safe(sm, sm2, &mg->saved_messages, list) {
		if (sm->type != MSG_OPTIONS)
			continue;
		_receive_options(mg, sm->buf, sm->len, sm->nodeid);
		list_del(&sm->list);
		free(sm);
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
	void (*start2)(struct mountgroup *mg) = mg->start2_fn;
	struct mg_member *memb, *memb2;
	struct gdlm_header *hd;
	int *ids, count, i, nodeid, jid, opts;

	hd = (struct gdlm_header *)buf;

	count = (len - sizeof(struct gdlm_header)) / (NUM * sizeof(int));
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

	/* we delay processing any options messages from new mounters
	   until after we receive the journals message for our own mount */
	process_saved_options(mg);

	start2(mg);
}

void receive_journals(struct mountgroup *mg, char *buf, int len, int from)
{
	struct gdlm_header *hd = (struct gdlm_header *)buf;
	struct mg_member *memb;
	int count;

	count = (len - sizeof(struct gdlm_header)) / (NUM * sizeof(int));

	log_group(mg, "receive_journals from %d to %d len %d count %d cb %d",
		  from, hd->to_nodeid, len, count, mg->last_callback);

	/* just like we can receive an options msg from a newly added node
	   before we get the start adding it, we can receive the journals
	   message sent to it before we get the start adding it */

	memb = find_memb_nodeid(mg, hd->to_nodeid);
	if (!memb) {
		log_group(mg, "receive_journals from %d to unknown %d",
			  from, hd->to_nodeid);
		return;
	}
	memb->needs_journals = 0;

	if (hd->to_nodeid && hd->to_nodeid != our_nodeid)
		return;

	if (mg->got_our_journals) {
		log_group(mg, "receive_journals from %d duplicate", from);
		return;
	}
	mg->got_our_journals = 1;

	_receive_journals(mg, buf, len, from);
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
	memb->jid = JID_INIT;
	add_ordered_member(mg, memb);
	mg->memb_count++;

	if (!mg->init)
		memb->needs_journals = 1;

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

/* New mounters may be waiting for a journals message that a failed node (as
   master) would have sent.  If the master failed and we're the new master,
   then send a journals message to any nodes for whom we've not seen a journals
   message.  We also need to checkpoint the plock state for the new nodes to
   read after they get their journals message. */

void resend_journals(struct mountgroup *mg)
{
	struct mg_member *memb;
	int stored_plocks = 0;

	list_for_each_entry(memb, &mg->members, list) {
		if (!memb->needs_journals)
			continue;

		if (!stored_plocks) {
			store_plocks(mg, memb->nodeid);
			stored_plocks = 1;
		}

		log_group(mg, "resend_journals to %d", memb->nodeid);
		send_journals(mg, memb->nodeid);
	}
}

/* The master node is the member of the group with the lowest nodeid who
   was also a member of the last "finished" group, i.e. a member of the
   group the last time it got a finish callback.  The job of the master
   is to send state info to new nodes joining the group, and doing that
   requires that the master has all the state to send -- a new joining
   node that has the lowest nodeid doesn't have any state, which is why
   we add the "finished" requirement. */

void update_master_nodeid(struct mountgroup *mg)
{
	struct mg_member *memb;
	int new = -1, low = -1;

	list_for_each_entry(memb, &mg->members, list) {
		if (low == -1 || memb->nodeid < low)
			low = memb->nodeid;
		if (!memb->finished)
			continue;
		if (new == -1 || memb->nodeid < new)
			new = memb->nodeid;
	}
	mg->master_nodeid = new;
	mg->low_nodeid = low;
}

/* This can happen before we receive a journals message for our mount. */

void recover_members(struct mountgroup *mg, int num_nodes,
 		     int *nodeids, int *pos_out, int *neg_out)
{
	struct mg_member *memb, *safe;
	int i, found, id, pos = 0, neg = 0, prev_master_nodeid;
	int master_failed = 0;

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
			   - failed node was assigned a journal
			   - no journal cb if failed node was spectator
			   - no journal cb if we've already done a journl cb */

			if ((memb->gone_type == GROUP_NODE_FAILED ||
			    memb->withdrawing) &&
			    memb->jid != JID_INIT &&
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
				  memb->withdrawing,
				  memb->jid,
				  memb->spectator,
				  memb->wait_gfs_recover_done);

			purge_plocks(mg, memb->nodeid, 0);

			if (mg->master_nodeid == memb->nodeid &&
			    memb->gone_type == GROUP_NODE_FAILED)
				master_failed = 1;
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

	prev_master_nodeid = mg->master_nodeid;
	update_master_nodeid(mg);

	*pos_out = pos;
	*neg_out = neg;

	log_group(mg, "total members %d master_nodeid %d prev %d",
		  mg->memb_count, mg->master_nodeid, prev_master_nodeid);

	/* the master failed and we're the new master, we need to:
	   - unlink the ckpt that the failed master had open so new ckpts
	     can be created down the road
	   - resend journals msg to any nodes that needed one from the
	     failed master
	   - store plocks in ckpt for the new mounters to read when they
	     get the journals msg from us */

	if (neg && master_failed &&
	    (prev_master_nodeid != -1) &&
	    (prev_master_nodeid != mg->master_nodeid) &&
	    (our_nodeid == mg->master_nodeid)) {
		log_group(mg, "unlink ckpt for failed master %d",
			  prev_master_nodeid);
		unlink_checkpoint(mg);
		resend_journals(mg);
	}

	/* Tricky situation when we're mounting and the failed node was
	   the only other node that had the fs mounted.  If the failed node
	   didn't send us a journals message, we need to: unlink ckpt, pick a
	   journal for ourselves, act like the first mounter of the fs (do
	   first-mounter-recovery, the dead node may have been mounting itself
	   and not finished first-mounter-recovery). */

	else if (neg && mg->memb_count == 1) {
		if (!mg->got_our_journals) {
			log_group(mg, "we are left alone, act first mounter");

			unlink_checkpoint(mg);
			memb = find_memb_nodeid(mg, our_nodeid);
			memb->jid = 0;
			mg->our_jid = 0;
			mg->first_mounter = 1;
			mg->first_mounter_done = 0;
			mg->got_our_options = 1;
			mg->got_our_journals = 1;
			mg->mount_client_delay = 0;
			notify_mount_client(mg);
		} else if (mg->mount_client_notified && !mg->got_kernel_mount) {

			/* FIXME */

			log_group(mg, "FIXME: case not handled");

			/* we got journals message from other node before it
			   died which means it finished first mounter recovery,
			   but we now need to tell gfs to recover the journal
			   after our own mount(2) completes */

		}
	}
}

struct mountgroup *create_mg(char *name)
{
	struct mountgroup *mg;

	mg = malloc(sizeof(struct mountgroup));
	memset(mg, 0, sizeof(struct mountgroup));

	INIT_LIST_HEAD(&mg->members);
	INIT_LIST_HEAD(&mg->members_gone);
	INIT_LIST_HEAD(&mg->resources);
	INIT_LIST_HEAD(&mg->saved_messages);
	mg->init = 1;
	mg->master_nodeid = -1;
	mg->low_nodeid = -1;

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
	int rv;

	memset(&data, 0, sizeof(data));

	rv = group_get_group(0, "default", &data);

	if (rv || strcmp(data.client_name, "fence"))
		return 0;

	if (data.member == 1)
		return 1;
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

	setup_mount_error_fd(mg);

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

		if (mg->wait_first_done)
			start_done(mg);

		if (mg->delay_send_journals)
			send_journals(mg, mg->delay_send_journals);
	}
	return 0;
}

/* recover_members() discovers which nodes need journal recovery
   and moves the memb structs for those nodes into members_gone
   and sets memb->tell_gfs_to_recover on them */

/* we don't want to tell gfs-kernel to do journal recovery for a failed
   node in a number of cases:
   - we're a spectator or readonly mount
   - gfs-kernel is currently withdrawing
   - we're mounting and haven't received a journals message yet
   - we're mounting and got a kernel mount error back from mount.gfs
   - we're mounting and haven't notified mount.gfs yet (to do mount(2))
   - we're mounting and got_kernel_mount is 0, i.e. we've not seen a uevent
     related to the kernel mount yet
   (some of the mounting checks should be obviated by others)

   the problem we're trying to avoid here is telling gfs-kernel to do
   recovery when it can't for some reason and then waiting forever for
   a recovery_done signal that will never arrive.

   FIXME: we want to do more here to avoid telling gfs-kernel to do recovery
   until our mount is really complete.  I want to keep the join/mount
   connection between mount.gfs and gfs_controld open throughout the mount
   and have mount.gfs use it to return the result from mount(2).  Then we'll
   know when the mount(2) is done and we should also be able to remove the
   special mount_error_fd since errors can be sent back through the original
   connection as well. */
 
void recover_journals(struct mountgroup *mg)
{
	struct mg_member *memb;
	int rv;

	if (mg->spectator ||
	    mg->readonly ||
	    mg->withdraw ||
	    mg->our_jid == JID_INIT ||
	    mg->kernel_mount_error ||
	    !mg->mount_client_notified ||
	    !mg->got_kernel_mount) {
		log_group(mg, "recover_journals: unable %d,%d,%d,%d,%d,%d,%d",
			  mg->spectator,
			  mg->readonly,
			  mg->withdraw,
			  mg->our_jid,
			  mg->kernel_mount_error,
			  mg->mount_client_notified,
			  mg->got_kernel_mount);

		list_for_each_entry(memb, &mg->members_gone, list) {
			log_group(mg, "member gone %d jid %d "
				  "tell_gfs_to_recover %d",
				  memb->nodeid, memb->jid,
				  memb->tell_gfs_to_recover);

			if (memb->tell_gfs_to_recover) {
				memb->tell_gfs_to_recover = 0;
				memb->local_recovery_status = RS_READONLY;
			}
		}
		start_done(mg);
		return;
	}

	/* we feed one jid into the kernel for recovery instead of all
	   at once because we need to get the result of each independently
	   through the single recovery_done sysfs file */

	list_for_each_entry(memb, &mg->members_gone, list) {
		if (memb->wait_gfs_recover_done) {
			log_group(mg, "delay new gfs recovery, "
			  	  "wait_gfs_recover_done for nodeid %d jid %d",
			  	  memb->nodeid, memb->jid);
			return;
		}
	}

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

int need_kernel_recovery_done(struct mountgroup *mg)
{
	struct mg_member *memb;

	list_for_each_entry(memb, &mg->members_gone, list) {
		if (memb->wait_gfs_recover_done)
			return 1;
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

	/* sanity check */
	if (need_kernel_recovery_done(mg))
		log_error("recovery_done: should be no pending gfs recoveries");

 out:
	recover_journals(mg);
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

int do_unmount(int ci, char *dir, int mnterr)
{
	struct mountgroup *mg;

	list_for_each_entry(mg, &withdrawn_mounts, list) {
		if (!strcmp(mg->dir, dir)) {
			log_group(mg, "unmount withdrawn fs");
			list_del(&mg->list);
			free(mg);
			return 0;
		}
	}

	mg = find_mg_dir(dir);
	if (!mg) {
		log_error("do_unmount: unknown mount dir %s", dir);
		return -1;
	}

	if (mnterr) {
		log_group(mg, "do_unmount: kernel mount error %d", mnterr);
		mg->kernel_mount_error = mnterr;
		goto out;
	}

	if (mg->withdraw) {
		log_error("do_unmount: fs on %s is withdrawing", dir);
		return -1;
	}

	/* Check to see if we're waiting for a kernel recovery_done to do a
	   start_done().  If so, call the start_done() here because we won't be
	   getting anything else from gfs-kernel which is now gone. */

	if (need_kernel_recovery_done(mg)) {
		log_group(mg, "do_unmount: fill in start_done");
		start_done(mg);
	}
 out:
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
		if (mg->mount_client_delay) {
			log_group(mg, "notify_mount_client delayed");
			return;
		}

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
	} else
		mg->mount_client_notified = 1;
}

void ping_kernel_mount(char *table)
{
	struct mountgroup *mg;
	char buf[MAXLINE];
	char *name = strstr(table, ":") + 1;
	int rv;

	mg = find_mg(name);
	if (!mg)
		return;

	rv = get_sysfs(mg, "id", buf, sizeof(buf));

	log_group(mg, "ping_kernel_mount %d", rv);
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

		memset(buf, 0, sizeof(buf));

		/* attempt to solve the problem described below where we
		   don't get the kernel_mount_error until after the stop and
		   this loop... this mount_error_fd was sent from mount.gfs and
		   mount.gfs will write on this fd if there was a mount(2)
		   error */

		if (!mg->mount_error_fd)
			continue;

		rv = read(mg->mount_error_fd, buf, sizeof(buf));
		if (rv > 0) {
			log_group(mg, "wait_for_kernel_mount: mount error %s",
				  buf);
			mg->kernel_mount_error = 1;
			break;
		}
	}

	close(mg->mount_error_fd);
	mg->mount_error_fd = 0;
}

/* The processing of new mounters (send/recv options, send/recv journals,
   notify mount.gfs) is not very integrated with the stop/start/finish
   callbacks from libgroup.  A start callback just notifies us of a new
   mounter and the options/journals messages drive things from there.
   Recovery for failed nodes _is_ controlled more directly by the
   stop/start/finish callbacks.  So, processing new mounters happens
   independently of recovery and of the libgroup callbacks.  One place
   where they need to intersect, though, is in stopping/suspending
   gfs-kernel:
   - When we get a stop callback, we need to be certain that gfs-kernel
     is blocked.
   - When a mounter notifies mount.gfs to go ahead, gfs-kernel will
     shortly begin running in an unblocked fashion as it goes through
     the kernel mounting process.
   Given this, we need to be sure that if gfs-kernel is supposed to be
   blocked, we don't notify mount.gfs to go ahead and do the kernel mount
   since that starts gfs-kernel in an unblocked state. */

/* - if we're unmounting, the kernel is gone, so no problem.
   - if we've just mounted and notified mount.gfs, then wait for kernel
     mount and then block.
   - if we're mounting and have not yet notified mount.gfs, then set
     a flag that delays the notification until block is set to 0. */

int do_stop(struct mountgroup *mg)
{
	int rv;

	for (;;) {
		rv = set_sysfs(mg, "block", 1);
		if (!rv)
			break;

		/* if the kernel instance of gfs existed before but now
		   we can't see it, that must mean it's been unmounted,
		   so it's implicitly stopped */

		if (mg->got_kernel_mount)
			break;

		if (mg->mount_client_notified) {

			/* this kernel_mount_error check isn't perfect, we
			   could still 1) notify mount.gfs, 2) get a stop cb,
			   3) kernel mount fails, 4) mount.gfs sends a leave
			   with mnterr, 5) we don't recv it and don't set
			   kernel_mount_error because we're stuck in
			   wait_for_kernel_mount() from do_stop.  update:
			   attempt to fix above using mount_error_fd */

			if (!mg->kernel_mount_error)
				wait_for_kernel_mount(mg);
			else {
				log_group(mg, "ignore stop, failed mount");
				break;
			}
		} else {
			mg->mount_client_delay = 1;
			break;
		}
	}

	group_stop_done(gh, mg->name);
	return 0;
}

/* FIXME: what happens if a node is unmounting, others have it in members_gone,
   and it crashes?  It shouldn't need journal recovery since the kernel umount
   happens before leaving the group. */

int do_finish(struct mountgroup *mg)
{
	struct mg_member *memb, *safe;
	int leave_blocked = 0;

	/* members_gone list are the members that were removed from the
	   members list when processing a start.  members are removed
	   from members_gone if their journals have been recovered */

	list_for_each_entry_safe(memb, safe, &mg->members_gone, list) {
		if (!memb->recovery_status) {
			list_del(&memb->list);
			free(memb);
		} else if (memb->recovery_status == RS_SUCCESS) {
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

	list_for_each_entry(memb, &mg->members, list)
		memb->finished = 1;

	if (mg->needs_recovery) {
		log_group(mg, "finish: leave locks blocked for needs_recovery");
		leave_blocked = 1;
	}

	if (!leave_blocked) {
		set_sysfs(mg, "block", 0);

		/* we may have been holding back our local mount due to
		   being stopped/blocked */
		if (mg->mount_client_delay) {
			mg->mount_client_delay = 0;
			notify_mount_client(mg);
		}
	}

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
		mg->got_our_options = 1;
		mg->got_our_journals = 1;
	}
	start_done(mg);
	notify_mount_client(mg);
}

/* called for the initial start on a rw/ro mounter;
   the existing mounters are running start_participant() */

void start_participant_init(struct mountgroup *mg)
{
	log_group(mg, "start_participant_init");
	set_our_memb_options(mg);
	send_options(mg);
	start_done(mg);
	mg->start2_fn = start_participant_init_2;
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

	retrieve_plocks(mg);
	process_saved_plocks(mg);
 out:
	notify_mount_client(mg);
}

/* called for a non-initial start on a normal mounter.
   NB we can get here without having received a journals message for
   our (recent) mount yet in which case we don't know the jid or ro/rw
   status of any members, and don't know our own jid. */

void start_participant(struct mountgroup *mg, int pos, int neg)
{
	log_group(mg, "start_participant pos=%d neg=%d", pos, neg);

	if (pos) {
		/* If we're the first mounter, and we're adding a second
		   node here, but haven't gotten first_done (others_may_mount)
		   from gfs yet, then don't do the start_done() to complete
		   adding the second node.  Set wait_first_done=1 to have
		   first_recovery_done() call start_done().  This also requires
		   that we unblock locking on the first mounter if gfs hasn't
		   done others_may_mount yet. */

		if (mg->first_mounter && !mg->first_mounter_done) {
			mg->wait_first_done = 1;
			set_sysfs(mg, "block", 0);
			log_group(mg, "delay start_done til others_may_mount");
		} else
			start_done(mg);

		mg->start2_fn = NULL;
		process_saved_options(mg);

	} else if (neg) {
		recover_journals(mg);
		process_saved_recovery_status(mg);
	}
}

/* called for the initial start on a spectator mounter */

void start_spectator_init(struct mountgroup *mg)
{
	log_group(mg, "start_spectator_init");
	set_our_memb_options(mg);
	send_options(mg);
	start_done(mg);
	mg->start2_fn = start_spectator_init_2;
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

	notify_mount_client(mg);
}

/* called for a non-initial start on a spectator mounter */

void start_spectator(struct mountgroup *mg, int pos, int neg)
{
	log_group(mg, "start_spectator pos=%d neg=%d", pos, neg);

	if (pos) {
		start_done(mg);
		process_saved_options(mg);
	} else if (neg) {
		recover_journals(mg);
		process_saved_recovery_status(mg);
	}
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
   old method:
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

   new method: decouples stop/start/finish from mount processing
   A is rw mount, B mounts rw

   do_start		do_start
   start_participant	start_participant_init
   start_done		send_options
   			start_done
   do_finish		do_finish

   receive_options
   assign_journal
   send_journals
   			receive_journals
			start_participant_init_2
			notify_mount_client
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
		mg->init = 0;
	} else {
		if (mg->spectator)
			start_spectator(mg, pos, neg);
		else
			start_participant(mg, pos, neg);
	}
}

/*
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
  any more to give us this signal and we'll never call start_done.
 
  update: we should be dealing with all these issues correctly now. */

int do_terminate(struct mountgroup *mg)
{
	purge_plocks(mg, 0, 1);

	if (mg->withdraw) {
		log_group(mg, "termination of our withdraw leave");
		set_sysfs(mg, "withdraw", 1);
		list_move(&mg->list, &withdrawn_mounts);
	} else {
		log_group(mg, "termination of our unmount leave");
		list_del(&mg->list);
		free(mg);
	}

	return 0;
}

/* The basic rule of withdraw is that we don't want to tell the kernel to drop
   all locks until we know gfs has been stopped/blocked on all nodes.  They'll
   be stopped for our leave, we just need to know when they've all arrived
   there.

   A withdrawing node is very much like a readonly node, differences are
   that others recover its journal when they remove it from the group,
   and when it's been removed from the group (gets terminate for its leave),
   it tells the locally withdrawing gfs to clear out locks. */

int do_withdraw(char *table)
{
	struct mountgroup *mg;
	char *name = strstr(table, ":") + 1;

	if (no_withdraw) {
		log_error("withdraw feature not enabled");
		return 0;
	}

	mg = find_mg(name);
	if (!mg) {
		log_error("do_withdraw no mountgroup %s", name);
		return -1;
	}

	mg->withdraw = 1;
	send_withdraw(mg);
	return 0;
}

