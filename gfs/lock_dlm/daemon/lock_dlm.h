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

#define MAXARGS			64
#define MAXLINE			256
#define MAXNAME			255
#define MAX_CLIENTS		8
#define MAX_MSGLEN		2048

#define LOCK_DLM_GROUP_LEVEL	2
#define LOCK_DLM_GROUP_NAME	"lock_dlmd"
#define LOCK_DLM_SOCK_PATH	"lock_dlmd_sock"

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
	char			type[5];
	char			dir[PATH_MAX+1];
	int			mount_client;
	uint32_t		id;
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
	struct list_head	resources; /* for plocks */
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

#define MSG_JOURNAL 1
#define MSG_PLOCK 2

struct gdlm_header {
	uint16_t		version[3];
	uint16_t		type;			/* MSG_ */
	uint32_t		nodeid;			/* sender */
};


struct mountgroup *find_mg(char *name);
struct mountgroup *find_mg_id(uint32_t id);

int setup_cman(void);
int setup_groupd(void);
int process_groupd(void);
int setup_libdlm(void);
int process_libdlm(void);
int setup_plocks(void);
int process_plocks(void);

int do_mount(int ci, char *dir, char *type, char *proto, char *table,
	     char *extra);
int do_unmount(int ci, char *dir);
int do_recovery_done(char *name);
int do_withdraw(char *name);

int client_send(int ci, char *buf, int len);

#endif
