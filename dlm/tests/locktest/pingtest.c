/* Ping Test the locking interface */

#ifdef __linux__
#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/capability.h>
#include <linux/inet.h>
#include <linux/file.h>
#include <linux/route.h>
#include <linux/interrupt.h>
#include <net/sock.h>
#include <net/scm.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <net/dst.h>

#include "osi.h"
#include "osi_list.h"
#include "gdlm.h"

static char our_lvb[GDLM_LVB_LEN];
static gdlm_lksb_t our_lksb;
static gdlm_lockspace_t *lockspace;
char *ls = "gdlm_user"; /* Lockspace name */
char *lockname="ping";
#define printf printk
//#define printf(fmt, args...)

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
    char sb_lvb;
};

static struct lksb our_lksb;

#define GDLM_LOCK_NL LCK$K_NLMODE
#define GDLM_LOCK_CR LCK$K_CRMODE
#define GDLM_LOCK_CW LCK$K_CWMODE
#define GDLM_LOCK_PR LCK$K_PRMODE
#define GDLM_LOCK_PW LCK$K_PWMODE
#define GDLM_LOCK_EX LCK$K_EXMODE
#define EDEADLOCK SS$_DEADLOCK

/* This is wrong...VMS does not deliver ASTs
   for $DEQ but we'll never get this code, honest.
 */
#define GDLM_EUNLOCK 65535

#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT

#define SUCCESS SS$_NORMAL

char *lockname="ping";
#endif

/* These are also module parameters for Linux */
int us = 1;
int maxnode = 2;

static int *lvb_int = (int *)our_lvb;
static int cur_mode;

static int granted = 0;
static void compast_routine(void *arg);
static void blockast_routine(void *arg, int mode);

#ifdef __linux__
static int convert_lock(int mode)
{
    int old_mode = cur_mode;
    int status;

    printf("pinglock: convert to %d starting\n", mode);
    status = gdlm_lock(lockspace,
		       mode,
		       &our_lksb,
		       GDLM_LKF_VALBLK | GDLM_LKF_CONVERT | (mode > old_mode?GDLM_LKF_QUECVT:0),
		       NULL,
		       0,
		       0,
		       compast_routine,
		       &our_lksb,
		       blockast_routine,
		       NULL);
    if (status != 0)
    {
	printf("pinglock: convert failed: %d\n", status);
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
    status = gdlm_unlock(lockspace,
			 our_lksb.sb_lkid,
			 0,
			 &our_lksb,
			 0);
    if (status != 0)
	printf("pinglock: unlock failed: %d\n", status);

}

static void start_lock()
{
    int status;
    cur_mode = GDLM_LOCK_EX;

    *lvb_int = us-1;

    printf("pinglock: starting\n");
    status = gdlm_lock(lockspace,
		       cur_mode,
		       &our_lksb,
		       GDLM_LKF_VALBLK,
		       lockname,
		       strlen(lockname)+1, /* include trailing NUL for ease of following */
		       0, /* Parent */
		       compast_routine,
		       &our_lksb,
		       blockast_routine,
		       NULL);
    if (status != 0)
	printf("pinglock: lock failed: %d\n", status);
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
		     LCK$M_CONVERT | LCK$M_VALBLK | (mode > old_mode?LCK$M_QUECVT:0),
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

    /* If it deadlocked then the lock will be demoted back to PR so try again */
    if (our_lksb.sb_status == -EDEADLOCK)
    {
	printf("pinglock: deadlocked\n");
	unlock();
	return;
    }

    if (our_lksb.sb_status == -GDLM_EUNLOCK)
    {
	MOD_DEC_USE_COUNT;
	return;
    }

    if (our_lksb.sb_status == SUCCESS)
    {
	granted = 1;
	switch (cur_mode)
	{
	case GDLM_LOCK_NL:
	    printf("pinglock. compast, (valblk = %d) now at NL\n", *lvb_int);
	    if (convert_lock(GDLM_LOCK_CR) == 0)
		granted = 0;
	    break;

	case GDLM_LOCK_CR:
	    printf("pinglock. compast, (valblk = %d) now at CR\n", *lvb_int);
	    if (*lvb_int == us-1)
	    {
		if (convert_lock(GDLM_LOCK_EX) == 0)
		    granted = 0;
	    }
	    break;

	case GDLM_LOCK_EX:
	    printf("pinglock. compast. (valblk = %d) now at EX\n", *lvb_int);
	    if (*lvb_int == us-1)
	    {
		(*lvb_int)++;
		*lvb_int %= maxnode;
		printf("pinglock. compast. incrementing valblk to %d\n", *lvb_int);
	    }
	    if (convert_lock(GDLM_LOCK_CR) == 0)
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

static void blockast_routine(void *arg, int mode)
{
    printf("pinglock. blkast, granted = %d\n", granted);
    if (!granted)
    {
	return;
    }

    printf("pinglock. blkast, demoting lock to NL\n");
    if (convert_lock(GDLM_LOCK_NL) == 0)
	granted = 0;
}


#ifdef __linux__
static int __init ping_init(void)
{
    int status;

    our_lksb.sb_lvbptr = our_lvb;

    status = gdlm_new_lockspace(ls, strlen(ls), &lockspace, 0);

    if (status < 0 && status != -EEXIST)
    {
	return -1;
    }

    MOD_INC_USE_COUNT;
    start_lock();
    return 0;
}

static void __exit ping_exit(void)
{
//    gdlm_release_lockspace(lockspace);
}

module_init(ping_init);
module_exit(ping_exit);

MODULE_PARM(ls, "s");
MODULE_PARM(us, "i");
MODULE_PARM(maxnode, "i");
MODULE_PARM(lockname, "s");
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
