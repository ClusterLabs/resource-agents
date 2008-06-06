/*
 * Threaded DLM example
 */

#include <pthread.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/poll.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <fcntl.h>
#include <netdb.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>

#include "libdlm.h"

struct lock_wait {
	pthread_cond_t cond;
	pthread_mutex_t mutex;
	struct dlm_lksb lksb;
};

struct thread_args
{
	dlm_lshandle_t *lockspace;
	char *resource;
	int flags;
	int delay;
	int mode;
	int verbose;
	int num;
};

static int modetonum(char *modestr)
{
    int mode = LKM_EXMODE;

    if (strncasecmp(modestr, "NL", 2) == 0) mode = LKM_NLMODE;
    if (strncasecmp(modestr, "CR", 2) == 0) mode = LKM_CRMODE;
    if (strncasecmp(modestr, "CW", 2) == 0) mode = LKM_CWMODE;
    if (strncasecmp(modestr, "PR", 2) == 0) mode = LKM_PRMODE;
    if (strncasecmp(modestr, "PW", 2) == 0) mode = LKM_PWMODE;
    if (strncasecmp(modestr, "EX", 2) == 0) mode = LKM_EXMODE;

    return mode;
}

static char *numtomode(int mode)
{
    switch (mode)
    {
    case LKM_NLMODE: return "NL";
    case LKM_CRMODE: return "CR";
    case LKM_CWMODE: return "CW";
    case LKM_PRMODE: return "PR";
    case LKM_PWMODE: return "PW";
    case LKM_EXMODE: return "EX";
    default: return "??";
    }
}

static void usage(char *prog, FILE *file)
{
    fprintf(file, "Usage:\n");
    fprintf(file, "%s [mcnpquhV] <lockname>\n", prog);
    fprintf(file, "\n");
    fprintf(file, "   -V           show version of %s\n", prog);
    fprintf(file, "   -h           show this help information\n");
    fprintf(file, "   -m <mode>    lock mode (default EX)\n");
    fprintf(file, "   -n           don't block\n");
    fprintf(file, "   -t <threads> number of threads\n");
    fprintf(file, "   -d <secs>    delay while holding lock\n");
    fprintf(file, "   -l <name>    lockspace name\n");
    fprintf(file, "   -v           verbose (up to 3)\n");
    fprintf(file, "\n");

}

/* Simply wakes the thread that initiated the lock */
static void sync_ast_routine(void *arg)
{
	struct lock_wait *lwait = arg;

	pthread_mutex_lock(&lwait->mutex);
	pthread_cond_signal(&lwait->cond);
	pthread_mutex_unlock(&lwait->mutex);
}

/* Get/Convert a lock and wait for it to complete (or fail) */
static int sync_lock(dlm_lshandle_t lockspace,
		     const char *resource,
		     int mode,
		     int flags,
		     int *lockid)
{
	int status;
	struct lock_wait lwait;

	if (!lockid) {
		errno = EINVAL;
		return -1;
	}

	/* Conversions need the lockid in the LKSB */
	if (flags & LKF_CONVERT)
		lwait.lksb.sb_lkid = *lockid;

	pthread_cond_init(&lwait.cond, NULL);
	pthread_mutex_init(&lwait.mutex, NULL);
	pthread_mutex_lock(&lwait.mutex);

	status = dlm_ls_lock(lockspace,
			     mode,
			     &lwait.lksb,
			     flags,
			     resource,
			     strlen(resource),
			     0, sync_ast_routine, &lwait, NULL, NULL);
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

/* Unlock - and wait for it to complete */
static int sync_unlock(dlm_lshandle_t lockspace, int lockid)
{
	int status;
	struct lock_wait lwait;

	pthread_cond_init(&lwait.cond, NULL);
	pthread_mutex_init(&lwait.mutex, NULL);
	pthread_mutex_lock(&lwait.mutex);

	status = dlm_ls_unlock(lockspace, lockid, 0, &lwait.lksb, &lwait);

	if (status)
		return status;

	/* Wait for it to complete */
	pthread_cond_wait(&lwait.cond, &lwait.mutex);
	pthread_mutex_unlock(&lwait.mutex);

	errno = lwait.lksb.sb_status;
	if (lwait.lksb.sb_status != EUNLOCK)
		return -1;
	else
		return 0;

}

static void *thread_fn(void *arg)
{
	struct thread_args *ta = arg;
	int lockid;
	int status;

	if (ta->verbose > 1)
		fprintf(stderr, "Locking in thread %d\n", pthread_self);

	status = sync_lock(ta->lockspace, ta->resource,
			   ta->mode, ta->flags, &lockid);

	if (status)
	{
		if (ta->verbose)
			fprintf(stderr, "Lock in thread %x failed: %s\n", pthread_self, strerror(errno));
		return NULL;
	}

	sleep(ta->delay);

	if (ta->verbose > 1)
		fprintf(stderr, "Unlocking in thread %x\n", pthread_self());

	status = sync_unlock(ta->lockspace,lockid);
	if (status)
	{
		if (ta->verbose)
			fprintf(stderr, "Unlock in thread %x failed: %s\n", pthread_self(), strerror(errno));
	}

	return NULL;
}


int main(int argc, char *argv[])
{
    char *resource = "LOCK-NAME";
    char *lockspace_name = "threadtest";
    dlm_lshandle_t *lockspace;
    int  flags = 0;
    int  num_threads = 5;
    int  delay = 1;
    int  mode;
    int  verbose;
    int  i;
    signed char opt;
    pthread_t *threads;
    struct thread_args ta;

    /* Deal with command-line arguments */
    opterr = 0;
    optind = 0;
    while ((opt=getopt(argc,argv,"h?nt:d:l:m:vV")) != EOF)
    {
	switch(opt)
	{
	case 'h':
	    usage(argv[0], stdout);
	    exit(0);

	case '?':
	    usage(argv[0], stderr);
	    exit(0);

	case 'n':
	    flags |= LKF_NOQUEUE;
	    break;

	case 't':
            num_threads = atoi(optarg);
	    break;

	case 'm':
            mode = modetonum(optarg);
	    break;

	case 'l':
	    lockspace_name = strdup(optarg);
	    break;

	case 'd':
            delay = atoi(optarg);
	    break;

	case 'v':
	    verbose++;
	    break;

	case 'V':
	    printf("\nthread example version 0.1\n\n");
	    exit(1);
	    break;
	}
    }

    if (argv[optind])
	resource = argv[optind];

    if (verbose)
	    fprintf(stderr, "Creating lockspace '%s'...", lockspace_name);

    /* You need to be root to create the lockspace ... but not to use it */
    lockspace = dlm_create_lockspace(lockspace_name, 0666);
    if (!lockspace) {
	    perror("Unable to create lockspace");
	    return 1;
    }
    if (verbose)
	    fprintf(stderr, "done\n");

    dlm_ls_pthread_init(lockspace);

    threads = malloc(sizeof(pthread_t) * num_threads);
    if (!threads)
    {
	    perror("can't malloc threads array");
	    return 2;
    }

    if (verbose)
	    fprintf(stderr, "Starting threads\n");

    ta.lockspace = lockspace;
    ta.mode = mode;
    ta.flags = flags;
    ta.delay = delay;
    ta.verbose = verbose;
    ta.resource = resource;

    for (i=0; i<num_threads; i++)
    {
	    if (verbose > 2)
		    fprintf(stderr, "Starting thread %d\n", i);

	    pthread_create(&threads[i], NULL, thread_fn, &ta);
    }

    if (verbose)
	    fprintf(stderr, "All threads started\n");

    for (i=0; i<num_threads; i++)
    {
	    void *status;
	    if (verbose > 2)
		    fprintf(stderr, "Waiting for thread %d\n", i);
	    pthread_join(threads[i], &status);
    }

    return 0;
}

