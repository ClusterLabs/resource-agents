#include "dlm_daemon.h"
#include "config.h"
#include "libgroup.h"

#define DO_STOP 1
#define DO_START 2
#define DO_FINISH 3
#define DO_TERMINATE 4
#define DO_SETID 5

#define GROUPD_TIMEOUT 10 /* seconds */

/* save all the params from callback functions here because we can't
   do the processing within the callback function itself */

group_handle_t gh;
static int cb_action;
static char cb_name[DLM_LOCKSPACE_LEN+1];
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
	strcpy(cb_name, name);
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
	strcpy(cb_name, name);
	cb_event_nr = event_nr;
}

static void terminate_cbfn(group_handle_t h, void *private, char *name)
{
	cb_action = DO_TERMINATE;
	strcpy(cb_name, name);
}

static void setid_cbfn(group_handle_t h, void *private, char *name,
		       unsigned int id)
{
	cb_action = DO_SETID;
	strcpy(cb_name, name);
	cb_id = id;
}

group_callbacks_t callbacks = {
	stop_cbfn,
	start_cbfn,
	finish_cbfn,
	terminate_cbfn,
	setid_cbfn
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
	struct lockspace *ls;
	int error = 0, val;

	group_dispatch(gh);

	if (!cb_action)
		goto out;

	ls = find_ls(cb_name);
	if (!ls) {
		log_error("callback %d group %s not found", cb_action, cb_name);
		error = -1;
		goto out;
	}

	switch (cb_action) {
	case DO_STOP:
		log_debug("groupd callback: stop %s", cb_name);
		set_sysfs_control(cb_name, 0);
		group_stop_done(gh, cb_name);
		break;

	case DO_START:
		log_debug("groupd callback: start %s count %d members %s",
			  cb_name, cb_member_count, str_members());

		set_configfs_members(cb_name, cb_member_count, cb_members,
				     0, NULL);

		/* this causes the dlm to do a "start" using the
		   members we just set */

		set_sysfs_control(cb_name, 1);

		/* the dlm doesn't need/use a "finish" stage following
		   start, so we can just do start_done immediately */

		group_start_done(gh, cb_name, cb_event_nr);

		if (!ls->joining)
			break;

		ls->joining = 0;
		log_debug("join event done %s", cb_name);

		/* this causes the dlm_new_lockspace() call (typically from
		   mount) to complete */
		set_sysfs_event_done(cb_name, 0);

		break;

	case DO_SETID:
		log_debug("groupd callback: set_id %s %x", cb_name, cb_id);
		set_sysfs_id(cb_name, cb_id);
		ls->global_id = cb_id;
		break;

	case DO_TERMINATE:
		log_debug("groupd callback: terminate %s", cb_name);

		if (ls->joining) {
			val = -1;
			ls->joining = 0;
			log_debug("join event failed %s", cb_name);
		} else {
			val = 0;
			log_debug("leave event done %s", cb_name);

			/* remove everything under configfs */
			set_configfs_members(ls->name, 0, NULL, 0, NULL);
		}

		set_sysfs_event_done(cb_name, val);
		list_del(&ls->list);
		free(ls);
		break;

	case DO_FINISH:
		log_debug("groupd callback: finish %s (unused)", cb_name);
		break;

	default:
		error = -EINVAL;
	}

	cb_action = 0;
 out:
	return;
}

int dlm_join_lockspace_group(struct lockspace *ls)
{
	int rv;

	ls->joining = 1;
	list_add(&ls->list, &lockspaces);

	rv = group_join(gh, ls->name);
	if (rv) {
		list_del(&ls->list);
		free(ls);
	}

	return rv;
}

int dlm_leave_lockspace_group(struct lockspace *ls)
{
	ls->leaving = 1;
	group_leave(gh, ls->name);
	return 0;
}

int setup_groupd(void)
{
	int rv;

	gh = group_init(NULL, "dlm", 1, &callbacks, GROUPD_TIMEOUT);
	if (!gh) {
		log_error("group_init error %p %d", gh, errno);
		return -ENOTCONN;
	}

	rv = group_get_fd(gh);
	if (rv < 0)
		log_error("group_get_fd error %d %d", rv, errno);

	return rv;
}

void close_groupd(void)
{
	group_exit(gh);
}

/* FIXME: most of the query info doesn't apply in the LIBGROUP mode,
   but we can emulate some basic parts of it */

int set_lockspace_info_group(struct lockspace *ls,
			     struct dlmc_lockspace *lockspace)
{
	return 0;
}

int set_node_info_group(struct lockspace *ls, int nodeid,
			struct dlmc_node *node)
{
	return 0;
}

int set_lockspaces_group(int *count, struct dlmc_lockspace **lss_out)
{
	return 0;
}

int set_lockspace_nodes_group(struct lockspace *ls, int option, int *node_count,
			      struct dlmc_node **nodes_out)
{
	return 0;
}

