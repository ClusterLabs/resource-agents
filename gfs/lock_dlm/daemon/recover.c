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

#define LOCK_DLM_SYSFS_DIR	("/sys/kernel/lock_dlm")

extern char *clustername;
extern int our_nodeid;
extern group_handle_t gh;

struct list_head mounts;

int send_journals_message(int nodeid, char *buf, int len);
struct mountgroup *find_mg(char *name);
int hold_withdraw_locks(struct mountgroup *mg);
void release_withdraw_lock(struct mountgroup *mg, struct mg_member *memb);
void release_withdraw_locks(struct mountgroup *mg);


int set_sysfs(struct mountgroup *mg, char *field, int val)
{
	char fname[512];
	char out[16];
	int rv, fd;

	snprintf(fname, 512, "%s/%s/%s", LOCK_DLM_SYSFS_DIR, mg->name, field);

	log_group(mg, "set %s to %d", fname, val);

	fd = open(fname, O_RDWR);
	if (fd < 0) {
		log_error("open error %s %d %d", fname, fd, errno);
		return -1;
	}

	memset(out, 0, 16);
	sprintf(out, "%d", val);
	rv = write(fd, out, strlen(out));

	if (rv != strlen(out))
		log_error("write error %s %d %d", fname, fd, errno);

	close(fd);
	return 0;
}

int get_sysfs(struct mountgroup *mg, char *field, char *buf, int len)
{
	char fname[512], *p;
	int fd, rv;

	snprintf(fname, 512, "%s/%s/%s", LOCK_DLM_SYSFS_DIR, mg->name, field);

	fd = open(fname, O_RDONLY);
	if (fd < 0) {
		log_error("open error %s %d %d", fname, fd, errno);
		return -1;
	}

	rv = read(fd, buf, len);
	if (rv < 0)
		log_error("read error %s %d %d", fname, rv, errno);
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

/* send nodeid/jid pairs for every member to nodeid */

void send_journals(struct mountgroup *mg, int nodeid)
{
	struct mg_member *memb;
	int i, len = MAXNAME + 1 + (mg->memb_count * 2 * sizeof(int));
	char *buf;
	int *ids;

	buf = malloc(len);
	if (!buf)
		return;
	memset(buf, 0, len);

	strncpy(buf, mg->name, MAXNAME);
	ids = (int *) (buf + MAXNAME + 1);

	/* FIXME: do byte swapping */

	i = 0;
	list_for_each_entry(memb, &mg->members, list) {
		ids[i] = memb->nodeid;
		i++;
		ids[i] = memb->jid;
		i++;
	}

	log_group(mg, "send_journals len %d to %d", len, nodeid);

	send_journals_message(nodeid, buf, len);

	free(buf);
}

void receive_journals(char *buf, int len, int from)
{
	struct mg_member *memb, *memb2;
	struct mountgroup *mg;
	int *ids, count, i, nodeid, jid;

	count = (len - MAXNAME - 1) / (2 * sizeof(int));

	mg = find_mg(buf);
	if (!mg) {
		log_error("receive_journals from %d no mountgroup %s",
			  from, buf);
		return;
	}

	log_group(mg, "receive_journals from %d count %d", from, count);

	if (count != mg->memb_count) {
		log_error("invalid journals message len %d counts %d %d",
			  len, count, mg->memb_count);
		return;
	}

	ids = (int *) (buf + MAXNAME + 1);

	/* FIXME: byte swap nodeid/jid */

	for (i = 0; i < count; i++) {
		nodeid = ids[i * 2];
		jid = ids[i * 2 + 1];

		log_debug("receive nodeid %d jid %d", nodeid, jid);

		memb = find_memb_nodeid(mg, nodeid);
		memb2 = find_memb_jid(mg, jid);

		if (!memb || memb2) {
			log_error("invalid journals message nodeid %d jid %d",
				  nodeid, jid);
			return;
		}

		memb->jid = jid;

		if (nodeid == our_nodeid)
			mg->our_jid = jid;
	}

	set_sysfs(mg, "jid", mg->our_jid);
	group_done(gh, mg->name, mg->start_event_nr);
}

/* We set the new member's jid to the lowest unused jid.
   If we're the lowest existing member (by nodeid), then
   send jid info to the new node. */

int discover_journals(struct mountgroup *mg)
{
	struct mg_member *memb, *new_memb = NULL;
	int i;

	list_for_each_entry(memb, &mg->members, list) {
		if (memb->spectator)
			continue;
		if (memb->jid == -1) {
			if (new_memb) {
				log_error("more than one new member %d %d",
					  new_memb->nodeid, memb->nodeid);
				return -1;
			}
			new_memb = memb;

			/* don't break so we can check that there is only
			   one new member */
		}
	}

	for (i = 0; i < 1024; i++) {
		memb = find_memb_jid(mg, i);
		if (!memb) {
			log_group(mg, "new member %d got jid %d",
				  new_memb->nodeid, i);
			new_memb->jid = i;
			break;
		}
	}

	if (mg->low_finished_nodeid == our_nodeid)
		send_journals(mg, new_memb->nodeid);
	return 0;
}

int recover_journals(struct mountgroup *mg)
{
	struct mg_member *memb;
	int rv, found = 0;

	list_for_each_entry(memb, &mg->members_gone, list) {
		if (memb->recover_journal) {
			rv = set_sysfs(mg, "recover", memb->jid);
			if (rv < 0)
				break;
			memb->recover_journal = 0;
			memb->wait_recover_done = 1;
			found = 1;
			break;
		}
	}
	return found;
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
	char buf[MAXLINE];
	int rv;

	memb = malloc(sizeof(struct mg_member));
	if (!memb)
		return -ENOMEM;

	memset(memb, 0, sizeof(*memb));
	memset(buf, 0, sizeof(buf));

	rv = group_join_info(GFS_GROUP_LEVEL, mg->name, nodeid, buf);
	if (!rv && strstr(buf, "spectator"))
		memb->spectator = 1;

	memb->nodeid = nodeid;
	memb->jid = -1;
	add_ordered_member(mg, memb);
	mg->memb_count++;
	return 0;
}

void remove_member(struct mountgroup *mg, struct mg_member *memb)
{
	list_move(&memb->list, &mg->members_gone);
	memb->gone_event = mg->start_event_nr;
	mg->memb_count--;
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
			remove_member(mg, memb);

			/* - spectators don't do journal callbacks
			   - journal cb for failed or withdrawing nodes
			   - journal cb only if failed node finished joining
			   - no journal cb if failed node was spectator
			   - no journal cb if we've already done a journl cb */

			if (!mg->spectator &&
			    (mg->start_type == GROUP_NODE_FAILED ||
			     memb->withdraw) &&
			    memb->mount_finished &&
			    !memb->spectator &&
			    !memb->wait_recover_done)
				memb->recover_journal = 1;

			log_group(mg, "remove member %d recover_journal %d",
				  memb->nodeid, memb->recover_journal);
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
		if (memb->spectator || !memb->mount_finished)
			continue;
		if (low == -1 || memb->nodeid < low)
			low = memb->nodeid;
	}
	mg->low_finished_nodeid = low;

	*pos_out = pos;
	*neg_out = neg;

	log_group(mg, "total members %d", mg->memb_count);
}

struct mountgroup *create_mg(char *name)
{
	struct mountgroup *mg;

	mg = malloc(sizeof(*mg));
	memset(mg, 0, sizeof(*mg));

	INIT_LIST_HEAD(&mg->members);
	INIT_LIST_HEAD(&mg->members_gone);
	mg->first_start = 1;

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

int do_mount(char *name)
{
	struct mountgroup *mg;
	char buf[MAXLINE], *info = NULL;
	group_data_t data;
	int rv;

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

	memset(buf, 0, sizeof(buf));

	rv = get_sysfs(mg, "cluster", buf, sizeof(buf));
	if (rv < 0)
		goto fail;

	if (strlen(buf) != strlen(clustername) ||
	    strlen(buf) == 0 || strcmp(buf, clustername)) {
		rv = -1;
		log_error("do_mount: different cluster names: fs=%s cman=%s",
			  buf, clustername);
		goto fail;
	} else
		log_group(mg, "cluster name matches: %s", clustername);

	memset(buf, 0, sizeof(buf));

	rv = get_sysfs(mg, "options", buf, sizeof(buf));

	if (strstr(buf, "spectator")) {
		log_group(mg, "spectator mount");
		mg->spectator = 1;
		info = "spectator";
	} else {
		/* check that we're in fence domain */
		memset(&data, 0, sizeof(data));
		rv = group_get_group(0, "default", &data);
		if (rv || strcmp(data.client_name, "fence") || !data.member) {
			log_error("do_mount: not in default fence domain");
			goto fail;
		}
	}

	list_add(&mg->list, &mounts);

	group_join(gh, name, info);
	return 0;

 fail:
	/* terminate the mount */
	set_sysfs(mg, "mounted", -1);

	log_error("do_mount: %d", rv);
	return rv;
}

int do_recovery_done(char *name)
{
	struct mountgroup *mg;
	struct mg_member *memb;
	char buf[MAXLINE];
	int rv, jid_done, wait, found = 0;

	mg = find_mg(name);
	if (!mg) {
		log_error("recovery_done: unknown mount group %s", name);
		return -1;
	}

	memset(buf, 0, sizeof(buf));

	rv = get_sysfs(mg, "recover_done", buf, sizeof(buf));
	if (rv < 0)
		return rv;
	jid_done = atoi(buf);

	list_for_each_entry(memb, &mg->members, list) {
		if (memb->jid == jid_done) {
			if (memb->wait_recover_done) {
				memb->wait_recover_done = 0;
				found = 1;
			}
			break;
		}
	}

	if (!found)
		log_group(mg, "jid_recovery_done %d: not waiting", jid_done);

	wait = recover_journals(mg);
	if (!wait)
		group_done(gh, name, mg->start_event_nr);

	return 0;
}

int do_unmount(char *name)
{
	struct mountgroup *mg;

	mg = find_mg(name);
	if (!mg) {
		log_error("do_unmount: unknown mount group %s", name);
		return -1;
	}

	release_withdraw_locks(mg);

	group_leave(gh, name, NULL);

	return 0;
}

int do_stop(struct mountgroup *mg)
{
	return set_sysfs(mg, "block", 1);
}

int do_finish(struct mountgroup *mg)
{
	struct mg_member *memb, *safe;
	int leave_blocked = 0;

	list_for_each_entry_safe(memb, safe, &mg->members_gone, list) {
		if (memb->gone_event <= mg->last_finish) {
			list_del(&memb->list);
			if (!memb->withdraw)
				release_withdraw_lock(mg, memb);
			free(memb);
		} else {
			/* not sure if/when this would happen... */
			log_group(mg, "finish member not cleared %d %d %d",
				  memb->nodeid, memb->gone_event,
				  mg->last_finish);
		}
	}

	list_for_each_entry(memb, &mg->members, list) {
		memb->mount_finished = 1;

		/* If there are still withdrawing nodes that haven't left
		   the group, we need to keep lock requests blocked */

		if (memb->withdraw) {
			log_group(mg, "finish: leave locks blocked for %d",
				  memb->nodeid);
			leave_blocked = 1;
		}
	}

	if (!leave_blocked)
		set_sysfs(mg, "block", 0);

	/* only needed if joining */
	set_sysfs(mg, "mounted", 1);

	return 0;
}

/* first mounter is the first non-spectator to join the group */

int first_participant(struct mountgroup *mg, int member_count)
{
	struct mg_member *memb;

	if (member_count == 1)
		return 1;

	list_for_each_entry(memb, &mg->members, list) {
		if (memb->nodeid == our_nodeid)
			continue;
		if (memb->spectator == 0)
			return 0;
	}

	log_group(mg, "first participant of %d members", member_count);
	return 1;
}

int do_start(struct mountgroup *mg, int type, int member_count, int *nodeids)
{
	struct mg_member *memb;
	int wait = 0, pos = 0, neg = 0;

	/* Reset things when the last stop aborted our first start,
	   i.e. there was no finish; we got a start/stop/start immediately
	   upon joining.  There should be no reseting necessary when we're
	   already a member and get a start/stop/start sequence. */

	if (!mg->last_finish && mg->last_stop) {
		log_debug("revert aborted first start");
		mg->first_start = 1;
		mg->last_stop = 0;
		mg->our_jid = -1;
		release_withdraw_locks(mg);
		clear_members(mg);
	}

	mg->start_event_nr = mg->last_start;
	mg->start_type = type;

	recover_members(mg, member_count, nodeids, &pos, &neg);

	hold_withdraw_locks(mg);

	if (mg->spectator) {
		/* If we're the first mounter, we set ls_first so gfs
		   will bail out if there are dirty journals.  We set
		   ls_first again later for the first participant. */
		if (member_count == 1)
			set_sysfs(mg, "first", 1);
		group_done(gh, mg->name, mg->last_start);
		goto out;
	}
		
	/* NB "first_start" doesn't mean the first group member,
	   it's set the first time we run do_start() and means we're
	   the new node being added to the mount group. */

	if (mg->first_start) {
		if (!pos || neg)
			log_error("invalid member change %d %d", pos, neg);

		if (first_participant(mg, member_count)) {
			memb = find_memb_nodeid(mg, our_nodeid);
			memb->jid = 0;
			mg->our_jid = 0;
			set_sysfs(mg, "jid", mg->our_jid);
			set_sysfs(mg, "first", 1);
			group_done(gh, mg->name, mg->last_start);
		}
		
		/* else we wait for a message from an existing member
		   telling us what jid to use and what jids others
		   are using; when we get that our mount can complete */

	} else {
		if (pos)
			discover_journals(mg);

		if (neg)
			wait = recover_journals(mg);

		if (!wait)
			group_done(gh, mg->name, mg->last_start);
	}
 out:
	mg->first_start = 0;
	return 0;
}

int do_terminate(struct mountgroup *mg)
{
	return set_sysfs(mg, "mounted", -1);
}

