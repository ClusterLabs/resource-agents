/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2008 Red Hat, Inc.  All rights reserved.
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

#ifndef _LIBDLMCONTROL_H_
#define _LIBDLMCONTROL_H_

#define DLMC_DUMP_SIZE		(1024 * 1024)

struct dlmc_node {
	int nodeid;
	int member;
};

struct dlmc_lockspace {
	int member_count;
	int state;
};

/* dlmc_lockspace_nodes() types */
#define DLMC_NODES_ALL		1
#define DLMC_NODES_MEMBERS	2

int dlmc_dump_debug(char *buf);
int dlmc_dump_plocks(char *name, char *buf);
int dlmc_node_info(char *name, int nodeid, struct dlmc_node *node);
int dlmc_lockspace_info(char *name, struct dlmc_lockspace *ls);
int dlmc_lockspace_nodes(char *name, int type, int max, int *count,
			 struct dlmc_node *nodes);

int dlmc_fs_connect(void);
void dlmc_fs_disconnect(int fd);
int dlmc_fs_register(int fd, char *name);
int dlmc_fs_unregister(int fd, char *name);
int dlmc_fs_notified(int fd, char *name, int nodeid);
int dlmc_fs_result(int fd, char *name, int *type, int *nodeid, int *result);

int dlmc_deadlock_check(char *name);

#endif
