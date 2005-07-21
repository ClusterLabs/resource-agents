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

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <asm/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <asm/uaccess.h>

#include "gfs2.h"
#include "glock.h"
#include "lm.h"
#include "proc.h"
#include "super.h"

struct list_head gfs2_fs_list;
struct semaphore gfs2_fs_lock;
char *gfs2_proc_margs;
spinlock_t gfs2_proc_margs_lock;
spinlock_t req_lock;

/**
 * gfs2_proc_fs_add - Add a FS to the list of mounted FSs
 * @sdp:
 *
 */

void gfs2_proc_fs_add(struct gfs2_sbd *sdp)
{
	ENTER(G2FN_PROC_FS_ADD)
	down(&gfs2_fs_lock);
	list_add(&sdp->sd_list, &gfs2_fs_list);
	up(&gfs2_fs_lock);
	RET(G2FN_PROC_FS_ADD);
}

/**
 * gfs2_proc_fs_del - Remove a FS from the list of mounted FSs
 * @sdp:
 *
 */

void gfs2_proc_fs_del(struct gfs2_sbd *sdp)
{
	ENTER(G2FN_PROC_FS_DEL)
	down(&gfs2_fs_lock);
	list_del(&sdp->sd_list);
	up(&gfs2_fs_lock);
	RET(G2FN_PROC_FS_DEL);
}

/**
 * do_list - Copy the list of mountes FSs to userspace
 * @user_buf:
 * @size:
 *
 * @Returns: -errno, or the number of bytes copied to userspace
 */

static ssize_t do_list(char *user_buf, size_t size)
{
	ENTER(G2FN_DO_LIST)
	struct list_head *tmp;
	struct gfs2_sbd *sdp = NULL;
	unsigned int x;
	char num[21];
	char *buf;
	int error = 0;

	down(&gfs2_fs_lock);

	x = 0;
	for (tmp = gfs2_fs_list.next; tmp != &gfs2_fs_list; tmp = tmp->next) {
		sdp = list_entry(tmp, struct gfs2_sbd, sd_list);
		x += sprintf(num, "%lu", (unsigned long)sdp) +
			strlen(sdp->sd_vfs->s_id) +
			strlen(sdp->sd_fsname) + 3;
	}

	if (!x)
		goto out;

	error = -EFBIG;
	if (x > size)
		goto out;

	error = -ENOMEM;
	buf = kmalloc(x + 1, GFP_KERNEL);
	if (!buf)
		goto out;

	x = 0;
	for (tmp = gfs2_fs_list.next; tmp != &gfs2_fs_list; tmp = tmp->next) {
		sdp = list_entry(tmp, struct gfs2_sbd, sd_list);
		x += sprintf(buf + x, "%lu %s %s\n",
			     (unsigned long)sdp, sdp->sd_vfs->s_id, sdp->sd_fsname);
	}

	if (copy_to_user(user_buf, buf, x))
		error = -EFAULT;
	else
		error = x;

	kfree(buf);

 out:
	up(&gfs2_fs_lock);

	RETURN(G2FN_DO_LIST, error);
}

/**
 * find_argument - 
 * @p:
 *
 * Returns:
 */

static char *find_argument(char *p)
{
	ENTER(G2FN_FIND_ARGUMENT)
	char *p2;

	while (*p == ' ' || *p == '\n')
		p++;
	if (!*p)
		RETURN(G2FN_FIND_ARGUMENT, NULL);
	for (p2 = p; *p2; p2++) /* do nothing */;
	p2--;
	while (*p2 == ' ' || *p2 == '\n')
		*p2-- = 0;

	RETURN(G2FN_FIND_ARGUMENT, p);
}

/**
 * do_freeze - freeze a filesystem
 * @p: the freeze command
 *
 * Returns: errno
 */

static int do_freeze(char *p)
{
	ENTER(G2FN_DO_FREEZE)
	struct list_head *tmp;
	struct gfs2_sbd *sdp;
	char num[21];
	int error = 0;

	p = find_argument(p + 6);
	if (!p)
		RETURN(G2FN_DO_FREEZE, -ENOENT);

	down(&gfs2_fs_lock);

	for (tmp = gfs2_fs_list.next; tmp != &gfs2_fs_list; tmp = tmp->next) {
		sdp = list_entry(tmp, struct gfs2_sbd, sd_list);
		sprintf(num, "%lu", (unsigned long)sdp);
		if (strcmp(num, p) == 0)
			break;
	}

	if (tmp == &gfs2_fs_list)
		error = -ENOENT;
	else
		error = gfs2_freeze_fs(sdp);

	up(&gfs2_fs_lock);

	RETURN(G2FN_DO_FREEZE, error);
}

/**
 * do_unfreeze - unfreeze a filesystem
 * @p: the unfreeze command
 *
 * Returns: errno
 */

static int do_unfreeze(char *p)
{
	ENTER(G2FN_DO_UNFREEZE)
	struct list_head *tmp;
	struct gfs2_sbd *sdp;
	char num[21];
	int error = 0;

	p = find_argument(p + 8);
	if (!p)
		RETURN(G2FN_DO_UNFREEZE, -ENOENT);

	down(&gfs2_fs_lock);

	for (tmp = gfs2_fs_list.next; tmp != &gfs2_fs_list; tmp = tmp->next) {
		sdp = list_entry(tmp, struct gfs2_sbd, sd_list);
		sprintf(num, "%lu", (unsigned long)sdp);
		if (strcmp(num, p) == 0)
			break;
	}

	if (tmp == &gfs2_fs_list)
		error = -ENOENT;
	else
		gfs2_unfreeze_fs(sdp);

	up(&gfs2_fs_lock);

	RETURN(G2FN_DO_UNFREEZE, error);
}

/**
 * do_margs - Pass in mount arguments
 * @p: the margs command
 *
 * Returns: errno
 */

static int do_margs(char *p)
{
	ENTER(G2FN_DO_MARGS)
	char *new_buf, *old_buf;

	p = find_argument(p + 5);
	if (!p)
		RETURN(G2FN_DO_MARGS, -ENOENT);

	new_buf = kmalloc(strlen(p) + 1, GFP_KERNEL);
	if (!new_buf)
		RETURN(G2FN_DO_MARGS, -ENOMEM);
	strcpy(new_buf, p);

	spin_lock(&gfs2_proc_margs_lock);
	old_buf = gfs2_proc_margs;
	gfs2_proc_margs = new_buf;
	spin_unlock(&gfs2_proc_margs_lock);

	kfree(old_buf);

	RETURN(G2FN_DO_MARGS, 0);
}

/**
 * do_withdraw - withdraw a from the cluster for one filesystem
 * @p: the cookie of the filesystem
 *
 * Returns: errno
 */

static int do_withdraw(char *p)
{
	ENTER(G2FN_DO_WITHDRAW)
	struct list_head *tmp;
	struct gfs2_sbd *sdp;
	char num[21];
	int error = 0;

	p = find_argument(p + 8);
	if (!p)
		RETURN(G2FN_DO_WITHDRAW, -ENOENT);

	down(&gfs2_fs_lock);

	for (tmp = gfs2_fs_list.next; tmp != &gfs2_fs_list; tmp = tmp->next) {
		sdp = list_entry(tmp, struct gfs2_sbd, sd_list);
		sprintf(num, "%lu", (unsigned long)sdp);
		if (strcmp(num, p) == 0)
			break;
	}

	if (tmp == &gfs2_fs_list)
		error = -ENOENT;
	else 
		gfs2_lm_withdraw(sdp,
				"GFS2: fsid=%s: withdrawing from cluster at user's request\n",
				sdp->sd_fsname);

	up(&gfs2_fs_lock);

	RETURN(G2FN_DO_WITHDRAW, error);
}

/**
 * do_lockdump - Copy out the lock hash table to userspace
 * @p: the cookie of the filesystem
 * @buf:
 * @size:
 *
 * Returns: errno
 */

static int do_lockdump(char *p, char *buf, size_t size)
{
	ENTER(G2FN_DO_LOCKDUMP)
	struct list_head *tmp;
	struct gfs2_sbd *sdp;
	char num[21];
	struct gfs2_user_buffer ub;
	int error = 0;

	p = find_argument(p + 8);
	if (!p)
		RETURN(G2FN_DO_LOCKDUMP, -ENOENT);

	down(&gfs2_fs_lock);

	for (tmp = gfs2_fs_list.next; tmp != &gfs2_fs_list; tmp = tmp->next) {
		sdp = list_entry(tmp, struct gfs2_sbd, sd_list);
		sprintf(num, "%lu", (unsigned long)sdp);
		if (strcmp(num, p) == 0)
			break;
	}

	if (tmp == &gfs2_fs_list)
		error = -ENOENT;
	else {
		ub.ub_data = buf;
		ub.ub_size = size;
		ub.ub_count = 0;

		error = gfs2_dump_lockstate(sdp, &ub);
		if (!error)
			error = ub.ub_count;
	}

	up(&gfs2_fs_lock);

	RETURN(G2FN_DO_LOCKDUMP, error);
}

/**
 * gfs2_proc_write - take a command from userspace
 * @file:
 * @buf:
 * @size:
 * @offset:
 *
 * Returns: -errno or the number of bytes taken
 */

static ssize_t gfs2_proc_write(struct file *file, const char *buf, size_t size,
			       loff_t *offset)
{
	ENTER(G2FN_PROC_WRITE)
	char *p;

	spin_lock(&req_lock);
	p = file->private_data;
	file->private_data = NULL;
	spin_unlock(&req_lock);

	kfree(p);

	if (!size)
		RETURN(G2FN_PROC_WRITE, -EINVAL);

	p = kmalloc(size + 1, GFP_KERNEL);
	if (!p)
		RETURN(G2FN_PROC_WRITE, -ENOMEM);
	p[size] = 0;

	if (copy_from_user(p, buf, size)) {
		kfree(p);
		RETURN(G2FN_PROC_WRITE, -EFAULT);
	}

	spin_lock(&req_lock);
	file->private_data = p;
	spin_unlock(&req_lock);

	RETURN(G2FN_PROC_WRITE, size);
}

/**
 * gfs2_proc_read - return the results of a command
 * @file:
 * @buf:
 * @size:
 * @offset:
 *
 * Returns: -errno or the number of bytes returned
 */

static ssize_t gfs2_proc_read(struct file *file, char *buf, size_t size,
			      loff_t *offset)
{
	ENTER(G2FN_PROC_READ)
	char *p;
	int error;

	spin_lock(&req_lock);
	p = file->private_data;
	file->private_data = NULL;
	spin_unlock(&req_lock);

	if (!p)
		RETURN(G2FN_PROC_READ, -ENOENT);

	if (!size) {
		kfree(p);
		RETURN(G2FN_PROC_READ, -EINVAL);
	}

	if (strncmp(p, "list", 4) == 0)
		error = do_list(buf, size);
	else if (strncmp(p, "freeze", 6) == 0)
		error = do_freeze(p);
	else if (strncmp(p, "unfreeze", 8) == 0)
		error = do_unfreeze(p);
	else if (strncmp(p, "margs", 5) == 0)
		error = do_margs(p);
	else if (strncmp(p, "withdraw", 8) == 0)
		error = do_withdraw(p);
	else if (strncmp(p, "lockdump", 8) == 0)
		error = do_lockdump(p, buf, size);
	else
		error = -EOPNOTSUPP;

	kfree(p);

	RETURN(G2FN_PROC_READ, error);
}

/**
 * gfs2_proc_close - free any mismatches writes
 * @inode:
 * @file:
 *
 * Returns: 0
 */

static int gfs2_proc_close(struct inode *inode, struct file *file)
{
	ENTER(G2FN_PROC_CLOSE)
	kfree(file->private_data);
	RETURN(G2FN_PROC_CLOSE, 0);
}

static struct file_operations gfs2_proc_fops =
{
	.owner = THIS_MODULE,
	.write = gfs2_proc_write,
	.read = gfs2_proc_read,
	.release = gfs2_proc_close,
};

/**
 * gfs2_proc_init - initialize GFS2' proc interface
 *
 */

int gfs2_proc_init(void)
{
	ENTER(G2FN_PROC_INIT)
	struct proc_dir_entry *pde;

	INIT_LIST_HEAD(&gfs2_fs_list);
	init_MUTEX(&gfs2_fs_lock);
	gfs2_proc_margs = NULL;
	spin_lock_init(&gfs2_proc_margs_lock);
	spin_lock_init(&req_lock);

	pde = create_proc_entry("fs/gfs2", S_IFREG | 0600, NULL);
	if (!pde)
		RETURN(G2FN_PROC_INIT, -ENOMEM);

	pde->owner = THIS_MODULE;
	pde->proc_fops = &gfs2_proc_fops;

	RETURN(G2FN_PROC_INIT, 0);
}

/**
 * gfs2_proc_uninit - uninitialize GFS2' proc interface
 *
 */

void gfs2_proc_uninit(void)
{
	ENTER(G2FN_PROC_UNINIT)
	kfree(gfs2_proc_margs);
	remove_proc_entry("fs/gfs2", NULL);
	RET(G2FN_PROC_UNINIT);
}

