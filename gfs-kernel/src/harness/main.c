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
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/kmod.h>
#include <linux/fs.h>
#include <linux/lm_interface.h>

#define RELEASE_NAME "<CVS>"

struct lmh_wrapper {
	struct list_head lw_list;
	struct lm_lockops *lw_ops;
};

static struct semaphore lmh_lock;
static struct list_head lmh_list;

/**
 * lm_register_proto - Register a low-level locking protocol
 * @proto: the protocol definition
 *
 * Returns: 0 on success, -EXXX on failure
 */

int
lm_register_proto(struct lm_lockops *proto)
{
	struct list_head *tmp, *head;
	struct lmh_wrapper *lw;

	down(&lmh_lock);

	for (head = &lmh_list, tmp = head->next; tmp != head; tmp = tmp->next) {
		lw = list_entry(tmp, struct lmh_wrapper, lw_list);

		if (strcmp(lw->lw_ops->lm_proto_name, proto->lm_proto_name) == 0) {
			up(&lmh_lock);
			printk("lock_harness:  protocol %s already exists\n",
			       proto->lm_proto_name);
			return -EEXIST;
		}
	}

	lw = kmalloc(sizeof (struct lmh_wrapper), GFP_KERNEL);
	if (!lw) {
		up(&lmh_lock);
		return -ENOMEM;
	}
	memset(lw, 0, sizeof (struct lmh_wrapper));

	lw->lw_ops = proto;
	list_add(&lw->lw_list, &lmh_list);

	up(&lmh_lock);

	return 0;
}

/**
 * lm_unregister_proto - Unregister a low-level locking protocol
 * @proto: the protocol definition
 *
 */

void
lm_unregister_proto(struct lm_lockops *proto)
{
	struct list_head *tmp, *head;
	struct lmh_wrapper *lw = NULL;

	down(&lmh_lock);

	for (head = &lmh_list, tmp = head->next; tmp != head; tmp = tmp->next) {
		lw = list_entry(tmp, struct lmh_wrapper, lw_list);

		if (strcmp(lw->lw_ops->lm_proto_name, proto->lm_proto_name) == 0) {
			list_del(&lw->lw_list);
			up(&lmh_lock);
			kfree(lw);
			return;
		}
	}

	up(&lmh_lock);

	printk("lock_harness:  can't unregister lock protocol %s\n",
	       proto->lm_proto_name);
}

/**
 * lm_mount - Mount a lock protocol
 * @proto_name - the name of the protocol
 * @table_name - the name of the lock space
 * @host_data - data specific to this host
 * @cb - the callback to the code using the lock module
 * @fsdata - data to pass back with the callback
 * @min_lvb_size - the mininum LVB size that the caller can deal with
 * @lockstruct - a structure returned describing the mount
 *
 * Returns: 0 on success, -EXXX on failure
 */

int
lm_mount(char *proto_name, char *table_name, char *host_data,
	 lm_callback_t cb, lm_fsdata_t * fsdata,
	 unsigned int min_lvb_size, struct lm_lockstruct *lockstruct)
{
	struct list_head *tmp;
	struct lmh_wrapper *lw = NULL;
	int try = 0;
	int error;

      retry:
	down(&lmh_lock);

	for (tmp = lmh_list.next; tmp != &lmh_list; tmp = tmp->next) {
		lw = list_entry(tmp, struct lmh_wrapper, lw_list);

		if (strcmp(lw->lw_ops->lm_proto_name, proto_name) == 0)
			break;
		else
			lw = NULL;
	}

	if (!lw) {
		if (!try && capable(CAP_SYS_MODULE)) {
			try = 1;
			up(&lmh_lock);
			request_module(proto_name);
			goto retry;
		}
		printk("lock_harness:  can't find protocol %s\n", proto_name);
		error = -ENOENT;
		goto out;
	}

	if (!try_module_get(lw->lw_ops->lm_owner)) {
		try = 0;
		up(&lmh_lock);
		current->state = TASK_UNINTERRUPTIBLE;
		schedule_timeout(HZ);
		goto retry;
	}

	error = lw->lw_ops->lm_mount(table_name, host_data,
				     cb, fsdata, min_lvb_size, lockstruct);
	if (error)
		module_put(lw->lw_ops->lm_owner);

      out:
	up(&lmh_lock);

	return error;
}

/**
 * lm_unmount - unmount a lock module
 * @lockstruct: the lockstruct passed into mount
 *
 */

void
lm_unmount(struct lm_lockstruct *lockstruct)
{
	down(&lmh_lock);
	lockstruct->ls_ops->lm_unmount(lockstruct->ls_lockspace);
	if (lockstruct->ls_ops->lm_owner)
		module_put(lockstruct->ls_ops->lm_owner);
	up(&lmh_lock);
}

/**
 * init_lmh - Initialize the lock module harness
 *
 * Returns: 0 on success, -EXXX on failure
 */

int __init
init_lmh(void)
{
	init_MUTEX(&lmh_lock);
	INIT_LIST_HEAD(&lmh_list);

	printk("Lock_Harness %s (built %s %s) installed\n",
	       RELEASE_NAME, __DATE__, __TIME__);

	return 0;
}

/**
 * exit_lmh - cleanup the Lock Module Harness
 *
 * Returns: 0 on success, -EXXX on failure
 */

void __exit
exit_lmh(void)
{
}

module_init(init_lmh);
module_exit(exit_lmh);

MODULE_DESCRIPTION("GFS Lock Module Harness " RELEASE_NAME);
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL_GPL(lm_register_proto);
EXPORT_SYMBOL_GPL(lm_unregister_proto);
EXPORT_SYMBOL_GPL(lm_mount);
EXPORT_SYMBOL_GPL(lm_unmount);
