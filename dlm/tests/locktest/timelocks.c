/* Time locking operations
 *
 * Note that this needs to be able to get an EX lock on 
 * a resource so any other locks on the same resource
 * (to force mastery etc) should be held at NL.
 * but you'd do that anyway, wouldn't you ?
 */

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

static gdlm_lksb_t our_lksb;
static gdlm_lockspace_t *lockspace;
char *ls = "gtest"; /* Share a lockspace with /proc/gtest */
char *lockname="timing";
int convert=0;
int num=1000; /* Number to do */
int count=0; /* Number done */

static struct timeval start_tv, end_tv;

static int cur_mode;
static int granted = 0;
static int failed = 0;
static void compast_routine(void *arg);
static void finish(void);

static int convert_lock(int mode)
{
    int status;

    status = gdlm_lock(lockspace,
		       mode,
		       &our_lksb,
		       GDLM_LKF_CONVERT,
		       NULL,
		       0,
		       0,
		       compast_routine,
		       &our_lksb,
		       NULL,
		       NULL);
    if (status != 0)
    {
	printk("timelocks: convert failed: %d\n", status);
	failed = 1;
	finish();
    }
    else
    {
	cur_mode = mode;
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
    {
	printk("timelocks: unlock failed: %d\n", status);
	failed = 1;
    }

}

static void start_lock()
{
    int status;
    cur_mode = GDLM_LOCK_EX;

    status = gdlm_lock(lockspace,
		       cur_mode,
		       &our_lksb,
		       0,
		       lockname,
		       strlen(lockname)+1, /* include trailing NUL for ease of following */
		       0, /* Parent */
		       compast_routine,
		       &our_lksb,
		       NULL,
		       NULL);
    if (status != 0)
    {
	printk("timelocks: lock failed: %d\n", status);
	failed = 1;
    }
}

static void finish()
{
    if (!failed)
    {
	unsigned long long time_taken;
	do_gettimeofday(&end_tv);

	time_taken = (end_tv.tv_sec*1000000 + end_tv.tv_usec) -
	    (start_tv.tv_sec*1000000 + start_tv.tv_usec);

	printk("timelocks: finished %d %s. Time taken = %lld\n",
	       num, convert?"converts":"lock/unlocks", time_taken);
    }

    MOD_DEC_USE_COUNT;
}

static void compast_routine(void *arg)
{
    /* If it deadlocked then the lock will be demoted back to PR so try again */
    if (our_lksb.sb_status == -EDEADLOCK)
    {
	printk("timelocks: deadlocked\n");
	failed = 1;
	unlock();
	finish();
	return;
    }

    if (our_lksb.sb_status == -GDLM_EUNLOCK)
    {
	if (++count < num)
	{
	    start_lock();
	}
	else
	{
	    finish();
	}
	return;
    }

    if (our_lksb.sb_status == 0)
    {
	granted = 1;
	switch (cur_mode)
	{
	case GDLM_LOCK_NL:
	    if (++count < num)
	    {
		if (convert_lock(GDLM_LOCK_EX) == 0)
		    granted = 0;
	    }
	    else
	    {
		unlock();
	    }
	    break;

	case GDLM_LOCK_EX:
	    if (convert)
	    {
		/* Only time the conversions */
		if (count == 0)
    		    do_gettimeofday(&start_tv);

		if (convert_lock(GDLM_LOCK_NL) == 0)
		    granted = 0;
	    }
	    else
	    {
		unlock();
	    }
	    break;
	}
    }
    else
    {
	printk("timelocks: lock failed (compast): %d\n", our_lksb.sb_status);
	failed = 1;
	unlock();
	finish();
    }
}


static int __init time_init(void)
{
    int status;

    status = gdlm_new_lockspace(ls, strlen(ls), &lockspace, 0);

    if (status < 0 && status != -EEXIST)
    {
	return -1;
    }

    MOD_INC_USE_COUNT;

    do_gettimeofday(&start_tv);
    start_lock();
    return 0;
}

static void __exit time_exit(void)
{
//    gdlm_release_lockspace(lockspace);
}

module_init(time_init);
module_exit(time_exit);

MODULE_PARM(num, "i");
MODULE_PARM(lockname, "s");
MODULE_PARM(ls, "s");
MODULE_PARM(convert, "i");
