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
#include <asm/uaccess.h>

#include "gfs.h"
#include "bmap.h"
#include "dio.h"
#include "dir.h"
#include "eattr.h"
#include "file.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "ioctl.h"
#include "quota.h"
#include "rgrp.h"
#include "super.h"
#include "trans.h"

/**
 * gfs_add_bh_to_ub - copy a buffer up to user space
 * @ub: the structure representing where to copy
 * @bh: the buffer
 *
 * Returns: errno
 */

int
gfs_add_bh_to_ub(struct gfs_user_buffer *ub, struct buffer_head *bh)
{
	uint64_t blkno = bh->b_blocknr;

	if (ub->ub_count + sizeof(uint64_t) + bh->b_size > ub->ub_size)
		return -ENOMEM;

	if (copy_to_user(ub->ub_data + ub->ub_count,
			 &blkno,
			 sizeof(uint64_t)))
		return -EFAULT;
	ub->ub_count += sizeof(uint64_t);

	if (copy_to_user(ub->ub_data + ub->ub_count,
			 bh->b_data,
			 bh->b_size))
		return -EFAULT;
	ub->ub_count += bh->b_size;

	return 0;
}

/**
 * get_meta - Read out all the metadata for a file
 * @ip: the file
 *
 * Returns: errno
 */

static int
get_meta(struct gfs_inode *ip, void *arg)
{
	struct gfs_holder i_gh;
	struct gfs_user_buffer ub;
	int error;

	if (copy_from_user(&ub, arg, sizeof(struct gfs_user_buffer)))
		return -EFAULT;
	ub.ub_count = 0;

	error = gfs_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY, &i_gh);
	if (error)
		return error;

	error = gfs_get_file_meta(ip, &ub);
	if (error)
		goto out;

	if (ip->i_di.di_type == GFS_FILE_DIR &&
	    (ip->i_di.di_flags & GFS_DIF_EXHASH)) {
		error = gfs_get_dir_meta(ip, &ub);
		if (error)
			goto out;
	}

	if (ip->i_di.di_eattr) {
		error = gfs_get_eattr_meta(ip, &ub);
		if (error)
			goto out;
	}

	if (copy_to_user(arg, &ub, sizeof(struct gfs_user_buffer)))
		error = -EFAULT;

 out:
	gfs_glock_dq_uninit(&i_gh);

	return error;
}

/**
 * file_stat - return the struct gfs_dinode of a file to user space
 * @ip: the inode
 * @arg: where to copy to
 *
 * Returns: errno
 */

static int
file_stat(struct gfs_inode *ip, void *arg)
{
	struct gfs_holder i_gh;
	struct gfs_dinode di;
	int error;

	error = gfs_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY, &i_gh);
	if (error)
		return error;

	memcpy(&di, &ip->i_di, sizeof(struct gfs_dinode));

	gfs_glock_dq_uninit(&i_gh);

	if (copy_to_user(arg, &di, sizeof(struct gfs_dinode)))
		return -EFAULT;

	return 0;
}

/**
 * do_get_super - Dump the superblock into a buffer
 * @sb: The superblock
 * @ptr: The buffer pointer
 *
 * Returns: 0 or error code
 */

static int
do_get_super(struct gfs_sbd *sdp, void *arg)
{
	struct gfs_sb *sb;
	struct gfs_holder sb_gh;
	struct buffer_head *bh;
	int error;

	sb = kmalloc(sizeof(struct gfs_sb), GFP_KERNEL);
	if (!sb)
		return -ENOMEM;

	error = gfs_glock_nq_num(sdp,
				 GFS_SB_LOCK, &gfs_meta_glops,
				 LM_ST_SHARED, 0, &sb_gh);
	if (error)
		goto out;

	error = gfs_dread(sdp, GFS_SB_ADDR >> sdp->sd_fsb2bb_shift, sb_gh.gh_gl,
			  DIO_START | DIO_WAIT, &bh);
	if (error) {
		gfs_glock_dq_uninit(&sb_gh);
		goto out;
	}

	gfs_sb_in(sb, bh->b_data);
	brelse(bh);

	gfs_glock_dq_uninit(&sb_gh);

	if (copy_to_user(arg, sb, sizeof(struct gfs_sb)))
		error = -EFAULT;

 out:
	kfree(sb);

	return error;
}

/**
 * jt2ip - convert the file type in a jio struct to the right hidden ip
 * @sdp: the filesystem
 * @jt: the gfs_jio_structure
 *
 * Returns: The inode structure for the correct hidden file
 */

static struct gfs_inode *
jt2ip(struct gfs_sbd *sdp, struct gfs_jio *jt)
{
	struct gfs_inode *ip = NULL;

	switch (jt->jio_file) {
	case GFS_HIDDEN_JINDEX:
		ip = sdp->sd_jiinode;
		break;

	case GFS_HIDDEN_RINDEX:
		ip = sdp->sd_riinode;
		break;

	case GFS_HIDDEN_QUOTA:
		ip = sdp->sd_qinode;
		break;

	case GFS_HIDDEN_LICENSE:
		ip = sdp->sd_linode;
		break;
	}

	return ip;
}

/**
 * jread_ioctl - Read from a journaled data file via ioctl
 * @sdp: the filesystem
 * @arg: The argument from ioctl
 *
 * Returns: Amount of data copied or error
 */

static int
jread_ioctl(struct gfs_sbd *sdp, void *arg)
{
	struct gfs_jio jt;
	struct gfs_inode *ip;
	struct gfs_holder i_gh;
	int error;

	if (copy_from_user(&jt, arg, sizeof(struct gfs_jio)))
		return -EFAULT;

	ip = jt2ip(sdp, &jt);
	if (!ip)
		return -EINVAL;

	GFS_ASSERT_INODE(gfs_is_jdata(ip), ip,);

	if (!access_ok(VERIFY_WRITE, jt.jio_data, jt.jio_size))
		return -EFAULT;

	error = gfs_glock_nq_init(ip->i_gl, LM_ST_SHARED, 0, &i_gh);
	if (error)
		return error;

	error = gfs_readi(ip, jt.jio_data, jt.jio_offset, jt.jio_size,
			  gfs_copy2user);

	gfs_glock_dq_uninit(&i_gh);

	if (error < 0)
		return error;
	jt.jio_count = error;

	if (copy_to_user(arg, &jt, sizeof(struct gfs_jio)))
		return -EFAULT;

	return 0;
}

/**
 * jwrite_ioctl - Write to a journaled file via ioctl
 * @sdp: the filesystem
 * @arg: The argument from ioctl
 *
 * Returns: Amount of data copied or error
 */

static int
jwrite_ioctl(struct gfs_sbd *sdp, void *arg)
{
	struct gfs_jio jt;
	struct gfs_inode *ip;
	struct gfs_alloc *al = NULL;
	struct gfs_holder i_gh;
	unsigned int data_blocks, ind_blocks;
	int alloc_required;
	int error;

	if (copy_from_user(&jt, arg, sizeof(struct gfs_jio)))
		return -EFAULT;

	ip = jt2ip(sdp, &jt);
	if (!ip)
		return -EINVAL;

	GFS_ASSERT_INODE(gfs_is_jdata(ip), ip,);

	if (!access_ok(VERIFY_READ, jt.jio_data, jt.jio_size))
		return -EFAULT;

	gfs_write_calc_reserv(ip, jt.jio_size, &data_blocks, &ind_blocks);

	error = gfs_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE,
				  LM_FLAG_PRIORITY | GL_SYNC, &i_gh);
	if (error)
		return error;

	error = gfs_write_alloc_required(ip, jt.jio_offset, jt.jio_size,
					 &alloc_required);
	if (error)
		goto out;

	if (alloc_required) {
		al = gfs_alloc_get(ip);

		error = gfs_quota_hold_m(ip, NO_QUOTA_CHANGE,
					 NO_QUOTA_CHANGE);
		if (error)
			goto out_alloc;

		al->al_requested_meta = ind_blocks + data_blocks;

		error = gfs_inplace_reserve(ip);
		if (error)
			goto out_qs;

		/* Trans may require:
		   All blocks for a RG bitmap, all the "data" blocks, whatever
		   indirect blocks we need, a modified dinode, and a quota change */

		error = gfs_trans_begin(sdp,
					1 + al->al_rgd->rd_ri.ri_length +
					ind_blocks + data_blocks, 1);
		if (error)
			goto out_relse;
	} else {
		/* Trans may require:
		   All the "data" blocks and a modified dinode. */

		error = gfs_trans_begin(sdp, 1 + data_blocks, 0);
		if (error)
			goto out_relse;
	}

	error = gfs_writei(ip, jt.jio_data, jt.jio_offset, jt.jio_size,
			   gfs_copy_from_user);
	if (error >= 0) {
		jt.jio_count = error;
		error = 0;
	}
	
	gfs_trans_end(sdp);
	
 out_relse:
	if (alloc_required) {
		GFS_ASSERT_INODE(error || al->al_alloced_meta, ip,);
		gfs_inplace_release(ip);
	}

 out_qs:
	if (alloc_required)
		gfs_quota_unhold_m(ip);

 out_alloc:
	if (alloc_required)
		gfs_alloc_put(ip);

 out:
	ip->i_gl->gl_vn++;
	gfs_glock_dq_uninit(&i_gh);

	if (!error && copy_to_user(arg, &jt, sizeof(struct gfs_jio)))
		return -EFAULT;

	return error;
}

/**
 * jstat_ioctl - Stat to a journaled file via ioctl
 * @sdp: the filesystem
 * @arg: The argument from ioctl
 *
 * Returns: errno
 */

static int
jstat_ioctl(struct gfs_sbd *sdp, void *arg)
{
	struct gfs_jio jt;
	struct gfs_inode *ip;
	struct gfs_holder i_gh;
	int error;

	if (copy_from_user(&jt, arg, sizeof(struct gfs_jio)))
	    return -EFAULT;

	ip = jt2ip(sdp, &jt);
	if (!ip)
		return -EINVAL;

	if (jt.jio_size < sizeof(struct gfs_dinode))
		return -EINVAL;

	error = gfs_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY, &i_gh);
	if (error)
		return error;

	error = copy_to_user(jt.jio_data, &ip->i_di, sizeof(struct gfs_dinode));

	gfs_glock_dq_uninit(&i_gh);

	if (error)
		return -EFAULT;

	return 0;
}

/**
 * jtrunc_ioctl - Truncate to a journaled file via ioctl
 * @sdp: the filesystem
 * @arg: The argument from ioctl
 *
 * Returns: errno
 */

static int
jtrunc_ioctl(struct gfs_sbd *sdp, void *arg)
{
	struct gfs_jio jt;
	struct gfs_inode *ip;
	struct gfs_holder i_gh;
	int error;

	if (copy_from_user(&jt, arg, sizeof(struct gfs_jio)))
	    return -EFAULT;

	ip = jt2ip(sdp, &jt);
	if (!ip)
		return -EINVAL;

	error = gfs_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, GL_SYNC, &i_gh);
	if (error)
		return error;

	error = gfs_truncatei(ip, jt.jio_offset, NULL);

	ip->i_gl->gl_vn++;
	gfs_glock_dq_uninit(&i_gh);

	return error;
}

/**
 * lock_dump - copy out info about the GFS' lock space
 * @sdp: the filesystem
 * @arg: a pointer to a struct gfs_user_buffer in user space
 *
 * Returns: errno
 */

static int
lock_dump(struct gfs_sbd *sdp, void *arg)
{
	struct gfs_user_buffer ub;
	int error;

	if (copy_from_user(&ub, arg, sizeof(struct gfs_user_buffer)))
		return -EFAULT;
	ub.ub_count = 0;

	error = gfs_dump_lockstate(sdp, &ub);
	if (error)
		return error;

	if (copy_to_user(arg, &ub, sizeof(struct gfs_user_buffer)))
		return -EFAULT;

	return 0;
}

/**
 * stat_gfs_ioctl - Do a GFS specific statfs
 * @sdp: the filesystem
 * @arg: the struct gfs_usage structure
 *
 * Returns: errno
 */

static int
stat_gfs_ioctl(struct gfs_sbd *sdp, void *arg)
{
	struct gfs_usage *u;
	int error;

	u = kmalloc(sizeof(struct gfs_usage), GFP_KERNEL);
	if (!u)
		return -ENOMEM;

	error = gfs_stat_gfs(sdp, u, TRUE);
	if (!error && copy_to_user(arg, u, sizeof(struct gfs_usage)))
		return -EFAULT;

	kfree(u);

	return error;
}

/**
 * reclaim_ioctl - ioctl called to perform metadata reclaimation
 * @sdp: the filesystem
 * @arg: a pointer to a struct gfs_reclaim_stats in user space
 *
 * Returns: errno
 */

static int
reclaim_ioctl(struct gfs_sbd *sdp, void *arg)
{
	struct gfs_reclaim_stats stats;
	int error;

	memset(&stats, 0, sizeof(struct gfs_reclaim_stats));

	error = gfs_reclaim_metadata(sdp, &stats);
	if (error)
		return error;

	if (copy_to_user(arg, &stats, sizeof(struct gfs_reclaim_stats)))
		return -EFAULT;

	return 0;
}

/**
 * get_tune - pass the current tuneable parameters up to user space
 * @sdp: the filesystem
 * @arg: a pointer to a struct gfs_tune in user space
 *
 * Returns: errno
 */

static int
get_tune(struct gfs_sbd *sdp, void *arg)
{
	if (copy_to_user(arg, &sdp->sd_tune, sizeof(struct gfs_tune)))
		return -EFAULT;

	return 0;
}

/**
 * set_tune - replace the current tuneable parameters with a set from user space
 * @sdp: the filesystem
 * @arg: a pointer to a struct gfs_tune in user space
 *
 * Returns: errno
 */

static int
set_tune(struct gfs_sbd *sdp, void *arg)
{
	struct gfs_tune *gt;
	int error = 0;

	gt = kmalloc(sizeof(struct gfs_tune), GFP_KERNEL);
	if (!gt)
		return -ENOMEM;

	if (copy_from_user(gt, arg, sizeof(struct gfs_tune)))
		error = -EFAULT;
	else {
		if (gt->gt_tune_version != GFS_TUNE_VERSION) {
			printk("GFS: fsid=%s: invalid version of tuneable parameters\n",
			       sdp->sd_fsname);
			error = -EINVAL;
		} else
			memcpy(&sdp->sd_tune, gt, sizeof(struct gfs_tune));
	}

	kfree(gt);

	return error;
}

/**
 * gfs_set_flag - set/clear a flag on an inode
 * @ip: the inode
 * @cmd: GFS_SET_FLAG or GFS_CLEAR_FLAG
 * @arg: the flag to change (in user space)
 *
 * Returns: errno
 */

static int
gfs_set_flag(struct gfs_inode *ip, unsigned int cmd, void *arg)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_holder i_gh;
	struct buffer_head *dibh;
	uint32_t flag;
	int error;

	if (copy_from_user(&flag, arg, sizeof(uint32_t)))
		return -EFAULT;

	error = gfs_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &i_gh);
	if (error)
		return error;

	error = -EACCES;
	if (ip->i_di.di_uid != current->fsuid && !capable(CAP_FOWNER))
		goto out;

	error = -EINVAL;

	switch (flag) {
	case GFS_DIF_EXHASH:
	case GFS_DIF_UNUSED:
	case GFS_DIF_EA_INDIRECT:
		goto out;

	case GFS_DIF_JDATA:
		if (ip->i_di.di_type != GFS_FILE_REG || ip->i_di.di_size)
			goto out;
		break;

	case GFS_DIF_DIRECTIO:
		if (ip->i_di.di_type != GFS_FILE_REG)
			goto out;
		break;

	case GFS_DIF_IMMUTABLE:
	case GFS_DIF_APPENDONLY:
        	/* The IMMUTABLE and APPENDONLY flags can only be changed by
		   the relevant capability. */
		if (((ip->i_di.di_flags ^ flag) & (GFS_DIF_IMMUTABLE | GFS_DIF_APPENDONLY)) &&
		    !capable(CAP_LINUX_IMMUTABLE)) {
			error = -EPERM;
			goto out;
		}
		break;

	case GFS_DIF_NOATIME:
	case GFS_DIF_SYNC:
		/*  FixMe!!!  */
		error = -ENOSYS;
		goto out;

	case GFS_DIF_INHERIT_DIRECTIO:
	case GFS_DIF_INHERIT_JDATA:
		if (ip->i_di.di_type != GFS_FILE_DIR)
			goto out;
		break;

	default:
		goto out;
	}

	error = gfs_trans_begin(sdp, 1, 0);
	if (error)
		goto out;

	error = gfs_get_inode_buffer(ip, &dibh);
	if (error)
		goto out_trans_end;

	if (cmd == GFS_SET_FLAG)
		ip->i_di.di_flags |= flag;
	else
		ip->i_di.di_flags &= ~flag;

	gfs_trans_add_bh(ip->i_gl, dibh);
	gfs_dinode_out(&ip->i_di, dibh->b_data);

	brelse(dibh);

 out_trans_end:
	gfs_trans_end(sdp);

 out:
	gfs_glock_dq_uninit(&i_gh);

	return error;
}

/**
 * handle_roll - Read a atomic_t as an unsigned int
 * @a: a counter
 *
 * if @a is negative, reset it to zero
 *
 * Returns: the value of the counter
 */

static unsigned int
handle_roll(atomic_t *a)
{
	int x = atomic_read(a);
	if (x < 0) {
		atomic_set(a, 0);
		return 0;
	}
	return (unsigned int)x;
}

/**
 * fill_counters - Write a FS' counters into a buffer
 * @sdp: the filesystem
 * @buf: the buffer
 * @size: the size of the buffer
 * @count: where we are in the buffer
 *
 * Returns: errno
 */

static int
fill_counters(struct gfs_sbd *sdp,
	      char *buf, unsigned int size, unsigned int *count)
{
	int error = 0;

	gfs_sprintf("sd_glock_count:locks::%d\n",
		    atomic_read(&sdp->sd_glock_count));
	gfs_sprintf("sd_glock_held_count:locks held::%d\n",
		    atomic_read(&sdp->sd_glock_held_count));
	gfs_sprintf("sd_inode_count:incore inodes::%d\n",
		    atomic_read(&sdp->sd_inode_count));
	gfs_sprintf("sd_bufdata_count:metadata buffers::%d\n",
		    atomic_read(&sdp->sd_bufdata_count));
	gfs_sprintf("sd_unlinked_ic_count:unlinked inodes::%d\n",
		    atomic_read(&sdp->sd_unlinked_ic_count));
	gfs_sprintf("sd_quota_count:quota IDs::%d\n",
		    atomic_read(&sdp->sd_quota_count));
	gfs_sprintf("sd_log_buffers:incore log buffers::%u\n",
		    sdp->sd_log_buffers);
	gfs_sprintf("sd_log_seg_free:log segments free::%u\n",
		    sdp->sd_log_seg_free);
	gfs_sprintf("ji_nsegment:log segments total::%u\n",
		    sdp->sd_jdesc.ji_nsegment);
	gfs_sprintf("sd_mhc_count:meta header cache entries::%d\n",
		    atomic_read(&sdp->sd_mhc_count));
	gfs_sprintf("sd_depend_count:glock dependencies::%d\n",
		    atomic_read(&sdp->sd_depend_count));
	gfs_sprintf("sd_reclaim_count:glocks on reclaim list::%d\n",
		    atomic_read(&sdp->sd_reclaim_count));
	gfs_sprintf("sd_log_wrap:log wraps::%"PRIu64"\n",
		    sdp->sd_log_wrap);
	gfs_sprintf("sd_fh2dentry_misses:fh2dentry misses:diff:%u\n",
		    handle_roll(&sdp->sd_fh2dentry_misses));
	gfs_sprintf("sd_reclaimed:glocks reclaimed:diff:%u\n",
		    handle_roll(&sdp->sd_reclaimed));
	gfs_sprintf("sd_glock_nq_calls:glock nq calls:diff:%u\n",
		    handle_roll(&sdp->sd_glock_nq_calls));
	gfs_sprintf("sd_glock_dq_calls:glock dq calls:diff:%u\n",
		    handle_roll(&sdp->sd_glock_dq_calls));
	gfs_sprintf("sd_glock_prefetch_calls:glock prefetch calls:diff:%u\n",
		    handle_roll(&sdp->sd_glock_prefetch_calls));
	gfs_sprintf("sd_lm_lock_calls:lm_lock calls:diff:%u\n",
		    handle_roll(&sdp->sd_lm_lock_calls));
	gfs_sprintf("sd_lm_unlock_calls:lm_unlock calls:diff:%u\n",
		    handle_roll(&sdp->sd_lm_unlock_calls));
	gfs_sprintf("sd_lm_callbacks:lm callbacks:diff:%u\n",
		    handle_roll(&sdp->sd_lm_callbacks));
	gfs_sprintf("sd_ops_address:address operations:diff:%u\n",
		    handle_roll(&sdp->sd_ops_address));
	gfs_sprintf("sd_ops_dentry:dentry operations:diff:%u\n",
		    handle_roll(&sdp->sd_ops_dentry));
	gfs_sprintf("sd_ops_export:export operations:diff:%u\n",
		    handle_roll(&sdp->sd_ops_export));
	gfs_sprintf("sd_ops_file:file operations:diff:%u\n",
		    handle_roll(&sdp->sd_ops_file));
	gfs_sprintf("sd_ops_inode:inode operations:diff:%u\n",
		    handle_roll(&sdp->sd_ops_inode));
	gfs_sprintf("sd_ops_super:super operations:diff:%u\n",
		    handle_roll(&sdp->sd_ops_super));
	gfs_sprintf("sd_ops_vm:vm operations:diff:%u\n",
		    handle_roll(&sdp->sd_ops_vm));

 out:
	return error;
}

/**
 * get_counters - return usage counters to user space
 * @sdp: the filesystem
 * @arg: the counter structure to fill
 *
 * Returns: errno
 */

static int
get_counters(struct gfs_sbd *sdp, void *arg)
{
	struct gfs_user_buffer ub;
	unsigned int size = sdp->sd_tune.gt_lockdump_size;
	char *buf;
	int error;

	if (copy_from_user(&ub, arg, sizeof(struct gfs_user_buffer)))
		return -EFAULT;
	ub.ub_count = 0;

	if (size > ub.ub_size)
		size = ub.ub_size;

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	error = fill_counters(sdp, buf, size, &ub.ub_count);
	if (!error) {
		if (copy_to_user(ub.ub_data, buf, ub.ub_count) ||
		    copy_to_user(arg, &ub, sizeof(struct gfs_user_buffer)))
			error = -EFAULT;
	}

	kfree(buf);

	return error;
}

/**
 * gfs_ioctli - filesystem independent ioctl function
 * @ip: the inode the ioctl was on
 * @cmd: the ioctl number
 * @arg: the argument (still in user space)
 *
 * Returns: errno
 */

int
gfs_ioctli(struct gfs_inode *ip, unsigned int cmd, void *arg)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	int error = 0;

	switch (cmd) {
	case GFS_GET_META:
		error = get_meta(ip, arg);
		break;

	case GFS_FILE_STAT:
		error = file_stat(ip, arg);
		break;

	case GFS_SHRINK:
		if (capable(CAP_SYS_ADMIN))
			gfs_gl_hash_clear(sdp, FALSE);
		else
			error = -EACCES;
		break;

	case GFS_GET_ARGS:
		if  (copy_to_user(arg, &sdp->sd_args,
				  sizeof(struct gfs_args)))
			error = -EFAULT;
		break;

	case GFS_GET_LOCKSTRUCT:
		if (copy_to_user(arg, &sdp->sd_lockstruct,
				 sizeof(struct lm_lockstruct)))
			error = -EFAULT;
		break;

	case GFS_GET_SUPER:
		error = do_get_super(sdp, arg);
		break;

	case GFS_JREAD:
		if (capable(CAP_SYS_ADMIN))
			error = jread_ioctl(sdp, arg);
		else
			error = -EACCES;
		break;

	case GFS_JWRITE:
		if (capable(CAP_SYS_ADMIN))
			error = jwrite_ioctl(sdp, arg);
		else
			error = -EACCES;
		break;

	case GFS_JSTAT:
		error = jstat_ioctl(sdp, arg);
		break;

	case GFS_JTRUNC:
		if (capable(CAP_SYS_ADMIN))
			error = jtrunc_ioctl(sdp, arg);
		else
			error = -EACCES;
		break;

	case GFS_LOCK_DUMP:
		if (capable(CAP_SYS_ADMIN))
			error = lock_dump(sdp, arg);
		else
			error = -EACCES;
		break;

	case GFS_STATGFS:
		error = stat_gfs_ioctl(sdp, arg);
		break;

	case GFS_RECLAIM_METADATA:
		if (capable(CAP_SYS_ADMIN))
			error = reclaim_ioctl(sdp, arg);
		else
			error = -EACCES;
		break;

	case GFS_QUOTA_SYNC:
		if (capable(CAP_SYS_ADMIN))
			error = gfs_quota_sync(sdp);
		else
			error = -EACCES;
		break;

	case GFS_QUOTA_REFRESH:
		if (capable(CAP_SYS_ADMIN))
			error = gfs_quota_refresh(sdp, arg);
		else
			error = -EACCES;
		break;

	case GFS_QUOTA_READ:
		/*  Permissions handled later  */
		error = gfs_quota_read(sdp, arg);
		break;

	case GFS_GET_TUNE:
		error = get_tune(sdp, arg);
		break;

	case GFS_SET_TUNE:
		if (capable(CAP_SYS_ADMIN))
			error = set_tune(sdp, arg);
		else
			error = -EACCES;
		break;

	case GFS_WHERE_ARE_YOU: {
		unsigned int x = GFS_MAGIC;
		if (copy_to_user(arg, &x, sizeof(unsigned int)))
			error = -EFAULT;
		break;
	}

	case GFS_COOKIE: {
		unsigned long x = (unsigned long)sdp;
		if (copy_to_user(arg, &x, sizeof(unsigned long)))
			error = -EFAULT;
		break;
	}

	case GFS_SET_FLAG:
	case GFS_CLEAR_FLAG:
		/*  Permissions handled later  */
		error = gfs_set_flag(ip, cmd, arg);
		break;

	case GFS_GET_COUNTERS:
		error = get_counters(sdp, arg);
		break;

	case GFS_FILE_FLUSH:
		gfs_glock_force_drop(ip->i_gl);
		break;

	default:
		error = -ENOTTY;
		break;
	}

	return error;
}
