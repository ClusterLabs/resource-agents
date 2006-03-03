
/* Ping Test the locking interface */

#ifdef __linux__
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <netdb.h>

#include <libdlm.h>

static char our_lvb[DLM_LVB_LEN];
static struct dlm_lksb our_lksb = {.sb_lvbptr = our_lvb};
static int *lvb_int = (int *)our_lvb;
#define SUCCESS 0
#endif


#ifdef VMS
#include starlet
#include psldef
#include ssdef
#include errno
#include descrip
#include rsdmdef
#include lckdef

#include stdio
#include stdlib
#include string

struct lksb
{
    int sb_status;
    int sb_lkid;
    char sb_lvb[16];
};

static struct lksb our_lksb;
static int *lvb_int = (int *)&our_lksb.sb_lvb;

#define LKM_NLMODE LCK$K_NLMODE
#define LKM_CRMODE LCK$K_CRMODE
#define LKM_CWMODE LCK$K_CWMODE
#define LKM_PRMODE LCK$K_PRMODE
#define LKM_PWMODE LCK$K_PWMODE
#define LKM_EXMODE LCK$K_EXMODE
#define EDEADLOCK SS$_DEADLOCK

/* This is wrong...VMS does not deliver ASTs
   for $DEQ but we'll never get this code, honest.
 */
#define EUNLOCK 65535

#define SUCCESS SS$_NORMAL
#endif

static char *lockname="ping";
static int us = 1;
static int maxnode = 2;
static int cur_mode;

static int granted = 0;
static void compast_routine(void *arg);
static void blockast_routine(void *arg);

#ifdef __linux__
static int convert_lock(int mode)
{
    int old_mode = cur_mode;
    int status;

    printf("pinglock: convert to %d starting\n", mode);
    status = dlm_lock( mode,
		       &our_lksb,
		       LKF_VALBLK | LKF_CONVERT,
		       NULL,
		       0,
		       0,
		       compast_routine,
		       &our_lksb,
		       blockast_routine,
		       NULL);
    if (status != 0)
    {
	perror("pinglock: convert failed");
    }
    else
    {
	cur_mode = mode;
	printf("pinglock: convert to %d started\n", mode);
    }
    return status;
}

static void unlock()
{
    int status;
    status = dlm_unlock( our_lksb.sb_lkid,
			 0,
			 &our_lksb,
			 0);
    if (status != 0)
	perror("pinglock: unlock failed");

}

static void start_lock()
{
    int status;
    cur_mode = LKM_EXMODE;

    *lvb_int = us-1;

    printf("pinglock: starting\n");
    status = dlm_lock( cur_mode,
		       &our_lksb,
		       LKF_VALBLK,
		       lockname,
		       strlen(lockname)+1, /* include trailing NUL for ease of following */
		       0, /* Parent */
		       compast_routine,
		       &our_lksb,
		       blockast_routine,
		       NULL);
    if (status != 0)
	perror("pinglock: lock failed");
}

#endif /* __linux__ */

#ifdef VMS

static void start_lock()
{
    struct dsc$descriptor_s name_s;
    int status;
    char *name = "PINGLOCK";

    cur_mode = LCK$K_EXMODE;

    *lvb_int = us-1;

/* Make a descriptor of the name */
    memset(&name_s, 0, sizeof(struct dsc$descriptor_s));
    name_s.dsc$w_length = strlen(name);
    name_s.dsc$a_pointer = name;

    /* Lock it */
    status = sys$enqw(0,
		      cur_mode,
		      &our_lksb,
		      0, /* flags */
		      &name_s,
		      0, /* parent */
		      compast_routine,
		      0, /* astp */
		      blockast_routine,
		      PSL$C_USER,
		      RSDM$K_PROCESS_RSDM_ID,
		      0);

    if (status != SS$_NORMAL)
    {
        printf("lock enq failed : %s\n", strerror(EVMSERR, status));
	sys$exit(status);
    }

    printf("Lock ID is %x\n", our_lksb.sb_lkid);
    return;
}

static int convert_lock(int mode)
{
    int status;
    int old_mode = cur_mode;

    printf("converting to %d\n", mode);

    /* Lock it */
    status = sys$enq(0,
		     mode,
		     &our_lksb,
		     LCK$M_CONVERT | LCK$M_VALBLK,
		     NULL,
		     0, /* parent */
		     compast_routine,
		     0, /* astp */
		     blockast_routine,
		     PSL$C_USER,
		     RSDM$K_PROCESS_RSDM_ID,
		     0);

    if (status != SS$_NORMAL)
    {
        printf("convert enq failed : %s\n", strerror(EVMSERR, status));
	sys$wake();
    }
    else
    {
	cur_mode = mode;
	status = 0; /* simulate Unix retcodes */
    }
    return status;
}

static void unlock()
{
    int status;

    status = sys$deq(our_lksb.sb_lkid, NULL,0,0,0);
    if (status != SS$_NORMAL)
    {
        printf("denq failed : %s\n", strerror(EVMSERR, status));
	sys$wake();
    }
}

#endif

static void compast_routine(void *arg)
{

#ifdef __linux__
    if (our_lksb.sb_flags & DLM_SBF_VALNOTVALID)
#endif
#ifdef VMS
    if (our_lksb.sb_status == SS$_VALNOTVALID)
#endif
    {
	    printf(" valblk not valid. current value is %d\n", *lvb_int);
	    unlock();
	    exit(10);
    }

    if (our_lksb.sb_status == EDEADLOCK)
    {
	printf("pinglock: deadlocked\n");
	unlock();
	exit(11);
    }

    if (our_lksb.sb_status == EUNLOCK)
    {
	return;
    }

    if (our_lksb.sb_status == SUCCESS)
    {
	granted = 1;
	switch (cur_mode)
	{
	case LKM_NLMODE:
	    printf("pinglock. compast, (valblk = %d) now at NL\n", *lvb_int);
	    if (convert_lock(LKM_CRMODE) == 0)
		granted = 0;
	    break;

	case LKM_CRMODE:
	    printf("pinglock. compast, (valblk = %d) now at CR\n", *lvb_int);
	    if (*lvb_int == us-1)
	    {
		if (convert_lock(LKM_EXMODE) == 0)
		    granted = 0;
	    }
	    break;

	case LKM_EXMODE:
	    printf("pinglock. compast. (valblk = %d) now at EX\n", *lvb_int);
	    if (*lvb_int == us-1)
	    {
		(*lvb_int)++;
		*lvb_int %= maxnode;
		printf("pinglock. compast. incrementing valblk to %d\n", *lvb_int);
	    }
	    if (convert_lock(LKM_CRMODE) == 0)
		granted = 0;
	    break;
	}
    }
    else
    {
	printf("pinglock: lock failed (compast): %d\n", our_lksb.sb_status);
	unlock();
    }
}

static void blockast_routine(void *arg)
{
    printf("pinglock. blkast, granted = %d\n", granted);
    if (!granted)
    {
	return;
    }

    printf("pinglock. blkast, demoting lock to NL\n");
    if (convert_lock(LKM_NLMODE) == 0)
	granted = 0;
}


#ifdef __linux__
int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        printf("usage: %s <maxnodes> <us>\n", argv[0]);
	return 2;
    }
    maxnode = atoi(argv[1]);
    us = atoi(argv[2]);

    dlm_pthread_init();
    start_lock();
    while(1)
	    sleep(100000);
    return 0;
}


#endif

#ifdef VMS
int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        printf("usage: %s <maxnodes> <us>\n", argv[0]);
	return 2;
    }
    maxnode = atoi(argv[1]);
    us = atoi(argv[2]);

    start_lock();
    while(1)
	sys$hiber();

    return SS$_NORMAL;
}
#endif
