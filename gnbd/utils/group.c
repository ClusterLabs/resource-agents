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

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include "gnbd_utils.h"

#include "group.h"

#define MAXLINE 256

/* save all the params from callback functions here because we can't
   do the processing within the callback function itself */

group_handle_t gh;
int cb_action;
char cb_name[MAX_GROUP_NAME_LEN+1];
int cb_event_nr;
int cb_id;
int cb_type;
int cb_member_count;
int cb_members[MAX_GROUP_MEMBERS];


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
		       int unsigned id)
{
	cb_action = DO_SETID;
	strcpy(cb_name, name);
	cb_id = id;
}

static group_callbacks_t callbacks = {
	stop_cbfn,
	start_cbfn,
	finish_cbfn,
	terminate_cbfn,
	setid_cbfn
};

static char *str_members(void)
{
	static char mbuf[MAXLINE];
	int i, len = 0;

	memset(mbuf, 0, MAXLINE);

	for (i = 0; i < cb_member_count; i++)
		len += sprintf(mbuf+len, "%d ", cb_members[i]);
	return mbuf;
}

int default_process_groupd(void)
{
	int error = -EINVAL;

	group_dispatch(gh);

	if (!cb_action)
		goto out;

	switch (cb_action) {
	case DO_STOP:
		log_msg("stop %s", cb_name);
		group_stop_done(gh, cb_name);
		break;
	case DO_START:
		log_msg("start %s %d members %s", cb_name, cb_event_nr,
			  str_members());
		group_start_done(gh, cb_name, cb_event_nr);
		break;
	case DO_FINISH:
		log_msg("finish %s %d", cb_name, cb_event_nr);
		break;
	case DO_TERMINATE:
		log_msg("terminate %s", cb_name);
		break;
	case DO_SETID:
		log_msg("setid %s %d", cb_name, cb_id);
		break;
	default:
		error = -EINVAL;
	}

	cb_action = 0;
 out:
	return error;
}

int setup_groupd(char *name)
{
	int rv;

	gh = group_init(NULL, name, 0, &callbacks);
	if (!gh) {
		log_err("group_init error %d %d", (int) gh, errno);
		return -ENOTCONN;
	}

	rv = group_get_fd(gh);
	if (rv < 0) {
		log_err("group_get_fd error %d %d", rv, errno);
	}

	return rv;
}

void exit_groupd(void)
{
	group_exit(gh);
}

