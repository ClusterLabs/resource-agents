
/* Apply queued events to apps */

#include "gd_internal.h"

struct list_head recovery_sets;

struct save_msg {
	struct list_head list;
	int nodeid;
	msg_t msg;
};

struct recovery_set {
	struct list_head list;
	struct list_head entries;
	int nodeid;
};

struct recovery_entry {
	struct list_head list;
	group_t *group;
};

struct nodeid {
	struct list_head list;
	int nodeid;
};

void extend_recover_event(event_t *ev, int nodeid)
{
	struct nodeid *id;
	id = malloc(sizeof(struct nodeid));
	id->nodeid = nodeid;
	list_add(&id->list, &ev->extended);
}

/* When a node fails, all the groups that that node was in are tracked by
   one of these recovery_sets.  These are the groups that will need layered
   recovery, i.e. all effected groups must be completely stopped (the app
   stopped on all nodes) before any are restarted.  When restarted, the groups
   must be restarted in order from lowest level first.  All groups on each
   level must complete recovery (on all nodes) before recovery can begin for
   the next level. */

/* create a struct recovery_set and search through groups to
   make the rs reference those groups the failed node was in */

/* when the app code processes a rev, it will look for recovery
   sets (rs) that reference the group.  if all rs's that reference
   the group indicate that lower level groups are recovered, then
   the rev can be processed */

void add_recovery_set(int nodeid)
{
	struct recovery_set *rs;
	struct recovery_entry *re;
	group_t *g;
	node_t *node;

	rs = malloc(sizeof(*rs));
	rs->nodeid = nodeid;
	INIT_LIST_HEAD(&rs->entries);

	list_for_each_entry(g, &gd_groups, list) {
		list_for_each_entry(node, &g->app->nodes, list) {
			if (node->nodeid == nodeid) {
				re = malloc(sizeof(*re));
				re->group = g;
				list_add_tail(&re->list, &rs->entries);
				break;
			}
		}
	}

	if (!list_empty(&rs->entries))
		list_add_tail(&rs->list, &recovery_sets);
	else
		free(rs);
}

/* all groups referenced by a recovery set have been stopped on all nodes */

static int set_is_all_stopped(struct recovery_set *rs)
{
	struct recovery_entry *re;
	event_t *ev;

	list_for_each_entry(re, &rs->entries, list) {
		ev = re->group->app->current_event;

		if (ev && ev->state == EST_FAIL_ALL_STOPPED)
			continue;
		else
			return 0;
	}
	return 1;
}

static int all_levels_all_stopped(group_t *g, event_t *ev)
{
	struct recovery_set *rs;
	struct recovery_entry *re;
	int found = 0;

	list_for_each_entry(rs, &recovery_sets, list) {
		list_for_each_entry(re, &rs->entries, list) {
			if (re->group == g) {
				found = 1;
				if (set_is_all_stopped(rs))
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

static int level_is_recovered(struct recovery_set *rs, int level)
{
	struct recovery_entry *re;
	event_t *ev;

	list_for_each_entry(re, &rs->entries, list) {
		if (re->group->level != level)
			continue;

		ev = re->group->app->current_event;

		if (ev == NULL)
			continue;
		else if (ev->state < EST_FAIL_BEGIN)
			continue;
		else
			return 0;
	}
	return 1;
}

static int lower_level_recovered(group_t *g)
{
	struct recovery_set *rs;
	struct recovery_entry *re;
	int found = 0;

	list_for_each_entry(rs, &recovery_sets, list) {
		list_for_each_entry(re, &rs->entries, list) {
			if (re->group == g) {
				found = 1;
				if (level_is_recovered(rs, g->level - 1))
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
	struct recovery_entry *re;
	int found = 0;

	list_for_each_entry(rs, &recovery_sets, list) {
		list_for_each_entry(re, &rs->entries, list) {
			if (re->group == g) {
				found = 1;
				if (level_is_lowest(rs, g->level))
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


/* when any group finishes recovery, we should go through rs's and
   see if any refer to groups that are all recovered and can be freed */


static event_t *create_event(group_t *g)
{
	event_t *ev;

	ev = malloc(sizeof(event_t));
	ASSERT(ev);

	memset(ev, 0, sizeof(event_t));

	INIT_LIST_HEAD(&ev->memb);
	ev->event_nr = ++gd_event_nr;

	return ev;
}

/* go through the queue and find all rev's with the same rev_id to
   process at once, i.e. multiple nodes failed at once */

int queue_app_recover(group_t *g, int nodeid)
{
	event_t *ev;

	ev = create_event(g);
	ev->nodeid = nodeid;
	ev->state = EST_FAIL_BEGIN;

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

int queue_app_join(group_t *g, int nodeid)
{
	event_t *ev;

	ev = create_event(g);
	ev->nodeid = nodeid;
	ev->state = EST_JOIN_BEGIN;

	log_group(g, "queue join event for nodeid %d", nodeid);

	if (nodeid == our_nodeid)
		add_event_nodes(g, ev);

	list_add_tail(&ev->list, &g->app->events);
	return 0;
}

int queue_app_leave(group_t *g, int nodeid)
{
	event_t *ev;

	ev = create_event(g);
	ev->nodeid = nodeid;
	ev->state = EST_LEAVE_BEGIN;

	log_group(g, "queue leave event for nodeid %d", nodeid);

	list_add_tail(&ev->list, &g->app->events);
	return 0;
}

/* messages are STOPPED or STARTED */

int queue_app_message(group_t *g, msg_t *msg, int nodeid)
{
	struct save_msg *save;

	save = malloc(sizeof(struct save_msg));
	ASSERT(save);
	memcpy(&save->msg, msg, sizeof(msg_t));
	save->nodeid = nodeid;

	log_group(g, "queue message %s from %d",
		  (msg->ms_type == MSG_APP_STOPPED ? "stopped" : "started"),
		  nodeid);

	/* might be able to put the messages list under g->app... */
	list_add_tail(&save->list, &g->messages);
	return 0;
}

static void del_app_nodes(app_t *a)
{
	node_t *node, *tmp;

	list_for_each_entry_safe(node, tmp, &a->nodes, list) {
		list_del(&node->list);
		free(node);
	}
}

static node_t *find_app_node(app_t *a, int nodeid)
{
	node_t *node;

	list_for_each_entry(node, &a->nodes, list) {
		if (node->nodeid == nodeid)
			return node;
	}
	return NULL;
}

static int send_stopped(group_t *g)
{
	msg_t msg;

	memset(&msg, 0, sizeof(msg));
	msg.ms_type = MSG_APP_STOPPED;

	log_group(g, "send stopped");
	return send_message(g, (char *) &msg, sizeof(msg));
}

static int send_started(group_t *g)
{
	msg_t msg;

	memset(&msg, 0, sizeof(msg));
	msg.ms_type = MSG_APP_STARTED;

	log_group(g, "send started");
	return send_message(g, (char *) &msg, sizeof(msg));
}

int do_stopdone(char *name, int level)
{
	group_t *g;
	g = find_group_level(name, level);
	return send_stopped(g);
}

int do_startdone(char *name, int level)
{
	group_t *g;
	g = find_group_level(name, level);
	return send_started(g);
}

static int is_our_join(event_t *ev)
{
	return (ev->nodeid == our_nodeid);
}

static int is_our_leave(event_t *ev)
{
	return (ev->nodeid == our_nodeid);
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

static int process_current_event(group_t *g)
{
	app_t *a = g->app;
	event_t *ev = a->current_event, *ev_tmp, *ev_safe;
	node_t *node, *n;
	struct nodeid *id, *id_safe;
	int rv = 0, do_start = 0;

	log_group(g, "process_current_event state %s", ev_state_str(ev));

	switch (ev->state) {

	case EST_JOIN_BEGIN:
		ev->state = EST_JOIN_STOP_WAIT;

		if (is_our_join(ev))
			send_stopped(g);
		else
			app_stop(a);
		break;

	case EST_JOIN_ALL_STOPPED:
		ev->state = EST_JOIN_START_WAIT;

		/* the initial set of members that we've joined, includes us */
		if (is_our_join(ev)) {
			list_for_each_entry_safe(node, n, &ev->memb, list) {
				list_move(&node->list, &a->nodes);
				a->node_count++;
				log_group(g, "app node init: add %d total %d",
					  node->nodeid, a->node_count);
			}
		} else {
			node = new_node(ev->nodeid);
			list_add(&node->list, &a->nodes);
			a->node_count++;
			log_group(g, "app node join: add %d total %d",
				  node->nodeid, a->node_count);
		}

		app_start(a);
		break;

	case EST_JOIN_ALL_STARTED:
		app_finish(a);
		free(ev);
		a->current_event = NULL;
		rv = 1;
		break;

	case EST_LEAVE_BEGIN:
		ev->state = EST_LEAVE_STOP_WAIT;
		app_stop(a);
		break;

	case EST_LEAVE_ALL_STOPPED:
		ev->state = EST_LEAVE_START_WAIT;

		if (is_our_leave(ev)) {
			g->app = NULL;
			del_app_nodes(a);
			free(a);
			/* FIXME: check if there are any recovery_sets
			   referencing this group somehow */
			remove_group(g);
		} else {
			node = find_app_node(a, ev->nodeid);
			list_del(&node->list);
			a->node_count--;
			log_group(g, "app node leave: del %d total %d",
				  node->nodeid, a->node_count);
			free(node);

			app_start(a);
		}
		break;

	case EST_LEAVE_ALL_STARTED:
		app_finish(a);
		free(ev);
		a->current_event = NULL;
		rv = 1;
		break;

	case EST_FAIL_BEGIN:
		ev->state = EST_FAIL_STOP_WAIT;
		app_stop(a);
		break;

	case EST_FAIL_ALL_STOPPED:

		/* when recovering for failed nodes, we immediately stop all
		   apps the node was involved with but wait for quorum before
		   starting them again  */

		/* FIXME: how are we assured that the cman callback that
		   clears quorate will happen before we begin processing
		   recovery events? */

		if (!cman_quorate)
			break;

		if (lowest_level(g)) {
			if (all_levels_all_stopped(g, ev)) {
				ev->state = EST_FAIL_START_WAIT;
				do_start = 1;
			}
		} else {
			if (lower_level_recovered(g)) {
				ev->state = EST_FAIL_START_WAIT;
				do_start = 1;
			}
		}

		if (!do_start)
			break;

		log_group(g, "app node fail: del %d total %d",
			  node->nodeid, a->node_count);

		node = find_app_node(a, ev->nodeid);
		list_del(&node->list);
		free(node);
		a->node_count--;

		list_for_each_entry_safe(id, id_safe, &ev->extended, list) {
			node = find_app_node(a, id->nodeid);
			list_del(&node->list);
			free(node);
			a->node_count--;

			log_group(g, "app node fail: del %d total %d (ext)",
			  	  node->nodeid, a->node_count);

			list_del(&id->list);
			free(id);
		}

		app_start(a);
		break;

	case EST_FAIL_ALL_STARTED:
		app_finish(a);
		free(ev);
		a->current_event = NULL;
		rv = 1;
		break;
	}

	return rv;
}

static int event_state_stopping(app_t *a)
{
	if (a->current_event->state == EST_JOIN_STOP_WAIT ||
	    a->current_event->state == EST_LEAVE_STOP_WAIT ||
	    a->current_event->state == EST_FAIL_STOP_WAIT)
		return TRUE;
	return FALSE;
}

static int event_state_starting(app_t *a)
{
	if (a->current_event->state == EST_JOIN_START_WAIT ||
	    a->current_event->state == EST_LEAVE_START_WAIT ||
	    a->current_event->state == EST_FAIL_START_WAIT)
		return TRUE;
	return FALSE;
}

static int mark_node_stopped(app_t *a, int nodeid)
{
	node_t *node;

	if (!event_state_stopping(a)) {
		log_error(a->g, "mark_node_stopped: event not stopping %d ",
			  "from %d", a->current_event->state, nodeid);
		return -1;
	}

	node = find_app_node(a, nodeid);
	if (!node) {
		log_error(a->g, "mark_node_stopped: no nodeid %d", nodeid);
		return -1;
	}

	node->stopped = 1;
	node->started = 0;

	return 0;
}

static int mark_node_started(app_t *a, int nodeid)
{
	node_t *node;

	if (!event_state_starting(a)) {
		log_error(a->g, "mark_node_started: event not starting %d ",
			  "from %d", a->current_event->state, nodeid);
		return -1;
	}

	node = find_app_node(a, nodeid);
	if (!node) {
		log_error(a->g, "mark_node_started: no nodeid %d", nodeid);
		return -1;
	}

	node->stopped = 0;
	node->started = 1;

	return 0;
}

static int all_nodes_stopped(app_t *a)
{
	node_t *node;

	list_for_each_entry(node, &a->nodes, list) {
		if (!node->stopped) {
			ASSERT(node->started);
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
			ASSERT(node->stopped);
			return FALSE;
		}
	}
	return TRUE;
}

static int is_stopped_message(app_t *a, msg_t *msg)
{
	return (msg->ms_type == MSG_APP_STOPPED);
}

static int is_started_message(app_t *a, msg_t *msg)
{
	return (msg->ms_type == MSG_APP_STARTED);
}

static int process_app_messages(group_t *g)
{
	app_t *a = g->app;
	struct save_msg *save, *tmp;
	int rv = 0;

	list_for_each_entry_safe(save, tmp, &g->messages, list) {
		if (is_stopped_message(a, &save->msg))
			mark_node_stopped(a, save->nodeid);
		else if (is_started_message(a, &save->msg))
			mark_node_started(a, save->nodeid);
		else {
			log_error(g, "process_app_messages: invalid type %d "
				  "from %d", save->msg.ms_type, save->nodeid);
			continue;
		}

		list_del(&save->list);
		free(save);
		rv = 1;
	}

	if (event_state_stopping(a) && all_nodes_stopped(a))
		a->current_event->state++;

	if (event_state_starting(a) && all_nodes_started(a))
		a->current_event->state++;

	return rv;
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

static int process_app(group_t *g)
{
	app_t *a = g->app;
	event_t *rev, *ev;
	int rv;

 restart:
	rv = 0;

	if (a->current_event) {
		rv += process_app_messages(g);
		rv += process_current_event(g);

		/* if there's a recovery event, the current event is
		   abandoned and replaced with the recovery event */

		rev = find_queued_recover_event(g);
		if (!rev)
			goto out;

		list_del(&rev->list);
		ev = a->current_event;
		a->current_event = rev;

		/* if the group hadn't been started for the replaced
		   event, then put it back on the list to do later */

		if (group_started(ev))
			free(ev);
		else
			list_add(&ev->list, &g->app->events);

		goto restart;
	} else {
		ev = find_queued_recover_event(g);

		if (!ev && !list_empty(&a->events))
			ev = list_entry(a->events.next, event_t, list);

		if (ev) {
			list_del(&ev->list);
			a->current_event = ev;
			rv = process_current_event(g);
		}
	}
 out:
	return rv;
}

int process_apps(void)
{
	group_t *g;
	int rv = 0;

	list_for_each_entry(g, &gd_groups, list)
		rv += process_app(g);

	return rv;
}

