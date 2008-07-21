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

int set_mountgroup_info_group(struct mountgroup *mg, struct gfsc_mountgroup *out)
{
	return 0;
}

int set_node_info_group(struct mountgroup *mg, int nodeid, struct gfsc_node *node)
{
	return 0;
}

int set_mountgroups_group(int *count, struct gfsc_mountgroup **mgs_out)
{
	return 0;
}

int set_mountgroup_nodes_group(struct mountgroup *mg, int option, int *node_count,
			       struct gfsc_node **nodes_out)
{
	return 0;
}

