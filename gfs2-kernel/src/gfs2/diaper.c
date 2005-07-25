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

#include "gfs2.h"
#include "diaper.h"
#include "ops_fstype.h"

struct diaper_holder {
	struct list_head dh_list;
	unsigned int dh_count;

	struct gendisk *dh_gendisk;

	struct block_device *dh_real;
	struct block_device *dh_diaper;

	struct gfs2_sbd *dh_sbd;
	mempool_t *dh_mempool;
	struct super_block *dh_dummy_sb;
};

struct bio_wrapper {
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
 * Returns: -EOPNOTSUPP
 */

static int diaper_open(struct inode *inode, struct file *file)
{
	return -EOPNOTSUPP;
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

static int diaper_end_io(struct bio *bio, unsigned int bytes_done, int error)
{
	struct bio_wrapper *bw = (struct bio_wrapper *)bio->bi_private;
	struct diaper_holder *dh = bw->bw_dh;
	struct gfs2_sbd *sdp = dh->dh_sbd;

	bio_endio(bw->bw_orig, bytes_done, error);
	if (bio->bi_size)
		return 1;

	atomic_dec(&sdp->sd_bio_outstanding);
	bio_put(bio);
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

static int diaper_make_request(request_queue_t *q, struct bio *bio)
{
	struct diaper_holder *dh = (struct diaper_holder *)q->queuedata;
	struct gfs2_sbd *sdp = dh->dh_sbd;
	struct bio_wrapper *bw;
	struct bio *bi;

	atomic_inc(&sdp->sd_bio_outstanding);
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags))) {
		atomic_dec(&sdp->sd_bio_outstanding);
		bio_endio(bio, bio->bi_size, 0);
		return 0;
	}
	if (bio_rw(bio) == WRITE)
		atomic_add(bio->bi_size >> 9, &sdp->sd_bio_writes);
	else
		atomic_add(bio->bi_size >> 9, &sdp->sd_bio_reads);

	bw = mempool_alloc(dh->dh_mempool, GFP_NOIO);
	bw->bw_orig = bio;
	bw->bw_dh = dh;

	bi = bio_clone(bio, GFP_NOIO);
	bi->bi_bdev = dh->dh_real;
	bi->bi_end_io = diaper_end_io;
	bi->bi_private = bw;

	generic_make_request(bi);

	return 0;
}

/**
 * minor_get -
 *
 * Returns: a unused minor number
 */

static int minor_get(void)
{
	int minor;
	int error;
      
	for (;;) {
		if (!idr_pre_get(&diaper_idr, GFP_KERNEL))
			return -ENOMEM;
      
		spin_lock(&diaper_lock);
		error = idr_get_new(&diaper_idr, NULL, &minor);
		spin_unlock(&diaper_lock);

		if (!error)
			break;
		if (error != -EAGAIN)
			return error;
	}

	return minor;
}

/**
 * minor_put - Free a used minor number
 * @minor:
 *
 */

static void minor_put(int minor)
{
	spin_lock(&diaper_lock);
	idr_remove(&diaper_idr, minor);
	spin_unlock(&diaper_lock);
}

/**
 * gfs2_dummy_write_super_lockfs - pass a freeze from real device to the diaper
 * @sb: the real device's dummy sb
 *
 */

static void gfs2_dummy_write_super_lockfs(struct super_block *sb)
{
	struct diaper_holder *dh = (struct diaper_holder *)sb->s_fs_info;
	freeze_bdev(dh->dh_diaper);
}

/**
 * gfs2_dummy_unlockfs - pass a thaw from the real device to the diaper
 * @sb: the real device's dummy sb
 *
 */

static void gfs2_dummy_unlockfs(struct super_block *sb)
{
	struct diaper_holder *dh = (struct diaper_holder *)sb->s_fs_info;
	thaw_bdev(dh->dh_diaper, dh->dh_sbd->sd_vfs);
}

struct super_operations gfs2_dummy_sops = {
	.write_super_lockfs = gfs2_dummy_write_super_lockfs,
	.unlockfs = gfs2_dummy_unlockfs,
};

/**
 * gfs2_dummy_sb - create a dummy superblock for the real device
 * @dh:
 *
 * Returns: errno
 */

static int get_dummy_sb(struct diaper_holder *dh)
{
	struct block_device *real = dh->dh_real;
	struct super_block *sb;
	struct inode *inode;
	int error;

	down(&real->bd_mount_sem);
	sb = sget(&gfs2_fs_type, gfs2_test_bdev_super, gfs2_set_bdev_super,
		  real);
	up(&real->bd_mount_sem);
	if (IS_ERR(sb))
		return PTR_ERR(sb);

	error = -ENOMEM;
	inode = new_inode(sb);
	if (!inode)
		goto fail;

	make_bad_inode(inode);

	sb->s_root = d_alloc_root(inode);
	if (!sb->s_root)
		goto fail_iput;

	sb->s_op = &gfs2_dummy_sops;
	sb->s_fs_info = dh;

	up_write(&sb->s_umount);
	module_put(gfs2_fs_type.owner);

	dh->dh_dummy_sb = sb;

	return 0;

 fail_iput:
	iput(inode);

 fail:
	up_write(&sb->s_umount);
	deactivate_super(sb);
	return error;
}

static int diaper_congested(void *congested_data, int bdi_bits)
{
	struct diaper_holder *dh = (struct diaper_holder *)congested_data;
	request_queue_t *q = bdev_get_queue(dh->dh_real);
	return bdi_congested(&q->backing_dev_info, bdi_bits);
}

static void diaper_unplug(request_queue_t *q)
{
	struct diaper_holder *dh = (struct diaper_holder *)q->queuedata;
	request_queue_t *rq = bdev_get_queue(dh->dh_real);

	if (rq->unplug_fn)
		rq->unplug_fn(rq);
}

static int diaper_flush(request_queue_t *q, struct gendisk *disk,
			sector_t *error_sector)
{
	struct diaper_holder *dh = (struct diaper_holder *)q->queuedata;
	request_queue_t *rq = bdev_get_queue(dh->dh_real);
	int error = -EOPNOTSUPP;

	if (rq->issue_flush_fn)
		error = rq->issue_flush_fn(rq, dh->dh_real->bd_disk, NULL);

	return error;
}

/**
 * diaper_get - Do the work of creating a diaper device
 * @real:
 * @flags:
 *
 * Returns: the diaper device or ERR_PTR()
 */

static struct diaper_holder *diaper_get(struct block_device *real, int flags)
{
	struct diaper_holder *dh;
	struct gendisk *gd;
	struct block_device *diaper;
	unsigned int minor;
	int error = -ENOMEM;

	minor = minor_get();
	if (minor < 0)
		return ERR_PTR(error);

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

	gd->queue->queuedata = dh;
	gd->queue->backing_dev_info.congested_fn = diaper_congested;
	gd->queue->backing_dev_info.congested_data = dh;
	gd->queue->unplug_fn = diaper_unplug;
	gd->queue->issue_flush_fn = diaper_flush;
	blk_queue_make_request(gd->queue, diaper_make_request);
	blk_queue_stack_limits(gd->queue, bdev_get_queue(real));
	if (bdev_get_queue(real)->merge_bvec_fn &&
	    gd->queue->max_sectors > (PAGE_SIZE >> 9))
		blk_queue_max_sectors(gd->queue, PAGE_SIZE >> 9);
	blk_queue_hardsect_size(gd->queue, bdev_hardsect_size(real));

	gd->major = diaper_major;
	gd->first_minor = minor;
	{
		char buf[BDEVNAME_SIZE];
		bdevname(real, buf);
		snprintf(gd->disk_name, sizeof(gd->disk_name),
			 "diapered_%s", buf);
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
		diaper->bd_inode->i_data.backing_dev_info = &gd->queue->backing_dev_info;
	} else
		printk("GFS2: diaper: reopening\n");
	diaper->bd_openers++;
	up(&diaper->bd_sem);

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

	return dh;

 fail_mempool:
	mempool_destroy(dh->dh_mempool);

 fail_bdput:
	down(&diaper->bd_sem);
	if (!--diaper->bd_openers) {
		invalidate_bdev(diaper, 1);
		diaper->bd_contains = NULL;
		diaper->bd_disk = NULL;
	} else
		printk("GFS2: diaper: not closed\n");
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
	return ERR_PTR(error);
}

/**
 * diaper_put - Do the work of destroying a diaper device
 * @dh:
 *
 */

static void diaper_put(struct diaper_holder *dh)
{
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
		printk("GFS2: diaper: not closed\n");
	up(&diaper->bd_sem);

	bdput(diaper);
	del_gendisk(gd);
	blk_put_queue(gd->queue);
	put_disk(gd);
	kfree(dh);
	minor_put(minor);
}

/**
 * gfs2_diaper_get - Get a hold of an existing diaper or create a new one
 * @real:
 * @flags:
 *
 * Returns: the diaper device or ERR_PTR()
 */

struct block_device *gfs2_diaper_get(struct block_device *real, int flags)
{
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
			return dh->dh_diaper;
		}

		dh_new = diaper_get(real, flags);
		if (IS_ERR(dh_new))
			return (struct block_device *)dh_new;
	}
}

/**
 * gfs2_diaper_put - Drop a reference on a diaper
 * @diaper:
 *
 */

void gfs2_diaper_put(struct block_device *diaper)
{
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
			return;
		}
	}
	spin_unlock(&diaper_lock);

	printk("GFS2: diaper: unknown undiaper\n");
}

/**
 * gfs2_diaper_register_sbd -
 * @diaper:
 * @sdp:
 *
 */

void gfs2_diaper_register_sbd(struct block_device *diaper, struct gfs2_sbd *sdp)
{
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
			return;
		}
	}
	spin_unlock(&diaper_lock);

	printk("GFS2: diaper: unknown register\n");
}

/**
 * gfs2_diaper_2real -
 * @diaper:
 *
 * Returns: the real device cooresponding to the diaper
 */

struct block_device *gfs2_diaper_2real(struct block_device *diaper)
{
        struct list_head *tmp;
        struct diaper_holder *dh;

        spin_lock(&diaper_lock);
        for (tmp = diaper_list.next;
             tmp != &diaper_list;
             tmp = tmp->next) {
                dh = list_entry(tmp, struct diaper_holder, dh_list);
                if (dh->dh_diaper == diaper) {
                        spin_unlock(&diaper_lock);
			return dh->dh_real;
                }
        }
        spin_unlock(&diaper_lock);

        printk("GFS2: diaper: unknown 2real\n");
	return NULL;
}

/**
 * gfs2_diaper_init -
 *
 * Returns: errno
 */

int gfs2_diaper_init(void)
{
	spin_lock_init(&diaper_lock);

	diaper_slab = kmem_cache_create("gfs2_bio_wrapper",
					sizeof(struct bio_wrapper),
					0, 0, NULL, NULL);
	if (!diaper_slab)
		return -ENOMEM;

	diaper_major = register_blkdev(0, "gfs2_diaper");
	if (diaper_major < 0) {
		kmem_cache_destroy(diaper_slab);
		return diaper_major;
	}
	
	return 0;
}

/**
 * gfs2_diaper_uninit -
 *
 */

void gfs2_diaper_uninit(void)
{
	kmem_cache_destroy(diaper_slab);
	unregister_blkdev(diaper_major, "gfs2_diaper");
}

