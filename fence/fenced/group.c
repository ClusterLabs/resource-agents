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

#include "fd.h"

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

int process_groupd(fd_t *fd)
{
	int error = 0;

	group_dispatch(gh);

	if (!cb_action)
		goto out;

	switch (cb_action) {
	case DO_STOP:
		fd->last_stop = fd->last_start;
		break;
	case DO_START:
		fd->last_start = cb_event_nr;
		do_recovery(fd, cb_type, cb_member_count, cb_members);
		break;
	case DO_FINISH:
		fd->last_finish = cb_event_nr;
		do_recovery_done(fd);
		break;
	case DO_TERMINATE:
		/* if leaving */
		fd->leave_done = 1;
		break;
	case DO_SETID:
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

	gh = group_init(NULL, "fence", 0, &callbacks);
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

void exit_groupd(void)
{
	group_exit(gh);
}

