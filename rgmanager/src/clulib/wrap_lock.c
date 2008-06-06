#ifdef WRAP_LOCKS
#include <stdio.h>
#include <sys/types.h>
#include <gettid.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <string.h>
#include <signal.h>

int __real_pthread_mutex_lock(pthread_mutex_t *lock);
int
__wrap_pthread_mutex_lock(pthread_mutex_t *lock)
{
	int status;
	struct timespec delay;

	while (1) {
		status = __real_pthread_mutex_lock(lock);

		switch(status) {
		case EDEADLK:
			/* Already own it: Note the error, but continue */
			fprintf(stderr, "[%d] %s(%p): %s; continuing\n",
				gettid(),
				__FUNCTION__, lock, strerror(status));
			/* deliberate fallthrough */
		case 0:
			return 0;
		case EBUSY:
			/* Try again */
			break;
		default:
			/* Other return codes */
			fprintf(stderr, "[%d] %s(%p): %s\n", gettid(),
				__FUNCTION__, lock, strerror(status));
			raise(SIGSEGV);
			/* EINVAL? */
			return 0;
		}

		delay.tv_sec = 0;
		delay.tv_nsec = 100000;
		nanosleep(&delay, NULL);
	}

	/* Not reached */
	return 0;
}


int __real_pthread_mutex_unlock(pthread_mutex_t *lock);
int
__wrap_pthread_mutex_unlock(pthread_mutex_t *lock)
{
	int status;
	struct timespec delay;

	while (1) {
		status = __real_pthread_mutex_unlock(lock);

		switch(status) {
		case EPERM:
			/* Don't own it: Note the error, but continue */
			fprintf(stderr, "[%d] %s(%p): %s; continuing\n",
				gettid(),
				__FUNCTION__, lock, strerror(status));
			/* deliberate fallthrough */
		case 0:
			return 0;
		default:
			fprintf(stderr, "[%d] %s(%p): %s\n", gettid(),
				__FUNCTION__, lock, strerror(status));
			raise(SIGSEGV);
			return 0;
		}

		delay.tv_sec = 0;
		delay.tv_nsec = 100000;
		nanosleep(&delay, NULL);
	}

	/* Not reached */
	return 0;
}


int __real_pthread_rwlock_rdlock(pthread_rwlock_t *lock);
int
__wrap_pthread_rwlock_rdlock(pthread_rwlock_t *lock)
{
	int status;
	struct timespec delay;

	while (1) {
		status = __real_pthread_rwlock_rdlock(lock);

		switch(status) {
		case EDEADLK:
			/* Already own it: Note the error, but continue */
			fprintf(stderr, "[%d] %s(%p): %s; continuing\n",
				gettid(),
				__FUNCTION__, lock, strerror(status));
			/* deliberate fallthrough */
		case 0:
			return 0;
		case EBUSY:
			/* Try again */
			break;
		default:
			/* Other return codes */
			fprintf(stderr, "[%d] %s(%p): %s\n", gettid(),
				__FUNCTION__, lock, strerror(status));
			raise(SIGSEGV);
			/* EINVAL? */
			return 0;
		}

		delay.tv_sec = 0;
		delay.tv_nsec = 100000;
		nanosleep(&delay, NULL);
	}

	/* Not reached */
	return 0;
}


int __real_pthread_rwlock_wrlock(pthread_rwlock_t *lock);
int
__wrap_pthread_rwlock_wrlock(pthread_rwlock_t *lock)
{
	int status;
	struct timespec delay;

	while (1) {
		status = __real_pthread_rwlock_wrlock(lock);

		switch(status) {
		case EDEADLK:
			/* Already own it: Note the error, but continue */
			fprintf(stderr, "[%d] %s(%p): %s; continuing\n",
				gettid(),
				__FUNCTION__, lock, strerror(status));
			/* deliberate fallthrough */
		case 0:
			return 0;
		case EBUSY:
			/* Try again */
			break;
		default:
			/* Other return codes */
			fprintf(stderr, "[%d] %s(%p): %s\n", gettid(),
				__FUNCTION__, lock, strerror(status));
			raise(SIGSEGV);
			/* EINVAL? */
			return 0;
		}

		delay.tv_sec = 0;
		delay.tv_nsec = 100000;
		nanosleep(&delay, NULL);
	}

	/* Not reached */
	return 0;
}


int __real_pthread_rwlock_unlock(pthread_rwlock_t *lock);
int
__wrap_pthread_rwlock_unlock(pthread_rwlock_t *lock)
{
	int status;
	struct timespec delay;

	while (1) {
		status = __real_pthread_rwlock_unlock(lock);

		switch(status) {
		case EPERM:
			/* Don't own it: Note the error, but continue */
			fprintf(stderr, "[%d] %s(%p): %s; continuing\n",
				gettid(),
				__FUNCTION__, lock, strerror(status));
			/* deliberate fallthrough */
		case 0:
			return 0;
		default:
			fprintf(stderr, "[%d] %s(%p): %s\n", gettid(),
				__FUNCTION__, lock, strerror(status));
			raise(SIGSEGV);
			return 0;
		}

		delay.tv_sec = 0;
		delay.tv_nsec = 100000;
		nanosleep(&delay, NULL);
	}

	/* Not reached */
	return 0;
}
#endif

