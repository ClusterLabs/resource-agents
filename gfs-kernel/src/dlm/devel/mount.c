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

#include "lock_dlm.h"

extern int lock_dlm_drop_count;
extern int lock_dlm_drop_period;

/*  
 * Parse superblock lock table <clustername>:<fsname>  
 * FIXME: simplify this
 */

static int init_names(dlm_t *dlm, char *table_name)
{
	char *buf, *c, *clname, *fsname;
	int len, error = -1;

	len = strlen(table_name) + 1;
	buf = kmalloc(len, GFP_KERNEL);
	if (!buf)
		goto out;
	memset(buf, 0, len);
	memcpy(buf, table_name, strlen(table_name));

	c = strstr(buf, ":");
	if (!c)
		goto out_buf;

	*c = '\0';
	clname = buf;
	fsname = ++c;

	len = strlen(clname) + 1;
	c = kmalloc(len, GFP_KERNEL);
	if (!c)
		goto out_buf;
	memset(c, 0, len);
	memcpy(c, clname, len-1);
	dlm->cnlen = len-1;
	dlm->clustername = c;

	len = strlen(fsname) + 1;
	c = kmalloc(len, GFP_KERNEL);
	if (!c)
		goto out_cn;
	memset(c, 0, len);
	memcpy(c, fsname, len-1);
	dlm->fnlen = len-1;
	dlm->fsname = c;

	kfree(buf);
	return 0;

 out_cn:
	kfree(dlm->clustername);
 out_buf:
	kfree(buf);
 out:
	printk("lock_dlm: init_names error %d\n", error);
	return error;
}

static int release_names(dlm_t *dlm)
{
	kfree(dlm->clustername);
	kfree(dlm->fsname);
	return 0;
}

static int init_dlm(dlm_t *dlm)
{
	int error;

	error = dlm_new_lockspace(dlm->fsname, dlm->fnlen, &dlm->gdlm_lsp, 0);
	if (error)
		printk("lock_dlm: new lockspace error %d\n", error);

	return error;
}

static int release_dlm(dlm_t *dlm)
{
	dlm_release_lockspace(dlm->gdlm_lsp, 2);
	return 0;
}

static dlm_t *init_lock_dlm(lm_callback_t cb, lm_fsdata_t *fsdata)
{
	dlm_t *dlm;

	dlm = kmalloc(sizeof(dlm_t), GFP_KERNEL);
	if (!dlm)
		return NULL;

	memset(dlm, 0, sizeof(dlm_t));

	dlm->drop_locks_count = lock_dlm_drop_count;
	dlm->drop_locks_period = lock_dlm_drop_period;

	dlm->fscb = cb;
	dlm->fsdata = fsdata;

	spin_lock_init(&dlm->async_lock);

	INIT_LIST_HEAD(&dlm->complete);
	INIT_LIST_HEAD(&dlm->blocking);
	INIT_LIST_HEAD(&dlm->delayed);
	INIT_LIST_HEAD(&dlm->submit);
	INIT_LIST_HEAD(&dlm->resources);
	INIT_LIST_HEAD(&dlm->null_cache);

	init_waitqueue_head(&dlm->wait);
	init_waitqueue_head(&dlm->wait_control);
	dlm->thread1 = NULL;
	dlm->thread2 = NULL;
	atomic_set(&dlm->lock_count, 0);
	dlm->drop_time = jiffies;
	dlm->shrink_time = jiffies;

	init_MUTEX(&dlm->res_lock);

	dlm->null_count = 0;
	spin_lock_init(&dlm->null_cache_spin);

	return dlm;
}

/**
 * dlm_mount - mount a dlm lockspace
 * @table_name: the name of the space to mount
 * @host_data: host specific data
 * @cb: the callback
 * @lockstruct: the structure of crap to fill in
 *
 * Returns: 0 on success, -EXXX on failure
 */

static int lm_dlm_mount(char *table_name, char *host_data,
			lm_callback_t cb, lm_fsdata_t *fsdata,
			unsigned int min_lvb_size, int flags,
			struct lm_lockstruct *lockstruct)
{
	dlm_t *dlm;
	int error = -ENOMEM;

	if (min_lvb_size > DLM_LVB_SIZE)
		goto out;

	dlm = init_lock_dlm(cb, fsdata);
	if (!dlm)
		goto out;

	error = init_names(dlm, table_name);
	if (error)
		goto out_free;

	error = init_async_thread(dlm);
	if (error)
		goto out_names;

	error = init_dlm(dlm);
	if (error)
		goto out_thread;

	error = lm_dlm_kobject_setup(dlm);
	if (error)
		goto out_dlm;

	/* Now we depend on userspace to notice the new mg, join the
	   appropriate group, and give us a mounted or terminate.

	   Before the start, userspace sets:
	   dlm->jid, dlm->first */

	error = wait_event_interruptible(dlm->wait_control,
			test_bit(DFL_JOIN_DONE, &dlm->flags));

	if (error)
		goto out_sysfs;

	if (test_bit(DFL_TERMINATE, &dlm->flags)) {
		error = -ERESTARTSYS;
		goto out_sysfs;
	}

	lockstruct->ls_jid = dlm->jid;
	lockstruct->ls_first = dlm->first;
	lockstruct->ls_lockspace = dlm;
	lockstruct->ls_ops = &lock_dlm_ops;
	lockstruct->ls_flags = 0;
	lockstruct->ls_lvb_size = DLM_LVB_SIZE;
	return 0;

 out_sysfs:
	lm_dlm_kobject_release(dlm);
 out_dlm:
	release_dlm(dlm);
 out_thread:
	release_async_thread(dlm);
 out_names:
	release_names(dlm);
 out_free:
	kfree(dlm);
 out:
	return error;
}

static void lm_dlm_unmount(lm_lockspace_t *lockspace)
{
	dlm_t *dlm = (dlm_t *) lockspace;
	int error;

	log_debug("unmount flags %lx", dlm->flags);

	error = kobject_uevent(&dlm->kobj, KOBJ_OFFLINE, NULL);

	error = wait_event_interruptible(dlm->wait_control,
			test_bit(DFL_LEAVE_DONE, &dlm->flags));

	lm_dlm_kobject_release(dlm);
	release_dlm(dlm);
	release_async_thread(dlm);
	release_names(dlm);
	/* clear_null_cache(dlm); */
	kfree(dlm);
}

static void lm_dlm_recovery_done(lm_lockspace_t *lockspace, unsigned int jid,
                                 unsigned int message)
{
	dlm_t *dlm = (dlm_t *) lockspace;
	int error;

	dlm->recover_done = jid;

	error = kobject_uevent(&dlm->kobj, KOBJ_CHANGE, NULL);
	if (error)
		printk("lock_dlm: kobject_uevent error %d", error);
}

static void lm_dlm_others_may_mount(lm_lockspace_t *lockspace)
{
	/* Do nothing.  Nodes are added to the mount group one
	   at a time and complete their mount before others are added */
}

static void lm_dlm_withdraw(lm_lockspace_t *lockspace)
{
}

int lm_dlm_plock_get(lm_lockspace_t *lockspace, struct lm_lockname *name,
		                     struct file *file, struct file_lock *fl)
{
	return 0;
}

int lm_dlm_punlock(lm_lockspace_t *lockspace, struct lm_lockname *name,
		                   struct file *file, struct file_lock *fl)
{
	return 0;
}

int lm_dlm_plock(lm_lockspace_t *lockspace, struct lm_lockname *name,
	struct file *file, int cmd, struct file_lock *fl)
{
	return 0;
}

struct lm_lockops lock_dlm_ops = {
	lm_proto_name:"lock_dlm",
	lm_mount:lm_dlm_mount,
	lm_others_may_mount:lm_dlm_others_may_mount,
	lm_unmount:lm_dlm_unmount,
	lm_withdraw:lm_dlm_withdraw,
	lm_get_lock:lm_dlm_get_lock,
	lm_put_lock:lm_dlm_put_lock,
	lm_lock:lm_dlm_lock,
	lm_unlock:lm_dlm_unlock,
	lm_plock:lm_dlm_plock,
	lm_punlock:lm_dlm_punlock,
	lm_plock_get:lm_dlm_plock_get,
	lm_cancel:lm_dlm_cancel,
	lm_hold_lvb:lm_dlm_hold_lvb,
	lm_unhold_lvb:lm_dlm_unhold_lvb,
	lm_sync_lvb:lm_dlm_sync_lvb,
	lm_recovery_done:lm_dlm_recovery_done,
	lm_owner:THIS_MODULE,
};
