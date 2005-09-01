/*
 * Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#include "lock_dlm.h"
#include "lock_dlm_plock.h"

#include <linux/miscdevice.h>

static spinlock_t ops_lock;
static struct list_head send_list;
static struct list_head recv_list;
static wait_queue_head_t send_wq;
static wait_queue_head_t recv_wq;

struct plock_op {
	struct list_head list;
	int done;
	struct gdlm_plock_info info;
};

static inline void set_version(struct gdlm_plock_info *info)
{
	info->version[0] = GDLM_PLOCK_VERSION_MAJOR;
	info->version[1] = GDLM_PLOCK_VERSION_MINOR;
	info->version[2] = GDLM_PLOCK_VERSION_PATCH;
}

static int check_version(struct gdlm_plock_info *info)
{
	if ((GDLM_PLOCK_VERSION_MAJOR != info->version[0]) ||
	    (GDLM_PLOCK_VERSION_MINOR < info->version[1])) {
		log_error("plock device version mismatch: "
			  "kernel (%u.%u.%u), user (%u.%u.%u)",
			  GDLM_PLOCK_VERSION_MAJOR,
			  GDLM_PLOCK_VERSION_MINOR,
			  GDLM_PLOCK_VERSION_PATCH,
			  info->version[0],
			  info->version[1],
			  info->version[2]);
		return -EINVAL;
	}
	return 0;
}

int gdlm_plock(lm_lockspace_t *lockspace, struct lm_lockname *name,
	       struct file *file, int cmd, struct file_lock *fl)
{
	struct gdlm_ls *ls = (struct gdlm_ls *) lockspace;
	struct plock_op *op;
	int rv;

	op = kzalloc(sizeof(*op), GFP_KERNEL);
	if (!op)
		return -ENOMEM;

	log_debug("en plock %x,%"PRIx64"", name->ln_type, name->ln_number);

	set_version(&op->info);
	op->info.optype		= GDLM_PLOCK_OP_LOCK;
	op->info.pid		= (uint32_t) fl->fl_owner;
	op->info.ex		= (fl->fl_type == F_WRLCK);
	op->info.wait		= IS_SETLKW(cmd);
	op->info.fsid		= ls->id;
	op->info.number		= name->ln_number;
	op->info.start		= fl->fl_start;
	op->info.end		= fl->fl_end;

	INIT_LIST_HEAD(&op->list);
	spin_lock(&ops_lock);
	list_add_tail(&op->list, &send_list);
	spin_unlock(&ops_lock);
	wake_up(&send_wq);

	wait_event(recv_wq, (op->done != 0));

	spin_lock(&ops_lock);
	if (!list_empty(&op->list)) {
		printk("plock op on list\n");
		list_del(&op->list);
	}
	spin_unlock(&ops_lock);

	log_debug("ex plock done %d rv %d", op->done, op->info.rv);

	rv = op->info.rv;

	if (!rv) {
		if (posix_lock_file_wait(file, fl) < 0)
			log_error("gdlm_plock: vfs lock error %x,%"PRIx64"",
				  name->ln_type, name->ln_number);
	}

	kfree(op);
	return rv;
}

int gdlm_punlock(lm_lockspace_t *lockspace, struct lm_lockname *name,
		 struct file *file, struct file_lock *fl)
{
	struct gdlm_ls *ls = (struct gdlm_ls *) lockspace;
	struct plock_op *op;
	int rv;

	op = kzalloc(sizeof(*op), GFP_KERNEL);
	if (!op)
		return -ENOMEM;

	log_debug("en punlock %x,%"PRIx64"", name->ln_type, name->ln_number);

	if (posix_lock_file_wait(file, fl) < 0)
		log_error("gdlm_punlock: vfs unlock error %x,%"PRIx64"",
			  name->ln_type, name->ln_number);

	set_version(&op->info);
	op->info.optype		= GDLM_PLOCK_OP_UNLOCK;
	op->info.pid		= (uint32_t) fl->fl_owner;
	op->info.fsid		= ls->id;
	op->info.number		= name->ln_number;
	op->info.start		= fl->fl_start;
	op->info.end		= fl->fl_end;

	INIT_LIST_HEAD(&op->list);
	spin_lock(&ops_lock);
	list_add_tail(&op->list, &send_list);
	spin_unlock(&ops_lock);
	wake_up(&send_wq);

	wait_event(recv_wq, (op->done != 0));

	spin_lock(&ops_lock);
	if (!list_empty(&op->list)) {
		printk("plock op on list\n");
		list_del(&op->list);
	}
	spin_unlock(&ops_lock);

	log_debug("ex punlock done %d rv %d", op->done, op->info.rv);

	rv = op->info.rv;

	kfree(op);
	return rv;
}

int gdlm_plock_get(lm_lockspace_t *lockspace, struct lm_lockname *name,
		   struct file *file, struct file_lock *fl)
{
	return -ENOSYS;
}

/* a read copies out one plock request from the send list */
static ssize_t dev_read(struct file *file, char __user *u, size_t count,
			loff_t *ppos)
{
	struct gdlm_plock_info info;
	struct plock_op *op = NULL;

	if (count < sizeof(info))
		return -EINVAL;

	spin_lock(&ops_lock);
	if (!list_empty(&send_list)) {
		op = list_entry(send_list.next, struct plock_op, list);
		list_move(&op->list, &recv_list);
		memcpy(&info, &op->info, sizeof(info));
	}
	spin_unlock(&ops_lock);

	if (!op)
		return -EAGAIN;

	log_debug("send %"PRIx64" op %d ex %d wait %d", info.number,
		  info.optype, info.ex, info.wait);

	if (copy_to_user(u, &info, sizeof(info)))
		return -EFAULT;
	return sizeof(info);
}

/* a write copies in one plock result that should match a plock_op
   on the recv list */
static ssize_t dev_write(struct file *file, const char __user *u, size_t count,
			 loff_t *ppos)
{
	struct gdlm_plock_info info;
	struct plock_op *op;
	int found = 0;

	if (count != sizeof(info))
		return -EINVAL;

	if (copy_from_user(&info, u, sizeof(info)))
		return -EFAULT;

	if (check_version(&info))
		return -EINVAL;

	log_debug("recv %"PRIx64" op %d ex %d wait %d", info.number,
		  info.optype, info.ex, info.wait);

	spin_lock(&ops_lock);
	list_for_each_entry(op, &recv_list, list) {
		if (op->info.fsid == info.fsid &&
		    op->info.number == info.number) {
			list_del_init(&op->list);
			found = 1;
			op->done = 1;
			memcpy(&op->info, &info, sizeof(info));
			break;
		}
	}
	spin_unlock(&ops_lock);

	if (found)
		wake_up(&recv_wq);
	else
		printk("gdlm dev_write no op %x %"PRIx64"\n", info.fsid,
			info.number);
	return count;
}

static unsigned int dev_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &send_wq, wait);

	spin_lock(&ops_lock);
	if (!list_empty(&send_list)) {
		spin_unlock(&ops_lock);
		return POLLIN | POLLRDNORM;
	}
	spin_unlock(&ops_lock);
	return 0;
}

static struct file_operations dev_fops = {
	.read    = dev_read,
	.write   = dev_write,
	.poll    = dev_poll,
	.owner   = THIS_MODULE
};

static struct miscdevice plock_dev_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = GDLM_PLOCK_MISC_NAME,
	.fops = &dev_fops
};

int gdlm_plock_init(void)
{
	int rv;

	spin_lock_init(&ops_lock);
	INIT_LIST_HEAD(&send_list);
	INIT_LIST_HEAD(&recv_list);
	init_waitqueue_head(&send_wq);
	init_waitqueue_head(&recv_wq);

	rv = misc_register(&plock_dev_misc);
	if (rv)
		printk("gdlm_plock_init: misc_register failed %d", rv);
	return rv;
}

void gdlm_plock_exit(void)
{
	if (misc_deregister(&plock_dev_misc) < 0)
		printk("gdlm_plock_exit: misc_deregister failed");
}

