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

#include "dlm_daemon.h"
#include "libgroup.h"

#define DO_STOP 1
#define DO_START 2
#define DO_FINISH 3
#define DO_TERMINATE 4
#define DO_SETID 5

/* save all the params from callback functions here because we can't
   do the processing within the callback function itself */

static group_handle_t gh;
static int cb_action;
static char cb_name[MAX_GROUP_NAME_LEN+1];
static int cb_event_nr;
static int cb_id;
static int cb_type;
static int cb_member_count;
static int cb_members[MAX_GROUP_MEMBERS];
static char buf[MAXLINE];


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

static void setid_cbfn(group_handle_t h, void *private, char *name, int id)
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

static void build_args(int action, int *argc, char **argv)
{
	int len, i;

	switch (action) {
	case DO_STOP:
	case DO_TERMINATE:
		*argc = 1;
		argv[0] = cb_name;
		break;

	case DO_FINISH:
		sprintf(buf, "%d", cb_event_nr);
		*argc = 2;
		argv[0] = cb_name;
		argv[1] = buf;
		break;

	case DO_SETID:
		sprintf(buf, "%d", cb_id);
		*argc = 2;
		argv[0] = cb_name;
		argv[1] = buf;
		break;

	case DO_START:
		/* first build one string with all args, then make
		   argv pointers using make_args */
		len = snprintf(buf, sizeof(buf), "%s %d %d",
			       cb_name, cb_event_nr, cb_type);
		for (i = 0; i < cb_member_count; i++)
			len += sprintf(buf+len, " %d", cb_members[i]);
		make_args(buf, argc, argv, ' ');
	}
}

int process_groupd(void)
{
	char *argv[MAXARGS];
	int argc = 0, error = 0;

	group_dispatch(gh);

	if (!cb_action)
		goto out;

	memset(buf, 0, sizeof(buf));
	build_args(cb_action, &argc, argv);

	log_debug("ls action %d name %s", cb_action, argv[1]);

	switch (cb_action) {
	case DO_STOP:
		ls_stop(argc, argv);
		break;
	case DO_START:
		ls_start(argc, argv);
		break;
	case DO_FINISH:
		ls_finish(argc, argv);
		break;
	case DO_TERMINATE:
		ls_terminate(argc, argv);
		break;
	case DO_SETID:
		ls_set_id(argc, argv);
		break;
	default:
		error = -EINVAL;
	}

	cb_action = 0;
 out:
	return error;
}

int setup_groupd(void)
{
	int rv;

	gh = group_init(NULL, "dlm", 1, &callbacks);
	if (!gh) {
		log_error("group_init error %d %d", (int) gh, errno);
		return -ENOTCONN;
	}

	rv = group_get_fd(gh);
	if (rv < 0) {
		log_error("group_get_fd error %d %d", rv, errno);
	}

	return rv;
}

