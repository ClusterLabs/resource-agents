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


static dlm_t *init_lock_dlm(lm_callback_t cb, lm_fsdata_t *fsdata, int flags,
			    char *table_name)
{
	dlm_t *dlm;
	char buf[256], *p;

	dlm = kmalloc(sizeof(dlm_t), GFP_KERNEL);
	if (!dlm)
		return NULL;

	memset(dlm, 0, sizeof(dlm_t));

	dlm->drop_locks_count = lock_dlm_drop_count;
	dlm->drop_locks_period = lock_dlm_drop_period;

	dlm->fscb = cb;
	dlm->fsdata = fsdata;
	dlm->fsflags = flags;

	spin_lock_init(&dlm->async_lock);

	INIT_LIST_HEAD(&dlm->complete);
	INIT_LIST_HEAD(&dlm->blocking);
	INIT_LIST_HEAD(&dlm->delayed);
	INIT_LIST_HEAD(&dlm->submit);
	INIT_LIST_HEAD(&dlm->all_locks);
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

	dlm->jid = -1;

	strncpy(buf, table_name, 256);
	buf[255] = '\0';

	p = strstr(buf, ":");
	if (!p) {
		printk("lock_dlm: invalid table_name \"%s\"\n", table_name);
		kfree(dlm);
		return NULL;
	}
	*p = '\0';
	p++;

	strncpy(dlm->clustername, buf, 128);
	strncpy(dlm->fsname, p, 128);

	return dlm;
}

static int lm_dlm_mount(char *table_name, char *host_data,
			lm_callback_t cb, lm_fsdata_t *fsdata,
			unsigned int min_lvb_size, int flags,
			struct lm_lockstruct *lockstruct)
{
	dlm_t *dlm;
	int error = -ENOMEM;

	if (min_lvb_size > DLM_LVB_SIZE)
		goto out;

	dlm = init_lock_dlm(cb, fsdata, flags, table_name);
	if (!dlm)
		goto out;

	error = init_async_thread(dlm);
	if (error)
		goto out_free;

	error = dlm_new_lockspace(dlm->fsname, strlen(dlm->fsname),
				  &dlm->gdlm_lsp, 0, DLM_LVB_SIZE);
	if (error) {
		printk("lock_dlm: dlm_new_lockspace error %d\n", error);
		goto out_thread;
	}

	error = lm_dlm_kobject_setup(dlm);
	if (error)
		goto out_dlm;
	kobject_uevent(&dlm->kobj, KOBJ_MOUNT, NULL);

	/* Now we depend on userspace to notice the new mount,
	   join the appropriate group, and do a write to our sysfs
	   "mounted" or "terminate" file.  Before the start, userspace
	   must set "jid" and "first". */

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
	dlm_release_lockspace(dlm->gdlm_lsp, 2);
 out_thread:
	release_async_thread(dlm);
 out_free:
	kfree(dlm);
 out:
	return error;
}

static void lm_dlm_unmount(lm_lockspace_t *lockspace)
{
	dlm_t *dlm = (dlm_t *) lockspace;
	int rv;

	log_debug("unmount flags %lx", dlm->flags);

	if (test_bit(DFL_WITHDRAW, &dlm->flags)) {
		lm_dlm_kobject_release(dlm);
		goto out;
	}

	kobject_uevent(&dlm->kobj, KOBJ_UMOUNT, NULL);

	wait_event_interruptible(dlm->wait_control,
				 test_bit(DFL_LEAVE_DONE, &dlm->flags));

	lm_dlm_kobject_release(dlm);
	dlm_release_lockspace(dlm->gdlm_lsp, 2);
	release_async_thread(dlm);
	rv = release_all_locks(dlm);
	if (rv)
		log_all("lm_dlm_unmount: %d stray locks freed", rv);
 out:
	kfree(dlm);
}

static void lm_dlm_recovery_done(lm_lockspace_t *lockspace, unsigned int jid,
                                 unsigned int message)
{
	dlm_t *dlm = (dlm_t *) lockspace;
	dlm->recover_done = jid;
	kobject_uevent(&dlm->kobj, KOBJ_CHANGE, NULL);
}

static void lm_dlm_others_may_mount(lm_lockspace_t *lockspace)
{
	dlm_t *dlm = (dlm_t *) lockspace;
	dlm->first_done = 1;
	kobject_uevent(&dlm->kobj, KOBJ_CHANGE, NULL);
}

static void lm_dlm_withdraw(lm_lockspace_t *lockspace)
{
	dlm_t *dlm = (dlm_t *) lockspace;

	/* userspace suspends locking on all other members */

	kobject_uevent(&dlm->kobj, KOBJ_OFFLINE, NULL);

	wait_event_interruptible(dlm->wait_control,
				 test_bit(DFL_WITHDRAW, &dlm->flags));

	dlm_release_lockspace(dlm->gdlm_lsp, 2);
	release_async_thread(dlm);
	release_all_locks(dlm);

	kobject_uevent(&dlm->kobj, KOBJ_UMOUNT, NULL);

	/* userspace leaves the mount group, we don't need to wait for
	   that to complete */
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

