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

#ifndef _LIBFENCED_H_
#define _LIBFENCED_H_

#define FENCED_DUMP_SIZE (1024 * 1024)

struct fenced_node {
	int nodeid;
	int member;
	int victim;
	int last_fenced_master;
	int last_fenced_how;
	uint64_t last_fenced_time;
	uint64_t last_joined_time;
	uint64_t last_remove_time;
};

struct fenced_domain {
	int member_count;
	int victim_count;
	int master_nodeid;
	int current_victim;
	int state;
};

int fenced_join(void);
int fenced_leave(void);
int fenced_dump_debug(char *buf);
int fenced_external(char *name);
int fenced_node_info(int nodeid, struct fenced_node *node);
int fenced_domain_info(struct fenced_domain *domain);
int fenced_domain_members(int max, int *count, struct fenced_node *members);

#endif
