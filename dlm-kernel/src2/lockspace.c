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

#include <linux/module.h>

#include "dlm_internal.h"
#include "lockspace.h"
#include "member.h"
#include "recoverd.h"
#include "ast.h"
#include "lkb.h"
#include "dir.h"
#include "lowcomms.h"
#include "config.h"
#include "memory.h"
#include "device.h"

#define GDST_NONE       (0)
#define GDST_RUNNING    (1)

static int dlmstate;
static int dlmcount;
static struct semaphore dlmstate_lock;
struct list_head lslist;
spinlock_t lslist_lock;

static int new_lockspace(char *name, int namelen, void **lockspace, int flags);


static struct kset dlm_kset = {
	.subsys = &kernel_subsys,
	.kobj = {.name = "dlm",},
};

int dlm_lockspace_init(void)
{
	int error;

	dlmstate = GDST_NONE;
	dlmcount = 0;
	init_MUTEX(&dlmstate_lock);
	INIT_LIST_HEAD(&lslist);
	spin_lock_init(&lslist_lock);

	error = kset_register(&dlm_kset);
	if (error)
		printk("dlm_lockspace_init: cannot register kset %d\n", error);

	return error;
}

void dlm_lockspace_exit(void)
{
	kset_unregister(&dlm_kset);
}

struct dlm_ls *find_lockspace_by_name(char *name, int namelen)
{
	struct dlm_ls *ls;

	spin_lock(&lslist_lock);

	list_for_each_entry(ls, &lslist, ls_list) {
		if (ls->ls_namelen == namelen &&
		    memcmp(ls->ls_name, name, namelen) == 0)
			goto out;
	}
	ls = NULL;
 out:
	spin_unlock(&lslist_lock);
	return ls;
}

struct dlm_ls *find_lockspace_by_global_id(uint32_t id)
{
	struct dlm_ls *ls;

	spin_lock(&lslist_lock);

	list_for_each_entry(ls, &lslist, ls_list) {
		if (ls->ls_global_id == id) {
			ls->ls_count++;
			goto out;
		}
	}
	ls = NULL;
 out:
	spin_unlock(&lslist_lock);
	return ls;
}

struct dlm_ls *find_lockspace_by_local_id(void *id)
{
	struct dlm_ls *ls = id;

	spin_lock(&lslist_lock);
	ls->ls_count++;
	spin_unlock(&lslist_lock);
	return ls;
}

/* must be called with lslist_lock held */
void hold_lockspace(struct dlm_ls *ls)
{
	ls->ls_count++;
}

void put_lockspace(struct dlm_ls *ls)
{
	spin_lock(&lslist_lock);
	ls->ls_count--;
	spin_unlock(&lslist_lock);
}

static void remove_lockspace(struct dlm_ls *ls)
{
	for (;;) {
		spin_lock(&lslist_lock);
		if (ls->ls_count == 0) {
			list_del(&ls->ls_list);
			spin_unlock(&lslist_lock);
			return;
		}
		spin_unlock(&lslist_lock);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);
	}
}

/*
 * Called from dlm_init.  These are the general threads which are not
 * lockspace-specific and work for all dlm lockspaces.
 */

static int threads_start(void)
{
	int error;

	/* Thread which process lock requests for all ls's */
	error = astd_start();
	if (error) {
		log_print("cannot start ast thread %d", error);
		goto fail;
	}

	/* Thread for sending/receiving messages for all ls's */
	error = lowcomms_start();
	if (error) {
		log_print("cannot start lowcomms %d", error);
		goto astd_fail;
	}

	return 0;

 astd_fail:
	astd_stop();
 fail:
	return error;
}

static void threads_stop(void)
{
	lowcomms_stop();
	astd_stop();
}

static int init_internal(void)
{
	int error = 0;

	if (dlmstate == GDST_RUNNING)
		dlmcount++;
	else {
		error = threads_start();
		if (error)
			goto out;

		dlmstate = GDST_RUNNING;
		dlmcount = 1;
	}
  out:
	return error;
}

/*
 * Called after dlm module is loaded and before any lockspaces are created.
 * Starts and initializes global threads and structures.  These global entities
 * are shared by and independent of all lockspaces.
 *
 * There should be a dlm-specific user command which a person can run which
 * calls this function.  If a user hasn't run that command and something
 * creates a new lockspace, this is called first.
 *
 * This also starts the default lockspace.
 */

int dlm_init(void)
{
	int error;

	down(&dlmstate_lock);
	error = init_internal();
	up(&dlmstate_lock);

	return error;
}

int dlm_release(void)
{
	int error = 0;

	down(&dlmstate_lock);

	if (dlmstate == GDST_NONE)
		goto out;

	if (dlmcount)
		dlmcount--;

	if (dlmcount)
		goto out;

	spin_lock(&lslist_lock);
	if (!list_empty(&lslist)) {
		spin_unlock(&lslist_lock);
		log_print("cannot stop threads, lockspaces still exist");
		goto out;
	}
	spin_unlock(&lslist_lock);

	threads_stop();
	dlmstate = GDST_NONE;
 out:
	up(&dlmstate_lock);
	return error;
}

static int new_lockspace(char *name, int namelen, void **lockspace, int flags)
{
	struct dlm_ls *ls;
	int i, size, error = -ENOMEM;

	if (namelen > DLM_LOCKSPACE_LEN)
		return -EINVAL;

	if (!try_module_get(THIS_MODULE))
		return -EINVAL;

	ls = find_lockspace_by_name(name, namelen);
	if (ls) {
		*lockspace = ls;
		module_put(THIS_MODULE);
		return -EEXIST;
	}

	ls = kmalloc(sizeof(struct dlm_ls) + namelen, GFP_KERNEL);
	if (!ls)
		goto out;
	memset(ls, 0, sizeof(struct dlm_ls) + namelen);
	memcpy(ls->ls_name, name, namelen);
	ls->ls_namelen = namelen;
	ls->ls_allocation = GFP_KERNEL;
	ls->ls_count = 0;
	ls->ls_flags = 0;

	size = dlm_config.rsbtbl_size;
	ls->ls_rsbtbl_size = size;

	ls->ls_rsbtbl = kmalloc(sizeof(struct dlm_rsbtable) * size, GFP_KERNEL);
	if (!ls->ls_rsbtbl)
		goto out_lsfree;
	for (i = 0; i < size; i++) {
		INIT_LIST_HEAD(&ls->ls_rsbtbl[i].list);
		rwlock_init(&ls->ls_rsbtbl[i].lock);
	}

	size = dlm_config.lkbtbl_size;
	ls->ls_lkbtbl_size = size;

	ls->ls_lkbtbl = kmalloc(sizeof(struct dlm_lkbtable) * size, GFP_KERNEL);
	if (!ls->ls_lkbtbl)
		goto out_rsbfree;
	for (i = 0; i < size; i++) {
		INIT_LIST_HEAD(&ls->ls_lkbtbl[i].list);
		rwlock_init(&ls->ls_lkbtbl[i].lock);
		ls->ls_lkbtbl[i].counter = 1;
	}

	size = dlm_config.dirtbl_size;
	ls->ls_dirtbl_size = size;

	ls->ls_dirtbl = kmalloc(sizeof(struct dlm_dirtable) * size, GFP_KERNEL);
	if (!ls->ls_dirtbl)
		goto out_lkbfree;
	for (i = 0; i < size; i++) {
		INIT_LIST_HEAD(&ls->ls_dirtbl[i].list);
		rwlock_init(&ls->ls_dirtbl[i].lock);
	}

	init_waitqueue_head(&ls->ls_wait_member);
	INIT_LIST_HEAD(&ls->ls_nodes);
	INIT_LIST_HEAD(&ls->ls_nodes_gone);
	ls->ls_num_nodes = 0;
	ls->ls_node_array = NULL;
	ls->ls_recoverd_task = NULL;
	init_MUTEX(&ls->ls_recoverd_active);
	INIT_LIST_HEAD(&ls->ls_recover);
	spin_lock_init(&ls->ls_recover_lock);
	INIT_LIST_HEAD(&ls->ls_recover_list);
	ls->ls_recover_list_count = 0;
	spin_lock_init(&ls->ls_recover_list_lock);
	init_waitqueue_head(&ls->ls_wait_general);
	INIT_LIST_HEAD(&ls->ls_rootres);
	INIT_LIST_HEAD(&ls->ls_requestqueue);
	INIT_LIST_HEAD(&ls->ls_rebuild_rootrsb_list);
	ls->ls_last_stop = 0;
	ls->ls_last_start = 0;
	ls->ls_last_finish = 0;
	ls->ls_rcom_msgid = 0;
	init_MUTEX(&ls->ls_requestqueue_lock);
	init_MUTEX(&ls->ls_rcom_lock);
	init_rwsem(&ls->ls_unlock_sem);
	init_rwsem(&ls->ls_root_lock);
	init_rwsem(&ls->ls_in_recovery);

	down_write(&ls->ls_in_recovery);

	if (flags & DLM_LSF_NOTIMERS)
		set_bit(LSFL_NOTIMERS, &ls->ls_flags);

	error = dlm_recoverd_start(ls);
	if (error) {
		log_error(ls, "can't start dlm_recoverd %d", error);
		goto out_dirfree;
	}

	ls->ls_state = LSST_INIT;

	spin_lock(&lslist_lock);
	list_add(&ls->ls_list, &lslist);
	spin_unlock(&lslist_lock);

	error = kobject_set_name(&ls->ls_kobj, ls->ls_name);
	if (error)
		goto out_del;
	ls->ls_kobj.kset = &dlm_kset;
	error = kobject_register(&ls->ls_kobj);
	if (error)
		goto out_del;

	error = kobject_uevent(&ls->ls_kobj, KOBJ_ONLINE, NULL);
	if (error)
		log_error(ls, "kobject_uevent error %d", error);

	/* Now we depend on userspace to notice the new ls, join it and
	   give us a start or terminate.  The ls isn't actually running
	   until it receives a start. */

	error = wait_event_interruptible(ls->ls_wait_member,
				test_bit(LSFL_JOIN_DONE, &ls->ls_flags));
	if (error)
		goto out_unreg;

	if (test_bit(LSFL_LS_TERMINATE, &ls->ls_flags)) {
		error = -ERESTARTSYS;
		goto out_unreg;
	}

	*lockspace = ls;
	return 0;

 out_unreg:
	kobject_unregister(&ls->ls_kobj);
 out_del:
	spin_lock(&lslist_lock);
	list_del(&ls->ls_list);
	spin_unlock(&lslist_lock);
	dlm_recoverd_stop(ls);
 out_dirfree:
	kfree(ls->ls_dirtbl);
 out_lkbfree:
	kfree(ls->ls_lkbtbl);
 out_rsbfree:
	kfree(ls->ls_rsbtbl);
 out_lsfree:
	kfree(ls);
 out:
	module_put(THIS_MODULE);
	return error;
}

/*
 * Called by a system like GFS which wants independent lock spaces.
 */

int dlm_new_lockspace(char *name, int namelen, void **lockspace, int flags)
{
	int error = -ENOSYS;

	down(&dlmstate_lock);
	error = init_internal();
	if (error)
		goto out;

	error = new_lockspace(name, namelen, lockspace, flags);
 out:
	up(&dlmstate_lock);
	return error;
}

/* Return 1 if the lockspace still has active remote locks,
 *        2 if the lockspace still has active local locks.
 */
static int lockspace_busy(struct dlm_ls *ls)
{
	int i, lkb_found = 0;
	struct dlm_lkb *lkb;

	/* NOTE: We check the lockidtbl here rather than the resource table.
	   This is because there may be LKBs queued as ASTs that have been
	   unlinked from their RSBs and are pending deletion once the AST has
	   been delivered */

	for (i = 0; i < ls->ls_lkbtbl_size; i++) {
		read_lock(&ls->ls_lkbtbl[i].lock);
		if (!list_empty(&ls->ls_lkbtbl[i].list)) {
			lkb_found = 1;
			list_for_each_entry(lkb, &ls->ls_lkbtbl[i].list,
					    lkb_idtbl_list) {
				if (!lkb->lkb_nodeid) {
					read_unlock(&ls->ls_lkbtbl[i].lock);
					return 2;
				}
			}
		}
		read_unlock(&ls->ls_lkbtbl[i].lock);
	}
	return lkb_found;
}

static int release_lockspace(struct dlm_ls *ls, int force)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *rsb;
	struct dlm_recover *rv;
	struct list_head *head;
	int i, error;
	int busy = lockspace_busy(ls);

	/* Don't destroy a busy lockspace */
	if (busy > force)
		return -EBUSY;

	if (force < 3) {
		error = kobject_uevent(&ls->ls_kobj, KOBJ_OFFLINE, NULL);
		error = wait_event_interruptible(ls->ls_wait_member,
				test_bit(LSFL_LEAVE_DONE, &ls->ls_flags));
	}


	dlm_recoverd_stop(ls);

	remove_lockspace(ls);

	/*
	 * Free direntry structs.
	 */

	dlm_dir_clear(ls);
	kfree(ls->ls_dirtbl);

	/*
	 * Free all lkb's on lkbtbl[] lists.
	 */

	astd_suspend();

	for (i = 0; i < ls->ls_lkbtbl_size; i++) {
		head = &ls->ls_lkbtbl[i].list;
		while (!list_empty(head)) {
			lkb = list_entry(head->next, struct dlm_lkb,
					 lkb_idtbl_list);
			list_del(&lkb->lkb_idtbl_list);

			if (lkb->lkb_lockqueue_state)
				remove_from_lockqueue(lkb);

			remove_from_astqueue(lkb);

			if (lkb->lkb_lvbptr && lkb->lkb_flags & GDLM_LKFLG_MSTCPY)
				free_lvb(lkb->lkb_lvbptr);

			free_lkb(lkb);
		}
	}
	astd_resume();

	kfree(ls->ls_lkbtbl);

	/*
	 * Free all rsb's on rsbtbl[] lists
	 */

	for (i = 0; i < ls->ls_rsbtbl_size; i++) {
		head = &ls->ls_rsbtbl[i].list;
		while (!list_empty(head)) {
			rsb = list_entry(head->next, struct dlm_rsb,
					 res_hashchain);
			list_del(&rsb->res_hashchain);

			if (rsb->res_lvbptr)
				free_lvb(rsb->res_lvbptr);

			free_rsb(rsb);
		}
	}

	kfree(ls->ls_rsbtbl);

	/*
	 * Free structures on any other lists
	 */

	head = &ls->ls_recover;
	while (!list_empty(head)) {
		rv = list_entry(head->next, struct dlm_recover, list);
		list_del(&rv->list);
		kfree(rv);
	}

	clear_free_de(ls);

	dlm_clear_members(ls);
	dlm_clear_members_gone(ls);
	if (ls->ls_node_array)
		kfree(ls->ls_node_array);

	kobject_unregister(&ls->ls_kobj);
	kfree(ls);
	dlm_release();
	module_put(THIS_MODULE);
	return 0;
}


/*
 * Called when a system has released all its locks and is not going to use the
 * lockspace any longer.  We blindly free everything we're managing for this
 * lockspace.  Remaining nodes will go through the recovery process as if we'd
 * died.  The lockspace must continue to function as usual, participating in
 * recoveries, until kcl_leave_service returns.
 *
 * Force has 4 possible values:
 * 0 - don't destroy locksapce if it has any LKBs
 * 1 - destroy lockspace if it has remote LKBs but not if it has local LKBs
 * 2 - destroy lockspace regardless of LKBs
 * 3 - destroy lockspace as part of a forced shutdown
 */

int dlm_release_lockspace(void *lockspace, int force)
{
	struct dlm_ls *ls;

	ls = find_lockspace_by_local_id(lockspace);
	if (!ls)
		return -EINVAL;
	put_lockspace(ls);
	return release_lockspace(ls, force);
}
