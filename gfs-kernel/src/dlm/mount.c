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

#include <linux/socket.h>
#include <net/sock.h>
#include <linux/delay.h>

#include "lock_dlm.h"
#include <cluster/cnxman.h>
#include <cluster/service.h>

extern int lock_dlm_max_nodes;
extern int lock_dlm_drop_count;
extern int lock_dlm_drop_period;


static int init_cman(dlm_t *dlm)
{
	int error = -1;
	char *name = NULL;

	if (!dlm->clustername)
		goto fail;

	error = kcl_addref_cluster();
	if (error) {
		printk("lock_dlm: cannot get cman reference %d\n", error);
		goto fail;
	}

	error = kcl_cluster_name(&name);
	if (error) {
		printk("lock_dlm: cannot get cman cluster name %d\n", error);
		goto fail_ref;
	}

	if (strcmp(name, dlm->clustername)) {
		error = -1;
		printk("lock_dlm: cman cluster name \"%s\" does not match "
		       "file system cluster name \"%s\"\n",
		       name, dlm->clustername);
		goto fail_ref;
	}

	kfree(name);
	return 0;

 fail_ref:
	kcl_releaseref_cluster();
 fail:
	if (name)
		kfree(name);
	return error;
}

static int release_cman(dlm_t *dlm)
{
	return kcl_releaseref_cluster();
}

static int init_cluster(dlm_t *dlm, char *table_name)
{
	char *buf, *c, *clname, *fsname;
	int len, error = -1;

	/*  
	 * Parse superblock lock table <clustername>:<fsname>  
	 */

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

	dlm->max_nodes = lock_dlm_max_nodes;

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

	error = init_cman(dlm);
	if (error)
		goto out_fn;

	kfree(buf);
	return 0;

 out_fn:
	kfree(dlm->fsname);
 out_cn:
	kfree(dlm->clustername);
 out_buf:
	kfree(buf);
 out:
	printk("lock_dlm: init_cluster error %d\n", error);
	return error;
}

static int release_cluster(dlm_t *dlm)
{
	release_cman(dlm);
	kfree(dlm->clustername);
	kfree(dlm->fsname);
	return 0;
}

static int init_fence(dlm_t *dlm)
{
	LIST_HEAD(head);
	struct kcl_service *s, *safe;
	int error, found = FALSE;

	error = kcl_get_services(&head, SERVICE_LEVEL_FENCE);
	if (error < 0)
		goto out;

	list_for_each_entry_safe(s, safe, &head, list) {
		list_del(&s->list);
		if (!found && !strcmp(s->name, "default"))
			found = TRUE;
		kfree(s);
	}

	if (found)
		return 0;

	error = -1;
 out:
	printk("lock_dlm: fence domain not found; check fenced\n");
	return error;
}

static int release_fence(dlm_t *dlm)
{
	return 0;
}

static int init_gdlm(dlm_t *dlm)
{
	int error;

	error = dlm_new_lockspace(dlm->fsname, dlm->fnlen, &dlm->gdlm_lsp,
				   DLM_LSF_NOTIMERS);
	if (error)
		printk("lock_dlm: new lockspace error %d\n", error);

	return error;
}

static int release_gdlm(dlm_t *dlm)
{
	dlm_release_lockspace(dlm->gdlm_lsp, 2);
	return 0;
}

static dlm_t *init_dlm(lm_callback_t cb, lm_fsdata_t *fsdata)
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
	INIT_LIST_HEAD(&dlm->starts);
	INIT_LIST_HEAD(&dlm->resources);
	INIT_LIST_HEAD(&dlm->null_cache);

	init_waitqueue_head(&dlm->wait);
	dlm->thread1 = NULL;
	dlm->thread2 = NULL;
	atomic_set(&dlm->lock_count, 0);
	dlm->drop_time = jiffies;
	dlm->shrink_time = jiffies;

	INIT_LIST_HEAD(&dlm->mg_nodes);
	init_MUTEX(&dlm->mg_nodes_lock);
	init_MUTEX(&dlm->unmount_lock);
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

	dlm = init_dlm(cb, fsdata);
	if (!dlm)
		goto out;

	error = init_cluster(dlm, table_name);
	if (error)
		goto out_free;

	error = init_fence(dlm);
	if (error)
		goto out_cluster;

	error = init_gdlm(dlm);
	if (error)
		goto out_fence;

	error = init_async_thread(dlm);
	if (error)
		goto out_gdlm;

	error = init_mountgroup(dlm);
	if (error)
		goto out_thread;

	lockstruct->ls_jid = dlm->jid;
	lockstruct->ls_first = test_bit(DFL_FIRST_MOUNT, &dlm->flags);
	lockstruct->ls_lockspace = dlm;
	lockstruct->ls_ops = &lock_dlm_ops;
	lockstruct->ls_flags = 0;
	lockstruct->ls_lvb_size = DLM_LVB_SIZE;
	return 0;

 out_thread:
	release_async_thread(dlm);
 out_gdlm:
	release_gdlm(dlm);
 out_fence:
	release_fence(dlm);
 out_cluster:
	release_cluster(dlm);
 out_free:
	kfree(dlm);
 out:
	return error;
}

/**
 * dlm_others_may_mount
 * @lockspace: the lockspace to unmount
 *
 */

static void lm_dlm_others_may_mount(lm_lockspace_t *lockspace)
{
	/* Do nothing.  The first node to join the Mount Group will complete
	 * before Service Manager allows another node to join. */
}

/**
 * dlm_unmount - unmount a lock space
 * @lockspace: the lockspace to unmount
 *
 */

static void lm_dlm_unmount(lm_lockspace_t *lockspace)
{
	dlm_t *dlm = (dlm_t *) lockspace;

	log_debug("unmount flags %lx", dlm->flags);
	if (test_bit(DFL_WITHDRAW, &dlm->flags))
		goto out;
	release_mountgroup(dlm);
	release_async_thread(dlm);
	release_gdlm(dlm);
	release_fence(dlm);
	release_cluster(dlm);
	clear_null_cache(dlm);
 out:
	kfree(dlm);
}

static void wd_ast(void *arg)
{
	dlm_lock_t *lp = (dlm_lock_t *) arg;
	complete(&lp->uast_wait);
}

static void wd_bast(void *arg, int mode)
{
	dlm_lock_t *lp = (dlm_lock_t *) arg;
	dlm_t *dlm = lp->dlm;
	dlm_node_t *node;
	int error;

	if (lp->cur == DLM_LOCK_NL) {
		log_all("withdraw bast cur NL arg %d", mode);
		return;
	}

	if (lp->cur != DLM_LOCK_PR) {
		log_all("withdraw bast cur %d arg %d", lp->cur, mode);
		return;
	}

	if (mode != DLM_LOCK_EX) {
		log_all("withdraw bast cur %d arg %d", lp->cur, mode);
		return;
	}

	set_bit(DFL_BLOCK_LOCKS, &dlm->flags);

	down(&dlm->mg_nodes_lock);
	list_for_each_entry(node, &dlm->mg_nodes, list) {
		if (node->withdraw_lp == lp) {
			log_debug("wd_bast node %d withdraw", node->nodeid);
			set_bit(NFL_WITHDRAW, &node->flags);
			break;
		}
	}
	up(&dlm->mg_nodes_lock);

	set_bit(LFL_UNLOCK_DELETE, &lp->flags);

	error = dlm_unlock(dlm->gdlm_lsp, lp->lksb.sb_lkid, 0, NULL, lp);

	DLM_ASSERT(!error, printk("error %d\n", error););

	node->withdraw_lp = NULL;
}

void lm_dlm_hold_withdraw(dlm_t *dlm)
{
	char name[16];
	dlm_node_t *node;
	dlm_lock_t *lp;
	int error;

	down(&dlm->mg_nodes_lock);
	list_for_each_entry(node, &dlm->mg_nodes, list) {
		if (test_bit(NFL_WITHDRAW, &node->flags))
			continue;

		lp = node->withdraw_lp;

		/* if we have the lp it should always be in PR */
		if (lp) {
			if (lp->cur != DLM_LOCK_PR)
				log_all("hold_withdraw cur %d", lp->cur);
			continue;
		}

		lp = kmalloc(sizeof(dlm_lock_t), GFP_KERNEL);
		if (!lp)
			continue;
		memset(lp, 0, sizeof(dlm_lock_t));
		init_completion(&lp->uast_wait);
		lp->dlm = dlm;
		node->withdraw_lp = lp;

		memset(name, 0, sizeof(name));
		snprintf(name, sizeof(name), "withdraw %u", node->nodeid);

		error = dlm_lock(dlm->gdlm_lsp, DLM_LOCK_PR, &lp->lksb,
				 DLM_LKF_NOQUEUE, name, sizeof(name), 0,
				 wd_ast, (void *) lp, wd_bast, NULL);

		DLM_ASSERT(!error, printk("error %d\n", error););

		wait_for_completion(&lp->uast_wait);

		DLM_ASSERT(lp->lksb.sb_status == 0,
			   printk("status %d\n", lp->lksb.sb_status););

		lp->cur = DLM_LOCK_PR;
	}
	up(&dlm->mg_nodes_lock);
}

static void do_withdraw(dlm_t *dlm)
{
	char name[16];
	dlm_node_t *node;
	dlm_lock_t *lp;
	int error;

	down(&dlm->mg_nodes_lock);
	list_for_each_entry(node, &dlm->mg_nodes, list) {
		if (node->nodeid == dlm->our_nodeid)
			break;
	}
	up(&dlm->mg_nodes_lock);

	if (!node) {
		log_all("node not found for %d", dlm->our_nodeid);
		return;
	}

	lp = node->withdraw_lp;
	if (!lp) {
		log_all("no withdraw lock for self");
		return;
	}

	if (lp->cur != DLM_LOCK_PR) {
		log_all("our withdraw lock in mode %d", lp->cur);
		return;
	}

	log_debug("do_withdraw");
	memset(name, 0, sizeof(name));
	snprintf(name, sizeof(name), "withdraw %u", dlm->our_nodeid);

	error = dlm_lock(dlm->gdlm_lsp, DLM_LOCK_EX, &lp->lksb,
			 DLM_LKF_CONVERT, name, sizeof(name), 0, wd_ast,
			 (void *) lp, wd_bast, NULL);

	DLM_ASSERT(!error, printk("error %d\n", error););

	wait_for_completion(&lp->uast_wait);

	DLM_ASSERT(lp->lksb.sb_status == 0,
		   printk("status %d\n", lp->lksb.sb_status););

	lp->cur = DLM_LOCK_EX;
}

/* Release the withdraw lock for this node.  If the node was removed because it
   withdrew, then we already released the lock (in wd_bast).  If the node has
   failed or unmounted, then we still hold its withdraw lock and need to unlock
   it.  If _we're_ withdrawing, then we've already left the lockspace so we
   don't unlock, just free. */

void lm_dlm_release_withdraw(dlm_t *dlm, dlm_node_t *node)
{
	dlm_lock_t *lp;
	int error;

	lp = node->withdraw_lp;
	if (!lp)
		return;

	if (test_bit(DFL_WITHDRAW, &dlm->flags)) {
		kfree(lp);
		goto out;
	}

	/* the lp is freed by the async thread when it gets the comp ast */
	set_bit(LFL_UNLOCK_DELETE, &lp->flags);

	error = dlm_unlock(dlm->gdlm_lsp, lp->lksb.sb_lkid, 0, NULL, lp);

	DLM_ASSERT(!error, printk("error %d\n", error););

 out:
	node->withdraw_lp = NULL;
}

/**
 * dlm_withdraw - withdraw from a lock space
 * @lockspace: the lockspace to withdraw from
 *
 * Holding the withdraw lock in EX means all gfs locks are blocked on other
 * nodes and we can safely leave the lockspace.
 *
 */

static void lm_dlm_withdraw(lm_lockspace_t *lockspace)
{
	dlm_t *dlm = (dlm_t *) lockspace;

	log_debug("withdraw flags %lx", dlm->flags);
	set_bit(DFL_WITHDRAW, &dlm->flags);

	/* process_start uses the dlm so leaving the ls while it's running
	   can hang it; this waits for it to complete. */
	down(&dlm->unmount_lock);
	up(&dlm->unmount_lock);

	do_withdraw(dlm);
	release_gdlm(dlm);
	release_mountgroup(dlm);
	release_cluster(dlm);

	/* FIXME: free all outstanding memory */
	log_all("withdraw abandoned memory");
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
