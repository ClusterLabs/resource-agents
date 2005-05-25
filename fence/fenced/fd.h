/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**  
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __FD_DOT_H__
#define __FD_DOT_H__

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/poll.h>

#include "list.h"
#include "libgroup.h"

extern char			*prog_name;
extern int			fenced_debug_opt;
extern char			fenced_debug_buf[256];

#define MAX_NODENAME_LEN	255   /* should match libcman.h */
#define MAX_GROUPNAME_LEN	32    /* should match libgroup.h */
#define MAX_NODES		256
#define MAXARGS                 100  /* FIXME */
#define MAXLINE                 256
#define MAX_CLIENTS		5

#define DEFAULT_POST_JOIN_DELAY	6
#define DEFAULT_POST_FAIL_DELAY	0
#define DEFAULT_CLEAN_START	0
#define FENCED_SOCK_PATH	"fenced_socket"


/* use this one before we fork into the background */
#define die1(fmt, args...) \
do \
{ \
  fprintf(stderr, "%s: ", prog_name); \
  fprintf(stderr, fmt "\n", ##args); \
  exit(EXIT_FAILURE); \
} \
while (0)

#define die(fmt, args...) \
do \
{ \
  fprintf(stderr, "%s: ", prog_name); \
  fprintf(stderr, fmt "\n", ##args); \
  syslog(LOG_ERR, fmt, ##args); \
  exit(EXIT_FAILURE); \
} \
while (0)

#define FENCE_ASSERT(x, todo) \
do \
{ \
  if (!(x)) \
  { \
    {todo} \
    die("assertion failed on line %d of file %s\n", __LINE__, __FILE__); \
  } \
} \
while (0)

#define FENCE_RETRY(do_this, until_this) \
for (;;) \
{ \
  do { do_this; } while (0); \
  if (until_this) \
    break; \
  fprintf(stderr, "fenced:  out of memory:  %s, %u\n", __FILE__, __LINE__); \
  sleep(1); \
}

#define log_print(fmt, args...) \
do { \
	snprintf(fenced_debug_buf, 255, "%ld " fmt "\n", time(NULL), ##args); \
	fprintf(stderr, "fenced: %s", fenced_debug_buf); \
} while (0)

/* FIXME: send down debug client connection */
#define log_debug(fmt, args...) \
do { \
	log_print(fmt, ##args); \
} while (0)

#define log_error(fmt, args...) \
do { \
	log_debug(fmt, ##args); \
	syslog(LOG_ERR, fmt, ##args); \
} while (0)


struct fd;
struct fd_node;
struct commandline;

typedef struct fd fd_t;
typedef struct fd_node fd_node_t;
typedef struct commandline commandline_t;

struct commandline
{
	int post_join_delay;
	int post_fail_delay;
	int8_t clean_start;
	int8_t post_join_delay_opt;
	int8_t post_fail_delay_opt;
	int8_t clean_start_opt;
};

#define FDFL_RUN        (0)
#define FDFL_START      (1)
#define FDFL_FINISH     (2)

struct fd {
	struct list_head	list;
	int			global_id;	/* global unique fd ID */
	int 			last_stop;
	int 			last_start;
	int 			last_finish;
	int			first_recovery;
	int 			prev_count;
	struct list_head 	prev;
	struct list_head 	victims;
	struct list_head 	leaving;
	struct list_head	complete;
	char 			name[MAX_GROUPNAME_LEN+1];
};

struct fd_node {
	struct list_head 	list;
	int			nodeid;
	char 			name[MAX_NODENAME_LEN+1];
};


/* main.c */
fd_t *find_domain(char *name);

/* recover.c */
void add_complete_node(fd_t *fd, int nodeid, char *name);
void do_recovery(fd_t *fd, int start_type, int member_count, int *nodeids);
void do_recovery_done(fd_t *fd);

/* agent.c */
int dispatch_fence_agent(int cd, char *victim);

/* group.c */
int setup_groupd(void);
void exit_groupd(void);
int process_groupd(void);

/* member_xxx.c */
int setup_member(void);
void exit_member(void);
int update_cluster_members(void);
int is_member(char *name);
fd_node_t *get_new_node(fd_t *fd, int nodeid, char *in_name);

#endif				/*  __FD_DOT_H__  */
