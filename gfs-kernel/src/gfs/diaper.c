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
#include <linux/blkdev.h>
#include <linux/idr.h>
#include <linux/mempool.h>

#include "gfs.h"
#include "diaper.h"
#include "ops_fstype.h"

struct diaper_holder {
	struct list_head dh_list;
	unsigned int dh_count;

	struct gendisk *dh_gendisk;

	struct block_device *dh_real;
	struct block_device *dh_diaper;

	struct gfs_sbd *dh_sbd;
	mempool_t *dh_mempool;
	struct super_block *dh_dummy_sb;
};

struct bio_wrapper {
	struct bio bw_bio;
	struct bio *bw_orig;
	struct diaper_holder *bw_dh;
};

static int diaper_major = 0;
static LIST_HEAD(diaper_list);
static spinlock_t diaper_lock;
static DEFINE_IDR(diaper_idr);
kmem_cache_t *diaper_slab;

/**
 * diaper_open -
 * @inode:
 * @file:
 *
 * Don't allow these devices to be opened from userspace
 * or from other kernel routines.  They should only be opened
 * from this file.
 *
 * Returns: -ENOSYS
 */

static int
diaper_open(struct inode *inode, struct file *file)
{
	ENTER(GFN_DIAPER_OPEN)
	RETURN(GFN_DIAPER_OPEN, -ENOSYS);
}

static struct block_device_operations diaper_fops = {
	.owner = THIS_MODULE,
	.open = diaper_open,
};

/**
 * diaper_end_io - Called at the end of a block I/O
 * @bio:
 * @bytes_done:
 * @error:
 *
 * Interrupt context, no ENTER/RETURN
 *
 * Returns: an integer thats usually discarded
 */

static int
diaper_end_io(struct bio *bio, unsigned int bytes_done, int error)
{
	struct bio_wrapper *bw = container_of(bio, struct bio_wrapper, bw_bio);
	struct diaper_holder *dh = bw->bw_dh;
	struct gfs_sbd *sdp = dh->dh_sbd;

        if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
                error = -EIO;

	bio_endio(bw->bw_orig, bytes_done, error);
	if (bio->bi_size)
		return 1;

	atomic_dec(&sdp->sd_bio_outstanding);
	mempool_free(bw, dh->dh_mempool);

	return 0;
}

/**
 * diaper_make_request -
 * @q:
 * @bio:
 *
 * Returns: 0
 */

static int
diaper_make_request(request_queue_t *q, struct bio *bio)
{
	ENTER(GFN_DIAPER_MAKE_REQUEST)
	struct diaper_holder *dh = (struct diaper_holder *)q->queuedata;
	struct gfs_sbd *sdp = dh->dh_sbd;
	struct bio_wrapper *bw;

	atomic_inc(&sdp->sd_bio_outstanding);
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags))) {
		atomic_dec(&sdp->sd_bio_outstanding);
		bio_io_error(bio, bio->bi_size);
		RETURN(GFN_DIAPER_MAKE_REQUEST, 0);
	}
	if (bio_rw(bio) == WRITE)
		atomic_inc(&sdp->sd_bio_writes);
	else
		atomic_inc(&sdp->sd_bio_reads);

	bw = mempool_alloc(dh->dh_mempool, GFP_NOIO);

	bw->bw_bio = *bio;
	bw->bw_bio.bi_bdev = dh->dh_real;
	bw->bw_bio.bi_end_io = diaper_end_io;
	bw->bw_orig = bio;
	bw->bw_dh = dh;

	generic_make_request(&bw->bw_bio);

	RETURN(GFN_DIAPER_MAKE_REQUEST, 0);
}

/**
 * minor_get -
 *
 * Returns: a unused minor number
 */

static int
minor_get(void)
{
	ENTER(GFN_MINOR_GET)
	int minor;
	int error;
      
	for (;;) {
		if (!idr_pre_get(&diaper_idr, GFP_KERNEL))
			RETURN(GFN_MINOR_GET, -ENOMEM);
      
		spin_lock(&diaper_lock);
		error = idr_get_new(&diaper_idr, NULL, &minor);
		spin_unlock(&diaper_lock);

		if (!error)
			break;
		if (error != -EAGAIN)
			RETURN(GFN_MINOR_GET, error);
	}

	RETURN(GFN_MINOR_GET, minor);
}

/**
 * minor_put - Free a used minor number
 * @minor:
 *
 */

static void
minor_put(int minor)
{
	ENTER(GFN_MINOR_PUT)
	spin_lock(&diaper_lock);
	idr_remove(&diaper_idr, minor);
	spin_unlock(&diaper_lock);
	RET(GFN_MINOR_PUT);
}

/**
 * gfs_dummy_write_super_lockfs - pass a freeze from the real device to the diaper
 * @sb: the real device's dummy sb
 *
 */

static void
gfs_dummy_write_super_lockfs(struct super_block *sb)
{
	ENTER(GFN_DUMMY_WRITE_SUPER_LOCKFS)
	struct diaper_holder *dh = (struct diaper_holder *)sb->s_fs_info;
	freeze_bdev(dh->dh_diaper);
	RET(GFN_DUMMY_WRITE_SUPER_LOCKFS);
}

/**
 * gfs_dummy_unlockfs - pass a thaw from the real device to the diaper
 * @sb: the real device's dummy sb
 *
 */

static void
gfs_dummy_unlockfs(struct super_block *sb)
{
	ENTER(GFN_DUMMY_UNLOCKFS)
	struct diaper_holder *dh = (struct diaper_holder *)sb->s_fs_info;
	thaw_bdev(dh->dh_diaper, dh->dh_sbd->sd_vfs);
	RET(GFN_DUMMY_UNLOCKFS);
}

struct super_operations gfs_dummy_sops = {
	.write_super_lockfs = gfs_dummy_write_super_lockfs,
	.unlockfs = gfs_dummy_unlockfs,
};

/**
 * gfs_dummy_sb - create a dummy superblock for the real device
 * @dh:
 *
 * Returns: errno
 */

static int
get_dummy_sb(struct diaper_holder *dh)
{
	ENTER(GFN_GET_DUMMY_SB)
	struct block_device *real = dh->dh_real;
	struct super_block *sb;
	struct inode *inode;
	int error;

	down(&real->bd_mount_sem);
	sb = sget(&gfs_fs_type, gfs_test_bdev_super, gfs_set_bdev_super, real);
	up(&real->bd_mount_sem);
	if (IS_ERR(sb))
		RETURN(GFN_GET_DUMMY_SB, PTR_ERR(sb));

	error = -ENOMEM;
	inode = new_inode(sb);
	if (!inode)
		goto fail;

	make_bad_inode(inode);

	sb->s_root = d_alloc_root(inode);
	if (!sb->s_root)
		goto fail_iput;

	sb->s_op = &gfs_dummy_sops;
	sb->s_fs_info = dh;

	up_write(&sb->s_umount);
	module_put(gfs_fs_type.owner);

	dh->dh_dummy_sb = sb;

	RETURN(GFN_GET_DUMMY_SB, 0);

 fail_iput:
	iput(inode);

 fail:
	up_write(&sb->s_umount);
	deactivate_super(sb);
	RETURN(GFN_GET_DUMMY_SB, error);
}

/**
 * diaper_get - Do the work of creating a diaper device
 * @real:
 * @flags:
 *
 * Returns: the diaper device or ERR_PTR()
 */

static struct diaper_holder *
diaper_get(struct block_device *real, int flags)
{
	ENTER(GFN_DIAPER_GET2)
	struct diaper_holder *dh;
	struct gendisk *gd;
	struct block_device *diaper;
	unsigned int minor;
	int error = -ENOMEM;

	minor = minor_get();
	if (minor < 0)
		RETURN(GFN_DIAPER_GET2, ERR_PTR(error));

	dh = kmalloc(sizeof(struct diaper_holder), GFP_KERNEL);
	if (!dh)
		goto fail;
	memset(dh, 0, sizeof(struct diaper_holder));

	gd = alloc_disk(1);
	if (!gd)
		goto fail_kfree;

	gd->queue = blk_alloc_queue(GFP_KERNEL);
	if (!gd->queue)
		goto fail_gd;

	blk_queue_hardsect_size(gd->queue, bdev_hardsect_size(real));
	blk_queue_make_request(gd->queue, diaper_make_request);
	gd->queue->queuedata = dh;

	gd->major = diaper_major;
	gd->first_minor = minor;
	{
		char buf[BDEVNAME_SIZE];
		bdevname(real, buf);
		snprintf(gd->disk_name, sizeof(gd->disk_name), "diapered_%s", buf);
	}
	gd->fops = &diaper_fops;
	gd->private_data = dh;
	gd->capacity--;

	add_disk(gd);

	diaper = bdget_disk(gd, 0);
	if (!diaper)
		goto fail_remove;
	down(&diaper->bd_sem);
	if (!diaper->bd_openers) {
		diaper->bd_disk = gd;
		diaper->bd_contains = diaper;
		bd_set_size(diaper, 0x7FFFFFFFFFFFFFFFULL);
		diaper->bd_inode->i_data.backing_dev_info = &default_backing_dev_info;
	} else
		printk("GFS: diaper: reopening\n");
	diaper->bd_openers++;
	up(&diaper->bd_sem);

	error = -ENOMEM;
	dh->dh_mempool = mempool_create(512,
					mempool_alloc_slab, mempool_free_slab,
					diaper_slab);
	if (!dh->dh_mempool)
		goto fail_bdput;

	dh->dh_count = 1;
	dh->dh_gendisk = gd;
	dh->dh_real = real;
	dh->dh_diaper = diaper;

	error = get_dummy_sb(dh);
	if (error)
		goto fail_mempool;

	RETURN(GFN_DIAPER_GET2, dh);

 fail_mempool:
	mempool_destroy(dh->dh_mempool);

 fail_bdput:
	down(&diaper->bd_sem);
	if (!--diaper->bd_openers) {
		invalidate_bdev(diaper, 1);
		diaper->bd_contains = NULL;
		diaper->bd_disk = NULL;
	} else
		printk("GFS: diaper: not closed\n");
	up(&diaper->bd_sem);
	bdput(diaper);	

 fail_remove:
	del_gendisk(gd);
	blk_put_queue(gd->queue);

 fail_gd:
	put_disk(gd);

 fail_kfree:
	kfree(dh);

 fail:
	minor_put(minor);
	RETURN(GFN_DIAPER_GET2, ERR_PTR(error));
}

/**
 * diaper_put - Do the work of destroying a diaper device
 * @dh:
 *
 */

static void
diaper_put(struct diaper_holder *dh)
{
	ENTER(GFN_DIAPER_PUT2)
	struct block_device *diaper = dh->dh_diaper;
	struct gendisk *gd = dh->dh_gendisk;
	int minor = dh->dh_gendisk->first_minor;

	generic_shutdown_super(dh->dh_dummy_sb);

	mempool_destroy(dh->dh_mempool);

	down(&diaper->bd_sem);
	if (!--diaper->bd_openers) {
		invalidate_bdev(diaper, 1);
		diaper->bd_contains = NULL;
		diaper->bd_disk = NULL;
	} else
		printk("GFS: diaper: not closed\n");
	up(&diaper->bd_sem);

	bdput(diaper);
	del_gendisk(gd);
	blk_put_queue(gd->queue);
	put_disk(gd);
	kfree(dh);
	minor_put(minor);

	RET(GFN_DIAPER_PUT2);
}

/**
 * gfs_diaper_get - Get a hold of an existing diaper or create a new one
 * @real:
 * @flags:
 *
 * Returns: the diaper device or ERR_PTR()
 */

struct block_device *
gfs_diaper_get(struct block_device *real, int flags)
{
	ENTER(GFN_DIAPER_GET)
	struct list_head *tmp;
	struct diaper_holder *dh, *dh_new = NULL;

	for (;;) {
		spin_lock(&diaper_lock);
		for (tmp = diaper_list.next;
		     tmp != &diaper_list;
		     tmp = tmp->next) {
			dh = list_entry(tmp, struct diaper_holder, dh_list);
			if (dh->dh_real == real) {
				dh->dh_count++;
				break;
			}
		}
		if (tmp == &diaper_list)
			dh = NULL;
		if (!dh && dh_new) {
			dh = dh_new;
			list_add(&dh->dh_list, &diaper_list);
			dh_new = NULL;
		}
		spin_unlock(&diaper_lock);

		if (dh) {
			if (dh_new)
				diaper_put(dh_new);
			RETURN(GFN_DIAPER_GET, dh->dh_diaper);
		}

		dh_new = diaper_get(real, flags);
		if (IS_ERR(dh_new))
			RETURN(GFN_DIAPER_GET, (struct block_device *)dh_new);
	}
}

/**
 * gfs_diaper_put - Drop a reference on a diaper
 * @diaper:
 *
 */

void
gfs_diaper_put(struct block_device *diaper)
{
	ENTER(GFN_DIAPER_PUT)
	struct list_head *tmp;
	struct diaper_holder *dh;

	spin_lock(&diaper_lock);
	for (tmp = diaper_list.next;
	     tmp != &diaper_list;
	     tmp = tmp->next) {
		dh = list_entry(tmp, struct diaper_holder, dh_list);
		if (dh->dh_diaper == diaper) {
			if (!--dh->dh_count) {
				list_del(&dh->dh_list);
				spin_unlock(&diaper_lock);
				diaper_put(dh);
			} else
				spin_unlock(&diaper_lock);
			RET(GFN_DIAPER_PUT);
		}
	}
	spin_unlock(&diaper_lock);

	printk("GFS: diaper: unknown undiaper\n");
	RET(GFN_DIAPER_PUT);
}

/**
 * gfs_diaper_register_sbd -
 * @diaper:
 * @sdp:
 *
 */

void
gfs_diaper_register_sbd(struct block_device *diaper, struct gfs_sbd *sdp)
{
	ENTER(GFN_DIAPER_REGISTER_SBD)
	struct list_head *tmp;
	struct diaper_holder *dh;

	spin_lock(&diaper_lock);
	for (tmp = diaper_list.next;
	     tmp != &diaper_list;
	     tmp = tmp->next) {
		dh = list_entry(tmp, struct diaper_holder, dh_list);
		if (dh->dh_diaper == diaper) {
			dh->dh_sbd = sdp;
			spin_unlock(&diaper_lock);
			RET(GFN_DIAPER_REGISTER_SBD);
		}
	}
	spin_unlock(&diaper_lock);

	printk("GFS: diaper: unknown register\n");
	RET(GFN_DIAPER_REGISTER_SBD);
}

/**
 * gfs_diaper_2real -
 * @diaper:
 *
 * Returns: the real device cooresponding to the diaper
 */

struct block_device *
gfs_diaper_2real(struct block_device *diaper)
{
	ENTER(GFN_DIAPER_2REAL)
        struct list_head *tmp;
        struct diaper_holder *dh;

        spin_lock(&diaper_lock);
        for (tmp = diaper_list.next;
             tmp != &diaper_list;
             tmp = tmp->next) {
                dh = list_entry(tmp, struct diaper_holder, dh_list);
                if (dh->dh_diaper == diaper) {
                        spin_unlock(&diaper_lock);
			RETURN(GFN_DIAPER_2REAL, dh->dh_real);
                }
        }
        spin_unlock(&diaper_lock);

        printk("GFS: diaper: unknown 2real\n");
	RETURN(GFN_DIAPER_2REAL, NULL);
}

/**
 * gfs_diaper_init -
 *
 * Returns: errno
 */

int
gfs_diaper_init(void)
{
	ENTER(GFN_DIAPER_INIT)

	spin_lock_init(&diaper_lock);

	diaper_slab = kmem_cache_create("gfs_bio_wrapper", sizeof(struct bio_wrapper),
					0, 0,
					NULL, NULL);	

	diaper_major = register_blkdev(0, "gfs_diaper");
	if (diaper_major < 0)
		RETURN(GFN_DIAPER_INIT, diaper_major);
	
	RETURN(GFN_DIAPER_INIT, 0);
}

/**
 * gfs_diaper_uninit -
 *
 */

void
gfs_diaper_uninit(void)
{
	ENTER(GFN_DIAPER_UNINIT)
	unregister_blkdev(diaper_major, "gfs_diaper");
	kmem_cache_destroy(diaper_slab);
	RET(GFN_DIAPER_UNINIT);
}

