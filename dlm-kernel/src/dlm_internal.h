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

#ifndef __DLM_INTERNAL_DOT_H__
#define __DLM_INTERNAL_DOT_H__

/*
 * This is the main header file to be included in each DLM source file.
 */

#define DLM_RELEASE_NAME "<CVS>"

#include <linux/slab.h>
#include <linux/sched.h>
#include <asm/semaphore.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <asm/uaccess.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/random.h>

#include <cluster/dlm.h>
#include <cluster/dlm_device.h>
#include <cluster/service.h>

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

#if (BITS_PER_LONG == 64)
#define PRIu64 "lu"
#define PRId64 "ld"
#define PRIo64 "lo"
#define PRIx64 "lx"
#define PRIX64 "lX"
#define SCNu64 "lu"
#define SCNd64 "ld"
#define SCNo64 "lo"
#define SCNx64 "lx"
#define SCNX64 "lX"
#else
#define PRIu64 "Lu"
#define PRId64 "Ld"
#define PRIo64 "Lo"
#define PRIx64 "Lx"
#define PRIX64 "LX"
#define SCNu64 "Lu"
#define SCNd64 "Ld"
#define SCNo64 "Lo"
#define SCNx64 "Lx"
#define SCNX64 "LX"
#endif

#define wchan_cond_sleep_intr(chan, sleep_cond) \
do \
{ \
  DECLARE_WAITQUEUE(__wait_chan, current); \
  current->state = TASK_INTERRUPTIBLE; \
  add_wait_queue(&chan, &__wait_chan); \
  if ((sleep_cond)) \
    schedule(); \
  remove_wait_queue(&chan, &__wait_chan); \
  current->state = TASK_RUNNING; \
} \
while (0)

static inline int check_timeout(unsigned long stamp, unsigned int seconds)
{
    return time_after(jiffies, stamp + seconds * HZ);
}


#define log_print(fmt, args...) printk("dlm: "fmt"\n", ##args)

#define log_all(ls, fmt, args...) \
	do { \
		printk("dlm: %s: " fmt "\n", (ls)->ls_name, ##args); \
		dlm_debug_log(ls, fmt, ##args); \
	} while (0)

#define log_error log_all


#define DLM_DEBUG
#if defined(DLM_DEBUG)
#define log_debug(ls, fmt, args...) dlm_debug_log(ls, fmt, ##args)
#else
#define log_debug(ls, fmt, args...)
#endif

#if defined(DLM_DEBUG) && defined(DLM_DEBUG_ALL)
#undef log_debug
#define log_debug log_all
#endif


#define DLM_ASSERT(x, do) \
{ \
  if (!(x)) \
  { \
    dlm_locks_dump(); \
    dlm_debug_dump(); \
    printk("\nDLM:  Assertion failed on line %d of file %s\n" \
               "DLM:  assertion:  \"%s\"\n" \
               "DLM:  time = %lu\n", \
               __LINE__, __FILE__, #x, jiffies); \
    {do} \
    printk("\n"); \
    BUG(); \
    panic("DLM:  Record message above and reboot.\n"); \
  } \
}


struct dlm_ls;
struct dlm_lkb;
struct dlm_rsb;
struct dlm_csb;
struct dlm_node;
struct dlm_lkbtable;
struct dlm_rsbtable;
struct dlm_dirtable;
struct dlm_direntry;
struct dlm_recover;
struct dlm_header;
struct dlm_request;
struct dlm_reply;
struct dlm_rcom;
struct dlm_query_request;
struct dlm_query_reply;


struct dlm_direntry {
	struct list_head	list;
	uint32_t		master_nodeid;
	uint16_t		length;
	char			name[1];
};

struct dlm_dirtable {
	struct list_head	list;
	rwlock_t		lock;
};

struct dlm_rsbtable {
	struct list_head	list;
	rwlock_t		lock;
};

struct dlm_lkbtable {
	struct list_head	list;
	rwlock_t		lock;
	uint16_t		counter;
};

/*
 * Cluster node (per node in cluster)
 */

struct dlm_node {
	struct list_head	list;
	uint32_t		nodeid;
	int			refcount;	/* num csb's referencing */
};

/*
 * Cluster System Block (per node in a ls)
 */

struct dlm_csb {
	struct list_head	list;		/* per-lockspace node list */
	struct dlm_node *	node;		/* global node structure */
	int			gone_event;	/* event id when node removed */

	/* recovery stats for debugging */

	uint32_t		names_send_count;
	uint32_t		names_send_msgid;
	uint32_t		names_recv_count;
	uint32_t		names_recv_msgid;
	uint32_t		locks_send_count;
	uint32_t		locks_send_msgid;
	uint32_t		locks_recv_count;
	uint32_t		locks_recv_msgid;
};

/*
 * Used to save and manage recovery state for a lockspace.
 */

struct dlm_recover {
	struct list_head	list;
	uint32_t *		nodeids;
	int			node_count;
	int			event_id;
};

/*
 * Elements in the range array
 */

#define GR_RANGE_START		(0)
#define GR_RANGE_END		(1)
#define RQ_RANGE_START		(2)
#define RQ_RANGE_END		(3)

/*
 * Lockspace structure
 */

#define LSFL_WORK		(0)
#define LSFL_LS_RUN		(1)
#define LSFL_LS_STOP		(2)
#define LSFL_LS_START		(3)
#define LSFL_LS_FINISH		(4)
#define LSFL_RECCOMM_WAIT	(5)
#define LSFL_RECCOMM_READY	(6)
#define LSFL_NOTIMERS		(7)
#define LSFL_FINISH_RECOVERY	(8)
#define LSFL_RESDIR_VALID	(9)
#define LSFL_ALL_RESDIR_VALID	(10)
#define LSFL_NODES_VALID	(11)
#define LSFL_ALL_NODES_VALID	(12)
#define LSFL_REQUEST_WARN	(13)
#define LSFL_NOCONVGRANT	(14)

#define LSST_NONE		(0)
#define LSST_INIT		(1)
#define LSST_INIT_DONE		(2)
#define LSST_CLEAR		(3)
#define LSST_WAIT_START		(4)
#define LSST_RECONFIG_DONE	(5)

struct dlm_ls {
	struct list_head	ls_list;	/* list of lockspaces */
	uint32_t		ls_local_id;	/* local unique lockspace ID */
	uint32_t		ls_global_id;	/* global unique lockspace ID */
	int			ls_allocation;	/* Memory allocation policy */
	int			ls_count;	/* reference count */
	unsigned long		ls_flags;	/* LSFL_ */

	struct dlm_rsbtable *	ls_rsbtbl;
	uint32_t		ls_rsbtbl_size;

	struct dlm_lkbtable *	ls_lkbtbl;
	uint32_t		ls_lkbtbl_size;

	struct dlm_dirtable *	ls_dirtbl;
	uint32_t		ls_dirtbl_size;

	struct list_head	ls_nodes;	/* current nodes in RC */
	struct list_head	ls_nodes_gone;	/* dead node list, recovery */
	uint32_t		ls_num_nodes;	/* number of nodes in RC */
	uint32_t		ls_low_nodeid;

	struct rw_semaphore	ls_unlock_sem;	/* To prevent unlock on a
						   parent lock racing with a
						   new child lock */

	struct list_head	ls_deadlockq;	/* List of locks in conversion
						   ordered by duetime. for
						   deadlock detection */

	/* recovery related */

	struct list_head	ls_recover;	/* dlm_recover structs */
	spinlock_t		ls_recover_lock;
	int			ls_last_stop;
	int			ls_last_start;
	int			ls_last_finish;
	int			ls_state;	/* recovery states */

	struct rw_semaphore	ls_in_recovery;	/* block local requests */
	struct list_head	ls_requestqueue;/* queue remote requests */
	struct semaphore	ls_requestqueue_lock;

	struct dlm_rcom *	ls_rcom;	/* recovery comms */
	uint32_t		ls_rcom_msgid;
	struct semaphore	ls_rcom_lock;

	struct list_head	ls_recover_list;
	spinlock_t		ls_recover_list_lock;
	int			ls_recover_list_count;
	wait_queue_head_t	ls_wait_general;

	struct list_head	ls_rootres;	/* root resources */
	struct rw_semaphore	ls_root_lock;	/* protect rootres list */

	struct list_head	ls_rebuild_rootrsb_list; /* Root of lock trees
							  we're deserialising */
	int			ls_namelen;
	char			ls_name[1];
};

/*
 * Resource block
 */

#define RESFL_NEW_MASTER	(0)
#define RESFL_RECOVER_LIST	(1)
#define RESFL_MASTER		(2)

struct dlm_rsb {
	struct list_head	res_hashchain;
	uint32_t		res_bucket;

	struct dlm_ls *		res_ls;		/* The owning lockspace */

	struct list_head	res_rootlist;	/* List of root rsb's */

	struct list_head	res_subreslist;	/* List of all sub-resources
						   for this root rsb */

	uint8_t			res_depth;	/* Depth in resource tree */
	unsigned long		res_flags;	/* Flags, RESFL_ */

	struct list_head	res_grantqueue;
	struct list_head	res_convertqueue;
	struct list_head	res_waitqueue;

	uint32_t		res_nodeid;	/* nodeid of master node */

	struct dlm_rsb *	res_root;	/* root rsb if a subresource */
	struct dlm_rsb *	res_parent;	/* parent rsb (if any) */

	atomic_t		res_ref;	/* Number of lkb's */
	uint16_t		res_remasterid;	/* ID used during remaster */

	struct list_head	res_recover_list; /* General list for use
						     during recovery */
	int			res_recover_msgid;
	int			res_newlkid_expect;

	struct rw_semaphore	res_lock;

	char *			res_lvbptr;	/* Lock value block */

	uint8_t			res_length;
	char			res_name[1];	/* <res_length> bytes */
};

/*
 * Lock block. To avoid confusion, where flags mirror the
 * public flags, they should have the same value.
 */

#define GDLM_LKSTS_NEW		(0)
#define GDLM_LKSTS_WAITING	(1)
#define GDLM_LKSTS_GRANTED	(2)
#define GDLM_LKSTS_CONVERT	(3)

#define GDLM_LKFLG_VALBLK	(0x00000008)
#define GDLM_LKFLG_PERSISTENT	(0x00000080)	/* Don't unlock when process exits */
#define GDLM_LKFLG_NODLCKWT	(0x00000100)	/* Don't do deadlock detection */
#define GDLM_LKFLG_EXPEDITE	(0x00000400)	/* Move to head of convert queue */

/* Internal flags */
#define GDLM_LKFLG_RANGE	(0x00001000)	/* Range field is present
						   (remote protocol only) */
#define GDLM_LKFLG_MSTCPY	(0x00002000)
#define GDLM_LKFLG_DELETED	(0x00004000)	/* LKB is being deleted */
#define GDLM_LKFLG_LQCONVERT	(0x00008000)
#define GDLM_LKFLG_LQRESEND	(0x00010000)	/* LKB on lockqueue must be resent */
#define GDLM_LKFLG_DEMOTED	(0x00020000)
#define GDLM_LKFLG_RESENT	(0x00040000)
#define GDLM_LKFLG_NOREBUILD	(0x00080000)
#define GDLM_LKFLG_UNLOCKDONE	(0x00100000)

#define AST_COMP		(1)
#define AST_BAST		(2)
#define AST_DEL			(4)

struct dlm_lkb {
	uint32_t		lkb_flags;
	uint16_t		lkb_status;	/* grant, wait, convert */
	int8_t			lkb_rqmode;	/* requested lock mode */
	int8_t			lkb_grmode;	/* granted lock mode */
	uint32_t		lkb_retstatus;	/* status to return in lksb */
	uint32_t		lkb_id;		/* our lock ID */
	struct dlm_lksb *	lkb_lksb;	/* status block of caller */
	struct list_head	lkb_idtbl_list;	/* lockidtbl */
	struct list_head	lkb_statequeue;	/* rsb's g/c/w queue */
	struct dlm_rsb *	lkb_resource;
	struct list_head	lkb_ownerqueue;	/* list of locks owned by a
						   process */
	struct dlm_lkb *	lkb_parent;	/* parent lock if any */
	atomic_t		lkb_childcnt;	/* number of children */

	struct list_head	lkb_lockqueue;	/* queue of locks waiting
						   for remote reply */
	int			lkb_lockqueue_state; /* reason on lockqueue */
	int			lkb_lockqueue_flags; /* as passed into
							lock/unlock */
	unsigned long		lkb_lockqueue_time;  /* time lkb went on the
							lockqueue */
	unsigned long		lkb_duetime;	/* for deadlock detection */

	uint32_t		lkb_remid;	/* id on remote partner */
	uint32_t		lkb_nodeid;	/* id of remote partner */

	void *			lkb_astaddr;
	void *			lkb_bastaddr;
	long			lkb_astparam;
	struct list_head	lkb_astqueue;	/* locks with asts to deliver */
	uint16_t		lkb_astflags;	/* COMP, BAST, DEL */
	uint8_t			lkb_bastmode;	/* requested mode */
	uint8_t			lkb_highbast;	/* highest mode bast sent for */

	struct dlm_request *	lkb_request;

	struct list_head	lkb_deadlockq;	/* ls_deadlockq list */

	char *			lkb_lvbptr;	/* points to lksb lvb on local
						   lock, allocated lvb on
						   on remote lock */
	uint64_t *		lkb_range;	/* Points to an array of 64 bit
						   numbers that represent the
						   requested and granted ranges
						   of the lock. NULL implies
						   0-ffffffffffffffff */
};

/*
 * Header part of the mid-level comms system. All packets start with
 * this header so we can identify them. The comms packet can
 * contain many of these structs but the are split into individual
 * work units before being passed to the lockqueue routines.
 * below this are the structs that this is a header for
 */

struct dlm_header {
	uint8_t			rh_cmd;		/* What we are */
	uint8_t			rh_flags;	/* maybe just a pad */
	uint16_t		rh_length;	/* Length of struct (so we can
						   send many in 1 message) */
	uint32_t		rh_lkid;	/* Lock ID tag: ie the local
						   (requesting) lock ID */
	uint32_t		rh_lockspace;	/* Lockspace ID */
};

/*
 * This is the struct used in a remote lock/unlock/convert request
 * The mid-level comms API should turn this into native byte order.
 * Most "normal" lock operations will use these two structs for
 * communications. Recovery operations use their own structs
 * but still with the gd_req_header on the front.
 */

struct dlm_request {
	struct dlm_header	rr_header;
	uint32_t		rr_remlkid;	/* Remote lock ID */
	uint32_t		rr_remparid;	/* Parent's remote lock ID */
	uint32_t		rr_flags;	/* Flags from lock/convert req*/
	uint64_t		rr_range_start; /* Yes, these are in the right
						   place... */
	uint64_t		rr_range_end;
	uint32_t		rr_status;	/* Status to return if this is
						   an AST request */
	uint8_t			rr_rqmode;	/* Requested lock mode */
	uint8_t			rr_asts;	/* Whether the LKB has ASTs */
	char			rr_lvb[DLM_LVB_LEN];
	char			rr_name[1];	/* As long as needs be. Only
						   used for directory lookups.
						   The length of this can be
						   worked out from the packet
						   length */
};

/*
 * This is the struct returned by a remote lock/unlock/convert request
 * The mid-level comms API should turn this into native byte order.
 */

struct dlm_reply {
	struct dlm_header	rl_header;
	uint32_t		rl_lockstate;	/* Whether request was
						   queued/granted/waiting */
	uint32_t		rl_nodeid;	/* nodeid of lock master */
	uint32_t		rl_status;	/* Status to return to caller */
	uint32_t		rl_lkid;	/* Remote lkid */
	char			rl_lvb[DLM_LVB_LEN];
};

/*
 * Recovery comms message
 */

struct dlm_rcom {
	struct dlm_header	rc_header;	/* 32 byte aligned */
	uint32_t		rc_msgid;
	uint16_t		rc_datalen;
	uint8_t			rc_expanded;
	uint8_t			rc_subcmd;	/* secondary command */
	char			rc_buf[1];	/* first byte of data goes here
						   and extends beyond here for
						   another datalen - 1 bytes.
						   rh_length is set to sizeof
						   dlm_rcom + datalen - 1 */
};


/* A remote query: GDLM_REMCMD_QUERY */

struct dlm_query_request {
	struct dlm_header	rq_header;
	uint32_t		rq_mstlkid;   /* LockID on master node */
	uint32_t		rq_query;     /* query from the user */
	uint32_t		rq_maxlocks;  /* max number of locks we can
						 cope with */
};

/* First block of a reply query.  cmd = GDLM_REMCMD_QUERY */
/* There may be subsequent blocks of
   lock info in GDLM_REMCMD_QUERYCONT messages which just have
   a normal header. The last of these will have rh_flags set to
   GDLM_REMFLAG_ENDQUERY
 */

struct dlm_query_reply {
	struct dlm_header	rq_header;
	uint32_t		rq_numlocks;  /* Number of locks in reply */
	uint32_t		rq_startlock; /* Which lock this block starts
						 at (for multi-block replies) */
	uint32_t		rq_status;

	/* Resource information */
	uint32_t		rq_grantcount;	/* No. of nodes on grantqueue */
	uint32_t		rq_convcount;	/* No. of nodes on convertq */
	uint32_t		rq_waitcount;	/* No. of nodes on waitqueue */
	char			rq_valblk[DLM_LVB_LEN];	/* Master's LVB
							   contents, if
							   applicable */
};

/*
 * Lockqueue wait lock states
 */

#define GDLM_LQSTATE_WAIT_RSB		1
#define GDLM_LQSTATE_WAIT_CONVERT	2
#define GDLM_LQSTATE_WAIT_CONDGRANT	3
#define GDLM_LQSTATE_WAIT_UNLOCK	4

/* Commands sent across the comms link */
#define GDLM_REMCMD_LOOKUP		1
#define GDLM_REMCMD_LOCKREQUEST		2
#define GDLM_REMCMD_UNLOCKREQUEST	3
#define GDLM_REMCMD_CONVREQUEST		4
#define GDLM_REMCMD_LOCKREPLY		5
#define GDLM_REMCMD_LOCKGRANT		6
#define GDLM_REMCMD_SENDBAST		7
#define GDLM_REMCMD_SENDCAST		8
#define GDLM_REMCMD_REM_RESDATA		9
#define GDLM_REMCMD_RECOVERMESSAGE	20
#define GDLM_REMCMD_RECOVERREPLY	21
#define GDLM_REMCMD_QUERY		30
#define GDLM_REMCMD_QUERYREPLY		31

/* Set in rh_flags when this is the last block of
   query information. Note this could also be the first
   block */
#define GDLM_REMFLAG_ENDQUERY       1

#ifndef BUG_ON
#define BUG_ON(x)
#endif

void dlm_debug_log(struct dlm_ls *ls, const char *fmt, ...);
void dlm_debug_dump(void);
void dlm_locks_dump(void);

#endif				/* __DLM_INTERNAL_DOT_H__ */
