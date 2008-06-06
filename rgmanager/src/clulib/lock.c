/** @file
 * Locking.
 */
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <lock.h>
#include <sys/types.h>
#include <sys/select.h>
#include <pthread.h>

/* Default lockspace stuff */
static dlm_lshandle_t _default_ls = NULL;
static pthread_mutex_t _default_lock = PTHREAD_MUTEX_INITIALIZER;


static void
ast_function(void * __attribute__ ((unused)) arg)
{
}


static int
wait_for_dlm_event(dlm_lshandle_t ls)
{
	fd_set rfds;
	int fd = dlm_ls_get_fd(ls);

	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);

	if (select(fd + 1, &rfds, NULL, NULL, NULL) == 1)
		return dlm_dispatch(fd);

	return -1;
}


int
clu_ls_lock(dlm_lshandle_t ls,
	    int mode,
	    struct dlm_lksb *lksb,
	    int options,
            char *resource)
{
        int ret;

	if (!ls || !lksb || !resource || !strlen(resource)) {
		printf("%p %p %p %d\n", ls, lksb, resource,
		       (int)strlen(resource));
		printf("INVAL...\n");
		errno = EINVAL;
		return -1;
	}

        ret = dlm_ls_lock(ls, mode, lksb, options, resource,
                          strlen(resource), 0, ast_function, lksb,
                          NULL, NULL);

        if (ret < 0) {
                if (errno == ENOENT)
                        assert(0);

                return -1;
        }

        if ((ret = (wait_for_dlm_event(ls) < 0))) {
                fprintf(stderr, "wait_for_dlm_event: %d / %d\n",
                        ret, errno);
                return -1;
        }

	if (lksb->sb_status == 0)
		return 0;

	errno = lksb->sb_status;
	return -1;
}



dlm_lshandle_t
clu_open_lockspace(const char *lsname)
{
        dlm_lshandle_t ls = NULL;

	//printf("opening lockspace %s\n", lsname);

        while (!ls) {
                ls = dlm_open_lockspace(lsname);
                if (ls)
                        break;

		/*
		printf("Failed to open: %s; trying create.\n",
		       strerror(errno));
		 */

                ls = dlm_create_lockspace(lsname, 0644);
                if (ls)
                        break;

                /* Work around race: Someone was closing lockspace as
                   we were trying to open it.  Retry. */
                if (errno == ENOENT)
                        continue;

                fprintf(stderr, "failed acquiring lockspace: %s\n",
                        strerror(errno));

                return NULL;
        }

        return ls;
}


int
clu_ls_unlock(dlm_lshandle_t ls, struct dlm_lksb *lksb)
{
        int ret;

	if (!ls || !lksb) {
		errno = EINVAL;
		return -1;
	}

        ret = dlm_ls_unlock(ls, lksb->sb_lkid, 0, lksb, NULL);

        if (ret != 0)
                return ret;

        /* lksb->sb_status should be EINPROG at this point */

        if (wait_for_dlm_event(ls) < 0) {
                errno = lksb->sb_status;
                return -1;
        }

        return 0;
}


int
clu_close_lockspace(dlm_lshandle_t ls, const char *name)
{
        return dlm_release_lockspace(name, ls, 1);
}


int
clu_lock(int mode,
	 struct dlm_lksb *lksb,
	 int options,
         char *resource)
{
	int ret = 0, block = 0, conv = 0, err;

	block = !(options & LKF_NOQUEUE);

	errno = EINVAL;
	if (!lksb)
		return -1;

	memset(lksb, 0, sizeof(struct dlm_lksb));

	/*
	   Try to use a conversion lock mechanism when possible
	   If the caller calls explicitly with a NULL lock, then
	   assume the caller knows what it is doing.

	   Only take the NULL lock if:
	   (a) the user isn't specifying CONVERT; if they are, they
	       know what they are doing.

	   ...and one of...

	   (b) This is a blocking call, or
	   (c) The user requested a NULL lock explicitly.  In this case,
	       short-out early; there's no reason to convert a NULL lock
	       to a NULL lock.
	 */
	if (!(options & LKF_CONVERT) &&
	    (block || (mode == LKM_NLMODE))) {
		/* Acquire NULL lock */
		pthread_mutex_lock(&_default_lock);
		ret = clu_ls_lock(_default_ls, LKM_NLMODE, lksb,
				  (options & ~LKF_NOQUEUE),
				  resource);
		err = errno;
		pthread_mutex_unlock(&_default_lock);
		if (ret == 0) {
			if (mode == LKM_NLMODE) {
				/* User only wanted a NULL lock... */
				return 0;
			}
			/*
			   Ok, NULL lock was taken, rest of blocking
			   call should be done using lock conversions.
			 */
			options |= LKF_CONVERT;
			conv = 1;
		} else {
			switch(err) {
			case EINVAL:
				/* Oops, null locks don't work on this
				   plugin; use normal spam mode */
				break;
			default:
				errno = err;
				return -1;
			}
		}
	}

	while (1) {
		pthread_mutex_lock(&_default_lock);
		ret = clu_ls_lock(_default_ls, mode, lksb,
				  (options | LKF_NOQUEUE),
				  resource);
		err = errno;
		pthread_mutex_unlock(&_default_lock);

		if ((ret != 0) && (err == EAGAIN) && block) {
			usleep(random()&32767);
			continue;
		}

		break;
	}

	if (ret != 0 && conv) {
		/* If we get some other error, release the NL lock we
		 took so we don't leak locks*/
		pthread_mutex_lock(&_default_lock);
		clu_ls_unlock(_default_ls, lksb);
		pthread_mutex_unlock(&_default_lock);
		errno = err;
	}

	return ret;
}


int
clu_unlock(struct dlm_lksb *lksb)
{
	int ret, err;
	pthread_mutex_lock(&_default_lock);
	ret = clu_ls_unlock(_default_ls, lksb);
	err = errno;
	pthread_mutex_unlock(&_default_lock);

	usleep(random()&32767);
	errno = err;
	return ret;
}


int
clu_lock_init(const char *dflt_lsname)
{
	int ret, err;

	pthread_mutex_lock(&_default_lock);
	if (_default_ls) {
		pthread_mutex_unlock(&_default_lock);
		return 0;
	}

	if (!dflt_lsname || !strlen(dflt_lsname)) {
		pthread_mutex_unlock(&_default_lock);
		errno = EINVAL;
		return -1;
	}

	_default_ls = clu_open_lockspace(dflt_lsname);
	ret = (_default_ls == NULL);
	err = errno;
	pthread_mutex_unlock(&_default_lock);

	errno = err;
	return ret;
}

void
clu_lock_finished(const char *name)
{
	pthread_mutex_lock(&_default_lock);
	if (_default_ls)
		clu_close_lockspace(_default_ls, name);
	pthread_mutex_unlock(&_default_lock);
}
