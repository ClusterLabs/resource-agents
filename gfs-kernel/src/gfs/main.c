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
#include <linux/init.h>

#include "gfs.h"
#include "mount.h"
#include "ops_fstype.h"

struct proc_dir_entry *gfs_proc_entry = NULL;

/**
 * init_gfs_fs - Register GFS as a filesystem
 *
 * Returns: 0 on success, error code on failure
 */

int __init
init_gfs_fs(void)
{
	int error = 0;

	init_MUTEX(&gfs_mount_args_lock);

	gfs_proc_entry = create_proc_read_entry("fs/gfs", S_IFREG | 0200, NULL, NULL, NULL);
	if (!gfs_proc_entry) {
		printk("GFS: can't register /proc/fs/gfs\n");
		error = -EINVAL;
		goto fail;
	}
	gfs_proc_entry->write_proc = gfs_proc_write;

	gfs_random_number = xtime.tv_nsec;

	gfs_glock_cachep = kmem_cache_create("gfs_glock", sizeof(struct gfs_glock),
					     0, 0,
					     NULL, NULL);
	if (!gfs_glock_cachep)
		goto fail2;

	gfs_inode_cachep = kmem_cache_create("gfs_inode", sizeof(struct gfs_inode),
					     0, 0,
					     NULL, NULL);
	if (!gfs_inode_cachep)
		goto fail2;

	gfs_bufdata_cachep = kmem_cache_create("gfs_bufdata", sizeof(struct gfs_bufdata),
					       0, 0,
					       NULL, NULL);
	if (!gfs_bufdata_cachep)
		goto fail2;

	gfs_mhc_cachep = kmem_cache_create("gfs_meta_header_cache", sizeof(struct gfs_meta_header_cache),
					   0, 0,
					   NULL, NULL);
	if (!gfs_mhc_cachep)
		goto fail2;

	error = register_filesystem(&gfs_fs_type);
	if (error)
		goto fail2;

	printk("GFS %s (built %s %s) installed\n",
	       GFS_RELEASE_NAME, __DATE__, __TIME__);

	return 0;

      fail2:
	if (gfs_mhc_cachep)
		kmem_cache_destroy(gfs_mhc_cachep);

	if (gfs_bufdata_cachep)
		kmem_cache_destroy(gfs_bufdata_cachep);

	if (gfs_inode_cachep)
		kmem_cache_destroy(gfs_inode_cachep);

	if (gfs_glock_cachep)
		kmem_cache_destroy(gfs_glock_cachep);

	down(&gfs_mount_args_lock);
	if (gfs_mount_args) {
		kfree(gfs_mount_args);
		gfs_mount_args = NULL;
	}
	up(&gfs_mount_args_lock);
	remove_proc_entry("fs/gfs", NULL);

      fail:
	return error;
}

/**
 * exit_gfs_fs - Unregister the file system
 *
 */

void __exit
exit_gfs_fs(void)
{
	unregister_filesystem(&gfs_fs_type);

	kmem_cache_destroy(gfs_mhc_cachep);
	kmem_cache_destroy(gfs_bufdata_cachep);
	kmem_cache_destroy(gfs_inode_cachep);
	kmem_cache_destroy(gfs_glock_cachep);

	down(&gfs_mount_args_lock);
	if (gfs_mount_args) {
		kfree(gfs_mount_args);
		gfs_mount_args = NULL;
	}
	up(&gfs_mount_args_lock);
	remove_proc_entry("fs/gfs", NULL);
}

MODULE_DESCRIPTION("Global File System " GFS_RELEASE_NAME);
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

module_init(init_gfs_fs);
module_exit(exit_gfs_fs);

