/**
  pthread mutex wrapper for a global CMAN handle
 */
#include <stdio.h>
#include <pthread.h>
#include <libcman.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <fcntl.h>

static cman_handle_t _chandle = NULL;
static pthread_mutex_t _chandle_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t _chandle_cond = PTHREAD_COND_INITIALIZER;
static pthread_t _chandle_holder = 0;
static int _chandle_preempt = 0;
static int _wakeup_pipe[2] = { -1, -1 };

static void
_set_nonblock(int fd)
{
	int flags;

	flags = fcntl(fd, F_GETFL, 0);
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
		perror("fcntl");
}


/**
  Lock / return the global CMAN handle.

  @param block		If nonzero, we wait until the handle is released
  @param preempt	If nonzero, *try* to wake up the holder who has
			taken the lock with cman_lock_preemptible.  Will not
			wake up holders which took it with cman_lock().
  @return		NULL / errno on failure; the global CMAN handle
			on success.
 */
cman_handle_t 
cman_lock(int block, int preempt)
{
	int err;
	pthread_t tid;
	cman_handle_t *ret = NULL;

	pthread_mutex_lock(&_chandle_lock);
	if (_chandle == NULL) {
		errno = ENOSYS;
		goto out_unlock;
	}

	tid = pthread_self();
	if (_chandle_holder == tid) {
		errno = EDEADLK;
		goto out_unlock;
	}

	if (_chandle_holder > 0) {
		if (!block) {
			errno = EAGAIN;
			goto out_unlock;
		}

		/* Try to wake up the holder! */
		if (preempt)
			err = write(_wakeup_pipe[1], "!", 1); /** XXX we don't care about errors here **/

		/* Blocking call; do the cond-thing */
		pthread_cond_wait(&_chandle_cond, &_chandle_lock);
	}
		
	_chandle_holder = tid;
	ret = _chandle;
out_unlock:
	pthread_mutex_unlock(&_chandle_lock);
	return ret;
}


/**
  Lock / return the global CMAN handle.

  @param block		If nonzero, we wait until the handle is released
  @param preempt_fd	Caller should include this file descriptor in
			blocking calls to select(2), so that we can wake
			it up if someone calls with cman_lock(xxx, 1);
  @return		NULL / errno on failure; the global CMAN handle
			on success.
 */
cman_handle_t
cman_lock_preemptible(int block, int *preempt_fd)
{
	pthread_t tid;
	cman_handle_t *ret = NULL;

	if (preempt_fd == NULL) {
		errno = EINVAL;
		return NULL;
	}

	pthread_mutex_lock(&_chandle_lock);
	if (_chandle == NULL) {
		errno = ENOSYS;
		goto out_unlock;
	}

	tid = pthread_self();
	if (_chandle_holder == tid) {
		errno = EDEADLK;
		goto out_unlock;
	}

	if (_chandle_holder > 0) {
		if (!block) {
			errno = EAGAIN;
			goto out_unlock;
		}

		/* Blocking call; do the cond-thing */
		pthread_cond_wait(&_chandle_cond, &_chandle_lock);
	}

	*preempt_fd = _wakeup_pipe[0];
	_chandle_holder = tid;
	_chandle_preempt = 1;
	ret = _chandle;
out_unlock:
	pthread_mutex_unlock(&_chandle_lock);
	return ret;
}


/**
  Release the global CMAN handle

  @param ch		Should match the global handle
  @return		-1 on failure, 0 on success
 */
int
cman_unlock(cman_handle_t ch)
{
	int err;
	int ret = -1;
	char c;

	pthread_mutex_lock(&_chandle_lock);
	if (_chandle == NULL) {
		errno = ENOSYS;
		goto out_unlock;
	}

	if (_chandle_holder != pthread_self() || !_chandle_holder) {
		errno = EBUSY;
		goto out_unlock;
	}

	if (_chandle != ch) {
		errno = EINVAL;
		goto out_unlock;
	}

	/* Empty wakeup pipe if we took it with the preempt flag */
	if (_chandle_preempt)
		err = read(_wakeup_pipe[0], &c, 1); /** XXX we don't care about errors here **/

	_chandle_preempt = 0;
	_chandle_holder = 0;
	ret = 0;

out_unlock:
	pthread_mutex_unlock(&_chandle_lock);
	if (ret == 0) 
		pthread_cond_broadcast(&_chandle_cond);
	return ret;
}


int
cman_init_subsys(cman_handle_t *ch)
{
	int ret = -1;

	pthread_mutex_lock(&_chandle_lock);
	if (_chandle) {
		errno = EAGAIN;
		goto out_unlock;
	}

	if (!ch) {
		errno = EAGAIN;
		goto out_unlock;
	}

	if (pipe(_wakeup_pipe) < 0) {
		goto out_unlock;
	}

	_set_nonblock(_wakeup_pipe[0]);
	_chandle = ch;
	_chandle_holder = 0;
	ret = 0;

out_unlock:
	pthread_mutex_unlock(&_chandle_lock);
	return ret;
}


int
cman_cleanup_subsys(void)
{
	int ret = -1;

	pthread_mutex_lock(&_chandle_lock);
	if (!_chandle) {
		errno = EAGAIN;
		goto out_unlock;
	}

	if (_chandle_holder > 0) {
		pthread_cond_wait(&_chandle_cond, &_chandle_lock);
	}

	ret = 0;
	_chandle = NULL;
	_chandle_holder = 0;

	close(_wakeup_pipe[0]);
	close(_wakeup_pipe[1]);
	
out_unlock:
	pthread_mutex_unlock(&_chandle_lock);
	return ret;
}


int
cman_send_data_unlocked(void *buf, int len, int flags,
			uint8_t port, int nodeid)
{
	return cman_send_data(_chandle, buf, len, flags, port, nodeid);
}
