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
#include <linux/blkdev.h>

#include "gfs.h"
#include "daemon.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "locking.h"
#include "mount.h"
#include "ops_export.h"
#include "ops_fstype.h"
#include "ops_super.h"
#include "quota.h"
#include "recovery.h"
#include "rgrp.h"
#include "super.h"
#include "unlinked.h"

/**
 * gfs_read_super - Read in superblock
 * @sb: The VFS superblock
 * @data: Mount options
 * @silent: Don't complain if its not a GFS filesystem
 *
 * Returns: The VFS superblock, or NULL on error
 */

static int
fill_super(struct super_block *sb, void *data, int silent)
{
	struct gfs_sbd *sdp;
	struct gfs_holder mount_gh, sb_gh, ji_gh;
	struct inode *inode;
	int super = TRUE, jindex = TRUE;
	unsigned int x;
	int error;

	error = -ENOMEM;
	sdp = vmalloc(sizeof(struct gfs_sbd));
	if (!sdp)
		goto fail;

	memset(sdp, 0, sizeof(struct gfs_sbd));

	vfs2sdp(sb) = sdp;
	sdp->sd_vfs = sb;

	/*  Init rgrp variables  */

	INIT_LIST_HEAD(&sdp->sd_rglist);
	init_MUTEX(&sdp->sd_rindex_lock);
	INIT_LIST_HEAD(&sdp->sd_rg_mru_list);
	spin_lock_init(&sdp->sd_rg_mru_lock);
	INIT_LIST_HEAD(&sdp->sd_rg_recent);
	spin_lock_init(&sdp->sd_rg_recent_lock);
	spin_lock_init(&sdp->sd_rg_forward_lock);

	for (x = 0; x < GFS_GL_HASH_SIZE; x++) {
		sdp->sd_gl_hash[x].hb_lock = RW_LOCK_UNLOCKED;
		INIT_LIST_HEAD(&sdp->sd_gl_hash[x].hb_list);
	}

	INIT_LIST_HEAD(&sdp->sd_reclaim_list);
	spin_lock_init(&sdp->sd_reclaim_lock);
	init_waitqueue_head(&sdp->sd_reclaim_wchan);

	for (x = 0; x < GFS_MHC_HASH_SIZE; x++)
		INIT_LIST_HEAD(&sdp->sd_mhc[x]);
	INIT_LIST_HEAD(&sdp->sd_mhc_single);
	spin_lock_init(&sdp->sd_mhc_lock);

	for (x = 0; x < GFS_DEPEND_HASH_SIZE; x++)
		INIT_LIST_HEAD(&sdp->sd_depend[x]);
	spin_lock_init(&sdp->sd_depend_lock);

	init_MUTEX(&sdp->sd_freeze_lock);

	init_MUTEX(&sdp->sd_thread_lock);
	init_completion(&sdp->sd_thread_completion);

	spin_lock_init(&sdp->sd_log_seg_lock);
	INIT_LIST_HEAD(&sdp->sd_log_seg_list);
	init_waitqueue_head(&sdp->sd_log_seg_wait);
	INIT_LIST_HEAD(&sdp->sd_log_ail);
	INIT_LIST_HEAD(&sdp->sd_log_incore);
	init_MUTEX(&sdp->sd_log_lock);
	INIT_LIST_HEAD(&sdp->sd_unlinked_list);
	spin_lock_init(&sdp->sd_unlinked_lock);
	INIT_LIST_HEAD(&sdp->sd_quota_list);
	spin_lock_init(&sdp->sd_quota_lock);

	INIT_LIST_HEAD(&sdp->sd_dirty_j);
	spin_lock_init(&sdp->sd_dirty_j_lock);

	spin_lock_init(&sdp->sd_ail_lock);
	INIT_LIST_HEAD(&sdp->sd_recovery_bufs);

	gfs_init_tune_data(sdp);

	error = gfs_make_args((char *)data, &sdp->sd_args);
	if (error) {
		printk("GFS: can't parse mount arguments\n");
		goto fail_vfree;
	}

	/*  Copy out mount flags  */

	if (sb->s_flags & (MS_NOATIME | MS_NODIRATIME))
		set_bit(SDF_NOATIME, &sdp->sd_flags);
	if (sb->s_flags & MS_RDONLY)
		set_bit(SDF_ROFS, &sdp->sd_flags);

	/*  Setup up Virtual Super Block  */

	sb->s_magic = GFS_MAGIC;
	sb->s_op = &gfs_super_ops;
	sb->s_export_op = &gfs_export_ops;
	sb->s_flags |= MS_NOATIME | MS_NODIRATIME;
	sb->s_maxbytes = ~0ULL;

	if (sdp->sd_args.ar_posixacls)
		sb->s_flags |= MS_POSIXACL;

	/*  Set up the buffer cache and fill in some fake values
	   to allow us to read in the superblock.  */

	sdp->sd_sb.sb_bsize = sb_min_blocksize(sb, GFS_BASIC_BLOCK);
	sdp->sd_sb.sb_bsize_shift = sb->s_blocksize_bits;
	sdp->sd_fsb2bb_shift = sdp->sd_sb.sb_bsize_shift - GFS_BASIC_BLOCK_SHIFT;
	sdp->sd_fsb2bb = 1 << sdp->sd_fsb2bb_shift;

	GFS_ASSERT_SBD(sizeof(struct gfs_sb) <= sdp->sd_sb.sb_bsize, sdp,);

	error = gfs_mount_lockproto(sdp, silent);
	if (error)
		goto fail_vfree;

	printk("GFS: fsid=%s: Joined cluster. Now mounting FS...\n",
	       sdp->sd_fsname);

	if ((sdp->sd_lockstruct.ls_flags & LM_LSFLAG_LOCAL) &&
	    !sdp->sd_args.ar_ignore_local_fs) {
		/*  Force local [p|f]locks  */
		sdp->sd_args.ar_localflocks = TRUE;

		/*  Force local read ahead and caching  */
		sdp->sd_args.ar_localcaching = TRUE;
	}

	/*  Start up the scand thread  */

	error = kernel_thread(gfs_scand, sdp, 0);
	if (error < 0) {
		printk("GFS: fsid=%s: can't start scand thread: %d\n",
		       sdp->sd_fsname, error);
		goto fail_lockproto;
	}
	wait_for_completion(&sdp->sd_thread_completion);

	/*  Start up the glockd thread  */

	for (sdp->sd_glockd_num = 0;
	     sdp->sd_glockd_num < sdp->sd_args.ar_num_glockd;
	     sdp->sd_glockd_num++) {
		error = kernel_thread(gfs_glockd, sdp, 0);
		if (error < 0) {
			printk("GFS: fsid=%s: can't start glockd thread: %d\n",
			       sdp->sd_fsname, error);
			goto fail_glockd;
		}
		wait_for_completion(&sdp->sd_thread_completion);
	}

	error = gfs_glock_nq_num(sdp,
				 GFS_MOUNT_LOCK, &gfs_nondisk_glops,
				 LM_ST_EXCLUSIVE, LM_FLAG_NOEXP | GL_NOCACHE,
				 &mount_gh);
	if (error) {
		printk("GFS: fsid=%s: can't acquire mount glock: %d\n",
		       sdp->sd_fsname, error);
		goto fail_glockd;
	}

	error = gfs_glock_nq_num(sdp,
				 GFS_LIVE_LOCK, &gfs_nondisk_glops,
				 LM_ST_SHARED, LM_FLAG_NOEXP | GL_EXACT,
				 &sdp->sd_live_gh);
	if (error) {
		printk("GFS: fsid=%s: can't acquire live glock: %d\n",
		       sdp->sd_fsname, error);
		goto fail_gunlock_mount;
	}

	sdp->sd_live_gh.gh_owner = NULL;

	error = gfs_glock_nq_num(sdp,
				 GFS_SB_LOCK, &gfs_meta_glops,
				 (sdp->sd_args.ar_upgrade) ? LM_ST_EXCLUSIVE : LM_ST_SHARED,
				 0, &sb_gh);
	if (error) {
		printk("GFS: fsid=%s: can't acquire superblock glock: %d\n",
		       sdp->sd_fsname, error);
		goto fail_gunlock_live;
	}

	error = gfs_read_sb(sdp, sb_gh.gh_gl, silent);
	if (error) {
		printk("GFS: fsid=%s: can't read superblock: %d\n",
		       sdp->sd_fsname, error);
		goto fail_gunlock_sb;
	}

	/*  Set up the buffer cache and SB for real  */

	error = -EINVAL;
	if (sdp->sd_sb.sb_bsize < bdev_hardsect_size(sb->s_bdev)) {
		printk("GFS: fsid=%s: FS block size (%u) is too small for device block size (%u)\n",
		       sdp->sd_fsname, sdp->sd_sb.sb_bsize, bdev_hardsect_size(sb->s_bdev));
		goto fail_gunlock_sb;
	}
	if (sdp->sd_sb.sb_bsize > PAGE_SIZE) {
		printk("GFS: fsid=%s: FS block size (%u) is too big for machine page size (%u)\n",
		       sdp->sd_fsname, sdp->sd_sb.sb_bsize,
		       (unsigned int)PAGE_SIZE);
		goto fail_gunlock_sb;
	}

	/*  Get rid of buffers from the original block size  */
	sb_gh.gh_gl->gl_ops->go_inval(sb_gh.gh_gl, DIO_METADATA | DIO_DATA);
	sb_gh.gh_gl->gl_aspace->i_blkbits = sdp->sd_sb.sb_bsize_shift;

	sb_set_blocksize(sb, sdp->sd_sb.sb_bsize);

	/*  Read in journal index inode  */

	error = gfs_get_jiinode(sdp);
	if (error) {
		printk("GFS: fsid=%s: can't get journal index inode: %d\n",
		       sdp->sd_fsname, error);
		goto fail_gunlock_sb;
	}

	init_MUTEX(&sdp->sd_jindex_lock);

	/*  Get a handle on the transaction glock  */

	error = gfs_glock_get(sdp, GFS_TRANS_LOCK, &gfs_trans_glops,
			      CREATE, &sdp->sd_trans_gl);
	if (error)
		goto fail_ji_free;
	set_bit(GLF_STICKY, &sdp->sd_trans_gl->gl_flags);

	/*  Upgrade version numbers if we need to  */

	if (sdp->sd_args.ar_upgrade) {
		error = gfs_do_upgrade(sdp, sb_gh.gh_gl);
		if (error)
			goto fail_trans_gl;
	}

	/*  Load in the journal index  */

	error = gfs_jindex_hold(sdp, &ji_gh);
	if (error) {
		printk("GFS: fsid=%s: can't read journal index: %d\n",
		       sdp->sd_fsname, error);
		goto fail_trans_gl;
	}

	error = -EINVAL;
	if (sdp->sd_lockstruct.ls_jid >= sdp->sd_journals) {
		printk("GFS: fsid=%s: can't mount journal #%u\n",
		       sdp->sd_fsname, sdp->sd_lockstruct.ls_jid);
		printk("GFS: fsid=%s: there are only %u journals (0 - %u)\n",
		     sdp->sd_fsname, sdp->sd_journals, sdp->sd_journals - 1);
		goto fail_gunlock_ji;
	}
	sdp->sd_jdesc = sdp->sd_jindex[sdp->sd_lockstruct.ls_jid];
	sdp->sd_log_seg_free = sdp->sd_jdesc.ji_nsegment - 1;

	error = gfs_glock_nq_num(sdp,
				 sdp->sd_jdesc.ji_addr, &gfs_meta_glops,
				 LM_ST_EXCLUSIVE, LM_FLAG_NOEXP,
				 &sdp->sd_journal_gh);
	if (error) {
		printk("GFS: fsid=%s: can't acquire the journal glock: %d\n",
		       sdp->sd_fsname, error);
		goto fail_gunlock_ji;
	}

	if (sdp->sd_lockstruct.ls_first) {
		for (x = 0; x < sdp->sd_journals; x++) {
			error = gfs_recover_journal(sdp,
						    x, sdp->sd_jindex + x,
						    TRUE);
			if (error) {
				printk("GFS: fsid=%s: error recovering journal %u: %d\n",
				       sdp->sd_fsname, x, error);
				goto fail_gunlock_journal;
			}
		}

		sdp->sd_lockstruct.ls_ops->lm_others_may_mount(sdp->sd_lockstruct.ls_lockspace);
		sdp->sd_lockstruct.ls_first = FALSE;
	} else {
		error = gfs_recover_journal(sdp,
					    sdp->sd_lockstruct.ls_jid, &sdp->sd_jdesc,
					    TRUE);
		if (error) {
			printk("GFS: fsid=%s: error recovering my journal: %d\n",
			       sdp->sd_fsname, error);
			goto fail_gunlock_journal;
		}
	}

	gfs_glock_dq_uninit(&ji_gh);
	jindex = FALSE;

	/*  Disown my Journal glock  */

	sdp->sd_journal_gh.gh_owner = NULL;

	/*  Drop our cache and reread all the things we read before the replay.  */

	error = gfs_read_sb(sdp, sb_gh.gh_gl, FALSE);
	if (error) {
		printk("GFS: fsid=%s: can't read superblock: %d\n",
		       sdp->sd_fsname, error);
		goto fail_gunlock_journal;
	}

	gfs_glock_force_drop(sdp->sd_jiinode->i_gl);

	error = gfs_jindex_hold(sdp, &ji_gh);
	if (error) {
		printk("GFS: fsid=%s: can't read journal index: %d\n",
		       sdp->sd_fsname, error);
		goto fail_gunlock_journal;
	}
	gfs_glock_dq_uninit(&ji_gh);

	/*  Make the FS read/write  */

	if (!test_bit(SDF_ROFS, &sdp->sd_flags)) {
		error = gfs_make_fs_rw(sdp);
		if (error) {
			printk("GFS: fsid=%s: can't make FS RW: %d\n",
			       sdp->sd_fsname, error);
			goto fail_gunlock_journal;
		}
	}

	/*  Start up the recover thread  */

	error = kernel_thread(gfs_recoverd, sdp, 0);
	if (error < 0) {
		printk("GFS: fsid=%s: can't start recoverd thread: %d\n",
		       sdp->sd_fsname, error);
		goto fail_recover_dump;
	}
	wait_for_completion(&sdp->sd_thread_completion);

	/*  Read in the resource index inode  */

	error = gfs_get_riinode(sdp);
	if (error) {
		printk("GFS: fsid=%s: can't get resource index inode: %d\n",
		       sdp->sd_fsname, error);
		goto fail_recoverd;
	}

	/*  Get the root inode  */

	error = gfs_get_rootinode(sdp);
	if (error) {
		printk("GFS: fsid=%s: can't read in root inode: %d\n",
		       sdp->sd_fsname, error);
		goto fail_ri_free;
	}

	/*  Read in the quota inode  */

	error = gfs_get_qinode(sdp);
	if (error) {
		printk("GFS: fsid=%s: can't get quota file inode: %d\n",
		       sdp->sd_fsname, error);
		goto fail_root_free;
	}

	/*  Read in the license inode  */

	error = gfs_get_linode(sdp);
	if (error) {
		printk("GFS: fsid=%s: can't get license file inode: %d\n",
		       sdp->sd_fsname, error);
		goto fail_qi_free;
	}

	/*  We're through with the superblock lock  */

	gfs_glock_dq_uninit(&sb_gh);
	super = FALSE;

	/*  Get the inode/dentry  */

	inode = gfs_iget(sdp->sd_rooti, CREATE);
	if (!inode) {
		printk("GFS: fsid=%s: can't get root inode\n", sdp->sd_fsname);
		error = -ENOMEM;
		goto fail_li_free;
	}

	sb->s_root = d_alloc_root(inode);
	if (!sb->s_root) {
		iput(inode);
		printk("GFS: fsid=%s: can't get root dentry\n", sdp->sd_fsname);
		error = -ENOMEM;
		goto fail_li_free;
	}

	/*  Start up the logd thread  */

	sdp->sd_jindex_refresh_time = jiffies;

	error = kernel_thread(gfs_logd, sdp, 0);
	if (error < 0) {
		printk("GFS: fsid=%s: can't start logd thread: %d\n",
		       sdp->sd_fsname, error);
		goto fail_dput;
	}
	wait_for_completion(&sdp->sd_thread_completion);

	/*  Start up the quotad thread  */

	error = kernel_thread(gfs_quotad, sdp, 0);
	if (error < 0) {
		printk("GFS: fsid=%s: can't start quotad thread: %d\n",
		       sdp->sd_fsname, error);
		goto fail_logd;
	}
	wait_for_completion(&sdp->sd_thread_completion);

	/*  Start up the inoded thread  */

	error = kernel_thread(gfs_inoded, sdp, 0);
	if (error < 0) {
		printk("GFS: fsid=%s: can't start inoded thread: %d\n",
		       sdp->sd_fsname, error);
		goto fail_quotad;
	}
	wait_for_completion(&sdp->sd_thread_completion);

	/*  Get a handle on the rename lock  */

	error = gfs_glock_get(sdp, GFS_RENAME_LOCK, &gfs_nondisk_glops,
			      CREATE, &sdp->sd_rename_gl);
	if (error)
		goto fail_inoded;

	gfs_glock_dq_uninit(&mount_gh);

	return 0;

      fail_inoded:
	down(&sdp->sd_thread_lock);
	clear_bit(SDF_INODED_RUN, &sdp->sd_flags);
	wake_up_process(sdp->sd_inoded_process);
	up(&sdp->sd_thread_lock);
	wait_for_completion(&sdp->sd_thread_completion);

      fail_quotad:
	down(&sdp->sd_thread_lock);
	clear_bit(SDF_QUOTAD_RUN, &sdp->sd_flags);
	wake_up_process(sdp->sd_quotad_process);
	up(&sdp->sd_thread_lock);
	wait_for_completion(&sdp->sd_thread_completion);

      fail_logd:
	down(&sdp->sd_thread_lock);
	clear_bit(SDF_LOGD_RUN, &sdp->sd_flags);
	wake_up_process(sdp->sd_logd_process);
	up(&sdp->sd_thread_lock);
	wait_for_completion(&sdp->sd_thread_completion);

      fail_dput:
	dput(sb->s_root);

      fail_li_free:
	gfs_inode_put(sdp->sd_linode);

      fail_qi_free:
	gfs_inode_put(sdp->sd_qinode);

      fail_root_free:
	gfs_inode_put(sdp->sd_rooti);

      fail_ri_free:
	gfs_inode_put(sdp->sd_riinode);
	gfs_clear_rgrpd(sdp);

      fail_recoverd:
	down(&sdp->sd_thread_lock);
	clear_bit(SDF_RECOVERD_RUN, &sdp->sd_flags);
	wake_up_process(sdp->sd_recoverd_process);
	up(&sdp->sd_thread_lock);
	wait_for_completion(&sdp->sd_thread_completion);

      fail_recover_dump:
	clear_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags);
	gfs_unlinked_cleanup(sdp);
	gfs_quota_cleanup(sdp);

      fail_gunlock_journal:
	gfs_glock_dq_uninit(&sdp->sd_journal_gh);

      fail_gunlock_ji:
	if (jindex)
		gfs_glock_dq_uninit(&ji_gh);

      fail_trans_gl:
	gfs_glock_put(sdp->sd_trans_gl);

      fail_ji_free:
	gfs_inode_put(sdp->sd_jiinode);
	gfs_clear_journals(sdp);

      fail_gunlock_sb:
	if (super)
		gfs_glock_dq_uninit(&sb_gh);

      fail_gunlock_live:
	gfs_glock_dq_uninit(&sdp->sd_live_gh);

      fail_gunlock_mount:
	gfs_glock_dq_uninit(&mount_gh);

      fail_glockd:
	clear_bit(SDF_GLOCKD_RUN, &sdp->sd_flags);
	wake_up(&sdp->sd_reclaim_wchan);
	while (sdp->sd_glockd_num--)
		wait_for_completion(&sdp->sd_thread_completion);

	down(&sdp->sd_thread_lock);
	clear_bit(SDF_SCAND_RUN, &sdp->sd_flags);
	wake_up_process(sdp->sd_scand_process);
	up(&sdp->sd_thread_lock);
	wait_for_completion(&sdp->sd_thread_completion);

      fail_lockproto:
	gfs_gl_hash_clear(sdp, TRUE);
	gfs_unmount_lockproto(sdp);
	gfs_clear_dirty_j(sdp);
	while (invalidate_inodes(sb))
		yield();

      fail_vfree:
	vfree(sdp);

      fail:
	vfs2sdp(sb) = NULL;
	return error;
}

/**
 * gfs_get_sb - 
 * @fs_type:
 * @flags:
 * @dev_name:
 * @data:
 *
 * Returns: the new superblock
 */

struct super_block *gfs_get_sb(struct file_system_type *fs_type, int flags,
			       const char *dev_name, void *data)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, fill_super);
}

struct file_system_type gfs_fs_type = {
	.name = "gfs",
	.fs_flags = FS_REQUIRES_DEV,
	.get_sb = gfs_get_sb,
	.kill_sb = kill_block_super,
	.owner = THIS_MODULE,
};
