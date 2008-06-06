#ifdef WRAP_THREADS
#include <stdio.h>
#include <sys/types.h>
#include <gettid.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <pthread.h>
#include <list.h>
#include <execinfo.h>

typedef struct _thr {
	list_head();
	void *(*fn)(void *arg);
	char **name;
	pthread_t th;
} mthread_t;

static mthread_t *_tlist = NULL;
static int _tcount = 0;
static pthread_rwlock_t _tlock = PTHREAD_RWLOCK_INITIALIZER;

void
dump_thread_states(FILE *fp)
{
	int x;
	mthread_t *curr;
	fprintf(fp, "Thread Information\n");
	pthread_rwlock_rdlock(&_tlock);
	list_for(&_tlist, curr, x) {
		fprintf(fp, "  Thread #%d   id: %d   function: %s\n",
			x, (unsigned)curr->th, curr->name[0]);
	}
	pthread_rwlock_unlock(&_tlock);
	fprintf(fp, "\n\n");
}


int __real_pthread_create(pthread_t *, const pthread_attr_t *,
			  void *(*)(void*), void *);
int
__wrap_pthread_create(pthread_t *th, const pthread_attr_t *attr,
	 	      void *(*start_routine)(void*),
	 	      void *arg)
{
	void *fn = start_routine;
	mthread_t *new;
	int ret;

	new = malloc(sizeof (*new));

	ret = __real_pthread_create(th, attr, start_routine, arg);
	if (ret) {
		if (new)
			free(new);
		return ret;
	}

	if (new) {
		new->th = *th;
		new->fn = start_routine;
		new->name = backtrace_symbols(&new->fn, 1);
		pthread_rwlock_wrlock(&_tlock);
		list_insert(&_tlist, new);
		++_tcount;
		pthread_rwlock_unlock(&_tlock);
	}

	return ret;
}


void __real_pthread_exit(void *);
void
__wrap_pthread_exit(void *exitval)
{
	mthread_t *old;
	int ret = 0, found = 0;
	pthread_t me = pthread_self();

	pthread_rwlock_rdlock(&_tlock);
	list_for(&_tlist, old, ret) {
		if (old->th == me) {
			found = 1;
			break;
		}
	}
	if (!found)
		old = NULL;
	pthread_rwlock_unlock(&_tlock);

	if (!old)
		__real_pthread_exit(exitval);

	pthread_rwlock_wrlock(&_tlock);
	list_remove(&_tlist, old);
	--_tcount;
	pthread_rwlock_unlock(&_tlock);

	if (old->name)
		free(old->name);
	free(old);
	__real_pthread_exit(exitval);
}
#endif
