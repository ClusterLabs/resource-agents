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
#include <linux/vmalloc.h>
#include <linux/statfs.h>
#include <linux/seq_file.h>
#include <linux/mount.h>

#include "gfs.h"
#include "dio.h"
#include "glock.h"
#include "inode.h"
#include "log.h"
#include "mount.h"
#include "ops_super.h"
#include "page.h"
#include "proc.h"
#include "quota.h"
#include "recovery.h"
#include "rgrp.h"
#include "super.h"

/**
 * gfs_write_inode - Make sure the inode is stable on the disk
 * @inode: The inode
 * @sync: synchronous write flag
 *
 * Returns: errno
 */

static int
gfs_write_inode(struct inode *inode, int sync)
{
	struct gfs_inode *ip = vn2ip(inode);

	atomic_inc(&ip->i_sbd->sd_ops_super);

	if (ip && sync && !gfs_in_panic)
		gfs_log_flush_glock(ip->i_gl);

	return 0;
}

/**
 * gfs_put_inode - put an inode
 * @inode: The inode
 *
 * If i_nlink is zero, any dirty data for the inode is thrown away.
 * If a process on another machine has the file open, it may need that
 * data.  So, sync it out.
 */

static void
gfs_put_inode(struct inode *inode)
{
	struct gfs_sbd *sdp = vfs2sdp(inode->i_sb);
	struct gfs_inode *ip = vn2ip(inode);

	atomic_inc(&sdp->sd_ops_super);

	if (ip &&
	    !inode->i_nlink &&
	    S_ISREG(inode->i_mode) &&
	    !sdp->sd_args.ar_localcaching)
		gfs_sync_page_i(inode, DIO_START | DIO_WAIT);
}

/**
 * gfs_put_super - Unmount the filesystem
 * @sb: The VFS superblock
 *
 */

static void
gfs_put_super(struct super_block *sb)
{
	struct gfs_sbd *sdp = vfs2sdp(sb);
	int error;

	atomic_inc(&sdp->sd_ops_super);

	gfs_proc_fs_del(sdp);

	/*  Unfreeze the filesystem, if we need to  */

	down(&sdp->sd_freeze_lock);
	if (sdp->sd_freeze_count)
		gfs_glock_dq_uninit(&sdp->sd_freeze_gh);
	up(&sdp->sd_freeze_lock);

	/*  Kill off the inode thread  */
	down(&sdp->sd_thread_lock);
	clear_bit(SDF_INODED_RUN, &sdp->sd_flags);
	wake_up_process(sdp->sd_inoded_process);
	up(&sdp->sd_thread_lock);
	wait_for_completion(&sdp->sd_thread_completion);

	/*  Kill off the quota thread  */
	down(&sdp->sd_thread_lock);
	clear_bit(SDF_QUOTAD_RUN, &sdp->sd_flags);
	wake_up_process(sdp->sd_quotad_process);
	up(&sdp->sd_thread_lock);
	wait_for_completion(&sdp->sd_thread_completion);

	/*  Kill off the log thread  */
	down(&sdp->sd_thread_lock);
	clear_bit(SDF_LOGD_RUN, &sdp->sd_flags);
	wake_up_process(sdp->sd_logd_process);
	up(&sdp->sd_thread_lock);
	wait_for_completion(&sdp->sd_thread_completion);

	/*  Kill off the recoverd thread  */
	down(&sdp->sd_thread_lock);
	clear_bit(SDF_RECOVERD_RUN, &sdp->sd_flags);
	wake_up_process(sdp->sd_recoverd_process);
	up(&sdp->sd_thread_lock);
	wait_for_completion(&sdp->sd_thread_completion);

	/*  Kill off the glockd threads  */
	clear_bit(SDF_GLOCKD_RUN, &sdp->sd_flags);
	wake_up(&sdp->sd_reclaim_wchan);
	while (sdp->sd_glockd_num--)
		wait_for_completion(&sdp->sd_thread_completion);

	/*  Kill off the scand thread  */
	down(&sdp->sd_thread_lock);
	clear_bit(SDF_SCAND_RUN, &sdp->sd_flags);
	wake_up_process(sdp->sd_scand_process);
	up(&sdp->sd_thread_lock);
	wait_for_completion(&sdp->sd_thread_completion);

	if (!test_bit(SDF_ROFS, &sdp->sd_flags)) {
		gfs_log_flush(sdp);
		gfs_quota_sync(sdp);
		gfs_quota_sync(sdp);

		error = gfs_make_fs_ro(sdp);
		if (error)
			gfs_io_error(sdp);
	}

	/*  At this point, we're through modifying the disk  */

	/*  Release stuff  */

	gfs_inode_put(sdp->sd_riinode);
	gfs_inode_put(sdp->sd_jiinode);
	gfs_inode_put(sdp->sd_rooti);
	gfs_inode_put(sdp->sd_qinode);
	gfs_inode_put(sdp->sd_linode);

	gfs_glock_put(sdp->sd_trans_gl);
	gfs_glock_put(sdp->sd_rename_gl);

	gfs_glock_dq_uninit(&sdp->sd_journal_gh);

	gfs_glock_dq_uninit(&sdp->sd_live_gh);

	/*  Get rid of rgrp bitmap structures  */
	gfs_clear_rgrpd(sdp);
	gfs_clear_journals(sdp);

	/*  Take apart glock structures and buffer lists  */
	gfs_gl_hash_clear(sdp, TRUE);

	/*  Unmount the locking protocol  */
	gfs_unmount_lockproto(sdp);

	/*  At this point, we're through participating in the lockspace  */

	gfs_clear_dirty_j(sdp);

	/*  Get rid of any extra inodes  */
	while (invalidate_inodes(sb))
		yield();

	vfree(sdp);

	vfs2sdp(sb) = NULL;
}

/**
 * gfs_write_super - disk commit all incore transactions
 * @sb: the filesystem
 *
 * This function is called every time sync(2) is called.
 * After this exits, all dirty buffers and synced.
 */

static void
gfs_write_super(struct super_block *sb)
{
	struct gfs_sbd *sdp = vfs2sdp(sb);

	atomic_inc(&sdp->sd_ops_super);

	if (!gfs_in_panic)
		gfs_log_flush(sdp);
}

/**
 * gfs_write_super_lockfs - prevent further writes to the filesystem
 * @sb: the VFS structure for the filesystem
 *
 */

static void
gfs_write_super_lockfs(struct super_block *sb)
{
	struct gfs_sbd *sdp = vfs2sdp(sb);
	int error;

	atomic_inc(&sdp->sd_ops_super);

	for (;;) {
		error = gfs_freeze_fs(sdp);
		if (!error)
			break;

		switch (error) {
		case -EBUSY:
			printk("GFS: fsid=%s: waiting for recovery before freeze\n",
			       sdp->sd_fsname);
			break;

		default:
			printk("GFS: fsid=%s: error freezing FS: %d\n",
			       sdp->sd_fsname, error);
			break;
		}

		printk("GFS: fsid=%s: retrying...\n", sdp->sd_fsname);

		current->state = TASK_UNINTERRUPTIBLE;
		schedule_timeout(HZ);
	}
}

/**
 * gfs_unlockfs - reallow writes to the filesystem
 * @sb: the VFS structure for the filesystem
 *
 */

static void
gfs_unlockfs(struct super_block *sb)
{
	struct gfs_sbd *sdp = vfs2sdp(sb);

	atomic_inc(&sdp->sd_ops_super);

	gfs_unfreeze_fs(sdp);
}

/**
 * gfs_statfs - Gather and return stats about the filesystem
 * @sb: The superblock
 * @statfsbuf: The buffer
 *
 * Returns: 0 on success or error code
 */

static int
gfs_statfs(struct super_block *sb, struct kstatfs *buf)
{
	struct gfs_sbd *sdp = vfs2sdp(sb);
	struct gfs_usage usage;
	int error;

	atomic_inc(&sdp->sd_ops_super);

	error = gfs_stat_gfs(sdp, &usage, TRUE);
	if (error)
		return error;

	memset(buf, 0, sizeof(struct kstatfs));

	buf->f_type = GFS_MAGIC;
	buf->f_bsize = usage.gu_block_size;
	buf->f_blocks = usage.gu_total_blocks;
	buf->f_bfree = usage.gu_free + usage.gu_free_dinode + usage.gu_free_meta;
	buf->f_bavail = usage.gu_free + usage.gu_free_dinode + usage.gu_free_meta;
	buf->f_files = usage.gu_used_dinode + usage.gu_free_dinode + usage.gu_free_meta + usage.gu_free;
	buf->f_ffree = usage.gu_free_dinode + usage.gu_free_meta + usage.gu_free;
	buf->f_namelen = GFS_FNAMESIZE;

	return 0;
}

/**
 * gfs_remount_fs - called when the FS is remounted
 * @sb:  the filesystem
 * @flags:  the remount flags
 * @data:  extra data passed in (not used right now)
 *
 * Returns: errno
 */

static int
gfs_remount_fs(struct super_block *sb, int *flags, char *data)
{
	struct gfs_sbd *sdp = vfs2sdp(sb);
	int error = 0;

	atomic_inc(&sdp->sd_ops_super);

	if (*flags & (MS_NOATIME | MS_NODIRATIME))
		set_bit(SDF_NOATIME, &sdp->sd_flags);
	else
		clear_bit(SDF_NOATIME, &sdp->sd_flags);

	if (*flags & MS_RDONLY) {
		if (!test_bit(SDF_ROFS, &sdp->sd_flags))
			error = gfs_make_fs_ro(sdp);
	} else if (!(*flags & MS_RDONLY) &&
		   test_bit(SDF_ROFS, &sdp->sd_flags)) {
		error = gfs_make_fs_rw(sdp);
	}

	/*  Don't let the VFS update atimes.  GFS handles this itself. */
	*flags |= MS_NOATIME | MS_NODIRATIME;

	return error;
}

/**
 * gfs_clear_inode - Deallocate an inode when VFS is done with it
 * @inode: The VFS inode
 *
 */

static void
gfs_clear_inode(struct inode *inode)
{
	struct gfs_inode *ip = vn2ip(inode);

	atomic_inc(&vfs2sdp(inode->i_sb)->sd_ops_super);

	if (ip) {
		spin_lock(&ip->i_lock);
		ip->i_vnode = NULL;
		vn2ip(inode) = NULL;
		spin_unlock(&ip->i_lock);

		gfs_glock_schedule_for_reclaim(ip->i_gl);
		gfs_inode_put(ip);
	}
}

/**
 * gfs_show_options - Show mount options for /proc/mounts
 * @s: seq_file structure
 * @mnt: vfsmount
 *
 * Returns: 0 on success or error code
 */

static int
gfs_show_options(struct seq_file *s, struct vfsmount *mnt)
{
	struct gfs_sbd *sdp = vfs2sdp(mnt->mnt_sb);
	struct gfs_args *args = &sdp->sd_args;

	atomic_inc(&sdp->sd_ops_super);

	if (args->ar_lockproto[0]) {
		seq_printf(s, ",lockproto=");
		seq_puts(s, args->ar_lockproto);
	}
	if (args->ar_locktable[0]) {
		seq_printf(s, ",locktable=");
		seq_puts(s, args->ar_locktable);
	}
	if (args->ar_hostdata[0]) {
		seq_printf(s, ",hostdata=");
		seq_puts(s, args->ar_hostdata);
	}
	if (args->ar_ignore_local_fs)
		seq_printf(s, ",ignore_local_fs");
	if (args->ar_localflocks)
		seq_printf(s, ",localflocks");
	if (args->ar_localcaching)
		seq_printf(s, ",localcaching");
	if (args->ar_upgrade)
		seq_printf(s, ",upgrade");
	if (args->ar_num_glockd != GFS_GLOCKD_DEFAULT)
		seq_printf(s, ",num_glockd=%u", args->ar_num_glockd);
	if (args->ar_posix_acls)
		seq_printf(s, ",acl");
	if (args->ar_suiddir)
		seq_printf(s, ",suiddir");

	return 0;
}

struct super_operations gfs_super_ops = {
	.write_inode = gfs_write_inode,
	.put_inode = gfs_put_inode,
	.put_super = gfs_put_super,
	.write_super = gfs_write_super,
	.write_super_lockfs = gfs_write_super_lockfs,
	.unlockfs = gfs_unlockfs,
	.statfs = gfs_statfs,
	.remount_fs = gfs_remount_fs,
	.clear_inode = gfs_clear_inode,
	.show_options = gfs_show_options,
};
