/* Test program for userland DLM interface */

#include <sys/types.h>
#include <sys/uio.h>
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

static void usage(char *prog, FILE *file)
{
    fprintf(file, "Usage:\n");
    fprintf(file, "%s [mndvV] <parent>\n", prog);
    fprintf(file, "\n");
    fprintf(file, "   -V         Show version of %s\n", prog);
    fprintf(file, "   -h         Show this help information\n");
    fprintf(file, "   -v         Do it verbosely\n");
    fprintf(file, "   -m <mode>  lock mode (default CR)\n");
    fprintf(file, "   -p         Pause at end\n");
    fprintf(file, "   -d <num>   Depth of lock tree (default 4)\n");
    fprintf(file, "   -n <num>   Number of children (default 4)\n");
    fprintf(file, "   -s <name>  Subresource name (eg SUBRES-%%d)\n");
    fprintf(file, "\n");

}

int  maxdepth = 4;
int  nchildren = 4;
int  verbose = 0;
char *subresformat="SUBRES-%d-%d";


int do_childlocks(int parent, int mode, int depth, int flags)
{
    int status;
    int i;
    char subresname[64];
    struct dlm_lksb lksb;

    if (depth > maxdepth) return 0;

    memset(&lksb, 0, sizeof(lksb));

    for (i = 0; i < nchildren; i++)
    {
	sprintf(subresname, subresformat, depth, i);

	if (verbose)
	    printf("locking '%s', depth %d\n", subresname, depth);

	status = dlm_lock_wait(mode, &lksb, flags, subresname, strlen(subresname)+1,
			       parent, NULL, NULL, NULL);
	if (status || !lksb.sb_lkid)
	{
	    perror("lock failed");
	    return 0;
	}
	do_childlocks(lksb.sb_lkid, mode, depth+1, flags);
    }
    return 0;
}


int main(int argc, char *argv[])
{
    char *resource = "LOCK-NAME";
    int  flags = 0;
    int  status;
    int  mode = LKM_CRMODE;
    int  pause_at_end=0;
    struct dlm_lksb lksb;
    signed char opt;

    /* Deal with command-line arguments */
    opterr = 0;
    optind = 0;
    while ((opt=getopt(argc,argv,"?m:n:d:s:pvV")) != EOF)
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

	case 'v':
	    verbose++;
	    break;

	case 'p':
	    pause_at_end++;
	    break;

	case 'd':
	    maxdepth = atoi(optarg);
	    break;

	case 'n':
	    nchildren = atoi(optarg);
	    break;

	case 's':
	    strcpy(subresformat, optarg);
	    break;

	case 'V':
	    printf("\n%s version 0.1\n\n", argv[0]);
	    exit(1);
	    break;
	}
    }

    if (argv[optind])
	resource = argv[optind];

    /* Lock parent */
    if (verbose)
	fprintf(stderr, "locking parent '%s'\n", resource);

    fflush(stderr);

    status = dlm_lock_wait(mode, &lksb, flags,
			   resource, strlen(resource)+1,
			   0, NULL, NULL, NULL);
    if (status == -1)
    {
	perror("lock");
	return -1;
    }

    if (lksb.sb_lkid == 0)
    {
	fprintf(stderr, "error: got lockid of zero\n");
	return 0;
    }

    do_childlocks(lksb.sb_lkid, mode, 0, flags);

    if (pause_at_end)
    {
	printf("paused\n");
	pause();
    }
    return 0;
}
