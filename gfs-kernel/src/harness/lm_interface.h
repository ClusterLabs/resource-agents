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

typedef void lm_lockspace_t;
typedef void lm_lock_t;
typedef void lm_fsdata_t;
typedef void (*lm_callback_t) (lm_fsdata_t *fsdata, unsigned int type,
			       void *data);

/* Flags for the struct lm_lockstruct->ls_flags field */

#define LM_LSFLAG_LOCAL        (0x00000001)
#define LM_LSFLAG_ASYNC        (0x00000002)

/* Lock types */

#define LM_TYPE_RESERVED       (0x00)
#define LM_TYPE_NONDISK        (0x01)
#define LM_TYPE_INODE          (0x02)
#define LM_TYPE_RGRP           (0x03)
#define LM_TYPE_META           (0x04)
#define LM_TYPE_IOPEN          (0x05)
#define LM_TYPE_FLOCK          (0x06)
#define LM_TYPE_PLOCK          (0x07)
#define LM_TYPE_QUOTA          (0x08)

/* States passed to lock() */

#define LM_ST_UNLOCKED         (0)
#define LM_ST_EXCLUSIVE        (1)
#define LM_ST_DEFERRED         (2)
#define LM_ST_SHARED           (3)

/* Flags passed to lock() */

#define LM_FLAG_TRY            (0x00000001)
#define LM_FLAG_TRY_1CB        (0x00000002)
#define LM_FLAG_NOEXP          (0x00000004)
#define LM_FLAG_ANY            (0x00000008)
#define LM_FLAG_PRIORITY       (0x00000010)

/* Flags returned by lock() */

#define LM_OUT_ST_MASK         (0x00000003)
#define LM_OUT_CACHEABLE       (0x00000004)
#define LM_OUT_CANCELED        (0x00000008)
#define LM_OUT_NEED_E          (0x00000010)
#define LM_OUT_NEED_D          (0x00000020)
#define LM_OUT_NEED_S          (0x00000040)
#define LM_OUT_ASYNC           (0x00000080)
#define LM_OUT_LVB_INVALID     (0x00000100)

/* Callback types */

#define LM_CB_NEED_E           (257)
#define LM_CB_NEED_D           (258)
#define LM_CB_NEED_S           (259)
#define LM_CB_NEED_RECOVERY    (260)
#define LM_CB_DROPLOCKS        (261)
#define LM_CB_ASYNC            (262)

/* Reset_exp messages */

#define LM_RD_GAVEUP           (308)
#define LM_RD_SUCCESS          (309)

struct lm_lockname {
	uint64_t ln_number;
	unsigned int ln_type;
};

#define lm_name_equal(name1, name2) \
(((name1)->ln_number == (name2)->ln_number) && \
 ((name1)->ln_type == (name2)->ln_type)) \

struct lm_async_cb {
	struct lm_lockname lc_name;
	int lc_ret;
};

struct lm_lockstruct;

struct lm_lockops {
	char lm_proto_name[256];

	/* Mount/Unmount */

	int (*lm_mount) (char *table_name, char *host_data,
			 lm_callback_t cb, lm_fsdata_t *fsdata,
			 unsigned int min_lvb_size,
			 struct lm_lockstruct *lockstruct);
	void (*lm_others_may_mount) (lm_lockspace_t *lockspace);
	void (*lm_unmount) (lm_lockspace_t *lockspace);

	/* Lock oriented operations */

	int (*lm_get_lock) (lm_lockspace_t *lockspace,
			    struct lm_lockname *name, lm_lock_t **lockp);
	void (*lm_put_lock) (lm_lock_t *lock);

	unsigned int (*lm_lock) (lm_lock_t *lock, unsigned int cur_state,
				 unsigned int req_state, unsigned int flags);
	unsigned int (*lm_unlock) (lm_lock_t *lock, unsigned int cur_state);

	void (*lm_cancel) (lm_lock_t *lock);

	int (*lm_hold_lvb) (lm_lock_t *lock, char **lvbp);
	void (*lm_unhold_lvb) (lm_lock_t *lock, char *lvb);
	void (*lm_sync_lvb) (lm_lock_t *lock, char *lvb);

	/* Posix Lock oriented operations  */

	int (*lm_plock_get) (lm_lockspace_t *lockspace,
			     struct lm_lockname *name, unsigned long owner,
			     uint64_t *start, uint64_t *end, int *exclusive,
			     unsigned long *rowner);

	int (*lm_plock) (lm_lockspace_t *lockspace,
			 struct lm_lockname *name, unsigned long owner,
			 int wait, int exclusive, uint64_t start,
			 uint64_t end);

	int (*lm_punlock) (lm_lockspace_t *lockspace,
			   struct lm_lockname *name, unsigned long owner,
			   uint64_t start, uint64_t end);

	/* Client oriented operations */

	void (*lm_recovery_done) (lm_lockspace_t *lockspace, unsigned int jid,
				  unsigned int message);

	struct module *lm_owner;
};

struct lm_lockstruct {
	unsigned int ls_jid;
	unsigned int ls_first;
	unsigned int ls_lvb_size;
	lm_lockspace_t *ls_lockspace;
	struct lm_lockops *ls_ops;
	int ls_flags;
};

/* Bottom interface */

int lm_register_proto(struct lm_lockops *proto);
void lm_unregister_proto(struct lm_lockops *proto);

/* Top interface */

int lm_mount(char *proto_name,
	     char *table_name, char *host_data,
	     lm_callback_t cb, lm_fsdata_t *fsdata,
	     unsigned int min_lvb_size, struct lm_lockstruct *lockstruct);
void lm_unmount(struct lm_lockstruct *lockstruct);

#endif /* __LM_INTERFACE_DOT_H__ */
