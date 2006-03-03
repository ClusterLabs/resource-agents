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

static struct dlm_lksb lksb;
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
    fprintf(file, "%s [hmcnQpequdV] <lockname>\n", prog);
    fprintf(file, "\n");
    fprintf(file, "   -V         Show version of dlmtest\n");
    fprintf(file, "   -h         Show this help information\n");
    fprintf(file, "   -m <mode>  lock mode (default EX)\n");
    fprintf(file, "   -c <mode>  mode to convert to (default none)\n");
    fprintf(file, "   -n         don't block\n");
    fprintf(file, "   -Q         query the lock\n");
    fprintf(file, "   -p         Persistent lock\n");
    fprintf(file, "   -e         Expedite conversion\n");
    fprintf(file, "   -q         Quiet\n");
    fprintf(file, "   -u         Don't unlock explicitly\n");
    fprintf(file, "   -d <secs>  Time to hold the lock for\n");
    fprintf(file, "\n");

}

#ifdef QUERY
static void query_ast_routine(void *arg)
{
    struct dlm_lksb *lksb = arg;
    struct dlm_queryinfo *qi = (struct dlm_queryinfo *)lksb->sb_lvbptr;
    int i;

    qi->gqi_resinfo->rsi_name[qi->gqi_resinfo->rsi_length] = '\0';
    /* Dump resource info */
    printf("lockinfo: status     = %d\n", lksb->sb_status);
    printf("lockinfo: resource   = '%s'\n", qi->gqi_resinfo->rsi_name);
    printf("lockinfo: grantcount = %d\n", qi->gqi_resinfo->rsi_grantcount);
    printf("lockinfo: convcount  = %d\n", qi->gqi_resinfo->rsi_convcount);
    printf("lockinfo: waitcount  = %d\n", qi->gqi_resinfo->rsi_waitcount);
    printf("lockinfo: masternode = %d\n", qi->gqi_resinfo->rsi_masternode);

    /* Dump all the locks */
    for (i = 0; i < qi->gqi_lockcount; i++)
    {
	struct dlm_lockinfo *li = &qi->gqi_lockinfo[i];

	printf("lockinfo: lock: lkid        = %x\n", li->lki_lkid);
	printf("lockinfo: lock: master lkid = %x\n", li->lki_mstlkid);
	printf("lockinfo: lock: parent lkid = %x\n", li->lki_parent);
	printf("lockinfo: lock: node        = %d\n", li->lki_node);
	printf("lockinfo: lock: pid         = %d\n", li->lki_ownpid);
	printf("lockinfo: lock: state       = %d\n", li->lki_state);
	printf("lockinfo: lock: grmode      = %d\n", li->lki_grmode);
	printf("lockinfo: lock: rqmode      = %d\n", li->lki_rqmode);
	printf("\n");
    }

    if (qi->gqi_lockinfo)
	free(qi->gqi_lockinfo);
}

static struct dlm_queryinfo qinfo;
static struct dlm_resinfo resinfo;
#define MAX_QUERY_LOCKS 10


static int query_lock(int lockid)
{
    int status;
struct dlm_lksb tmplksb;
    lksb.sb_lkid = lockid;
    qinfo.gqi_resinfo = &resinfo;
    qinfo.gqi_lockinfo = malloc(sizeof(struct dlm_lockinfo) * MAX_QUERY_LOCKS);
    qinfo.gqi_locksize = MAX_QUERY_LOCKS;
    lksb.sb_lvbptr = (char *)&qinfo;

    status = dlm_query(&tmplksb,
		       DLM_QUERY_QUEUE_ALL | DLM_QUERY_LOCKS_ALL,
		       &qinfo,
		       query_ast_routine,
		       &tmplksb);
    if (status)
	perror("Query failed");
    else
	sleep(1); /* Just to allow the result to come back. There isn't
		     a synchronous version of this call */
    return status;
}
#endif


int main(int argc, char *argv[])
{
    char *resource = "LOCK-NAME";
    int  flags = 0;
    int  status;
    int  delay = 5;
    int  mode = LKM_EXMODE;
    int  convmode = -1;
    int  lockid;
    int  quiet = 0;
    int  do_unlock = 1;
    int  do_query = 0;
    int  do_expedite = 0;
    signed char opt;
    int i;

    /* Deal with command-line arguments */
    opterr = 0;
    optind = 0;
    while ((opt=getopt(argc,argv,"?m:nquQepd:c:vV")) != EOF)
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

        case 'e':
            do_expedite = 1;
            break;

        case 'p':
            flags |= LKF_PERSISTENT;
            break;

	case 'n':
	    flags |= LKF_NOQUEUE;
	    break;

	case 'd':
	    delay = atoi(optarg);
	    break;

        case 'q':
            quiet = 1;
            break;

        case 'u':
            do_unlock = 0;
            break;

	case 'Q':
	    do_query = 1;
	    break;

	case 'V':
	    printf("\ndlmtest version 0.3\n\n");
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

    status = lock_resource(resource, mode, flags, &lockid);
    if (status == -1)
    {
	if (!quiet) fprintf(stderr, "\n");
	perror("lock");

	return -1;
    }
    if (lockid == 0)
    {
	fprintf(stderr, "error: got lockid of zero\n");
	return 0;
    }

    if (!quiet) fprintf(stderr, "done (lkid = %x)\n", lockid);

    if (!do_unlock) return 0;

#ifdef QUERY
    if (do_query) query_lock(lockid);
#endif

    sleep(delay);

    if (convmode != -1)
    {
        if (do_expedite)
		flags |= LKF_EXPEDITE;

	if (!quiet)
	{
	    fprintf(stderr, "converting %s to %s...", resource, numtomode(convmode));
	    fflush(stderr);
	}

	status = lock_resource(resource, convmode, flags | LKF_CONVERT, &lockid);
	if (status == -1)
	{
	    if (!quiet) fprintf(stderr, "\n");
	    perror("convert");
	    return -1;
	}
	if (!quiet) fprintf(stderr, "done\n");
    }

    sleep(delay);

    if (!quiet)
    {
        fprintf(stderr, "unlocking %s...", resource);
        fflush(stderr);
    }

    status = unlock_resource(lockid);
    if (status == -1)
    {
	if (!quiet) fprintf(stderr, "\n");
	perror("unlock");
	return -1;
    }

    if (!quiet) fprintf(stderr, "done\n");

    /* For some reason, calling this IMMEDIATELY before
       exitting, causes a thread hang. either don't call it at
       all or do something in afterwards before calling exit
    */
    dlm_pthread_cleanup();
    return 0;
}

