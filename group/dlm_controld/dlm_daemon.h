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

#ifndef __DLM_DAEMON_DOT_H__
#define __DLM_DAEMON_DOT_H__

#include <sys/types.h>
#include <asm/types.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <fcntl.h>
#include <netdb.h>
#include <limits.h>
#include <unistd.h>
#include <time.h>
#include <syslog.h>

#include "netlink.h"

#include "list.h"
#include "libgroup.h"

#define MAXARGS		64
#define MAXLINE		256
#define MAXCON		4
#define MAXNAME		255
#define MAX_NODES	256 /* should be same as MAX_GROUP_MEMBERS */

extern char *prog_name;
extern int daemon_debug_opt;
extern char daemon_debug_buf[256];

#define log_debug(fmt, args...) \
do { \
	snprintf(daemon_debug_buf, 255, "%ld " fmt "\n", time(NULL), ##args); \
	if (daemon_debug_opt) fprintf(stderr, "%s", daemon_debug_buf); \
} while (0)

#define log_error(fmt, args...) \
do { \
	log_debug(fmt, ##args); \
	syslog(LOG_ERR, fmt, ##args); \
} while (0)


struct lockspace {
	struct list_head	list;
	char			name[MAXNAME+1];
	int			joining;
};

/* action.c */
int set_control(char *name, int val);
int set_event_done(char *name, int val);
int add_configfs_node(int nodeid, char *addr, int addrlen, int local);
void del_configfs_node(int nodeid);
void clear_configfs(void);
int set_members(char *name, int new_count, int *new_members);
int set_id(char *name, uint32_t id);

/* member_xxx.c */
int setup_member(void);
int process_member(void);
char *nodeid2name(int nodeid);

/* group.c */
int setup_groupd(void);
int process_groupd(void);

/* main.c */
struct lockspace *create_ls(char *name);
struct lockspace *find_ls(char *name);

#endif

