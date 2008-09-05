#ifdef _REENTRANT
#include <pthread.h>
#endif
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <linux/major.h>
#ifdef HAVE_SELINUX
#include <selinux/selinux.h>
#endif
#include <linux/types.h>
#include <linux/dlm.h>
#define BUILDING_LIBDLM
#include "libdlm.h"
#include <linux/dlm_device.h>

#define MISC_PREFIX		"/dev/misc/"
#define DLM_PREFIX		"dlm_"
#define DLM_MISC_PREFIX		MISC_PREFIX DLM_PREFIX
#define DLM_CONTROL_NAME	"dlm-control"
#define DLM_CONTROL_PATH	MISC_PREFIX DLM_CONTROL_NAME
#define DEFAULT_LOCKSPACE	"default"

/*
 * V5 of the dlm_device.h kernel/user interface structs
 */

struct dlm_lock_params_v5 {
	__u8 mode;
	__u8 namelen;
	__u16 flags;
	__u32 lkid;
	__u32 parent;
	void *castparam;
	void *castaddr;
	void *bastparam;
	void *bastaddr;
	struct dlm_lksb *lksb;
	char lvb[DLM_USER_LVB_LEN];
	char name[0];
};

struct dlm_write_request_v5 {
	__u32 version[3];
	__u8 cmd;
	__u8 is64bit;
	__u8 unused[2];

	union  {
		struct dlm_lock_params_v5 lock;
		struct dlm_lspace_params lspace;
	} i;
};

struct dlm_lock_result_v5 {
	__u32 length;
	void *user_astaddr;
	void *user_astparam;
	struct dlm_lksb *user_lksb;
	struct dlm_lksb lksb;
	__u8 bast_mode;
	__u8 unused[3];
	/* Offsets may be zero if no data is present */
	__u32 lvb_offset;
};


/*
 * One of these per lockspace in use by the application
 */

struct dlm_ls_info {
    int fd;
#ifdef _REENTRANT
    pthread_t tid;
#else
    int tid;
#endif
};

/*
 * The default lockspace.
 * I've resisted putting locking around this as the user should be
 * "sensible" and only do lockspace operations either in the
 * main thread or ... carefully...
 */

static struct dlm_ls_info *default_ls = NULL;
static int control_fd = -1;
static struct dlm_device_version kernel_version;
static int kernel_version_detected = 0;


static int release_lockspace(uint32_t minor, uint32_t flags);


static void ls_dev_name(const char *lsname, char *devname, int devlen)
{
	snprintf(devname, devlen, DLM_MISC_PREFIX "%s", lsname);
}

static void dummy_ast_routine(void *arg)
{
}

#ifdef _REENTRANT
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


static void set_version_v5(struct dlm_write_request_v5 *req)
{
	req->version[0] = kernel_version.version[0];
	req->version[1] = kernel_version.version[1];
	req->version[2] = kernel_version.version[2];
	if (sizeof(long) == sizeof(long long))
		req->is64bit = 1;
	else
		req->is64bit = 0;
}

static void set_version_v6(struct dlm_write_request *req)
{
	req->version[0] = kernel_version.version[0];
	req->version[1] = kernel_version.version[1];
	req->version[2] = kernel_version.version[2];
	if (sizeof(long) == sizeof(long long))
		req->is64bit = 1;
	else
		req->is64bit = 0;
}

static int open_default_lockspace(void)
{
	if (!default_ls) {
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

static void detect_kernel_version(void)
{
	struct dlm_device_version v;
	int rv;

	rv = read(control_fd, &v, sizeof(struct dlm_device_version));
	if (rv < 0) {
		kernel_version.version[0] = 5;
		kernel_version.version[1] = 0;
		kernel_version.version[2] = 0;
	} else {
		kernel_version.version[0] = v.version[0];
		kernel_version.version[1] = v.version[1];
		kernel_version.version[2] = v.version[2];
	}

	kernel_version_detected = 1;
}

static int find_control_minor(int *minor)
{
	FILE *f;
	char name[256];
	int found = 0, m = 0;

	f = fopen("/proc/misc", "r");
	if (!f)
		return -1;

	while (!feof(f)) {
		if (fscanf(f, "%d %s", &m, name) != 2)
			continue;
		if (strcmp(name, DLM_CONTROL_NAME))
			continue;
		found = 1;
		break;
	}
	fclose(f);

	if (found) {
		*minor = m;
		return 0;
	}
	return -1;
}

static int open_control_device(void)
{
	struct stat st;
	int i, rv, minor, found = 0;

	if (control_fd > -1)
		goto out;

	rv = find_control_minor(&minor);
	if (rv < 0)
		return -1;

	/* wait for udev to create the device */

	for (i = 0; i < 10; i++) {
		if (stat(DLM_CONTROL_PATH, &st) == 0 &&
		    minor(st.st_rdev) == minor) {
			found = 1;
			break;
		}
		sleep(1);
		continue;
	}

	if (!found)
		return -1;

	control_fd = open(DLM_CONTROL_PATH, O_RDWR);
	if (control_fd == -1)
		return -1;

 out:
	fcntl(control_fd, F_SETFD, 1);

	if (!kernel_version_detected)
		detect_kernel_version();
	return 0;
}

/* the max number of characters in a sysfs device name, not including \0 */
#define MAX_SYSFS_NAME 19

static int find_udev_device(const char *lockspace, int minor, char *udev_path)
{
	char basename[PATH_MAX];
	char tmp_path[PATH_MAX];
	DIR *d;
	struct dirent *de;
	struct stat st;
	size_t basename_len;
	int i;

	ls_dev_name(lockspace, udev_path, PATH_MAX);
	snprintf(basename, PATH_MAX, DLM_PREFIX "%s", lockspace);
	basename_len = strlen(basename);

	for (i = 0; i < 10; i++) {

		/* look for a device with the full name */

		if (stat(udev_path, &st) == 0 && minor(st.st_rdev) == minor)
			return 0;

		if (basename_len < MAX_SYSFS_NAME) {
			sleep(1);
			continue;
		}

		/* look for a device with a truncated name */

		d = opendir(MISC_PREFIX);
		while ((de = readdir(d))) {
			if (de->d_name[0] == '.')
				continue;
			if (strlen(de->d_name) < MAX_SYSFS_NAME)
				continue;
			if (strncmp(de->d_name, basename, MAX_SYSFS_NAME))
				continue;
			snprintf(tmp_path, PATH_MAX, MISC_PREFIX "%s",
				 de->d_name);
			if (stat(tmp_path, &st))
				continue;
			if (minor(st.st_rdev) != minor)
				continue;

			/* truncated name */
			strncpy(udev_path, tmp_path, PATH_MAX);
			closedir(d);
			return 0;
		}
		closedir(d);
		sleep(1);
	}

	return -1;
}

/*
 * do_dlm_dispatch()
 * Read an ast from the kernel.
 */

static int do_dlm_dispatch_v5(int fd)
{
	char resultbuf[sizeof(struct dlm_lock_result_v5) + DLM_USER_LVB_LEN];
	struct dlm_lock_result_v5 *result = (struct dlm_lock_result_v5 *)resultbuf;
	char *fullresult = NULL;
	int status;
	void (*astaddr)(void *astarg);

	status = read(fd, result, sizeof(resultbuf));
	if (status <= 0)
		return -1;

	/* This shouldn't happen any more, can probably be removed */

	if (result->length != status) {
		int newstat;

		fullresult = malloc(result->length);
		if (!fullresult)
			return -1;

		newstat = read(fd, (struct dlm_lock_result_v5 *)fullresult,
			       result->length);

		/* If it read OK then use the new data. otherwise we can
		   still deliver the AST, it just might not have all the
		   info in it...hmmm */

		if (newstat == result->length)
			result = (struct dlm_lock_result_v5 *)fullresult;
	} else {
		fullresult = resultbuf;
	}


	/* Copy lksb to user's buffer - except the LVB ptr */
	memcpy(result->user_lksb, &result->lksb,
	       sizeof(struct dlm_lksb) - sizeof(char*));

	/* Flip the status. Kernel space likes negative return codes,
	   userspace positive ones */
	result->user_lksb->sb_status = -result->user_lksb->sb_status;

	/* Copy optional items */
	if (result->lvb_offset)
		memcpy(result->user_lksb->sb_lvbptr,
		       fullresult + result->lvb_offset, DLM_LVB_LEN);

	/* Call AST */
	if (result->user_astaddr) {
		astaddr = result->user_astaddr;
		astaddr(result->user_astparam);
	}

	if (fullresult != resultbuf)
		free(fullresult);

	return 0;
}

static int do_dlm_dispatch_v6(int fd)
{
	char resultbuf[sizeof(struct dlm_lock_result) + DLM_USER_LVB_LEN];
	struct dlm_lock_result *result = (struct dlm_lock_result *)resultbuf;
	int status;
	void (*astaddr)(void *astarg);

	status = read(fd, result, sizeof(resultbuf));
	if (status <= 0)
		return -1;

	/* Copy lksb to user's buffer - except the LVB ptr */
	memcpy(result->user_lksb, &result->lksb,
	       sizeof(struct dlm_lksb) - sizeof(char*));

	/* Copy lvb to user's buffer */
	if (result->lvb_offset)
		memcpy(result->user_lksb->sb_lvbptr,
		       (char *)result + result->lvb_offset, DLM_LVB_LEN);

	result->user_lksb->sb_status = -result->user_lksb->sb_status;

	if (result->user_astaddr) {
		astaddr = result->user_astaddr;
		astaddr(result->user_astparam);
	}

	return 0;
}

static int do_dlm_dispatch(int fd)
{
	if (kernel_version.version[0] == 5)
		return do_dlm_dispatch_v5(fd);
	else
		return do_dlm_dispatch_v6(fd);
}


/*
 * sync_write()
 * Helper routine which supports the synchronous DLM calls. This
 * writes a parameter block down to the DLM and waits for the
 * operation to complete. This hides the different completion mechanism
 * used when called from the main thread or the DLM 'AST' thread.
 */

#ifdef _REENTRANT

static int sync_write_v5(struct dlm_ls_info *lsinfo,
			 struct dlm_write_request_v5 *req, int len)
{
	struct lock_wait lwait;
	int status;

	if (pthread_self() == lsinfo->tid) {
		/* This is the DLM worker thread, don't use lwait to sync */
		req->i.lock.castaddr  = dummy_ast_routine;
		req->i.lock.castparam = NULL;

		status = write(lsinfo->fd, req, len);
		if (status < 0)
			return -1;

		while (req->i.lock.lksb->sb_status == EINPROG) {
			do_dlm_dispatch_v5(lsinfo->fd);
		}
	} else {
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

	return status; /* lock status is in the lksb */
}

static int sync_write_v6(struct dlm_ls_info *lsinfo,
			 struct dlm_write_request *req, int len)
{
	struct lock_wait lwait;
	int status;

	if (pthread_self() == lsinfo->tid) {
		/* This is the DLM worker thread, don't use lwait to sync */
		req->i.lock.castaddr  = dummy_ast_routine;
		req->i.lock.castparam = NULL;

		status = write(lsinfo->fd, req, len);
		if (status < 0)
			return -1;

		while (req->i.lock.lksb->sb_status == EINPROG) {
			do_dlm_dispatch_v6(lsinfo->fd);
		}
	} else {
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

	return status; /* lock status is in the lksb */
}

#else /* _REENTRANT */

static int sync_write_v5(struct dlm_ls_info *lsinfo,
			 struct dlm_write_request_v5 *req, int len)
{
	int status;

	req->i.lock.castaddr  = dummy_ast_routine;
	req->i.lock.castparam = NULL;

	status = write(lsinfo->fd, req, len);
	if (status < 0)
		return -1;

	while (req->i.lock.lksb->sb_status == EINPROG) {
		do_dlm_dispatch_v5(lsinfo->fd);
	}

	errno = req->i.lock.lksb->sb_status;
	if (errno && errno != EUNLOCK)
		return -1;
	return 0;
}

static int sync_write_v6(struct dlm_ls_info *lsinfo,
			 struct dlm_write_request *req, int len)
{
	int status;

	req->i.lock.castaddr  = dummy_ast_routine;
	req->i.lock.castparam = NULL;

	status = write(lsinfo->fd, req, len);
	if (status < 0)
		return -1;

	while (req->i.lock.lksb->sb_status == EINPROG) {
		do_dlm_dispatch_v6(lsinfo->fd);
	}

	errno = req->i.lock.lksb->sb_status;
	if (errno && errno != EUNLOCK)
		return -1;
	return 0;
}

#endif /* _REENTRANT */


/*
 * Lock
 * All the ways to request/convert a lock
 */

static int ls_lock_v5(dlm_lshandle_t ls,
		uint32_t mode,
		struct dlm_lksb *lksb,
		uint32_t flags,
		const void *name,
		unsigned int namelen,
		uint32_t parent,
		void (*astaddr) (void *astarg),
		void *astarg,
		void (*bastaddr) (void *astarg))
{
	char parambuf[sizeof(struct dlm_write_request_v5) + DLM_RESNAME_MAXLEN];
	struct dlm_write_request_v5 *req = (struct dlm_write_request_v5 *)parambuf;
	struct dlm_ls_info *lsinfo = (struct dlm_ls_info *)ls;
	int status;
	int len;

	memset(req, 0, sizeof(*req));
	set_version_v5(req);

	req->cmd = DLM_USER_LOCK;
	req->i.lock.mode = mode;
	req->i.lock.flags = (flags & ~LKF_WAIT);
	req->i.lock.lkid = lksb->sb_lkid;
	req->i.lock.parent = parent;
	req->i.lock.lksb = lksb;
	req->i.lock.castaddr = astaddr;
	req->i.lock.bastaddr = bastaddr;
	req->i.lock.castparam = astarg;	/* same comp and blocking ast arg */
	req->i.lock.bastparam = astarg;

	if (flags & LKF_CONVERT) {
		req->i.lock.namelen = 0;
	} else {
		if (namelen > DLM_RESNAME_MAXLEN) {
			errno = EINVAL;
			return -1;
		}
		req->i.lock.namelen = namelen;
		memcpy(req->i.lock.name, name, namelen);
	}

	if (flags & LKF_VALBLK) {
		memcpy(req->i.lock.lvb, lksb->sb_lvbptr, DLM_LVB_LEN);
	}

	len = sizeof(struct dlm_write_request_v5) + namelen;
	lksb->sb_status = EINPROG;

	if (flags & LKF_WAIT)
		status = sync_write_v5(lsinfo, req, len);
	else
		status = write(lsinfo->fd, req, len);

	if (status < 0)
		return -1;

	/*
	 * the lock id is the return value from the write on the device
	 */

	if (status > 0)
		lksb->sb_lkid = status;
	return 0;
}

static int ls_lock_v6(dlm_lshandle_t ls,
		uint32_t mode,
		struct dlm_lksb *lksb,
		uint32_t flags,
		const void *name,
		unsigned int namelen,
		uint32_t parent,
		void (*astaddr) (void *astarg),
		void *astarg,
		void (*bastaddr) (void *astarg),
		uint64_t *xid,
		uint64_t *timeout)
{
	char parambuf[sizeof(struct dlm_write_request) + DLM_RESNAME_MAXLEN];
	struct dlm_write_request *req = (struct dlm_write_request *)parambuf;
	struct dlm_ls_info *lsinfo = (struct dlm_ls_info *)ls;
	int status;
	int len;

	memset(req, 0, sizeof(*req));
	set_version_v6(req);

	req->cmd = DLM_USER_LOCK;
	req->i.lock.mode = mode;
	req->i.lock.flags = (flags & ~LKF_WAIT);
	req->i.lock.lkid = lksb->sb_lkid;
	req->i.lock.parent = parent;
	req->i.lock.lksb = lksb;
	req->i.lock.castaddr = astaddr;
	req->i.lock.bastaddr = bastaddr;
	req->i.lock.castparam = astarg;	/* same comp and blocking ast arg */
	req->i.lock.bastparam = astarg;

	if (xid)
		req->i.lock.xid = *xid;
	if (timeout)
		req->i.lock.timeout = *timeout;

	if (flags & LKF_CONVERT) {
		req->i.lock.namelen = 0;
	} else {
		if (namelen > DLM_RESNAME_MAXLEN) {
			errno = EINVAL;
			return -1;
		}
		req->i.lock.namelen = namelen;
		memcpy(req->i.lock.name, name, namelen);
	}

	if (flags & LKF_VALBLK) {
		memcpy(req->i.lock.lvb, lksb->sb_lvbptr, DLM_LVB_LEN);
	}

	len = sizeof(struct dlm_write_request) + namelen;
	lksb->sb_status = EINPROG;

	if (flags & LKF_WAIT)
		status = sync_write_v6(lsinfo, req, len);
	else
		status = write(lsinfo->fd, req, len);

	if (status < 0)
		return -1;

	/*
	 * the lock id is the return value from the write on the device
	 */

	if (status > 0)
		lksb->sb_lkid = status;
	return 0;
}

static int ls_lock(dlm_lshandle_t ls,
		uint32_t mode,
		struct dlm_lksb *lksb,
		uint32_t flags,
		const void *name,
		unsigned int namelen,
		uint32_t parent,
		void (*astaddr) (void *astarg),
		void *astarg,
		void (*bastaddr) (void *astarg),
		void *range)
{
	/* no support for range locks */
	if (range) {
		errno = ENOSYS;
		return -1;
	}

	if (flags & LKF_VALBLK && !lksb->sb_lvbptr) {
		errno = EINVAL;
		return -1;
	}

	if (kernel_version.version[0] == 5)
		return ls_lock_v5(ls, mode, lksb, flags, name, namelen, parent,
				  astaddr, astarg, bastaddr);
	else
		return ls_lock_v6(ls, mode, lksb, flags, name, namelen, parent,
				  astaddr, astarg, bastaddr, NULL, NULL);
}

/*
 * Extended async locking in own lockspace
 */
int dlm_ls_lockx(dlm_lshandle_t ls,
		 uint32_t mode,
		 struct dlm_lksb *lksb,
		 uint32_t flags,
		 const void *name,
		 unsigned int namelen,
		 uint32_t parent,
		 void (*astaddr) (void *astarg),
		 void *astarg,
		 void (*bastaddr) (void *astarg),
		 uint64_t *xid,
		 uint64_t *timeout)
{
	if (kernel_version.version[0] < 6) {
		errno = ENOSYS;
		return -1;
	}

	return ls_lock_v6(ls, mode, lksb, flags, name, namelen, parent,
			  astaddr, astarg, bastaddr, xid, timeout);
}

/*
 * Async locking in own lockspace
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
		void *range)
{
	return ls_lock(ls, mode, lksb, flags, name, namelen, parent,
		       astaddr, astarg, bastaddr, range);
}

/*
 * Sync locking in own lockspace
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
		     void *range)
{
	return ls_lock(ls, mode, lksb, flags | LKF_WAIT, name, namelen, parent,
		       NULL, bastarg, bastaddr, range);
}

/*
 * Async locking in the default lockspace
 */
int dlm_lock(uint32_t mode,
	     struct dlm_lksb *lksb,
	     uint32_t flags,
	     const void *name,
	     unsigned int namelen,
	     uint32_t parent,
	     void (*astaddr) (void *astarg),
	     void *astarg,
	     void (*bastaddr) (void *astarg),
	     void *range)
{
	if (open_default_lockspace())
		return -1;

	return ls_lock(default_ls, mode, lksb, flags, name, namelen, parent,
		       astaddr, astarg, bastaddr, range);
}

/*
 * Sync locking in the default lockspace
 */
int dlm_lock_wait(uint32_t mode,
		     struct dlm_lksb *lksb,
		     uint32_t flags,
		     const void *name,
		     unsigned int namelen,
		     uint32_t parent,
		     void *bastarg,
		     void (*bastaddr) (void *bastarg),
		     void *range)
{
	if (open_default_lockspace())
		return -1;

	return ls_lock(default_ls, mode, lksb, flags | LKF_WAIT, name, namelen,
		       parent, NULL, bastarg, bastaddr, range);
}


/*
 * Unlock
 * All the ways to unlock/cancel a lock
 */

static int ls_unlock_v5(struct dlm_ls_info *lsinfo, uint32_t lkid,
			uint32_t flags, struct dlm_lksb *lksb, void *astarg)
{
	struct dlm_write_request_v5 req;

	set_version_v5(&req);
	req.cmd = DLM_USER_UNLOCK;
	req.i.lock.lkid = lkid;
	req.i.lock.flags = (flags & ~LKF_WAIT);
	req.i.lock.lksb  = lksb;
	req.i.lock.castparam = astarg;
	/* DLM_USER_UNLOCK will default to existing completion AST */
	req.i.lock.castaddr = 0;
	lksb->sb_status = EINPROG;

	if (flags & LKF_WAIT)
		return sync_write_v5(lsinfo, &req, sizeof(req));
	else
		return write(lsinfo->fd, &req, sizeof(req));
}

static int ls_unlock_v6(struct dlm_ls_info *lsinfo, uint32_t lkid,
			uint32_t flags, struct dlm_lksb *lksb, void *astarg)
{
	struct dlm_write_request req;

	set_version_v6(&req);
	req.cmd = DLM_USER_UNLOCK;
	req.i.lock.lkid = lkid;
	req.i.lock.flags = (flags & ~LKF_WAIT);
	req.i.lock.lksb  = lksb;
	req.i.lock.namelen = 0;
	req.i.lock.castparam = astarg;
	/* DLM_USER_UNLOCK will default to existing completion AST */
	req.i.lock.castaddr = 0;
	lksb->sb_status = EINPROG;

	if (flags & LKF_WAIT)
		return sync_write_v6(lsinfo, &req, sizeof(req));
	else
		return write(lsinfo->fd, &req, sizeof(req));
}

int dlm_ls_unlock(dlm_lshandle_t ls, uint32_t lkid, uint32_t flags,
		  struct dlm_lksb *lksb, void *astarg)
{
	struct dlm_ls_info *lsinfo = (struct dlm_ls_info *)ls;
	int status;

	if (ls == NULL) {
		errno = ENOTCONN;
		return -1;
	}

	if (!lkid) {
		errno = EINVAL;
		return -1;
	}

	if (kernel_version.version[0] == 5)
		status = ls_unlock_v5(lsinfo, lkid, flags, lksb, astarg);
	else
		status = ls_unlock_v6(lsinfo, lkid, flags, lksb, astarg);

	if (status < 0)
		return -1;
	return 0;
}

int dlm_ls_unlock_wait(dlm_lshandle_t ls, uint32_t lkid, uint32_t flags,
		       struct dlm_lksb *lksb)
{
	return dlm_ls_unlock(ls, lkid, flags | LKF_WAIT, lksb, NULL);
}

int dlm_unlock_wait(uint32_t lkid, uint32_t flags, struct dlm_lksb *lksb)
{
	return dlm_ls_unlock_wait(default_ls, lkid, flags | LKF_WAIT, lksb);
}

int dlm_unlock(uint32_t lkid, uint32_t flags, struct dlm_lksb *lksb,
	       void *astarg)
{
	return dlm_ls_unlock(default_ls, lkid, flags, lksb, astarg);
}

int dlm_ls_deadlock_cancel(dlm_lshandle_t ls, uint32_t lkid, uint32_t flags)
{
	struct dlm_ls_info *lsinfo = (struct dlm_ls_info *)ls;
	struct dlm_write_request req;

	if (kernel_version.version[0] < 6) {
		errno = ENOSYS;
		return -1;
	}

	if (ls == NULL) {
		errno = ENOTCONN;
		return -1;
	}

	if (!lkid) {
		errno = EINVAL;
		return -1;
	}

	set_version_v6(&req);
	req.cmd = DLM_USER_DEADLOCK;
	req.i.lock.lkid = lkid;
	req.i.lock.flags = flags;

	return write(lsinfo->fd, &req, sizeof(req));
}


/*
 * Purge
 * Clear away orphan locks
 */

int dlm_ls_purge(dlm_lshandle_t ls, int nodeid, int pid)
{
	struct dlm_write_request req;
	struct dlm_ls_info *lsinfo = (struct dlm_ls_info *)ls;
	int status;

	if (kernel_version.version[0] < 6) {
		errno = ENOSYS;
		return -1;
	}

	if (ls == NULL) {
		errno = ENOTCONN;
		return -1;
	}

	set_version_v6(&req);
	req.cmd = DLM_USER_PURGE;
	req.i.purge.nodeid = nodeid;
	req.i.purge.pid = pid;

	status = write(lsinfo->fd, &req, sizeof(req));

	if (status < 0)
		return -1;
	return 0;
}


/* These two routines for for users that want to
 * do their own fd handling.
 * This allows a non-threaded app to use the DLM.
 */
int dlm_get_fd(void)
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
 * Privileged users (checked by the kernel) can create/release lockspaces
 */

static int create_lockspace_v5(const char *name, uint32_t flags)
{
	char reqbuf[sizeof(struct dlm_write_request_v5) + DLM_LOCKSPACE_LEN];
	struct dlm_write_request_v5 *req = (struct dlm_write_request_v5 *)reqbuf;
	int namelen = strlen(name);
	int minor;

	memset(reqbuf, 0, sizeof(reqbuf));
	set_version_v5(req);

	req->cmd = DLM_USER_CREATE_LOCKSPACE;
	req->i.lspace.flags = flags;

	if (namelen > DLM_LOCKSPACE_LEN) {
		errno = EINVAL;
		return -1;
	}
	memcpy(req->i.lspace.name, name, namelen);

	minor = write(control_fd, req, sizeof(*req) + namelen);

	return minor;
}

static int create_lockspace_v6(const char *name, uint32_t flags)
{
	char reqbuf[sizeof(struct dlm_write_request) + DLM_LOCKSPACE_LEN];
	struct dlm_write_request *req = (struct dlm_write_request *)reqbuf;
	int namelen = strlen(name);
	int minor;

	memset(reqbuf, 0, sizeof(reqbuf));
	set_version_v6(req);

	req->cmd = DLM_USER_CREATE_LOCKSPACE;
	req->i.lspace.flags = flags;

	if (namelen > DLM_LOCKSPACE_LEN) {
		errno = EINVAL;
		return -1;
	}
	memcpy(req->i.lspace.name, name, namelen);

	minor = write(control_fd, req, sizeof(*req) + namelen);

	return minor;
}

static dlm_lshandle_t create_lockspace(const char *name, mode_t mode,
				       uint32_t flags)
{
	char dev_path[PATH_MAX];
	char udev_path[PATH_MAX];
	struct dlm_ls_info *newls;
	int error, saved_errno, minor;

	/* We use the control device for creating lockspaces. */
	if (open_control_device())
		return NULL;

	newls = malloc(sizeof(struct dlm_ls_info));
	if (!newls)
		return NULL;

	ls_dev_name(name, dev_path, sizeof(dev_path));

	if (kernel_version.version[0] == 5)
		minor = create_lockspace_v5(name, flags);
	else
		minor = create_lockspace_v6(name, flags);

	if (minor < 0)
		goto fail;

	/* Wait for udev to create the device; the device it creates may
	   have a truncated name due to the sysfs device name limit. */
	   
	error = find_udev_device(name, minor, udev_path);
	if (error)
		goto fail;

	/* If the symlink already exists, find_udev_device() will return
	   it and we'll skip this. */

	if (strcmp(dev_path, udev_path)) {
		error = symlink(udev_path, dev_path);
		if (error)
			goto fail;
	}

	/* Open it and return the struct as a handle */

	newls->fd = open(dev_path, O_RDWR);
	if (newls->fd == -1)
		goto fail;
	if (mode)
		fchmod(newls->fd, mode);
	newls->tid = 0;
	fcntl(newls->fd, F_SETFD, 1);
	return (dlm_lshandle_t)newls;

 fail:
	saved_errno = errno;
	free(newls);
	errno = saved_errno;
	return NULL;
}

dlm_lshandle_t dlm_new_lockspace(const char *name, mode_t mode, uint32_t flags)
{
	return create_lockspace(name, mode, flags);
}

dlm_lshandle_t dlm_create_lockspace(const char *name, mode_t mode)
{
	return create_lockspace(name, mode, 0);
}

static int release_lockspace_v5(uint32_t minor, uint32_t flags)
{
	struct dlm_write_request_v5 req;

	set_version_v5(&req);
	req.cmd = DLM_USER_REMOVE_LOCKSPACE;
	req.i.lspace.minor = minor;
	req.i.lspace.flags = flags;

	return write(control_fd, &req, sizeof(req));
}

static int release_lockspace_v6(uint32_t minor, uint32_t flags)
{
	struct dlm_write_request req;

	set_version_v6(&req);
	req.cmd = DLM_USER_REMOVE_LOCKSPACE;
	req.i.lspace.minor = minor;
	req.i.lspace.flags = flags;

	return write(control_fd, &req, sizeof(req));
}

static int release_lockspace(uint32_t minor, uint32_t flags)
{
	if (kernel_version.version[0] == 5)
		return release_lockspace_v5(minor, flags);
	else
		return release_lockspace_v6(minor, flags);
}

int dlm_release_lockspace(const char *name, dlm_lshandle_t ls, int force)
{
	char dev_path[PATH_MAX];
	struct stat st;
	struct dlm_ls_info *lsinfo = (struct dlm_ls_info *)ls;
	uint32_t flags = 0;
	int fd, is_symlink = 0;

	ls_dev_name(name, dev_path, sizeof(dev_path));
	if (!lstat(dev_path, &st) && S_ISLNK(st.st_mode))
		is_symlink = 1;

	/* We need the minor number */
	if (fstat(lsinfo->fd, &st))
		return -1;

	/* Close the lockspace first if it's in use */
	ls_pthread_cleanup(lsinfo);

	if (open_control_device())
		return -1;

	if (force)
		flags = DLM_USER_LSFLG_FORCEFREE;

	release_lockspace(minor(st.st_rdev), flags);

	if (!is_symlink)
		return 0;

	/* The following open is used to detect if our release was the last.
	   It will fail if our release was the last, because either:
	   . udev has already removed the truncated sysfs device name (ENOENT)
	   . the misc device has been deregistered in the kernel (ENODEV)
	     (the deregister completes before release returns)

	   So, if the open fails, we know that our release was the last,
	   udev will be removing the device with the truncated name (if it
	   hasn't already), and we should remove the symlink. */

	fd = open(dev_path, O_RDWR);
	if (fd < 0)
		unlink(dev_path);
	else
		close(fd); /* our release was not the last */

	return 0;
}

/*
 * Normal users just open/close lockspaces
 */

dlm_lshandle_t dlm_open_lockspace(const char *name)
{
	char dev_name[PATH_MAX];
	struct dlm_ls_info *newls;
	int saved_errno;

	/* Need to detect kernel version */
	if (open_control_device())
		return NULL;

	newls = malloc(sizeof(struct dlm_ls_info));
	if (!newls)
		return NULL;

	newls->tid = 0;
	ls_dev_name(name, dev_name, sizeof(dev_name));

	newls->fd = open(dev_name, O_RDWR);
	saved_errno = errno;

	if (newls->fd == -1) {
		free(newls);
		errno = saved_errno;
		return NULL;
	}
	fcntl(newls->fd, F_SETFD, 1);
	return (dlm_lshandle_t)newls;
}

int dlm_close_lockspace(dlm_lshandle_t ls)
{
	struct dlm_ls_info *lsinfo = (struct dlm_ls_info *)ls;

	ls_pthread_cleanup(lsinfo);
	return 0;
}

int dlm_kernel_version(uint32_t *major, uint32_t *minor, uint32_t *patch)
{
	if (open_control_device())
		return -1;
	*major = kernel_version.version[0];
	*minor = kernel_version.version[1];
	*patch = kernel_version.version[2];
	return 0;
}

void dlm_library_version(uint32_t *major, uint32_t *minor, uint32_t *patch)
{
	*major = DLM_DEVICE_VERSION_MAJOR;
	*minor = DLM_DEVICE_VERSION_MINOR;
	*patch = DLM_DEVICE_VERSION_PATCH;
}

