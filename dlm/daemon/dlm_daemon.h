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
#include <linux/netlink.h>

/* FIXME: linux-2.6.11/include/linux/netlink.h (use header) */
#define NETLINK_KOBJECT_UEVENT  15

#include "libgroup.h"

#define MAXARGS		64
#define MAXLINE		256
#define MAX_NODES	256 /* should be same as MAX_GROUP_MEMBERS */

#define log_error(fmt, args...) fprintf(stderr, fmt "\n", ##args)
#define log_debug(fmt, args...) fprintf(stderr, fmt "\n", ##args)

/* action.c */
int set_local(int argc, char **argv);
int set_node(int argc, char **argv);
int ls_control(int argc, char **argv);
int ls_event_done(int argc, char **argv);
int ls_members(int argc, char **argv);
int ls_set_id(int argc, char **argv);

/* member_xxx.c */
int setup_member(void);
int process_member(void);

/* group.c */
int setup_groupd(void);
int process_groupd(void);

/* main.c */
void make_args(char *buf, int *argc, char **argv, char sep);

#endif

