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

#ifndef GULM_DOT_H
#define GULM_DOT_H

#define GULM_RELEASE_NAME "v6.0.0"

#ifdef MODVERSIONS
#include <linux/modversions.h>
#endif				/*  MODVERSIONS  */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <asm/uaccess.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>
#include <linux/config.h>
#include <linux/version.h>
#include <linux/smp_lock.h>
#include <linux/ctype.h>
#include <linux/string.h>

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

#include <linux/list.h>

#undef MAX
#define MAX(a,b) ((a>b)?a:b)

#undef MIN
#define MIN(a,b) ((a<b)?a:b)

/*  Extern Macro  */

#ifndef EXTERN
#define EXTERN extern
#define INIT(X)
#else
#undef EXTERN
#define EXTERN
#define INIT(X) =X
#endif

/*  Static Macro  */
#ifndef DEBUG_SYMBOLS
#define STATIC static
#else
#define STATIC
#endif

/*  Divide x by y.  Round up if there is a remainder.  */
#define DIV_RU(x, y) (((x) + (y) - 1) / (y))

#include <linux/lm_interface.h>

#include "gulm_prints.h"

#include "libgulm.h"

#include "handler.h"

/* Some fixed length constants.
 * Some of these should be made dynamic in size in the future.
 */
#define GIO_KEY_SIZE  (48)
#define GIO_LVB_SIZE  (32)
#define GIO_NAME_SIZE (32)
#define GIO_NAME_LEN  (GIO_NAME_SIZE-1)

/* What we know about this filesytem */
struct gulm_fs_s {
	struct list_head fs_list;
	char fs_name[GIO_NAME_SIZE];	/* lock table name */

	lm_callback_t cb;	/* file system callback function */
	lm_fsdata_t *fsdata;	/* private file system data */

	callback_qu_t cq;

	uint32_t fsJID;
	uint32_t lvb_size;

	struct semaphore get_lock;	/* I am not 100% sure this is needed.
					 * But it only hurts performance,
					 * not correctness if it is
					 * useless.  Sometime post52, need
					 * to investigate.
					 */

	/* Stuff for the first mounter lock and state */
	int firstmounting;
	/* the recovery done func needs to behave slightly differnt when we are
	 * the first node in an fs.
	 */

	void *mountlock;	/* this lock holds the Firstmounter state of the FS */
	/* this is because all lock traffic is async, and really at this point
	 * in time we want a sync behavor, so I'm left with doing something to
	 * achive that.
	 *
	 * this works, but it is crufty, but I don't want to build a huge
	 * queuing system for one lock that we touch twice at the beginning and
	 * once on the end.
	 *
	 * I should change the firstmounter lock to work like the journal locks
	 * and the node locks do.  Things are a lot cleaner now with the libgulm
	 * interface than before. (when the firstmounter lock code was written)
	 */
	struct completion sleep;

	/* Stuff for JID mapping locks */
	uint32_t JIDcount;	/* how many JID locks are there. */
};
typedef struct gulm_fs_s gulm_fs_t;

/* What we know about each locktable.
 * only one now-a-days. (the LTPX)
 * */
typedef struct lock_table_s {
	uint32_t magic_one;

	int running;
	struct task_struct *recver_task;
	struct completion startup;
	struct semaphore sender;

	struct task_struct *sender_task;
	wait_queue_head_t send_wchan;
	spinlock_t queue_sender;
	struct list_head to_be_sent;

	int hashbuckets;
	spinlock_t *hshlk;
	struct list_head *lkhsh;

	/* stats
	 * it may be wise to make some of these into atomic numbers.
	 * or something.  or not.
	 * */
	uint32_t locks_total;
	uint32_t locks_unl;
	uint32_t locks_exl;
	uint32_t locks_shd;
	uint32_t locks_dfr;
	uint32_t locks_lvbs;
	atomic_t locks_pending;
	/* cannot count expired here. clients don't know this */

	uint32_t lops;		/* just incr on each op */

} lock_table_t;

typedef struct gulm_cm_s {
	uint8_t myName[64];
	uint8_t clusterID[256]; /* doesn't need to be 256. */
	uint8_t loaded;		/* True|False whether we grabbed the config data */
	uint8_t starts;

	uint32_t handler_threads;	/* howmany to have */
	uint32_t verbosity;

	uint64_t GenerationID;

	lock_table_t ltpx;

	gulm_interface_p hookup;

} gulm_cm_t;

/* things about each lock. */
typedef struct gulm_lock_s {
	struct list_head gl_list;
	atomic_t count;

	uint32_t magic_one;
	gulm_fs_t *fs;		/* which filesystem we belong to. */
	uint8_t key[GIO_KEY_SIZE];
	uint16_t keylen;
	uint8_t last_suc_state;	/* last state we succesfully got. */
	char *lvb;

	/* this is true when there is a lock request sent out for this lock.
	 * All it really means is that if we've lost the master, and reconnect
	 * to another, this lock needs to have it's request resent.
	 *
	 * This now has two stages.  Since a lock could be pending, but still in
	 * the send queue.  So we don't want to resend requests that haven't
	 * been sent yet.
	 *
	 * we don't handle the master losses here any more.  LTPX does that for
	 * us.  Should consider removing the dupicated code then.
	 */
	int actuallypending;	/* may need to be atomic */
	int in_to_be_sent;

	enum { glck_nothing, glck_action, glck_state } req_type;
	/* these three for the lock req.  We save them here so we can rebuild
	 * the lock request if there was a server failover. (?still needed?)
	 */
	unsigned int cur_state;
	unsigned int req_state;
	unsigned int flags;

	/* these three for actions. First is the action, next is result, last is
	 * what threads wait on for the reply.
	 */
	int action;
	int result;		/* ok, both are using this. */
	struct completion actsleep;

} gulm_lock_t;

/*****************************************************************************/
/* cross pollenate prototypes */

/* from gulm_lt.c */
int pack_lock_key(uint8_t *key, uint16_t keylen, uint8_t type,
		uint8_t *fsname, uint8_t *pk, uint8_t pklen);
void lt_logout (void);
int lt_login (void);
int get_mount_lock (gulm_fs_t * fs, int *first);
int downgrade_mount_lock (gulm_fs_t * fs);
int drop_mount_lock (gulm_fs_t * fs);
int send_drop_all_exp (lock_table_t * lt);
int send_drop_exp (gulm_fs_t * fs, lock_table_t * lt, char *name);

/*from gulm_core.c */
void cm_logout (void);
int cm_login (void);
void delete_ipnames (struct list_head *namelist);

/* from gulm_fs.c */
void init_gulm_fs (void);
void request_journal_replay (uint8_t * name);
void passup_droplocks (void);
gulm_fs_t *get_fs_by_name (uint8_t * name);
void dump_internal_lists (void);
void gulm_recovery_done (lm_lockspace_t * lockspace,
			 unsigned int jid, unsigned int message);
void gulm_unmount (lm_lockspace_t * lockspace);
void gulm_others_may_mount (lm_lockspace_t * lockspace);
int gulm_mount (char *table_name, char *host_data,
		lm_callback_t cb, lm_fsdata_t * fsdata,
		unsigned int min_lvb_size, struct lm_lockstruct *lockstruct);

extern struct lm_lockops gulm_ops;

#endif				/*  GULM_DOT_H  */
/* vim: set ai cin noet sw=8 ts=8 : */
