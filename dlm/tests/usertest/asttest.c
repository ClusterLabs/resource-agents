/* Test program for userland DLM interface w/ AST */
/* NOTE: This is not much of a program and it fails in all
   sorts of ways. But it /does/ illustrate the full dlm_lock
   call and ASTs. It doesn /not/ show how you should use the
   FD parts fo the API!
*/

#ifdef _REENTRANT
#include <pthread.h>
#endif
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

static struct dlm_lksb lksb;
static int use_threads = 0;
static int quiet = 0;

/* Used by the pthread test */
static pthread_cond_t  cond;
static pthread_mutex_t mutex;

/* Used by the poll() test */
static int ast_called = 0;

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
    fprintf(file, "   -V         Show version of dlmtest\n");
    fprintf(file, "   -h         Show this help information\n");
    fprintf(file, "   -m <mode>  lock mode (default EX)\n");
    fprintf(file, "   -c <mode>  mode to convert to (default none)\n");
    fprintf(file, "   -n         don't block\n");
    fprintf(file, "   -p         Use pthreads\n");
    fprintf(file, "   -q         Quiet\n");
    fprintf(file, "   -u         Don't unlock explicitly\n");
    fprintf(file, "\n");

}

static void ast_routine(void *arg)
{
    struct dlm_lksb *lksb = arg;

    if (!quiet)
	printf("ast called, status = %d, lkid=%x\n", lksb->sb_status, lksb->sb_lkid);

    /* Wake the main thread */
    if (use_threads)
    {
	pthread_mutex_lock(&mutex);
	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&mutex);
    }
    else
    {
	ast_called = 1;
    }
}

static void bast_routine(void *arg)
{
    struct dlm_lksb *lksb = arg;

    if (!quiet)
	printf("\nblocking ast called, status = %d, lkid=%x\n", lksb->sb_status, lksb->sb_lkid);
}

/* Using poll(2) to wait for and dispatch ASTs */
static int poll_for_ast()
{
    struct pollfd pfd;

    pfd.fd = dlm_get_fd();
    pfd.events = POLLIN;
    while (!ast_called)
    {
	if (poll(&pfd, 1, 0) < 0)
	{
	    perror("poll");
	    return -1;
	}
	dlm_dispatch(pfd.fd);
    }
    ast_called = 0;
    return 0;
}

int main(int argc, char *argv[])
{
    char *resource = "LOCK-NAME";
    int  flags = 0;
    int  status;
    int  mode = LKM_EXMODE;
    int  convmode = -1;
    int  do_unlock = 1;
    signed char opt;

    /* Deal with command-line arguments */
    opterr = 0;
    optind = 0;
    while ((opt=getopt(argc,argv,"?m:nqupc:vV")) != EOF)
    {
	switch(opt)
	{
	case 'h':
	    usage(argv[0], stdout);
	    exit(0);

	case '?':
	    usage(argv[0], stderr);
	    exit(0);

	case 'm':
	    mode = modetonum(optarg);
	    break;

	case 'c':
	    convmode = modetonum(optarg);
	    break;

        case 'p':
            use_threads++;
            break;

	case 'n':
	    flags |= LKF_NOQUEUE;
	    break;

        case 'q':
            quiet = 1;
            break;

        case 'u':
            do_unlock = 0;
            break;


	case 'V':
	    printf("\nasttest version 0.1\n\n");
	    exit(1);
	    break;
	}
    }

    if (argv[optind])
	resource = argv[optind];

    if (!quiet)
    fprintf(stderr, "locking %s %s %s...", resource,
	    numtomode(mode),
	    (flags&LKF_NOQUEUE?"(NOQUEUE)":""));

    fflush(stderr);

    if (use_threads)
    {
	pthread_cond_init(&cond, NULL);
	pthread_mutex_init(&mutex, NULL);
	pthread_mutex_lock(&mutex);

	dlm_pthread_init();
    }

    status = dlm_lock(mode,
		      &lksb,
		      flags,
		      resource,
		      strlen(resource),
		      0, // Parent,
		      ast_routine,
		      &lksb,
		      bast_routine,
		      NULL); // Range
    if (status == -1)
    {
	if (!quiet) fprintf(stderr, "\n");
	perror("lock");

	return -1;
    }
    printf("(lkid=%x)", lksb.sb_lkid);

    /* Wait */
    if (use_threads)
	pthread_cond_wait(&cond, &mutex);
    else
	poll_for_ast();

    if (!quiet)
    {
        fprintf(stderr, "unlocking %s...", resource);
        fflush(stderr);
    }

    if (do_unlock)
    {
	status = dlm_unlock(lksb.sb_lkid,
			    0, // flags
			    &lksb,
			    &lksb); // AST args
	if (status == -1)
	{
	    if (!quiet) fprintf(stderr, "\n");
	    perror("unlock");
	    return -1;
	}

	/* Wait */
	if (use_threads)
	    pthread_cond_wait(&cond, &mutex);
	else
	    poll_for_ast();
    }

    return 0;
}

