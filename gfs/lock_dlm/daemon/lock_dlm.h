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
#include <asm/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/netlink.h>

#include "list.h"
#include "libgroup.h"

/* FIXME: linux-2.6.11/include/linux/netlink.h (use header) */
#define NETLINK_KOBJECT_UEVENT  15

#define MAXARGS 64
#define MAXLINE 256
#define MAXCON  4
#define MAXNAME 256

#define GFS_GROUP_NAME "gfs_dlm"
#define GFS_GROUP_LEVEL 2

#ifndef TRUE
#define TRUE (1)
#endif
#ifndef FALSE
#define FALSE (0)
#endif

#define log_error(fmt, args...) fprintf(stderr, fmt "\n", ##args)
#define log_debug(fmt, args...) fprintf(stderr, fmt "\n", ##args)
#define log_group(g, fmt, args...) \
	fprintf(stderr, "%s " fmt "\n", (g)->name, ##args)

int setup_member(void);

struct mountgroup {
	struct list_head	list;
	char			name[MAXNAME+1];
	int			namelen;
	struct list_head	members;
	struct list_head	members_gone;
	int			num_memb;
	int			start_event_nr;
	int			finish_event_nr;
	int			start_type;
	int			our_jid;
	int			first_start;
	int			low_nodeid;
	int			spectator;
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
};

#endif
