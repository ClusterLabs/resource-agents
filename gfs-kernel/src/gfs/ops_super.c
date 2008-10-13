#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/buffer_head.h>
#include <linux/vmalloc.h>
#include <linux/statfs.h>
#include <linux/seq_file.h>
#include <linux/mount.h>

#include "gfs.h"
#include "dio.h"
#include "glock.h"
#include "inode.h"
#include "lm.h"
#include "log.h"
#include "ops_fstype.h"
#include "ops_super.h"
#include "page.h"
#include "proc.h"
#include "quota.h"
#include "recovery.h"
#include "rgrp.h"
#include "super.h"
#include "sys.h"
#include "mount.h"

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
	struct gfs_inode *ip = get_v2ip(inode);

	atomic_inc(&ip->i_sbd->sd_ops_super);

	if (ip && sync)
		gfs_log_flush_glock(ip->i_gl);

	return 0;
}

/**
 * gfs_drop_inode - drop an inode
 * @inode: The inode
 *
 * If i_nlink is zero, any dirty data for the inode is thrown away.
 * If a process on another machine has the file open, it may need that
 * data.  So, sync it out.
 */

static void
gfs_drop_inode(struct inode *inode)
{
	struct gfs_sbd *sdp = get_v2sdp(inode->i_sb);
	struct gfs_inode *ip = get_v2ip(inode);

	atomic_inc(&sdp->sd_ops_super);

	if (ip &&
	    !inode->i_nlink &&
	    S_ISREG(inode->i_mode) &&
	    !sdp->sd_args.ar_localcaching)
		gfs_sync_page_i(inode, DIO_START | DIO_WAIT);
	generic_drop_inode(inode);
}

/**
 * gfs_put_super - Unmount the filesystem
 * @sb: The VFS superblock
 *
 */

static void
gfs_put_super(struct super_block *sb)
{
	struct gfs_sbd *sdp = get_v2sdp(sb);
	int error;

        if (!sdp)
                return;

	atomic_inc(&sdp->sd_ops_super);

	gfs_proc_fs_del(sdp);

	/*  Unfreeze the filesystem, if we need to  */

	down(&sdp->sd_freeze_lock);
	if (sdp->sd_freeze_count)
		gfs_glock_dq_uninit(&sdp->sd_freeze_gh);
	up(&sdp->sd_freeze_lock);

	/*  Kill off the inode thread  */
	kthread_stop(sdp->sd_inoded_process);

	/*  Kill off the quota thread  */
	kthread_stop(sdp->sd_quotad_process);

	/*  Kill off the log thread  */
	kthread_stop(sdp->sd_logd_process);

	/*  Kill off the recoverd thread  */
	kthread_stop(sdp->sd_recoverd_process);

	/*  Kill off the glockd threads  */
	while (sdp->sd_glockd_num--)
		kthread_stop(sdp->sd_glockd_process[sdp->sd_glockd_num]);

	/*  Kill off the scand thread  */
	kthread_stop(sdp->sd_scand_process);

	if (!test_bit(SDF_ROFS, &sdp->sd_flags)) {
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

	if (!sdp->sd_args.ar_spectator)
		gfs_glock_dq_uninit(&sdp->sd_journal_gh);

	gfs_glock_dq_uninit(&sdp->sd_live_gh);

	/*  Get rid of rgrp bitmap structures  */
	gfs_clear_rgrpd(sdp);
	gfs_clear_journals(sdp);

	/*  Take apart glock structures and buffer lists  */
	gfs_gl_hash_clear(sdp, TRUE);

	/*  Unmount the locking protocol  */
	gfs_lm_unmount(sdp);

	/*  At this point, we're through participating in the lockspace  */

	gfs_sys_fs_del(sdp);

	gfs_clear_dirty_j(sdp);

	/*  Get rid of any extra inodes  */
	while (invalidate_inodes(sb))
		yield();

	vfree(sdp);

	set_v2sdp(sb, NULL);
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
	struct gfs_sbd *sdp = get_v2sdp(sb);
	atomic_inc(&sdp->sd_ops_super);
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
	struct gfs_sbd *sdp = get_v2sdp(sb);
	int error;

	if (test_bit(SDF_SHUTDOWN, &sdp->sd_flags))
		return;

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

		set_current_state(TASK_UNINTERRUPTIBLE);
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
	struct gfs_sbd *sdp = get_v2sdp(sb);

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

static int gfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_inode->i_sb;
	struct gfs_sbd *sdp = get_v2sdp(sb);
	struct gfs_stat_gfs sg;
	int error;

	atomic_inc(&sdp->sd_ops_super);

	if (gfs_tune_get(sdp, gt_statfs_fast))
		return(gfs_statfs_fast(sdp, (void *)buf));

	error = gfs_stat_gfs(sdp, &sg, TRUE);
	if (error)
		return error;

	memset(buf, 0, sizeof(struct kstatfs));

	buf->f_type = GFS_MAGIC;
	buf->f_bsize = sdp->sd_sb.sb_bsize;
	buf->f_blocks = sg.sg_total_blocks;
	buf->f_bfree = sg.sg_free + sg.sg_free_dinode + sg.sg_free_meta;
	buf->f_bavail = sg.sg_free + sg.sg_free_dinode + sg.sg_free_meta;
	buf->f_files = sg.sg_used_dinode + sg.sg_free_dinode +
		sg.sg_free_meta + sg.sg_free;
	buf->f_ffree = sg.sg_free_dinode + sg.sg_free_meta + sg.sg_free;
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
	struct gfs_sbd *sdp = get_v2sdp(sb);
	struct gfs_tune *gt = &sdp->sd_tune;
	int error = 0;
	struct gfs_args *args;

	atomic_inc(&sdp->sd_ops_super);

	args = kmalloc(sizeof(struct gfs_args), GFP_KERNEL);
	if (!args)
		return -ENOMEM;

	error = gfs_make_args(data, args, TRUE);
	if (error) {
		printk("GFS: can't parse remount arguments\n");
		goto out;
	}
	if (args->ar_posix_acls) {
		sdp->sd_args.ar_posix_acls = TRUE;
		sb->s_flags |= MS_POSIXACL;
	}
	else {
		sdp->sd_args.ar_posix_acls = FALSE;
		sb->s_flags &= ~MS_POSIXACL;
	}

	if (*flags & (MS_NOATIME | MS_NODIRATIME))
		set_bit(SDF_NOATIME, &sdp->sd_flags);
	else
		clear_bit(SDF_NOATIME, &sdp->sd_flags);

	if (sdp->sd_args.ar_spectator)
		*flags |= MS_RDONLY;
	else {
		if (*flags & MS_RDONLY) {
			if (!test_bit(SDF_ROFS, &sdp->sd_flags))
				error = gfs_make_fs_ro(sdp);
		} else if (!(*flags & MS_RDONLY) &&
			   test_bit(SDF_ROFS, &sdp->sd_flags)) {
			error = gfs_make_fs_rw(sdp);
		}
	}

	if (args->ar_noquota) {
		if (sdp->sd_args.ar_noquota == FALSE)
			printk("GFS: remounting without quota\n");
		sdp->sd_args.ar_noquota = TRUE;
		spin_lock(&gt->gt_spin);
		gt->gt_quota_enforce = 0;
		gt->gt_quota_account = 0;
		spin_unlock(&gt->gt_spin);
	}
	else {
		if (sdp->sd_args.ar_noquota == TRUE)
			printk("GFS: remounting with quota\n");
		sdp->sd_args.ar_noquota = FALSE;
		spin_lock(&gt->gt_spin);
		gt->gt_quota_enforce = 1;
		gt->gt_quota_account = 1;
		spin_unlock(&gt->gt_spin);
	}

	/*  Don't let the VFS update atimes.  GFS handles this itself. */
	*flags |= MS_NOATIME | MS_NODIRATIME;

out:
	kfree(args);
	return error;
}

/**
 * gfs_clear_inode - Deallocate an inode when VFS is done with it
 * @inode: The VFS inode
 *
 * If there's a GFS incore inode structure attached to the VFS inode:
 * --  Detach them from one another.
 * --  Schedule reclaim of GFS inode struct, the glock protecting it, and
 *     the associated iopen glock.
 */

static void
gfs_clear_inode(struct inode *inode)
{
	struct gfs_inode *ip = get_v2ip(inode);

	atomic_inc(&get_v2sdp(inode->i_sb)->sd_ops_super);

	if (ip) {
		spin_lock(&ip->i_spin);
		ip->i_vnode = NULL;
		set_v2ip(inode, NULL);
		spin_unlock(&ip->i_spin);

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
	struct gfs_sbd *sdp = get_v2sdp(mnt->mnt_sb);
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
	if (args->ar_spectator)
		seq_printf(s, ",spectator");
	if (args->ar_ignore_local_fs)
		seq_printf(s, ",ignore_local_fs");
	if (args->ar_localflocks)
		seq_printf(s, ",localflocks");
	if (args->ar_localcaching)
		seq_printf(s, ",localcaching");
	if (args->ar_oopses_ok)
		seq_printf(s, ",oopses_ok");
	if (args->ar_debug)
		seq_printf(s, ",debug");
	if (args->ar_upgrade)
		seq_printf(s, ",upgrade");
	if (args->ar_num_glockd != GFS_GLOCKD_DEFAULT)
		seq_printf(s, ",num_glockd=%u", args->ar_num_glockd);
	if (args->ar_posix_acls)
		seq_printf(s, ",acl");
	if (args->ar_noquota)
		seq_printf(s, ",noquota");
	if (args->ar_suiddir)
		seq_printf(s, ",suiddir");

	return 0;
}

struct super_operations gfs_super_ops = {
	.write_inode = gfs_write_inode,
	.drop_inode = gfs_drop_inode,
	.put_super = gfs_put_super,
	.write_super = gfs_write_super,
	.write_super_lockfs = gfs_write_super_lockfs,
	.unlockfs = gfs_unlockfs,
	.statfs = gfs_statfs,
	.remount_fs = gfs_remount_fs,
	.clear_inode = gfs_clear_inode,
	.show_options = gfs_show_options,
};
