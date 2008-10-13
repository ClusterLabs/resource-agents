
/* Apply queued events to apps */

#include "gd_internal.h"

struct list_head recovery_sets;

struct nodeid {
	struct list_head list;
	int nodeid;
};

char *msg_type(int type)
{
	switch (type) {
	case MSG_APP_STOPPED:
		return "stopped";
	case MSG_APP_STARTED:
		return "started";
	case MSG_APP_RECOVER:
		return "recover";
	case MSG_APP_INTERNAL:
		return "internal";
	case MSG_GLOBAL_ID:
		return "global_id";
	}
	return "unknown";
}

void msg_bswap_out(msg_t *msg)
{
	msg->ms_version[0]	= cpu_to_le32(MSG_VER_MAJOR);
	msg->ms_version[1]	= cpu_to_le32(MSG_VER_MINOR);
	msg->ms_version[2]	= cpu_to_le32(MSG_VER_PATCH);
	msg->ms_type		= cpu_to_le16(msg->ms_type);
	msg->ms_level		= cpu_to_le16(msg->ms_level);
	msg->ms_length		= cpu_to_le32(msg->ms_length);
	msg->ms_global_id	= cpu_to_le32(msg->ms_global_id);
	msg->ms_event_id	= cpu_to_le64(msg->ms_event_id);
}

void msg_bswap_in(msg_t *msg)
{
	msg->ms_version[0]	= le32_to_cpu(MSG_VER_MAJOR);
	msg->ms_version[1]	= le32_to_cpu(MSG_VER_MINOR);
	msg->ms_version[2]	= le32_to_cpu(MSG_VER_PATCH);
	msg->ms_type		= le16_to_cpu(msg->ms_type);
	msg->ms_level		= le16_to_cpu(msg->ms_level);
	msg->ms_length		= le32_to_cpu(msg->ms_length);
	msg->ms_global_id	= le32_to_cpu(msg->ms_global_id);
	msg->ms_event_id	= le64_to_cpu(msg->ms_event_id);
}

uint64_t make_event_id(group_t *g, int state, int nodeid)
{
	uint64_t id;
	uint32_t n = nodeid;
	uint32_t memb_count = g->memb_count;
	uint16_t type = 0;

	if (state == EST_JOIN_BEGIN)
		type = 1;
	else if (state == EST_LEAVE_BEGIN)
		type = 2;
	else if (state == EST_FAIL_BEGIN)
		type = 3;
	else
		log_error(g, "make_event_id invalid state %d", state);

	id = n;
	id = id << 32;
	memb_count = memb_count << 16;
	id = id | memb_count;
	id = id | type;

	log_group(g, "make_event_id %llx nodeid %d memb_count %d type %u",
		  (unsigned long long)id, nodeid, g->memb_count, type);

	return id;
}

void free_event(event_t *ev)
{
	struct nodeid *id, *id_safe;

	list_for_each_entry_safe(id, id_safe, &ev->extended, list) {
		list_del(&id->list);
		free(id);
	}
	free(ev);
}

int event_id_to_nodeid(uint64_t id)
{
	int nodeid;
	uint64_t n = id >> 32;
	nodeid = n & 0xFFFFFFFF;
	return nodeid;
}

int event_id_to_type(uint64_t id)
{
	uint64_t n;
	n = id & 0x000000000000FFFF;
	return ((int) n);
}

/*
 * Free queued message if:
 * - the id indicates a join for node X and X is a member
 * - the id indicates a leave for node X and X is not a member
 *
 * Note sure if all these cases are relevant, currently we only
 * purge messages after we join.
 */

static void purge_messages(group_t *g)
{
	struct save_msg *save, *tmp;
	node_t *node;
	int nodeid, type;
	char *state_str;

	list_for_each_entry_safe(save, tmp, &g->messages, list) {
		if (save->msg.ms_type == MSG_APP_INTERNAL)
			continue;

		nodeid = event_id_to_nodeid(save->msg.ms_event_id);
		type = event_id_to_type(save->msg.ms_event_id);
		node = find_app_node(g->app, nodeid);

		if ((type == 1 && node) || (type != 1 && !node) ||
		    (save->msg.ms_type == MSG_APP_RECOVER)) {

			if (save->msg.ms_type == MSG_APP_RECOVER)
				state_str = "MSG_APP_RECOVER";
			else if (type == 1)
				state_str = "EST_JOIN_BEGIN";
			else if (type == 2)
				state_str = "EST_LEAVE_BEGIN";
			else if (type == 3)
				state_str = "EST_FAIL_BEGIN";
			else
				state_str = "error";

			log_group(g, "purge msg %llx from %d %s",
				  (unsigned long long)save->msg.ms_event_id,
				  nodeid, state_str);

			list_del(&save->list);
			if (save->msg_long)
				free(save->msg_long);
			free(save);
		}
	}
}

/* For a recovery event where multiple nodes have failed, the event id
   is based on the lowest nodeid of all the failed nodes.  The event
   id is also based on the number of remaining group members which
   changes as failed nodes are added to the recovery event. */

void extend_recover_event(group_t *g, event_t *ev, int nodeid)
{
	struct nodeid *id;
	int new_id_nodeid = nodeid;

	log_group(g, "extend_recover_event for %d with node %d",
		  ev->nodeid, nodeid);

	/* the lowest nodeid in a recovery event is kept in ev->nodeid,
	   the other nodeid's are kept in the extended list */

	if (nodeid < ev->nodeid) {
		new_id_nodeid = ev->nodeid;
		ev->nodeid = nodeid;
		ev->id = make_event_id(g, EST_FAIL_BEGIN, nodeid);
	}

	id = malloc(sizeof(struct nodeid));
	// FIXME: handle failed malloc
	id->nodeid = new_id_nodeid;
	list_add(&id->list, &ev->extended);
}

struct recovery_set *get_recovery_set(int nodeid)
{
	struct recovery_set *rs;

	list_for_each_entry(rs, &recovery_sets, list) {
		if (rs->nodeid == nodeid)
			return rs;
	}

	rs = malloc(sizeof(*rs));
	ASSERT(rs);
	memset(rs, 0, sizeof(struct recovery_set));
	rs->nodeid = nodeid;
	rs->cman_update = 0;
	rs->cpg_update = 0;
	INIT_LIST_HEAD(&rs->entries);

	list_add_tail(&rs->list, &recovery_sets);

	return rs;
}

/* When a node fails, all the groups that that node was in are tracked by
   one of these recovery_sets.  These are the groups that will need layered
   recovery, i.e. all effected groups must be completely stopped (the app
   stopped on all nodes) before any are restarted.  When restarted, the groups
   must be restarted in order from lowest level first.  All groups on each
   level must complete recovery (on all nodes) before recovery can begin for
   the next level. */

/* FIXME: do we need to worry about the case where we get an
   add_recovery_set_cman() that finds an old rs, the old rs completes
   and goes away, and then we get the add_recovery_set_cpg() matching
   the _cman() variant that we ignored? */

void add_recovery_set_cman(int nodeid)
{
	struct recovery_set *rs;

	log_debug("add_recovery_set_cman nodeid %d", nodeid);

	rs = get_recovery_set(nodeid);
	if (rs->cman_update) {
		log_debug("old recovery for %d still in progress", nodeid);
		return;
	}
	rs->cman_update = 1;

	if (!rs->cpg_update && !in_groupd_cpg(nodeid)) {
		log_debug("free recovery set %d not running groupd", nodeid);
		list_del(&rs->list);
		free(rs);
		return;
	}

	if (list_empty(&rs->entries) && rs->cpg_update) {
		log_debug("free unused recovery set %d cman", nodeid);
		list_del(&rs->list);
		free(rs);
	}
}

/* procdown of 1 means the groupd daemon process exited, but the node didn't
   fail.  when only the process fails, we won't get a cman callback which is
   only for nodedown.  if the node wasn't in any groups we don't add a recovery
   set and don't care about the exited groupd; if the node with the failed
   groupd _was_ in any groups, we add a rs and process_groupd_confchg() will do
   cman_kill_node() to make the node really fail (and we'll get an
   add_recovery_set_cman()). */

struct recovery_set *add_recovery_set_cpg(int nodeid, int procdown)
{
	struct recovery_set *rs;
	struct recovery_entry *re;
	group_t *g;
	node_t *node;

	log_debug("add_recovery_set_cpg nodeid %d procdown %d",
		  nodeid, procdown);

	rs = get_recovery_set(nodeid);
	if (rs->cpg_update) {
		log_debug("old recovery for %d still in progress", nodeid);
		return rs;
	}
	rs->cpg_update = 1;

	list_for_each_entry(g, &gd_groups, list) {
		list_for_each_entry(node, &g->app->nodes, list) {
			if (node->nodeid == nodeid) {
				log_group(g, "add to recovery set %d", nodeid);
				re = malloc(sizeof(*re));
				// FIXME: handle failed malloc
				memset(re, 0, sizeof(struct recovery_entry));
				re->group = g;
				list_add_tail(&re->list, &rs->entries);
				break;
			}
		}
	}

	if (list_empty(&rs->entries)) {
		if (rs->cman_update || procdown) {
			log_debug("free unused recovery set %d cpg", nodeid);
			list_del(&rs->list);
			free(rs);
			return NULL;
		}
	}

	return rs;
}

void _del_recovery_set(group_t *g, int nodeid, int purge)
{
	struct recovery_set *rs, *rs2;
	struct recovery_entry *re, *re2;
	int found = 0, entries_not_recovered;

	list_for_each_entry_safe(rs, rs2, &recovery_sets, list) {
		if (rs->nodeid != nodeid)
			continue;

		entries_not_recovered = 0;

		list_for_each_entry_safe(re, re2, &rs->entries, list) {
			if (re->group == g) {
				if (purge) {
					list_del(&re->list);
					free(re);
					log_group(g, "purged from rs %d",
						  rs->nodeid);
				} else {
					re->recovered = 1;
					log_group(g, "done in recovery set %d",
					  	  rs->nodeid);
					found++;
				}
			} else {
				if (re->recovered == 0)
					entries_not_recovered++;
			}
		}

		if (entries_not_recovered) {
			log_debug("recovery set %d has %d entries not done",
				  rs->nodeid, entries_not_recovered);
			continue;
		}

		/* all entries in this rs are recovered, free it */
		log_debug("recovery set %d is all done", rs->nodeid);

		list_for_each_entry_safe(re, re2, &rs->entries, list) {
			list_del(&re->list);
			free(re);
		}
		list_del(&rs->list);
		free(rs);
	}

	if (!found)
		log_group(g, "not found in any recovery sets for %d", nodeid);
}

/* A group has finished recovery for given event (which can encompass more than
   one failed nodeid).  Remove this group from recovery sets for those nodeids
   and free any recovery sets that are now completed. */

void del_recovery_set(group_t *g, event_t *ev, int purge)
{
	struct nodeid *id;

	log_group(g, "rev %llx done, remove group from rs %d",
		  (unsigned long long)ev->id, ev->nodeid);
	_del_recovery_set(g, ev->nodeid, purge);

	list_for_each_entry(id, &ev->extended, list) {
		log_group(g, "rev %llx done, remove group from rs %d",
			  (unsigned long long)ev->id, id->nodeid);
		_del_recovery_set(g, id->nodeid, purge);
	}
}

/* go through all recovery sets and check that all failed nodes have
   been removed by cman callbacks; if they haven't then cman may be
   inquorate and we just haven't gotten the cman callback yet that
   will set cman_quorate = 0  [group recoveries are driven by cpg
   callbacks, not cman callbacks, so that's why we might be trying
   to do recovery without having heard from cman yet.] */

int cman_quorum_updated(void)
{
	struct recovery_set *rs;

	list_for_each_entry(rs, &recovery_sets, list) {
		if (rs->cman_update)
			continue;
		log_debug("no cman update for recovery_set %d quorate %d",
			  rs->nodeid, cman_quorate);
		return 0;
	}
	return 1;
}

int is_recovery_event(event_t *ev)
{
	if (event_id_to_type(ev->id) == 3)
		return 1;
	return 0;
}

/* all groups referenced by a recovery set are stopped on all nodes,
   and stopped for recovery */

static int set_is_all_stopped(struct recovery_set *rs, event_t *rev)
{
	struct recovery_entry *re;
	event_t *ev;

	list_for_each_entry(re, &rs->entries, list) {
		ev = re->group->app->current_event;

		/* we need to use ev->fail_all_stopped instead of checking
		   ev->state == FAIL_ALL_STOPPED because if two groups are at
		   the low level, one will detect all_levels_all_stopped first
		   and then immediately move on to starting before the other,
		   also checking all_levels_all_stopped, can see it's in
		   FAIL_ALL_STOPPED */

		if (ev && is_recovery_event(ev) && ev->fail_all_stopped)
			continue;
		else
			return 0;
	}
	return 1;
}

/* for every recovery set that this group is in, are all other groups in
   each of those sets in the "all stopped" state for recovery? */

static int all_levels_all_stopped(group_t *g, event_t *rev)
{
	struct recovery_set *rs;
	struct recovery_entry *re;
	int found = 0;

	list_for_each_entry(rs, &recovery_sets, list) {
		list_for_each_entry(re, &rs->entries, list) {
			if (re->group == g) {
				found = 1;
				if (set_is_all_stopped(rs, rev))
					break;
				else
					return 0;
			}
		}
	}

	if (!found)
		return 0;
	return 1;
}

void dump_recovery_sets(void)
{
	struct recovery_set *rs;
	struct recovery_entry *re;

	list_for_each_entry(rs, &recovery_sets, list) {
		log_debug("recovery_set %d", rs->nodeid);
		list_for_each_entry(re, &rs->entries, list) {
			log_debug("  recovery_entry %d:%s recovered %d",
				  re->group->level, re->group->name,
				  re->recovered);
		}
	}
}

static int group_in_recovery_set(struct recovery_set *rs, group_t *g)
{
	struct recovery_entry *re;

	list_for_each_entry(rs, &recovery_sets, list) {
		list_for_each_entry(re, &rs->entries, list) {
			if (re->group == g)
				return 1;
		}
	}
	return 0;
}

static int rs_lower_levels_recovered(struct recovery_set *rs, int level)
{
	struct recovery_entry *re;

	list_for_each_entry(re, &rs->entries, list) {
		if (re->group->level < level && !re->recovered)
			return 0;
	}
	return 1;
}

/* lower level groups should be recovered in each rs this group is in */

static int lower_levels_recovered(group_t *g)
{
	struct recovery_set *rs;

	list_for_each_entry(rs, &recovery_sets, list) {
		if (!group_in_recovery_set(rs, g))
			continue;

		if (rs_lower_levels_recovered(rs, g->level))
			continue;

		log_group(g, "lower levels not recovered in rs %d", rs->nodeid);
		return 0;
	}
	return 1;
}

/* We're interested in any unrecovered group at a lower level than g, not
   just lower groups in the same recovery set. */

static int lower_groups_need_recovery(group_t *g)
{
	struct recovery_set *rs;

	list_for_each_entry(rs, &recovery_sets, list) {
		if (rs_lower_levels_recovered(rs, g->level))
			continue;
		log_group(g, "lower group not recovered in rs %d", rs->nodeid);
		return 1;
	}
	return 0;
}

static int level_is_lowest(struct recovery_set *rs, int level)
{
	struct recovery_entry *re;

	list_for_each_entry(re, &rs->entries, list) {
		if (re->group->level < level)
			return 0;
	}
	return 1;
}

/* this group is at the lowest level in any recovery sets it's in */

static int lowest_level(group_t *g)
{
	struct recovery_set *rs;

	list_for_each_entry(rs, &recovery_sets, list) {
		if (!group_in_recovery_set(rs, g))
			continue;
		if (level_is_lowest(rs, g->level))
			continue;
		return 0;
	}
	return 1;
}

static event_t *create_event(group_t *g)
{
	event_t *ev;

	ev = malloc(sizeof(event_t));
	ASSERT(ev);

	memset(ev, 0, sizeof(event_t));

	INIT_LIST_HEAD(&ev->memb);
	INIT_LIST_HEAD(&ev->extended);
	ev->event_nr = ++gd_event_nr;

	return ev;
}

int queue_app_recover(group_t *g, int nodeid)
{
	event_t *ev;

	ev = create_event(g);
	ev->nodeid = nodeid;
	ev->state = EST_FAIL_BEGIN;
	ev->fail_all_stopped = 0;
	ev->id = make_event_id(g, EST_FAIL_BEGIN, nodeid);

	log_group(g, "queue recover event for nodeid %d", nodeid);

	list_add_tail(&ev->list, &g->app->events);
	return 0;
}

void del_event_nodes(event_t *ev)
{
	node_t *node, *n;

	list_for_each_entry_safe(node, n, &ev->memb, list) {
		list_del(&node->list);
		free(node);
	}
}

static void add_event_nodes(group_t *g, event_t *ev)
{
	node_t *node, *n;

	list_for_each_entry(node, &g->memb, list) {
		n = new_node(node->nodeid);
		list_add(&n->list, &ev->memb);
	}
}

event_t *search_event(group_t *g, int nodeid)
{
	event_t *ev;

	list_for_each_entry(ev, &g->app->events, list) {
		if (ev->nodeid == nodeid)
			return ev;
	}
	return NULL;
}

void dump_queued_events(group_t *g)
{
	event_t *ev;

	list_for_each_entry(ev, &g->app->events, list) {
		log_group(g, "    queued ev %d %llx %s",
			  ev->nodeid, (unsigned long long)ev->id,
			  ev_state_str(ev));
	}
}

int queue_app_join(group_t *g, int nodeid)
{
	event_t *ev;

	/* sanity check */
	ev = g->app->current_event;
	if (ev && ev->nodeid == nodeid) {
		log_group(g, "queue_app_join: current event %d %llx %s",
			  nodeid, (unsigned long long)ev->id, ev_state_str(ev));
	}

	/* sanity check */
	ev = search_event(g, nodeid);
	if (ev) {
		log_group(g, "queue_app_join: queued event %d %llx %s",
			  nodeid, (unsigned long long)ev->id, ev_state_str(ev));
	}

	ev = create_event(g);
	ev->nodeid = nodeid;
	ev->state = EST_JOIN_BEGIN;
	ev->id = make_event_id(g, EST_JOIN_BEGIN, nodeid);

	log_group(g, "queue join event for nodeid %d", nodeid);
	dump_queued_events(g);

	if (nodeid == our_nodeid)
		add_event_nodes(g, ev);

	list_add_tail(&ev->list, &g->app->events);
	return 0;
}

int queue_app_leave(group_t *g, int nodeid)
{
	event_t *ev;

	/* sanity check */
	ev = g->app->current_event;
	if (ev && ev->nodeid == nodeid) {
		log_group(g, "queue_app_leave: current event %d %llx %s",
			  nodeid, (unsigned long long)ev->id, ev_state_str(ev));
	}

	/* sanity check */
	ev = search_event(g, nodeid);
	if (ev) {
		log_group(g, "queue_app_leave: queued event %d %llx %s",
			  nodeid, (unsigned long long)ev->id, ev_state_str(ev));
	}

	ev = create_event(g);
	ev->nodeid = nodeid;
	ev->state = EST_LEAVE_BEGIN;
	ev->id = make_event_id(g, EST_LEAVE_BEGIN, nodeid);

	log_group(g, "queue leave event for nodeid %d", nodeid);
	dump_queued_events(g);

	list_add_tail(&ev->list, &g->app->events);
	return 0;
}

int queue_app_message(group_t *g, struct save_msg *save)
{
	/* log_group(g, "queue message %s from %d",
	             msg_type(save->msg.ms_type), save->nodeid); */
	list_add_tail(&save->list, &g->messages);
	return 0;
}

/* This is called when we get the nodedown for the per-group cpg; we know
   that after the cpg nodedown we won't get any further messages. bz 436984
   It's conceivable but unlikely that the nodedown processing (initiated by
   the groupd cpg nodedown) could begin before the per-group cpg nodedown
   is received where this purging occurs.  If it does, then we may need to
   add code to wait for the nodedown to happen in both the groupd cpg and the
   per-group cpg before processing the nodedown. */

void purge_node_messages(group_t *g, int nodeid)
{
	struct save_msg *save, *tmp;

	list_for_each_entry_safe(save, tmp, &g->messages, list) {
		if (save->nodeid != nodeid)
			continue;

		log_group(g, "purge msg from dead node %d", nodeid);

		list_del(&save->list);
		if (save->msg_long)
			free(save->msg_long);
		free(save);
	}
}

static void del_app_nodes(app_t *a)
{
	node_t *node, *tmp;

	list_for_each_entry_safe(node, tmp, &a->nodes, list) {
		list_del(&node->list);
		free(node);
	}
}

node_t *find_app_node(app_t *a, int nodeid)
{
	node_t *node;

	list_for_each_entry(node, &a->nodes, list) {
		if (node->nodeid == nodeid)
			return node;
	}
	return NULL;
}

int is_our_join(event_t *ev)
{
	return (ev->nodeid == our_nodeid);
}

static int is_our_leave(event_t *ev)
{
	return (ev->nodeid == our_nodeid);
}

/* Called after all nodes have acked that they're stopped for our
   leave.  We get their stopped messages even though we've left the
   cpg because the messages are sent through the groupd cpg.
   groupd_down() will fill in stops for us for nodes that fail before
   sending stopped for our leave. */

void finalize_our_leave(group_t *g)
{
	struct recovery_set *rs;
	struct recovery_entry *re, *re2;
	app_t *a = g->app;

	log_group(g, "finalize_our_leave");

	app_terminate(a);
	cpg_finalize(g->cpg_handle);
	client_dead(g->cpg_client);
	g->app = NULL;
	del_app_nodes(a);
	free(a);

	/* this group shouldn't be in any recovery sets... sanity check
	   and avoid future segfault by removing re's referencing this g */

	list_for_each_entry(rs, &recovery_sets, list) {
		list_for_each_entry_safe(re, re2, &rs->entries, list) {
			if (re->group == g) {
				log_error(g, "finalize: still in recovery "
					  "set %d", rs->nodeid);
				list_del(&re->list);
				free(re);
			}
		}
	}

	remove_group(g);
}

static int send_stopped(group_t *g)
{
	msg_t msg;
	event_t *ev = g->app->current_event;

	memset(&msg, 0, sizeof(msg));
	msg.ms_type = MSG_APP_STOPPED;
	msg.ms_global_id = g->global_id;
	msg.ms_event_id = ev->id;
	msg.ms_level = g->level;
	memcpy(&msg.ms_name, &g->name, MAX_NAMELEN);

	msg_bswap_out(&msg);

	log_group(g, "send stopped");
	return send_message_groupd(g, &msg, sizeof(msg), MSG_APP_STOPPED);
}

static int send_started(group_t *g)
{
	msg_t msg;
	event_t *ev = g->app->current_event;

	memset(&msg, 0, sizeof(msg));
	msg.ms_type = MSG_APP_STARTED;
	msg.ms_global_id = g->global_id;
	msg.ms_event_id = ev->id;
	msg.ms_level = g->level;
	memcpy(&msg.ms_name, &g->name, MAX_NAMELEN);

	msg_bswap_out(&msg);

	log_group(g, "send started");
	return send_message_groupd(g, &msg, sizeof(msg), MSG_APP_STARTED);
}

static int send_recover(group_t *g, event_t *rev)
{
	msg_t msg;

	memset(&msg, 0, sizeof(msg));
	msg.ms_type = MSG_APP_RECOVER;
	msg.ms_global_id = g->global_id;
	msg.ms_event_id = rev->id;
	msg.ms_level = g->level;
	memcpy(&msg.ms_name, &g->name, MAX_NAMELEN);

	msg_bswap_out(&msg);

	log_group(g, "send recover");
	return send_message_groupd(g, &msg, sizeof(msg), MSG_APP_RECOVER);
}

int do_stopdone(char *name, int level)
{
	group_t *g;
	g = find_group_level(name, level);
	return send_stopped(g);
}

int do_startdone(char *name, int level, int event_nr)
{
	group_t *g;
	event_t *ev;
	char *state;

	g = find_group_level(name, level);
	if (!g) {
		log_print("do_startdone: no group level %d name %s",
			  level, name);
		return -1;
	}

	ev = g->app->current_event;

	state = ev ? ev_state_str(ev) : "no-event";

	if (!ev || ev->event_nr != event_nr) {
		log_group(g, "ignore startdone %d state %s", event_nr, state);
		return 0;
	}

	if (!event_state_starting(g->app)) {
		log_error(g, "IGNORE startdone %d state %s", event_nr, state);
		return 0;
	}

	return send_started(g);
}

char *ev_state_str(event_t *ev)
{
	switch (ev->state) {
	case EST_JOIN_BEGIN:
		return "JOIN_BEGIN";
	case EST_JOIN_STOP_WAIT:
		return "JOIN_STOP_WAIT";
	case EST_JOIN_ALL_STOPPED:
		return "JOIN_ALL_STOPPED";
	case EST_JOIN_START_WAIT:
		return "JOIN_START_WAIT";
	case EST_JOIN_ALL_STARTED:
		return "JOIN_ALL_STARTED";
	case EST_LEAVE_BEGIN:
		return "LEAVE_BEGIN";
	case EST_LEAVE_STOP_WAIT:
		return "LEAVE_STOP_WAIT";
	case EST_LEAVE_ALL_STOPPED:
		return "LEAVE_ALL_STOPPED";
	case EST_LEAVE_START_WAIT:
		return "LEAVE_START_WAIT";
	case EST_LEAVE_ALL_STARTED:
		return "LEAVE_ALL_STARTED";
	case EST_FAIL_BEGIN:
		return "FAIL_BEGIN";
	case EST_FAIL_STOP_WAIT:
		return "FAIL_STOP_WAIT";
	case EST_FAIL_ALL_STOPPED:
		return "FAIL_ALL_STOPPED";
	case EST_FAIL_START_WAIT:
		return "FAIL_START_WAIT";
	case EST_FAIL_ALL_STARTED:
		return "FAIL_ALL_STARTED";
	default:
		return "unknown";
	}
}

static int count_nodes_not_stopped(app_t *a)
{
	node_t *node;
	int i = 0;

	list_for_each_entry(node, &a->nodes, list) {
		if (!node->stopped)
			i++;
	}
	return i;
}

int event_state_begin(app_t *a)
{
	if (a->current_event->state == EST_JOIN_BEGIN ||
	    a->current_event->state == EST_LEAVE_BEGIN ||
	    a->current_event->state == EST_FAIL_BEGIN)
		return TRUE;
	return FALSE;
}

int event_state_stopping(app_t *a)
{
	if (a->current_event->state == EST_JOIN_STOP_WAIT ||
	    a->current_event->state == EST_LEAVE_STOP_WAIT ||
	    a->current_event->state == EST_FAIL_STOP_WAIT)
		return TRUE;
	return FALSE;
}

int event_state_starting(app_t *a)
{
	if (a->current_event->state == EST_JOIN_START_WAIT ||
	    a->current_event->state == EST_LEAVE_START_WAIT ||
	    a->current_event->state == EST_FAIL_START_WAIT)
		return TRUE;
	return FALSE;
}

int event_state_all_stopped(app_t *a)
{
	if (a->current_event->state == EST_JOIN_ALL_STOPPED ||
	    a->current_event->state == EST_LEAVE_ALL_STOPPED ||
	    a->current_event->state == EST_FAIL_ALL_STOPPED)
		return TRUE;
	return FALSE;
}

int event_state_all_started(app_t *a)
{
	if (a->current_event->state == EST_JOIN_ALL_STARTED ||
	    a->current_event->state == EST_LEAVE_ALL_STARTED ||
	    a->current_event->state == EST_FAIL_ALL_STARTED)
		return TRUE;
	return FALSE;
}

static int process_current_event(group_t *g)
{
	app_t *a = g->app;
	event_t *ev = a->current_event;
	node_t *node, *n;
	struct nodeid *id;
	int rv = 0, do_start = 0, count;

	if (!(event_state_stopping(a) || event_state_starting(a)))
		log_group(g, "process_current_event %llx %d %s",
			  (unsigned long long)ev->id, ev->nodeid,
			  ev_state_str(ev));

	switch (ev->state) {

	case EST_JOIN_BEGIN:
		ev->state = EST_JOIN_STOP_WAIT;

		if (is_our_join(ev)) {
			/* the initial set of members that we've joined,
			   includes us */

			list_for_each_entry_safe(node, n, &ev->memb, list) {
				list_move(&node->list, &a->nodes);
				a->node_count++;
				log_group(g, "app node init: add %d total %d",
					  node->nodeid, a->node_count);
			}

			/* we could have the joining node send out a stopped
			   message here for all to receive and count but
			   that's unnecessary, we just have everyone
			   set the joining node as stopped initially */

			node = find_app_node(a, our_nodeid);
			ASSERT(node);
			node->stopped = 1;

			/* if we're the first node to be joining, move
			   ahead to the JOIN_ALL_STOPPED state */

			if (a->node_count == 1)
				ev->state++;

			rv = 1;
		} else {
			app_stop(a);

			node = new_node(ev->nodeid);
			list_add(&node->list, &a->nodes);
			a->node_count++;
			log_group(g, "app node join: add %d total %d",
				  node->nodeid, a->node_count);
			node->stopped = 1;
		}
		break;

	case EST_JOIN_STOP_WAIT:
		count = count_nodes_not_stopped(a);
		log_group(g, "waiting for %d more stopped messages "
			  "before JOIN_ALL_STOPPED %d", count, ev->nodeid);
		break;

	case EST_JOIN_ALL_STOPPED:
		if (!cman_quorate) {
			log_group(g, "wait for quorum before starting app");
			break;
		}

		/* We want to move ahead to start here if this ev is to be
		   started before a pending rev that will abort it.  Once
		   started, the rev becomes current and stops the app
		   immediately. */

		if (lower_groups_need_recovery(g) &&
		    !ev->start_app_before_pending_rev) {
			log_group(g, "wait for lower_groups_need_recovery "
				  "before starting app");
			break;
		}
		ev->start_app_before_pending_rev = 0;

		ev->state = EST_JOIN_START_WAIT;

		if (!g->have_set_id) {
			g->have_set_id = 1;
			app_setid(a);
		}

		app_start(a);
		break;

	case EST_JOIN_ALL_STARTED:
		app_finish(a);

		if (is_our_join(ev)) {
			purge_messages(g);
			g->joining = 0;
		}
		free_event(ev);
		a->current_event = NULL;
		rv = 1;
		break;

	case EST_LEAVE_BEGIN:
		ev->state = EST_LEAVE_STOP_WAIT;
		app_stop(a);
		break;

	case EST_LEAVE_STOP_WAIT:
		count = count_nodes_not_stopped(a);
		log_group(g, "waiting for %d more stopped messages "
			  "before LEAVE_ALL_STOPPED %d", count, ev->nodeid);
		break;

	case EST_LEAVE_ALL_STOPPED:
		if (is_our_leave(ev)) {
			/* frees group structure */
			finalize_our_leave(g);
			rv = -1;
			break;
		}
		ev->state = EST_LEAVE_START_WAIT;

		node = find_app_node(a, ev->nodeid);
		list_del(&node->list);
		a->node_count--;
		log_group(g, "app node leave: del %d total %d",
			  node->nodeid, a->node_count);
		free(node);
		app_start(a);
		break;

	case EST_LEAVE_ALL_STARTED:
		app_finish(a);
		free_event(ev);
		a->current_event = NULL;
		rv = 1;
		break;

	case EST_FAIL_BEGIN:
		ev->state = EST_FAIL_STOP_WAIT;
		app_stop(a);

		/* set the failed nodes as stopped since we won't
		   be getting a "stopped" message from them.  the node
		   may already have been removed in the case where one
		   rev interrupts another */

		node = find_app_node(a, ev->nodeid);
		if (node)
			node->stopped = 1;

		/* multiple nodes failed together making an extended event */
		list_for_each_entry(id, &ev->extended, list) {
			node = find_app_node(a, id->nodeid);
			if (node)
				node->stopped = 1;
		}

		break;

	case EST_FAIL_STOP_WAIT:
		count = count_nodes_not_stopped(a);
		log_group(g, "waiting for %d more stopped messages "
			  "before FAIL_ALL_STOPPED %d", count, ev->nodeid);
		break;

	case EST_FAIL_ALL_STOPPED:
		ev->fail_all_stopped = 1;

		/* when recovering for failed nodes, we immediately stop all
		   apps the node was involved with but wait for quorum before
		   starting them again  */

		/* we make sure that cman has updated our quorum status since
		   the last node failure */

		if (!cman_quorum_updated())
			break;

		if (!cman_quorate)
			break;

		if (lowest_level(g)) {
			if (all_levels_all_stopped(g, ev)) {
				ev->state = EST_FAIL_START_WAIT;
				do_start = 1;
			} else
				log_group(g, "wait for all_levels_all_stopped");
		} else {
			if (lower_levels_recovered(g)) {
				ev->state = EST_FAIL_START_WAIT;
				do_start = 1;
			} else
				log_group(g, "wait for lower_levels_recovered");
		}

		if (!do_start)
			break;

		node = find_app_node(a, ev->nodeid);
		if (node) {
			a->node_count--;
			log_group(g, "app node fail: del node %d total %d",
			  	  node->nodeid, a->node_count);
			list_del(&node->list);
			free(node);
		} else
			log_group(g, "app node fail: %d prev removed",
				  ev->nodeid);

		list_for_each_entry(id, &ev->extended, list) {
			node = find_app_node(a, id->nodeid);
			if (node) {
				a->node_count--;
				log_group(g, "app node fail: del node %d "
					  "total %d, ext", node->nodeid,
					  a->node_count);
				list_del(&node->list);
				free(node);
			} else
				log_group(g, "app node fail: %d prev removed",
					  id->nodeid);
		}

		app_start(a);
		break;

	case EST_FAIL_ALL_STARTED:
		app_finish(a);
		del_recovery_set(g, ev, 0);
		free_event(ev);
		a->current_event = NULL;
		rv = 1;
		break;

	default:
		/*
		log_group(g, "nothing to do: %llx %d %s",
			  (unsigned long long)ev->id, ev->nodeid,
			  ev_state_str(ev));
		*/
		break;
	}

	return rv;
}

static void clear_all_nodes_stopped(app_t *a)
{
	node_t *node;
	log_group(a->g, "clear_all_nodes_stopped");
	list_for_each_entry(node, &a->nodes, list)
		node->stopped = 0;
}

static int mark_node_stopped(app_t *a, int nodeid)
{
	node_t *node;

	/* we might get a stopped message from another node who's going
	   through X_BEGIN before we get to X_BEGIN ourselves, so we need
	   to accept their message if we're in X_BEGIN, too */

	if (!event_state_stopping(a) && !event_state_begin(a)) {
		log_error(a->g, "mark_node_stopped: event not stopping/begin: "
			  "state %s from %d",
			  ev_state_str(a->current_event), nodeid);
		return -1;
	}

	node = find_app_node(a, nodeid);
	if (!node) {
		log_error(a->g, "mark_node_stopped: no nodeid %d", nodeid);
		return -1;
	}

	log_group(a->g, "mark node %d stopped", nodeid);

	node->stopped = 1;
	node->started = 0;

	return 0;
}

static int mark_node_started(app_t *a, int nodeid)
{
	node_t *node;

	if (!event_state_starting(a))
		log_group(a->g, "mark_node_started: event not starting %d "
			  "from %d", a->current_event->state, nodeid);

	node = find_app_node(a, nodeid);
	if (!node) {
		log_error(a->g, "mark_node_started: no nodeid %d", nodeid);
		return -1;
	}

	log_group(a->g, "mark node %d started", nodeid);

	node->stopped = 0;
	node->started = 1;

	return 0;
}

static int all_nodes_stopped(app_t *a)
{
	node_t *node;

	list_for_each_entry(node, &a->nodes, list) {
		if (!node->stopped) {
			/* ASSERT(node->started); */
			return FALSE;
		}
	}
	return TRUE;
}

static int all_nodes_started(app_t *a)
{
	node_t *node;

	list_for_each_entry(node, &a->nodes, list) {
		if (!node->started) {
			/* ASSERT(node->stopped); */
			return FALSE;
		}
	}
	return TRUE;
}

static int process_app_messages(group_t *g)
{
	app_t *a = g->app;
	struct save_msg *save, *tmp;
	event_t *ev;
	int rv = 0;

	list_for_each_entry_safe(save, tmp, &g->messages, list) {

		/* internal messages, sent not by groupd but by apps
		   to each other, are delivered to the apps in
		   deliver_app_messages() */

		if (save->msg.ms_type == MSG_APP_INTERNAL)
			continue;

		ev = a->current_event;

		if (save->msg.ms_type == MSG_APP_RECOVER) {
			if (ev && ev->state == EST_JOIN_STOP_WAIT &&
			    is_our_join(ev)) {
				/* keep this msg around for
				   recover_current_event() to see, it will
				   be purged later */
				if (!save->print_ignore) {
					log_group(g, "rev %llx taken on node %d",
						   (unsigned long long)save->msg.ms_event_id,
						   save->nodeid);
					save->print_ignore = 1;
				}
				continue;
			} else {
				goto free_save;
			}
		}


		if (!ev || ev->id != save->msg.ms_event_id) {
			if (!save->print_ignore) {
				log_group(g, "ignore msg from %d id %llx %s",
				  	  save->nodeid,
					  (unsigned long long)save->msg.ms_event_id,
				  	  msg_type(save->msg.ms_type));
				save->print_ignore = 1;
			}
			continue;
		}

		switch (save->msg.ms_type) {

		case MSG_APP_STOPPED:
			mark_node_stopped(a, save->nodeid);
			break;

		case MSG_APP_STARTED:
			mark_node_started(a, save->nodeid);
			break;

		default:
			log_error(g, "process_app_messages: invalid type %d "
				  "from %d", save->msg.ms_type, save->nodeid);
		}

		if (g->global_id == 0 && save->msg.ms_global_id != 0) {
			g->global_id = save->msg.ms_global_id;
			log_group(g, "set global_id %x from %d",
				  g->global_id, save->nodeid);
		}
	 free_save:
		list_del(&save->list);
		if (save->msg_long)
			free(save->msg_long);
		free(save);
		rv = 1;
	}

	/* state changes to X_ALL_STOPPED or X_ALL_STARTED */

	if (event_state_stopping(a) && all_nodes_stopped(a))
		a->current_event->state++;

	if (event_state_starting(a) && all_nodes_started(a))
		a->current_event->state++;

	return rv;
}

static void deliver_app_messages(group_t *g)
{
	app_t *a = g->app;
	struct save_msg *save, *tmp;

	list_for_each_entry_safe(save, tmp, &g->messages, list) {
		switch (save->msg.ms_type) {
		case MSG_APP_INTERNAL:
			app_deliver(a, save);
			break;
		default:
			continue;
		}

		list_del(&save->list);
		if (save->msg_long)
			free(save->msg_long);
		free(save);
	}
}

event_t *find_queued_recover_event(group_t *g)
{
	event_t *ev;

	list_for_each_entry(ev, &g->app->events, list) {
		if (ev->state == EST_FAIL_BEGIN)
			return ev;
	}
	return NULL;
}

#if 0
static int group_started(event_t *ev)
{
	switch (ev->state) {
	case EST_JOIN_BEGIN:
	case EST_JOIN_STOP_WAIT:
	case EST_JOIN_ALL_STOPPED:
	case EST_LEAVE_BEGIN:
	case EST_LEAVE_STOP_WAIT:
	case EST_LEAVE_ALL_STOPPED:
	case EST_FAIL_BEGIN:
	case EST_FAIL_STOP_WAIT:
	case EST_FAIL_ALL_STOPPED:
		return 0;
	default:
		return 1;
	};
}
#endif

void dump_group(group_t *g)
{
	app_t *a = g->app;
	node_t *node;
	struct save_msg *save;
	event_t *ev;

	printf("---\n");
	printf("name: %s\n", g->name);
	printf("level: %d\n", g->level);
	printf("global_id: %u\n", g->global_id);
	printf("cpg handle: %llx\n", (unsigned long long)g->cpg_handle);
	printf("cpg client: %d\n", g->cpg_client);
	printf("app client: %d\n", g->app->client);

	printf("memb count: %u\n", g->memb_count);
	printf("memb list: ");
	list_for_each_entry(node, &g->memb, list)
		printf("%d ", node->nodeid);
	printf("\n");

	printf("app node count: %u\n", g->app->node_count);
	printf("app node list: ");
	list_for_each_entry(node, &g->app->nodes, list)
		printf("%d ", node->nodeid);
	printf("\n");

	printf("saved messages: ");
	list_for_each_entry(save, &g->messages, list)
		printf("%d/%d ", save->nodeid, save->msg.ms_type);
	printf("\n");

	if (a->current_event)
		printf("current_event %d-%d\n", a->current_event->nodeid, a->current_event->state);

	printf("events: ");
	list_for_each_entry(ev, &a->events, list)
		printf("%d-%d ", ev->nodeid, ev->state);
	printf("\n");

	printf("---\n");
}

void dump_all_groups(void)
{
	group_t *g;
	list_for_each_entry(g, &gd_groups, list)
		dump_group(g);
}

/* handle a node failure while processing an event. returning > 0 means
   we want process_current_event() to be called for the group */

int recover_current_event(group_t *g)
{
	app_t *a = g->app;
	event_t *ev, *rev;
	node_t *node, *us;
	struct save_msg *save;
	struct nodeid *id, *safe;
	int rv = 0;

	ev = a->current_event;
	if (!ev)
		return 0;

	rev = find_queued_recover_event(g);
	if (!rev)
		return 0;

	/* if the current ev is for recovery, we merge the new rev into it;
	   if the current ev is still stopping (or all stopped), it just
	   continues as usual; if the current ev is starting, the state is
	   reset back to FAIL_BEGIN so it goes through a stopping cycle for
	   the new node failure that's been added to it */

	if (is_recovery_event(ev)) {
		log_group(g, "merge new rev %d into current rev %d %s",
			  rev->nodeid, ev->nodeid, ev_state_str(ev));

		if (ev->state > EST_FAIL_ALL_STOPPED) {
			ev->state = EST_FAIL_BEGIN;
			ev->fail_all_stopped = 0;
			clear_all_nodes_stopped(a);
		} else if (event_state_stopping(a)) {
			mark_node_stopped(a, rev->nodeid);
			list_for_each_entry(id, &rev->extended, list)
				mark_node_stopped(a, id->nodeid);
		}

		id = malloc(sizeof(struct nodeid));
		// FIXME: handle failed malloc
		id->nodeid = rev->nodeid;
		list_add(&id->list, &ev->extended);
		log_group(g, "extend active rev %d with failed node %d",
			  ev->nodeid, rev->nodeid);
		list_for_each_entry_safe(id, safe, &rev->extended, list) {
			list_del(&id->list);
			list_add(&id->list, &ev->extended);
			log_group(g, "extend active rev %d with failed node %d",
				  ev->nodeid, id->nodeid);
		}

		send_recover(g, rev);
		list_del(&rev->list);
		free_event(rev);
		return 1;
	}

	/* This is a really gross situation, wish I could find a better way
	   to deal with it... (rev's skip ahead of other queued ev's, I think
	   that's the root of the difficulties here, we don't know if the
	   rev has skipped ahead of our join on remote nodes or not).

	   If our own join event is current on other nodes, then we want a
	   rev (which will replace our join ev once it's starting).  If our
	   join event isn't current on other nodes, then recovery will occur
	   before we're added to the app group and the rev doesn't apply to us
	   (apart from needing to remove the failed node from the memb list).

	   We won't know if our join ev is current on other nodes, though,
	   until we see a message -- if the message event id is for our join,
	   then our ev is current and we'll process the rev after our ev, if
	   the message event id is for the rev, then the rev is being done
	   by the current members without us and our ev will be done later;
	   the rev doesn't apply to us.

	   Do nothing until we see a message indicating whether other nodes
	   are on our join ev (in which case go to "rev will abort curr" code),
	   or whether they're processing this rev (before our join ev comes
	   up) in which case we can drop the rev (NB attend to rs, too). */

	if (ev->state == EST_JOIN_STOP_WAIT && is_our_join(ev)) {

		log_group(g, "rev %d is for group we're waiting to join",
			  rev->nodeid);

		/* If the failed node is the only other app member apart
		   from us in the pending membership list, then we must go
		   ahead with our own join event, there will be no remote nodes
		   processing a rev or an ev for this group.  We send a recover
		   message so other nodes waiting to join after us will purge
		   their rev on the group. */

		if (a->node_count == 2) {
			node = find_app_node(a, rev->nodeid);
			us = find_app_node(a, our_nodeid);

			if (node && us) {
				log_group(g, "joining group with one other node"
					  " now dead rev %d", rev->nodeid);
				a->node_count--;
				list_del(&node->list);
				free(node);
				send_recover(g, rev);
				del_recovery_set(g, rev, 1);
				list_del(&rev->list);
				free_event(rev);
				return 0;
			}
		}

		/* Look for a remote node with stopped of 1, if we find one,
		   then fall through to the 'else if (event_state_stopping)'
		   below.  A remote node with stopped of 1 means we've received
		   a stopped message with an event_id of our join event. */

		list_for_each_entry(node, &a->nodes, list) {
			if (node->nodeid == our_nodeid)
				continue;
			if (node->stopped) {
				log_group(g, "our join is current on %d",
					  node->nodeid);
				log_group(g, "rev %d behind our join ev %llx",
					  rev->nodeid,
					  (unsigned long long)ev->id);
				goto next;
			}
		}

		/* Look through saved messages for one with an event_id
		   matching the rev, if we find one, then we get rid of this
		   rev and clear this group (that we're joining) from any
		   recovery sets that are sequencing recovery of groups the
		   failed node was in.  The other nodes are processing the
		   rev before processing our join ev. */
		   
		list_for_each_entry(save, &g->messages, list) {
			if (save->msg.ms_type == MSG_APP_INTERNAL)
				continue;
			if (save->msg.ms_event_id != rev->id)
				continue;

			log_group(g, "rev %d %llx ahead of our join ev %llx",
				  rev->nodeid,
				  (unsigned long long)rev->id,
				  (unsigned long long)ev->id);

			node = find_app_node(a, rev->nodeid);
			if (node) {
				a->node_count--;
				log_group(g, "not joined, remove %d rev %d",
					  node->nodeid, rev->nodeid);
				list_del(&node->list);
				free(node);
			}
			list_for_each_entry(id, &rev->extended, list) {
				node = find_app_node(a, id->nodeid);
				if (node) {
					a->node_count--;
					log_group(g, "not joined, remove %d "
						  "rev %d", id->nodeid,
						  rev->nodeid);
					list_del(&node->list);
					free(node);
				}
			}

			del_recovery_set(g, rev, 1);
			list_del(&rev->list);
			log_group(g, "got rid of rev %d for unjoined group",
				  rev->nodeid);
			free_event(rev);
			return 0;
		}

		log_group(g, "no messages indicating remote state of group");
		return 0;
	}

 next:
	/* Before starting the rev we need to apply the node addition/removal
	 * of the current ev to the app.  This means processing the current ev
	 * up through the starting stage.  So, we're sending the app the start
	 * to inform it of the ev node change, knowing that the start won't
	 * complete due to the node failure (pending rev), and knowing that
	 * we'll shortly be sending it a stop and new start for the rev.
	 *
	 * If the current event is waiting for a "stopped" message from failed
	 * node(s), fill in those stopped messages so we move along to the
	 * starting state so the recovery event can then take over. */

	if (event_state_starting(a) || event_state_all_started(a)) {
		log_group(g, "rev %d replaces current ev %d %s",
			  rev->nodeid, ev->nodeid, ev_state_str(ev));

		/* what we do for our own join when reaching JOIN_ALL_STARTED */
		if (is_our_join(ev)) {
			purge_messages(g);
			g->joining = 0;
		}
		clear_all_nodes_stopped(a);
		list_del(&rev->list);
		a->current_event = rev;
		free_event(ev);
		send_recover(g, rev);
		rv = 1;
	} else if (event_state_stopping(a)) {
		/* We'll come back through here multiple times until all the
		   stopped messages are received; we need to continue to
		   process this event that's stopping so it will get to the
		   starting state at which point the rev can replace it. */

		log_group(g, "rev %d will abort current ev %d %s",
			  rev->nodeid, ev->nodeid, ev_state_str(ev));

		ev->start_app_before_pending_rev = 1;

		mark_node_stopped(a, rev->nodeid);
		list_for_each_entry(id, &rev->extended, list)
			mark_node_stopped(a, id->nodeid);
		rv = 1;
	} else {
		log_group(g, "rev %d delayed for ev %d %s",
			  rev->nodeid, ev->nodeid, ev_state_str(ev));
	}

	/* FIXME: does the code above work ok if ev->nodeid == rev->noded
	   (joining node failed) */

	/* FIXME: if the current event is a leave and the leaving node has
	   failed, then replace the current event with the rev */

	return rv;
}

int process_app(group_t *g)
{
	app_t *a = g->app;
	event_t *ev = NULL;
	int rv = 0, ret;

	if (a->current_event) {
		rv += process_app_messages(g);

		ret = process_current_event(g);
		if (ret < 0)
			goto out;
		rv += ret;

		ret = recover_current_event(g);
		if (ret <= 0)
			goto out;

		ret = process_current_event(g);
		if (ret < 0)
			goto out;
		rv += ret;
	} else {

		/* We only take on a new non-recovery event if there are
		   no recovery sets outstanding.  The new event may be
		   to mount gfs X where there are no living mounters of X,
		   and the pending recovery set is to fence a node that
		   had X mounted.  update: relax this so events are taken
		   if there are unrecovered groups _at a lower level_. */

		ev = find_queued_recover_event(g);
		if (ev) {
			log_group(g, "set current event to recovery for %d",
				  ev->nodeid);
			list_del(&ev->list);
		} else if (!list_empty(&a->events)) {
#if 0
			if (!cman_quorate) {
				log_group(g, "no new event while inquorate");
			} else if (lower_groups_need_recovery(g)) {
				log_group(g, "no new event while lower level "
					  "groups need recovery");
			} else {
				ev = list_entry(a->events.next, event_t, list);
				list_del(&ev->list);
			}
#endif
			ev = list_entry(a->events.next, event_t, list);
			list_del(&ev->list);
		}

		if (ev) {
			a->need_first_event = 0;
			a->current_event = ev;
			rv = process_current_event(g);
		} else if (a->need_first_event) {
			log_group(g, "waiting for first cpg event");
		}
	}
 out:
	return rv;
}

/* process_apps() will be called again immediately if it returns > 0 */

int process_apps(void)
{
	group_t *g, *safe;
	int rv = 0;

	if (group_mode != GROUP_LIBGROUP)
		return 0;

	list_for_each_entry_safe(g, safe, &gd_groups, list) {
		rv += process_app(g);
		deliver_app_messages(g);
	}

	return rv;
}

/* This is a bit of a hack that may not be entirely necessary.  The problem
   we're solving with this function is when a node leaves a group and is
   collecting all the "stopped" messages from the remaining members, some
   of those members may fail, so we wouldn't get a stopped message from
   them and never finalize_our_leave (terminate the group).  I'm not entirely
   sure that we _need_ to wait for stopped messages from remaining members
   before we do the finalize_our_leave/terminate... The reasoning at this
   point is that when gfs is withdrawing, we want to be sure gfs is
   suspended everywhere before we leave the lockspace (which happens at
   terminate for the withdraw/leave) */

void groupd_down(int nodeid)
{
	group_t *g;

	list_for_each_entry(g, &gd_groups, list) {
		if (g->app &&
		    g->app->current_event &&
		    g->app->current_event->state == EST_LEAVE_STOP_WAIT &&
		    is_our_leave(g->app->current_event)) {
			log_group(g, "groupd down on %d, push our leave",
				  nodeid);
			mark_node_stopped(g->app, nodeid);
		}
	}
}

