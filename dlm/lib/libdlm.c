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


#ifdef _REENTRANT
#include <pthread.h>
#endif
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <linux/major.h>
#ifdef HAVE_SELINUX
#include <selinux/selinux.h>
#endif
#include "dlm.h"
#define BUILDING_LIBDLM
#include "libdlm.h"
#include "dlm_device.h"

#define MISC_PREFIX "/dev/misc/"
#define PROC_MISC "/proc/misc"
#define DLM_PREFIX "dlm_"
#define DLM_MISC_PREFIX MISC_PREFIX DLM_PREFIX
#define DLM_CONTROL_DEV "dlm-control"
#define DEFAULT_LOCKSPACE "default"

/* This is the name of the control device */
#define DLM_CTL_DEVICE_NAME MISC_PREFIX DLM_CONTROL_DEV

/* One of these per lockspace in use by the application */
struct dlm_ls_info
{
    int fd;
#ifdef _REENTRANT
    pthread_t tid;
#else
    int tid;
#endif
};

/* The default lockspace.
   I've resisted putting locking around this as the user should be
   "sensible" and only do lockspace operations either in the
   main thread or ... carefully...
*/
static struct dlm_ls_info *default_ls = NULL;
static int control_fd = -1;

static void ls_dev_name(const char *lsname, char *devname, int devlen)
{
    snprintf(devname, devlen, DLM_MISC_PREFIX "%s", lsname);
}

#ifdef HAVE_SELINUX
static int set_selinux_context(const char *path)
{
	security_context_t scontext;

	if (is_selinux_enabled() <= 0)
		return 1;

	if (matchpathcon(path, 0, &scontext) < 0) {
		return 0;
	}

	if ((lsetfilecon(path, scontext) < 0) && (errno != ENOTSUP)) {
		freecon(scontext);
		return 0;
	}

	free(scontext);
	return 1;
}
#endif


#ifdef _REENTRANT
/* Used for the synchronous and "simplified, synchronous" API routines */
struct lock_wait
{
    pthread_cond_t  cond;
    pthread_mutex_t mutex;
    struct dlm_lksb lksb;
};

static void dummy_ast_routine(void *arg)
{
}

static void sync_ast_routine(void *arg)
{
    struct lock_wait *lwait = arg;

    pthread_mutex_lock(&lwait->mutex);
    pthread_cond_signal(&lwait->cond);
    pthread_mutex_unlock(&lwait->mutex);
}

/* lock_resource & unlock_resource
 * are the simplified, synchronous API.
 * Aways uses the default lockspace.
 */
int lock_resource(const char *resource, int mode, int flags, int *lockid)
{
    int status;
    struct lock_wait lwait;

    if (default_ls == NULL)
    {
	if (dlm_pthread_init())
	{
	    return -1;
	}
    }

    if (!lockid)
    {
	errno = EINVAL;
	return -1;
    }

    /* Conversions need the lockid in the LKSB */
    if (flags & LKF_CONVERT)
	lwait.lksb.sb_lkid = *lockid;

    pthread_cond_init(&lwait.cond, NULL);
    pthread_mutex_init(&lwait.mutex, NULL);
    pthread_mutex_lock(&lwait.mutex);

    status = dlm_lock(mode,
		      &lwait.lksb,
		      flags,
		      resource,
		      strlen(resource),
		      0,
		      sync_ast_routine,
		      &lwait,
		      NULL,
		      NULL);
    if (status)
	return status;

    /* Wait for it to complete */
    pthread_cond_wait(&lwait.cond, &lwait.mutex);
    pthread_mutex_unlock(&lwait.mutex);

    *lockid = lwait.lksb.sb_lkid;

    errno = lwait.lksb.sb_status;
    if (lwait.lksb.sb_status)
	return -1;
    else
	return 0;
}


int unlock_resource(int lockid)
{
    int status;
    struct lock_wait lwait;

    if (default_ls == NULL)
    {
	errno = -ENOTCONN;
	return -1;
    }

    pthread_cond_init(&lwait.cond, NULL);
    pthread_mutex_init(&lwait.mutex, NULL);
    pthread_mutex_lock(&lwait.mutex);

    status = dlm_unlock(lockid, 0, &lwait.lksb, &lwait);

    if (status)
	return status;

    /* Wait for it to complete */
    pthread_cond_wait(&lwait.cond, &lwait.mutex);
    pthread_mutex_unlock(&lwait.mutex);

    errno = lwait.lksb.sb_status;
    if (lwait.lksb.sb_status != DLM_EUNLOCK)
	return -1;
    else
	return 0;
}

/* Tidy up threads after a lockspace is closed */
static int ls_pthread_cleanup(struct dlm_ls_info *lsinfo)
{
    int status = 0;
    int fd;

    /* Must close the fd after the thread has finished */
    fd = lsinfo->fd;
    if (lsinfo->tid)
    {
	status = pthread_cancel(lsinfo->tid);
	if (!status)
	    pthread_join(lsinfo->tid, NULL);
    }
    if (!status)
    {
	free(lsinfo);
	close(fd);
    }

    return status;
}

/* Cleanup default lockspace */
int dlm_pthread_cleanup()
{
    struct dlm_ls_info *lsinfo = default_ls;

    /* Protect users from their own stupidity */
    if (!lsinfo)
	return 0;

    default_ls = NULL;

    return ls_pthread_cleanup(lsinfo);
}
#else

/* Non-pthread version of cleanup */
static int ls_pthread_cleanup(struct dlm_ls_info *lsinfo)
{
    close(lsinfo->fd);
    free(lsinfo);
    return 0;
}
#endif

static void set_version(struct dlm_write_request *req)
{
    req->version[0] = DLM_DEVICE_VERSION_MAJOR;
    req->version[1] = DLM_DEVICE_VERSION_MINOR;
    req->version[2] = DLM_DEVICE_VERSION_PATCH;
}

/* Open the default lockspace */
static int open_default_lockspace()
{
    if (!default_ls)
    {
	dlm_lshandle_t ls;

	/* This isn't the race it looks, create_lockspace will
	 * do the right thing if the lockspace has already been
	 * created.
	 */
	ls = dlm_open_lockspace(DEFAULT_LOCKSPACE);
	if (!ls)
    	    ls = dlm_create_lockspace(DEFAULT_LOCKSPACE, 0600);
	if (!ls)
	    return -1;

	default_ls = (struct dlm_ls_info *)ls;
    }
    return 0;
}

static int create_control_device()
{
    FILE *pmisc;
    int minor;
    char name[256];
    int status = 0;
    int saved_errno = 0;
    mode_t oldmode;

    /* Make sure the parent directory exists */
    oldmode = umask(0);
    status = mkdir(MISC_PREFIX, 0755);
    umask(oldmode);
    if (status != 0 && errno != EEXIST)
    {
	return status;
    }

    pmisc = fopen(PROC_MISC, "r");
    if (!pmisc)
	return -1;

    while (!feof(pmisc))
    {
	fscanf(pmisc, "%d %s\n", &minor, name);
	if (strcmp(name, DLM_CONTROL_DEV) == 0)
	{
	    status = mknod(DLM_CTL_DEVICE_NAME, S_IFCHR | 0600, makedev(MISC_MAJOR, minor));
	    saved_errno = errno;
#ifdef HAVE_SELINUX
	    if (status == 0)
		set_selinux_context(DLM_CTL_DEVICE_NAME);
#endif
	    break;
	}
    }
    fclose(pmisc);

    errno = saved_errno;
    return status;
}

static int open_control_device()
{
    if (control_fd == -1)
    {
	control_fd = open(DLM_CTL_DEVICE_NAME, O_RDWR);
	if (control_fd == -1)
	{
	    create_control_device();
	    control_fd = open(DLM_CTL_DEVICE_NAME, O_RDWR);
	    if (control_fd == -1)
		return -1;
	}
    }
    return 0;
}

static int do_dlm_dispatch(int fd)
{
    char resultbuf[sizeof(struct dlm_lock_result)];
    struct dlm_lock_result *result = (struct dlm_lock_result *)resultbuf;
    char *fullresult=NULL;
    int status;
    void (*astaddr)(void *astarg);

    /* Just read the header first */
    status = read(fd, result, sizeof(struct dlm_lock_result));
    if (status <= 0)
	return -1;

    if (result->length != status)
    {
        int newstat;

	fullresult = malloc(result->length);
	if (!fullresult)
	    return -1;

	newstat = read(fd, fullresult, result->length);

	/* If it read OK then use the new data. otherwise we can
	   still deliver the AST, it just might not have all the
	   info in it...hmmm */
	if (newstat == result->length)
		result = (struct dlm_lock_result *)fullresult;
    }

    /* Copy lksb to user's buffer - except the LVB ptr */
    memcpy(result->user_lksb, &result->lksb, sizeof(struct dlm_lksb) - sizeof(char*));

    /* Flip the status. Kernel space likes negative return codes,
       userspace positive ones */
    result->user_lksb->sb_status = -result->user_lksb->sb_status;

    /* Copy optional items */
    if (result->lvb_offset)
	memcpy(result->user_lksb->sb_lvbptr, fullresult+result->lvb_offset, DLM_LVB_LEN);

    if (result->qinfo_offset)
    {
	/* Just need the lockcount written out here */
	struct dlm_queryinfo *qi = (struct dlm_queryinfo *)(fullresult+result->qinfo_offset);
	result->user_qinfo->gqi_lockcount = qi->gqi_lockcount;
    }

    if (result->qresinfo_offset)
	memcpy(result->user_qinfo->gqi_resinfo, fullresult+result->qresinfo_offset,
	       sizeof(struct dlm_resinfo));

    if (result->qlockinfo_offset)
	memcpy(result->user_qinfo->gqi_lockinfo, fullresult+result->qlockinfo_offset,
	       sizeof(struct dlm_lockinfo) * result->user_qinfo->gqi_lockcount);

    /* Call AST */
    if (result->user_astaddr)
    {
	astaddr = result->user_astaddr;
	astaddr(result->user_astparam);
    }

    if (fullresult)
	free(fullresult);
    return 0;
}

#ifdef _REENTRANT

/* Helper routine which supports the synchronous DLM calls. This
   writes a parameter block down to the DLM and waits for the
   operation to complete. This hides the different completion mechanism
   used when called from the main thread or the DLM 'AST' thread.
*/
static int sync_write(struct dlm_ls_info *lsinfo, struct dlm_write_request *req, int len)
{
    int	status;
    struct lock_wait lwait;

    if (pthread_self() == lsinfo->tid)
    {
        /* This is the DLM worker thread, don't use lwait to sync */
	req->i.lock.castaddr  = dummy_ast_routine;
	req->i.lock.castparam = NULL;

	status = write(lsinfo->fd, req, len);
	if (status < 0)
	    return -1;

	while (req->i.lock.lksb->sb_status == EINPROG) {
	    do_dlm_dispatch(lsinfo->fd);
	}
    }
    else
    {
	pthread_cond_init(&lwait.cond, NULL);
	pthread_mutex_init(&lwait.mutex, NULL);
	pthread_mutex_lock(&lwait.mutex);

	req->i.lock.castaddr  = sync_ast_routine;
	req->i.lock.castparam = &lwait;

	status = write(lsinfo->fd, req, len);
	if (status < 0)
	    return -1;

	pthread_cond_wait(&lwait.cond, &lwait.mutex);
	pthread_mutex_unlock(&lwait.mutex);
    }
    return status;	/* lock status is in the lksb */
}
#endif

static int find_minor_from_proc(const char *lsname)
{
    FILE *f = fopen(PROC_MISC, "r");
    char miscname[strlen(lsname)+strlen(DLM_PREFIX)+1];
    char name[256];
    int minor;

    sprintf(miscname, "%s%s", DLM_PREFIX, lsname);

    if (f)
    {
	while (!feof(f))
	{
	    if (fscanf(f, "%d %s", &minor, name) == 2 &&
		strcmp(name, miscname) == 0)
	    {
		fclose(f);
		return minor;
	    }
	}
    }

    fclose(f);
    return 0;
}

/* Lock on default lockspace*/
int dlm_lock(uint32_t mode,
	     struct dlm_lksb *lksb,
	     uint32_t flags,
	     const void *name,
	     unsigned int namelen,
	     uint32_t parent,
	     void (*astaddr) (void *astarg),
	     void *astarg,
	     void (*bastaddr) (void *astarg),
	     struct dlm_range *range)
{
    if (open_default_lockspace())
	return -1;

    return dlm_ls_lock(default_ls, mode, lksb, flags, name, namelen, parent,
		       astaddr, astarg, bastaddr, range);
}


/*
 * This is the full-fat, aynchronous DLM call
 */
int dlm_ls_lock(dlm_lshandle_t ls,
		uint32_t mode,
		struct dlm_lksb *lksb,
		uint32_t flags,
		const void *name,
		unsigned int namelen,
		uint32_t parent,
		void (*astaddr) (void *astarg),
		void *astarg,
		void (*bastaddr) (void *astarg),
		struct dlm_range *range)
{
    int len;
    char param_buf[sizeof(struct dlm_write_request) + DLM_RESNAME_MAXLEN];
    struct dlm_write_request *req =  (struct dlm_write_request *)param_buf;
    struct dlm_ls_info *lsinfo = (struct dlm_ls_info *)ls;
    int status;

    set_version(req);

    req->cmd = DLM_USER_LOCK;
    req->i.lock.mode = mode;
    req->i.lock.flags = flags;
    req->i.lock.lkid = lksb->sb_lkid;
    req->i.lock.parent = parent;
    req->i.lock.lksb = lksb;
    req->i.lock.castaddr  = astaddr;
    req->i.lock.bastaddr = bastaddr;
    req->i.lock.castparam = astarg;	/* completion and blocking ast arg are the same */
    req->i.lock.bastparam = astarg;
    if (range)
    {
	req->i.lock.range.ra_start = range->ra_start;
	req->i.lock.range.ra_end = range->ra_end;
    }
    else
    {
	req->i.lock.range.ra_start = 0L;
	req->i.lock.range.ra_end = 0L;
    }
    if (flags & LKF_CONVERT)
    {
	req->i.lock.namelen = 0;
    }
    else
    {
	if (namelen > DLM_RESNAME_MAXLEN)
	{
	    errno = EINVAL;
	    return -1;
	}
	req->i.lock.namelen = namelen;
	memcpy(req->i.lock.name, name, namelen);
    }
    if (flags & LKF_VALBLK)
    {
	memcpy(req->i.lock.lvb, lksb->sb_lvbptr, DLM_LVB_LEN);
    }
    len = sizeof(struct dlm_write_request) + namelen - 1;
    lksb->sb_status = EINPROG;

#ifdef _REENTRANT
    if (flags & LKF_WAIT)
	status = sync_write(lsinfo, req, len);
    else
#endif
	status = write(lsinfo->fd, req, len);

    if (status < 0)
    {
	return -1;
    }
    else
    {
	if (status > 0)
	    lksb->sb_lkid = status;
	return 0;
    }
}

#ifdef _REENTRANT
int dlm_lock_wait(uint32_t mode,
		     struct dlm_lksb *lksb,
		     uint32_t flags,
		     const void *name,
		     unsigned int namelen,
		     uint32_t parent,
		     void *bastarg,
		     void (*bastaddr) (void *bastarg),
		     struct dlm_range *range)
{
    if (open_default_lockspace())
	    return -1;

    return dlm_ls_lock(default_ls, mode, lksb, flags | LKF_WAIT,
		       name, namelen, parent, NULL, bastarg,
		       bastaddr, range);
}

/*
 * This is the full-fat, synchronous DLM call
 */
int dlm_ls_lock_wait(dlm_lshandle_t ls,
		     uint32_t mode,
		     struct dlm_lksb *lksb,
		     uint32_t flags,
		     const void *name,
		     unsigned int namelen,
		     uint32_t parent,
		     void *bastarg,
		     void (*bastaddr) (void *bastarg),
		     struct dlm_range *range)
{

    return dlm_ls_lock(ls, mode, lksb, flags | LKF_WAIT,
		       name, namelen, parent, NULL, bastarg,
		       bastaddr, range);
}

int dlm_ls_unlock_wait(dlm_lshandle_t ls, uint32_t lkid,
		       uint32_t flags, struct dlm_lksb *lksb)
{

	return dlm_ls_unlock(ls, lkid, flags | LKF_WAIT,
			     lksb, NULL);
}

int dlm_ls_query_wait(dlm_lshandle_t lockspace,
		      struct dlm_lksb *lksb,
		      int query,
		      struct dlm_queryinfo *qinfo)
{
    struct dlm_write_request req;
    struct dlm_ls_info *lsinfo = (struct dlm_ls_info *)lockspace;
    int status;

    if (lockspace == NULL)
    {
	errno = ENOTCONN;
	return -1;
    }

    if (!lksb)
    {
	errno = EINVAL;
	return -1;
    }

    if (!lksb->sb_lkid)
    {
	errno = EINVAL;
	return -1;
    }

    set_version(&req);
    req.cmd = DLM_USER_QUERY;
    req.i.query.query = query;
    req.i.query.lksb  = lksb;
    req.i.query.lkid  = lksb->sb_lkid;
    req.i.query.qinfo = qinfo;
    req.i.query.lockinfo_max = qinfo->gqi_locksize;
    req.i.query.lockinfo = qinfo->gqi_lockinfo;
    req.i.query.lockinfo_max = qinfo->gqi_locksize;
    lksb->sb_status = EINPROG;

    status = sync_write(lsinfo, &req, sizeof(req));
    return (status >= 0);
}

int dlm_unlock_wait(uint32_t lkid,
		    uint32_t flags, struct dlm_lksb *lksb)
{
    return dlm_ls_unlock_wait(default_ls, lkid, flags | LKF_WAIT, lksb);
}

int dlm_query_wait(struct dlm_lksb *lksb,
		   int query,
		   struct dlm_queryinfo *qinfo)
{
    return dlm_ls_query_wait(default_ls, lksb, query, qinfo);
}

#endif

/* Unlock on default lockspace*/
int dlm_unlock(uint32_t lkid,
	       uint32_t flags, struct dlm_lksb *lksb, void *astarg)
{
    return dlm_ls_unlock(default_ls, lkid, flags, lksb, astarg);
}


int dlm_ls_unlock(dlm_lshandle_t ls, uint32_t lkid,
		  uint32_t flags, struct dlm_lksb *lksb, void *astarg)
{
    struct dlm_write_request req;
    struct dlm_ls_info *lsinfo = (struct dlm_ls_info *)ls;
    int status;

    if (ls == NULL)
    {
	errno = ENOTCONN;
	return -1;
    }

    if (!lkid)
    {
	errno = EINVAL;
	return -1;
    }

    set_version(&req);
    req.cmd = DLM_USER_UNLOCK;
    req.i.lock.lkid = lkid;
    req.i.lock.flags = flags;
    req.i.lock.lksb  = lksb;
    req.i.lock.castparam = astarg;
	    /* DLM_USER_UNLOCK will default to existing completion AST */
    req.i.lock.castaddr = 0;
    lksb->sb_status = EINPROG;

#ifdef _REENTRANT
    if (flags & LKF_WAIT)
	    status = sync_write(lsinfo, &req, sizeof(req));
    else
#endif
	    status = write(lsinfo->fd, &req, sizeof(req));

    if (status < 0)
	return -1;
    else
	return 0;
}


int dlm_ls_query(dlm_lshandle_t lockspace,
		 struct dlm_lksb *lksb,
		 int query,
		 struct dlm_queryinfo *qinfo,
		 void (*astaddr) (void *astarg),
		 void *astarg)
{
    struct dlm_write_request req;
    struct dlm_ls_info *lsinfo = (struct dlm_ls_info *)lockspace;
    int status;

    if (lockspace == NULL)
    {
	errno = ENOTCONN;
	return -1;
    }

    if (!lksb)
    {
	errno = EINVAL;
	return -1;
    }

    if (!lksb->sb_lkid)
    {
	errno = EINVAL;
	return -1;
    }

    set_version(&req);
    req.cmd = DLM_USER_QUERY;
    req.i.query.query = query;
    req.i.query.lksb  = lksb;
    req.i.query.lkid  = lksb->sb_lkid;
    req.i.query.castparam = astarg;
    req.i.query.castaddr = astaddr;
    req.i.query.qinfo = qinfo;
    req.i.query.resinfo = qinfo->gqi_resinfo;
    req.i.query.lockinfo = qinfo->gqi_lockinfo;
    req.i.query.lockinfo_max = qinfo->gqi_locksize;
    lksb->sb_status = EINPROG;

    status = write(lsinfo->fd, &req, sizeof(req));
    if (status != sizeof(req))
	return -1;
    else
	return 0;
}

int dlm_query(struct dlm_lksb *lksb,
	      int query,
	      struct dlm_queryinfo *qinfo,
	      void (*astaddr) (void *astarg),
	      void *astarg)
{
    return dlm_ls_query(default_ls, lksb, query, qinfo, astaddr, astarg);
}


/* These two routines for for users that want to
 * do their own fd handling.
 * This allows a non-threaded app to use the DLM.
 */
int dlm_get_fd()
{
    if (default_ls)
    {
	return default_ls->fd;
    }
    else
    {
	if (open_default_lockspace())
	    return -1;
	else
	    return default_ls->fd;
    }
}

int dlm_dispatch(int fd)
{
    int status;
    int fdflags;

    fdflags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL,  fdflags | O_NONBLOCK);
    do
    {
	status = do_dlm_dispatch(fd);
    } while (status == 0);

    /* EAGAIN is not an error */
    if (status < 0 && errno == EAGAIN)
	status = 0;

    fcntl(fd, F_SETFL, fdflags);
    return status;
}

/* Converts a lockspace handle into a file descriptor */
int dlm_ls_get_fd(dlm_lshandle_t lockspace)
{
    struct dlm_ls_info *lsinfo = (struct dlm_ls_info *)lockspace;

    return lsinfo->fd;
}

#ifdef _REENTRANT
static void *dlm_recv_thread(void *lsinfo)
{
    struct dlm_ls_info *lsi = lsinfo;

    for (;;)
	do_dlm_dispatch(lsi->fd);
}

/* Multi-threaded callers normally use this */
int dlm_pthread_init()
{
    if (open_default_lockspace())
	return -1;

    if (default_ls->tid)
    {
	errno = EEXIST;
	return -1;
    }

    if (pthread_create(&default_ls->tid, NULL, dlm_recv_thread, default_ls))
    {
	int saved_errno = errno;
	close(default_ls->fd);
	free(default_ls);
	default_ls = NULL;
	errno = saved_errno;
	return -1;
    }
    return 0;
}

/* And same, for those with their own lockspace */
int dlm_ls_pthread_init(dlm_lshandle_t ls)
{
    struct dlm_ls_info *lsinfo = (struct dlm_ls_info *)ls;

    if (lsinfo->tid)
    {
	errno = EEXIST;
	return -1;
    }

    return pthread_create(&lsinfo->tid, NULL, dlm_recv_thread, (void *)ls);
}
#endif

/*
 * Lockspace manipulation functions
 */

/*
 * Privileged users (checked by the kernel)
 * can create/release lockspaces
 */
dlm_lshandle_t dlm_create_lockspace(const char *name, mode_t mode)
{
    int status;
    int minor;
    struct stat st;
    int stat_ret;
    int create_dev = 1;
    char dev_name[PATH_MAX];
    struct dlm_ls_info *newls;
    char reqbuf[sizeof(struct dlm_write_request) + strlen(name)];
    struct dlm_write_request *req = (struct dlm_write_request *)reqbuf;

    /* We use the control device for creating lockspaces. */
    if (open_control_device())
	return NULL;

    newls = malloc(sizeof(struct dlm_ls_info));
    if (!newls)
	return NULL;

    ls_dev_name(name, dev_name, sizeof(dev_name));

    req->cmd = DLM_USER_CREATE_LOCKSPACE;
    set_version(req);

    /* Make the default lockspace free itself when all users have released it */
    if (strcmp(name, DEFAULT_LOCKSPACE) == 0)
	    req->i.lspace.flags = DLM_USER_LSFLG_AUTOFREE;
    else
	    req->i.lspace.flags = 0;
    strcpy(req->i.lspace.name, name);
    minor = write(control_fd, req, sizeof(*req) + strlen(name));

    if (minor < 0 && errno != EEXIST)
    {
	free(newls);
	return NULL;
    }

    /* If the lockspace already exists, we don't get the minor
     * number returned. If the device exists we check the minor number.
     * If the device doesn't exist then we have to look in /proc/misc
     * to find the minor number.
     */
    stat_ret = stat(dev_name, &st);

    /* Check if the device exists and has the right modes */
    if (!stat_ret) {
	if (S_ISCHR(st.st_mode) && st.st_rdev == makedev(MISC_MAJOR, minor))
	    create_dev = 0;
    }
    else {
	if (minor <= 0)
	    minor = find_minor_from_proc(name);
    }

    if (create_dev && minor > 0) {

	unlink(dev_name);

	/* Now try to create the device, EEXIST is OK cos it must have
	   been devfs or udev that created it */
	status = mknod(dev_name, S_IFCHR | mode, makedev(MISC_MAJOR, minor));
	if (status == -1 && errno != EEXIST)
	{
	    /* Try to remove it */
	    req->cmd = DLM_USER_REMOVE_LOCKSPACE;
	    req->i.lspace.minor = minor;
	    write(control_fd, req, sizeof(*req));
	    free(newls);
	    return NULL;
	}
#ifdef HAVE_SELINUX
	set_selinux_context(dev_name);
#endif
    }

    /* Open it and return the struct as a handle */
    newls->fd = open(dev_name, O_RDWR);
    if (newls->fd == -1)
    {
	int saved_errno = errno;
	free(newls);
	errno = saved_errno;
	return NULL;
    }
    newls->tid = 0;

    return (dlm_lshandle_t)newls;
}


int dlm_release_lockspace(const char *name, dlm_lshandle_t ls, int force)
{
    int status;
    char dev_name[PATH_MAX];
    struct stat st;
    struct dlm_ls_info *lsinfo = (struct dlm_ls_info *)ls;
    struct dlm_write_request req;

    /* We need the minor number */
    if (fstat(lsinfo->fd, &st))
	return -1;

    /* Close the lockspace first if it's in use */
    ls_pthread_cleanup(lsinfo);

    if (open_control_device())
	return -1;

    if (force)
        req.i.lspace.flags = 2;
    else
	req.i.lspace.flags = 0;

    set_version(&req);
    req.cmd = DLM_USER_REMOVE_LOCKSPACE;
    req.i.lspace.minor = minor(st.st_rdev);

    status = write(control_fd, &req, sizeof(req));
    if (status < 0)
	return status;

    /* Remove the device */
    ls_dev_name(name, dev_name, sizeof(dev_name));

    status = unlink(dev_name);

    /* ENOENT is OK here if devfs has cleaned up */
    if (status == 0 ||
	(status == -1 && errno == ENOENT))
	return 0;
    else
	return -1;
}

/*
 * Normal users just open/close lockspaces
 */
dlm_lshandle_t dlm_open_lockspace(const char *name)
{
    char dev_name[PATH_MAX];
    struct dlm_ls_info *newls;
    int saved_errno;

    newls = malloc(sizeof(struct dlm_ls_info));
    if (!newls)
	return NULL;

    newls->tid = 0;
    ls_dev_name(name, dev_name, sizeof(dev_name));

    newls->fd = open(dev_name, O_RDWR);
    saved_errno = errno;

    if (newls->fd == -1)
    {
	free(newls);
	errno = saved_errno;
	return NULL;
    }

    return (dlm_lshandle_t)newls;
}

int dlm_close_lockspace(dlm_lshandle_t ls)
{
    struct dlm_ls_info *lsinfo = (struct dlm_ls_info *)ls;

    ls_pthread_cleanup(lsinfo);
    return 0;
}
