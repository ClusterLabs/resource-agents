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

char *str_members(void)
{
	static char mbuf[MAXLINE];
	int i, len = 0;

	memset(mbuf, 0, MAXLINE);

	for (i = 0; i < cb_member_count; i++)
		len += sprintf(mbuf+len, "%d ", cb_members[i]);
	return mbuf;
}

int process_groupd(void)
{
	fd_t *fd;
	int error = -EINVAL;

	group_dispatch(gh);

	if (!cb_action)
		goto out;

	fd = find_domain(cb_name);
	if (!fd)
		goto out;

	/*
	log_debug("process %s global_id %d cb_action %d last_stop %d "
		  "last_start %d last_finish %d first %d prev_count %d",
		  fd->name, fd->global_id, cb_action,
		  fd->last_stop, fd->last_start, fd->last_finish,
		  fd->first_recovery, fd->prev_count);
	*/

	switch (cb_action) {
	case DO_STOP:
		log_debug("stop %s", cb_name);
		fd->last_stop = fd->last_start;
		break;
	case DO_START:
		log_debug("start %s members %s", cb_name, str_members());
		fd->last_start = cb_event_nr;
		do_recovery(fd, cb_type, cb_member_count, cb_members);
		group_done(gh, cb_name, cb_event_nr);
		break;
	case DO_FINISH:
		log_debug("finish %s", cb_name);
		fd->last_finish = cb_event_nr;
		do_recovery_done(fd);
		break;
	case DO_TERMINATE:
		log_debug("terminate %s", cb_name);
		/* if leaving */
		list_del(&fd->list);
		free(fd);
		break;
	case DO_SETID:
		log_debug("setid %s %d", cb_name, cb_id);
		fd->global_id = cb_id;
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

