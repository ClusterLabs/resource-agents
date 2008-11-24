
/* Interface with corosync's closed-process-group (cpg) API */

#include "gd_internal.h"

static cpg_handle_t groupd_handle;
static struct cpg_name groupd_name;
static int global_id_counter = 0;
static int groupd_joined = 0;
static int groupd_ci;

static int			got_confchg;
static struct cpg_address	groupd_cpg_member[MAX_GROUP_MEMBERS];
static int			groupd_cpg_member_count;
static struct cpg_address	saved_member[MAX_GROUP_MEMBERS];
static struct cpg_address	saved_joined[MAX_GROUP_MEMBERS];
static struct cpg_address	saved_left[MAX_GROUP_MEMBERS];
static int			saved_member_count;
static int			saved_joined_count;
static int			saved_left_count;
static cpg_handle_t		saved_handle;
static struct cpg_name		saved_name;
static int			message_flow_control_on;
static struct list_head		group_nodes;
static uint64_t			send_version_first;

#define CLUSTER2		2
#define CLUSTER3		3

struct group_version {
	uint32_t nodeid;
	uint16_t cluster;
	uint16_t group_mode;
	uint16_t groupd_compat;
	uint16_t groupd_count;
	uint32_t unused;
};

struct group_node {
	uint32_t nodeid;
	uint32_t got_from;
	int got_version;
	uint64_t add_time;
	struct group_version ver;
	struct list_head list;
};

static void block_old_nodes(void);

static char *mode_str(int m)
{
	switch (m) {
	case GROUP_PENDING:
		return "PENDING";
	case GROUP_LIBGROUP:
		return "LIBGROUP";
	case GROUP_LIBCPG:
		return "LIBCPG";
	default:
		return "UNKNOWN";
	}
}

static struct group_node *get_group_node(int nodeid)
{
	struct group_node *node;

	list_for_each_entry(node, &group_nodes, list) {
		if (node->nodeid == nodeid)
			return node;
	}
	return NULL;
}

static void group_node_add(int nodeid)
{
	struct group_node *node;

	node = get_group_node(nodeid);
	if (node)
		return;

	node = malloc(sizeof(struct group_node));
	if (!node)
		return;
	memset(node, 0, sizeof(struct group_node));

	node->nodeid = nodeid;
	node->add_time = time(NULL);
	list_add_tail(&node->list, &group_nodes);
}

static void group_node_del(int nodeid)
{
	struct group_node *node;

	node = get_group_node(nodeid);
	if (!node) {
		log_print("group_node_del %d no node", nodeid);
		return;
	}

	list_del(&node->list);
	free(node);
}

static void version_copy_in(struct group_version *ver)
{
	ver->nodeid        = le32_to_cpu(ver->nodeid);
	ver->cluster       = le16_to_cpu(ver->cluster);
	ver->group_mode    = le16_to_cpu(ver->group_mode);
	ver->groupd_compat = le16_to_cpu(ver->groupd_compat);
	ver->groupd_count  = le16_to_cpu(ver->groupd_count);
}

static void _send_version(int nodeid, int cluster, int mode, int compat)
{
	group_t g, *gp;
	char *buf;
	msg_t *msg;
	int len;
	int count = 0;
	struct group_version *ver;

	list_for_each_entry(gp, &gd_groups, list)
		count++;

	/* just so log_group will work */
	memset(&g, 0, sizeof(group_t));
	strcpy(g.name, "groupd");

	len = sizeof(msg_t) + sizeof(struct group_version);

	buf = malloc(len);
	if (!buf)
		return;
	memset(buf, 0, len);

	msg = (msg_t *)buf;
	ver = (struct group_version *)(buf + sizeof(msg_t));

	msg->ms_type = MSG_GROUP_VERSION;
	msg_bswap_out(msg);

	log_debug("send_version nodeid %d cluster %d mode %s compat %d",
		  nodeid, cluster, mode_str(mode), compat);

	ver->nodeid        = cpu_to_le32(nodeid);
	ver->cluster       = cpu_to_le16(cluster);
	ver->group_mode    = cpu_to_le16(mode);
	ver->groupd_compat = cpu_to_le16(compat);
	ver->groupd_count  = cpu_to_le16(count);

	send_message_groupd(&g, buf, len, MSG_GROUP_VERSION);
}

static void send_version(void)
{
	_send_version(our_nodeid, CLUSTER3, group_mode, cfgd_groupd_compat);
}

static void set_group_mode(void)
{
	struct group_node *node;
	int need_version, pending_count;

	need_version = 0;
	pending_count = 0;

	list_for_each_entry(node, &group_nodes, list) {
		if (!node->got_version) {
			need_version++;
			continue;
		}
		if (node->ver.group_mode == GROUP_PENDING) {
			pending_count++;
			continue;
		}

		/* If we receive any non-pending group mode, adopt it
		   immediately. */

		group_mode = node->ver.group_mode;

		switch (group_mode) {
		case GROUP_PENDING:
			/* shouldn't happen */
			log_level(LOG_INFO, "groupd compatibility mode 2 ver");
			break;
		case GROUP_LIBGROUP:
			log_level(LOG_INFO, "groupd compatibility mode 1 ver");
			break;
		case GROUP_LIBCPG:
			log_level(LOG_INFO, "groupd compatibility mode 0 ver");
			break;
		default:
			log_level(LOG_INFO, "groupd compatibility mode %d ver",
				  group_mode);
			break;
		}

		log_debug("set_group_mode %s matching nodeid %d got_from %d",
			  mode_str(group_mode), node->nodeid, node->got_from);
		break;
	}

	if (group_mode == GROUP_LIBCPG)
		block_old_nodes();
}

static void receive_version(int from, msg_t *msg, int len)
{
	struct group_node *node;
	struct group_version *ver;

	if (group_mode != GROUP_PENDING)
		return;

	ver = (struct group_version *)((char *)msg + sizeof(msg_t));

	version_copy_in(ver);

	node = get_group_node(ver->nodeid);
	if (!node) {
		log_print("receive_version from %d nodeid %d not found",
			  from, ver->nodeid);
		return;
	}

	/* ignore a repeat of what we've seen before */

	if (node->got_version && from == node->got_from &&
	    node->ver.group_mode == ver->group_mode)
		return;

	log_debug("receive_version from %d nodeid %d cluster %d mode %s "
		  "compat %d", from, ver->nodeid, ver->cluster,
		  mode_str(ver->group_mode), ver->groupd_compat);

	node->got_version = 1;
	node->got_from = from;
	memcpy(&node->ver, ver, sizeof(struct group_version));

	set_group_mode();
}

void group_mode_check_timeout(void)
{
	struct group_node *node;
	int need_version, pending_count;
	uint64_t now;

	if (group_mode != GROUP_PENDING)
		return;

	if (!send_version_first)
		return;

	/* Wait for cfgd_groupd_wait seconds to receive a version message from
	   an added node, after which we'll send a version message for it,
	   calling it a cluster2 node; receiving this will cause everyone to
	   immediately set mode to LIBGROUP. */

	need_version = 0;
	pending_count = 0;
	now = time(NULL);

	list_for_each_entry(node, &group_nodes, list) {
		if (node->got_version) {
			pending_count++;
			continue;
		}
		need_version++;

		if (now - node->add_time >= cfgd_groupd_wait) {
			log_print("send version for nodeid %d times %llu %llu",
				  node->nodeid,
				  (unsigned long long)node->add_time,
				  (unsigned long long)now);
			_send_version(node->nodeid, CLUSTER2, GROUP_LIBGROUP,1);
		}
	}

	if (need_version) {
		log_debug("group_mode_check_timeout need %d pending %d",
			  need_version, pending_count);
		return;
	}

	/* we have a version from everyone, and they all are pending;
	   wait for cfgd_groupd_mode_delay to give any old cluster2 nodes
	   a chance to join and cause us to use LIBGROUP */

	if (now - send_version_first < cfgd_groupd_mode_delay) {
		log_debug("group_mode_check_timeout delay times %llu %llu",
			  (unsigned long long)send_version_first,
			  (unsigned long long)now);
		return;
	}

	/* everyone is cluster3/pending so we can use LIBCPG; receiving
	   this will cause everyone to immediately set mode to LIBCPG */

	log_debug("send version LIBCPG all %d pending", pending_count);

	_send_version(our_nodeid, CLUSTER3, GROUP_LIBCPG, cfgd_groupd_compat);
}

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

	node = find_group_node(g, nodeid);
	if (!node)
		return;

	log_group(g, "process_node_down %d", nodeid);

	list_del(&node->list);
	g->memb_count--;
	free(node);

	log_group(g, "cpg del node %d total %d - down",
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
			node = new_node(saved_member[i].nodeid);
			list_add_tail(&node->list, &g->memb);
			g->memb_count++;
			log_group(g, "cpg add node %d total %d",
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
		log_group(g, "cpg add node %d total %d",
			  node->nodeid, g->memb_count);
	}

	queue_app_join(g, nodeid);

	/* if this is for our own join, then make it current immediately;
	   other code gets confused if we're not joined and have no current
	   event */
	if (nodeid == our_nodeid)
		process_app(g);
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

	log_group(g, "cpg del node %d total %d", nodeid, g->memb_count);

	queue_app_leave(g, nodeid);
}

static uint32_t max_global_id(uint32_t add_nodeid)
{
	group_t *g;
	uint32_t nodeid, counter, max_counter = 0, max_gid = 0;

	list_for_each_entry(g, &gd_groups, list) {
		nodeid = g->global_id & 0x0000FFFF;
		counter = (g->global_id >> 16) & 0x0000FFFF;
		if (nodeid != add_nodeid)
			continue;
		if (!max_counter || counter > max_counter) {
			max_counter = counter;
			max_gid = g->global_id;
		}
	}
	return max_gid;
}

static int send_gid(uint32_t gid)
{
	group_t g;
	msg_t msg;

	/* just so log_group will work */
	memset(&g, 0, sizeof(group_t));
	strcpy(g.name, "groupd");

	memset(&msg, 0, sizeof(msg));
	msg.ms_type = MSG_GLOBAL_ID;
	msg.ms_global_id = gid;

	msg_bswap_out(&msg);

	return send_message_groupd(&g, &msg, sizeof(msg), MSG_GLOBAL_ID);
}

void process_groupd_confchg(void)
{
	group_t *g;
	struct recovery_set *rs;
	int i, found = 0;
	uint32_t gid;

	log_debug("groupd confchg total %d left %d joined %d",
		  saved_member_count, saved_left_count, saved_joined_count);

	if (!send_version_first) {
		for (i = 0; i < saved_member_count; i++) {
			group_node_add(saved_member[i].nodeid);
			log_debug("groupd init %d", saved_member[i].nodeid);
		}

		send_version_first = time(NULL);
	} else {
		for (i = 0; i < saved_left_count; i++) {
			group_node_del(saved_left[i].nodeid);
			log_debug("groupd del %d", saved_left[i].nodeid);
		}
		for (i = 0; i < saved_joined_count; i++) {
			group_node_add(saved_joined[i].nodeid);
			log_debug("groupd add %d", saved_joined[i].nodeid);
		}
	}

	if (saved_joined_count)
		send_version();

	memcpy(&groupd_cpg_member, &saved_member, sizeof(saved_member));
	groupd_cpg_member_count = saved_member_count;

	if (group_mode != GROUP_LIBGROUP)
		return;

	for (i = 0; i < saved_member_count; i++) {
		if (saved_member[i].nodeid == our_nodeid &&
		    saved_member[i].pid == (uint32_t) getpid()) {
			found = 1;
		}
	}

	if (!groupd_joined)
		goto next;

	/* find any groups that were created in the past by a new node
	   and send it the id it used so it can initialize global_id_counter
	   to avoid creating a new group with a duplicate id */

	for (i = 0; i < saved_joined_count; i++) {
		gid = max_global_id(saved_joined[i].nodeid);
		if (!gid)
			continue;
		log_debug("joined node %d had old max gid %x",
			  saved_joined[i].nodeid, gid);
		send_gid(gid);
	}

 next:
	if (found)
		groupd_joined = 1;
	else
		log_print("we are not in groupd confchg: %u %u",
			  our_nodeid, (uint32_t) getpid());

	for (i = 0; i < saved_left_count; i++) {
		if (saved_left[i].reason == CPG_REASON_LEAVE)
			continue;

		if (saved_left[i].reason == CPG_REASON_NODEDOWN) {
			/* a nice clean failure */
			add_recovery_set_cpg(saved_left[i].nodeid, 0);
		} else if (saved_left[i].reason == CPG_REASON_PROCDOWN) {
			/* groupd failed, but the node is still up; if
			   the node was in any groups (non-NULL rs is
			   returned), then kill the node so it'll be a
			   real nodedown */
			rs = add_recovery_set_cpg(saved_left[i].nodeid, 1);
			if (rs) {
				log_print("kill node %d - groupd PROCDOWN",
					  saved_left[i].nodeid);
				kill_cman(saved_left[i].nodeid);
			}
		}
		groupd_down(saved_left[i].nodeid);
	}

	/* we call process_node_down from here, instead of from the other cpg
	   confchg's because we want everyone to see the same order of
	   confchg's with respect to messages.  see bz 258121 */

	for (i = 0; i < saved_left_count; i++) {
		if (saved_left[i].reason == CPG_REASON_NODEDOWN ||
		    saved_left[i].reason == CPG_REASON_PROCDOWN) {
			list_for_each_entry(g, &gd_groups, list)
				process_node_down(g, saved_left[i].nodeid);
		}
	}
}

void copy_groupd_data(group_data_t *data)
{
	int i;

	data->level = -1;
	data->member_count = groupd_cpg_member_count;
	for (i = 0; i < groupd_cpg_member_count; i++)
		data->members[i] = groupd_cpg_member[i].nodeid;
}

int in_groupd_cpg(int nodeid)
{
	int i;
	for (i = 0; i < groupd_cpg_member_count; i++) {
		if (nodeid == groupd_cpg_member[i].nodeid)
			return 1;
	}
	return 0;
}

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
	uint32_t to_nodeid, counter;
	int len;

	memset(&name, 0, sizeof(name));

	msg_bswap_in(msg);

	if (msg->ms_type == MSG_GROUP_VERSION) {
		receive_version(nodeid, msg, data_len);
		return;
	}

	if (msg->ms_type == MSG_GLOBAL_ID) {
		to_nodeid = msg->ms_global_id & 0x0000FFFF;
		counter = (msg->ms_global_id >> 16) & 0x0000FFFF;

		if (to_nodeid == our_nodeid) {
			log_debug("recv global_id %x from %u cur counter %u",
			  	  msg->ms_global_id, nodeid, global_id_counter);
			if (counter > global_id_counter)
				global_id_counter = counter;
		}
		return;
	}

	if (handle == groupd_handle) {
		memcpy(&name, &msg->ms_name, MAX_NAMELEN);

		g = find_group_level(name, msg->ms_level);
		if (!g) {
			if (daemon_debug_verbose > 1) {
				log_print("%d:%s RECV len %d %s from %d, "
					  "no group",
				  	  msg->ms_level, name, data_len,
				  	  msg_type(msg->ms_type), nodeid);
			}
			return;
		}
	} else {
		if (group_mode != GROUP_LIBGROUP)
			return;

		g = find_group_by_handle(handle);
		if (!g) {
			len = group_name->length;
			if (len > MAX_NAMELEN)
				len = MAX_NAMELEN;
			memcpy(&name, &group_name->value, len);

			log_print("deliver_cb no group handle %llx name %s",
				  (unsigned long long)handle, name);
			return;
		}
	}

	if (daemon_debug_verbose > 1)
		log_group(g, "RECV len %d %s from %d", data_len,
			  msg_type(msg->ms_type), nodeid);

	save = malloc(sizeof(struct save_msg));
	// FIXME: handle failed malloc
	memset(save, 0, sizeof(struct save_msg));
	save->nodeid = nodeid;
	save->msg_len = data_len;

	if (data_len > sizeof(msg_t)) {
		buf = malloc(data_len);
		// FIXME: handle failed malloc
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

	if (group_mode != GROUP_LIBGROUP)
		return;

	g = find_group_by_handle(saved_handle);
	if (!g) {
		log_debug("confchg: no group for handle %llx name %s",
			  (unsigned long long)saved_handle,
			  saved_name.value);
		return;
	}

	log_group(g, "confchg left %d joined %d total %d",
		  saved_left_count, saved_joined_count, saved_member_count);

	for (i = 0; i < saved_joined_count; i++)
		process_node_join(g, saved_joined[i].nodeid);

	for (i = 0; i < saved_left_count; i++) {
		log_group(g, "confchg removed node %d reason %d",
			  saved_left[i].nodeid, saved_left[i].reason);

		switch (saved_left[i].reason) {
		case CPG_REASON_LEAVE:
			process_node_leave(g, saved_left[i].nodeid);
			break;
		case CPG_REASON_NODEDOWN:
		case CPG_REASON_PROCDOWN:
			/* process_node_down(g, saved_left[i].nodeid); */
			purge_node_messages(g, saved_left[i].nodeid);
			break;
		default:
			log_error(g, "unknown leave reason %d node %d",
				  saved_left[i].reason,
				  saved_joined[i].nodeid);
		}
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

	/*
	log_debug("%d:%s confchg_cb total %d left %d joined %d", level, name,
		  member_list_entries, left_list_entries, joined_list_entries);
	*/

	saved_handle = handle;

	if (left_list_entries > MAX_GROUP_MEMBERS) {
		log_debug("left_list_entries %d", left_list_entries);
		left_list_entries = MAX_GROUP_MEMBERS;
	}
	if (joined_list_entries > MAX_GROUP_MEMBERS) {
		log_debug("joined_list_entries %d", joined_list_entries);
		joined_list_entries = MAX_GROUP_MEMBERS;
	}
	if (member_list_entries > MAX_GROUP_MEMBERS) {
		log_debug("member_list_entries %d", joined_list_entries);
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
	group_t *g = NULL;
	cpg_error_t error;
	cpg_handle_t handle;
	int found = 0;
	cpg_flow_control_state_t flow_control_state;

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

	error = cpg_flow_control_state_get(handle, &flow_control_state);
	if (error != CPG_OK)
		log_error(g, "cpg_flow_control_state_get %d", error);
	else if (flow_control_state == CPG_FLOW_CONTROL_ENABLED) {
		message_flow_control_on = 1;
		log_debug("flow control on");
	} else {
		if (message_flow_control_on)
			log_debug("flow control off");
		message_flow_control_on = 0;
	}

	if (got_confchg)
		process_confchg();
}

int setup_cpg(void)
{
	cpg_error_t error;
	int fd;

	INIT_LIST_HEAD(&group_nodes);

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

 retry:
	error = cpg_join(groupd_handle, &groupd_name);
	if (error == CPG_ERR_TRY_AGAIN) {
		log_debug("setup_cpg cpg_join retry");
		sleep(1);
		goto retry;
	}
	if (error != CPG_OK) {
		log_print("cpg_join error %d", error);
		cpg_finalize(groupd_handle);
		return error;
	}

	log_debug("setup_cpg groupd_handle %llx",
		  (unsigned long long)groupd_handle);

	return 0;
}

int do_cpg_join(group_t *g)
{
	cpg_error_t error;
	cpg_handle_t h;
	struct cpg_name name;
	int fd, ci, i = 0;

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

	log_group(g, "is cpg client %d name %s handle %llx", ci, name.value,
		  (unsigned long long)h);

 retry:
	error = cpg_join(h, &name);
	if (error == CPG_ERR_TRY_AGAIN) {
		log_debug("cpg_join error retry");
		sleep(1);
		if (!(++i % 10))
			log_error(g, "cpg_join error retrying");
		goto retry;
	}
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
	int i = 0;

	memset(&name, 0, sizeof(name));
	sprintf(name.value, "%d_%s", g->level, g->name);
	name.length = strlen(name.value) + 1;

 retry:
	error = cpg_leave(g->cpg_handle, &name);
	if (error == CPG_ERR_TRY_AGAIN) {
		log_debug("cpg_leave error retry");
		sleep(1);
		if (!(++i % 10))
			log_error(g, "cpg_leave error retrying");
		goto retry;
	}
	if (error != CPG_OK) {
		log_group(g, "cpg_leave error %d", error);
		return error;
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
	if (error == CPG_ERR_TRY_AGAIN) {
		retries++;
		usleep(1000);
		if (!(retries % 100))
			log_error(g, "cpg_mcast_joined retry %d", retries);
		goto retry;
	} else if (error != CPG_OK)
		log_error(g, "cpg_mcast_joined error %d handle %llx", error,
			  (unsigned long long)h);

	if (retries)
		log_group(g, "cpg_mcast_joined retried %d", retries);

	return 0;
}

int send_message_groupd(group_t *g, void *buf, int len, int type)
{
	if (daemon_debug_verbose > 1)
		log_group(g, "SEND len %d %s", len, msg_type(type));

	return _send_message(groupd_handle, g, buf, len);
}

int send_message(group_t *g, void *buf, int len)
{
	return _send_message(g->cpg_handle, g, buf, len);
}

static void block_old_group(char *name, int level)
{
	group_t *g;
	app_t *a;
	cpg_error_t error;
	cpg_handle_t h;
	struct cpg_name cpgname;
	int rv, fd, ci, i = 0;

	rv = create_group(name, level, &g);
	if (rv)
		return;
	a = create_app(g);
	if (!a)
		return;

	error = cpg_initialize(&h, &callbacks);
	if (error != CPG_OK) {
		log_print("cpg_initialize error %d", error);
		return;
	}

	cpg_fd_get(h, &fd);

	ci = client_add(fd, process_cpg, NULL);

	g->cpg_client = ci;
	g->cpg_handle = h;
	g->cpg_fd = fd;
	g->joining = 1;
	a->client = ci;

	memset(&cpgname, 0, sizeof(cpgname));
	sprintf(cpgname.value, "%d_%s", level, name);
	cpgname.length = strlen(cpgname.value) + 1;

 retry:
	error = cpg_join(h, &cpgname);
	if (error == CPG_ERR_TRY_AGAIN) {
		log_debug("cpg_join error retry");
		sleep(1);
		if (!(++i % 10))
			log_print("cpg_join error retrying");
		goto retry;
	}
	if (error != CPG_OK) {
		log_print("cpg_join error %d", error);
		cpg_finalize(h);
		return;
	}
}

/* Problem: GROUP_LIBCPG is selected during version detection, then
   an old cluster2 node starts (people aren't supposed to do this, but it may
   happen, so it's nice to do what we can to address it).  groupd on the old
   cluster2 node, using libgroup, will allow new groups to be formed on it.
   Solution is a hack: when the cluster3 nodes select LIBCPG mode, they also
   create unused/placeholder cpg's with the names of old known cluster2 groups,
   which blocks them being fully joined by old groupd's that may come along. */

static void block_old_nodes(void)
{
	block_old_group("default", 0);
	block_old_group("clvmd", 1);
	block_old_group("rgmanager", 1);
}

