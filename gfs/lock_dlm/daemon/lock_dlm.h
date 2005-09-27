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

#ifndef __LOCK_DLM_DOT_H__
#define __LOCK_DLM_DOT_H__

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <syslog.h>
#include <asm/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <linux/netlink.h>

#include "list.h"
#include "libgroup.h"
#include "libdlm.h"

/* FIXME: linux-2.6.11/include/linux/netlink.h (use header) */
#define NETLINK_KOBJECT_UEVENT  15

#define MAXARGS 64
#define MAXLINE 256
#define MAXCON  4
#define MAXNAME 255
#define MAX_MSGLEN 1024

#define LOCK_DLM_PORT 3
#define GFS_GROUP_NAME "gfs_dlm"
#define GFS_GROUP_LEVEL 2

#ifndef TRUE
#define TRUE (1)
#endif
#ifndef FALSE
#define FALSE (0)
#endif

extern char *prog_name;
extern int daemon_debug_opt;
extern char daemon_debug_buf[256];

#define log_debug(fmt, args...) \
do { \
	snprintf(daemon_debug_buf, 255, "%ld " fmt "\n", time(NULL), ##args); \
	if (daemon_debug_opt) fprintf(stderr, "%s", daemon_debug_buf); \
} while (0)

#define log_group(g, fmt, args...) \
do { \
	snprintf(daemon_debug_buf, 255, "%ld %s " fmt "\n", time(NULL), \
		 (g)->name, ##args); \
	if (daemon_debug_opt) fprintf(stderr, "%s", daemon_debug_buf); \
} while (0)

#define log_error(fmt, args...) \
do { \
	log_debug(fmt, ##args); \
	syslog(LOG_ERR, fmt, ##args); \
} while (0)


struct mountgroup {
	struct list_head	list;
	char			name[MAXNAME+1];
	char			table[MAXNAME+1];
	struct list_head	members;
	struct list_head	members_gone;
	int			memb_count;
	int			last_stop;
	int			last_start;
	int			last_finish;
	int			start_event_nr;
	int			start_type;
	int			our_jid;
	int			first_mount;
	int			first_mount_done;
	int			wait_first_done;
	int			first_start;
	int			low_finished_nodeid;
	int			spectator;
	int			withdraw;
};

struct mg_member {
	struct list_head	list;
	int			nodeid;
	int			jid;
	int			recover_journal;
	int			wait_recover_done;
	int			gone_event;
	int			mount_finished;
	int			spectator;
	int			withdraw;
	struct dlm_lksb		wd_lksb;
};

#endif
