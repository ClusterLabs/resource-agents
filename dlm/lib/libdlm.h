/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This library is free software; you can redistribute it and/or
**  modify it under the terms of the GNU Lesser General Public
**  License as published by the Free Software Foundation; either
**  version 2 of the License, or (at your option) any later version.
**
**  This library is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
**  Lesser General Public License for more details.
**
**  You should have received a copy of the GNU Lesser General Public
**  License along with this library; if not, write to the Free Software
**  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
**
*******************************************************************************
******************************************************************************/

#ifndef __LIBDLM_H
#define __LIBDLM_H

typedef void * dlm_lshandle_t;

/* Typedefs for things that are compatible with the kernel
 * but replicated here so that users only need the libdlm include file.
 * libdlm itself needs the full kernel file so shouldn't use these.
 */
#ifndef BUILDING_LIBDLM
#define DLM_RESNAME_MAXLEN     (64)
#define DLM_LVB_LEN            (32)

struct dlm_lksb {
	int sb_status;
	uint32_t sb_lkid;
	char sb_flags;
	char *sb_lvbptr;
};

struct dlm_range {
	uint64_t ra_start;
	uint64_t ra_end;
};


/*
 * These defines are the bits that make up the
 * query code.
 */

/* Bits 0, 1, 2, the lock mode or DLM_LOCK_THIS, see DLM_LOCK_NL etc in
 * dlm.h Ignored for DLM_QUERY_LOCKS_ALL */
#define DLM_LOCK_THIS            0x0007
#define DLM_QUERY_MODE_MASK      0x0007

/* Bits 3, 4, 5  bitmap of queue(s) to query */
#define DLM_QUERY_QUEUE_WAIT     0x0008
#define DLM_QUERY_QUEUE_CONVERT  0x0010
#define DLM_QUERY_QUEUE_GRANT    0x0020
#define DLM_QUERY_QUEUE_GRANTED  0x0030	/* Shorthand */
#define DLM_QUERY_QUEUE_ALL      0x0038	/* Shorthand */

/* Bit 6, Return only the information that can be established without a network
 * round-trip. The caller must be aware of the implications of this. Useful for
 * just getting the master node id or resource name. */
#define DLM_QUERY_LOCAL          0x0040

/* Bits 8 up, query type */
#define DLM_QUERY_LOCKS_HIGHER   0x0100
#define DLM_QUERY_LOCKS_LOWER    0x0200
#define DLM_QUERY_LOCKS_EQUAL    0x0300
#define DLM_QUERY_LOCKS_BLOCKING 0x0400
#define DLM_QUERY_LOCKS_NOTBLOCK 0x0500
#define DLM_QUERY_LOCKS_ALL      0x0600
#define DLM_QUERY_MASK           0x0F00

/* GRMODE is the default for mode comparisons,
   RQMODE might also be handy */
#define DLM_QUERY_GRMODE         0x0000
#define DLM_QUERY_RQMODE         0x1000

/* Structures passed into and out of the query */

struct dlm_lockinfo {
	int lki_lkid;		/* Lock ID on originating node */
        int lki_mstlkid;        /* Lock ID on master node */
	int lki_parent;
	int lki_node;		/* Originating node (not master) */
	int lki_ownpid;		/* Owner pid on originating node */
	uint8_t lki_state;	/* Queue the lock is on */
	uint8_t lki_grmode;	/* Granted mode */
	uint8_t lki_rqmode;	/* Requested mode */
	struct dlm_range lki_grrange;	/* Granted range, if applicable */
	struct dlm_range lki_rqrange;	/* Requested range, if applicable */
};

struct dlm_resinfo {
	int rsi_length;
	int rsi_grantcount;	/* No. of nodes on grant queue */
	int rsi_convcount;	/* No. of nodes on convert queue */
	int rsi_waitcount;	/* No. of nodes on wait queue */
	int rsi_masternode;	/* Master for this resource */
	char rsi_name[DLM_RESNAME_MAXLEN];	/* Resource name */
	char rsi_valblk[DLM_LVB_LEN];	/* Master's LVB contents, if applicable
					 */
};

struct dlm_queryinfo {
	struct dlm_resinfo *gqi_resinfo;
	struct dlm_lockinfo *gqi_lockinfo;	/* This points to an array
						 * of structs */
	int gqi_locksize;	/* input */
	int gqi_lockcount;	/* output */
};

#endif

#ifdef _REENTRANT
/* These synchronous functions require pthreads */
extern int lock_resource(const char *resource, int mode, int flags, int *lockid);
extern int unlock_resource(int lockid);
#endif

extern int dlm_lock(uint32_t mode,
		    struct dlm_lksb *lksb,
		    uint32_t flags,
		    const void *name,
		    unsigned int namelen,
		    uint32_t parent,
		    void (*astaddr) (void *astarg),
		    void *astarg,
		    void (*bastaddr) (void *astarg),
		    struct dlm_range *range);

extern int dlm_lock_wait(uint32_t mode,
			    struct dlm_lksb *lksb,
			    uint32_t flags,
			    const void *name,
			    unsigned int namelen,
			    uint32_t parent,
			    void *bastarg,
			    void (*bastaddr) (void *bastarg),
			    struct dlm_range *range);

extern int dlm_unlock(uint32_t lkid,
		      uint32_t flags, struct dlm_lksb *lksb, void *astarg);

extern int dlm_unlock_wait(uint32_t lkid,
			   uint32_t flags, struct dlm_lksb *lksb);

extern int dlm_query(struct dlm_lksb *lksb,
		     int query,
		     struct dlm_queryinfo *qinfo,
		     void (*astaddr) (void *astarg),
		     void *astarg);

extern int dlm_query_wait(struct dlm_lksb *lksb,
			  int query,
			  struct dlm_queryinfo *qinfo);

/* These two are for users that want to do their
 * own FD handling
 */
extern int dlm_get_fd();
extern int dlm_dispatch(int fd);

/* Lockspace manipulation calls
   dlm_create_lockspace() also opens the lockspace and returns
   a handle to it. privileges are required to create/release
   lockspaces.
   dlm_open_lockspace() simply returns a handle an already
   created lockspace and may be called by ordinary users.

   NOTE: that if you dlm_create_lockspace() then dlm_open_lockspace()
   you will have two open files on the same device. Hardly a major problem
   but I thought it worth pointing out.
*/
extern dlm_lshandle_t dlm_create_lockspace(const char *name, mode_t mode);
extern int dlm_release_lockspace(const char *name, dlm_lshandle_t ls, int force);
extern dlm_lshandle_t dlm_open_lockspace(const char *name);
extern int dlm_close_lockspace(dlm_lshandle_t ls);
extern int dlm_ls_get_fd(dlm_lshandle_t ls);

/* Lockspace-specific locking calls */
extern int dlm_ls_lock(dlm_lshandle_t lockspace,
		       uint32_t mode,
		       struct dlm_lksb *lksb,
		       uint32_t flags,
		       const void *name,
		       unsigned int namelen,
		       uint32_t parent,
		       void (*astaddr) (void *astarg),
		       void *astarg,
		       void (*bastaddr) (void *astarg),
		       struct dlm_range *range);

extern int dlm_ls_lock_wait(dlm_lshandle_t lockspace,
			       uint32_t mode,
			       struct dlm_lksb *lksb,
			       uint32_t flags,
			       const void *name,
			       unsigned int namelen,
			       uint32_t parent,
			       void *bastarg,
			       void (*bastaddr) (void *bastarg),
			       struct dlm_range *range);

extern int dlm_ls_unlock(dlm_lshandle_t lockspace,
			 uint32_t lkid,
			 uint32_t flags, struct dlm_lksb *lksb, void *astarg);

extern int dlm_ls_unlock_wait(dlm_lshandle_t lockspace,
			      uint32_t lkid,
			      uint32_t flags, struct dlm_lksb *lksb);

extern int dlm_ls_query(dlm_lshandle_t lockspace,
			struct dlm_lksb *lksb,
			int query,
			struct dlm_queryinfo *qinfo,
			void (*astaddr) (void *astarg),
			void *astarg);

extern int dlm_ls_query_wait(dlm_lshandle_t lockspace,
			     struct dlm_lksb *lksb,
			     int query,
			     struct dlm_queryinfo *qinfo);

/* This is for threaded applications. call this
 * before any locking operations and the ASTs will
 * be delivered in their own thread.
 *
 * Call the cleanup routine at application exit (optional)
 * or, if the locking functions are in a shared library that
 * is to be unloaded.
 *
 * dlm_close/release_lockspace() will tidy the threads for
 * a non-default lockspace.
 */
#ifdef _REENTRANT
extern int dlm_pthread_init();
extern int dlm_ls_pthread_init(dlm_lshandle_t lockspace);
extern int dlm_pthread_cleanup();
#endif

/* Lock modes: */
#define LKM_NLMODE      0               /* null lock */
#define LKM_CRMODE      1               /* concurrent read */
#define LKM_CWMODE      2               /* concurrent write */
#define LKM_PRMODE      3               /* protected read */
#define LKM_PWMODE      4               /* protected write */
#define LKM_EXMODE      5               /* exclusive */


/* Locking flags - these match the ones
 * in dlm.h
 */
#define LKF_NOQUEUE        (0x00000001)
#define LKF_CANCEL         (0x00000002)
#define LKF_CONVERT        (0x00000004)
#define LKF_VALBLK         (0x00000008)
#define LKF_QUECVT         (0x00000010)
#define LKF_IVVALBLK       (0x00000020)
#define LKF_CONVDEADLK     (0x00000040)
#define LKF_PERSISTENT     (0x00000080)
#define LKF_NODLCKWT       (0x00000100)
#define LKF_NODLCKBLK      (0x00000200)
#define LKF_EXPEDITE       (0x00000400)
#define LKF_NOQUEUEBAST    (0x00000800)
#define LKF_HEADQUE        (0x00001000)
#define LKF_NOORDER        (0x00002000)


/*
 * Extra return codes used by the DLM
 */
#define ECANCEL            (0x10001)
#define EUNLOCK            (0x10002)
#define	EINPROG		   (0x10003)	/* lock operation is in progress */
#endif
