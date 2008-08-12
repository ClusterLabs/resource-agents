#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/time.h>

#include "gfs.h"
#include "ops_fstype.h"
#include "sys.h"
#include "proc.h"

/**
 * init_gfs_fs - Register GFS as a filesystem
 *
 * Returns: 0 on success, error code on failure
 */

int __init init_gfs_fs(void)
{
	int error;
	struct timespec tv;

/*	gfs2_init_lmh(); gfs2 should do this for us*/

	error = gfs_sys_init();
	if (error)
		return error;
	error = gfs_proc_init();
	if (error)
		goto fail;

	getnstimeofday(&tv);
	gfs_random_number = tv.tv_nsec;

	gfs_glock_cachep = kmem_cache_create("gfs_glock", sizeof(struct gfs_glock),
					     0, 0,
					     NULL);
	gfs_inode_cachep = NULL;
	gfs_bufdata_cachep = NULL;
	gfs_mhc_cachep = NULL;
	error = -ENOMEM;
	if (!gfs_glock_cachep)
		goto fail1;

	gfs_inode_cachep = kmem_cache_create("gfs_inode", sizeof(struct gfs_inode),
					     0, 0,
					     NULL);
	if (!gfs_inode_cachep)
		goto fail1;

	gfs_bufdata_cachep = kmem_cache_create("gfs_bufdata", sizeof(struct gfs_bufdata),
					       0, 0,
					       NULL);
	if (!gfs_bufdata_cachep)
		goto fail1;

	gfs_mhc_cachep = kmem_cache_create("gfs_meta_header_cache", sizeof(struct gfs_meta_header_cache),
					   0, 0,
					   NULL);
	if (!gfs_mhc_cachep)
		goto fail;

	error = register_filesystem(&gfs_fs_type);
	if (error)
		goto fail;

	printk("GFS %s (built %s %s) installed\n",
	       RELEASE_VERSION, __DATE__, __TIME__);

	error = init_lock_dlm();
	if (error)
		goto fail1;

	error = init_nolock();
	if (error)
		goto fail1;

	return 0;

 fail1:
	if (gfs_mhc_cachep)
		kmem_cache_destroy(gfs_mhc_cachep);

	if (gfs_bufdata_cachep)
		kmem_cache_destroy(gfs_bufdata_cachep);

	if (gfs_inode_cachep)
		kmem_cache_destroy(gfs_inode_cachep);

	if (gfs_glock_cachep)
		kmem_cache_destroy(gfs_glock_cachep);

	gfs_proc_uninit();
	
 fail:
	gfs_sys_uninit();

	return error;
}

/**
 * exit_gfs_fs - Unregister the file system
 *
 */

void __exit
exit_gfs_fs(void)
{
	exit_nolock();
	exit_lock_dlm();
	unregister_filesystem(&gfs_fs_type);

	kmem_cache_destroy(gfs_mhc_cachep);
	kmem_cache_destroy(gfs_bufdata_cachep);
	kmem_cache_destroy(gfs_inode_cachep);
	kmem_cache_destroy(gfs_glock_cachep);

	gfs_proc_uninit();
	gfs_sys_uninit();
}

MODULE_DESCRIPTION("Global File System " RELEASE_VERSION);
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

module_init(init_gfs_fs);
module_exit(exit_gfs_fs);

