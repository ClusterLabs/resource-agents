/* Test the locking interface */

/* Creates a file called /proc/gtest that you can echo the following commands into:

   lock <name> <mode> [<parent-id>] [<range start>] [<range end>]
   unlock <id> [force]
   convert <id> <mode> [<range start>] [<range end>]

   Lock IDs must be in hex.

   cat /proc/gtest will show the locks known to this module

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


#define atoi(x) simple_strtoul(x, NULL, 16)

struct my_lock
{
    osi_list_t list;
    char name[GDLM_RESNAME_MAXLEN];
    int  rqmode;
    int  grmode;
    int  parent;
    int  bastmode;
    gdlm_lksb_t lksb;
    gdlm_range_t range;
};
static osi_list_t our_locks;

/* A "spare" lksb for unlocking other peoples locks with */
static gdlm_lksb_t spare_lksb;

static gdlm_lockspace_t *lockspace;
char *ls = "gtest"; /* Lockspace name */
char *procname = "gtest";       /* proc file name */

static void compast_routine(void *arg);
static void blockast_routine(void *arg, int mode);

struct proc_dir_entry *gdlm_proc_entry = NULL;

static int modetonum(char *modestr)
{
    int mode = GDLM_LOCK_EX;

    if (strnicmp(modestr, "NL", 2) == 0) mode = GDLM_LOCK_NL;
    if (strnicmp(modestr, "CR", 2) == 0) mode = GDLM_LOCK_CR;
    if (strnicmp(modestr, "CW", 2) == 0) mode = GDLM_LOCK_CW;
    if (strnicmp(modestr, "PR", 2) == 0) mode = GDLM_LOCK_PR;
    if (strnicmp(modestr, "PW", 2) == 0) mode = GDLM_LOCK_PW;
    if (strnicmp(modestr, "EX", 2) == 0) mode = GDLM_LOCK_EX;

    return mode;
}

static char *numtomode(int mode)
{
    switch (mode)
    {
    case GDLM_LOCK_NL: return "NL";
    case GDLM_LOCK_CR: return "CR";
    case GDLM_LOCK_CW: return "CW";
    case GDLM_LOCK_PR: return "PR";
    case GDLM_LOCK_PW: return "PW";
    case GDLM_LOCK_EX: return "EX";
    default: return "??";
    }
}

static struct my_lock *find_mylock(int id)
{
    osi_list_t *l;
    osi_list_foreach(l, &our_locks) {
	struct my_lock *mlk = osi_list_entry(l, struct my_lock, list);
	if (mlk->lksb.sb_lkid == id)
	    return mlk;
    }
    return NULL;
}

static int show_locks(char *buf, int maxlen)
{
    osi_list_t *l;
    int len = 0;

    len += sprintf(buf+len, "    lkid    parent rq gr  name\n");
    osi_list_foreach(l, &our_locks) {
	struct my_lock *mlk = osi_list_entry(l, struct my_lock, list);

	len += sprintf(buf+len, "%8x  %8x %s %s  %-32s  %llx-%llx\n",
		       mlk->lksb.sb_lkid,
		       mlk->parent,
		       numtomode(mlk->rqmode),
		       numtomode(mlk->grmode),
		       mlk->name,
		       mlk->range.ra_start, mlk->range.ra_end);
    }
    return len;
}

static void compast_routine(void *arg)
{
    struct my_lock *lki = (struct my_lock *)arg;

    printk("locktest: Completion AST for %x, status = %d\n", lki->lksb.sb_lkid, lki->lksb.sb_status);
    /* Lock or convert suceeded */
    if (lki->lksb.sb_status == 0) {
	lki->grmode = lki->rqmode;
    }

    /* Convert failed */
    if (lki->lksb.sb_status != 0 && lki->grmode != -1) {
	lki->rqmode = lki->grmode;
    }

    /* Lock failed - remove it */
    if (lki->lksb.sb_status != 0 && lki->grmode == -1) {
	osi_list_del(&lki->list);
	osi_free(lki->lksb.sb_lvbptr, GDLM_LVB_LEN);
	osi_free(lki, sizeof(struct my_lock));
	MOD_DEC_USE_COUNT;

    }

    /* Unlock suceeded */
    if (lki->lksb.sb_status == -GDLM_EUNLOCK) {
	osi_list_del(&lki->list);
	osi_free(lki->lksb.sb_lvbptr, GDLM_LVB_LEN);
	osi_free(lki, sizeof(struct my_lock));
	MOD_DEC_USE_COUNT;
    }

}

static void blockast_routine(void *arg, int mode)
{
    struct my_lock *lki = (struct my_lock *)arg;
    int status;

    printk("locktest: Blocking AST for %x\n", lki->lksb.sb_lkid);
    if (lki->bastmode != -1) {
	/* convert lock to bastmode */
	lki->rqmode = lki->bastmode;

	status = gdlm_lock(lockspace,
			   lki->bastmode,
			   &lki->lksb,
			   GDLM_LKF_CONVERT,
			   NULL,
			   0,
			   0,
			   compast_routine,
			   lki,
			   blockast_routine, NULL);
	if (status == 0) {
	    lki->rqmode = lki->bastmode;
	    lki->bastmode = -1;
	}
    }
}


static int lock_command(char *cmd)
{
    char *str;
    int  i = 1;
    struct my_lock *newlock;
    int status;

    newlock = osi_malloc(sizeof(struct my_lock));
    if (!newlock)
	return -ENOMEM;
 
    memset(newlock, 0, sizeof(*newlock));

    newlock->lksb.sb_lvbptr = osi_malloc(GDLM_LVB_LEN);
    if (!newlock->lksb.sb_lvbptr)
    {
        osi_free(newlock, sizeof(struct my_lock));
        return -ENOMEM;
    }
    memset(newlock->lksb.sb_lvbptr, 0, GDLM_LVB_LEN);

    newlock->bastmode = -1;
    newlock->grmode = -1;

    str = strtok(cmd, " ");
    while (str) {
	switch (i) {
	case 1:
	    strcpy(newlock->name, str);
	    break;

	case 2:
	    newlock->rqmode = modetonum(str);
	    break;

	case 3:
	    newlock->parent = atoi(str);
	    break;

	case 4:
	    newlock->range.ra_start = atoi(str);
	    newlock->range.ra_end = 0xffffffffffffffffULL;
	    break;

	case 5:
	    newlock->range.ra_end = atoi(str);
	    break;
	}
	i++;
	str = strtok(NULL, " ");
    }

    status = gdlm_lock(lockspace,
		       newlock->rqmode,
		       &newlock->lksb,
		       0,
		       newlock->name,
		       strlen(newlock->name)+1,
		       newlock->parent,
		       compast_routine,
		       newlock,
		       blockast_routine,
	               (i>4)?&newlock->range:NULL);

    if (status < 0) {
	osi_free(newlock->lksb.sb_lvbptr, GDLM_LVB_LEN);
	osi_free(newlock, sizeof(struct my_lock));
    }
    else {
	MOD_INC_USE_COUNT;
	osi_list_add(&newlock->list, &our_locks);
    }

    return status;
}


static int unlock_command(char *cmd)
{
    struct my_lock *lockstruct=NULL;
    char *str;
    int i=1;
    int status;
    int lkid = 0;
    int force = 0;

    str = strtok(cmd, " ");
    while (str) {
	switch (i) {
	case 1:
	    lkid = atoi(str);
	    lockstruct = find_mylock(lkid);
	    break;

	case 2:
	    if (strcmp(str, "force") == 0)
		force = 1;
	    break;
	}
	i++;
	str = strtok(NULL, " ");
    }

    /* Must have "force" if it's not our lock */
    if (!lockstruct && !force) return -EINVAL;

    if (!lockstruct)
    {
	/* Force out a lock */
	status = gdlm_unlock(lockspace,
			     lkid,
			     0,
			     &spare_lksb,
			     NULL);
    }
    else
    {
	status = gdlm_unlock(lockspace,
			     lockstruct->lksb.sb_lkid,
			     i>1? GDLM_LKF_VALBLK: 0,
			     &lockstruct->lksb,
			     NULL);
    }
    return status;
}

static int convert_command(char *cmd)
{
    struct my_lock *lockstruct = NULL;
    char *str;
    int i=1;
    int status;
    int flags;

    str = strtok(cmd, " ");
    while (str) {
	switch (i) {
	case 1:
	    lockstruct = find_mylock(atoi(str));
	    if (!lockstruct) return -EINVAL;
	    break;

	case 2:
	    lockstruct->rqmode = modetonum(str);
	    break;

	case 3:
	    lockstruct->range.ra_start = atoi(str);
	    lockstruct->range.ra_end = 0xffffffffffffffffULL;
	    break;

	case 4:
	    lockstruct->range.ra_end = atoi(str);
	    break;
	}
	i++;
	str = strtok(NULL, " ");
    }
    if (lockstruct->rqmode > lockstruct->grmode) flags = GDLM_LKF_QUECVT;
    status = gdlm_lock(lockspace,
		       lockstruct->rqmode,
		       &lockstruct->lksb,
		       GDLM_LKF_CONVERT,
		       NULL,
		       0,
		       0,
		       compast_routine,
		       lockstruct,
		       blockast_routine,
		       (i>3)?&lockstruct->range:NULL);

    return status;
}

static int gdlm_proc_read(char *page, char **start, off_t off,
                     int count, int *eof, void *data)
{
    return show_locks(page, count);
}

static int gdlm_proc_write(struct file *file, const char *buffer,
			   unsigned long count, void *data)
{
    int error = -EINVAL, n = count;
    char proc_buf[132];

    MOD_INC_USE_COUNT;

    error = -EFAULT;
    memset(proc_buf, 0, sizeof(proc_buf));

    if (count > sizeof(proc_buf))
        goto fail;

    error = osi_copy_from_user(proc_buf, buffer, n);
    if (error)
        goto fail;

    /* Remove trailing NL */
    if (proc_buf[n-1] == '\n')
	proc_buf[n-1] = '\0';
    error = -EINVAL;
    if (strncmp(proc_buf, "lock ", 5) == 0)
	error = lock_command(proc_buf+5);

    if (strncmp(proc_buf, "unlock ", 7) == 0)
	error = unlock_command(proc_buf+7);

    if (strncmp(proc_buf, "convert ", 8) == 0)
	error = convert_command(proc_buf+8);

    if (error < 0)
	goto fail;

    MOD_DEC_USE_COUNT;
    return count;

 fail:
    printk("LOCKTEST: '%s' failed : %d\n", proc_buf, error);
    MOD_DEC_USE_COUNT;
    return error;
}



static int __init test_init(void)
{
    int ls_status;

    osi_list_init(&our_locks);

    gdlm_proc_entry = create_proc_read_entry(procname, S_IFREG | 0666, NULL,
                                             NULL, NULL);
    gdlm_proc_entry->write_proc = gdlm_proc_write;
    gdlm_proc_entry->read_proc = gdlm_proc_read;

    ls_status = gdlm_new_lockspace(ls, strlen(ls), &lockspace, 0);
    if (ls_status < 0 && ls_status != -EEXIST) {
	remove_proc_entry(procname, NULL);
	return -1;
    }
    return 0;
}

static void __exit test_exit(void)
{
    remove_proc_entry(procname, NULL);
    gdlm_release_lockspace(lockspace);
}

module_init(test_init);
module_exit(test_exit);

MODULE_PARM(ls, "s");
MODULE_PARM(procname, "s");
