/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

/*
 * device.c
 *
 * This is the userland interface to the DLM.
 *
 * The locking is done via a misc char device (find the
 * registered minor number in /proc/misc).
 *
 * User code should not use this interface directly but
 * call the library routines in libdlm.a instead.
 *
 */

#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/signal.h>
#include <linux/spinlock.h>
#include <asm/ioctls.h>

#include "dlm_internal.h"
#include "device.h"

extern struct dlm_lkb *dlm_get_lkb(struct dlm_ls *, int);
static struct file_operations _dlm_fops;
static const char *name_prefix="dlm";
static struct list_head user_ls_list;
static struct semaphore user_ls_lock;

/* Flags in li_flags */
#define LI_FLAG_COMPLETE  1
#define LI_FLAG_FIRSTLOCK 2

struct lock_info {
	uint8_t li_cmd;
	struct dlm_lksb li_lksb;
	wait_queue_head_t li_waitq;
	unsigned long li_flags;
	void __user *li_castparam;
	void __user *li_castaddr;
	void __user *li_bastparam;
	void __user *li_bastaddr;
	struct file_info *li_file;
	struct dlm_lksb __user *li_user_lksb;
	struct semaphore li_firstlock;
	struct dlm_queryinfo *li_queryinfo;
	struct dlm_queryinfo __user *li_user_queryinfo;
};

/* A queued AST no less */
struct ast_info {
	struct dlm_lock_result result;
	struct dlm_queryinfo *queryinfo;
	struct dlm_queryinfo __user *user_queryinfo;
	struct list_head list;
};

/* One of these per userland lockspace */
struct user_ls {
	void    *ls_lockspace;
	atomic_t ls_refcnt;
	long     ls_flags; /* bit 1 means LS has been deleted */

	/* Passed into misc_register() */
	struct miscdevice ls_miscinfo;
	struct list_head  ls_list;
};

/* misc_device info for the control device */
static struct miscdevice ctl_device;

/*
 * Stuff we hang off the file struct.
 * The first two are to cope with unlocking all the
 * locks help by a process when it dies.
 */
struct file_info {
	struct list_head    fi_lkb_list;     /* List of active lkbs */
	spinlock_t          fi_lkb_lock;
	struct list_head    fi_ast_list;     /* Queue of ASTs to be delivered */
	spinlock_t          fi_ast_lock;
	wait_queue_head_t   fi_wait;
	struct user_ls     *fi_ls;
	atomic_t            fi_refcnt;       /* Number of users */
	unsigned long       fi_flags;        /* Bit 1 means the device is open */
};


/* get and put ops for file_info.
   Actually I don't really like "get" and "put", but everyone
   else seems to use them and I can't think of anything
   nicer at the moment */
static void get_file_info(struct file_info *f)
{
	atomic_inc(&f->fi_refcnt);
}

static void put_file_info(struct file_info *f)
{
	if (atomic_dec_and_test(&f->fi_refcnt))
		kfree(f);
}

static struct user_ls *__find_lockspace(int minor)
{
	struct user_ls *lsinfo;

	list_for_each_entry(lsinfo, &user_ls_list, ls_list) {

		if (lsinfo->ls_miscinfo.minor == minor)
			return lsinfo;
	}
	return NULL;
}

/* Find a lockspace struct given the device minor number */
static struct user_ls *find_lockspace(int minor)
{
	struct user_ls *lsinfo;

	down(&user_ls_lock);
	lsinfo = __find_lockspace(minor);
	up(&user_ls_lock);

	return lsinfo;
}

static void add_lockspace_to_list(struct user_ls *lsinfo)
{
	down(&user_ls_lock);
	list_add(&lsinfo->ls_list, &user_ls_list);
	up(&user_ls_lock);
}

/* Register a lockspace with the DLM and create a misc
   device for userland to access it */
static int register_lockspace(char *name, struct user_ls **ls)
{
	struct user_ls *newls;
	int status;
	int namelen;

	namelen = strlen(name)+strlen(name_prefix)+2;

	newls = kmalloc(sizeof(struct user_ls), GFP_KERNEL);
	if (!newls)
		return -ENOMEM;
	memset(newls, 0, sizeof(struct user_ls));

	newls->ls_miscinfo.name = kmalloc(namelen, GFP_KERNEL);
	if (!newls->ls_miscinfo.name) {
		kfree(newls);
		return -ENOMEM;
	}
	snprintf((char*)newls->ls_miscinfo.name, namelen, "%s_%s", name_prefix, name);

	status = dlm_new_lockspace((char *)newls->ls_miscinfo.name+strlen(name_prefix)+1,
				    strlen(newls->ls_miscinfo.name) - strlen(name_prefix) - 1,
				    &newls->ls_lockspace, DLM_LSF_NOCONVGRANT);

	if (status != 0) {
		kfree(newls->ls_miscinfo.name);
		kfree(newls);
		return status;
	}

	newls->ls_miscinfo.fops = &_dlm_fops;
	newls->ls_miscinfo.minor = MISC_DYNAMIC_MINOR;

	status = misc_register(&newls->ls_miscinfo);
	if (status) {
		log_print("failed to register misc device for %s", name);
		dlm_release_lockspace(newls->ls_lockspace, 0);
		kfree(newls->ls_miscinfo.name);
		kfree(newls);
		return status;
	}


	add_lockspace_to_list(newls);
	*ls = newls;
	return 0;
}

/* Called with the user_ls_lock semaphore held */
static int unregister_lockspace(struct user_ls *lsinfo, int force)
{
	int status;

	status = dlm_release_lockspace(lsinfo->ls_lockspace, force);
	if (status)
		return status;

	status = misc_deregister(&lsinfo->ls_miscinfo);
	if (status)
		return status;

	list_del(&lsinfo->ls_list);
	set_bit(1, &lsinfo->ls_flags); /* LS has been deleted */
	lsinfo->ls_lockspace = NULL;
	if (atomic_dec_and_test(&lsinfo->ls_refcnt)) {
		kfree(lsinfo->ls_miscinfo.name);
		kfree(lsinfo);
	}

	return 0;
}

/* Add it to userland's AST queue */
static void add_to_astqueue(struct lock_info *li, void *astaddr, void *astparam)
{
	struct ast_info *ast = kmalloc(sizeof(struct ast_info), GFP_KERNEL);
	if (!ast)
		return;

	ast->result.astparam  = astparam;
	ast->result.astaddr   = astaddr;
	ast->result.user_lksb = li->li_user_lksb;
	ast->result.cmd       = li->li_cmd;
	memcpy(&ast->result.lksb, &li->li_lksb, sizeof(struct dlm_lksb));

	/* These two will both be NULL for anything other than queries */
	ast->queryinfo        = li->li_queryinfo;
	ast->user_queryinfo   = li->li_user_queryinfo;

	spin_lock(&li->li_file->fi_ast_lock);
	list_add_tail(&ast->list, &li->li_file->fi_ast_list);
	spin_unlock(&li->li_file->fi_ast_lock);
	wake_up_interruptible(&li->li_file->fi_wait);
}

static void bast_routine(void *param, int mode)
{
	struct lock_info *li = param;

	if (param) {
		add_to_astqueue(li, li->li_bastaddr, li->li_bastparam);
	}
}

/*
 * This is the kernel's AST routine.
 * All lock, unlock & query operations complete here.
 * The only syncronous ops are those done during device close.
 */
static void ast_routine(void *param)
{
	struct lock_info *li = param;

	/* Param may be NULL if a persistent lock is unlocked by someone else */
	if (!param)
		return;

	/* If it's an async request then post data to the user's AST queue. */
	if (li->li_castaddr) {

		/* Only queue AST if the device is still open */
		if (test_bit(1, &li->li_file->fi_flags))
			add_to_astqueue(li, li->li_castaddr, li->li_castparam);

		/* If it's a new lock operation that failed, then
		 * remove it from the owner queue and free the
		 * lock_info. The DLM will not free the LKB until this
		 * AST has completed.
		 */
		if (test_and_clear_bit(LI_FLAG_FIRSTLOCK, &li->li_flags) &&
		    li->li_lksb.sb_status != 0) {
			struct dlm_lkb *lkb;

			/* Wait till dlm_lock() has finished */
			down(&li->li_firstlock);
			lkb = dlm_get_lkb(li->li_file->fi_ls->ls_lockspace, li->li_lksb.sb_lkid);
			if (lkb) {
				spin_lock(&li->li_file->fi_lkb_lock);
				list_del(&lkb->lkb_ownerqueue);
				spin_unlock(&li->li_file->fi_lkb_lock);
			}
			up(&li->li_firstlock);
			put_file_info(li->li_file);
			kfree(li);
			return;
		}
		/* Free unlocks & queries */
		if (li->li_lksb.sb_status == -DLM_EUNLOCK ||
		    li->li_cmd == DLM_USER_QUERY) {
			put_file_info(li->li_file);
			kfree(li);
		}
	}
	else {
		/* Syncronous request, just wake up the caller */
		set_bit(LI_FLAG_COMPLETE, &li->li_flags);
		wake_up_interruptible(&li->li_waitq);
	}
}

/*
 * Wait for the lock op to complete and return the status.
 */
static int wait_for_ast(struct lock_info *li)
{
	/* Wait for the AST routine to complete */
	set_task_state(current, TASK_INTERRUPTIBLE);
	while (!test_bit(LI_FLAG_COMPLETE, &li->li_flags))
		schedule();

	set_task_state(current, TASK_RUNNING);

	return li->li_lksb.sb_status;
}


/* Open on control device */
static int dlm_ctl_open(struct inode *inode, struct file *file)
{
	return 0;
}

/* Close on control device */
static int dlm_ctl_close(struct inode *inode, struct file *file)
{
	return 0;
}

/* Open on lockspace device */
static int dlm_open(struct inode *inode, struct file *file)
{
	struct file_info *f;
	struct user_ls *lsinfo;

	lsinfo = find_lockspace(iminor(inode));
	if (!lsinfo)
		return -ENOENT;

	f = kmalloc(sizeof(struct file_info), GFP_KERNEL);
	if (!f)
		return -ENOMEM;

	atomic_inc(&lsinfo->ls_refcnt);
	INIT_LIST_HEAD(&f->fi_lkb_list);
	INIT_LIST_HEAD(&f->fi_ast_list);
	spin_lock_init(&f->fi_ast_lock);
	spin_lock_init(&f->fi_lkb_lock);
	init_waitqueue_head(&f->fi_wait);
	f->fi_ls = lsinfo;
	atomic_set(&f->fi_refcnt, 1);
	set_bit(1, &f->fi_flags);

	file->private_data = f;

	return 0;
}

/* Check the user's version matches ours */
static int check_version(struct dlm_lock_params *params)
{
	if (params->version[0] != DLM_DEVICE_VERSION_MAJOR ||
	    (params->version[0] == DLM_DEVICE_VERSION_MAJOR &&
	     params->version[1] > DLM_DEVICE_VERSION_MINOR)) {

		log_print("version mismatch user (%d.%d.%d) kernel (%d.%d.%d)",
		       params->version[0],
		       params->version[1],
		       params->version[2],
		       DLM_DEVICE_VERSION_MAJOR,
		       DLM_DEVICE_VERSION_MINOR,
		       DLM_DEVICE_VERSION_PATCH);
		return -EINVAL;
	}
	return 0;
}

/* Close on lockspace device */
static int dlm_close(struct inode *inode, struct file *file)
{
	struct file_info *f = file->private_data;
	struct lock_info li;
	sigset_t tmpsig;
	sigset_t allsigs;
	struct dlm_lkb *lkb, *safe;
	struct user_ls *lsinfo;
	DECLARE_WAITQUEUE(wq, current);

	lsinfo = find_lockspace(iminor(inode));
	if (!lsinfo)
		return -ENOENT;

	/* Mark this closed so that ASTs will not be delivered any more */
	clear_bit(1, &f->fi_flags);

	/* Block signals while we are doing this */
	sigfillset(&allsigs);
	sigprocmask(SIG_BLOCK, &allsigs, &tmpsig);

	/* We use our own lock_info struct here, so that any
	 * outstanding "real" ASTs will be delivered with the
	 * corresponding "real" params, thus freeing the lock_info
	 * that belongs the lock. This catches the corner case where
	 * a lock is BUSY when we try to unlock it here
	 */
	memset(&li, 0, sizeof(li));
	clear_bit(LI_FLAG_COMPLETE, &li.li_flags);
	init_waitqueue_head(&li.li_waitq);
	add_wait_queue(&li.li_waitq, &wq);

	/*
	 * Free any outstanding locks, they are on the
	 * list in LIFO order so there should be no problems
	 * about unlocking parents before children.
	 * Although we don't remove the lkbs from the list here
	 * (what would be the point?), foreach_safe is needed
	 * because the lkbs are freed during dlm_unlock operations
	 */
	list_for_each_entry_safe(lkb, safe, &f->fi_lkb_list, lkb_ownerqueue) {
		int status;
		int lock_status;
		int flags = 0;
		struct lock_info *old_li;

		/* Make a copy of this pointer. If all goes well we will
		 * free it later. if not it will be left to the AST routine
		 * to tidy up
		 */
		old_li = (struct lock_info *)lkb->lkb_astparam;

		/* Don't unlock persistent locks */
		if (lkb->lkb_flags & GDLM_LKFLG_PERSISTENT) {
			list_del(&lkb->lkb_ownerqueue);

			/* But tidy our references in it */
			kfree(old_li);
			lkb->lkb_astparam = (long)NULL;
			put_file_info(f);
			continue;
		}

		clear_bit(LI_FLAG_COMPLETE, &li.li_flags);

		/* If it's not granted then cancel the request.
		 * If the lock was WAITING then it will be dropped,
		 *    if it was converting then it will be reverted to GRANTED,
		 *    then we will unlock it.
		 */
		lock_status = lkb->lkb_status;

		if (lock_status != GDLM_LKSTS_GRANTED)
			flags = DLM_LKF_CANCEL;

		status = dlm_unlock(f->fi_ls->ls_lockspace, lkb->lkb_id, flags, &li.li_lksb, &li);

		/* Must wait for it to complete as the next lock could be its
		 * parent */
		if (status == 0)
			wait_for_ast(&li);

		/* If it was waiting for a conversion, it will
		   now be granted so we can unlock it properly */
		if (lock_status == GDLM_LKSTS_CONVERT) {

			clear_bit(LI_FLAG_COMPLETE, &li.li_flags);
			status = dlm_unlock(f->fi_ls->ls_lockspace, lkb->lkb_id, 0, &li.li_lksb, &li);

			if (status == 0)
				wait_for_ast(&li);
		}
		/* Unlock suceeded, free the lock_info struct. */
		if (status == 0) {
			kfree(old_li);
			put_file_info(f);
		}
	}

	remove_wait_queue(&li.li_waitq, &wq);

	/* If this is the last reference, and the lockspace has been deleted
	   then free the struct */
	if (atomic_dec_and_test(&lsinfo->ls_refcnt) && !lsinfo->ls_lockspace) {
		kfree(lsinfo->ls_miscinfo.name);
		kfree(lsinfo);
	}

	/* Restore signals */
	sigprocmask(SIG_SETMASK, &tmpsig, NULL);
	recalc_sigpending();

	return 0;
}

/*
 * ioctls to create/remove lockspaces, and check how many
 * outstanding ASTs there are against a particular LS.
 */
static int dlm_ioctl(struct inode *inode, struct file *file,
		     uint command, ulong u)
{
	struct file_info *fi = file->private_data;
	int status = -EINVAL;
	int count;
	struct list_head *tmp_list;

	switch (command) {

		/* Are there any ASTs for us to read?
		 * Warning, this returns the number of messages (ASTs)
		 * in the queue, NOT the number of bytes to read
		 */
	case FIONREAD:
		count = 0;
		spin_lock(&fi->fi_ast_lock);
		list_for_each(tmp_list, &fi->fi_ast_list)
			count++;
		spin_unlock(&fi->fi_ast_lock);
		status = put_user(count, (int *)u);
		break;

	default:
		return -ENOTTY;
	}

	return status;
}

/*
 * ioctls to create/remove lockspaces.
 */
static int dlm_ctl_ioctl(struct inode *inode, struct file *file,
			 uint command, ulong u)
{
	int status = -EINVAL;
	char ls_name[MAX_LS_NAME_LEN];
	struct user_ls *lsinfo;
	int force = 0;

	switch (command) {
	case DLM_CREATE_LOCKSPACE:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (strncpy_from_user(ls_name, (char*)u, MAX_LS_NAME_LEN) < 0)
			return -EFAULT;
		status = register_lockspace(ls_name, &lsinfo);

		/* If it succeeded then return the minor number */
		if (status == 0)
			status = lsinfo->ls_miscinfo.minor;
		break;

	case DLM_FORCE_RELEASE_LOCKSPACE:
		force = 2;

	case DLM_RELEASE_LOCKSPACE:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		down(&user_ls_lock);
		lsinfo = __find_lockspace(u);
		if (!lsinfo) {
			up(&user_ls_lock);
			return -EINVAL;
		}

		status = unregister_lockspace(lsinfo, force);
		up(&user_ls_lock);
		break;

	default:
		return -ENOTTY;
	}

	return status;
}

/* Deal with the messy stuff of copying a web of structs
   from kernel space to userspace */
static int copy_query_result(struct ast_info *ast)
{
	int status = -EFAULT;
	struct dlm_queryinfo qi;

	/* Get the pointers to userspace structs */
	if (copy_from_user(&qi, ast->user_queryinfo,
			   sizeof(struct dlm_queryinfo)))
		goto copy_out;

	/* TODO: does this deref a user pointer? */
	if (put_user(ast->queryinfo->gqi_lockcount,
		     &ast->user_queryinfo->gqi_lockcount))
		goto copy_out;

	if (qi.gqi_resinfo) {
		if (copy_to_user(qi.gqi_resinfo, ast->queryinfo->gqi_resinfo,
				 sizeof(struct dlm_resinfo)))
			goto copy_out;
	}

	if (qi.gqi_lockinfo) {
		if (copy_to_user(qi.gqi_lockinfo, ast->queryinfo->gqi_lockinfo,
				 sizeof(struct dlm_lockinfo) * ast->queryinfo->gqi_lockcount))
			goto copy_out;
	}

	status = 0;

	if (ast->queryinfo->gqi_lockinfo)
		kfree(ast->queryinfo->gqi_lockinfo);

	if (ast->queryinfo->gqi_resinfo)
		kfree(ast->queryinfo->gqi_resinfo);

	kfree(ast->queryinfo);

 copy_out:
	return status;
}

/* Read call, might block if no ASTs are waiting.
 * It will only ever return one message at a time, regardless
 * of how many are pending.
 */
static ssize_t dlm_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct file_info *fi = file->private_data;
	struct ast_info *ast;
	int ret;
	DECLARE_WAITQUEUE(wait, current);

	if (count < sizeof(struct dlm_lock_result))
		return -EINVAL;

	spin_lock(&fi->fi_ast_lock);
	if (list_empty(&fi->fi_ast_list)) {

		/* No waiting ASTs.
		 * Return EOF if the lockspace been deleted.
		 */
		if (test_bit(1, &fi->fi_ls->ls_flags))
			return 0;

		if (file->f_flags & O_NONBLOCK) {
			spin_unlock(&fi->fi_ast_lock);
			return -EAGAIN;
		}

		add_wait_queue(&fi->fi_wait, &wait);

	repeat:
		set_current_state(TASK_INTERRUPTIBLE);
		if (list_empty(&fi->fi_ast_list) &&
		    !signal_pending(current)) {

			spin_unlock(&fi->fi_ast_lock);
			schedule();
			spin_lock(&fi->fi_ast_lock);
			goto repeat;
		}

		current->state = TASK_RUNNING;
		remove_wait_queue(&fi->fi_wait, &wait);

		if (signal_pending(current)) {
			spin_unlock(&fi->fi_ast_lock);
			return -ERESTARTSYS;
		}
	}

	ast = list_entry(fi->fi_ast_list.next, struct ast_info, list);
	list_del(&ast->list);
	spin_unlock(&fi->fi_ast_lock);

	ret = sizeof(struct dlm_lock_result);
	if (copy_to_user(buffer, &ast->result, sizeof(struct dlm_lock_result)))
		ret = -EFAULT;

	/* If it was a query then copy the result block back here */
	if (ast->queryinfo) {
		int status = copy_query_result(ast);
		if (status)
			ret = status;
	}

	kfree(ast);
	return ret;
}

static unsigned int dlm_poll(struct file *file, poll_table *wait)
{
	struct file_info *fi = file->private_data;

	poll_wait(file, &fi->fi_wait, wait);

	spin_lock(&fi->fi_ast_lock);
	if (!list_empty(&fi->fi_ast_list)) {
		spin_unlock(&fi->fi_ast_lock);
		return POLLIN | POLLRDNORM;
	}

	spin_unlock(&fi->fi_ast_lock);
	return 0;
}

static int do_user_query(struct file_info *fi, struct dlm_lock_params *kparams)
{
	struct lock_info *li;
	int status;

	if (!kparams->castaddr)
		return -EINVAL;

	if (!kparams->lksb)
		return -EINVAL;

	li = kmalloc(sizeof(struct lock_info), GFP_KERNEL);
	if (!li)
		return -ENOMEM;

	get_file_info(fi);
	li->li_user_lksb = kparams->lksb;
	li->li_bastparam = kparams->bastparam;
	li->li_bastaddr  = kparams->bastaddr;
	li->li_castparam = kparams->castparam;
	li->li_castaddr  = kparams->castaddr;
	li->li_file      = fi;
	li->li_flags     = 0;
	li->li_cmd       = kparams->cmd;
	clear_bit(LI_FLAG_FIRSTLOCK, &li->li_flags);

	if (copy_from_user(&li->li_lksb, kparams->lksb,
			   sizeof(struct dlm_lksb))) {
		kfree(li);
		return -EFAULT;
	}
	li->li_user_queryinfo = (struct dlm_queryinfo *)li->li_lksb.sb_lvbptr;

	/* Allocate query structs */
	status = -ENOMEM;
	li->li_queryinfo = kmalloc(sizeof(struct dlm_queryinfo), GFP_KERNEL);
	if (!li->li_queryinfo)
		goto out1;

	/* Mainly to get gqi_lock buffer size */
	if (copy_from_user(li->li_queryinfo, li->li_lksb.sb_lvbptr,
			   sizeof(struct dlm_queryinfo))) {
		status = -EFAULT;
		goto out1;
	}

	/* Overwrite userspace pointers we just copied with kernel space ones */
	if (li->li_queryinfo->gqi_resinfo) {
		li->li_queryinfo->gqi_resinfo = kmalloc(sizeof(struct dlm_resinfo), GFP_KERNEL);
		if (!li->li_queryinfo->gqi_resinfo)
			goto out1;
	}
	if (li->li_queryinfo->gqi_lockinfo) {
		li->li_queryinfo->gqi_lockinfo =
			kmalloc(sizeof(struct dlm_lockinfo) * li->li_queryinfo->gqi_locksize,
				GFP_KERNEL);
		if (!li->li_queryinfo->gqi_lockinfo)
			goto out2;
	}

	li->li_lksb.sb_lvbptr = (char *)li->li_queryinfo;

	return dlm_query(fi->fi_ls->ls_lockspace, &li->li_lksb,
			  kparams->flags, /* query */
			  li->li_queryinfo,
			  ast_routine, li);

 out2:
	kfree(li->li_queryinfo);

 out1:
	kfree(li);
	return status;
}

static int do_user_lock(struct file_info *fi, struct dlm_lock_params *kparams,
			const char *buffer)
{
	struct lock_info *li;
	int status;
	char name[DLM_RESNAME_MAXLEN];

	/*
	 * Validate things that we need to have correct.
	 */
	if (!kparams->castaddr)
		return -EINVAL;

	if (!kparams->lksb)
		return -EINVAL;

	/* For conversions, the lock will already have a lock_info
	   block squirelled away in astparam */
	if (kparams->flags & DLM_LKF_CONVERT) {
		struct dlm_lkb *lkb = dlm_get_lkb(fi->fi_ls->ls_lockspace, kparams->lkid);
		if (!lkb) {
			return -EINVAL;
		}
		li = (struct lock_info *)lkb->lkb_astparam;
		li->li_flags = 0;
	}
	else {
		li = kmalloc(sizeof(struct lock_info), GFP_KERNEL);
		if (!li)
			return -ENOMEM;

		li->li_file      = fi;
		li->li_cmd       = kparams->cmd;
		li->li_queryinfo = NULL;
		li->li_flags     = 0;

		/* Get the lock name */
		if (copy_from_user(name, buffer + offsetof(struct dlm_lock_params, name),
				   kparams->namelen)) {
			return -EFAULT;
		}

		/* semaphore to allow us to complete our work before
  		   the AST routine runs. In fact we only need (and use) this
		   when the initial lock fails */
		init_MUTEX_LOCKED(&li->li_firstlock);
		set_bit(LI_FLAG_FIRSTLOCK, &li->li_flags);

		get_file_info(fi);
	}

	li->li_user_lksb = kparams->lksb;
	li->li_bastaddr  = kparams->bastaddr;
        li->li_bastparam = kparams->bastparam;
	li->li_castaddr  = kparams->castaddr;
	li->li_castparam = kparams->castparam;

	/* Copy the user's LKSB into kernel space,
	   needed for conversions & value block operations */
	if (kparams->lksb && copy_from_user(&li->li_lksb, kparams->lksb,
					    sizeof(struct dlm_lksb)))
		return -EFAULT;

	/* Lock it ... */
	status = dlm_lock(fi->fi_ls->ls_lockspace, kparams->mode, &li->li_lksb,
			   kparams->flags, name, kparams->namelen,
			   kparams->parent,
			   ast_routine,
			   li,
			   li->li_bastaddr ? bast_routine : NULL,
			   kparams->range.ra_end ? &kparams->range : NULL);

	/* If it succeeded (this far) with a new lock then keep track of
	   it on the file's lkb list */
	if (!status && !(kparams->flags & DLM_LKF_CONVERT)) {
		struct dlm_lkb *lkb;
		lkb = dlm_get_lkb(fi->fi_ls->ls_lockspace, li->li_lksb.sb_lkid);

		if (lkb) {
			spin_lock(&fi->fi_lkb_lock);
			list_add(&lkb->lkb_ownerqueue,
				 &fi->fi_lkb_list);
			spin_unlock(&fi->fi_lkb_lock);
		}
		else {
			log_print("failed to get lkb for new lock");
		}
		up(&li->li_firstlock);
	}

	return status;
}

static int do_user_unlock(struct file_info *fi, struct dlm_lock_params *kparams)
{
	struct lock_info *li;
	struct dlm_lkb *lkb;
	int status;

	lkb = dlm_get_lkb(fi->fi_ls->ls_lockspace, kparams->lkid);
	if (!lkb) {
		return -EINVAL;
	}

	li = (struct lock_info *)lkb->lkb_astparam;

	li->li_user_lksb = kparams->lksb;
	li->li_castparam = kparams->castparam;
	li->li_cmd       = kparams->cmd;
	/* dlm_unlock() passes a 0 for castaddr which means don't overwrite
	   the existing li_castaddr as that's the completion routine for
	   unlocks. dlm_unlock_wait() specifies a new AST routine to be
	   executed when the unlock completes. */
	if (kparams->castaddr)
		li->li_castaddr = kparams->castaddr;

	/* Have to do it here cos the lkb may not exist after
	 * dlm_unlock() */
	spin_lock(&fi->fi_lkb_lock);
	list_del(&lkb->lkb_ownerqueue);
	spin_unlock(&fi->fi_lkb_lock);

	/* Use existing lksb & astparams */
	status = dlm_unlock(fi->fi_ls->ls_lockspace,
			     kparams->lkid,
			     kparams->flags, NULL, NULL);

	if (status) {
		/* It failed, put it back on the list */
		spin_lock(&fi->fi_lkb_lock);
		list_add(&lkb->lkb_ownerqueue, &fi->fi_lkb_list);
		spin_unlock(&fi->fi_lkb_lock);
	}

	return status;
}

/* Write call, submit a locking request */
static ssize_t dlm_write(struct file *file, const char __user *buffer,
			 size_t count, loff_t *ppos)
{
	struct file_info *fi = file->private_data;
	struct dlm_lock_params kparams;
	sigset_t tmpsig;
	sigset_t allsigs;
	int status;

	if (count < sizeof(kparams)-1)	/* -1 because lock name is optional */
		return -EINVAL;

	/* Has the lockspace been deleted */
	if (test_bit(1, &fi->fi_ls->ls_flags))
		return -ENOENT;

	/* Get the command info */
	if (copy_from_user(&kparams, buffer, sizeof(kparams)))
		return -EFAULT;

	if (check_version(&kparams))
		return -EINVAL;

	/* Block signals while we are doing this */
	sigfillset(&allsigs);
	sigprocmask(SIG_BLOCK, &allsigs, &tmpsig);

	switch (kparams.cmd)
	{
	case DLM_USER_LOCK:
		status = do_user_lock(fi, &kparams, buffer);
		break;

	case DLM_USER_UNLOCK:
		status = do_user_unlock(fi, &kparams);
		break;

	case DLM_USER_QUERY:
		status = do_user_query(fi, &kparams);
		break;

	default:
		status = -EINVAL;
		break;
	}
	/* Restore signals */
	sigprocmask(SIG_SETMASK, &tmpsig, NULL);
	recalc_sigpending();

	if (status == 0)
		return count;
	else
		return status;
}

/* Called when the cluster is shutdown uncleanly, all lockspaces
   have been summarily removed */
void dlm_device_free_devices()
{
	struct user_ls *tmp;
	struct user_ls *lsinfo;

	down(&user_ls_lock);
	list_for_each_entry_safe(lsinfo, tmp, &user_ls_list, ls_list) {
		misc_deregister(&lsinfo->ls_miscinfo);

		/* Tidy up, but don't delete the lsinfo struct until
		   all the users have closed their devices */
		list_del(&lsinfo->ls_list);
		set_bit(1, &lsinfo->ls_flags); /* LS has been deleted */
		lsinfo->ls_lockspace = NULL;
	}
	up(&user_ls_lock);
}

static struct file_operations _dlm_fops = {
      .open    = dlm_open,
      .release = dlm_close,
      .ioctl   = dlm_ioctl,
      .read    = dlm_read,
      .write   = dlm_write,
      .poll    = dlm_poll,
      .owner   = THIS_MODULE,
};

static struct file_operations _dlm_ctl_fops = {
      .open    = dlm_ctl_open,
      .release = dlm_ctl_close,
      .ioctl   = dlm_ctl_ioctl,
      .owner   = THIS_MODULE,
};

/*
 * Create control device
 */
int dlm_device_init(void)
{
	int r;

	INIT_LIST_HEAD(&user_ls_list);
	init_MUTEX(&user_ls_lock);

	ctl_device.name = "dlm-control";
	ctl_device.fops = &_dlm_ctl_fops;
	ctl_device.minor = MISC_DYNAMIC_MINOR;

	r = misc_register(&ctl_device);
	if (r) {
		log_print("misc_register failed for DLM control device");
		return r;
	}

	return 0;
}

void dlm_device_exit(void)
{
	misc_deregister(&ctl_device);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
