
/* Interface with openais's closed-process-group (cpg) API */

#include "gd_internal.h"

static cpg_handle_t groupd_handle;
static struct cpg_name groupd_name;
static int global_id_counter = 0;
static int groupd_joined = 0;
static int groupd_ci;

static int			got_confchg;
static struct cpg_address	saved_member[MAX_GROUP_MEMBERS];
static struct cpg_address	saved_joined[MAX_GROUP_MEMBERS];
static struct cpg_address	saved_left[MAX_GROUP_MEMBERS];
static int			saved_member_count;
static int			saved_joined_count;
static int			saved_left_count;
static cpg_handle_t		saved_handle;
static struct cpg_name		saved_name;
static int			saved_nodeid;
static int			saved_pid;


static node_t *find_group_node(group_t *g, int nodeid)
{
	node_t *node;

	list_for_each_entry(node, &g->memb, list) {
		if (node->nodeid == nodeid)
			return node;
	}
	return NULL;
}

static void process_node_down(group_t *g, int nodeid)
{
	node_t *node;
	event_t *ev, *ev_safe;
	int no_rev = 0;

	log_group(g, "process_node_down %d", nodeid);

	node = find_group_node(g, nodeid);
	if (!node) {
		log_error(g, "process_node_down: no member %d", nodeid);
		return;
	}

	list_del(&node->list);
	g->memb_count--;
	free(node);

	log_group(g, "group del node %d total %d - down",
		  nodeid, g->memb_count);

	/* purge any queued join/leave events from the dead node */

	list_for_each_entry_safe(ev, ev_safe, &g->app->events, list) {
		if (ev->nodeid != nodeid)
			continue;

		if (ev->state == EST_JOIN_BEGIN ||
		    ev->state == EST_LEAVE_BEGIN) {
			if (ev->state == EST_JOIN_BEGIN)
				no_rev = 1;

			log_group(g, "purge event %s from %d", ev_state_str(ev),
			  	  nodeid);
			del_event_nodes(ev);
			list_del(&ev->list);
			free(ev);
		}
	}

	/* the failed node was never added to the app, so the app
	   doesn't need to be recovered for it */
	if (no_rev)
		return;

	ev = find_queued_recover_event(g);
	if (ev)
		extend_recover_event(g, ev, nodeid);
	else
		queue_app_recover(g, nodeid);
}

static void process_node_join(group_t *g, int nodeid)
{
	node_t *node;
	int i;

	log_group(g, "process_node_join %d", nodeid);

	if (nodeid == our_nodeid) {
		for (i = 0; i < saved_member_count; i++) {
			node = new_node(saved_member[i].nodeId);
			list_add_tail(&node->list, &g->memb);
			g->memb_count++;
			log_group(g, "group add node %d total %d - init",
				  node->nodeid, g->memb_count);
		}

		/* if we're the first one to join (create) the group,
		   then set its global_id */

		if (saved_member_count == 1) {
			g->global_id = (++global_id_counter << 16) |
				       (0x0000FFFF & our_nodeid);
			log_group(g, "create group id %x our_nodeid %d",
				  g->global_id, our_nodeid);
		}
	} else {
		node = new_node(nodeid);
		list_add_tail(&node->list, &g->memb);
		g->memb_count++;
		log_group(g, "group add node %d total %d",
			  node->nodeid, g->memb_count);
	}

	queue_app_join(g, nodeid);
}

static void process_node_leave(group_t *g, int nodeid)
{
	node_t *node;

	log_group(g, "process_node_leave %d", nodeid);

	node = find_group_node(g, nodeid);
	if (!node) {
		log_error(g, "process_node_leave: no member %d", nodeid);
		return;
	}

	list_del(&node->list);
	g->memb_count--;
	free(node);

	log_group(g, "group del node %d total %d", nodeid, g->memb_count);

	queue_app_leave(g, nodeid);
}

void process_groupd_confchg(void)
{
	int i, found = 0;

	log_print("process_groupd_confchg members %d -%d +%d",
		  saved_member_count, saved_left_count, saved_joined_count);

	for (i = 0; i < saved_member_count; i++) {
		if (saved_member[i].nodeId == our_nodeid &&
		    saved_member[i].pid == (uint32_t) getpid()) {
			found = 1;
		}
	}

	if (found)
		groupd_joined = 1;
	else
		log_print("we are not in groupd confchg: %u %u",
			  our_nodeid, (uint32_t) getpid());

	for (i = 0; i < saved_left_count; i++) {
		if (saved_left[i].reason != CPG_REASON_LEAVE)
			add_recovery_set(saved_left[i].nodeId);
	}
}

/* FIXME: also match name */

group_t *find_group_by_handle(cpg_handle_t h)
{
	group_t *g;

	list_for_each_entry(g, &gd_groups, list) {
		if (g->cpg_handle == h)
			return g;
	}
	return NULL;
}

void deliver_cb(cpg_handle_t handle, struct cpg_name *group_name,
		uint32_t nodeid, uint32_t pid, void *data, int data_len)
{
	group_t *g;
	struct save_msg *save;
	msg_t *msg = (msg_t *) data;
	char *buf;
	char name[MAX_NAMELEN+1];
	int len;

	memset(&name, 0, sizeof(name));

	msg_bswap_in(msg);

	if (handle == groupd_handle) {
		memcpy(&name, &msg->ms_name, MAX_NAMELEN);

		g = find_group_level(name, msg->ms_level);
		if (!g)
			return;
	} else {
		g = find_group_by_handle(handle);
		if (!g) {
			len = group_name->length;
			if (len > MAX_NAMELEN)
				len = MAX_NAMELEN;
			memcpy(&name, &group_name->value, len);

			log_print("deliver_cb no group handle %d name %s",
				  handle, name);
			return;
		}
	}

	/*
	log_group(g, "deliver from %d len %d", nodeid, data_len);
	*/

	save = malloc(sizeof(struct save_msg));
	memset(save, 0, sizeof(struct save_msg));
	save->nodeid = nodeid;
	save->msg_len = data_len;

	if (data_len > sizeof(msg_t)) {
		buf = malloc(data_len);
		memcpy(buf, data, data_len);
		save->msg_long = buf;
		memcpy(&save->msg, data, sizeof(msg_t));
	} else
		memcpy(&save->msg, data, sizeof(msg_t));

	queue_app_message(g, save);
}

void process_confchg(void)
{
	group_t *g;
	int i;

	if (saved_handle == groupd_handle) {
		process_groupd_confchg();
		return;
	}

	g = find_group_by_handle(saved_handle);
	if (!g) {
		log_print("process_confchg: no group for handle %u name %s",
			  saved_handle, saved_name.value);
		return;
	}

	log_group(g, "process_confchg members %d -%d +%d",
		  saved_member_count, saved_left_count, saved_joined_count);

	for (i = 0; i < saved_joined_count; i++)
		process_node_join(g, saved_joined[i].nodeId);

	for (i = 0; i < saved_left_count; i++) {
		log_group(g, "node %d removed reason %d",
			  saved_left[i].nodeId, saved_left[i].reason);

		if (saved_left[i].reason == CPG_REASON_LEAVE)
			process_node_leave(g, saved_left[i].nodeId);
		else
			process_node_down(g, saved_left[i].nodeId);
	}
}

void confchg_cb(cpg_handle_t handle, struct cpg_name *group_name,
		struct cpg_address *member_list, int member_list_entries,
		struct cpg_address *left_list, int left_list_entries,
		struct cpg_address *joined_list, int joined_list_entries)
{
	group_t *g;
	char *name = "unknown";
	int i, level = -1;

	if (handle == groupd_handle)
		name = "groupd";
	else {
		g = find_group_by_handle(handle);
		if (g) {
			name = g->name;
			level = g->level;
		}
	}

	log_print("%d:%s confchg members %d -%d +%d", level, name,
		  member_list_entries, left_list_entries, joined_list_entries);

	saved_handle = handle;

	if (left_list_entries > MAX_GROUP_MEMBERS) {
		log_print("left_list_entries %d", left_list_entries);
		left_list_entries = MAX_GROUP_MEMBERS;
	}
	if (joined_list_entries > MAX_GROUP_MEMBERS) {
		log_print("joined_list_entries %d", joined_list_entries);
		joined_list_entries = MAX_GROUP_MEMBERS;
	}
	if (member_list_entries > MAX_GROUP_MEMBERS) {
		log_print("member_list_entries %d", joined_list_entries);
		member_list_entries = MAX_GROUP_MEMBERS;
	}

	saved_left_count = left_list_entries;
	saved_joined_count = joined_list_entries;
	saved_member_count = member_list_entries;

	memset(&saved_name, 0, sizeof(saved_name));
	saved_name.length = group_name->length;
	memcpy(&saved_name.value, &group_name->value, group_name->length);

	for (i = 0; i < left_list_entries; i++)
		saved_left[i] = left_list[i];

	for (i = 0; i < joined_list_entries; i++)
		saved_joined[i] = joined_list[i];

	for (i = 0; i < member_list_entries; i++)
		saved_member[i] = member_list[i];

	got_confchg = 1;
}

cpg_callbacks_t callbacks = {
	.cpg_deliver_fn = deliver_cb,
	.cpg_confchg_fn = confchg_cb,
};

void process_cpg(int ci)
{
	group_t *g;
	cpg_error_t error;
	cpg_handle_t handle;
	int found = 0;

	if (ci == groupd_ci) {
		handle = groupd_handle;
		goto dispatch;
	}

	list_for_each_entry(g, &gd_groups, list) {
		if (g->cpg_client == ci) {
			handle = g->cpg_handle;
			found = 1;
			break;
		}
	}

	if (!found) {
		log_print("process_cpg: no group found for ci %d", ci);
		sleep(1);
		return;
	}

 dispatch:
	got_confchg = 0;

	error = cpg_dispatch(handle, CPG_DISPATCH_ONE);
	if (error != CPG_OK) {
		log_print("cpg_dispatch error %d", error);
		return;
	}

	if (got_confchg)
		process_confchg();
}

int setup_cpg(void)
{
	cpg_error_t error;
	int fd;

	error = cpg_initialize(&groupd_handle, &callbacks);
	if (error != CPG_OK) {
		log_print("cpg_initialize error %d", error);
		return error;
	}

	cpg_fd_get(groupd_handle, &fd);

	groupd_ci = client_add(fd, process_cpg, NULL);

	memset(&groupd_name, 0, sizeof(groupd_name));
	strcpy(groupd_name.value, "groupd");
	groupd_name.length = 7;

	error = cpg_join(groupd_handle, &groupd_name);
	if (error != CPG_OK) {
		log_print("cpg_join error %d", error);
		cpg_finalize(groupd_handle);
		return error;
	}

	log_debug("setup_cpg groupd_handle %llx", groupd_handle);
	return 0;
}

int do_cpg_join(group_t *g)
{
	cpg_error_t error;
	cpg_handle_t h;
	struct cpg_name name;
	int fd, ci;

	error = cpg_initialize(&h, &callbacks);
	if (error != CPG_OK) {
		log_group(g, "cpg_initialize error %d", error);
		return error;
	}

	cpg_fd_get(h, &fd);

	ci = client_add(fd, process_cpg, NULL);

	g->cpg_client = ci;
	g->cpg_handle = h;
	g->cpg_fd = fd;

	memset(&name, 0, sizeof(name));
	sprintf(name.value, "%d_%s", g->level, g->name);
	name.length = strlen(name.value) + 1;

	log_group(g, "is cpg client %d name %s handle %llx", ci, name.value, h);

	error = cpg_join(h, &name);
	if (error != CPG_OK) {
		log_group(g, "cpg_join error %d", error);
		cpg_finalize(h);
		return error;
	}

	log_group(g, "cpg_join ok");
	return 0;
}

int do_cpg_leave(group_t *g)
{
	cpg_error_t error;
	struct cpg_name name;

	memset(&name, 0, sizeof(name));
	sprintf(name.value, "%d_%s", g->level, g->name);
	name.length = strlen(name.value) + 1;

	error = cpg_leave(g->cpg_handle, &name);
	if (error != CPG_OK) {
		log_group(g, "cpg_leave error %d", error);

		/* FIXME: what do do here? */
	}

	log_group(g, "cpg_leave ok");
	return 0;
}

static int _send_message(cpg_handle_t h, group_t *g, void *buf, int len)
{
	struct iovec iov;
	cpg_error_t error;
	int retries = 0;

	iov.iov_base = buf;
	iov.iov_len = len;

 retry:
	error = cpg_mcast_joined(h, CPG_TYPE_AGREED, &iov, 1);
	if (error != CPG_OK)
		log_group(g, "cpg_mcast_joined error %d handle %llx", error, h);
	if (error == CPG_ERR_TRY_AGAIN) {
		/* FIXME: backoff say .25 sec, .5 sec, .75 sec, 1 sec */
		retries++;
		if (retries > 3)
			sleep(1);
		goto retry;
	}

	return 0;
}

int send_message_groupd(group_t *g, void *buf, int len)
{
	return _send_message(groupd_handle, g, buf, len);
}

int send_message(group_t *g, void *buf, int len)
{
	return _send_message(g->cpg_handle, g, buf, len);
}

