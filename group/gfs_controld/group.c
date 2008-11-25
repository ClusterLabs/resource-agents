#include "gfs_daemon.h"
#include "config.h"
#include "cpg-old.h"
#include "libgroup.h"

#define LOCK_DLM_GROUP_LEVEL    2
#define LOCK_DLM_GROUP_NAME     "gfs"

/* save all the params from callback functions here because we can't
   do the processing within the callback function itself */

group_handle_t gh;
static int cb_action;
static char cb_name[GFS_MOUNTGROUP_LEN+1];
static int cb_event_nr;
static unsigned int cb_id;
static int cb_type;
static int cb_member_count;
static int cb_members[MAX_NODES];


static void stop_cbfn(group_handle_t h, void *private, char *name)
{
	cb_action = DO_STOP;
	strcpy(cb_name, name);
}

static void start_cbfn(group_handle_t h, void *private, char *name,
		       int event_nr, int type, int member_count, int *members)
{
	int i;

	cb_action = DO_START;
	strncpy(cb_name, name, GFS_MOUNTGROUP_LEN);
	cb_event_nr = event_nr;
	cb_type = type;
	cb_member_count = member_count;

	for (i = 0; i < member_count; i++)
		cb_members[i] = members[i];
}

static void finish_cbfn(group_handle_t h, void *private, char *name,
			int event_nr)
{
	cb_action = DO_FINISH;
	strncpy(cb_name, name, GFS_MOUNTGROUP_LEN);
	cb_event_nr = event_nr;
}

static void terminate_cbfn(group_handle_t h, void *private, char *name)
{
	cb_action = DO_TERMINATE;
	strncpy(cb_name, name, GFS_MOUNTGROUP_LEN);
}

static void setid_cbfn(group_handle_t h, void *private, char *name,
		       unsigned int id)
{
	cb_action = DO_SETID;
	strncpy(cb_name, name, GFS_MOUNTGROUP_LEN);
	cb_id = id;
}

static group_callbacks_t callbacks = {
	stop_cbfn,
	start_cbfn,
	finish_cbfn,
	terminate_cbfn,
	setid_cbfn,
};

static char *str_members(void)
{
	static char str_members_buf[MAXLINE];
	int i, ret, pos = 0, len = MAXLINE;

	memset(str_members_buf, 0, MAXLINE);

	for (i = 0; i < cb_member_count; i++) {
		if (i != 0) {
			ret = snprintf(str_members_buf + pos, len - pos, " ");
			if (ret >= len - pos)
				break;
			pos += ret;
		}
		ret = snprintf(str_members_buf + pos, len - pos, "%d",
			       cb_members[i]);
		if (ret >= len - pos)
			break;
		pos += ret;
	}
	return str_members_buf;
}

void process_groupd(int ci)
{
	struct mountgroup *mg;
	int error = 0;

	error = group_dispatch(gh);
	if (error) {
		log_error("groupd_dispatch error %d errno %d", error, errno);
		goto out;
	}

	if (!cb_action)
		goto out;

	mg = find_mg(cb_name);
	if (!mg) {
		log_error("callback %d group %s not found", cb_action, cb_name);
		error = -1;
		goto out;
	}

	switch (cb_action) {
	case DO_STOP:
		log_debug("groupd cb: stop %s", cb_name);
		mg->last_callback = DO_STOP;
		mg->last_stop = mg->last_start;
		do_stop(mg);
		break;

	case DO_START:
		log_debug("groupd cb: start %s type %d count %d members %s",
			  cb_name, cb_type, cb_member_count, str_members());
		mg->last_callback = DO_START;
		mg->last_start = cb_event_nr;
		do_start(mg, cb_type, cb_member_count, cb_members);
		break;

	case DO_FINISH:
		log_debug("groupd cb: finish %s", cb_name);
		mg->last_callback = DO_FINISH;
		mg->last_finish = cb_event_nr;
		do_finish(mg);
		break;

	case DO_TERMINATE:
		log_debug("groupd cb: terminate %s", cb_name);
		mg->last_callback = DO_TERMINATE;
		do_terminate(mg);
		break;

	case DO_SETID:
		log_debug("groupd cb: set_id %s %x", cb_name, cb_id);
		mg->id = cb_id;
		break;

	default:
		error = -EINVAL;
	}

 out:
	cb_action = 0;
}

int setup_groupd(void)
{
	int rv;

	gh = group_init(NULL, LOCK_DLM_GROUP_NAME, LOCK_DLM_GROUP_LEVEL,
			&callbacks, 10);
	if (!gh) {
		log_error("group_init error %p %d", gh, errno);
		return -ENOTCONN;
	}

	rv = group_get_fd(gh);
	if (rv < 0)
		log_error("group_get_fd error %d %d", rv, errno);

	log_debug("groupd %d", rv);

	return rv;
}

void close_groupd(void)
{
	group_exit(gh);
}

/* most of the query info doesn't apply in the LIBGROUP mode, but we can
   emulate some basic parts of it */

int set_mountgroup_info_group(struct mountgroup *mg,
			      struct gfsc_mountgroup *out)
{
	strncpy(out->name, mg->name, GFS_MOUNTGROUP_LEN);
	out->global_id = mg->id;

	if (mg->joining)
		out->flags |= GFSC_MF_JOINING;
	if (mg->leaving)
		out->flags |= GFSC_MF_LEAVING;
	if (mg->kernel_stopped)
		out->flags |= GFSC_MF_KERNEL_STOPPED;

	out->cg_prev.member_count = mg->memb_count;

	return 0;
}

static int _set_node_info(struct mountgroup *mg, int nodeid,
			  struct gfsc_node *node)
{
	struct mg_member *memb;
	int is_member = 0, is_gone = 0;

	list_for_each_entry(memb, &mg->members, list) {
		if (memb->nodeid != nodeid)
			continue;
		is_member = 1;
		goto found;
	}
	list_for_each_entry(memb, &mg->members_gone, list) {
		if (memb->nodeid != nodeid)
			continue;
		is_gone = 1;
		break;
	}
	if (!is_member && !is_gone)
		goto out;
 found:
	node->nodeid = nodeid;

	if (is_member)
		node->flags |= GFSC_NF_MEMBER;
	if (memb->spectator)
		node->flags |= GFSC_NF_SPECTATOR;
	if (memb->readonly)
		node->flags |= GFSC_NF_READONLY;
	if (memb->ms_kernel_mount_done)
		node->flags |= GFSC_NF_KERNEL_MOUNT_DONE;
	if (memb->ms_kernel_mount_error)
		node->flags |= GFSC_NF_KERNEL_MOUNT_ERROR;

	node->jid = memb->jid;

	if (is_gone && memb->gone_type == GROUP_NODE_FAILED)
		node->failed_reason = 1;
 out:
	return 0;
}

int set_node_info_group(struct mountgroup *mg, int nodeid,
			struct gfsc_node *node)
{
	return _set_node_info(mg, nodeid, node);
}

int set_mountgroups_group(int *count, struct gfsc_mountgroup **mgs_out)
{
	struct mountgroup *mg;
	struct gfsc_mountgroup *mgs, *mgp;
	int mg_count = 0;

	list_for_each_entry(mg, &mountgroups, list)
		mg_count++;

	mgs = malloc(mg_count * sizeof(struct gfsc_mountgroup));
	if (!mgs)
		return -ENOMEM;
	memset(mgs, 0, mg_count * sizeof(struct gfsc_mountgroup));

	mgp = mgs;
	list_for_each_entry(mg, &mountgroups, list) {
		set_mountgroup_info(mg, mgp++);
	}

	*count = mg_count;
	*mgs_out = mgs;
	return 0;
}

int list_count(struct list_head *head)
{
	struct list_head *tmp;
	int count = 0;

	list_for_each(tmp, head)
		count++;
	return count;
}

int set_mountgroup_nodes_group(struct mountgroup *mg, int option,
			       int *node_count, struct gfsc_node **nodes_out)
{
	struct gfsc_node *nodes = NULL, *nodep;
	struct mg_member *memb;
	int count = 0;

	if (option == GFSC_NODES_ALL) {
		count = mg->memb_count + list_count(&mg->members_gone);
	} else if (option == GFSC_NODES_MEMBERS) {
		count = mg->memb_count;
	} else
		goto out;

	nodes = malloc(count * sizeof(struct gfsc_node));
	if (!nodes)
		return -ENOMEM;
	memset(nodes, 0, count * sizeof(struct gfsc_node));
	nodep = nodes;

	list_for_each_entry(memb, &mg->members, list)
		_set_node_info(mg, memb->nodeid, nodep++);

	if (option == GFSC_NODES_ALL) {
		list_for_each_entry(memb, &mg->members_gone, list)
			_set_node_info(mg, memb->nodeid, nodep++);
	}
 out:
	*node_count = count;
	*nodes_out = nodes;
	return 0;
}

int set_group_mode(void)
{
	int i = 0, rv, version, limit;

	while (1) {
		rv = group_get_version(&version);

		if (rv || version < 0) {
			/* we expect to get version of -EAGAIN while groupd
			   is detecting the mode of everyone; don't retry
			   as long if we're not getting anything back from
			   groupd */

			log_debug("set_group_mode get_version %d ver %d",
				  rv, version);

			limit = (version == -EAGAIN) ? 30 : 5;

			if (i++ > limit) {
				log_error("cannot get groupd compatibility "
					  "mode rv %d ver %d", rv, version);
				return -1;
			}
			sleep(1);
			continue;
		}


		if (version == GROUP_LIBGROUP) {
			group_mode = GROUP_LIBGROUP;
			return 0;
		} else if (version == GROUP_LIBCPG) {
			group_mode = GROUP_LIBCPG;
			return 0;
		} else {
			log_error("set_group_mode invalid ver %d", version);
			return -1;
		}
	}
}

