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
#include <linux/module.h>
#include <asm/uaccess.h>

#include "gfs.h"
#include "mount.h"

char *gfs_mount_args = NULL;
struct semaphore gfs_mount_args_lock;

/**
 * gfs_make_args - Parse mount arguments
 * @data:
 * @args:
 *
 * Return: 0 on success, -EXXX on failure
 */

int
gfs_make_args(char *data, struct gfs_args *args)
{
	char *options, *x, *y;
	int do_free = FALSE;
	int error = 0;

	/*  If someone preloaded options, use those instead  */

	down(&gfs_mount_args_lock);
	if (gfs_mount_args) {
		data = gfs_mount_args;
		gfs_mount_args = NULL;
		do_free = TRUE;
	}
	up(&gfs_mount_args_lock);

	/*  Set some defaults  */

	memset(args, 0, sizeof(struct gfs_args));
	args->ar_num_glockd = GFS_GLOCKD_DEFAULT;

	/*  Split the options into tokens with the "," character and
	    process them  */

	for (options = data; (x = strsep(&options, ",")); ) {
		if (!*x)
			continue;

		y = strchr(x, '=');
		if (y)
			*y++ = 0;

		if (!strcmp(x, "lockproto")) {
			if (!y) {
				printk("GFS: need argument to lockproto\n");
				error = -EINVAL;
				break;
			}
			strncpy(args->ar_lockproto, y, 256);
			args->ar_lockproto[255] = 0;
		}

		else if (!strcmp(x, "locktable")) {
			if (!y) {
				printk("GFS: need argument to locktable\n");
				error = -EINVAL;
				break;
			}
			strncpy(args->ar_locktable, y, 256);
			args->ar_locktable[255] = 0;
		}

		else if (!strcmp(x, "hostdata")) {
			if (!y) {
				printk("GFS: need argument to hostdata\n");
				error = -EINVAL;
				break;
			}
			strncpy(args->ar_hostdata, y, 256);
			args->ar_hostdata[255] = 0;
		}

		else if (!strcmp(x, "ignore_local_fs"))
			args->ar_ignore_local_fs = TRUE;

		else if (!strcmp(x, "localflocks"))
			args->ar_localflocks = TRUE;

		else if (!strcmp(x, "localcaching"))
			args->ar_localcaching = TRUE;

		else if (!strcmp(x, "upgrade"))
			args->ar_upgrade = TRUE;

		else if (!strcmp(x, "num_glockd")) {
			if (!y) {
				printk("GFS: need argument to num_glockd\n");
				error = -EINVAL;
				break;
			}
			sscanf(y, "%u", &args->ar_num_glockd);
			if (!args->ar_num_glockd || args->ar_num_glockd > GFS_GLOCKD_MAX) {
				printk("GFS: 0 < num_glockd <= %u  (not %u)\n",
				       GFS_GLOCKD_MAX, args->ar_num_glockd);
				error = -EINVAL;
				break;
			}
		}

		else if (!strcmp(x, "acl"))
			args->ar_posixacls = TRUE;

		/*  Unknown  */

		else {
			printk("GFS: unknown option: %s\n", x);
			error = -EINVAL;
			break;
		}
	}

	if (error)
		printk("GFS: invalid mount option(s)\n");

	if (do_free)
		kfree(data);

	return error;
}

/**
 * gfs_proc_write - Read in some mount options
 * @file: unused
 * @buffer: a buffer of mount options
 * @count: the length of the mount options
 * @data: unused
 *
 * Called when someone writes to /proc/fs/gfs.
 * It allows you to specify mount options when you can't do it
 * from mount.  i.e. from a inital ramdisk
 *
 * Returns: 0 on success, -EXXX on failure
 */

int
gfs_proc_write(struct file *file,
	       const char *buffer, unsigned long count,
	       void *data)
{
	int error;
	char *p;

	if (!try_module_get(THIS_MODULE))
		return -EAGAIN; /* Huh!?! */
	down(&gfs_mount_args_lock);

	if (gfs_mount_args) {
		kfree(gfs_mount_args);
		gfs_mount_args = NULL;
	}

	if (!count) {
		error = 0;
		goto fail;
	}

	gfs_mount_args = gmalloc(count + 1);

	error = -EFAULT;
	if (copy_from_user(gfs_mount_args, buffer, count))
		goto fail_free;

	gfs_mount_args[count] = 0;

	/*  Get rid of extra newlines  */

	for (p = gfs_mount_args; *p; p++)
		if (*p == '\n')
			*p = 0;

	up(&gfs_mount_args_lock);
	module_put(THIS_MODULE);

	return count;

      fail_free:
	kfree(gfs_mount_args);
	gfs_mount_args = NULL;

      fail:
	up(&gfs_mount_args_lock);
	module_put(THIS_MODULE);
	return error;
}
