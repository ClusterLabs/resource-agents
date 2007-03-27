/* Flood the DLM !
   but not too much...
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

static pthread_cond_t  cond;
static pthread_mutex_t mutex;

static int count = 0;

static void usage(char *prog, FILE *file)
{
    fprintf(file, "Usage:\n");
    fprintf(file, "%s [mcnpquhV] <lockname>\n", prog);
    fprintf(file, "\n");
    fprintf(file, "   -V         Show version of %s\n", prog);
    fprintf(file, "   -h         Show this help information\n");
    fprintf(file, "   -m <num>   Maximum number of locks to hold (default MAX_INT)\n");
    fprintf(file, "   -i <num>   Show progress in <n> increments (default 1000)\n");
    fprintf(file, "   -n <num>   Number of resources (default 10)\n");
    fprintf(file, "   -q         Quit\n");


    fprintf(file, "\n");

}

static void ast_routine(void *arg)
{
    struct dlm_lksb *lksb = arg;

    if (lksb->sb_status == 0) {
	dlm_unlock(lksb->sb_lkid, 0, lksb, lksb);
	return;
    }

    if (lksb->sb_status == EUNLOCK) {
	    count--;
    }
}

int main(int argc, char *argv[])
{
    int  flags = 0;
    int  lockops = 0;
    int  maxlocks = INT_MAX;
    int  rescount = 10;
    int  increment = 1000;
    int  quiet = 0;
    int  status;
    int  i;
    int  mode = LKM_CRMODE;
    int  lksbnum = 0;
    signed char opt;
    char **resources;
    struct dlm_lksb *lksbs;

    /* Deal with command-line arguments */
    opterr = 0;
    optind = 0;
    while ((opt=getopt(argc,argv,"?m:i:qn:vV")) != EOF)
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
	    maxlocks = atoi(optarg);
	    break;

	case 'i':
	    increment = atoi(optarg);
	    break;

	case 'n':
	    rescount = atoi(optarg);
	    break;

	case 'q':
	    quiet = 1;
	    break;

	case 'V':
	    printf("\nflood version 0.3\n\n");
	    exit(1);
	    break;
	}
    }

    resources = malloc(sizeof(char*) * rescount);
    for (i=0; i < rescount; i++) {
	    char resname[256];
	    sprintf(resname, "TESTLOCK%d", i);
	    resources[i] = strdup(resname);
    }

    lksbs = malloc(sizeof(struct dlm_lksb) * maxlocks);
    if (!lksbs)
    {
	    perror("cannot allocate lksbs");
	    return 1;
    }

    pthread_cond_init(&cond, NULL);
    pthread_mutex_init(&mutex, NULL);
    pthread_mutex_lock(&mutex);

    dlm_pthread_init();

    while (1) {
	    char *resource = resources[rand() % rescount];

	    status = dlm_lock(mode,
			      &lksbs[lksbnum],
			      flags,
			      resource,
			      strlen(resource),
			      0, // Parent,
			      ast_routine,
			      &lksbs[lksbnum],
			      NULL, // bast_routine,
			      NULL); // Range
	    if (status == -1)
	    {
		    perror("lock failed");
		    return -1;
	    }

	    count++;
	    lockops++;
	    if ((lockops % increment) == 0 && !quiet)
		    fprintf(stderr, "%d lockops, %d locks\n", lockops, count);

	    while (count > maxlocks) {
		    sleep(1);
	    }
	    lksbnum = (lksbnum+1)%maxlocks;
    }

    return 0;
}

