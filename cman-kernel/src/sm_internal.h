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

#ifndef __SM_INTERNAL_DOT_H__
#define __SM_INTERNAL_DOT_H__

/* 
 * Any header files needed by this file should be included before it in sm.h.
 * This file should only be included by sm.h.
 */

struct sm_group;
struct sm_sevent;
struct sm_uevent;
struct sm_node;
struct sm_msg;

typedef struct sm_group sm_group_t;
typedef struct sm_sevent sm_sevent_t;
typedef struct sm_uevent sm_uevent_t;
typedef struct sm_node sm_node_t;
typedef struct sm_msg sm_msg_t;


/* 
 * Number of seconds to wait before trying again to join or leave an SG
 */
#define RETRY_DELAY		(2)


/* 
 * Service Event - what a node uses to join or leave an sg
 */

/* SE Flags */
#define SEFL_CHECK              (0)
#define SEFL_ALLOW_JOIN         (1)
#define SEFL_ALLOW_JSTOP        (2)
#define SEFL_ALLOW_LEAVE        (3)
#define SEFL_ALLOW_LSTOP        (4)
#define SEFL_ALLOW_STARTDONE    (5)
#define SEFL_ALLOW_BARRIER      (6)
#define SEFL_DELAY              (7)
#define SEFL_LEAVE              (8)
#define SEFL_CANCEL             (9)

/* SE States */
#define SEST_JOIN_BEGIN         (1)
#define SEST_JOIN_ACKWAIT       (2)
#define SEST_JOIN_ACKED         (3)
#define SEST_JSTOP_ACKWAIT      (4)
#define SEST_JSTOP_ACKED        (5)
#define SEST_JSTART_SERVICEWAIT (6)
#define SEST_JSTART_SERVICEDONE (7)
#define SEST_BARRIER_WAIT       (8)
#define SEST_BARRIER_DONE       (9)
#define SEST_LEAVE_BEGIN        (10)
#define SEST_LEAVE_ACKWAIT      (11)
#define SEST_LEAVE_ACKED        (12)
#define SEST_LSTOP_ACKWAIT      (13)
#define SEST_LSTOP_ACKED        (14)
#define SEST_LSTART_WAITREMOTE  (15)
#define SEST_LSTART_REMOTEDONE  (16)

struct sm_sevent {
	struct list_head 	se_list;
	unsigned int 		se_id;
	sm_group_t *		se_sg;
	unsigned long 		se_flags;
	unsigned int 		se_state;

	int 			se_node_count;
	int 			se_memb_count;
	int 			se_reply_count;

	uint32_t *		se_node_ids;
	char *			se_node_status;
	int 			se_len_ids;	/* length of node_ids */
	int 			se_len_status;	/* length of node_status */

	int 			se_barrier_status;
	struct timer_list 	se_restart_timer;
};

/* 
 * Update Event - what an sg member uses to respond to an sevent 
 */

/* UE Flags */
#define UEFL_ALLOW_STARTDONE    (0)
#define UEFL_ALLOW_BARRIER      (1)
#define UEFL_CANCEL             (2)
#define UEFL_LEAVE              (3)
#define UEFL_CHECK              (4)

/* UE States */
#define UEST_JSTOP              (1)
#define UEST_JSTART_WAITCMD     (2)
#define UEST_JSTART             (3)
#define UEST_JSTART_SERVICEWAIT (4)
#define UEST_JSTART_SERVICEDONE (5)
#define UEST_BARRIER_WAIT       (6)
#define UEST_BARRIER_DONE       (7)
#define UEST_LSTOP              (8)
#define UEST_LSTART_WAITCMD     (9)
#define UEST_LSTART             (10)
#define UEST_LSTART_SERVICEWAIT (11)
#define UEST_LSTART_SERVICEDONE (12)

struct sm_uevent {
	unsigned int 		ue_state;
	unsigned long 		ue_flags;
	uint32_t 		ue_id;
	uint32_t 		ue_nodeid;
	int 			ue_num_nodes;
	int 			ue_barrier_status;
	uint16_t 		ue_remote_seid;
};

/* 
 * Service Group
 */

#define RECOVER_NONE		(0)
#define RECOVER_STOP		(1)
#define RECOVER_START		(2)
#define RECOVER_STARTDONE	(3)
#define RECOVER_BARRIERWAIT	(4)
#define RECOVER_BARRIERDONE	(5)

/* SG Flags */
#define SGFL_SEVENT             (1)
#define SGFL_UEVENT             (2)
#define SGFL_NEED_RECOVERY      (3)

/* SG States */
#define SGST_NONE		(0)
#define SGST_JOIN		(1)
#define SGST_RUN		(2)
#define SGST_RECOVER		(3)
#define SGST_UEVENT		(4)

struct sm_group {
	struct list_head 	list;		/* list of sg's */
	uint16_t 		level;
	uint32_t 		local_id;
	uint32_t 		global_id;
	unsigned long 		flags;
	int 			state;
	int 			refcount;	/* references from reg/unreg */
	void *			service_data;	/* data from the service */
	struct kcl_service_ops *ops;		/* ops from the service */
	struct completion 	event_comp;

	struct list_head 	memb;		/* Membership List for RC */
	int 			memb_count;	/* number of nodes in memb */
	struct list_head 	joining;	/* nodes joining the sg */
	sm_sevent_t *		sevent;
	sm_uevent_t 		uevent;

	int			recover_state;
	int			recover_stop;
	struct list_head 	recover_list;	/* recovery event list */
	void *			recover_data;
	char 			recover_barrier[MAX_BARRIER_NAME_LEN];

	int 			namelen;
	char 			name[1];	/* must be last field */
};

/* 
 * Service Message
 */

/* SMSG Type */
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

/* SMSG Status */
#define STATUS_POS              (1)
#define STATUS_NEG              (2)
#define STATUS_WAIT             (3)

struct sm_msg {
	uint8_t 		ms_type;
	uint8_t 		ms_status;
	uint16_t 		ms_sevent_id;
	uint32_t 		ms_global_sgid;
	uint32_t 		ms_global_lastid;
	uint16_t 		ms_sglevel;
	uint16_t 		ms_length;
	/* buf of ms_length bytes follows */
};

/* 
 * Node structure
 */

#define SNFL_NEED_RECOVERY  	(0)
#define SNFL_CLUSTER_MEMBER	(1)
#define SNFL_LEAVING        	(2)

struct sm_node {
	struct list_head 	list;
	uint32_t 		id;		/* node id from cnxman */
	unsigned long 		flags;
	int 			incarnation;	/* node incarnation number */
};

#endif				/* __SM_INTERNAL_DOT_H__ */
