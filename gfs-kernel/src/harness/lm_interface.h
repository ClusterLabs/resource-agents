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

/*

   Sooner or later, I need to put all the documentation back into this file.
   In the mean time, here are some notes.

   -  The lock module is now responsible for STOMITHing the an expired
   client before calling the callback with type LM_CB_NEED_RECOVERY.

   -  If mount() operation returns first == TRUE, GFS will check all the
   journals.  GFS itself can't/shouldn't stomith the machines, so the lock module
   needs to make sure that there are no zombie machines on any of the
   journals.  (i.e. this should probably be on the first mount of the lock
   space where all mounts by other machines are blocked.)  GFS will call
   others_may_mount() when the filesystem is in a consistent state.

   -  GFS can issue multiple simultaneous get_lock()s for the same lockname.
   The lock module needs to deal with it, either by 1)  building a hash table
   to lookup the structures and keeping a reference count so there is only
   on lm_lock_t for a given lockname. or 2) just dealing with multiple 
   lm_lock_t structures for a given lockname.

*/

#ifndef __LM_INTERFACE_DOT_H__
#define __LM_INTERFACE_DOT_H__

/*
 * Lock-module- or filesystem-specific opaque handles for various things.
 * Lock module gives lockspace and lock handles to GFS, which uses them
 *   to identify LM instance and lock when calling LM functions.
 * GFS gives fsdata to lock module, so LM can identify filesystem instance
 *   when doing callbacks to GFS.
 */
typedef void lm_lockspace_t;  /* lockspace; lock module instance structure */
typedef void lm_lock_t;       /* lock module's internal lock structure */
typedef void lm_fsdata_t;     /* filesystem; GFS instance (superblock struct) */

typedef void (*lm_callback_t) (lm_fsdata_t *fsdata, unsigned int type,
			       void *data);

/*
 * Flags for the struct lm_lockstruct->ls_flags field.
 * The nolock module is useful for single-node mounting of GFS; it sets the
 *   LOCAL flag to allow GFS to perform caching and other optimizations that
 *   it cannot do when in a cluster.
 */

#define LM_LSFLAG_LOCAL        (0x00000001) /* Local filesystem (no locks) */

/* Lock types */

#define LM_TYPE_RESERVED       (0x00)
#define LM_TYPE_NONDISK        (0x01)  /* Non-disk cluster-wide, e.g. TRANS */
#define LM_TYPE_INODE          (0x02)  /* Inode, e.g. files */
#define LM_TYPE_RGRP           (0x03)  /* Resource Group (block allocation) */
#define LM_TYPE_META           (0x04)  /* Metadata, e.g. superblock, journals */
#define LM_TYPE_IOPEN          (0x05)
#define LM_TYPE_FLOCK          (0x06)  /* Linux file lock */
#define LM_TYPE_PLOCK          (0x07)  /* POSIX file lock */
#define LM_TYPE_QUOTA          (0x08)  /* User or group block usage quota */

/* States passed to lock() */

#define LM_ST_UNLOCKED         (0)  /* Not locked */
#define LM_ST_EXCLUSIVE        (1)  /* Allow writes */
#define LM_ST_DEFERRED         (2)  /* Locking deferred to application level */
#define LM_ST_SHARED           (3)  /* Allow reads, protected from writes */

/* Flags passed to lock() */

#define LM_FLAG_TRY            (0x00000001) /* Don't block if not immediately
                                             *   grantable */
#define LM_FLAG_TRY_1CB        (0x00000002) /* Don't block if not grantable
                                             *   after request to other node */
#define LM_FLAG_NOEXP          (0x00000004) /* Don't grant if held by expired
                                             *   (crashed/dead) node */
#define LM_FLAG_ANY            (0x00000008) /* Grant if lock state is any
                                             *   other than LM_ST_UNLOCKED */
#define LM_FLAG_PRIORITY       (0x00000010) /* High priority lock request,
                                             *   put at top of wait queue */

/* Flags returned by lock() */

#define LM_OUT_ST_MASK         (0x00000003) /* 4-state LM_ST_XX mask */
#define LM_OUT_CACHEABLE       (0x00000004)
#define LM_OUT_CANCELED        (0x00000008) /* Lock request was cancelled */
#define LM_OUT_ASYNC           (0x00000080)

/* Callback types */

#define LM_CB_NEED_E           (257)  /* Other node needs EXCLUSIVE lock */
#define LM_CB_NEED_D           (258)  /* Other node needs DEFERRED lock */
#define LM_CB_NEED_S           (259)  /* Other node needs SHARED lock */
#define LM_CB_NEED_RECOVERY    (260)  /* A node crashed, needs jrnl recovery */
#define LM_CB_DROPLOCKS        (261)  /* Locking system running out of space */
#define LM_CB_ASYNC            (262)  /* Asynchronous lock request results */

/* Reset_exp messages */

#define LM_RD_GAVEUP           (308)
#define LM_RD_SUCCESS          (309)

struct lm_lockname {
	uint64_t ln_number;           /* Lock number */
	unsigned int ln_type;         /* LM_TYPE_XXX lock type */
};

#define lm_name_equal(name1, name2) \
(((name1)->ln_number == (name2)->ln_number) && \
 ((name1)->ln_type == (name2)->ln_type)) \

struct lm_async_cb {
	struct lm_lockname lc_name;
	int lc_ret;
};

struct lm_lockstruct;

/*
 * Operations that form the interface between GFS' glock layer
 * and the lock plug-in module that supports inter-node locks.
 */
struct lm_lockops {
	char lm_proto_name[256];

	/*
	 * Mount/Unmount
	 */

	/* Mount the lock module on lock harness, so GFS can use it */
	int (*lm_mount) (char *table_name, char *host_data,
			 lm_callback_t cb, lm_fsdata_t *fsdata,
			 unsigned int min_lvb_size,
			 struct lm_lockstruct *lockstruct);

	/* We've completed mount operations for this GFS filesystem/lockspace,
	     other nodes may now mount it */
	void (*lm_others_may_mount) (lm_lockspace_t *lockspace);

	/* Unmount the lock module */
	void (*lm_unmount) (lm_lockspace_t *lockspace);

	/* Abnormal unmount */
	void (*lm_withdraw) (lm_lockspace_t *lockspace);

	/*
	 * Lock oriented operations
	 */

	/* Find or create structures, etc. for lock (but don't lock it yet) */
	int (*lm_get_lock) (lm_lockspace_t *lockspace,
			    struct lm_lockname *name, lm_lock_t **lockp);

	/* Done with structure */
	void (*lm_put_lock) (lm_lock_t *lock);

	/* Lock inter-node lock in requested state */
	unsigned int (*lm_lock) (lm_lock_t *lock, unsigned int cur_state,
				 unsigned int req_state, unsigned int flags);

	/* Unlock inter-node lock */
	unsigned int (*lm_unlock) (lm_lock_t *lock, unsigned int cur_state);

	/* Cancel a lock request */
	void (*lm_cancel) (lm_lock_t *lock);

	/* Lock Value Block operations */
	int (*lm_hold_lvb) (lm_lock_t *lock, char **lvbp);
	void (*lm_unhold_lvb) (lm_lock_t *lock, char *lvb);

	/* Make new LVB contents visible to other nodes */
	void (*lm_sync_lvb) (lm_lock_t *lock, char *lvb);

	/*
	 * Posix Lock oriented operations
	 */

	int (*lm_plock_get) (lm_lockspace_t *lockspace,
			     struct lm_lockname *name,
			     struct file *file, struct file_lock *fl);

	int (*lm_plock) (lm_lockspace_t *lockspace,
			 struct lm_lockname *name,
			 struct file *file, int cmd, struct file_lock *fl);

	int (*lm_punlock) (lm_lockspace_t *lockspace,
			   struct lm_lockname *name,
			   struct file *file, struct file_lock *fl);

	/*
	 * Client oriented operations
	 */

	/* This node has completed journal recovery for a crashed node */
	void (*lm_recovery_done) (lm_lockspace_t *lockspace, unsigned int jid,
				  unsigned int message);

	struct module *lm_owner;
};

/*
 * GFS passes this structure to the lock module to fill when mounting.
 */
struct lm_lockstruct {
	unsigned int ls_jid;           /* Journal ID # for this node */
	unsigned int ls_first;         /* This node is first to mount this FS */
	unsigned int ls_lvb_size;      /* Size (bytes) of Lock Value Block */
	lm_lockspace_t *ls_lockspace;  /* Lock module instance handle */
	struct lm_lockops *ls_ops;     /* Pointers to functions in lock module*/
	int ls_flags;                  /* LM_LSFLAG_XXX, e.g. local filesystem*/
};

/*
 * Lock Module Bottom interface.
 * Each lock module makes itself known or unknown to the lock harness
 *   via these functions.
 */

int lm_register_proto(struct lm_lockops *proto);
void lm_unregister_proto(struct lm_lockops *proto);

/*
 * Lock Module Top interface.
 * GFS calls these functions to mount or unmount a particular lock module.
 */

int lm_mount(char *proto_name,
	     char *table_name, char *host_data,
	     lm_callback_t cb, lm_fsdata_t *fsdata,
	     unsigned int min_lvb_size, struct lm_lockstruct *lockstruct);
void lm_unmount(struct lm_lockstruct *lockstruct);
void lm_withdraw(struct lm_lockstruct *lockstruct);

#endif /* __LM_INTERFACE_DOT_H__ */
