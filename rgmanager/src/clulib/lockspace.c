#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <lock.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <clulog.h>
#include <signal.h>
#include <gettid.h>
#include <libdlm.h>
#include <errno.h>


#define RGMGR_LOCKSPACE "rgmanager"

static pthread_mutex_t _default_lock = PTHREAD_MUTEX_INITIALIZER;
static char _init = 0;
static pid_t _holder_id = 0;
static dlm_lshandle_t _default_ls;


static int
_init_lockspace(void)
{
	_default_ls = clu_acquire_lockspace(RGMGR_LOCKSPACE);
	if (!_default_ls) {
		return -1;
	}
	_init = 1;
	return 0;
}


dlm_lshandle_t 
ls_hold_default(void)
{
	pthread_mutex_lock(&_default_lock);
	if (!_init && (_init_lockspace() < 0)) {
		pthread_mutex_unlock(&_default_lock);
		errno = ENOLCK;
		return NULL;
	}

	if (_holder_id != 0) {
		pthread_mutex_unlock(&_default_lock);
		errno = EAGAIN;
		return NULL;
	}

	_holder_id = gettid();
	pthread_mutex_unlock(&_default_lock);
	return _default_ls;
}


void
ls_release_default(void)
{
	pthread_mutex_lock(&_default_lock);
	if (_holder_id != gettid()) {
		clulog(LOG_ERR, "Attempt to release lockspace when I am not"
		       "the holder!\n");
		raise(SIGSTOP);
	}

	_holder_id = 0;
	pthread_mutex_unlock(&_default_lock);
}


