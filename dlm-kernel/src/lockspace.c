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

static int gdlmstate;
static int gdlmcount;
static struct semaphore gdlmstate_lock;
struct list_head lslist;
spinlock_t lslist_lock;
struct kcl_service_ops ls_ops;

static int new_lockspace(char *name, int namelen, void **lockspace, int flags);


void dlm_lockspace_init(void)
{
	gdlmstate = GDST_NONE;
	gdlmcount = 0;
	init_MUTEX(&gdlmstate_lock);
	INIT_LIST_HEAD(&lslist);
	spin_lock_init(&lslist_lock);
}

gd_ls_t *find_lockspace_by_global_id(uint32_t id)
{
	gd_ls_t *ls;

	spin_lock(&lslist_lock);

	list_for_each_entry(ls, &lslist, ls_list) {
		if (ls->ls_global_id == id)
			goto out;
	}
	ls = NULL;
      out:
	spin_unlock(&lslist_lock);
	return ls;
}

/* TODO: make this more efficient */
gd_ls_t *find_lockspace_by_local_id(void *id)
{
	gd_ls_t *ls;

	spin_lock(&lslist_lock);

	list_for_each_entry(ls, &lslist, ls_list) {
		if (ls->ls_local_id == (uint32_t)(long)id)
			goto out;
	}
	ls = NULL;
      out:
	spin_unlock(&lslist_lock);
	return ls;
}

gd_ls_t *find_lockspace_by_name(char *name, int namelen)
{
	gd_ls_t *ls;

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

/*
 * Called from dlm_init.  These are the general threads which are not
 * lockspace-specific and work for all gdlm lockspaces.
 */

static int threads_start(void)
{
	int error;

	/* Thread which interacts with cman for all ls's */
	error = recoverd_start();
	if (error) {
		log_print("cannot start recovery thread %d", error);
		goto fail;
	}

	/* Thread which process lock requests for all ls's */
	error = astd_start();
	if (error) {
		log_print("cannot start ast thread %d", error);
		goto recoverd_fail;
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

      recoverd_fail:
	recoverd_stop();

      fail:
	return error;
}

static void threads_stop(void)
{
	lowcomms_stop();
	astd_stop();
	recoverd_stop();
}

static int init_internal(void)
{
	int error = 0;

	if (gdlmstate == GDST_RUNNING)
		gdlmcount++;
	else {
		error = threads_start();
		if (error)
			goto out;

		gdlmstate = GDST_RUNNING;
		gdlmcount = 1;
	}

      out:
	return error;
}


/*
 * Called after gdlm module is loaded and before any lockspaces are created.
 * Starts and initializes global threads and structures.  These global entities
 * are shared by and independent of all lockspaces.
 *
 * There should be a gdlm-specific user command which a person can run which
 * calls this function.  If a user hasn't run that command and something
 * creates a new lockspace, this is called first.
 *
 * This also starts the default lockspace.
 */

int dlm_init(void)
{
	int error;

	down(&gdlmstate_lock);
	error = init_internal();
	up(&gdlmstate_lock);

	return error;
}

int dlm_release(void)
{
	int error = 0;

	down(&gdlmstate_lock);

	if (gdlmstate == GDST_NONE)
		goto out;

	if (gdlmcount)
		gdlmcount--;

	if (gdlmcount)
		goto out;

	spin_lock(&lslist_lock);
	if (!list_empty(&lslist)) {
		spin_unlock(&lslist_lock);
		log_print("cannot stop threads, lockspaces still exist");
		goto out;
	}
	spin_unlock(&lslist_lock);

	threads_stop();
	gdlmstate = GDST_NONE;

      out:
	up(&gdlmstate_lock);

	return error;
}

gd_ls_t *allocate_ls(int namelen)
{
	gd_ls_t *ls;

	/* FIXME: use appropriate malloc type */

	ls = kmalloc(sizeof(gd_ls_t) + namelen, GFP_KERNEL);
	if (ls)
		memset(ls, 0, sizeof(gd_ls_t) + namelen);

	return ls;
}

void free_ls(gd_ls_t *ls)
{
	kfree(ls);
}

static int new_lockspace(char *name, int namelen, void **lockspace, int flags)
{
	gd_ls_t *ls;
	int i, error = -ENOMEM;
	uint32_t local_id = 0;

	if (!try_module_get(THIS_MODULE))
		return -EINVAL;

	if (namelen > MAX_SERVICE_NAME_LEN)
		return -EINVAL;

	if ((ls = find_lockspace_by_name(name, namelen))) {
		*lockspace = (void *)(long)ls->ls_local_id;
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
	memset(&ls->ls_flags, 0, sizeof(unsigned long));
	INIT_LIST_HEAD(&ls->ls_rootres);
	ls->ls_hashsize = dlm_config.reshashtbl;
	ls->ls_hashmask = ls->ls_hashsize - 1;

	ls->ls_reshashtbl =
	    kmalloc(sizeof(struct list_head) * ls->ls_hashsize, GFP_KERNEL);
	if (!ls->ls_reshashtbl)
		goto out_lsfree;

	for (i = 0; i < ls->ls_hashsize; i++)
		INIT_LIST_HEAD(&ls->ls_reshashtbl[i]);

	rwlock_init(&ls->ls_reshash_lock);

	if (init_lockidtbl(ls, dlm_config.lockidtbl) == -1)
		goto out_htfree;

	INIT_LIST_HEAD(&ls->ls_nodes);
	ls->ls_num_nodes = 0;
	INIT_LIST_HEAD(&ls->ls_nodes_gone);
	INIT_LIST_HEAD(&ls->ls_recover);
	spin_lock_init(&ls->ls_recover_lock);
	INIT_LIST_HEAD(&ls->ls_recover_list);
	ls->ls_recover_list_count = 0;
	spin_lock_init(&ls->ls_recover_list_lock);
	init_waitqueue_head(&ls->ls_wait_general);
	INIT_LIST_HEAD(&ls->ls_requestqueue);
	INIT_LIST_HEAD(&ls->ls_rebuild_rootrsb_list);
	ls->ls_last_stop = 0;
	ls->ls_last_start = 0;
	ls->ls_last_finish = 0;
	ls->ls_rcom_msgid = 0;
	init_MUTEX(&ls->ls_rcom_lock);
	init_rwsem(&ls->ls_in_recovery);
	init_rwsem(&ls->ls_unlock_sem);
	init_rwsem(&ls->ls_rec_rsblist);
	init_rwsem(&ls->ls_gap_rsblist);
	down_write(&ls->ls_in_recovery);

	for (i = 0; i < RESDIRHASH_SIZE; i++) {
		INIT_LIST_HEAD(&ls->ls_resdir_hash[i].rb_reslist);
		rwlock_init(&ls->ls_resdir_hash[i].rb_lock);
	}

	if (flags & DLM_LSF_NOTIMERS)
		set_bit(LSFL_NOTIMERS, &ls->ls_flags);

	/*
	 * Connect this lockspace with the cluster manager
	 */

	error = kcl_register_service(name, namelen, SERVICE_LEVEL_GDLM,
				     &ls_ops, TRUE, (void *) ls, &local_id);
	if (error)
		goto out_idtblfree;

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
	 * Neither does it have a global ls id until started. */


	/* Return the local ID as the lockspace handle. I've left this
	   cast to a void* as it allows us to replace it with pretty much
	   anything at a future date without breaking clients. But returning
	   the address of the lockspace is a bad idea as it could get
	   forcibly removed, leaving client with a dangling pointer */
	*lockspace = (void *)(long)local_id;

	return 0;

      out_reg:
	kcl_unregister_service(ls->ls_local_id);

      out_idtblfree:
	free_lockidtbl(ls);

      out_htfree:
	kfree(ls->ls_reshashtbl);

      out_lsfree:
	free_ls(ls);

      out:
	return error;
}

/*
 * Called by a system like GFS which wants independent lock spaces.
 */

int dlm_new_lockspace(char *name, int namelen, void **lockspace, int flags)
{
	int error = -ENOSYS;

	down(&gdlmstate_lock);

	error = init_internal();
	if (error)
		goto out;

	error = new_lockspace(name, namelen, lockspace, flags);

      out:
	up(&gdlmstate_lock);

	return error;
}

/* Return 1 if the lockspace still has active remote locks,
 *        2 if the lockspace still has active local locks.
 */
static int lockspace_busy(gd_ls_t *ls)
{
    int i;
    int lkb_found = 0;
    gd_lkb_t *lkb;

    /* NOTE: We check the lockidtbl here rather than the resource table.
     * This is because there may be LKBs queued as ASTs that have been unlinked
     * from their RSBs and are pending deletion once the AST has been delivered
     */
    read_lock(&ls->ls_lockidtbl_lock);
    for (i = 0; i < ls->ls_lockidtbl_size; i++) {
	if (!list_empty(&ls->ls_lockidtbl[i].list)) {
	    lkb_found = 1;
	    list_for_each_entry(lkb, &ls->ls_lockidtbl[i].list, lkb_idtbl_list) {
		if (!lkb->lkb_nodeid) {
		    read_unlock(&ls->ls_lockidtbl_lock);
		    return 2;
		}
	    }
	}
    }
    read_unlock(&ls->ls_lockidtbl_lock);
    return lkb_found;
}

/* Actually release the lockspace */
static int release_lockspace(gd_ls_t *ls, int force)
{
	gd_lkb_t *lkb;
	gd_res_t *rsb;
	gd_recover_t *gr;
	gd_csb_t *csb;
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

	spin_lock(&lslist_lock);
	list_del(&ls->ls_list);
	spin_unlock(&lslist_lock);

	/*
	 * Free resdata structs.
	 */

	resdir_clear(ls);

	/*
	 * Free all lkb's on lockidtbl[] lists.
	 */

	for (i = 0; i < ls->ls_lockidtbl_size; i++) {
		head = &ls->ls_lockidtbl[i].list;
		while (!list_empty(head)) {
			lkb = list_entry(head->next, gd_lkb_t, lkb_idtbl_list);
			list_del(&lkb->lkb_idtbl_list);

			if (lkb->lkb_lockqueue_state)
				remove_from_lockqueue(lkb);

			if (lkb->lkb_astflags & (AST_COMP | AST_BAST))
				list_del(&lkb->lkb_astqueue);

			if (lkb->lkb_lvbptr
			    && lkb->lkb_flags & GDLM_LKFLG_MSTCPY)
				free_lvb(lkb->lkb_lvbptr);

			free_lkb(lkb);
		}
	}

	/*
	 * Free lkidtbl[] itself
	 */

	kfree(ls->ls_lockidtbl);

	/*
	 * Free all rsb's on reshashtbl[] lists
	 */

	for (i = 0; i < ls->ls_hashsize; i++) {
		head = &ls->ls_reshashtbl[i];
		while (!list_empty(head)) {
			rsb = list_entry(head->next, gd_res_t, res_hashchain);
			list_del(&rsb->res_hashchain);

			if (rsb->res_lvbptr)
				free_lvb(rsb->res_lvbptr);

			free_rsb(rsb);
		}
	}

	/*
	 * Free reshashtbl[] itself
	 */

	kfree(ls->ls_reshashtbl);

	/*
	 * Free structures on any other lists
	 */

	head = &ls->ls_recover;
	while (!list_empty(head)) {
		gr = list_entry(head->next, gd_recover_t, gr_list);
		list_del(&gr->gr_list);
		free_dlm_recover(gr);
	}

	head = &ls->ls_nodes;
	while (!list_empty(head)) {
		csb = list_entry(head->next, gd_csb_t, csb_list);
		list_del(&csb->csb_list);
		release_csb(csb);
	}

	head = &ls->ls_nodes_gone;
	while (!list_empty(head)) {
		csb = list_entry(head->next, gd_csb_t, csb_list);
		list_del(&csb->csb_list);
		release_csb(csb);
	}

	free_ls(ls);

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
	gd_ls_t *ls;

	ls = find_lockspace_by_local_id(lockspace);
	if (!ls)
	    return -EINVAL;

	return release_lockspace(ls, force);
}


/* Called when the cluster is being shut down dirtily */
void dlm_emergency_shutdown()
{
	gd_ls_t *ls;
	gd_ls_t *tmp;

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

gd_recover_t *allocate_dlm_recover(void)
{
	gd_recover_t *gr;

	gr = (gd_recover_t *) kmalloc(sizeof(gd_recover_t), GFP_KERNEL);
	if (gr)
		memset(gr, 0, sizeof(gd_recover_t));

	return gr;
}

void free_dlm_recover(gd_recover_t * gr)
{
	kfree(gr);
}

/*
 * Called by CMAN on a specific ls.  "stop" means set flag which while set
 * causes all new requests to ls to be queued and not submitted until flag is
 * cleared.  stop on a ls also needs to cancel any prior starts on the ls.
 * The recoverd thread carries out any work called for by this event.
 */

static int dlm_ls_stop(void *servicedata)
{
	gd_ls_t *ls = (gd_ls_t *) servicedata;
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

	clear_bit(LSFL_RESDIR_VALID, &ls->ls_flags);
	clear_bit(LSFL_ALL_RESDIR_VALID, &ls->ls_flags);
	clear_bit(LSFL_NODES_VALID, &ls->ls_flags);
	clear_bit(LSFL_ALL_NODES_VALID, &ls->ls_flags);

	recoverd_kick(ls);

	return 0;
}

/*
 * Called by CMAN on a specific ls.  "start" means enable the lockspace to do
 * request processing which first requires that the recovery procedure be
 * stepped through with all nodes sharing the lockspace (nodeids).  The first
 * start on the ls after it's created is a special case and requires some extra
 * work like figuring out our own local nodeid.  We can't do all this in the
 * calling CMAN context, so we must pass this work off to the recoverd thread
 * which was created in gdlm_init().  The recoverd thread carries out any work
 * called for by this event.
 */

static int dlm_ls_start(void *servicedata, uint32_t *nodeids, int count,
			int event_id, int type)
{
	gd_ls_t *ls = (gd_ls_t *) servicedata;
	gd_recover_t *gr;
	int error = -ENOMEM;

	gr = allocate_dlm_recover();
	if (!gr)
		goto out;

	gr->gr_nodeids = nodeids;
	gr->gr_node_count = count;
	gr->gr_event_id = event_id;

	spin_lock(&ls->ls_recover_lock);
	ls->ls_last_start = event_id;
	list_add_tail(&gr->gr_list, &ls->ls_recover);
	set_bit(LSFL_LS_START, &ls->ls_flags);
	spin_unlock(&ls->ls_recover_lock);

	recoverd_kick(ls);
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
	gd_ls_t *ls = (gd_ls_t *) servicedata;

	spin_lock(&ls->ls_recover_lock);
	ls->ls_last_finish = event_id;
	set_bit(LSFL_LS_FINISH, &ls->ls_flags);
	spin_unlock(&ls->ls_recover_lock);

	recoverd_kick(ls);
}

struct kcl_service_ops ls_ops = {
	.stop = dlm_ls_stop,
	.start = dlm_ls_start,
	.finish = dlm_ls_finish
};
