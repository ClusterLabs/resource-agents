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
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/kobject.h>
#include <linux/kref.h>

#include "dlm.h"
#include "dlm_member.h"

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

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

struct dlm_ls;
struct dlm_lkb;
struct dlm_rsb;
struct dlm_node;
struct dlm_member;
struct dlm_lkbtable;
struct dlm_rsbtable;
struct dlm_dirtable;
struct dlm_direntry;
struct dlm_recover;
struct dlm_header;
struct dlm_message;
struct dlm_rcom;
struct dlm_mhandle;

#define log_print(fmt, args...) printk("dlm: "fmt"\n", ##args)
#define log_error(ls, fmt, args...) printk("dlm: %s: " fmt "\n", (ls)->ls_name, ##args)

#define DLM_DEBUG
#if defined(DLM_DEBUG)
#define log_debug(ls, fmt, args...) log_error(ls, fmt, ##args)
int dlm_create_debug_file(struct dlm_ls *ls);
void dlm_delete_debug_file(struct dlm_ls *ls);
#else
#define log_debug(ls, fmt, args...)
static inline int dlm_create_debug_file(struct dlm_ls *ls) { return 0; }
static inline void dlm_delete_debug_file(struct dlm_ls *ls) { return 0; }
#endif

#define DLM_DEBUG1
#if defined(DLM_DEBUG1)
#define log_debug1(ls, fmt, args...) log_error(ls, fmt, ##args)
#else
#define log_debug1(ls, fmt, args...)
#endif

#define DLM_DEBUG2
#if defined(DLM_DEBUG2)
#define log_debug2(fmt, args...) log_print(fmt, ##args)
#else
#define log_debug2(fmt, args...)
#endif

#define DLM_ASSERT(x, do) \
{ \
  if (!(x)) \
  { \
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
	struct list_head	toss;
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
	int			nodeid;
	int			weight;
	char			addr[DLM_ADDR_LEN];
};

/*
 * Lockspace member (per node in a ls)
 */

struct dlm_member {
	struct list_head	list;
	struct dlm_node *	node;
	int			gone_event;
};

/*
 * Used to save and manage recovery state for a lockspace.
 */

struct dlm_recover {
	struct list_head	list;
	int *			nodeids;
	int			node_count;
	int			event_id;
};

/*
 * Lock block
 *
 * A lock can be one of three types:
 *
 * local copy      lock is mastered locally
 *                 (lkb_nodeid is zero and DLM_LKF_MSTCPY is not set)
 * process copy    lock is mastered on a remote node
 *                 (lkb_nodeid is non-zero and DLM_LKF_MSTCPY is not set)
 * master copy     master node's copy of a lock owned by remote node
 *                 (lkb_nodeid is non-zero and DLM_LKF_MSTCPY is set)
 *
 * lkb_exflags: a copy of the most recent flags arg provided to dlm_lock or
 * dlm_unlock.  The dlm does not modify these or use any private flags in
 * this field; it only contains DLM_LKF_ flags from dlm.h.  These flags
 * are sent as-is to the remote master when the lock is remote.
 *
 * lkb_flags: internal dlm flags (DLM_IFL_ prefix) from dlm_internal.h.
 * Some internal flags are shared between the master and process nodes
 * (e.g. DLM_IFL_RETURNLVB); these shared flags are kept in the lower
 * two bytes.  One of these flags set on the master copy will be propagated
 * to the process copy and v.v.  Other internal flags are private to
 * the master or process node (e.g. DLM_IFL_MSTCPY).  These are kept in
 * the high two bytes.
 *
 * lkb_sbflags: status block flags.  These flags are copied directly into
 * the caller's lksb.sb_flags prior to the dlm_lock/dlm_unlock completion
 * ast.  All defined in dlm.h with DLM_SBF_ prefix.
 *
 * lkb_status: the lock status indicates which rsb queue the lock is
 * on, grant, convert, or wait.  DLM_LKSTS_ WAITING/GRANTED/CONVERT
 *
 * lkb_wait_type: the dlm message type (DLM_MSG_ prefix) for which a
 * reply is needed.  Only set when the lkb is on the lockspace waiters
 * list awaiting a reply from a remote node.
 *
 * lkb_nodeid: when the lkb is a local copy, nodeid is 0; when the lkb
 * is a master copy, nodeid specifies the remote lock holder, when the
 * lkb is a process copy, the nodeid specifies the lock master.
 */

/* lkb_ast_type */

#define AST_COMP		(1)
#define AST_BAST		(2)

/* lkb_range[] */

#define GR_RANGE_START		(0)
#define GR_RANGE_END		(1)
#define RQ_RANGE_START		(2)
#define RQ_RANGE_END		(3)

/* lkb_status */

#define DLM_LKSTS_WAITING	(1)
#define DLM_LKSTS_GRANTED	(2)
#define DLM_LKSTS_CONVERT	(3)

/* lkb_flags */

#define DLM_IFL_MSTCPY		(0x00010000)
#define DLM_IFL_RESEND		(0x00020000)
#define DLM_IFL_CONVERTING	(0x00040000)
#define DLM_IFL_RETURNLVB	(0x00000001)
#define DLM_IFL_RANGE		(0x00000002)

struct dlm_lkb {
	struct dlm_rsb *	lkb_resource;	/* the rsb */
	struct kref		lkb_ref;
	int			lkb_nodeid;	/* copied from rsb */
	int			lkb_ownpid;	/* pid of lock owner */
	uint32_t		lkb_id;		/* our lock ID */
	uint32_t		lkb_remid;	/* lock ID on remote partner */
	uint32_t		lkb_exflags;	/* external flags from caller */
	uint32_t		lkb_sbflags;	/* lksb flags */
	uint32_t		lkb_flags;	/* internal flags */
	uint32_t		lkb_lvbseq;	/* lvb sequence number */

	int8_t			lkb_status;     /* granted, waiting, convert */
	int8_t			lkb_rqmode;	/* requested lock mode */
	int8_t			lkb_grmode;	/* granted lock mode */
	int8_t			lkb_bastmode;	/* requested mode */
	int8_t			lkb_highbast;	/* highest mode bast sent for */

	int8_t			lkb_wait_type;	/* type of reply waiting for */
	int8_t			lkb_ast_type;	/* type of ast queued for */

	struct list_head	lkb_idtbl_list;	/* lockspace lkbtbl */
	struct list_head	lkb_statequeue;	/* rsb g/c/w list */
	struct list_head	lkb_rsb_lookup;	/* waiting for rsb lookup */
	struct list_head	lkb_wait_reply;	/* waiting for remote reply */
	struct list_head	lkb_astqueue;	/* need ast to be sent */

	uint64_t *		lkb_range;	/* array of gr/rq ranges */
	char *			lkb_lvbptr;
	struct dlm_lksb *       lkb_lksb;       /* caller's status block */
	void *			lkb_astaddr;	/* caller's ast function */
	void *			lkb_bastaddr;	/* caller's bast function */
	long			lkb_astparam;	/* caller's ast arg */

	/* parent/child locks not yet implemented */
#if 0
	struct dlm_lkb *	lkb_parent;	/* parent lkid */
	int			lkb_childcnt;	/* number of children */
#endif
};

/* find_rsb() flags */

#define R_MASTER		(1)     /* create/add rsb if not found */
#define R_CREATE		(2)     /* only return rsb if it's a master */

#define RESFL_MASTER_WAIT	(0)
#define RESFL_MASTER_UNCERTAIN	(1)
#define RESFL_VALNOTVALID	(2)
#define RESFL_VALNOTVALID_PREV	(3)

struct dlm_rsb {
	struct dlm_ls *		res_ls;		/* the lockspace */
	struct kref		res_ref;
	struct semaphore	res_sem;
	unsigned long		res_flags;	/* RESFL_ */
	int			res_length;	/* length of rsb name */
	int			res_nodeid;
	uint32_t                res_lvbseq;
	uint32_t		res_bucket;	/* rsbtbl */
	unsigned long		res_toss_time;
	uint32_t		res_trial_lkid;	/* lkb trying lookup result */
	struct list_head	res_lookup;	/* lkbs waiting lookup confirm*/
	struct list_head	res_hashchain;	/* rsbtbl */
	struct list_head	res_grantqueue;
	struct list_head	res_convertqueue;
	struct list_head	res_waitqueue;

	struct list_head	res_rootlist;	    /* used for recovery */
	struct list_head	res_recover_list;   /* used for recovery */
	int			res_remasterid;     /* used for recovery */
	int			res_recover_msgid;  /* used for recovery */
	int			res_newlkid_expect; /* used for recovery */

	char *			res_lvbptr;
	char			res_name[1];

	/* parent/child locks not yet implemented */
#if 0
	struct list_head	res_subreslist;	/* sub-rsbs for this root */
	struct dlm_rsb *	res_root;	/* root rsb if a subresource */
	struct dlm_rsb *	res_parent;	/* parent rsb (if any) */
	uint8_t			res_depth;	/* depth in resource tree */
#endif
};


#define LSST_NONE		(0)
#define LSST_INIT		(1)
#define LSST_INIT_DONE		(2)
#define LSST_CLEAR		(3)
#define LSST_WAIT_START		(4)
#define LSST_RECONFIG_DONE	(5)

#define LSFL_WORK		(0)
#define LSFL_LS_RUN		(1)
#define LSFL_LS_STOP		(2)
#define LSFL_LS_START		(3)
#define LSFL_LS_FINISH		(4)
#define LSFL_RCOM_READY		(5)
#define LSFL_FINISH_RECOVERY	(6)
#define LSFL_DIR_VALID		(7)
#define LSFL_ALL_DIR_VALID	(8)
#define LSFL_NODES_VALID	(9)
#define LSFL_ALL_NODES_VALID	(10)
#define LSFL_LS_TERMINATE	(11)
#define LSFL_JOIN_DONE		(12)
#define LSFL_LEAVE_DONE		(13)

struct dlm_ls {
	struct list_head	ls_list;	/* list of lockspaces */
	uint32_t		ls_global_id;	/* global unique lockspace ID */
	int			ls_count;	/* reference count */
	unsigned long		ls_flags;	/* LSFL_ */
	struct kobject		ls_kobj;

	struct dlm_rsbtable *	ls_rsbtbl;
	uint32_t		ls_rsbtbl_size;

	struct dlm_lkbtable *	ls_lkbtbl;
	uint32_t		ls_lkbtbl_size;

	struct dlm_dirtable *	ls_dirtbl;
	uint32_t		ls_dirtbl_size;

	struct list_head	ls_nodes;	/* current nodes in ls */
	struct list_head	ls_nodes_gone;	/* dead node list, recovery */
	int			ls_num_nodes;	/* number of nodes in ls */
	int			ls_low_nodeid;
	int *			ls_node_array;
	int *			ls_nodeids_next;
	int			ls_nodeids_next_count;

	struct dlm_rsb		ls_stub_rsb;	/* for returning errors */
	struct dlm_lkb		ls_stub_lkb;	/* for returning errors */

	struct dentry *		ls_debug_dentry;/* debugfs */
	struct list_head	ls_debug_list;	/* debugfs */

	/* recovery related */

	wait_queue_head_t	ls_wait_member;
	struct task_struct *	ls_recoverd_task;
	struct semaphore	ls_recoverd_active;
	struct list_head	ls_recover;	/* dlm_recover structs */
	spinlock_t		ls_recover_lock;
	int			ls_last_stop;
	int			ls_last_start;
	int			ls_last_finish;
	int			ls_startdone;
	int			ls_state;	/* recovery states */

	struct rw_semaphore	ls_in_recovery;	/* block local requests */
	struct list_head	ls_requestqueue;/* queue remote requests */
	struct semaphore	ls_requestqueue_lock;

	struct dlm_rcom *       ls_rcom;	/* recovery comms */
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


/* dlm_header is first element of all structs sent between nodes */

#define DLM_HEADER_MAJOR	(0x00020000)
#define DLM_HEADER_MINOR	(0x00000001)

#define DLM_MSG			(1)
#define DLM_RCOM		(2)

struct dlm_header {
	uint32_t		h_version;
	uint32_t		h_lockspace;
	uint32_t		h_nodeid;	/* nodeid of sender */
	uint16_t		h_length;
	uint8_t			h_cmd;		/* DLM_MSG, DLM_RCOM */
	uint8_t			h_pad;
};

#define DLM_MSG_REQUEST		(1)
#define DLM_MSG_CONVERT		(2)
#define DLM_MSG_UNLOCK		(3)
#define DLM_MSG_CANCEL		(4)
#define DLM_MSG_REQUEST_REPLY	(5)
#define DLM_MSG_CONVERT_REPLY	(6)
#define DLM_MSG_UNLOCK_REPLY	(7)
#define DLM_MSG_CANCEL_REPLY	(8)
#define DLM_MSG_GRANT		(9)
#define DLM_MSG_BAST		(10)
#define DLM_MSG_LOOKUP		(11)
#define DLM_MSG_REMOVE		(12)
#define DLM_MSG_LOOKUP_REPLY	(13)

struct dlm_message {
	struct dlm_header	m_header;
	uint32_t		m_type;		/* DLM_MSG_ */
	uint32_t		m_nodeid;
	uint32_t		m_pid;
	uint32_t		m_lkid;		/* lkid on sender */
	uint32_t		m_remlkid;	/* lkid on receiver */
	uint32_t		m_lkid_parent;
	uint32_t		m_remlkid_parent;
	uint32_t		m_exflags;
	uint32_t		m_sbflags;
	uint32_t		m_flags;
	uint32_t		m_lvbseq;
	int			m_status;
	int			m_grmode;
	int			m_rqmode;
	int			m_bastmode;
	int			m_asts;
	int			m_result;	/* 0 or -EXXX */
	char			m_lvb[DLM_LVB_LEN];
	uint64_t		m_range[2];
	char			m_name[0];
};

#define DIR_VALID		(1)
#define DIR_ALL_VALID		(2)
#define NODES_VALID		(4)
#define NODES_ALL_VALID		(8)

#define DLM_RCOM_STATUS		(1)
#define DLM_RCOM_NAMES		(2)
#define DLM_RCOM_LOOKUP		(3)
#define DLM_RCOM_LOCKS		(4)
#define DLM_RCOM_STATUS_REPLY	(5)
#define DLM_RCOM_NAMES_REPLY	(6)
#define DLM_RCOM_LOOKUP_REPLY	(7)
#define DLM_RCOM_LOCKS_REPLY	(8)

struct dlm_rcom {
	struct dlm_header	rc_header;
	uint32_t		rc_type;	/* DLM_RCOM_ */
	uint32_t		rc_msgid;
	uint32_t		rc_datalen;
	char			rc_buf[1];
};

#endif				/* __DLM_INTERNAL_DOT_H__ */
