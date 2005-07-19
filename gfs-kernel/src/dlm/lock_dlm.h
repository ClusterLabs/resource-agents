/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef LOCK_DLM_DOT_H
#define LOCK_DLM_DOT_H

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/socket.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/kobject.h>
#include <net/sock.h>

#include "dlm.h"
#include "../harness/lm_interface.h"

/*
 * Internally, we prefix things with gdlm_ and GDLM_ (for gfs-dlm) since a
 * prefix of lock_dlm_ gets awkward.  Externally, GFS refers to this module
 * as "lock_dlm".
 */

#define GDLM_STRNAME_BYTES	24
#define GDLM_LVB_SIZE		32
#define GDLM_DROP_COUNT		50000
#define GDLM_DROP_PERIOD	60

/* GFS uses 12 bytes to identify a resource (32 bit type + 64 bit number).
   We sprintf these numbers into a 24 byte string of hex values to make them
   human-readable (to make debugging simpler.) */

struct gdlm_strname {
	unsigned char		name[GDLM_STRNAME_BYTES];
	unsigned short		namelen;
};

#define DFL_BLOCK_LOCKS         0
#define DFL_JOIN_DONE		1
#define DFL_LEAVE_DONE		2
#define DFL_TERMINATE		3
#define DFL_SPECTATOR		4
#define DFL_WITHDRAW		5

struct gdlm_ls {
	int			jid;
	int			first;
	int			first_done;
	unsigned long		flags;
	struct kobject		kobj;
	char			clustername[128];
	char			fsname[128];
	int			fsflags;
	dlm_lockspace_t		*dlm_lockspace;
	lm_callback_t		fscb;
	lm_fsdata_t		*fsdata;
	int			recover_jid;
	int			recover_done;
	spinlock_t		async_lock;
	struct list_head	complete;
	struct list_head	blocking;
	struct list_head	delayed;
	struct list_head	submit;
	struct list_head	all_locks;
	uint32_t		all_locks_count;
	wait_queue_head_t	wait_control;
	struct task_struct	*thread1;
	struct task_struct	*thread2;
	wait_queue_head_t	thread_wait;
	unsigned long		drop_time;
	int			drop_locks_count;
	int			drop_locks_period;
};

#define LFL_NOBLOCK             0
#define LFL_NOCACHE             1
#define LFL_DLM_UNLOCK          2
#define LFL_DLM_CANCEL          3
#define LFL_SYNC_LVB            4
#define LFL_FORCE_PROMOTE       5
#define LFL_REREQUEST           6
#define LFL_ACTIVE              7
#define LFL_INLOCK              8
#define LFL_CANCEL              9
#define LFL_NOBAST              10
#define LFL_HEADQUE             11
#define LFL_UNLOCK_DELETE       12

struct gdlm_lock {
	struct gdlm_ls		*ls;
	struct lm_lockname	lockname;
	char			*lvb;
	struct dlm_lksb		lksb;

	int16_t			cur;
	int16_t			req;
	int16_t			prev_req;
	uint32_t		lkf;		/* dlm flags DLM_LKF_ */
	unsigned long		flags;		/* lock_dlm flags LFL_ */

	int			bast_mode;	/* protected by async_lock */
	struct completion	ast_wait;

	struct list_head	clist;		/* complete */
	struct list_head	blist;		/* blocking */
	struct list_head	delay_list;	/* delayed */
	struct list_head	all_list;	/* all locks for the fs */
	struct gdlm_lock	*hold_null;	/* NL lock for hold_lvb */
};

#if (BITS_PER_LONG == 64)
#define PRIx64 "lx"
#else
#define PRIx64 "Lx"
#endif

#define GDLM_ASSERT(x, do) \
{ \
  if (!(x)) \
  { \
    printk("\nlock_dlm:  Assertion failed on line %d of file %s\n" \
           "lock_dlm:  assertion:  \"%s\"\n" \
	   "lock_dlm:  time = %lu\n", \
	   __LINE__, __FILE__, #x, jiffies); \
    {do} \
    printk("\n"); \
    BUG(); \
    panic("lock_dlm:  Record message above and reboot.\n"); \
  } \
}

#define log_debug(fmt, args...)
#define log_all(fmt, args...)
#define log_error(fmt, args...)

/* sysfs.c */

int gdlm_sysfs_init(void);
void gdlm_sysfs_exit(void);
int gdlm_kobject_setup(struct gdlm_ls *);
void gdlm_kobject_release(struct gdlm_ls *);

/* thread.c */

int gdlm_init_threads(struct gdlm_ls *);
void gdlm_release_threads(struct gdlm_ls *);

/* lock.c */

int16_t gdlm_make_lmstate(int16_t);
void gdlm_queue_delayed(struct gdlm_lock *);
void gdlm_submit_delayed(struct gdlm_ls *);
int gdlm_release_all_locks(struct gdlm_ls *);
int gdlm_create_lp(struct gdlm_ls *, struct lm_lockname *, struct gdlm_lock **);
void gdlm_delete_lp(struct gdlm_lock *);
int gdlm_add_lvb(struct gdlm_lock *);
void gdlm_del_lvb(struct gdlm_lock *);
void gdlm_do_lock(struct gdlm_lock *, struct dlm_range *);
void gdlm_do_unlock(struct gdlm_lock *);

int gdlm_get_lock(lm_lockspace_t *, struct lm_lockname *, lm_lock_t **);
void gdlm_put_lock(lm_lock_t *);
unsigned int gdlm_lock(lm_lock_t *, unsigned int, unsigned int, unsigned int);
unsigned int gdlm_unlock(lm_lock_t *, unsigned int);
void gdlm_cancel(lm_lock_t *);
int gdlm_hold_lvb(lm_lock_t *, char **);
void gdlm_unhold_lvb(lm_lock_t *, char *);
void gdlm_sync_lvb(lm_lock_t *, char *);

#endif

