#include "gfs_daemon.h"
#include "config.h"
#include "cpg-old.h"
#include "libgroup.h"

#define ASSERT(x) \
do { \
	if (!(x)) { \
		log_error("Assertion failed on line %d of file %s\n" \
			  "Assertion:  \"%s\"\n", __LINE__, __FILE__, #x); \
	} \
} while (0)

#define JID_INIT	-9

/* mg_member opts bit field */

enum {
	MEMB_OPT_RW = 1,
	MEMB_OPT_RO = 2,
	MEMB_OPT_SPECT = 4,
	MEMB_OPT_RECOVER = 8,
};

/* mg_member state: local_recovery_status, recovery_status */

enum {
	RS_NEED_RECOVERY = 1,
	RS_SUCCESS,
	RS_GAVEUP,
	RS_NOFS,
	RS_READONLY,
};

extern group_handle_t gh;

/* cpg message protocol
   1.0.0 is initial version
   2.0.0 is incompatible with 1.0.0 and allows plock ownership */
static unsigned int protocol_v100[3] = {1, 0, 0};
static unsigned int protocol_v200[3] = {2, 0, 0};
static unsigned int protocol_active[3];


static void send_journals(struct mountgroup *mg, int nodeid);


static char *msg_name(int type)
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
	case MSG_MOUNT_STATUS:
		return "MSG_MOUNT_STATUS";
	case MSG_RECOVERY_STATUS:
		return "MSG_RECOVERY_STATUS";
	case MSG_RECOVERY_DONE:
		return "MSG_RECOVERY_DONE";
	case MSG_WITHDRAW:
		return "MSG_WITHDRAW";
	}
	return "unknown";
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

int send_group_message_old(struct mountgroup *mg, int len, char *buf)
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

	return _send_message(cpg_handle_daemon, buf, len, type);
}

static struct mg_member *find_memb_nodeid(struct mountgroup *mg, int nodeid)
{
	struct mg_member *memb;

	list_for_each_entry(memb, &mg->members, list) {
		if (memb->nodeid == nodeid)
			return memb;
	}
	return NULL;
}

static struct mg_member *find_memb_jid(struct mountgroup *mg, int jid)
{
	struct mg_member *memb;

	list_for_each_entry(memb, &mg->members, list) {
		if (memb->jid == jid)
			return memb;
	}
	return NULL;
}

static void notify_mount_client(struct mountgroup *mg)
{
	struct mg_member *memb;

	if (!mg->mount_client_result && mg->mount_client_delay) {
		log_group(mg, "notify_mount_client delayed");
		return;
	}

	client_reply_join_full(mg, mg->mount_client_result);

	if (mg->mount_client_result) {
		log_group(mg, "leaving due to mount error: %d",
			  mg->mount_client_result);

		memb = find_memb_nodeid(mg, our_nodeid);
		if (memb->finished)
			group_leave(gh, mg->name);
		else {
			log_group(mg, "delay leave until after join");
			mg->group_leave_on_finish = 1;
		}
	} else {
		mg->mount_client_notified = 1;
	}
}

/* we can receive recovery_status messages from other nodes doing start before
   we actually process the corresponding start callback ourselves */

void save_message_old(struct mountgroup *mg, char *buf, int len, int from,
		      int type)
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

static int first_mounter_recovery(struct mountgroup *mg)
{
	struct mg_member *memb;

	list_for_each_entry(memb, &mg->members, list) {
		if (memb->opts & MEMB_OPT_RECOVER)
			return memb->nodeid;
	}
	return 0;
}

static int local_first_mounter_recovery(struct mountgroup *mg)
{
	int nodeid;

	nodeid = first_mounter_recovery(mg);
	if (nodeid == our_nodeid)
		return 1;
	return 0;
}

int remote_first_mounter_recovery(struct mountgroup *mg)
{
	int nodeid;

	nodeid = first_mounter_recovery(mg);
	if (nodeid && (nodeid != our_nodeid))
		return 1;
	return 0;
}

static void start_done(struct mountgroup *mg)
{
	log_group(mg, "start_done %d", mg->start_event_nr);
	group_start_done(gh, mg->name, mg->start_event_nr);
}

void send_withdraw_old(struct mountgroup *mg)
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

	send_group_message_old(mg, len, buf);

	free(buf);
}

static void receive_withdraw(struct mountgroup *mg, char *buf, int len, int from)
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

static void send_recovery_status(struct mountgroup *mg)
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

	send_group_message_old(mg, len, buf);

	free(buf);
}

/* Note: we can get more than one node reporting success in recovering
   the journal for a failed node.  The first has really recovered it,
   the rest have found the fs clean and report success. */

static void _receive_recovery_status(struct mountgroup *mg, char *buf, int len,
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

static void process_saved_recovery_status(struct mountgroup *mg)
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

static void assign_next_first_mounter(struct mountgroup *mg)
{
	struct mg_member *memb, *next = NULL;
	int low = -1;

	list_for_each_entry(memb, &mg->members, list) {
		if (memb->jid == -2)
			continue;
		if (memb->jid == -9)
			continue;
		if (memb->spectator || memb->readonly || memb->withdrawing ||
		    memb->ms_kernel_mount_done)
			continue;
		if (low == -1 || memb->nodeid < low) {
			next = memb;
			low = memb->nodeid;
		}
	}

	if (next) {
		log_group(mg, "next first mounter is %d jid %d opts %x",
			  next->nodeid, next->jid, next->opts);
		next->opts |= MEMB_OPT_RECOVER;
		ASSERT(next->jid >= 0);
	} else
		log_group(mg, "no next mounter available yet");
}

#define SEND_MS_INTS 4

void send_mount_status_old(struct mountgroup *mg)
{
	struct gdlm_header *hd;
	int len, *p;
	char *buf;

	len = sizeof(struct gdlm_header) + (SEND_MS_INTS * sizeof(int));

	buf = malloc(len);
	if (!buf)
		return;
	memset(buf, 0, len);

	hd = (struct gdlm_header *)buf;
	hd->type = MSG_MOUNT_STATUS;
	hd->nodeid = our_nodeid;
	hd->to_nodeid = 0;

	p = (int *) (buf + sizeof(struct gdlm_header));

	p[0] = cpu_to_le32(mg->first_mounter);
	p[1] = cpu_to_le32(mg->kernel_mount_error);
	p[2] = 0; /* unused */
	p[3] = 0; /* unused */

	log_group(mg, "send_mount_status kernel_mount_error %d "
		      "first_mounter %d",
		      mg->kernel_mount_error,
		      mg->first_mounter);

	send_group_message_old(mg, len, buf);

	free(buf);
}

static void _receive_mount_status(struct mountgroup *mg, char *buf, int len,
				  int from)
{
	struct mg_member *memb, *us;
	int *p;

	p = (int *) (buf + sizeof(struct gdlm_header));

	memb = find_memb_nodeid(mg, from);
	if (!memb) {
		log_group(mg, "_receive_mount_status no node %d", from);
		return;
	}

	memb->ms_kernel_mount_done = 1;
	memb->ms_first_mounter = le32_to_cpu(p[0]);
	memb->ms_kernel_mount_error = le32_to_cpu(p[1]);

	log_group(mg, "_receive_mount_status from %d kernel_mount_error %d "
		      "first_mounter %d opts %x", from,
		      memb->ms_kernel_mount_error, memb->ms_first_mounter,
		      memb->opts);

	if (memb->opts & MEMB_OPT_RECOVER) {
		ASSERT(memb->ms_first_mounter);
	}
	if (memb->ms_first_mounter) {
		ASSERT(memb->opts & MEMB_OPT_RECOVER);
	}

	if (memb->ms_first_mounter) {
		memb->opts &= ~MEMB_OPT_RECOVER;

		if (!memb->ms_kernel_mount_error) {
			/* the first mounter has successfully mounted, we can
			   go ahead and mount now */

			if (mg->mount_client_delay) {
				mg->mount_client_delay = 0;
				notify_mount_client(mg);
			}
		} else {
			/* first mounter mount failed, next low node should be
			   made first mounter */

			memb->jid = -2;
			if (from == our_nodeid)
				mg->our_jid = -2;

			assign_next_first_mounter(mg);

			/* if we became the next first mounter, then notify
			   mount client */

			us = find_memb_nodeid(mg, our_nodeid);
			if (us->opts & MEMB_OPT_RECOVER) {
				log_group(mg, "we are next first mounter");
				mg->first_mounter = 1;
				mg->first_mounter_done = 0;
				mg->mount_client_delay = 0;
				notify_mount_client(mg);
			}
		}
	}
}

static void receive_mount_status(struct mountgroup *mg, char *buf, int len,
				 int from)
{
	log_group(mg, "receive_mount_status from %d len %d last_cb %d",
		  from, len, mg->last_callback);

	if (!mg->got_our_options) {
		log_group(mg, "ignore mount_status from %d", from);
		return;
	}

	if (!mg->got_our_journals)
		save_message_old(mg, buf, len, from, MSG_MOUNT_STATUS);
	else
		_receive_mount_status(mg, buf, len, from);
}

/* We delay processing mount_status msesages until we receive the journals
   message for our own mount.  Our journals message is a snapshot of the memb
   list at the time our options message is received on the remote node.  We
   ignore any messages that would change the memb list prior to seeing our own
   options message and we save any messages that would change the memb list
   after seeing our own options message and before we receive the memb list
   from the journals message. */

static void process_saved_mount_status(struct mountgroup *mg)
{
	struct save_msg *sm, *sm2;

	if (list_empty(&mg->saved_messages))
		return;

	log_group(mg, "process_saved_mount_status");

	list_for_each_entry_safe(sm, sm2, &mg->saved_messages, list) {
		if (sm->type != MSG_MOUNT_STATUS)
			continue;
		_receive_mount_status(mg, sm->buf, sm->len, sm->nodeid);
		list_del(&sm->list);
		free(sm);
	}
}

static void receive_recovery_status(struct mountgroup *mg, char *buf, int len,
			     int from)
{
	switch (mg->last_callback) {
	case DO_STOP:
		save_message_old(mg, buf, len, from, MSG_RECOVERY_STATUS);
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

static void send_recovery_done(struct mountgroup *mg)
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

	send_group_message_old(mg, len, buf);

	free(buf);
}

static void receive_recovery_done(struct mountgroup *mg, char *buf, int len,
				  int from)
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
	mg->kernel_stopped = 0; /* for queries */
	set_sysfs(mg, "block", 0);
}

void send_remount_old(struct mountgroup *mg, struct gfsc_mount_args *ma)
{
	struct gdlm_header *hd;
	char *buf;
	int len;
	int ro = strstr(ma->options, "ro") ? 1 : 0;

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

	log_group(mg, "send_remount_old len %d \"%s\"", len,
		  buf+sizeof(struct gdlm_header));

	send_group_message_old(mg, len, buf);

	free(buf);
}

static void receive_remount(struct mountgroup *mg, char *buf, int len, int from)
{
	struct mg_member *memb;
	char *options;
	int rw = 0, ro = 0;
	int result = 0;

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
		result = -EINVAL;
		goto out;
	}

	/* FIXME: check if we've even fully completed our normal mount yet
	   (received our own mount-status?)  if not, then disallow remount */

	/* FIXME: going ro->rw may mean we can now do journal or first-mounter
	   recovery that we couldn't do before. */

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
		if (!result) {
			mg->rw = memb->rw;
			mg->ro = memb->readonly;
		}
		client_reply_remount(mg, mg->remount_client, result);
	}

	log_group(mg, "receive_remount from %d rw=%d ro=%d opts=%x",
		  from, memb->rw, memb->readonly, memb->opts);
}

static void set_our_memb_options(struct mountgroup *mg)
{
	struct mg_member *memb;
	memb = find_memb_nodeid(mg, our_nodeid);
	ASSERT(memb);

	if (mg->ro) {
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

static void send_options(struct mountgroup *mg)
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

	strncpy(buf+sizeof(struct gdlm_header), mg->mount_args.options,
		MAX_OPTIONS_LEN-1);

	log_group(mg, "send_options len %d \"%s\"", len,
		  buf+sizeof(struct gdlm_header));

	send_group_message_old(mg, len, buf);

	free(buf);
}

/* We set the new member's jid to the lowest unused jid.  If we're the lowest
   existing member (by nodeid), then send jid info to the new node. */

/* Look at rw/ro/spectator status of all existing mounters and whether
   we need to do recovery.  Based on that, decide if the current mount
   mode (ro/spectator) is permitted; if not, set jid = -2.  If spectator
   mount and it's ok, set jid = -1.  If ro or rw mount and it's ok, set
   real jid. */

static int assign_journal(struct mountgroup *mg, struct mg_member *new)
{
	struct mg_member *memb, *memb_recover = NULL, *memb_mounted = NULL;
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

		if (memb->opts & MEMB_OPT_RECOVER) {
			memb_recover = memb;
			log_group(mg, "assign_journal: memb %d has OPT_RECOVER",
				  memb->nodeid);
		}

		if (memb->ms_kernel_mount_done && !memb->ms_kernel_mount_error)
			memb_mounted = memb;
	}

	log_group(mg, "assign_journal: total %d iv %d rw %d ro %d spect %d "
		  "needs_recovery %d", total, invalid_count, rw_count,
		  ro_count, spect_count, mg->needs_recovery);

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

	/* Repeat first-mounter recovery: the fs has been mounted and in-use,
	   but nodes have failed and none of the current mounters has been able
	   to do recovery (all remaining nodes may be ro/spect for example).
	   This puts us into the special "needs_recovery" state where new
	   mounters are asked to do first-mounter recovery of the fs while
	   the current mounters sit in a blocked state. */

	if (mg->needs_recovery) {
		if (!memb_recover) {
			log_group(mg, "assign_journal: needs_recovery: "
				  "new memb %d gets OPT_RECOVER",
				  new->nodeid);
			new->opts |= MEMB_OPT_RECOVER;
		} else {
			log_group(mg, "assign_journal: needs_recovery: "
				  "new memb %d memb %d has OPT_RECOVER",
				  new->nodeid, memb_recover->nodeid);
		}
		goto out;
	}

	/* Initial first-mounter recovery: the fs is coming online, the first
	   mg member assumes first-mounter role and other nodes join the mg
	   while the first-mounter is working.  These non-first mounters wait
	   for the first-mounter to finish before notifying mount.gfs.  If the
	   first-mounter fails, one of them will become the first-mounter. */

	/* it shouldn't be possible to have someone doing first mounter
	   recovery and also have someone with the fs fully mounted */

	if (memb_mounted && memb_recover) {
		log_group(mg, "memb_mounted %d memb_recover %d",
			  memb_mounted->nodeid, memb_recover->nodeid);
		ASSERT(0);
	}

	/* someone has successfully mounted the fs which means the fs doesn't
	   need first mounter recovery */

	if (memb_mounted) {
		log_group(mg, "assign_journal: no first recovery needed %d",
			  memb_mounted->nodeid);
		goto out;
	}

	/* someone is currently doing first mounter recovery, they'll send
	   mount_status when they're done letting everyone know the result */

	if (memb_recover) {
		log_group(mg, "assign_journal: %d doing first recovery",
			  memb_recover->nodeid);
		goto out;
	}

	/* when we received our journals, no one was flagged with OPT_RECOVER
	   which means no first mounter recovery is needed or is current */

	if (mg->global_first_recover_done) {
		log_group(mg, "assign_journal: global_first_recover_done");
		goto out;
	}

	/* no one has done kernel mount successfully and no one is doing first
	   mounter recovery, the new node gets to try first mounter recovery */

	log_group(mg, "kernel_mount_done %d kernel_mount_error %d "
		      "first_mounter %d first_mounter_done %d",
		      mg->kernel_mount_done, mg->kernel_mount_error,
		      mg->first_mounter, mg->first_mounter_done);

	log_group(mg, "assign_journal: new memb %d gets OPT_RECOVER for: "
		  "fs not mounted", new->nodeid);
	new->opts |= MEMB_OPT_RECOVER;

 out:
	log_group(mg, "assign_journal: new member %d got jid %d opts %x",
		  new->nodeid, new->jid, new->opts);

	if (mg->master_nodeid == our_nodeid) {
		store_plocks(mg, new->nodeid);
		send_journals(mg, new->nodeid);
	}
	return 0;
}

static void _receive_options(struct mountgroup *mg, char *buf, int len,
			     int from)
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

static void receive_options(struct mountgroup *mg, char *buf, int len, int from)
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
		save_message_old(mg, buf, len, from, MSG_OPTIONS);
	else
		_receive_options(mg, buf, len, from);
}

static void process_saved_options(struct mountgroup *mg)
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

static void send_journals(struct mountgroup *mg, int nodeid)
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

	send_group_message_old(mg, len, buf);

	free(buf);
}

static void received_our_jid(struct mountgroup *mg)
{
	log_group(mg, "received_our_jid %d", mg->our_jid);

	/* we've been given jid of -2 which means we're not permitted
	   to mount the fs; probably because we're trying to mount readonly
	   but the next mounter is required to be rw */

	if (mg->our_jid == -2) {
		mg->mount_client_result = -EUCLEAN;
		goto out;
	}

	/* fs needs recovery and existing mounters can't recover it,
	   i.e. they're spectator/readonly or the first mounter's
	   mount(2) failed, so we're told to do first-mounter recovery
	   on the fs. */

	if (local_first_mounter_recovery(mg)) {
		log_group(mg, "we're told to do first mounter recovery");
		mg->first_mounter = 1;
		mg->first_mounter_done = 0;
		mg->mount_client_delay = 0;
		mg->save_plocks = 0;
		goto out;
	} else if (remote_first_mounter_recovery(mg)) {
		/* delay notifying mount client until we get a successful
		   mount status from the first mounter */
		log_group(mg, "other node doing first mounter recovery, "
			  "set mount_client_delay");
		mg->mount_client_delay = 1;
		mg->save_plocks = 0;
		return;
	}

	retrieve_plocks(mg);
	mg->save_plocks = 0;
	process_saved_plocks(mg);
 out:
	notify_mount_client(mg);
}

static void _receive_journals(struct mountgroup *mg, char *buf, int len,
			      int from)
{
	struct mg_member *memb, *memb2;
	struct gdlm_header *hd;
	int *ids, count, i, nodeid, jid, opts;
	int current_first_recover = 0;

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
		}
		if (!memb)
			continue;

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

		if (opts & MEMB_OPT_RECOVER)
			current_first_recover = 1;
	}

	/* FIXME: use global_first_recover_done more widely instead of
	   as a single special case */
	if (!current_first_recover)
		mg->global_first_recover_done = 1;

	process_saved_mount_status(mg);

	/* we delay processing any options messages from new mounters
	   until after we receive the journals message for our own mount */

	process_saved_options(mg);

	received_our_jid(mg);
}

static void receive_journals(struct mountgroup *mg, char *buf, int len,
			     int from)
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

static int add_member(struct mountgroup *mg, int nodeid)
{
	struct mg_member *memb;

	memb = malloc(sizeof(struct mg_member));
	if (!memb)
		return -ENOMEM;

	memset(memb, 0, sizeof(struct mg_member));

	memb->nodeid = nodeid;
	memb->jid = JID_INIT;
	add_ordered_member(mg, memb);
	mg->memb_count++;

	if (!mg->init)
		memb->needs_journals = 1;

	return 0;
}

static int is_member(struct mountgroup *mg, int nodeid)
{
	struct mg_member *memb;

	list_for_each_entry(memb, &mg->members, list) {
		if (memb->nodeid == nodeid)
			return 1;
	}
	return 0;
}

static int is_removed(struct mountgroup *mg, int nodeid)
{
	struct mg_member *memb;

	list_for_each_entry(memb, &mg->members_gone, list) {
		if (memb->nodeid == nodeid)
			return 1;
	}
	return 0;
}

/* New mounters may be waiting for a journals message that a failed node (as
   master) would have sent.  If the master failed and we're the new master,
   then send a journals message to any nodes for whom we've not seen a journals
   message.  We also need to checkpoint the plock state for the new nodes to
   read after they get their journals message. */

static void resend_journals(struct mountgroup *mg)
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

static void update_master_nodeid(struct mountgroup *mg)
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

static void recover_members(struct mountgroup *mg, int num_nodes,
			    int *nodeids, int *pos_out, int *neg_out)
{
	struct mg_member *memb, *safe, *memb_gone_recover = NULL;
	int i, found, id, pos = 0, neg = 0, prev_master_nodeid;
	int master_failed = 0;

	/* move departed nodes from members list to members_gone */

	list_for_each_entry_safe(memb, safe, &mg->members, list) {
		found = 0;
		for (i = 0; i < num_nodes; i++) {
			if (memb->nodeid == nodeids[i]) {
				found = 1;
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
			    memb->jid != -2 &&
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

			if (mg->master_nodeid == memb->nodeid &&
			    memb->gone_type == GROUP_NODE_FAILED)
				master_failed = 1;

			if (memb->opts & MEMB_OPT_RECOVER)
				memb_gone_recover = memb;
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


	/* The master failed and we're the new master, we need to:

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

	/* Do we need a new first mounter?

	   If we've not gotten a journals message yet (implies we're mounting)
	   and there's only one node left in the group (us, after removing the
	   failed node), then it's possible that the failed node was doing
	   first mounter recovery, so we need to become first mounter.

	   If we've received a journals message, we can check if the failed
	   node was doing first mounter recovery (MEMB_OPT_RECOVER set) and
	   if so select the next first mounter. */

	if (!neg)
		return;

	if (!mg->got_our_journals && mg->memb_count == 1) {
		log_group(mg, "we are left alone, act as first mounter");
		unlink_checkpoint(mg);
		memb = find_memb_nodeid(mg, our_nodeid);
		memb->jid = 0;
		memb->opts |= MEMB_OPT_RECOVER;
		mg->our_jid = 0;
		mg->first_mounter = 1;
		mg->first_mounter_done = 0;
		mg->got_our_options = 1;
		mg->got_our_journals = 1;
		mg->mount_client_delay = 0;
		notify_mount_client(mg);
		return;
	}

	if (memb_gone_recover) {
		log_group(mg, "failed node %d had MEMB_OPT_RECOVER",
			  memb_gone_recover->nodeid);
		memb_gone_recover->tell_gfs_to_recover = 0;
	}

	if (memb_gone_recover && mg->got_our_journals) {
		assign_next_first_mounter(mg);
		memb = find_memb_nodeid(mg, our_nodeid);
		if (memb->opts & MEMB_OPT_RECOVER) {
			log_group(mg, "first mounter failed, we get "
				  "MEMB_OPT_RECOVER");
			unlink_checkpoint(mg);
			memb->opts |= MEMB_OPT_RECOVER;
			mg->first_mounter = 1;
			mg->first_mounter_done = 0;
			mg->mount_client_delay = 0;
			notify_mount_client(mg);
		}
	}
}

int gfs_join_mountgroup_old(struct mountgroup *mg, struct gfsc_mount_args *ma)
{
	int rv;

	if (strlen(ma->options) > MAX_OPTIONS_LEN-1) {
		log_error("join: options too long %zu", strlen(ma->options));
		return -EMLINK;
	}

	rv = group_join(gh, mg->name);
	if (rv)
		return -ENOTCONN;
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
   a recovery_done signal that will never arrive. */

static void recover_journals(struct mountgroup *mg)
{
	struct mg_member *memb;
	int rv;

	if (mg->spectator ||
	    mg->ro ||
	    mg->withdraw_suspend ||
	    mg->our_jid == JID_INIT ||
	    mg->kernel_mount_error ||
	    !mg->mount_client_notified ||
	    !mg->got_kernel_mount ||
	    !mg->kernel_mount_done) {
		log_group(mg, "recover_journals: unable %d,%d,%d,%d,%d,%d,%d,%d",
			  mg->spectator,
			  mg->ro,
			  mg->withdraw_suspend,
			  mg->our_jid,
			  mg->kernel_mount_error,
			  mg->mount_client_notified,
			  mg->got_kernel_mount,
			  mg->kernel_mount_done);

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
   journals are ok, an others_may_mount/first_done. */

/* When gfs does first-mount recovery, the mount(2) fails if it can't
   recover one of the journals.  If we get o_m_m, then we know it was
   able to successfully recover all the journals. */

/* When we're the first mounter, gfs does recovery on all the journals
   and does "recovery_done" callbacks when it finishes each.  We ignore
   these and wait for gfs to be finished with all at which point it calls
   others_may_mount() and first_done is set. */

static int kernel_recovery_done_first(struct mountgroup *mg)
{
	int rv, first_done;

	rv = read_sysfs_int(mg, "first_done", &first_done);
	if (rv < 0)
		return rv;

	log_group(mg, "kernel_recovery_done_first first_done %d", first_done);

	if (mg->kernel_mount_done)
		log_group(mg, "FIXME: assuming kernel_mount_done comes after "
			  "first_done");

	if (first_done) {
		mg->first_mounter_done = 1;
		send_recovery_done(mg);
	}

	return 0;
}

static int need_kernel_recovery_done(struct mountgroup *mg)
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

int process_recovery_uevent_old(char *table)
{
	struct mountgroup *mg;
	struct mg_member *memb;
	char *name = strstr(table, ":") + 1;
	char *ss;
	int rv, jid_done, status, found = 0;

	mg = find_mg(name);
	if (!mg) {
		log_error("recovery_done: unknown mount group %s", table);
		return -1;
	}

	if (mg->first_mounter && !mg->first_mounter_done)
		return kernel_recovery_done_first(mg);

	rv = read_sysfs_int(mg, "recover_done", &jid_done);
	if (rv < 0)
		return rv;

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

	rv = read_sysfs_int(mg, "recover_status", &status);
	if (rv < 0) {
		log_group(mg, "recovery_done jid %d nodeid %d sysfs error %d",
			  memb->jid, memb->nodeid, rv);
		memb->local_recovery_status = RS_NOFS;
		goto out;
	}

	switch (status) {
	case LM_RD_GAVEUP:
		/*
		 * This is unfortunate; it's needed for bz 442451 where
		 * gfs-kernel fails to acquire the journal lock on all nodes
		 * because a withdrawing node has not yet called
		 * dlm_release_lockspace() to free it's journal lock.  With
		 * this, all nodes should repeatedly try to to recover the
		 * journal of the withdrawn node until the withdrawing node
		 * clears its dlm locks, and gfs on each of the remaining nodes
		 * succeeds in doing the recovery.
		 */

		if (memb->withdrawing) {
			log_group(mg, "recovery_done jid %d nodeid %d retry "
				  "for withdraw", memb->jid, memb->nodeid);
			memb->tell_gfs_to_recover = 1;
			memb->wait_gfs_recover_done = 0;
			usleep(500000);
		}

		memb->local_recovery_status = RS_GAVEUP;
		ss = "gaveup";
		break;
	case LM_RD_SUCCESS:
		memb->local_recovery_status = RS_SUCCESS;
		ss = "success";
		break;
	default:
		log_error("recovery_done: jid %d nodeid %d unknown status %d",
			  memb->jid, memb->nodeid, status);
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

static void leave_mountgroup(struct mountgroup *mg, int mnterr)
{
	/* sanity check: we should already have gotten the error from
	   the mount.gfs mount_done; so this shouldn't happen */

	if (mnterr && !mg->kernel_mount_error) {
		log_error("leave: mount_error is new %d %d",
			  mg->kernel_mount_error, mnterr);
	}

	mg->leaving = 1;

	/* Check to see if we're waiting for a kernel recovery_done to do a
	   start_done().  If so, call the start_done() here because we won't be
	   getting anything else from gfs-kernel which is now gone. */

	if (need_kernel_recovery_done(mg)) {
		log_group(mg, "leave: fill in start_done");
		start_done(mg);
	}

	group_leave(gh, mg->name);
}

void do_leave_old(char *table, int mnterr)
{
	struct mountgroup *mg;
	char *name = strstr(table, ":") + 1;

	log_debug("do_leave_old %s mnterr %d", table, mnterr);

	list_for_each_entry(mg, &withdrawn_mounts, list) {
		if (strcmp(mg->name, name))
			continue;
		log_group(mg, "leave for withdrawn fs");
		list_del(&mg->list);
		free_mg(mg);
		return;
	}

	mg = find_mg(name);
	if (!mg) {
		log_error("do_leave_old: %s not found", name);
		return;
	}

	leave_mountgroup(mg, mnterr);
}

/* When mounting a fs, we first join the mountgroup, then tell mount.gfs
   to procede with the kernel mount.  Once we're in the mountgroup, we
   can get a stop callback at any time, which requires us to block the
   fs by setting a sysfs file.  If the kernel mount is slow, we can get
   a stop callback and try to set the sysfs file before the kernel mount
   has actually created the sysfs files for the fs.  This function delays
   any further processing until the sysfs files exist. */

/* This function returns 0 when the kernel mount is successfully detected
   and we know that do_stop() will be able to block the fs.
   This function returns a negative error if it detects the kernel mount
   has failed which means there's nothing to stop and do_stop() can assume
   an implicit stop. */

/* wait for
   - kernel mount to get to the point of creating sysfs files we
     can read (and that do_stop can then use), or
   - kernel mount to fail causing mount.gfs to send us a MOUNT_DONE
     which we read in process_connection() */

static int wait_for_kernel_mount(struct mountgroup *mg)
{
	int rv, val;

	while (1) {
		/* This is the standard way we leave this loop, where the
		   kernel mount gets to the point of creating the sysfs files
		   which we see by successfully reading "id".  With the
		   sysfs files in place, do_stop() will be able to block
		   the kernel. */

		rv = read_sysfs_int(mg, "id", &val);
		if (!rv)
			break;
		usleep(100000);

		/* kernel_mount_done is set by mount_done_old() which is called
		   by process_connection() if mount.gfs sends MOUNT_DONE. */

		if (mg->kernel_mount_done && !mg->kernel_mount_error) {
			/* mount(2) was successful and we should be able
			   to read "id" very shortly... */
			continue;
		}

		if (mg->kernel_mount_done && mg->kernel_mount_error) {
			/* mount(2) failed, stop becomes implicit */
			break;
		}

		/* this should either do nothing and return immediatley, or
		   read a MOUNT_DONE from mount.gfs and call mount_done_old()
		   which will set kernel_mount_done and set kernel_mount_error */

		process_connection(mg->mount_client);
	}

	return rv;
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

	if (mg->first_mounter && !mg->kernel_mount_done) {
		log_group(mg, "do_stop skip during first mount recovery");
		goto out;
	}

	for (;;) {
		rv = set_sysfs(mg, "block", 1);
		if (!rv) {
			mg->kernel_stopped = 1; /* for queries */
			break;
		}

		/* We get an error trying to block gfs, this could be due
		   to a number of things:
		   1. if the kernel instance of gfs existed before but now
		      we can't see it, that must mean it's been unmounted,
		      so it's implicitly stopped
		   2. we're in the process of mounting and gfs hasn't created
		      the sysfs files for this fs yet
		   3. we're mounting and mount(2) returned an error
		   4. we're mounting but haven't told mount.gfs to go ahead
		      with mount(2) yet
		   We also need to handle the situation where we get here in
		   case 2 but it turns into case 3 while we're in
		   wait_for_kernel_mount() */

		if (mg->got_kernel_mount) {
			log_group(mg, "do_stop skipped fs unmounted");
			break;
		}

		if (mg->mount_client_notified) {
			if (!mg->kernel_mount_error) {
				log_group(mg, "do_stop wait for kernel mount");
				rv = wait_for_kernel_mount(mg);
				if (rv < 0)
					break;
			} else {
				log_group(mg, "do_stop ignore, failed mount");
				break;
			}
		} else {
			log_group(mg, "do_stop causes mount_client_delay");
			mg->mount_client_delay = 1;
			break;
		}
	}
 out:
	group_stop_done(gh, mg->name);
	return 0;
}

/*  After a start that initiated a recovery, everyone will go and see if they
    can do recovery and try if they can.  If a node can't, it does start_done,
    if it tries and fails, it does start_done, if it tries and succeeds it
    sends a message and then does start_done once it receives's it back.  So,
    when we get a finish we know that we have all the results from the recovery
    cycle and can judge if everything is recovered properly or not.  If so, we
    can unblock locks (in the finish), if not, we leave them blocked (in the
    finish).

    If we leave locks blocked in the finish, then they can only be unblocked
    after someone is able to do the recovery that's needed.  So, leaving locks
    blocked in a finish because recovery hasn't worked puts us into a special
    state: the fs needs recovery, none of the current mounters has been able to
    recover it, all current mounters have locks blocked in gfs, new mounters
    are allowed, nodes can unmount, new mounters are asked to do first-mounter
    recovery, if one of them succeeds then we can all clear this special state
    and unblock locks (the unblock would happen upon recving the success
    message from the new pseudo-first mounter, not as part of a finish), future
    finishes would then go back to being able to unblock locks.

    While in this special state, a new node has been added and asked to do
    first-mounter recovery, other nodes can also be added while the new
    first-mounter is active.  These other nodes don't notify mount.gfs.
    They'll receive the result of the first mounter and if it succeeded they'll
    notify mount.gfs, otherwise one of them will become the next first-mounter
    and notify mount.gfs. */

int do_finish(struct mountgroup *mg)
{
	struct mg_member *memb, *safe;

	log_group(mg, "finish %d needs_recovery %d", mg->last_finish,
		  mg->needs_recovery);

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
			log_error("%s finish: needs recovery jid %d nodeid %d "
				  "status %d", mg->name, memb->jid,
				  memb->nodeid, memb->recovery_status);
			mg->needs_recovery = 1;
		}
	}

	list_for_each_entry(memb, &mg->members, list)
		memb->finished = 1;

	if (mg->group_leave_on_finish) {
		log_group(mg, "leaving group after delay for join to finish");
		group_leave(gh, mg->name);
		mg->group_leave_on_finish = 0;
		return 0;
	}

	if (!mg->needs_recovery) {
		mg->kernel_stopped = 0; /* for queries */
		set_sysfs(mg, "block", 0);

		/* we may have been holding back our local mount due to
		   being stopped/blocked */
		if (mg->mount_client_delay && !first_mounter_recovery(mg)) {
			mg->mount_client_delay = 0;
			notify_mount_client(mg);
		}
	} else
		log_group(mg, "finish: leave locks blocked for needs_recovery");

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

static void start_first_mounter(struct mountgroup *mg)
{
	struct mg_member *memb;

	log_group(mg, "start_first_mounter");
	set_our_memb_options(mg);
	memb = find_memb_nodeid(mg, our_nodeid);
	ASSERT(memb);

	if (mg->ro || mg->spectator) {
		memb->jid = -2;
		mg->our_jid = -2;
		log_group(mg, "start_first_mounter not rw ro=%d spect=%d",
			  mg->ro , mg->spectator);
		mg->mount_client_result = -EUCLEAN;
	} else {
		memb->opts |= MEMB_OPT_RECOVER;
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

static void start_participant_init(struct mountgroup *mg)
{
	log_group(mg, "start_participant_init");
	set_our_memb_options(mg);
	send_options(mg);
	start_done(mg);
}

/* called for a non-initial start on a normal mounter.
   NB we can get here without having received a journals message for
   our (recent) mount yet in which case we don't know the jid or ro/rw
   status of any members, and don't know our own jid. */

static void start_participant(struct mountgroup *mg, int pos, int neg)
{
	log_group(mg, "start_participant pos=%d neg=%d", pos, neg);

	if (pos) {
		start_done(mg);
		/* we save options messages from nodes for whom we've not
		   received a start yet */
		process_saved_options(mg);
	} else if (neg) {
		recover_journals(mg);
		process_saved_recovery_status(mg);
	}
}

/* called for the initial start on a spectator mounter,
   after _receive_journals() */

static void start_spectator_init_2(struct mountgroup *mg)
{
	log_group(mg, "start_spectator_init_2 our_jid=%d", mg->our_jid);

	/* we've been given jid of -2 which means we're not permitted
	   to mount the fs; probably because the next mounter must be rw */

	if (mg->our_jid == -2) {
		mg->mount_client_result = -EUCLEAN;
	} else
		ASSERT(mg->our_jid == -1);

	notify_mount_client(mg);
}

/* called for the initial start on a spectator mounter */

static void start_spectator_init(struct mountgroup *mg)
{
	log_group(mg, "start_spectator_init");
	set_our_memb_options(mg);
	send_options(mg);
	start_done(mg);
	mg->start2_fn = start_spectator_init_2;
}

/* called for a non-initial start on a spectator mounter */

static void start_spectator(struct mountgroup *mg, int pos, int neg)
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

static void reset_unfinished_recoveries(struct mountgroup *mg)
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

	if (mg->withdraw_suspend) {
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

static void deliver_cb(cpg_handle_t handle, struct cpg_name *group_name,
		uint32_t nodeid, uint32_t pid, void *data, int data_len)
{
	do_deliver(nodeid, data, data_len);
}

/* Not sure if purging plocks (driven by confchg) needs to be synchronized with
   the other recovery steps (driven by libgroup) for a node, don't think so.
   Is it possible for a node to have been cleared from the members_gone list
   before this confchg is processed? */

static void confchg_cb(cpg_handle_t handle, struct cpg_name *group_name,
		struct cpg_address *member_list, int member_list_entries,
		struct cpg_address *left_list, int left_list_entries,
		struct cpg_address *joined_list, int joined_list_entries)
{
	struct mountgroup *mg;
	int i, nodeid;

	for (i = 0; i < left_list_entries; i++) {
		nodeid = left_list[i].nodeid;
		list_for_each_entry(mg, &mountgroups, list) {
			if (is_member(mg, nodeid) || is_removed(mg, nodeid))
				purge_plocks(mg, left_list[i].nodeid, 0);
		}
	}
}

static cpg_callbacks_t callbacks = {
	.cpg_deliver_fn = deliver_cb,
	.cpg_confchg_fn = confchg_cb,
};

void process_cpg_old(int ci)
{
	cpg_error_t error;

	error = cpg_dispatch(cpg_handle_daemon, CPG_DISPATCH_ALL);
	if (error != CPG_OK) {
		log_error("cpg_dispatch error %d", error);
		return;
	}

	update_flow_control_status();
}

int setup_cpg_old(void)
{
	static struct cpg_name name;
	cpg_error_t error;
	int fd = 0;

	if (cfgd_plock_ownership)
		memcpy(protocol_active, protocol_v200, sizeof(protocol_v200));
	else
		memcpy(protocol_active, protocol_v100, sizeof(protocol_v100));

	error = cpg_initialize(&cpg_handle_daemon, &callbacks);
	if (error != CPG_OK) {
		log_error("daemon cpg_initialize error %d", error);
		return -1;
	}

	cpg_fd_get(cpg_handle_daemon, &fd);
	if (fd < 0) {
		log_error("daemon cpg_fd_get error %d", error);
		return -1;
	}

	memset(&name, 0, sizeof(name));
	strcpy(name.value, "gfs_controld");
	name.length = 12;

 retry:
	error = cpg_join(cpg_handle_daemon, &name);
	if (error == CPG_ERR_TRY_AGAIN) {
		log_debug("daemon cpg_join retry");
		sleep(1);
		goto retry;
	}
	if (error != CPG_OK) {
		log_error("daemon cpg_join error %d", error);
		cpg_finalize(cpg_handle_daemon);
		return -1;
	}

	log_debug("setup_cpg_old %d", fd);
	return fd;
}

void close_cpg_old(void)
{
	static struct cpg_name name;
	cpg_error_t error;
	int i = 0;

	if (!cpg_handle_daemon || cluster_down)
		return;

	memset(&name, 0, sizeof(name));
	strcpy(name.value, "gfs_controld");
	name.length = 12;

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

