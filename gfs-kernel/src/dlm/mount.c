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
	printk("lock_dlm: init_fence error %d\n", error);
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
			unsigned int min_lvb_size,
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

	release_mountgroup(dlm);
	release_async_thread(dlm);
	release_gdlm(dlm);
	release_fence(dlm);
	release_cluster(dlm);
	clear_null_cache(dlm);
	kfree(dlm);
}

struct lm_lockops lock_dlm_ops = {
	lm_proto_name:"lock_dlm",
	lm_mount:lm_dlm_mount,
	lm_others_may_mount:lm_dlm_others_may_mount,
	lm_unmount:lm_dlm_unmount,
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
