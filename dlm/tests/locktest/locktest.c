/* Test the locking interface */

/* Creates a file called /proc/gtest that you can echo the following commands into:

   lock <name> <mode> [<parent-id>] [<value>] [<bastmode>]
   unlock <id> [<value>] [force]
   convert <id> <mode> [<value>]

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

#include <cluster/dlm.h>


#define atoi(x) simple_strtoul(x, NULL, 16)

struct my_lock
{
    struct list_head list;
    char name[DLM_RESNAME_MAXLEN];
    int  rqmode;
    int  grmode;
    int  parent;
    int  bastmode;
    struct dlm_lksb lksb;
};
static struct list_head our_locks;

/* A "spare" lksb for unlocking other peoples locks with */
static struct dlm_lksb spare_lksb;

static dlm_lockspace_t *lockspace;
char *ls = "gtest";        /* Lockspace name */
char *procname = "gtest";  /* proc file name */

static void compast_routine(void *arg);
static void blockast_routine(void *arg, int mode);

struct proc_dir_entry *dlm_proc_entry = NULL;

static int modetonum(char *modestr)
{
    int mode = DLM_LOCK_EX;

    if (strnicmp(modestr, "NL", 2) == 0) mode = DLM_LOCK_NL;
    if (strnicmp(modestr, "CR", 2) == 0) mode = DLM_LOCK_CR;
    if (strnicmp(modestr, "CW", 2) == 0) mode = DLM_LOCK_CW;
    if (strnicmp(modestr, "PR", 2) == 0) mode = DLM_LOCK_PR;
    if (strnicmp(modestr, "PW", 2) == 0) mode = DLM_LOCK_PW;
    if (strnicmp(modestr, "EX", 2) == 0) mode = DLM_LOCK_EX;

    return mode;
}

static char *numtomode(int mode)
{
    switch (mode)
    {
    case DLM_LOCK_NL: return "NL";
    case DLM_LOCK_CR: return "CR";
    case DLM_LOCK_CW: return "CW";
    case DLM_LOCK_PR: return "PR";
    case DLM_LOCK_PW: return "PW";
    case DLM_LOCK_EX: return "EX";
    default: return "??";
    }
}

static struct my_lock *find_mylock(int id)
{
    struct list_head *l;
    list_for_each(l, &our_locks) {
	struct my_lock *mlk = list_entry(l, struct my_lock, list);
	if (mlk->lksb.sb_lkid == id)
	    return mlk;
    }
    return NULL;
}

static int show_locks(char *buf, int maxlen)
{
    struct list_head *l;
    int len = 0;

    len += sprintf(buf+len, "    lkid    parent rq gr  name                              lvb\n");
    list_for_each(l, &our_locks) {
	struct my_lock *mlk = list_entry(l, struct my_lock, list);

	len += sprintf(buf+len, "%8x  %8x %s %s  %-32s  %s\n",
		       mlk->lksb.sb_lkid,
		       mlk->parent,
		       numtomode(mlk->rqmode),
		       numtomode(mlk->grmode),
		       mlk->name,
		       mlk->lksb.sb_lvbptr);
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
	list_del(&lki->list);
	kfree(lki->lksb.sb_lvbptr);
	kfree(lki);
	module_put(THIS_MODULE);
    }

    /* Unlock suceeded */
    if (lki->lksb.sb_status == -DLM_EUNLOCK) {
	list_del(&lki->list);
	kfree(lki->lksb.sb_lvbptr);
	kfree(lki);
	module_put(THIS_MODULE);
    }

}

static void query_ast_routine(void *arg)
{
    struct dlm_lksb *lksb = arg;
    struct dlm_queryinfo *qi = (struct dlm_queryinfo *)lksb->sb_lvbptr;
    int i;

    /* Dump resource info */
    printk("lockinfo: status     = %d\n", lksb->sb_status);
    printk("lockinfo: resource   = '%s'\n", qi->gqi_resinfo->rsi_name);
    printk("lockinfo: grantcount = %d\n", qi->gqi_resinfo->rsi_grantcount);
    printk("lockinfo: convcount  = %d\n", qi->gqi_resinfo->rsi_convcount);
    printk("lockinfo: waitcount  = %d\n", qi->gqi_resinfo->rsi_waitcount);
    printk("lockinfo: masternode = %d\n", qi->gqi_resinfo->rsi_masternode);

    /* Dump all the locks */
    for (i = 0; i < qi->gqi_lockcount; i++)
    {
	struct dlm_lockinfo *li = &qi->gqi_lockinfo[i];

	printk("lockinfo: lock: lkid        = %x\n", li->lki_lkid);
	printk("lockinfo: lock: master lkid = %x\n", li->lki_mstlkid);
	printk("lockinfo: lock: parent lkid = %x\n", li->lki_parent);
	printk("lockinfo: lock: node        = %d\n", li->lki_node);
	printk("lockinfo: lock: state       = %d\n", li->lki_state);
	printk("lockinfo: lock: grmode      = %d\n", li->lki_grmode);
	printk("lockinfo: lock: rqmode      = %d\n", li->lki_rqmode);
	printk("\n");
    }

    if (qi->gqi_lockinfo)
	kfree(qi->gqi_lockinfo);
    kfree(qi->gqi_resinfo);
    kfree(qi);
    kfree(lksb);
}

static void blockast_routine(void *arg, int mode)
{
    struct my_lock *lki = (struct my_lock *)arg;
    int status;

    printk("locktest: Blocking AST for %x\n", lki->lksb.sb_lkid);
    if (lki->bastmode != -1) {
	/* convert lock to bastmode */
	lki->rqmode = lki->bastmode;

	status = dlm_lock(lockspace,
			   lki->bastmode,
			   &lki->lksb,
			   DLM_LKF_CONVERT,
			   NULL,
			   0,
			   0,
			   compast_routine,
			   lki,
			   blockast_routine,
			   NULL);
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

    newlock = kmalloc(sizeof(struct my_lock), GFP_KERNEL);
    if (!newlock)
	return -ENOMEM;

    memset(newlock, 0, sizeof(*newlock));

    newlock->lksb.sb_lvbptr = kmalloc(DLM_LVB_LEN, GFP_KERNEL);
    if (!newlock->lksb.sb_lvbptr)
    {
        kfree(newlock);
        return -ENOMEM;
    }
    memset(newlock->lksb.sb_lvbptr, 0, DLM_LVB_LEN);

    newlock->bastmode = -1;
    newlock->grmode = -1;

    str = strsep(&cmd, " ");
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
	    strcpy(newlock->lksb.sb_lvbptr, str);
	    break;

	case 5:
	    newlock->bastmode = modetonum(str);
	    break;
	}
	i++;
	str = strsep(&cmd, " ");
    }

    status = dlm_lock(lockspace,
		       newlock->rqmode,
		       &newlock->lksb,
		       (i>4)? DLM_LKF_VALBLK: 0,
		       newlock->name,
		       strlen(newlock->name)+1,
		       newlock->parent,
		       compast_routine,
		       newlock,
		       blockast_routine,
		       NULL);

    if (status < 0) {
	kfree(newlock->lksb.sb_lvbptr);
	kfree(newlock);
    }
    else {
	try_module_get(THIS_MODULE);
	list_add(&newlock->list, &our_locks);
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

    str = strsep(&cmd, " ");
    while (str) {
	switch (i) {
	case 1:
	    lkid = atoi(str);
	    lockstruct = find_mylock(lkid);
	    break;

	case 2:
	    if (lockstruct)
		strcpy(lockstruct->lksb.sb_lvbptr, str);
	    break;
	case 3:
	    if (strcmp(str, "force") == 0)
		force = 1;
	}
	i++;
	str = strsep(&cmd, " ");
    }

    /* Must have "force" if it's not our lock */
    if (!lockstruct && !force) return -EINVAL;

    if (!lockstruct)
    {
	/* Force out a lock */
	status = dlm_unlock(lockspace,
			     lkid,
			     0,
			     &spare_lksb,
			     NULL);
    }
    else
    {
	status = dlm_unlock(lockspace,
			     lockstruct->lksb.sb_lkid,
			     i>1? DLM_LKF_VALBLK: 0,
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

    str = strsep(&cmd, " ");
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
	    strcpy(lockstruct->lksb.sb_lvbptr, str);
	    break;
	}
	i++;
	str = strsep(&cmd, " ");
    }
    if (lockstruct->rqmode > lockstruct->grmode) flags = DLM_LKF_QUECVT;
    status = dlm_lock(lockspace,
		       lockstruct->rqmode,
		       &lockstruct->lksb,
		       DLM_LKF_CONVERT,
		       NULL,
		       0,
		       0,
		       compast_routine,
		       lockstruct,
		       blockast_routine,
		       NULL);

    return status;
}


static int query_command(char *cmd)
{
    char *str;
    int i=1;
    int status;
    int lkid = 0;
    int querytype = DLM_QUERY_QUEUE_ALL | DLM_QUERY_LOCKS_ALL;
    struct dlm_queryinfo *qi;
    struct dlm_lksb *lksb;
    int maxlocks = 100;

    str = strsep(&cmd, " ");
    while (str) {
	switch (i) {
	case 1:
	    lkid = atoi(str);
	    break;

	case 2: /* Optional */
	    querytype = atoi(str);
	    break;

	case 3: /* max locks */
	    maxlocks = atoi(str);
	    break;
	}
	i++;
	str = strsep(&cmd, " ");
    }

    qi = kmalloc(sizeof(struct dlm_queryinfo), GFP_KERNEL);
    qi->gqi_resinfo = kmalloc(sizeof(struct dlm_resinfo), GFP_KERNEL);
    qi->gqi_lockinfo = kmalloc(sizeof(struct dlm_lockinfo)*maxlocks, GFP_KERNEL);
    lksb = kmalloc(sizeof(struct dlm_lksb), GFP_KERNEL);
    qi->gqi_locksize = maxlocks;
    lksb->sb_lkid = lkid;
    lksb->sb_status = 0 ;
    lksb->sb_lvbptr = (char *)qi;

    status = dlm_query(lockspace,
			lksb,
			querytype,
			qi,
			query_ast_routine,
			lksb);

    return status;
}

static int dlm_proc_read(char *page, char **start, off_t off,
                     int count, int *eof, void *data)
{
    return show_locks(page, count);
}

static int dlm_proc_write(struct file *file, const char *buffer,
			   unsigned long count, void *data)
{
    int error = -EINVAL, n = count;
    char proc_buf[132];

    try_module_get(THIS_MODULE);

    error = -EFAULT;
    memset(proc_buf, 0, sizeof(proc_buf));

    if (count > sizeof(proc_buf))
        goto fail;

    error = copy_from_user(proc_buf, buffer, n) ? -EFAULT : 0;
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

    if (strncmp(proc_buf, "query ", 6) == 0)
	error = query_command(proc_buf+6);

    if (error < 0)
	goto fail;

    module_put(THIS_MODULE);
    return count;

 fail:
    printk("LOCKTEST: '%s' failed : %d\n", proc_buf, error);
    module_put(THIS_MODULE);
    return error;
}



static int __init test_init(void)
{
    int ls_status;

    INIT_LIST_HEAD(&our_locks);

    dlm_proc_entry = create_proc_read_entry(procname, S_IFREG | 0666, NULL,
                                             NULL, NULL);
    dlm_proc_entry->write_proc = dlm_proc_write;
    dlm_proc_entry->read_proc = dlm_proc_read;

    ls_status = dlm_new_lockspace(ls, strlen(ls), &lockspace, 0);
    if (ls_status < 0 && ls_status != -EEXIST) {
	remove_proc_entry(procname, NULL);
	return -1;
    }
    return 0;
}

static void __exit test_exit(void)
{
    remove_proc_entry(procname, NULL);
    dlm_release_lockspace(lockspace,0);
}

module_init(test_init);
module_exit(test_exit);

MODULE_PARM(ls, "s");
MODULE_PARM(procname, "s");
MODULE_LICENSE("GPL");
