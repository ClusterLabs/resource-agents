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

#define GULM_RELEASE_NAME "<CVS>"

/* uh, do I need all of these headers? */
#ifdef MODVERSIONS
#include <linux/modversions.h>
#endif				/*  MODVERSIONS  */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>
#include <linux/config.h>
#include <linux/version.h>
#include <linux/smp_lock.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/fs.h>

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


#undef MAX
#define MAX(a,b) ((a>b)?a:b)

#undef MIN
#define MIN(a,b) ((a<b)?a:b)

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
#define GULM_CRC_INIT (0x6d696b65)

/* a hash bucket
 * this puts the bucket list and the spinlock for the list next to each
 * other.  Mostly because it makes things nice and easy if we're on a
 * nonsmp machine and the spinlocks don't exist.  (previously I tried to
 * malloc nothing.  kernel wasn't happy about that.)
 *
 * An array of these makes a hash table.
 */
struct gulm_hash_bucket_s {
	struct list_head bucket;
	spinlock_t lock;
};
typedef struct gulm_hash_bucket_s gulm_hb_t;

/* What we know about this filesytem */
struct gulm_fs_s {
	struct list_head fs_list;
	char fs_name[GIO_NAME_SIZE];	/* lock table name */

	lm_callback_t cb;	/* file system callback function */
	lm_fsdata_t *fsdata;	/* private file system data */

	callback_qu_t cq;

	uint32_t fsJID;
	uint32_t lvb_size;

	/* Stuff for the first mounter lock and state */
	int firstmounting;
	/* the recovery done func needs to behave slightly differnt when we are
	 * the first node in an fs.
	 */

	/* Stuff for JID mapping locks */
	uint32_t JIDcount;	/* how many JID locks are there. */
	struct semaphore headerlock;
};
typedef struct gulm_fs_s gulm_fs_t;

typedef struct gulm_cm_s {
	uint8_t myName[64];
	uint8_t clusterID[256]; /* doesn't need to be 256. */
	uint8_t starts;

	uint32_t handler_threads;	/* howmany to have */
	uint32_t verbosity;

	uint64_t GenerationID;

	/* lm interface pretty much requires that we maintian a table of
	 * locks.  The way lvbs work is a prefect example of why.  As is
	 * the panic you get if you send a cb up about a lock that has been
	 * put away.
	 */
	gulm_hb_t *gfs_lockmap;

	gulm_interface_p hookup;

} gulm_cm_t;

/* things about each lock. */
typedef struct gulm_lock_s {
   struct list_head gl_list;
   atomic_t count; /* gfs can call multiple gets and puts for same lock. */

   uint8_t *key;
   uint16_t keylen;
   gulm_fs_t *fs; /* which fs we belong to */
   char *lvb;
   int cur_state; /* for figuring out what reply to tell gfs. */
} gulm_lock_t;


/*****************************************************************************/
/* cross pollenate prototypes */

/* from gulm_firstlock.c */
int get_mount_lock (gulm_fs_t * fs, int *first);
int downgrade_mount_lock (gulm_fs_t * fs);
int drop_mount_lock (gulm_fs_t * fs);

/* from gulm_lt.c */
int gulm_lt_init (void);
void gulm_lt_release(void);
int pack_lock_key(uint8_t *key, uint16_t keylen, uint8_t type,
		uint8_t *fsname, uint8_t *pk, uint8_t pklen);
int pack_drop_mask(uint8_t *mask, uint16_t mlen, uint8_t *fsname);
void do_drop_lock_req (uint8_t *key, uint16_t keylen, uint8_t state);
int gulm_get_lock (lm_lockspace_t * lockspace, struct lm_lockname *name,
	       lm_lock_t ** lockp);
void gulm_put_lock (lm_lock_t * lock);
unsigned int gulm_lock (lm_lock_t * lock, unsigned int cur_state,
	   unsigned int req_state, unsigned int flags);
unsigned int gulm_unlock (lm_lock_t * lock, unsigned int cur_state);
void gulm_cancel (lm_lock_t * lock);
int gulm_hold_lvb (lm_lock_t * lock, char **lvbp);
void gulm_unhold_lvb (lm_lock_t * lock, char *lvb);
void gulm_sync_lvb (lm_lock_t * lock, char *lvb);

/* from gulm_plock.c */
int gulm_punlock (lm_lockspace_t * lockspace, struct lm_lockname *name,
	      struct file *file, struct file_lock *fl);
int gulm_plock (lm_lockspace_t *lockspace, struct lm_lockname *name,
		struct file *file, int cmd, struct file_lock *fl);
int gulm_plock_get (lm_lockspace_t * lockspace, struct lm_lockname *name,
		 struct file *file, struct file_lock *fl);

/*from gulm_core.c */
void cm_logout (void);
int cm_login (void);
void delete_ipnames (struct list_head *namelist);

/* from gulm_fs.c */
void init_gulm_fs (void);
void request_journal_replay (uint8_t * name);
void check_all_for_stales(void);
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
void gulm_withdraw (lm_lockspace_t * lockspace);

/* from gulm_jid.c */
void jid_fs_init (gulm_fs_t * fs);
void jid_fs_release (gulm_fs_t * fs);
void get_journalID (gulm_fs_t * fs);
int lookup_name_by_jid (gulm_fs_t * fs, uint32_t jid, uint8_t * name);
void release_JID (gulm_fs_t * fs, uint32_t jid);
void put_journalID (gulm_fs_t * fs, int leavebehind);
void check_for_stale_expires (gulm_fs_t * fs);

int find_jid_by_name_and_mark_replay (gulm_fs_t * fs, uint8_t * name, uint32_t * jid);

/* to be called from the lg_lock callbacks. */
void jid_header_lock_drop (uint8_t * key, uint16_t keylen);
void sig_watcher_lock_drop(uint8_t * key, uint16_t keylen);

/* from gulm_recsig.c */
void tap_sig(gulm_fs_t *fs, uint8_t *name, uint8_t len);
int watch_sig(gulm_fs_t *fs, uint8_t *name, uint8_t len,
		void(*func)(void *misc), void *misc);
void sig_watcher_init(void);

extern struct lm_lockops gulm_ops;

#endif				/*  GULM_DOT_H  */
/* vim: set ai cin noet sw=8 ts=8 : */
