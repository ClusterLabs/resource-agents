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

#define DO_STOP 1
#define DO_START 2
#define DO_FINISH 3
#define DO_TERMINATE 4
#define DO_SETID 5

extern int joining;

/* save all the params from callback functions here because we can't
   do the processing within the callback function itself */

group_handle_t gh;
static int cb_action;
static char cb_name[MAX_GROUP_NAME_LEN+1];
static int cb_event_nr;
static int cb_id;
static int cb_type;
static int cb_member_count;
static int cb_members[MAX_GROUP_MEMBERS];


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

char *str_members(void)
{
	static char str_members_buf[MAXLINE];
	int i, len = 0;

	memset(str_members_buf, 0, MAXLINE);

	for (i = 0; i < cb_member_count; i++) {
		if (i != 0)
			len += sprintf(str_members_buf+len, " ");
		len += sprintf(str_members_buf+len, "%d", cb_members[i]);
	}
	return str_members_buf;
}

int process_groupd(void)
{
	char str_id[32], *str_memb;
	char *argv[2];
	int error = 0;

	group_dispatch(gh);

	if (!cb_action)
		goto out;

	switch (cb_action) {

	case DO_STOP:
		log_debug("stop %s", cb_name);

		argv[0] = cb_name;
		argv[1] = "0";
		ls_control(2, argv);

		group_stop_done(gh, cb_name);

		break;

	case DO_START:
		str_memb = str_members();
		log_debug("start %s \"%s\"", cb_name, str_memb);

		argv[0] = cb_name;
		argv[1] = str_memb;
		ls_members(2, argv);

		argv[0] = cb_name;
		argv[1] = "1";
		ls_control(2, argv);

		group_start_done(gh, cb_name, cb_event_nr);

		if (!joining)
			break;
		joining = 0;
		log_debug("join event done %s", cb_name);
		argv[0] = cb_name;
		argv[1] = "0";
		ls_event_done(2, argv);

		break;

	case DO_SETID:
		log_debug("set id %s %d", cb_name, cb_id);
		memset(str_id, 0, sizeof(str_id));
		sprintf(str_id, "%d", cb_id);

		argv[0] = cb_name;
		argv[1] = str_id;
		ls_set_id(2, argv);

		break;

	case DO_TERMINATE:
		log_debug("terminate %s", cb_name);

		argv[0] = cb_name;

		if (joining) {
			argv[1] = "-1";
			joining = 0;
			log_debug("join event failed %s", cb_name);
		} else {
			argv[1] = "0";
			log_debug("leave event done %s", cb_name);
		}
		ls_event_done(2, argv);

		break;

	case DO_FINISH:
		log_debug("finish %s ignored", cb_name);
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
	if (rv < 0)
		log_error("group_get_fd error %d %d", rv, errno);

	return rv;
}

