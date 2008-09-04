
/* Initiate join/leave requests from apps */

#include "gd_internal.h"


group_t *find_group_level(char *name, int level)
{
	group_t *g;

	list_for_each_entry(g, &gd_levels[level], level_list) {
		if (!strcmp(g->name, name))
			return g;
	}
	return NULL;
}

int create_group(char *name, int level, group_t **g_out)
{
	group_t *g;

	g = malloc(sizeof(*g));
	if (!g)
		return -ENOMEM;

	memset(g, 0, sizeof(*g));

	strcpy(g->name, name);
	g->namelen = strlen(name);
	g->level = level;
	INIT_LIST_HEAD(&g->memb);
	INIT_LIST_HEAD(&g->messages);

	list_add_tail(&g->list, &gd_groups);
	list_add_tail(&g->level_list, &gd_levels[level]);

	*g_out = g;
	return 0;
}

void free_group_memb(group_t *g)
{
	node_t *node, *n;

	list_for_each_entry_safe(node, n, &g->memb, list) {
		list_del(&node->list);
		free(node);
	}
}

void remove_group(group_t *g)
{
	list_del(&g->list);
	list_del(&g->level_list);
	free_group_memb(g);
	free(g);
}

app_t *create_app(group_t *g)
{
	app_t *a;

	a = malloc(sizeof(app_t));
	if (!a)
		return NULL;
	memset(a, 0, sizeof(app_t));

	a->need_first_event = 1;
	INIT_LIST_HEAD(&a->nodes);
	INIT_LIST_HEAD(&a->events);
	a->g = g;
	g->app = a;

	return a;
}

int do_join(char *name, int level, int ci)
{
	group_t *g;
	app_t *a;
	int rv;

	g = find_group_level(name, level);
	if (g) {
		log_group(g, "%d:%s can't join existing group", level, name);
		rv = -EEXIST;
		goto out;
	}

	rv = create_group(name, level, &g);
	if (rv)
		goto out;

	a = create_app(g);
	if (!a) {
		rv = -ENOMEM;
		goto out;
	}

	a->client = ci;

	log_debug("%d:%s got join", level, name);
	g->joining = 1;
	rv = do_cpg_join(g);
 out:
	return rv;
}

int do_leave(char *name, int level)
{
	group_t *g;
	event_t *ev;
	int rv;

	g = find_group_level(name, level);
	if (!g)
		return -ENOENT;

	if (!g->app) {
		log_group(g, "leave: no app");
		return -EINVAL;
	}

	if (g->joining) {
		log_error(g, "leave: still joining");
		return -EAGAIN;
	}

	if (g->leaving) {
		log_error(g, "leave: already leaving");
		return -EBUSY;
	}

	ev = g->app->current_event;

	if (ev && ev->nodeid == our_nodeid) {
		log_error(g, "leave: busy event %llx state %s",
			  (unsigned long long)ev->id,
			  ev_state_str(ev));
		return -EAGAIN;
	}

	list_for_each_entry(ev, &g->app->events, list) {
		ASSERT(ev->nodeid != our_nodeid);
		log_group(g, "do_leave: found queued event id %llx",
			  (unsigned long long)ev->id);
	}

	log_debug("%d:%s got leave", level, name);
	g->leaving = 1;
	rv = do_cpg_leave(g);
	return rv;
}

node_t *new_node(int nodeid)
{
	node_t *node;

	node = malloc(sizeof(*node));
	memset(node, 0, sizeof(*node));
	node->nodeid = nodeid;
	return node;
}

