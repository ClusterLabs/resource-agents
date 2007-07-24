/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005-2007 Red Hat, Inc.  All rights reserved.
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
#include <sched.h>
#include <signal.h>
#include <sys/time.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>
#include <linux/dlm.h>
#include <linux/dlm_netlink.h>

#include <openais/saAis.h>
#include <openais/saCkpt.h>
#include <openais/cpg.h>

#include "dlm_controld.h"
#include "list.h"
#include "linux_endian.h"
#include "libgroup.h"

#define MAXARGS		8
#define MAXLINE		256
#define MAXCON		4
#define MAXNAME		255
#define MAX_NODES	256 /* should be same as MAX_GROUP_MEMBERS */

extern char *prog_name;
extern int daemon_debug_opt;
extern int kernel_debug_opt;
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

#define log_group(ls, fmt, args...) \
do { \
	snprintf(daemon_debug_buf, 255, "%ld %s " fmt "\n", time(NULL), \
		 (ls)->name, ##args); \
	if (daemon_debug_opt) fprintf(stderr, "%s", daemon_debug_buf); \
} while (0)


struct lockspace {
	struct list_head	list;
	char			name[MAXNAME+1];
	uint32_t		global_id;
	int			joining;
	int			cpg_ci;
	cpg_handle_t		cpg_h;
	SaCkptCheckpointHandleT lock_ckpt_handle;
	struct list_head	transactions;
	struct list_head	resources;
	struct list_head	nodes;
	struct timeval		last_deadlock_check;
	unsigned int		timewarn_count;
};

/* action.c */
int set_control(char *name, int val);
int set_event_done(char *name, int val);
int add_configfs_node(int nodeid, char *addr, int addrlen, int local);
void del_configfs_node(int nodeid);
void clear_configfs(void);
int set_members(char *name, int new_count, int *new_members);
int set_id(char *name, uint32_t id);
void set_ccs_options(void);
int do_read(int fd, void *buf, size_t count);
int do_write(int fd, void *buf, size_t count);

/* member_xxx.c */
int setup_member(void);
void process_member(int ci);
char *nodeid2name(int nodeid);

/* group.c */
int setup_groupd(void);
void process_groupd(int ci);

/* main.c */
int client_add(int fd, void (*workfn)(int ci), void (*deadfn)(int ci));
void client_dead(int ci);
void set_client_lockspace(int ci, struct lockspace *ls);
struct lockspace *get_client_lockspace(int ci);
struct lockspace *create_ls(char *name);
struct lockspace *find_ls(char *name);

/* member_cman.c */
int is_cman_member(int nodeid);
void cman_statechange(void);

/* deadlock.c */
void setup_deadlock(void);
void join_deadlock_cpg(struct lockspace *ls);
void leave_deadlock_cpg(struct lockspace *ls);
void send_cycle_start(struct lockspace *ls);

#endif

