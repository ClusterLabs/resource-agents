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
#include "recoverd.h"
#include "ast.h"
#include "lkb.h"
#include "nodes.h"
#include "dir.h"
#include "lowcomms.h"
#include "config.h"
#include "memory.h"
#include "lockspace.h"
#include "device.h"

#define GDST_NONE       (0)
#define GDST_RUNNING    (1)

static int dlmstate;
static int dlmcount;
static struct semaphore dlmstate_lock;
struct list_head lslist;
spinlock_t lslist_lock;
struct kcl_service_ops ls_ops;

static int new_lockspace(char *name, int namelen, void **lockspace, int flags);


void dlm_lockspace_init(void)
{
	dlmstate = GDST_NONE;
	dlmcount = 0;
	init_MUTEX(&dlmstate_lock);
	INIT_LIST_HEAD(&lslist);
	spin_lock_init(&lslist_lock);
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
	struct dlm_ls *ls;

	spin_lock(&lslist_lock);

	list_for_each_entry(ls, &lslist, ls_list) {
		if (ls->ls_local_id == (uint32_t)(long)id) {
			ls->ls_count++;
			goto out;
		}
	}
	ls = NULL;
      out:
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

struct dlm_ls *allocate_ls(int namelen)
{
	struct dlm_ls *ls;

	ls = kmalloc(sizeof(struct dlm_ls) + namelen, GFP_KERNEL);
	if (ls)
		memset(ls, 0, sizeof(struct dlm_ls) + namelen);

	return ls;
}

static int new_lockspace(char *name, int namelen, void **lockspace, int flags)
{
	struct dlm_ls *ls;
	int i, size, error = -ENOMEM;
	uint32_t local_id = 0;

	if (namelen > MAX_SERVICE_NAME_LEN)
		return -EINVAL;

	if (!try_module_get(THIS_MODULE))
		return -EINVAL;

	ls = find_lockspace_by_name(name, namelen);
	if (ls) {
		*lockspace = (void *)(long) ls->ls_local_id;
		module_put(THIS_MODULE);
		return -EEXIST;
	}

	/*
	 * Initialize ls fields
	 */

	ls = allocate_ls(namelen);
	if (!ls)
		goto out;

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

	/*
	 * Connect this lockspace with the cluster manager
	 */

	error = kcl_register_service(name, namelen, SERVICE_LEVEL_GDLM,
				     &ls_ops, TRUE, (void *) ls, &local_id);
	if (error)
		goto out_recoverd;

	ls->ls_state = LSST_INIT;
	ls->ls_local_id = local_id;

	spin_lock(&lslist_lock);
	list_add(&ls->ls_list, &lslist);
	spin_unlock(&lslist_lock);

	error = kcl_join_service(local_id);
	if (error) {
		log_error(ls, "service manager join error %d", error);
		goto out_reg;
	}

	/* The ls isn't actually running until it receives a start() from CMAN.
	   Neither does it have a global ls id until started. */

	/* Return the local ID as the lockspace handle. I've left this
	   cast to a void* as it allows us to replace it with pretty much
	   anything at a future date without breaking clients. But returning
	   the address of the lockspace is a bad idea as it could get
	   forcibly removed, leaving client with a dangling pointer */

	*lockspace = (void *)(long) local_id;
	return 0;

 out_reg:
	kcl_unregister_service(ls->ls_local_id);
 out_recoverd:
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
	int i;
	int busy = lockspace_busy(ls);

	/* Don't destroy a busy lockspace */
	if (busy > force)
		return -EBUSY;

	if (force < 3) {
		kcl_leave_service(ls->ls_local_id);
		kcl_unregister_service(ls->ls_local_id);
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

	ls_nodes_clear(ls);
	ls_nodes_gone_clear(ls);
	if (ls->ls_node_array)
		kfree(ls->ls_node_array);

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


/* Called when the cluster is being shut down dirtily */
void dlm_emergency_shutdown()
{
	struct dlm_ls *ls;
	struct dlm_ls *tmp;

	/* Shut lowcomms down to prevent any socket activity */
	lowcomms_stop_accept();

	/* Delete the devices that belong the the userland
	   lockspaces to be deleted. */
	dlm_device_free_devices();

	/* Now try to clean the lockspaces */
	spin_lock(&lslist_lock);

	list_for_each_entry_safe(ls, tmp, &lslist, ls_list) {
		spin_unlock(&lslist_lock);
		release_lockspace(ls, 3);
		spin_lock(&lslist_lock);
	}

	spin_unlock(&lslist_lock);
}

struct dlm_recover *allocate_dlm_recover(void)
{
	struct dlm_recover *rv;

	rv = kmalloc(sizeof(struct dlm_recover), GFP_KERNEL);
	if (rv)
		memset(rv, 0, sizeof(struct dlm_recover));
	return rv;
}

/*
 * Called by CMAN on a specific ls.  "stop" means set flag which while set
 * causes all new requests to ls to be queued and not submitted until flag is
 * cleared.  stop on a ls also needs to cancel any prior starts on the ls.
 * The recoverd thread carries out any work called for by this event.
 */

static int dlm_ls_stop(void *servicedata)
{
	struct dlm_ls *ls = (struct dlm_ls *) servicedata;
	int new;

	spin_lock(&ls->ls_recover_lock);
	ls->ls_last_stop = ls->ls_last_start;
	set_bit(LSFL_LS_STOP, &ls->ls_flags);
	new = test_and_clear_bit(LSFL_LS_RUN, &ls->ls_flags);
	spin_unlock(&ls->ls_recover_lock);

	/*
	 * This in_recovery lock does two things:
	 *
	 * 1) Keeps this function from returning until all threads are out
	 *    of locking routines and locking is truely stopped.
	 * 2) Keeps any new requests from being processed until it's unlocked
	 *    when recovery is complete.
	 */

	if (new)
		down_write(&ls->ls_in_recovery);

	/*
	 * The recoverd suspend/resume makes sure that dlm_recoverd (if
	 * running) has noticed the clearing of LS_RUN above and quit
	 * processing the previous recovery.  This will be true for all nodes
	 * before any nodes get the start.
	 */

	dlm_recoverd_suspend(ls);
	clear_bit(LSFL_RESDIR_VALID, &ls->ls_flags);
	clear_bit(LSFL_ALL_RESDIR_VALID, &ls->ls_flags);
	clear_bit(LSFL_NODES_VALID, &ls->ls_flags);
	clear_bit(LSFL_ALL_NODES_VALID, &ls->ls_flags);
	dlm_recoverd_resume(ls);
	dlm_recoverd_kick(ls);

	return 0;
}

/*
 * Called by CMAN on a specific ls.  "start" means enable the lockspace to do
 * request processing which first requires that the recovery procedure be
 * stepped through with all nodes sharing the lockspace (nodeids).  The first
 * start on the ls after it's created is a special case and requires some extra
 * work like figuring out our own local nodeid.  We can't do all this in the
 * calling CMAN context, so we must pass this work off to the recoverd thread
 * which was created in dlm_init().  The recoverd thread carries out any work
 * called for by this event.
 */

static int dlm_ls_start(void *servicedata, uint32_t *nodeids, int count,
			int event_id, int type)
{
	struct dlm_ls *ls = (struct dlm_ls *) servicedata;
	struct dlm_recover *rv;
	int error = -ENOMEM;

	rv = allocate_dlm_recover();
	if (!rv)
		goto out;

	rv->nodeids = nodeids;
	rv->node_count = count;
	rv->event_id = event_id;

	spin_lock(&ls->ls_recover_lock);
	if (ls->ls_last_start == event_id)
		log_debug(ls, "repeated start %d stop %d finish %d",
			  event_id, ls->ls_last_stop, ls->ls_last_finish);
	ls->ls_last_start = event_id;
	list_add_tail(&rv->list, &ls->ls_recover);
	set_bit(LSFL_LS_START, &ls->ls_flags);
	spin_unlock(&ls->ls_recover_lock);

	dlm_recoverd_kick(ls);
	error = 0;

      out:
	return error;
}

/*
 * Called by CMAN on a specific ls.  "finish" means that all nodes which
 * received a "start" have completed the start and called kcl_start_done.
 * The recoverd thread carries out any work called for by this event.
 */

static void dlm_ls_finish(void *servicedata, int event_id)
{
	struct dlm_ls *ls = (struct dlm_ls *) servicedata;

	spin_lock(&ls->ls_recover_lock);
	ls->ls_last_finish = event_id;
	set_bit(LSFL_LS_FINISH, &ls->ls_flags);
	spin_unlock(&ls->ls_recover_lock);

	dlm_recoverd_kick(ls);
}

struct kcl_service_ops ls_ops = {
	.stop = dlm_ls_stop,
	.start = dlm_ls_start,
	.finish = dlm_ls_finish
};
