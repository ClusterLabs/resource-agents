/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
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

static struct list_head gfs2_fs_list;
static struct semaphore gfs2_fs_lock;
char *gfs2_proc_margs;
spinlock_t gfs2_proc_margs_lock;
static spinlock_t req_lock;

/**
 * gfs2_proc_fs_add - Add a FS to the list of mounted FSs
 * @sdp:
 *
 */

void gfs2_proc_fs_add(struct gfs2_sbd *sdp)
{
	down(&gfs2_fs_lock);
	list_add(&sdp->sd_list, &gfs2_fs_list);
	up(&gfs2_fs_lock);
}

/**
 * gfs2_proc_fs_del - Remove a FS from the list of mounted FSs
 * @sdp:
 *
 */

void gfs2_proc_fs_del(struct gfs2_sbd *sdp)
{
	down(&gfs2_fs_lock);
	list_del(&sdp->sd_list);
	up(&gfs2_fs_lock);
}

/**
 * do_list - Copy the list of mounted FSs to userspace
 * @user_buf:
 * @size:
 *
 * @Returns: -errno, or the number of bytes copied to userspace
 */

static ssize_t do_list(char *user_buf, size_t size)
{
	struct gfs2_sbd *sdp = NULL;
	unsigned int x;
	char num[21];
	char *buf;
	int error = 0;

	down(&gfs2_fs_lock);

	x = 0;
	list_for_each_entry(sdp, &gfs2_fs_list, sd_list) {
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
	list_for_each_entry(sdp, &gfs2_fs_list, sd_list) {
		x += sprintf(buf + x, "%lu %s %s\n",
			     (unsigned long)sdp,
			     sdp->sd_vfs->s_id,
			     sdp->sd_fsname);
	}

	if (copy_to_user(user_buf, buf, x))
		error = -EFAULT;
	else
		error = x;

	kfree(buf);

 out:
	up(&gfs2_fs_lock);

	return error;
}

/**
 * find_argument -
 * @p:
 *
 * Returns:
 */

static char *find_argument(char *p)
{
	char *p2;

	while (*p == ' ' || *p == '\n')
		p++;
	if (!*p)
		return NULL;
	for (p2 = p; *p2; p2++) /* do nothing */;
	p2--;
	while (*p2 == ' ' || *p2 == '\n')
		*p2-- = 0;

	return p;
}

/**
 * find_sdp - find a superblock with the given address
 * @p:
 *
 * Returns: superblock-data pointer
 */

static struct gfs2_sbd *find_sdp(char *p)
{
	struct gfs2_sbd *sdp;
	char num[21];

	list_for_each_entry(sdp, &gfs2_fs_list, sd_list) {
		sprintf(num, "%lu", (unsigned long)sdp);
		if (strcmp(num, p) == 0)
			return sdp;
	}

	return NULL;
}

/**
 * do_freeze - freeze a filesystem
 * @p: the freeze command
 *
 * Returns: errno
 */

static int do_freeze(char *p)
{
	struct gfs2_sbd *sdp;
	int error = 0;

	p = find_argument(p + 6);
	if (!p)
		return -ENOENT;

	down(&gfs2_fs_lock);

	sdp = find_sdp(p);
	if (!sdp)
		error = -ENOENT;
	else
		error = gfs2_freeze_fs(sdp);

	up(&gfs2_fs_lock);

	return error;
}

/**
 * do_unfreeze - unfreeze a filesystem
 * @p: the unfreeze command
 *
 * Returns: errno
 */

static int do_unfreeze(char *p)
{
	struct gfs2_sbd *sdp;
	int error = 0;

	p = find_argument(p + 8);
	if (!p)
		return -ENOENT;

	down(&gfs2_fs_lock);

	sdp = find_sdp(p);
	if (!sdp)
		error = -ENOENT;
	else
		gfs2_unfreeze_fs(sdp);

	up(&gfs2_fs_lock);

	return error;
}

/**
 * do_margs - Pass in mount arguments
 * @p: the margs command
 *
 * Returns: errno
 */

static int do_margs(char *p)
{
	char *new_buf, *old_buf;

	p = find_argument(p + 5);
	if (!p)
		return -ENOENT;

	new_buf = kmalloc(strlen(p) + 1, GFP_KERNEL);
	if (!new_buf)
		return -ENOMEM;
	strcpy(new_buf, p);

	spin_lock(&gfs2_proc_margs_lock);
	old_buf = gfs2_proc_margs;
	gfs2_proc_margs = new_buf;
	spin_unlock(&gfs2_proc_margs_lock);

	kfree(old_buf);

	return 0;
}

/**
 * do_withdraw - withdraw a from the cluster for one filesystem
 * @p: the cookie of the filesystem
 *
 * Returns: errno
 */

static int do_withdraw(char *p)
{
	struct gfs2_sbd *sdp;
	int error = 0;

	p = find_argument(p + 8);
	if (!p)
		return -ENOENT;

	down(&gfs2_fs_lock);

	sdp = find_sdp(p);
	if (!sdp)
		error = -ENOENT;
	else
		gfs2_lm_withdraw(sdp,
				"GFS2: fsid=%s: withdrawing from cluster at user's request\n",
				sdp->sd_fsname);

	up(&gfs2_fs_lock);

	return error;
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
	struct gfs2_sbd *sdp;
	struct gfs2_user_buffer ub;
	int error = 0;

	p = find_argument(p + 8);
	if (!p)
		return -ENOENT;

	down(&gfs2_fs_lock);

	sdp = find_sdp(p);
	if (!sdp)
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

	return error;
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
	char *p;

	spin_lock(&req_lock);
	p = file->private_data;
	file->private_data = NULL;
	spin_unlock(&req_lock);

	kfree(p);

	if (!size)
		return -EINVAL;

	p = kmalloc(size + 1, GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	p[size] = 0;

	if (copy_from_user(p, buf, size)) {
		kfree(p);
		return -EFAULT;
	}

	spin_lock(&req_lock);
	file->private_data = p;
	spin_unlock(&req_lock);

	return size;
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
	char *p;
	int error;

	spin_lock(&req_lock);
	p = file->private_data;
	file->private_data = NULL;
	spin_unlock(&req_lock);

	if (!p)
		return -ENOENT;

	if (!size) {
		kfree(p);
		return -EINVAL;
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

	return error;
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
	kfree(file->private_data);
	return 0;
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
	struct proc_dir_entry *pde;

	INIT_LIST_HEAD(&gfs2_fs_list);
	init_MUTEX(&gfs2_fs_lock);
	gfs2_proc_margs = NULL;
	spin_lock_init(&gfs2_proc_margs_lock);
	spin_lock_init(&req_lock);

	pde = create_proc_entry("fs/gfs2", S_IFREG | 0600, NULL);
	if (!pde)
		return -ENOMEM;

	pde->owner = THIS_MODULE;
	pde->proc_fops = &gfs2_proc_fops;

	return 0;
}

/**
 * gfs2_proc_uninit - uninitialize GFS2' proc interface
 *
 */

void gfs2_proc_uninit(void)
{
	kfree(gfs2_proc_margs);
	remove_proc_entry("fs/gfs2", NULL);
}

