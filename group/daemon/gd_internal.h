/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**  
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __GD_INTERNAL_DOT_H__
#define __GD_INTERNAL_DOT_H__

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/stat.h>


#include "list.h"
#include "linux_endian.h"
#include "groupd.h"
#include "libgroup.h"

#define GROUPD_PORT		(2)
#define MAX_BARRIERLEN		(33)
#define MAX_NAMELEN		(32)	/* should match libgroup.h */
#define MAX_MSGLEN		(512)
#define MAX_LEVELS		(4)
#define MAX_NODES		(256)
#define MAXARGS			(100)  /* FIXME */
#define MAXLINE			(256)
#define MAXCON			(16)
#define NALLOC			(16)
#define RETRY_DELAY		(2)

#define log_in(fmt, args...)    fprintf(stderr, "I: " fmt "\n", ##args)
#define log_out(fmt, args...)   fprintf(stderr, "O: " fmt "\n", ##args)
#define log_debug(fmt, args...) fprintf(stderr, "D: " fmt "\n", ##args)
#define log_print(fmt, args...) fprintf(stderr, "E: " fmt "\n", ##args)
#define log_group(g, fmt, args...) \
	fprintf(stderr, "D: %d:%s " fmt "\n", (g)->level, (g)->name, ##args)
#define log_error(g, fmt, args...) \
	fprintf(stderr, "E: %d:%s " fmt "\n", (g)->level, (g)->name, ##args)

#define ASSERT(x, do) \
{ \
	if (!(x)) { \
		fprintf(stderr, "Assertion failed on line %d of file %s\n" \
				"Assertion:  \"%s\"\n", \
				__LINE__, __FILE__, #x); \
		{do} \
	} \
}

#ifndef TRUE
#define TRUE (1)
#endif
#ifndef FALSE
#define FALSE (0)
#endif

#define test_bit(nr, addr)	((*addr) &   (1 << nr))
#define set_bit(nr, addr)	((*addr) |=  (1 << nr))
#define clear_bit(nr, addr)	((*addr) &= ~(1 << nr))

extern struct list_head		gd_groups;
extern struct list_head		gd_levels[MAX_LEVELS];
extern struct list_head		gd_nodes;
extern int			gd_node_count;
extern int			gd_member_count;
extern int			gd_quorate;
extern int			gd_nodeid;
extern int			gd_generation;
extern int			gd_event_barriers;
extern int			gd_update_barriers;
extern int			gd_recover_barriers;
extern int			ignore_cman_errors;

struct group;
struct event;
struct update;
struct node;
struct msg;
typedef struct group group_t;
typedef struct event event_t;
typedef struct update update_t;
typedef struct node node_t;
typedef struct msg msg_t;


/*
 * Event - what a node uses to join or leave a group
 */

#define EFL_ALLOW_JOIN         (0)
#define EFL_ALLOW_JSTOP        (1)
#define EFL_ALLOW_LEAVE        (2)
#define EFL_ALLOW_LSTOP        (3)
#define EFL_ALLOW_STARTDONE    (4)
#define EFL_ALLOW_BARRIER      (5)
#define EFL_DELAY              (6)
#define EFL_DELAY_RECOVERY     (7)
#define EFL_CANCEL             (8)

#define EST_JOIN_BEGIN         (1)
#define EST_JOIN_ACKWAIT       (2)
#define EST_JOIN_ACKED         (3)
#define EST_JSTOP_ACKWAIT      (4)
#define EST_JSTOP_ACKED        (5)
#define EST_JSTART_SERVICEWAIT (6)
#define EST_JSTART_SERVICEDONE (7)
#define EST_BARRIER_WAIT       (8)
#define EST_BARRIER_DONE       (9)
#define EST_LEAVE_BEGIN        (10)
#define EST_LEAVE_ACKWAIT      (11)
#define EST_LEAVE_ACKED        (12)
#define EST_LSTOP_ACKWAIT      (13)
#define EST_LSTOP_ACKED        (14)
#define EST_LSTART_WAITREMOTE  (15)
#define EST_LSTART_REMOTEDONE  (16)

struct event {
	struct list_head 	list;
	unsigned int 		id;
	group_t *		group;
	unsigned long 		flags;
	unsigned int 		state;

	int 			node_count;
	int 			memb_count;
	int 			reply_count;

	int *			node_ids;
	char *			node_status;
	int 			len_ids;	/* length of node_ids */
	int 			len_status;	/* length of node_status */

	int 			barrier_status;
	unsigned long		restart_time;
};

/*
 * Update - what group member uses to respond to an event 
 */

#define UFL_ALLOW_STARTDONE    (0)
#define UFL_ALLOW_BARRIER      (1)
#define UFL_CANCEL             (2)
#define UFL_LEAVE              (3)
#define UFL_CHECK              (4)

#define UST_JSTOP              (1)
#define UST_JSTART_WAITCMD     (2)
#define UST_JSTART             (3)
#define UST_JSTART_SERVICEWAIT (4)
#define UST_JSTART_SERVICEDONE (5)
#define UST_BARRIER_WAIT       (6)
#define UST_BARRIER_DONE       (7)
#define UST_LSTOP              (8)
#define UST_LSTART_WAITCMD     (9)
#define UST_LSTART             (10)
#define UST_LSTART_SERVICEWAIT (11)
#define UST_LSTART_SERVICEDONE (12)

struct update {
	unsigned int 		state;
	unsigned long 		flags;
	uint32_t 		id;
	uint32_t 		nodeid;
	int 			num_nodes;
	int 			barrier_status;
	uint16_t 		remote_seid;
};

/*
 * Group
 */

#define RECOVER_NONE		(0)
#define RECOVER_STOP		(1)
#define RECOVER_START		(2)
#define RECOVER_STARTDONE	(3)
#define RECOVER_BARRIERWAIT	(4)
#define RECOVER_BARRIERDONE	(5)

#define GFL_JOINING		(0)
#define GFL_LEAVING		(1)
#define GFL_MEMBER		(2)
#define GFL_UPDATE		(3)
#define GFL_NEED_RECOVERY	(4)

#define GST_NONE		(0)
#define GST_JOIN		(1)
#define GST_RUN			(2)
#define GST_RECOVER		(3)
#define GST_UPDATE		(4)

struct group {
	struct list_head 	list;		/* list of sg's */
	struct list_head	level_list;
	uint16_t 		level;
	uint32_t 		global_id;
	unsigned long 		flags;
	int 			state;
	int			client;
	int 			refcount;	/* references from reg/unreg */

	struct list_head 	memb;		/* Membership List for RC */
	int 			memb_count;	/* number of nodes in memb */
	struct list_head 	joining;	/* nodes joining the sg */
	event_t *		event;
	update_t *		update;

	int			recover_state;
	int			recover_stop;
	struct list_head 	recover_list;	/* recovery event list */
	void *			recover_data;
	char			recover_barrier[MAX_BARRIERLEN];

	int 			namelen;
	char 			name[1];	/* must be last field */
};

/*
 * Message
 */

#define SMSG_JOIN_REQ           (1)
#define SMSG_JOIN_REP           (2)
#define SMSG_JSTOP_REQ          (3)
#define SMSG_JSTOP_REP          (4)
#define SMSG_JSTART_CMD         (5)
#define SMSG_LEAVE_REQ          (6)
#define SMSG_LEAVE_REP          (7)
#define SMSG_LSTOP_REQ          (8)
#define SMSG_LSTOP_REP          (9)
#define SMSG_LSTART_CMD         (10)
#define SMSG_LSTART_DONE        (11)
#define SMSG_RECOVER		(12)

#define STATUS_POS              (1)
#define STATUS_NEG              (2)
#define STATUS_WAIT             (3)

struct msg {
	uint8_t 		ms_type;
	uint8_t 		ms_status;
	uint16_t 		ms_level;
	uint32_t 		ms_event_id;
	uint32_t 		ms_group_id;
	uint32_t 		ms_last_id;
	int			ms_to_nodeid;
};

/*
 * Node
 */

#define NFL_NEED_RECOVERY  	(0)
#define NFL_CLUSTER_MEMBER	(1)
#define NFL_LEAVING        	(2)

struct node {
	struct list_head 	list;
	int			id;
	unsigned long 		flags;
	int 			incarnation;
	char			join_info[GROUP_INFO_LEN];
	char			leave_info[GROUP_INFO_LEN];
};


#define GD_BARRIER_STARTDONE		(0)
#define GD_BARRIER_STARTDONE_NEW	(1)
#define GD_BARRIER_RECOVERY		(2)
#define GD_BARRIER_RESET		(3)

/* main.c */
void group_stop(group_t *g);
void group_setid(group_t *g);
void group_start(group_t *g, int *memb, int count, int event_nr, int type);
void group_finish(group_t *g, int event_nr);
void group_terminate(group_t *g);

/* cman.c */
int setup_member_message(void);
int process_member_message(void);
int send_nodeid_message(char *buf, int len, int nodeid);
int send_broadcast_message(char *buf, int len);
int send_members_message(group_t *g, char *buf, int len);
int send_members_message_ev(group_t *g, char *buf, int len, event_t *ev);
int send_broadcast_message_ev(char *buf, int len, event_t *ev);
node_t *find_node(int nodeid);
int do_barrier(group_t *g, char *name, int count, int type);
int process_barriers(void);
void cancel_recover_barrier(group_t *g);
void cancel_update_barrier(group_t *g);

/* message.c */
char *create_msg(group_t *g, int type, int datalen, int *msglen, event_t *ev);
void process_message(char *buf, int len, int nodeid);
uint32_t new_global_id(int level);
node_t *find_joiner(group_t *g, int nodeid);
int test_allowed_msgtype(event_t *ev, int type);
void clear_allowed_msgtype(event_t *ev, int type);
void set_allowed_msgtype(event_t *ev, int type);
void msg_copy_out(msg_t *m);

/* joinleave.c */
void set_event_id(uint32_t *id);
void init_joinleave(void);
void add_joinleave_event(event_t *ev);
int process_joinleave(void);
int do_join(char *name, int level, int ci);
int do_leave(char *name, int level, int nowait);
int in_event(group_t *g);
int in_update(group_t *g);
event_t *find_event(unsigned int id);
group_t *find_group(char *name);
group_t *find_group_id(int id);
group_t *find_group_level(char *name, int level);
void remove_group(group_t *g);
node_t *new_node(int nodeid);
void backout_events(void);

/* update.c */
int process_updates(void);
void cancel_updates(int *effected);

/* recover.c */
void process_recover_msg(msg_t *msg, int nodeid);
void process_nodechange(void);
int process_recoveries(void);

/* done.c */
int do_done(char *name, int level, int event_nr);


#endif				/* __GD_INTERNAL_DOT_H__ */
