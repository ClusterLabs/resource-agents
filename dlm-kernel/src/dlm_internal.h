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


#define GDLM_ASSERT(x, do) \
{ \
  if (!(x)) \
  { \
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


struct gd_ls;
struct gd_lkb;
struct gd_res;
struct gd_csb;
struct gd_node;
struct gd_resmov;
struct gd_resdata;
struct gd_recover;
struct gd_recinfo;
struct gd_resdir_bucket;
struct gd_remlockreply;
struct gd_remlockrequest;
struct gd_rcom;

typedef struct gd_ls gd_ls_t;
typedef struct gd_lkb gd_lkb_t;
typedef struct gd_res gd_res_t;
typedef struct gd_csb gd_csb_t;
typedef struct gd_node gd_node_t;
typedef struct gd_resmov gd_resmov_t;
typedef struct gd_resdata gd_resdata_t;
typedef struct gd_recover gd_recover_t;
typedef struct gd_resdir_bucket gd_resdir_bucket_t;
typedef struct gd_rcom gd_rcom_t;

/*
 * Resource Data - an entry for a resource in the resdir hash table
 */

struct gd_resdata {
	struct list_head rd_list;
	uint32_t rd_master_nodeid;
	uint16_t rd_length;
	uint8_t rd_sequence;
	char rd_name[1];	/* <rd_length> bytes */
};

/*
 * Resource Directory Bucket - a hash bucket of resdata entries in the resdir
 * hash table
 */

struct gd_resdir_bucket {
	struct list_head rb_reslist;
	rwlock_t rb_lock;
};

/*
 * A resource description as moved between nodes
 */

struct gd_resmov {
	uint32_t rm_nodeid;
	uint16_t rm_length;
	uint16_t rm_pad;
};

/*
 * An entry in the lock ID table.  Locks for this bucket are kept on list.
 * Counter is used to assign an id to locks as they are added to this bucket.
 */

struct gd_lockidtbl_entry {
	struct list_head list;
	uint16_t counter;
};

/* Elements in the range array */

#define GR_RANGE_START 0
#define GR_RANGE_END   1
#define RQ_RANGE_START 2
#define RQ_RANGE_END   3

/*
 * Lockspace structure.  The context for GDLM locks.
 */

#define RESHASHTBL_SIZE     (256)

#define RESDIRHASH_SHIFT    (9)
#define RESDIRHASH_SIZE     (1 << RESDIRHASH_SHIFT)
#define RESDIRHASH_MASK     (RESDIRHASH_SIZE - 1)

#define LSFL_WORK               (0)
#define LSFL_LS_RUN             (1)
#define LSFL_LS_STOP            (2)
#define LSFL_LS_START           (3)
#define LSFL_LS_FINISH          (4)
#define LSFL_RECCOMM_WAIT       (5)
#define LSFL_RECCOMM_READY      (6)
#define LSFL_NOTIMERS           (7)
#define LSFL_FINISH_RECOVERY    (8)
#define LSFL_RESDIR_VALID       (9)
#define LSFL_ALL_RESDIR_VALID   (10)
#define LSFL_NODES_VALID        (11)
#define LSFL_ALL_NODES_VALID    (12)
#define LSFL_REQUEST_WARN       (13)

#define LSST_NONE           (0)
#define LSST_INIT           (1)
#define LSST_INIT_DONE      (2)
#define LSST_CLEAR          (3)
#define LSST_WAIT_START     (4)
#define LSST_RECONFIG_DONE  (5)

struct gd_ls {
	struct list_head ls_list;	/* list of lockspaces */
	uint32_t ls_local_id;	/* local unique lockspace ID */
	uint32_t ls_global_id;	/* global unique lockspace ID */
	int ls_allocation;	/* Memory allocation policy */
	unsigned long ls_flags;	/* LSFL_ */

	struct list_head ls_rootres;	/* List of root resources */

	int ls_hashsize;
	int ls_hashmask;
	struct list_head *ls_reshashtbl;	/* Hash table for resources */
	rwlock_t ls_reshash_lock;	/* Lock for hash table */

	struct gd_lockidtbl_entry *ls_lockidtbl;
	uint32_t ls_lockidtbl_size;	/* Size of lock id table */
	rwlock_t ls_lockidtbl_lock;

	struct list_head ls_nodes;	/* current nodes in RC */
	uint32_t ls_num_nodes;	/* number of nodes in RC */
	uint32_t ls_nodes_mask;
	uint32_t ls_low_nodeid;

	int ls_state;		/* state changes for recovery */
	struct list_head ls_recover;	/* gr_recover_t structs */
	int ls_last_stop;	/* event ids from sm */
	int ls_last_start;
	int ls_last_finish;
	spinlock_t ls_recover_lock;
	struct list_head ls_nodes_gone;	/* dead node list for recovery */

	wait_queue_head_t ls_wait_general;

	gd_rcom_t *ls_rcom;
	uint32_t ls_rcom_msgid;
	struct semaphore ls_rcom_lock;

	struct list_head ls_recover_list;
	int ls_recover_list_count;
	spinlock_t ls_recover_list_lock;

	struct rw_semaphore ls_in_recovery;	/* held in write during
						 * recovery, read for normal
						 * locking ops */
	struct rw_semaphore ls_unlock_sem;	/* To prevent unlock on a
						 * parent lock racing with a
						 * new child lock */

	struct rw_semaphore ls_rec_rsblist;	/* To prevent incoming recovery
						 * operations happening while
						 * we are purging */

	struct rw_semaphore ls_gap_rsblist;	/* To protect rootres list
						 * in grant_after_purge() which
						 * runs outside recovery */

	struct list_head ls_rebuild_rootrsb_list;	/* Root of lock trees
							 * we are deserialising
							 */

	struct list_head ls_deadlockq;	/* List of locks in conversion ordered
					 * by duetime. for deadlock detection */

	struct list_head ls_requestqueue;	/* List of incoming requests
						 * held while we are in
						 * recovery */

	gd_resdir_bucket_t ls_resdir_hash[RESDIRHASH_SIZE];

	int ls_namelen;
	char ls_name[1];	/* <namelen> bytes */
};

/*
 * Cluster node (per node in cluster)
 */

struct gd_node {
	struct list_head gn_list;	/* global list of cluster nodes */
	uint32_t gn_nodeid;	/* cluster unique nodeid (cman) */
	uint32_t gn_ipaddr;	/* node's first IP address (cman) */
	int gn_refcount;	/* number of csb's referencing */
};

/*
 * Cluster System Block (per node in a ls)
 */

struct gd_csb {
	struct list_head csb_list;	/* per-lockspace list of nodes */
	gd_node_t *csb_node;	/* global node structure */
	int csb_gone_event;	/* event id when node was removed */

	uint32_t csb_names_send_count;
	uint32_t csb_names_send_msgid;
	uint32_t csb_names_recv_count;
	uint32_t csb_names_recv_msgid;
	uint32_t csb_locks_send_count;
	uint32_t csb_locks_send_msgid;
	uint32_t csb_locks_recv_count;
	uint32_t csb_locks_recv_msgid;
};

/*
 * Resource block
 */

/* status */

#define GDLM_RESSTS_DIRENTRY     1	/* This is a directory entry */
#define GDLM_RESSTS_LVBINVALID   2	/* The LVB is invalid */

#define RESFL_NEW_MASTER         (0)
#define RESFL_RECOVER_LIST       (1)

struct gd_res {
	struct list_head res_hashchain;	/* Chain of resources in this hash
					 * bucket */

	gd_ls_t *res_ls;	/* The owning lockspace */

	struct list_head res_rootlist;	/* List of root resources in lockspace */

	struct list_head res_subreslist;	/* List of all sub-resources
						 * for this root res. */
	/* This is a list head on the root res and holds the whole tree below
	 * it. */
	uint8_t res_depth;	/* Depth in resource tree */
	uint16_t res_status;
	unsigned long res_flags;	/* Flags, RESFL_ */

	struct list_head res_grantqueue;
	struct list_head res_convertqueue;
	struct list_head res_waitqueue;

	uint32_t res_nodeid;	/* nodeid of master node */

	gd_res_t *res_root;	/* If a subresource, this is our root */
	gd_res_t *res_parent;	/* Our parent resource (if any) */

	atomic_t res_ref;	/* No of lkb's */
	uint16_t res_remasterid;	/* ID used during remaster */
	struct list_head res_recover_list;	/* General list for use during
						 * recovery */
	int res_recover_msgid;
	int res_newlkid_expect;

	struct rw_semaphore res_lock;

	char *res_lvbptr;	/* Lock value block */

	uint8_t res_resdir_seq;	/* Last directory sequence number */

	uint8_t res_length;
	char res_name[1];	/* <res_length> bytes */
};

/*
 * Lock block. To avoid confusion, where flags mirror the
 * public flags, they should have the same value.
 */

#define GDLM_LKSTS_NEW          (0)
#define GDLM_LKSTS_WAITING      (1)
#define GDLM_LKSTS_GRANTED      (2)
#define GDLM_LKSTS_CONVERT      (3)

#define GDLM_LKFLG_VALBLK       (0x00000008)
#define GDLM_LKFLG_PERSISTENT   (0x00000080)	/* Don't unlock when process exits */
#define GDLM_LKFLG_NODLCKWT     (0x00000100)       /* Don't do deadlock detection */
#define GDLM_LKFLG_EXPEDITE     (0x00000400)       /* Move to head of convert queue */

/* Internal flags */
#define GDLM_LKFLG_RANGE        (0x00001000)	/* Range field is present (remote protocol only) */
#define GDLM_LKFLG_MSTCPY       (0x00002000)
#define GDLM_LKFLG_DELETED      (0x00004000)	/* LKB is being deleted */
#define GDLM_LKFLG_DELAST       (0x00008000)	/* Delete after delivering AST */
#define GDLM_LKFLG_LQRESEND     (0x00010000)	/* LKB on lockqueue must be resent */
#define GDLM_LKFLG_DEMOTED      (0x00020000)
#define GDLM_LKFLG_RESENT       (0x00040000)
#define GDLM_LKFLG_NOREBUILD    (0x00080000)
#define GDLM_LKFLG_LQCONVERT    (0x00100000)

struct gd_lkb {
	void *lkb_astaddr;
	void *lkb_bastaddr;
	long lkb_astparam;

	uint32_t lkb_flags;
	uint16_t lkb_status;	/* LKSTS_ granted, waiting, converting */
	int8_t lkb_rqmode;	/* Requested lock mode */
	int8_t lkb_grmode;	/* Granted lock mode */
	uint8_t lkb_bastmode;	/* Requested mode returned in bast */
	uint8_t lkb_highbast;	/* Highest mode we have sent a BAST for */
	uint32_t lkb_retstatus;	/* Status to return in lksb */

	uint32_t lkb_id;	/* Our lock ID */
	struct dlm_lksb *lkb_lksb;	/* Lock status block of caller */
	struct list_head lkb_idtbl_list;	/* list pointer into the
						 * lockidtbl */

	struct list_head lkb_statequeue;	/* List of locks in this state */

	struct list_head lkb_ownerqueue;	/* List of locks owned by a
						 * process */

	gd_lkb_t *lkb_parent;	/* Pointer to parent if any */

	atomic_t lkb_childcnt;	/* Number of children */

	struct list_head lkb_lockqueue;	/* For when we are on the lock queue */
	int lkb_lockqueue_state;
	int lkb_lockqueue_flags;	/* As passed into lock/unlock */
	unsigned long lkb_lockqueue_time;	/* Time we went on the lock
						 * queue */

	gd_res_t *lkb_resource;

	unsigned long lkb_duetime;	/* For deadlock detection */

	uint32_t lkb_remid;	/* Remote partner */
	uint32_t lkb_nodeid;

	struct list_head lkb_astqueue;	/* For when we are on the AST queue */
	uint32_t lkb_asts_to_deliver;

	struct gd_remlockrequest *lkb_request;

	struct list_head lkb_deadlockq;	/* on ls_deadlockq list */

	char *lkb_lvbptr;	/* Points to lksb on a local lock, allocated
				 * LVB (if necessary) on a remote lock */
	uint64_t *lkb_range;	/* Points to an array of 64 bit numbers that
				 * represent the requested and granted ranges
				 * of the lock. NULL implies 0-ffffffffffffffff
				 */
};

/*
 * Used to save and manage recovery state for a lockspace.
 */

struct gd_recover {
	struct list_head gr_list;
	uint32_t *gr_nodeids;
	int gr_node_count;
	int gr_event_id;
};

/*
 * Header part of the mid-level comms system. All packets start with
 * this header so we can identify them. The comms packet can
 * contain many of these structs but the are split into individual
 * work units before being passed to the lockqueue routines.
 * below this are the structs that this is a header for
 */

struct gd_req_header {
	uint8_t rh_cmd;		/* What we are */
	uint8_t rh_flags;	/* maybe just a pad */
	uint16_t rh_length;	/* Length of struct (so we can send several in
				 * one message) */
	uint32_t rh_lkid;	/* Lock ID tag: ie the local (requesting) lock
				 * ID */
	uint32_t rh_lockspace;	/* Lockspace ID */
};

/*
 * This is the struct used in a remote lock/unlock/convert request
 * The mid-level comms API should turn this into native byte order.
 * Most "normal" lock operations will use these two structs for
 * communications. Recovery operations use their own structs
 * but still with the gd_req_header on the front.
 */

struct gd_remlockrequest {
	struct gd_req_header rr_header;

	uint32_t rr_remlkid;	/* Remote lock ID */
	uint32_t rr_remparid;	/* Parent's remote lock ID or 0 */
	uint32_t rr_flags;	/* Flags from lock/convert request */
        uint64_t rr_range_start;/* Yes, these are in the right place... */
	uint64_t rr_range_end;
	uint32_t rr_status;	/* Status to return if this is an AST request */
	uint8_t rr_rqmode;	/* Requested lock mode */
	uint8_t rr_asts;	/* Whether the LKB has ASTs or not */
	uint8_t rr_resdir_seq;	/* Directory sequence number */
	char rr_lvb[DLM_LVB_LEN];	/* Value block */
	char rr_name[1];	/* As long as needs be. Only used for directory
				 * lookups. The length of this can be worked
				 * out from the packet length */
};

/*
 * This is the struct returned by a remote lock/unlock/convert request
 * The mid-level comms API should turn this into native byte order.
 */

struct gd_remlockreply {
	struct gd_req_header rl_header;

	uint32_t rl_lockstate;	/* Whether request was queued/granted/waiting */
	uint32_t rl_nodeid;	/* nodeid of lock master */
	uint32_t rl_status;	/* Status to return to caller */
	uint32_t rl_lkid;	/* Remote lkid */
	uint8_t rl_resdir_seq;	/* Returned directory sequence number */
	char rl_lvb[DLM_LVB_LEN];	/* LVB itself */
};

/*
 * Recovery comms message
 */

struct gd_rcom {
	struct gd_req_header rc_header;	/* 32 byte aligned */
	uint32_t rc_msgid;
	uint16_t rc_datalen;
	uint8_t rc_expanded;
	uint8_t rc_subcmd;	/* secondary command */
	char rc_buf[1];		/* first byte of data goes here and extends
				 * beyond here for another datalen - 1 bytes.
				 * rh_length is set to sizeof(gd_rcom_t) +
				 * datalen - 1 */
};


/* A remote query: GDLM_REMCMD_QUERY */
struct gd_remquery {
	struct gd_req_header rq_header;

	uint32_t rq_mstlkid;   /* LockID on master node */
        uint32_t rq_query;     /* query from the user */
        uint32_t rq_maxlocks;  /* max number of locks we can cope with */
};

/* First block of a reply query.  cmd = GDLM_REMCMD_QUERY */
/* There may be subsequent blocks of
   lock info in GDLM_REMCMD_QUERYCONT messages which just have
   a normal header. The last of these will have rh_flags set to
   GDLM_REMFLAG_ENDQUERY
 */
struct gd_remqueryreply {
	struct gd_req_header rq_header;

        uint32_t rq_numlocks;  /* Number of locks in reply */
        uint32_t rq_startlock; /* Which lock this block starts at (for multiple block replies) */
        uint32_t rq_status;

        /* Resource information */
	uint32_t rq_grantcount;	/* No. of nodes on grant queue */
	uint32_t rq_convcount;	/* No. of nodes on convert queue */
	uint32_t rq_waitcount;	/* No. of nodes on wait queue */
        char rq_valblk[DLM_LVB_LEN];	/* Master's LVB contents, if applicable */
};

/*
 * Lockqueue wait lock states
 */

#define GDLM_LQSTATE_WAIT_RSB       1
#define GDLM_LQSTATE_WAIT_CONVERT   2
#define GDLM_LQSTATE_WAIT_CONDGRANT 3
#define GDLM_LQSTATE_WAIT_UNLOCK    4

/* Commands sent across the comms link */
#define GDLM_REMCMD_LOOKUP          1
#define GDLM_REMCMD_LOCKREQUEST     2
#define GDLM_REMCMD_UNLOCKREQUEST   3
#define GDLM_REMCMD_CONVREQUEST     4
#define GDLM_REMCMD_LOCKREPLY       5
#define GDLM_REMCMD_LOCKGRANT       6
#define GDLM_REMCMD_SENDBAST        7
#define GDLM_REMCMD_SENDCAST        8
#define GDLM_REMCMD_REM_RESDATA     9
#define GDLM_REMCMD_RECOVERMESSAGE  20
#define GDLM_REMCMD_RECOVERREPLY    21
#define GDLM_REMCMD_QUERY           30
#define GDLM_REMCMD_QUERYREPLY      31

/* Set in rh_flags when this is the last block of
   query information. Note this could also be the first
   block */
#define GDLM_REMFLAG_ENDQUERY       1

/*
 * This is a both a parameter to queue_ast and also the bitmap of ASTs in
 * lkb_asts_to_deliver
 */

typedef enum { GDLM_QUEUE_COMPAST = 1, GDLM_QUEUE_BLKAST = 2 } gd_ast_type_t;

#ifndef BUG_ON
#define BUG_ON(x)
#endif

void dlm_debug_log(gd_ls_t *ls, const char *fmt, ...);
void dlm_debug_dump(void);

#endif				/* __DLM_INTERNAL_DOT_H__ */
