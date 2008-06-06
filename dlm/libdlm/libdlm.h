#ifndef __LIBDLM_H
#define __LIBDLM_H

/*
 * Typedefs for things that are compatible with the kernel but replicated here
 * so that users only need the libdlm include file.  libdlm itself needs the
 * full kernel file so shouldn't use these.
 */

#define DLM_LVB_LEN             32

#ifndef BUILDING_LIBDLM

/*
 * These two lengths are copied from linux/dlmconstants.h
 * They are the max length of a lockspace name and the max length of a
 * resource name.
 */

#define DLM_LOCKSPACE_LEN       64
#define DLM_RESNAME_MAXLEN      64

struct dlm_lksb {
	int sb_status;
	uint32_t sb_lkid;
	char sb_flags;
	char *sb_lvbptr;
};

/* lksb flags */
#define DLM_SBF_DEMOTED         0x01
#define DLM_SBF_VALNOTVALID     0x02
#define DLM_SBF_ALTMODE         0x04

/* dlm_new_lockspace flags */
#define DLM_LSFL_NODIR          0x00000001
#define DLM_LSFL_TIMEWARN       0x00000002

#endif


#if 0
/* Dummy definition to keep linkages */
struct dlm_queryinfo;
#endif

extern int dlm_kernel_version(uint32_t *maj, uint32_t *min, uint32_t *patch);
extern void dlm_library_version(uint32_t *maj, uint32_t *min, uint32_t *patch);


/*
 * Using the default lockspace
 *
 * lock_resource() - simple sync request or convert (requires pthreads)
 * unlock_resource() - simple sync unlock (requires pthreads)
 * dlm_lock() - async request or convert
 * dlm_unlock() - async unlock or cancel
 * dlm_lock_wait() - sync request or convert
 * dlm_unlock_wait() - sync unlock or cancel
 */

#ifdef _REENTRANT
extern int lock_resource(const char *resource, int mode, int flags, int *lockid);
extern int unlock_resource(int lockid);
#endif

extern int dlm_lock(uint32_t mode,
		struct dlm_lksb *lksb,
		uint32_t flags,
		const void *name,
		unsigned int namelen,
		uint32_t parent,			/* unusued */
		void (*astaddr) (void *astarg),
		void *astarg,
		void (*bastaddr) (void *astarg),
		void *range);				/* unused */

extern int dlm_unlock(uint32_t lkid,
		uint32_t flags,
		struct dlm_lksb *lksb,
		void *astarg);

extern int dlm_lock_wait(uint32_t mode,
		struct dlm_lksb *lksb,
		uint32_t flags,
		const void *name,
		unsigned int namelen,
		uint32_t parent,			/* unused */
		void *bastarg,
		void (*bastaddr) (void *bastarg),
		void *range);				/* unused */

extern int dlm_unlock_wait(uint32_t lkid,
		uint32_t flags,
		struct dlm_lksb *lksb);


/* 
 * These two are for users that want to do their own FD handling
 *
 * dlm_get_fd() - returns fd for the default lockspace for polling and dispatch
 * dlm_dispatch() - dispatches pending asts and basts
 */

extern int dlm_get_fd(void);
extern int dlm_dispatch(int fd);


/*
 * Creating your own lockspace
 *
 * dlm_create_lockspace() - create and open a lockspace and return a handle
 *                          to it.  Privileges are required to create/release.
 * dlm_new_lockspace() - same as create but allows flags
 * dlm_open_lockspace() - simply returns a handle for an existing lockspace and
 *                        may be called by ordinary users.
 * dlm_release_lockspace()
 * dlm_close_lockspace()
 * dlm_ls_get_fd()
 *
 * NOTE: that if you dlm_create_lockspace() then dlm_open_lockspace() you will
 * have two open files on the same device. Hardly a major problem but I thought
 * it worth pointing out.
 */

typedef void *dlm_lshandle_t;

extern dlm_lshandle_t dlm_create_lockspace(const char *name, mode_t mode);
extern int dlm_release_lockspace(const char *name, dlm_lshandle_t ls,
		int force);
extern dlm_lshandle_t dlm_open_lockspace(const char *name);
extern int dlm_close_lockspace(dlm_lshandle_t ls);
extern int dlm_ls_get_fd(dlm_lshandle_t ls);
extern dlm_lshandle_t dlm_new_lockspace(const char *name, mode_t mode,
		uint32_t flags);


/*
 * Using your own lockspace
 *
 * dlm_ls_lock()
 * dlm_ls_lockx()
 * dlm_ls_unlock()
 * dlm_ls_lock_wait()
 * dlm_ls_unlock_wait()
 * dlm_ls_deadlock_cancel()
 * dlm_ls_purge()
 */

extern int dlm_ls_lock(dlm_lshandle_t lockspace,
		uint32_t mode,
		struct dlm_lksb *lksb,
		uint32_t flags,
		const void *name,
		unsigned int namelen,
		uint32_t parent,			/* unused */
		void (*astaddr) (void *astarg),
		void *astarg,
		void (*bastaddr) (void *astarg),
		void *range);				/* unused */

extern int dlm_ls_lockx(dlm_lshandle_t lockspace,
		uint32_t mode,
		struct dlm_lksb *lksb,
		uint32_t flags,
		const void *name,
		unsigned int namelen,
		uint32_t parent,			/* unused */
		void (*astaddr) (void *astarg),
		void *astarg,
		void (*bastaddr) (void *astarg),
		uint64_t *xid,
		uint64_t *timeout);

extern int dlm_ls_unlock(dlm_lshandle_t lockspace,
		uint32_t lkid,
		uint32_t flags,
		struct dlm_lksb *lksb,
		void *astarg);

extern int dlm_ls_lock_wait(dlm_lshandle_t lockspace,
		uint32_t mode,
		struct dlm_lksb *lksb,
		uint32_t flags,
		const void *name,
		unsigned int namelen,
		uint32_t parent,			/* unused */
		void *bastarg,
		void (*bastaddr) (void *bastarg),
		void *range);				/* unused */

extern int dlm_ls_unlock_wait(dlm_lshandle_t lockspace,
		uint32_t lkid,
		uint32_t flags,
		struct dlm_lksb *lksb);

extern int dlm_ls_deadlock_cancel(dlm_lshandle_t ls,
		uint32_t lkid,
		uint32_t flags);

extern int dlm_ls_purge(dlm_lshandle_t lockspace,
		int nodeid,
		int pid);


/*
 * For threaded applications
 *
 * dlm_pthread_init()
 * dlm_ls_pthread_init() - call this before any locking operations and the ASTs
 *                         will be delivered in their own thread.
 * dlm_pthread_cleanup() - call the cleanup routine at application exit
 *			   (optional) or, if the locking functions are in a
 *			   shared library that is to be unloaded.
 *
 * dlm_close/release_lockspace() will tidy the threads for a non-default
 * lockspace
 */

#ifdef _REENTRANT
extern int dlm_pthread_init();
extern int dlm_ls_pthread_init(dlm_lshandle_t lockspace);
extern int dlm_pthread_cleanup();
#endif


/*
 * Lock modes
 */

#define LKM_NLMODE          0           /* null lock */
#define LKM_CRMODE          1           /* concurrent read */
#define LKM_CWMODE          2           /* concurrent write */
#define LKM_PRMODE          3           /* protected read */
#define LKM_PWMODE          4           /* protected write */
#define LKM_EXMODE          5           /* exclusive */


/*
 * Locking flags - these match the ones in dlm.h
 */

#define LKF_NOQUEUE         0x00000001
#define LKF_CANCEL          0x00000002
#define LKF_CONVERT         0x00000004
#define LKF_VALBLK          0x00000008
#define LKF_QUECVT          0x00000010
#define LKF_IVVALBLK        0x00000020
#define LKF_CONVDEADLK      0x00000040
#define LKF_PERSISTENT      0x00000080
#define LKF_NODLCKWT        0x00000100
#define LKF_NODLCKBLK       0x00000200
#define LKF_EXPEDITE        0x00000400
#define LKF_NOQUEUEBAST     0x00000800
#define LKF_HEADQUE         0x00001000
#define LKF_NOORDER         0x00002000
#define LKF_ORPHAN          0x00004000
#define LKF_ALTPR           0x00008000
#define LKF_ALTCW           0x00010000
#define LKF_FORCEUNLOCK     0x00020000
#define LKF_TIMEOUT         0x00040000
#define LKF_WAIT            0x80000000  /* Userspace only, for sync API calls */

/*
 * Extra return codes used by the DLM
 */

#define ECANCEL             0x10001
#define EUNLOCK             0x10002
#define	EINPROG		    0x10003     /* lock operation is in progress */

#endif

