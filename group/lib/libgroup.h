/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This library is free software; you can redistribute it and/or
**  modify it under the terms of the GNU Lesser General Public
**  License as published by the Free Software Foundation; either
**  version 2 of the License, or (at your option) any later version.
**
**  This library is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
**  Lesser General Public License for more details.
**
**  You should have received a copy of the GNU Lesser General Public
**  License along with this library; if not, write to the Free Software
**  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
**
*******************************************************************************
******************************************************************************/

#ifndef _LIBGROUP_H_
#define _LIBGROUP_H_

#define MAX_GROUP_MEMBERS	256
#define MAX_GROUP_NAME_LEN	32
#define GROUP_INFO_LEN		32

/* these need to match what's in groupd.h */
#define GROUP_NODE_FAILED	1
#define GROUP_NODE_JOIN		2
#define GROUP_NODE_LEAVE	3

typedef void *group_handle_t;

typedef void (*group_stop_t)(group_handle_t h, void *private, char *name);
typedef void (*group_start_t)(group_handle_t h, void *private, char *name,
			      int event_nr, int type, int member_count,
			      int *members);
typedef void (*group_finish_t)(group_handle_t h, void *private, char *name,
			       int event_nr);
typedef void (*group_terminate_t)(group_handle_t h, void *private, char *name);
typedef void (*group_set_id_t)(group_handle_t h, void *private, char *name,
			       int id);

typedef struct {
	group_stop_t stop;
	group_start_t start;
	group_finish_t finish;
	group_terminate_t terminate;
	group_set_id_t set_id;
} group_callbacks_t;

group_handle_t group_init(void *private, char *prog_name, int level, group_callbacks_t *cbs);
int group_exit(group_handle_t handle);

/* When joining or leaving a group, the program can provide an info string
   that other members can access via group_join_info() and group_leave_info().
   The string can be at most GROUP_INFO_LEN bytes, including the terminating
   '\0' character.  These strings are analagous to mount options and can
   allow some simple applications to get by without any explicit send/receive
   communication. */

int group_join(group_handle_t handle, char *name, char *info);
int group_leave(group_handle_t handle, char *name, char *info);
int group_stop_done(group_handle_t handle, char *name);
int group_start_done(group_handle_t handle, char *name, int event_nr);
int group_get_fd(group_handle_t handle);
int group_dispatch(group_handle_t handle);

/*
int group_send();
int group_receive();
int group_count_groups(void);
*/

typedef struct group_data {
	char client_name[32+1];
	char name[MAX_GROUP_NAME_LEN+1];
	int level;
	int id;
	int member;
	int member_count;
	int members[MAX_GROUP_MEMBERS];
	int event_state;	/* debugging */
	int update_state;	/* debugging */
	int recover_state;	/* debugging */
	int flags;		/* debugging */
} group_data_t;

/* These routines create their own temporary connection to groupd so they
   don't interfere with dispatchable callback messages. */

int group_get_groups(int max, int *count, group_data_t *data);
int group_get_group(int level, char *name, group_data_t *data);

/* The join_info() and leave_info() routines copy into the caller's buffer
   the info string that the given nodeid provided when joining or leaving the
   group.  The caller's buffer should be GROUP_INFO_LEN bytes. */

int group_join_info(int level, char *name, int nodeid, char *info);
int group_leave_info(int level, char *name, int nodeid, char *info);

#endif

