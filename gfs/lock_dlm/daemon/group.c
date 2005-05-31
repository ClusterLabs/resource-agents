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

#include "lock_dlm.h"

#define DO_STOP 1
#define DO_START 2
#define DO_FINISH 3
#define DO_TERMINATE 4
#define DO_SETID 5

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

int do_stop(struct mountgroup *mg);
int do_finish(struct mountgroup *mg);
int do_terminate(struct mountgroup *mg);
int do_start(struct mountgroup *mg, int type, int count, int *nodeids);
struct mountgroup *find_mg(char *name);


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
	static char buf[MAXLINE];
	int i, len = 0;

	memset(buf, 0, MAXLINE);

	for (i = 0; i < cb_member_count; i++)
		len += sprintf(buf+len, "%d ", cb_members[i]);
	return buf;
}

int process_groupd(void)
{
	struct mountgroup *mg;
	int error = 0;

	group_dispatch(gh);

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
		log_debug("stop %s", cb_name);
		mg->last_stop = mg->last_start;
		do_stop(mg);
		break;
	case DO_START:
		log_debug("start %s %s", cb_name, str_members());
		mg->last_start = cb_event_nr;
		do_start(mg, cb_type, cb_member_count, cb_members);
		break;
	case DO_FINISH:
		log_debug("finish %s", cb_name);
		mg->last_finish = cb_event_nr;
		do_finish(mg);
		break;
	case DO_TERMINATE:
		log_debug("terminate %s", cb_name);
		do_terminate(mg);
		list_del(&mg->list);
		free(mg);
		break;
	case DO_SETID:
		break;
	default:
		error = -EINVAL;
	}

 out:
	cb_action = 0;
	return error;
}

int setup_groupd(void)
{
	int rv;

	gh = group_init(NULL, GFS_GROUP_NAME, GFS_GROUP_LEVEL, &callbacks);
	if (!gh) {
		log_error("group_init error %d %d", (int) gh, errno);
		return -ENOTCONN;
	}

	rv = group_get_fd(gh);
	if (rv < 0)
		log_error("group_get_fd error %d %d", rv, errno);

	log_debug("groupd %d", rv);

	return rv;
}
