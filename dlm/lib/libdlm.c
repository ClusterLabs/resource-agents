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
#include "dlm.h"
#define BUILDING_LIBDLM
#include "libdlm.h"
#include "dlm_device.h"

#define MISC_PREFIX "/dev/misc/"
#define DLM_MISC_PREFIX MISC_PREFIX "dlm_"
#define DLM_CONTROL_DEV "dlm-control"

/* This is the name of the control device */
#define DLM_CTL_DEVICE_NAME MISC_PREFIX DLM_CONTROL_DEV

/* One of these per lockspace in use by the application */
struct dlm_ls_info
{
    int fd;
    pthread_t tid;
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

/* Used for the synchronous and "simplified, synchronous" API routines */
struct lock_wait
{
    pthread_cond_t  cond;
    pthread_mutex_t mutex;
    struct dlm_lksb lksb;
};

static void sync_ast_routine(void *arg)
{
    struct lock_wait *lwait = arg;

    pthread_mutex_lock(&lwait->mutex);
    pthread_cond_signal(&lwait->cond);
    pthread_mutex_unlock(&lwait->mutex);
}

static void dummy_ast_routine(void *arg)
{
}

#ifdef _REENTRANT
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

static void set_version(struct dlm_lock_params *params)
{
    params->version[0] = DLM_DEVICE_VERSION_MAJOR;
    params->version[1] = DLM_DEVICE_VERSION_MINOR;
    params->version[2] = DLM_DEVICE_VERSION_PATCH;
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
	ls = dlm_open_lockspace("default");
	if (!ls)
    	    ls = dlm_create_lockspace("default", 0600);
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

    /* Make sure the parent directory exists */
    status = mkdir(MISC_PREFIX, 0600);
    if (status != 0 && errno != EEXIST)
    {
	return status;
    }

    pmisc = fopen("/proc/misc", "r");
    if (!pmisc)
	return -1;

    while (!feof(pmisc))
    {
	fscanf(pmisc, "%d %s\n", &minor, name);
	if (strcmp(name, DLM_CONTROL_DEV) == 0)
	{
	    status = mknod(DLM_CTL_DEVICE_NAME, S_IFCHR | 0600, makedev(MISC_MAJOR, minor));
	    saved_errno = errno;
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
    struct dlm_lock_result result;
    int status;

    status = read(fd, &result, sizeof(result));
    if (status != sizeof(result))
	return -1;

    /* Copy lksb to user's buffer - except the LVB ptr */
    memcpy(result.user_lksb, &result.lksb, sizeof(struct dlm_lksb) - sizeof(char*));

    /* Flip the status. Kernel space likes negative return codes,
       userspace positive ones */
    result.user_lksb->sb_status = -result.user_lksb->sb_status;

    /* Call AST */
    if (result.astaddr)
       result.astaddr(result.astparam);
    return 0;
}

/* Helper routine which supports the synchronous DLM calls. This
   writes a parameter block down to the DLM and waits for the
   operation to complete. This hides the different completion mechanism
   used when called from the main thread or the DLM 'AST' thread.
*/
static int sync_write(struct dlm_ls_info *lsinfo, struct dlm_lock_params *params, int len)
{
    int	status;
    struct lock_wait lwait;

    if (pthread_self() == lsinfo->tid)
    {
        /* This is the DLM worker thread, don't use lwait to sync */
	params->castaddr  = dummy_ast_routine;
	params->castparam = NULL;

	status = write(lsinfo->fd, params, len);
	if (status != len)
	    return -1;
	while (params->lksb->sb_status == EINPROG) {
	    do_dlm_dispatch(lsinfo->fd);
	}
    }
    else
    {
	pthread_cond_init(&lwait.cond, NULL);
	pthread_mutex_init(&lwait.mutex, NULL);
	pthread_mutex_lock(&lwait.mutex);

	params->castaddr  = sync_ast_routine;
	params->castparam = &lwait;

	status = write(lsinfo->fd, params, len);
	if (status != len)
	    return -1;
	pthread_cond_wait(&lwait.cond, &lwait.mutex);
	pthread_mutex_unlock(&lwait.mutex);
    }
    return 0;	/* lock status is in the lksb */
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

    return dlm_ls_lock_wait(default_ls, mode, lksb, flags, name, namelen, parent,
			    bastarg, bastaddr, range);
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
    char param_buf[sizeof(struct dlm_lock_params) + DLM_RESNAME_MAXLEN];
    struct dlm_lock_params *params =  (struct dlm_lock_params *)param_buf;
    struct dlm_ls_info *lsinfo = (struct dlm_ls_info *)ls;
    int status;

    set_version(params);

    params->cmd = DLM_USER_LOCK;
    params->mode = mode;
    params->flags = flags;
    params->lkid = lksb->sb_lkid;
    params->parent = parent;
    params->lksb = lksb;
    params->castaddr  = astaddr;
    params->bastaddr = bastaddr;
    params->castparam = astarg;	/* completion and blocking ast arg are the same */
    params->bastparam = astarg;
    if (range)
    {
	params->range.ra_start = range->ra_start;
	params->range.ra_end = range->ra_end;
    }
    else
    {
	params->range.ra_start = 0L;
	params->range.ra_end = 0L;
    }
    if (flags & LKF_CONVERT)
    {
	params->namelen = 0;
    }
    else
    {
	if (namelen > DLM_RESNAME_MAXLEN)
	{
	    errno = EINVAL;
	    return -1;
	}
	params->namelen = namelen;
	memcpy(params->name, name, namelen);
    }
    len = sizeof(struct dlm_lock_params) + params->namelen - 1;
    lksb->sb_status = EINPROG;

    status = write(lsinfo->fd, params, len);
    if (status != len)
	return -1;
    else
	return 0;
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
    int len;
    char param_buf[sizeof(struct dlm_lock_params) + DLM_RESNAME_MAXLEN];
    struct dlm_lock_params *params =  (struct dlm_lock_params *)param_buf;
    struct dlm_ls_info *lsinfo = (struct dlm_ls_info *)ls;
    int status;

    set_version(params);

    params->cmd = DLM_USER_LOCK;
    params->mode = mode;
    params->flags = flags;
    params->lkid = lksb->sb_lkid;
    params->parent = parent;
    params->lksb = lksb;
    params->bastaddr = bastaddr;
    params->bastparam = bastarg;

    if (range)
    {
        params->range.ra_start = range->ra_start;
	params->range.ra_end = range->ra_end;
    }
    else
    {
	params->range.ra_start = 0L;
	params->range.ra_end = 0L;
    }
    if (flags & LKF_CONVERT)
    {
	params->namelen = 0;
    }
    else
    {
	if (namelen > DLM_RESNAME_MAXLEN)
	{
	    errno = EINVAL;
	    return -1;
	}
	params->namelen = namelen;
	memcpy(params->name, name, namelen);
    }
    len = sizeof(struct dlm_lock_params) + params->namelen - 1;
    lksb->sb_status = EINPROG;

    status = sync_write(lsinfo, params, len);
    return status;
}
/* Unlock on default lockspace*/
int dlm_unlock(uint32_t lkid,
	       uint32_t flags, struct dlm_lksb *lksb, void *astarg)
{
    return dlm_ls_unlock(default_ls, lkid, flags, lksb, astarg);
}

int dlm_unlock_wait(uint32_t lkid,
               uint32_t flags, struct dlm_lksb *lksb)
{
    return dlm_ls_unlock_wait(default_ls, lkid, flags, lksb);
}

int dlm_ls_unlock(dlm_lshandle_t ls, uint32_t lkid,
		  uint32_t flags, struct dlm_lksb *lksb, void *astarg)
{
    struct dlm_lock_params params;
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

    set_version(&params);
    params.cmd = DLM_USER_UNLOCK;
    params.lkid = lkid;
    params.flags = flags;
    params.lksb  = lksb;
    params.castparam = astarg;
	    /* DLM_USER_UNLOCK will default to existing completion AST */
    params.castaddr = 0;
    lksb->sb_status = EINPROG;

    status = write(lsinfo->fd, &params, sizeof(params));
    if (status != sizeof(params))
	return -1;
    else
	return 0;
}

int dlm_ls_unlock_wait(dlm_lshandle_t ls, uint32_t lkid,
		  uint32_t flags, struct dlm_lksb *lksb)
{
    struct dlm_lock_params params;
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

    set_version(&params);
    params.cmd = DLM_USER_UNLOCK;
    params.lkid = lkid;
    params.flags = flags;
    params.lksb  = lksb;
    lksb->sb_status = EINPROG;

    status = sync_write(lsinfo, &params, sizeof(params));
    return status;
}

int dlm_ls_query(dlm_lshandle_t lockspace,
		 struct dlm_lksb *lksb,
		 int query,
		 struct dlm_queryinfo *qinfo,
		 void (*astaddr) (void *astarg),
		 void *astarg)
{
    struct dlm_lock_params params;
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

    set_version(&params);
    params.cmd = DLM_USER_QUERY;
    params.flags = query;
    params.lksb  = lksb;
    params.castparam = astarg;
    params.castaddr = astaddr;
    lksb->sb_lvbptr = (char *)qinfo;
    lksb->sb_status = EINPROG;

    status = write(lsinfo->fd, &params, sizeof(params));
    if (status != sizeof(params))
	return -1;
    else
	return 0;
}
int dlm_ls_query_wait(dlm_lshandle_t lockspace,
		 struct dlm_lksb *lksb,
		 int query,
		 struct dlm_queryinfo *qinfo)
{
    struct dlm_lock_params params;
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

    set_version(&params);
    params.cmd = DLM_USER_QUERY;
    params.flags = query;
    params.lksb  = lksb;
    lksb->sb_lvbptr = (char *)qinfo;
    lksb->sb_status = EINPROG;
    status = sync_write(lsinfo, &params, sizeof(params));
    return status;
}

int dlm_query(struct dlm_lksb *lksb,
	      int query,
	      struct dlm_queryinfo *qinfo,
	      void (*astaddr) (void *astarg),
	      void *astarg)
{
    return dlm_ls_query(default_ls, lksb, query, qinfo, astaddr, astarg);
}

int dlm_query_wait(struct dlm_lksb *lksb,
	      int query,
	      struct dlm_queryinfo *qinfo)
{
    return dlm_ls_query_wait(default_ls, lksb, query, qinfo);
}


/* These two routines for for users that want to
 * do their own fd handling.
 * This allows a non-threaded app to use the DLM.
 */
int dlm_get_fd()
{
    if (default_ls)
	return default_ls->fd;
    else
	return -1;
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
    int create_dev;
    char dev_name[PATH_MAX];
    struct dlm_ls_info *newls;

    /* We use the control device for creating lockspaces. */
    if (open_control_device())
	return NULL;

    newls = malloc(sizeof(struct dlm_ls_info));
    if (!newls)
	return NULL;

    ls_dev_name(name, dev_name, sizeof(dev_name));

    minor = ioctl(control_fd, DLM_CREATE_LOCKSPACE, name);
    if (minor < 0 && errno != EEXIST)
    {
	free(newls);
	return NULL;
    }

    /* If the lockspace already exists, don't try to create
     * the device node - we don't know the minor number anyway!
     */
    if (errno == EEXIST)
	create_dev = 0;
    else
	create_dev = 1;

    /* Check if the device exists and has the right modes */
    if (create_dev && !stat(dev_name, &st)) {
	if (S_ISCHR(st.st_mode) && st.st_rdev == makedev(MISC_MAJOR, minor))
	    create_dev = 0;
    }

    if (create_dev) {

	unlink(dev_name);

	/* Now try to create the device, EEXIST is OK cos it must have
	   been devfs or udev that created it */
	status = mknod(dev_name, S_IFCHR | mode, makedev(MISC_MAJOR, minor));
	if (status == -1 && errno != EEXIST)
	{
	    /* Try to remove it */
	    ioctl(control_fd, DLM_RELEASE_LOCKSPACE, minor);
	    free(newls);
	    return NULL;
	}
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
    int cmd = DLM_RELEASE_LOCKSPACE;
    char dev_name[PATH_MAX];
    struct stat st;
    struct dlm_ls_info *lsinfo = (struct dlm_ls_info *)ls;

    /* We need the minor number */
    if (fstat(lsinfo->fd, &st))
	return -1;

    /* Close the lockspace first if it's in use */
    ls_pthread_cleanup(lsinfo);

    if (open_control_device())
	return -1;

    if (force)
	cmd = DLM_FORCE_RELEASE_LOCKSPACE;

    status = ioctl(control_fd, cmd, minor(st.st_rdev));
    if (status)
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
