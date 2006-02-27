
/* Apply queued events to apps */

#include "gd_internal.h"

struct list_head recovery_sets;

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
	INIT_LIST_HEAD(&ev->extended);
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

int queue_app_message(group_t *g, struct save_msg *save)
{
	char *m = "unknown";

	switch (save->msg.ms_type) {
	case MSG_APP_STOPPED:
		m = "stopped";
		break;
	case MSG_APP_STARTED:
		m = "started";
		break;
	case MSG_APP_INTERNAL:
		m = "internal";
		break;
	}

	log_group(g, "queue message %s from %d", m, save->nodeid);

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

static int is_our_join(event_t *ev)
{
	return (ev->nodeid == our_nodeid);
}

static int is_our_leave(event_t *ev)
{
	return (ev->nodeid == our_nodeid);
}

/* called after the local app has acked that it is stopped as part
   of our own leave.  We've gotten the final confchg for our leave
   so we can't send anything out to the group at this point. */

void finalize_our_leave(group_t *g)
{
	app_t *a = g->app;

	log_group(g, "finalize_our_leave");

	app_terminate(a);
	cpg_finalize(g->cpg_handle);
	client_dead(g->cpg_client);
	g->app = NULL;
	del_app_nodes(a);
	free(a);

	/* FIXME: check if there are any recovery_sets
	   referencing this group somehow */

	remove_group(g);
}

static int send_stopped(group_t *g)
{
	msg_t msg;
	event_t *ev = g->app->current_event;

	if (ev && ev->state == EST_LEAVE_STOP_WAIT && is_our_leave(ev)) {
		finalize_our_leave(g);
		return 0;
	}

	memset(&msg, 0, sizeof(msg));
	msg.ms_type = MSG_APP_STOPPED;
	msg.ms_id = g->global_id;

	log_group(g, "send stopped");
	return send_message(g, &msg, sizeof(msg));
}

static int send_started(group_t *g)
{
	msg_t msg;

	memset(&msg, 0, sizeof(msg));
	msg.ms_type = MSG_APP_STARTED;
	msg.ms_id = g->global_id;

	log_group(g, "send started");
	return send_message(g, &msg, sizeof(msg));
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

static int process_current_event(group_t *g)
{
	app_t *a = g->app;
	event_t *ev = a->current_event, *ev_tmp, *ev_safe;
	node_t *node, *n;
	struct nodeid *id, *id_safe;
	int rv = 0, do_start = 0, count;

	log_group(g, "process_current_event state %s", ev_state_str(ev));

	switch (ev->state) {

	case EST_JOIN_BEGIN:
		ev->state = EST_JOIN_STOP_WAIT;

		if (is_our_join(ev)) {
			send_stopped(g);

			/* the initial set of members that we've joined,
			   includes us */
			list_for_each_entry_safe(node, n, &ev->memb, list) {
				list_move(&node->list, &a->nodes);
				a->node_count++;
				log_group(g, "app node init: add %d total %d",
					  node->nodeid, a->node_count);
			}
		} else {
			app_stop(a);

			node = new_node(ev->nodeid);
			list_add(&node->list, &a->nodes);
			a->node_count++;
			log_group(g, "app node join: add %d total %d",
				  node->nodeid, a->node_count);
		}
		break;

	case EST_JOIN_STOP_WAIT:
		count = count_nodes_not_stopped(a);
		log_group(g, "waiting for %d more nodes to be stopped", count);
		break;

	case EST_JOIN_ALL_STOPPED:
		ev->state = EST_JOIN_START_WAIT;

		if (!g->have_set_id) {
			g->have_set_id = 1;
			app_setid(a);
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

		/* Set leaving node as stopped because it can't send a
		   stopped message for us to receive.

                   FIXME: see below about getting the leaving node's
		   special stopped message through the groupd group so
		   we can be sure it's stopped before we start.  When we
		   do that, then we won't want to set the leaving node
		   as stopped here. */

		if (!is_our_leave(ev)) {
			node = find_app_node(a, ev->nodeid);
			ASSERT(node);
			node->stopped = 1;
		}

		break;

	case EST_LEAVE_STOP_WAIT:
		count = count_nodes_not_stopped(a);
		log_group(g, "waiting for %d more nodes to be stopped", count);
		break;

		/* The leaving node won't get the "stopped" messages
		   from the remaining group members (or be able to send/recv
		   its own stopped message) because the confchg for our leave
		   is the last thing we get from libcpg.
		   
		   The other nodes can't get our "stopped" message either;
		   we can't send to the group since we're not in it any more.

		   FIXME: we should add an extra level of certainty to
		   the leaving process by sending a special "stopped" message
		   (after the local app has acked that it's in fact stopped)
		   through the groupd group to the remaining group members.
		   The remaining members should wait for our special out-of-
		   band "stopped" message for the group we've left before they
		   go ahead and start following our leave.  This adds
		   certainly that they're not starting the group before
		   our stop for our leave is actually completed. */

	case EST_LEAVE_ALL_STOPPED:
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
		free(ev);
		a->current_event = NULL;
		rv = 1;
		break;

	case EST_FAIL_BEGIN:
		ev->state = EST_FAIL_STOP_WAIT;
		app_stop(a);

		/* set the failed node as stopped since we won't
		   be getting a "stopped" message from it */

		node = find_app_node(a, ev->nodeid);
		ASSERT(node);
		node->stopped = 1;

		break;

	case EST_FAIL_STOP_WAIT:
		count = count_nodes_not_stopped(a);
		log_group(g, "waiting for %d more nodes to be stopped", count);
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

	default:
		log_group(g, "nothing to do");
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

	log_group(a->g, "mark node %d stopped", nodeid);

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
	int rv = 0;

	list_for_each_entry_safe(save, tmp, &g->messages, list) {
		switch (save->msg.ms_type) {

		case MSG_APP_STOPPED:
			mark_node_stopped(a, save->nodeid);
			break;

		case MSG_APP_STARTED:
			mark_node_started(a, save->nodeid);
			break;

		case MSG_APP_INTERNAL:
			continue;

		default:
			log_error(g, "process_app_messages: invalid type %d "
				  "from %d", save->msg.ms_type, save->nodeid);
		}

		if (g->global_id == 0 && save->msg.ms_id != 0) {
			g->global_id = save->msg.ms_id;
			log_group(g, "set global_id %x from %d",
				  g->global_id, save->nodeid);
		}

		list_del(&save->list);
		if (save->msg_long)
			free(save->msg_long);
		free(save);
		rv = 1;
	}

	if (event_state_stopping(a) && all_nodes_stopped(a))
		a->current_event->state++;

	if (event_state_starting(a) && all_nodes_started(a))
		a->current_event->state++;

	return rv;
}

static int deliver_app_messages(group_t *g)
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
	printf("cpg handle: %x\n", g->cpg_handle);
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

static int process_app(group_t *g)
{
	app_t *a = g->app;
	event_t *rev, *ev = NULL;
	int rv = 0;

	if (a->current_event) {
		rv += process_app_messages(g);
		rv += process_current_event(g);

		/* if the current event has started the app and there's
		   a recovery event, the current event is abandoned and
		   replaced with the recovery event */

		/* (this assumes that we don't ever remove/free the group
		   in process_current_event) */

		ev = a->current_event;
		rev = find_queued_recover_event(g);

		if (rev && event_state_starting(a)) {
			list_del(&rev->list);
			a->current_event = rev;
			free(ev);
			rv = 1;
		}

		/* if we're waiting for a "stopped" message from a failed
		   node, make one up so we move along to a starting state
		   so the recovery event can then take over */

		/*
		if (rev && event_state_stopping(ev))
			make_up_stops(g, rev);
		*/

		/* if the current event is a leave and the leaving node
		   has failed, then replace the current event with the
		   rev */

	} else {
		ev = find_queued_recover_event(g);
		if (ev) {
			log_debug("setting recovery event");
			list_del(&ev->list);
		} else if (!list_empty(&a->events)) {
			ev = list_entry(a->events.next, event_t, list);
			list_del(&ev->list);
		}

		if (ev) {
			a->current_event = ev;
			rv = process_current_event(g);
		}
	}
 out:
	return rv;
}

int process_apps(void)
{
	group_t *g, *safe;
	int rv = 0;

	list_for_each_entry_safe(g, safe, &gd_groups, list) {
		rv += process_app(g);
		deliver_app_messages(g);
	}

	return rv;
}

