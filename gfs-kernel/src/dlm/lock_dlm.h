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
#include <net/sock.h>
#include <linux/lm_interface.h>
#include <cluster/cnxman.h>
#include <cluster/service.h>
#include <cluster/dlm.h>

/* We take a shortcut and use lm_lockname structs for internal locks.  This
   means we must be careful to keep these types different from those used in
   lm_interface.h. */

#define LM_TYPE_JID		(0x10)
#define LM_TYPE_PLOCK_UPDATE	(0x11)

#define DLM_LVB_SIZE		(DLM_LVB_LEN)

/* GFS uses 12 bytes to identify a resource (32 bit type + 64 bit number).
   We sprintf these numbers into a 24 byte string of hex values to make them
   human-readable (to make debugging simpler.) */

#define LOCK_DLM_STRNAME_BYTES	(24)

#define LOCK_DLM_MAX_NODES	(128)

struct dlm;
struct dlm_lock;
struct dlm_node;
struct dlm_start;
struct strname;

typedef struct dlm dlm_t;
typedef struct dlm_lock dlm_lock_t;
typedef struct dlm_node dlm_node_t;
typedef struct dlm_start dlm_start_t;
typedef struct strname strname_t;

#define DFL_FIRST_MOUNT         0
#define DFL_THREAD_STOP         1
#define DFL_GOT_NODEID          2
#define DFL_MG_FINISH           3
#define DFL_HAVE_JID            4
#define DFL_BLOCK_LOCKS         5
#define DFL_START_ERROR         6
#define DFL_UMOUNT		7
#define DFL_NEED_STARTDONE	8

struct dlm {
	uint32_t		jid;
	uint32_t		our_nodeid;
	unsigned long		flags;

	int			cnlen;
	char *			clustername;
	int			fnlen;
	char *			fsname;
	int			max_nodes;

	dlm_lockspace_t *	gdlm_lsp;

	lm_callback_t		fscb;
	lm_fsdata_t *		fsdata;
	dlm_lock_t *		jid_lock;

	spinlock_t		async_lock;
	struct list_head	complete;
	struct list_head	blocking;
	struct list_head	delayed;
	struct list_head	submit;
	struct list_head	starts;

	wait_queue_head_t	wait;
	atomic_t		threads;

	int			mg_local_id;
	int			mg_last_start;
	int			mg_last_stop;
	int			mg_last_finish;
	struct list_head	mg_nodes;
	struct semaphore	mg_nodes_lock;

	struct list_head	resources;
	struct semaphore	res_lock;
};

struct dlm_resource {
	dlm_t *                 dlm;
	struct list_head        list;           /* list of resources */
	struct lm_lockname      name;           /* the resource name */
	struct semaphore        sema;
	struct list_head        locks;          /* one lock for each range */
	int                     count;
	dlm_lock_t *		update;
	struct list_head	async_locks;
	spinlock_t		async_spin;
};

struct posix_lock {
	struct list_head        list;           /* resource locks list */
	struct list_head	async_list;	/* resource async_locks list */
	struct dlm_resource *   resource;
	dlm_lock_t *            lp;
	unsigned long           owner;
	uint64_t                start;
	uint64_t                end;
	int                     count;
	int                     ex;
};

#define LFL_NOBLOCK             0
#define LFL_NOCACHE             1
#define LFL_UNLOCK_RECOVERY     2
#define LFL_DLM_UNLOCK          3
#define LFL_TRYFAILED           4
#define LFL_SYNC_LVB            5
#define LFL_FORCE_PROMOTE       6
#define LFL_REREQUEST           7
#define LFL_WAIT_COMPLETE       8
#define LFL_CLIST               9
#define LFL_BLIST               10
#define LFL_DLIST               11
#define LFL_SLIST               12
#define LFL_IDLOCK              13
#define LFL_CANCEL              14

struct dlm_lock {
	dlm_t *			dlm;
	struct lm_lockname	lockname;
	char *			lvb;
	struct dlm_lksb		lksb;

	int16_t			cur;
	int16_t			req;
	int16_t			prev_req;
	unsigned int		lkf;
	unsigned int		type;
	unsigned long		flags;

	int			bast_mode;	/* protected by async_lock */
	struct completion	uast_wait;

	struct list_head	clist;		/* complete */
	struct list_head	blist;		/* blocking */
	struct list_head	dlist;		/* delayed */
	struct list_head	slist;		/* submit */

	struct posix_lock *	posix;
};

#define NFL_SENT_CB             0
#define NFL_NOT_MEMBER          1
#define NFL_RECOVERY_DONE       2
#define NFL_LAST_FINISH         3
#define NFL_HAVE_JID            4

struct dlm_node {
	uint32_t		nodeid;
	uint32_t		jid;
	unsigned long		flags;
	struct list_head	list;
};

#define QUEUE_LOCKS_BLOCKED     1
#define QUEUE_ERROR_UNLOCK      2
#define QUEUE_ERROR_LOCK        3
#define QUEUE_ERROR_RETRY       4

struct strname {
	unsigned char		name[LOCK_DLM_STRNAME_BYTES];
	unsigned short		namelen;
};

struct dlm_start {
	uint32_t *		nodeids;
	int			count;
	int			type;
	int			event_id;
	struct list_head	list;
};

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

extern struct lm_lockops lock_dlm_ops;

/* group.c */

int init_mountgroup(dlm_t *dlm);
void release_mountgroup(dlm_t *dlm);
void process_start(dlm_t *dlm, dlm_start_t *ds);
void process_finish(dlm_t *dlm);
void jid_recovery_done(dlm_t *dlm, unsigned int jid, unsigned int message);

/* thread.c */

int init_async_thread(dlm_t *dlm);
void release_async_thread(dlm_t *dlm);

/* lock.c */

int16_t make_lmstate(int16_t dlmmode);
void queue_delayed(dlm_lock_t *lp, int type);
void process_submit(dlm_lock_t *lp);
int create_lp(dlm_t *dlm, struct lm_lockname *name, dlm_lock_t **lpp);

int lm_dlm_get_lock(lm_lockspace_t *lockspace, struct lm_lockname *name,
		    lm_lock_t **lockp);
void lm_dlm_put_lock(lm_lock_t *lock);

void do_dlm_lock(dlm_lock_t *lp, struct dlm_range *range);
int do_dlm_lock_sync(dlm_lock_t *lp, struct dlm_range *range);
void do_dlm_unlock(dlm_lock_t *lp);
void do_dlm_unlock_sync(dlm_lock_t *lp);

unsigned int lm_dlm_lock(lm_lock_t *lock, unsigned int cur_state,
			 unsigned int req_state, unsigned int flags);
int lm_dlm_lock_sync(lm_lock_t *lock, unsigned int cur_state,
		     unsigned int req_state, unsigned int flags);
unsigned int lm_dlm_unlock(lm_lock_t *lock, unsigned int cur_state);
void lm_dlm_unlock_sync(lm_lock_t *lock, unsigned int cur_state);

void lm_dlm_cancel(lm_lock_t *lock);
int lm_dlm_hold_lvb(lm_lock_t *lock, char **lvbp);
void lm_dlm_unhold_lvb(lm_lock_t *lock, char *lvb);
void lm_dlm_sync_lvb(lm_lock_t *lock, char *lvb);
void lm_dlm_recovery_done(lm_lockspace_t *lockspace, unsigned int jid,
		          unsigned int message);

/* plock.c */

int lm_dlm_plock(lm_lockspace_t *lockspace, struct lm_lockname *name,
              unsigned long owner, int wait, int ex, uint64_t start,
              uint64_t end);

int lm_dlm_punlock(lm_lockspace_t *lockspace, struct lm_lockname *name,
                unsigned long owner, uint64_t start, uint64_t end);

int lm_dlm_plock_get(lm_lockspace_t *lockspace, struct lm_lockname *name,
                  unsigned long owner, uint64_t *start, uint64_t *end,
                  int *ex, unsigned long *rowner);

/* main.c */

void lock_dlm_debug_log(const char *fmt, ...);
void lock_dlm_debug_dump(void);


#define LOCK_DLM_DEBUG

#ifdef LOCK_DLM_DEBUG
#define log_debug(fmt, args...) lock_dlm_debug_log(fmt, ##args)
#else
#define log_debug(fmt, args...)
#endif

#define DLM_ASSERT(x, do) \
{ \
  if (!(x)) \
  { \
    lock_dlm_debug_dump(); \
    printk("\nlock_dlm:  Assertion failed on line %d of file %s\n" \
           "lock_dlm:  assertion:  \"%s\"\n" \
	   "lock_dlm:  time = %lu\n", \
	   __LINE__, __FILE__, #x, jiffies); \
    {do} \
    printk("\n"); \
    panic("lock_dlm:  Record message above and reboot.\n"); \
  } \
}

#define DLM_RETRY(do_this, until_this) \
for (;;) \
{ \
  do { do_this; } while (0); \
  if (until_this) \
    break; \
  printk("lock_dlm:  out of memory:  %s, %u\n", __FILE__, __LINE__); \
  schedule();\
}

#endif
