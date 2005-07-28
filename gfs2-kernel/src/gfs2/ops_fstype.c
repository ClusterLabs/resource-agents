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
#include <linux/vmalloc.h>
#include <linux/blkdev.h>
#include <linux/kthread.h>

#include "gfs2.h"
#include "daemon.h"
#include "diaper.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "lm.h"
#include "mount.h"
#include "ops_export.h"
#include "ops_fstype.h"
#include "ops_super.h"
#include "proc.h"
#include "recovery.h"
#include "rgrp.h"
#include "super.h"
#include "unlinked.h"

#define DO FALSE
#define UNDO TRUE

#undef NO_DIAPER

static struct gfs2_sbd *init_sbd(struct super_block *sb)
{
	struct gfs2_sbd *sdp;
	unsigned int x;

	sdp = vmalloc(sizeof(struct gfs2_sbd));
	if (!sdp)
		return NULL;

	memset(sdp, 0, sizeof(struct gfs2_sbd));

	set_v2sdp(sb, sdp);
	sdp->sd_vfs = sb;

	gfs2_tune_init(&sdp->sd_tune);

	for (x = 0; x < GFS2_GL_HASH_SIZE; x++) {
		sdp->sd_gl_hash[x].hb_lock = RW_LOCK_UNLOCKED;
		INIT_LIST_HEAD(&sdp->sd_gl_hash[x].hb_list);
	}
	INIT_LIST_HEAD(&sdp->sd_reclaim_list);
	spin_lock_init(&sdp->sd_reclaim_lock);
	init_waitqueue_head(&sdp->sd_reclaim_wq);

	init_MUTEX(&sdp->sd_inum_mutex);
	spin_lock_init(&sdp->sd_statfs_spin);
	init_MUTEX(&sdp->sd_statfs_mutex);

	spin_lock_init(&sdp->sd_rindex_spin);
	init_MUTEX(&sdp->sd_rindex_mutex);
	INIT_LIST_HEAD(&sdp->sd_rindex_list);
	INIT_LIST_HEAD(&sdp->sd_rindex_mru_list);
	INIT_LIST_HEAD(&sdp->sd_rindex_recent_list);

	INIT_LIST_HEAD(&sdp->sd_jindex_list);
	spin_lock_init(&sdp->sd_jindex_spin);
	init_MUTEX(&sdp->sd_jindex_mutex);

	INIT_LIST_HEAD(&sdp->sd_unlinked_list);
	spin_lock_init(&sdp->sd_unlinked_spin);
	init_MUTEX(&sdp->sd_unlinked_mutex);

	INIT_LIST_HEAD(&sdp->sd_quota_list);
	spin_lock_init(&sdp->sd_quota_spin);
	init_MUTEX(&sdp->sd_quota_mutex);

	spin_lock_init(&sdp->sd_log_lock);
	init_waitqueue_head(&sdp->sd_log_trans_wq);
	init_waitqueue_head(&sdp->sd_log_flush_wq);

	INIT_LIST_HEAD(&sdp->sd_log_le_gl);
	INIT_LIST_HEAD(&sdp->sd_log_le_buf);
	INIT_LIST_HEAD(&sdp->sd_log_le_revoke);
	INIT_LIST_HEAD(&sdp->sd_log_le_rg);
	INIT_LIST_HEAD(&sdp->sd_log_le_databuf);

	INIT_LIST_HEAD(&sdp->sd_log_blks_list);
	init_waitqueue_head(&sdp->sd_log_blks_wait);

	INIT_LIST_HEAD(&sdp->sd_ail1_list);
	INIT_LIST_HEAD(&sdp->sd_ail2_list);

	init_MUTEX(&sdp->sd_log_flush_lock);
	INIT_LIST_HEAD(&sdp->sd_log_flush_list);

	INIT_LIST_HEAD(&sdp->sd_revoke_list);

	init_MUTEX(&sdp->sd_freeze_lock);

	return sdp;
}

static void init_vfs(struct gfs2_sbd *sdp)
{
	struct super_block *sb = sdp->sd_vfs;

	sb->s_magic = GFS2_MAGIC;
	sb->s_op = &gfs2_super_ops;
	sb->s_export_op = &gfs2_export_ops;
	sb->s_maxbytes = MAX_LFS_FILESIZE;

	if (sb->s_flags & (MS_NOATIME | MS_NODIRATIME))
		set_bit(SDF_NOATIME, &sdp->sd_flags);

	/* Don't let the VFS update atimes.  GFS2 handles this itself. */
	sb->s_flags |= MS_NOATIME | MS_NODIRATIME;

	/* Set up the buffer cache and fill in some fake block size values
	   to allow us to read-in the on-disk superblock. */
	sdp->sd_sb.sb_bsize = sb_min_blocksize(sb, GFS2_BASIC_BLOCK);
	sdp->sd_sb.sb_bsize_shift = sb->s_blocksize_bits;
	sdp->sd_fsb2bb_shift = sdp->sd_sb.sb_bsize_shift - GFS2_BASIC_BLOCK_SHIFT;
	sdp->sd_fsb2bb = 1 << sdp->sd_fsb2bb_shift;
}

static int init_locking(struct gfs2_sbd *sdp, struct gfs2_holder *mount_gh,
			int undo)
{
	struct task_struct *p;
	int error = 0;

	if (undo)
		goto fail_trans;

	p = kthread_run(gfs2_scand, sdp, "gfs2_scand");
	error = IS_ERR(p);
	if (error) {
		printk("GFS2: fsid=%s: can't start scand thread: %d\n",
		       sdp->sd_fsname, error);
		return error;
	}
	sdp->sd_scand_process = p;

	for (sdp->sd_glockd_num = 0;
	     sdp->sd_glockd_num < sdp->sd_args.ar_num_glockd;
	     sdp->sd_glockd_num++) {
		p = kthread_run(gfs2_glockd, sdp, "gfs2_glockd");
		error = IS_ERR(p);
		if (error) {
			printk("GFS2: fsid=%s: can't start glockd thread: %d\n",
			       sdp->sd_fsname, error);
			goto fail;
		}
		sdp->sd_glockd_process[sdp->sd_glockd_num] = p;
	}

	error = gfs2_glock_nq_num(sdp,
				  GFS2_MOUNT_LOCK, &gfs2_nondisk_glops,
				  LM_ST_EXCLUSIVE, LM_FLAG_NOEXP | GL_NOCACHE,
				  mount_gh);
	if (error) {
		printk("GFS2: fsid=%s: can't acquire mount glock: %d\n",
		       sdp->sd_fsname, error);
		goto fail;
	}

	error = gfs2_glock_nq_num(sdp,
				  GFS2_LIVE_LOCK, &gfs2_nondisk_glops,
				  LM_ST_SHARED,
				  LM_FLAG_NOEXP | GL_EXACT | GL_NEVER_RECURSE,
				  &sdp->sd_live_gh);
	if (error) {
		printk("GFS2: fsid=%s: can't acquire live glock: %d\n",
		       sdp->sd_fsname, error);
		goto fail_mount;
	}

	error = gfs2_glock_get(sdp, GFS2_RENAME_LOCK, &gfs2_nondisk_glops,
			       CREATE, &sdp->sd_rename_gl);
	if (error) {
		printk("GFS2: fsid=%s: can't create rename glock: %d\n",
		       sdp->sd_fsname, error);
		goto fail_live;
	}

	error = gfs2_glock_get(sdp, GFS2_TRANS_LOCK, &gfs2_trans_glops,
			       CREATE, &sdp->sd_trans_gl);
	if (error) {
		printk("GFS2: fsid=%s: can't create transaction glock: %d\n",
		       sdp->sd_fsname, error);
		goto fail_rename;
	}
	set_bit(GLF_STICKY, &sdp->sd_trans_gl->gl_flags);

	return 0;

 fail_trans:
	gfs2_glock_put(sdp->sd_trans_gl);

 fail_rename:
	gfs2_glock_put(sdp->sd_rename_gl);

 fail_live:
	gfs2_glock_dq_uninit(&sdp->sd_live_gh);

 fail_mount:
	gfs2_glock_dq_uninit(mount_gh);

 fail:
	while (sdp->sd_glockd_num--)
		kthread_stop(sdp->sd_glockd_process[sdp->sd_glockd_num]);

	kthread_stop(sdp->sd_scand_process);

	return error;
}

static int init_sb(struct gfs2_sbd *sdp, int silent, int undo)
{
	struct super_block *sb = sdp->sd_vfs;
	struct gfs2_holder sb_gh;
	int error = 0;

	if (undo) {
		gfs2_inode_put(sdp->sd_master_dir);
		return 0;
	}
	
	error = gfs2_glock_nq_num(sdp,
				 GFS2_SB_LOCK, &gfs2_meta_glops,
				 LM_ST_SHARED, 0, &sb_gh);
	if (error) {
		printk("GFS2: fsid=%s: can't acquire superblock glock: %d\n",
		       sdp->sd_fsname, error);
		return error;
	}

	error = gfs2_read_sb(sdp, sb_gh.gh_gl, silent);
	if (error) {
		printk("GFS2: fsid=%s: can't read superblock: %d\n",
		       sdp->sd_fsname, error);
		goto out;
	}

	/* Set up the buffer cache and SB for real */
	error = -EINVAL;
	if (sdp->sd_sb.sb_bsize < bdev_hardsect_size(sb->s_bdev)) {
		printk("GFS2: fsid=%s: FS block size (%u) is too small "
		       "for device block size (%u)\n",
		       sdp->sd_fsname,
		       sdp->sd_sb.sb_bsize, bdev_hardsect_size(sb->s_bdev));
		goto out;
	}
	if (sdp->sd_sb.sb_bsize > PAGE_SIZE) {
		printk("GFS2: fsid=%s: FS block size (%u) is too big "
		       "for machine page size (%u)\n",
		       sdp->sd_fsname,
		       sdp->sd_sb.sb_bsize, (unsigned int)PAGE_SIZE);
		goto out;
	}

	/* Get rid of buffers from the original block size */
	sb_gh.gh_gl->gl_ops->go_inval(sb_gh.gh_gl, DIO_METADATA | DIO_DATA);
	sb_gh.gh_gl->gl_aspace->i_blkbits = sdp->sd_sb.sb_bsize_shift;

	sb_set_blocksize(sb, sdp->sd_sb.sb_bsize);
#ifndef NO_DIAPER
	set_blocksize(gfs2_diaper_2real(sb->s_bdev), sdp->sd_sb.sb_bsize);
#endif

	error = gfs2_lookup_master_dir(sdp);
	if (error)
		printk("GFS2: fsid=%s: can't read in master directory: %d\n",
		       sdp->sd_fsname, error);

 out:
	gfs2_glock_dq_uninit(&sb_gh);

	return error;
}

static int init_journal(struct gfs2_sbd *sdp, int undo)
{
	struct gfs2_holder ji_gh;
	struct task_struct *p;
	int jindex = TRUE;
	int error = 0;

	if (undo) {
		jindex = FALSE;
		goto fail_recoverd;
	}

	error = gfs2_lookup_simple(sdp->sd_master_dir, "jindex",
				   &sdp->sd_jindex);
	if (error) {
		printk("GFS2: fsid=%s: can't lookup journal index: %d\n",
		       sdp->sd_fsname, error);
		return error;
	}
	set_bit(GLF_STICKY, &sdp->sd_jindex->i_gl->gl_flags);

	/* Load in the journal index special file */

	error = gfs2_jindex_hold(sdp, &ji_gh);
	if (error) {
		printk("GFS2: fsid=%s: can't read journal index: %d\n",
		       sdp->sd_fsname, error);
		goto fail;
	}

	error = -EINVAL;
	if (!gfs2_jindex_size(sdp)) {
		printk("GFS2: fsid=%s: no journals!\n",
		       sdp->sd_fsname);
		goto fail_jindex;		
	}

	if (sdp->sd_args.ar_spectator) {
		sdp->sd_jdesc = gfs2_jdesc_find(sdp, 0);
		sdp->sd_log_blks_free = sdp->sd_jdesc->jd_blocks;
	} else {
		if (sdp->sd_lockstruct.ls_jid >= gfs2_jindex_size(sdp)) {
			printk("GFS2: fsid=%s: can't mount journal #%u\n",
			       sdp->sd_fsname, sdp->sd_lockstruct.ls_jid);
			printk("GFS2: fsid=%s: "
			       "there are only %u journals (0 - %u)\n",
			       sdp->sd_fsname,
			       gfs2_jindex_size(sdp),
			       gfs2_jindex_size(sdp) - 1);
			goto fail_jindex;
		}
		sdp->sd_jdesc = gfs2_jdesc_find(sdp, sdp->sd_lockstruct.ls_jid);

		error = gfs2_glock_nq_num(sdp,
					  sdp->sd_lockstruct.ls_jid,
					  &gfs2_journal_glops,
					  LM_ST_EXCLUSIVE, LM_FLAG_NOEXP,
					  &sdp->sd_journal_gh);
		if (error) {
			printk("GFS2: fsid=%s: "
			       "can't acquire the journal glock: %d\n",
			       sdp->sd_fsname, error);
			goto fail_jindex;
		}

		error = gfs2_glock_nq_init(sdp->sd_jdesc->jd_inode->i_gl,
					   LM_ST_SHARED,
					   LM_FLAG_NOEXP | GL_EXACT,
					   &sdp->sd_jinode_gh);
		if (error) {
			printk("gfs2: fsid=%s: "
			       "can't acquire out journal inode glock: %d\n",
			       sdp->sd_fsname, error);
			goto fail_journal_gh;
		}

		error = gfs2_jdesc_check(sdp->sd_jdesc);
		if (error) {
			printk("GFS2: fsid=%s: my journal (%u) is bad: %d\n",
			       sdp->sd_fsname, sdp->sd_jdesc->jd_jid, error);
			goto fail_jinode_gh;
		}
		sdp->sd_log_blks_free = sdp->sd_jdesc->jd_blocks;
	}

	if (sdp->sd_lockstruct.ls_first) {
		unsigned int x;
		for (x = 0; x < sdp->sd_journals; x++) {
			error = gfs2_recover_journal(gfs2_jdesc_find(sdp, x),
						     WAIT);
			if (error) {
				printk("GFS2: fsid=%s: "
				       "error recovering journal %u: %d\n",
				       sdp->sd_fsname, x, error);
				goto fail_jinode_gh;
			}
		}

		gfs2_lm_others_may_mount(sdp);
	} else if (!sdp->sd_args.ar_spectator) {
		error = gfs2_recover_journal(sdp->sd_jdesc, WAIT);
		if (error) {
			printk("GFS2: fsid=%s: "
			       "error recovering my journal: %d\n",
			       sdp->sd_fsname, error);
			goto fail_jinode_gh;
		}
	}

	set_bit(SDF_JOURNAL_CHECKED, &sdp->sd_flags);
	gfs2_glock_dq_uninit(&ji_gh);
	jindex = FALSE;

	/* Disown my Journal glock */

	sdp->sd_journal_gh.gh_owner = NULL;
	sdp->sd_jinode_gh.gh_owner = NULL;

	p = kthread_run(gfs2_recoverd, sdp, "gfs2_recoverd");
	error = IS_ERR(p);
	if (error) {
		printk("GFS2: fsid=%s: can't start recoverd thread: %d\n",
		       sdp->sd_fsname, error);
		goto fail_jinode_gh;
	}
	sdp->sd_recoverd_process = p;

	return 0;

 fail_recoverd:
	kthread_stop(sdp->sd_recoverd_process);

 fail_jinode_gh:
	if (!sdp->sd_args.ar_spectator)
		gfs2_glock_dq_uninit(&sdp->sd_jinode_gh);

 fail_journal_gh:
	if (!sdp->sd_args.ar_spectator)
		gfs2_glock_dq_uninit(&sdp->sd_journal_gh);

 fail_jindex:
	gfs2_jindex_free(sdp);
	if (jindex)
		gfs2_glock_dq_uninit(&ji_gh);

 fail:
	gfs2_inode_put(sdp->sd_jindex);

	return error;
}

static int init_inodes(struct gfs2_sbd *sdp, int undo)
{
	struct inode *inode;
	struct dentry **dentry = &sdp->sd_vfs->s_root;
	int error = 0;

	if (undo)
		goto fail_dput;

	/* Read in the master inode number inode */
	error = gfs2_lookup_simple(sdp->sd_master_dir, "inum",
				   &sdp->sd_inum_inode);
	if (error) {
		printk("GFS2: fsid=%s: can't read in inum inode: %d\n",
		       sdp->sd_fsname, error);
		return error;
	}

	/* Read in the master statfs inode */
	error = gfs2_lookup_simple(sdp->sd_master_dir, "statfs",
				   &sdp->sd_statfs_inode);
	if (error) {
		printk("GFS2: fsid=%s: can't read in statfs inode: %d\n",
		       sdp->sd_fsname, error);
		goto fail;
	}

	/* Read in the resource index inode */
	error = gfs2_lookup_simple(sdp->sd_master_dir, "rindex",
				   &sdp->sd_rindex);
	if (error) {
		printk("GFS2: fsid=%s: can't get resource index inode: %d\n",
		       sdp->sd_fsname, error);
		goto fail_statfs;
	}
	set_bit(GLF_STICKY, &sdp->sd_rindex->i_gl->gl_flags);
	sdp->sd_rindex_vn = sdp->sd_rindex->i_gl->gl_vn - 1;

	/* Read in the quota inode */
	error = gfs2_lookup_simple(sdp->sd_master_dir, "quota",
				   &sdp->sd_quota_inode);
	if (error) {
		printk("GFS2: fsid=%s: can't get quota file inode: %d\n",
		       sdp->sd_fsname, error);
		goto fail_rindex;
	}

	/* Get the root inode */
	error = gfs2_lookup_simple(sdp->sd_master_dir, "root",
				   &sdp->sd_root_dir);
	if (error) {
		printk("GFS2: fsid=%s: can't read in root inode: %d\n",
		       sdp->sd_fsname, error);
		goto fail_qinode;
	}

	/* Get the root inode/dentry */
	inode = gfs2_ip2v(sdp->sd_root_dir, CREATE);
	if (!inode) {
		printk("GFS2: fsid=%s: can't get root inode\n", sdp->sd_fsname);
		error = -ENOMEM;
		goto fail_rooti;
	}

	*dentry = d_alloc_root(inode);
	if (!*dentry) {
		iput(inode);
		printk("GFS2: fsid=%s: can't get root dentry\n", sdp->sd_fsname);
		error = -ENOMEM;
		goto fail_rooti;
	}

	return 0;

 fail_dput:
	dput(*dentry);
	*dentry = NULL;

 fail_rooti:
	gfs2_inode_put(sdp->sd_root_dir);

 fail_qinode:
	gfs2_inode_put(sdp->sd_quota_inode);

 fail_rindex:
	gfs2_clear_rgrpd(sdp);
	gfs2_inode_put(sdp->sd_rindex);

 fail_statfs:
	gfs2_inode_put(sdp->sd_statfs_inode);

 fail:
	gfs2_inode_put(sdp->sd_inum_inode);

	return error;
}

static int init_per_node(struct gfs2_sbd *sdp, int undo)
{
       	struct gfs2_inode *pn = NULL;
	char buf[30];
	int error = 0;

	if (sdp->sd_args.ar_spectator)
		return 0;

	if (undo)
		goto fail_qc_gh;

	error = gfs2_lookup_simple(sdp->sd_master_dir, "per_node", &pn);
	if (error) {
		printk("GFS2: fsid=%s: can't find per_node directory: %d\n",
		       sdp->sd_fsname, error);
		return error;
	}

	sprintf(buf, "inum_range%u", sdp->sd_jdesc->jd_jid);
	error = gfs2_lookup_simple(pn, buf, &sdp->sd_ir_inode);
	if (error) {
		printk("GFS2: fsid=%s: can't find local \"ir\" file: %d\n",
		       sdp->sd_fsname, error);
		goto fail;
	}

	sprintf(buf, "statfs_change%u", sdp->sd_jdesc->jd_jid);
	error = gfs2_lookup_simple(pn, buf, &sdp->sd_sc_inode);
	if (error) {
		printk("GFS2: fsid=%s: can't find local \"sc\" file: %d\n",
		       sdp->sd_fsname, error);
		goto fail_ir_i;
	}

	sprintf(buf, "unlinked_tag%u", sdp->sd_jdesc->jd_jid);
	error = gfs2_lookup_simple(pn, buf, &sdp->sd_ut_inode);
	if (error) {
		printk("GFS2: fsid=%s: can't find local \"ut\" file: %d\n",
		       sdp->sd_fsname, error);
		goto fail_sc_i;
	}

	sprintf(buf, "quota_change%u", sdp->sd_jdesc->jd_jid);
	error = gfs2_lookup_simple(pn, buf, &sdp->sd_qc_inode);
	if (error) {
		printk("GFS2: fsid=%s: can't find local \"qc\" file: %d\n",
		       sdp->sd_fsname, error);
		goto fail_ut_i;
	}

	gfs2_inode_put(pn);
	pn = NULL;

	error = gfs2_glock_nq_init(sdp->sd_ir_inode->i_gl,
				   LM_ST_EXCLUSIVE, GL_NEVER_RECURSE,
				   &sdp->sd_ir_gh);
	if (error) {
		printk("GFS2: fsid=%s: can't lock local \"ir\" file: %d\n",
		       sdp->sd_fsname, error);
		goto fail_qc_i;
	}

	error = gfs2_glock_nq_init(sdp->sd_sc_inode->i_gl,
				   LM_ST_EXCLUSIVE, GL_NEVER_RECURSE,
				   &sdp->sd_sc_gh);
	if (error) {
		printk("GFS2: fsid=%s: can't lock local \"sc\" file: %d\n",
		       sdp->sd_fsname, error);
		goto fail_ir_gh;
	}

	error = gfs2_glock_nq_init(sdp->sd_ut_inode->i_gl,
				   LM_ST_EXCLUSIVE, GL_NEVER_RECURSE,
				   &sdp->sd_ut_gh);
	if (error) {
		printk("GFS2: fsid=%s: can't lock local \"ut\" file: %d\n",
		       sdp->sd_fsname, error);
		goto fail_sc_gh;
	}

	error = gfs2_glock_nq_init(sdp->sd_qc_inode->i_gl,
				   LM_ST_EXCLUSIVE, GL_NEVER_RECURSE,
				   &sdp->sd_qc_gh);
	if (error) {
		printk("GFS2: fsid=%s: can't lock local \"qc\" file: %d\n",
		       sdp->sd_fsname, error);
		goto fail_ut_gh;
	}

	return 0;

 fail_qc_gh:
	gfs2_glock_dq_uninit(&sdp->sd_qc_gh);

 fail_ut_gh:
	gfs2_glock_dq_uninit(&sdp->sd_ut_gh);

 fail_sc_gh:
	gfs2_glock_dq_uninit(&sdp->sd_sc_gh);

 fail_ir_gh:
	gfs2_glock_dq_uninit(&sdp->sd_ir_gh);

 fail_qc_i:
	gfs2_inode_put(sdp->sd_qc_inode);

 fail_ut_i:
	gfs2_inode_put(sdp->sd_ut_inode);

 fail_sc_i:
	gfs2_inode_put(sdp->sd_sc_inode);

 fail_ir_i:
	gfs2_inode_put(sdp->sd_ir_inode);

 fail:
	if (pn)
		gfs2_inode_put(pn);
	return error;
}

static int init_threads(struct gfs2_sbd *sdp, int undo)
{
	struct task_struct *p;
	int error = 0;

	if (undo)
		goto fail_inoded;

	sdp->sd_log_flush_time = jiffies;
	sdp->sd_jindex_refresh_time = jiffies;

	p = kthread_run(gfs2_logd, sdp, "gfs2_logd");
	error = IS_ERR(p);
	if (error) {
		printk("GFS2: fsid=%s: can't start logd thread: %d\n",
		       sdp->sd_fsname, error);
		return error;
	}
	sdp->sd_logd_process = p;

	sdp->sd_statfs_sync_time = jiffies;
	sdp->sd_quota_sync_time = jiffies;

	p = kthread_run(gfs2_quotad, sdp, "gfs2_quotad");
	error = IS_ERR(p);
	if (error) {
		printk("GFS2: fsid=%s: can't start quotad thread: %d\n",
		       sdp->sd_fsname, error);
		goto fail;
	}
	sdp->sd_quotad_process = p;

	p = kthread_run(gfs2_inoded, sdp, "gfs2_inoded");
	error = IS_ERR(p);
	if (error) {
		printk("GFS2: fsid=%s: can't start inoded thread: %d\n",
		       sdp->sd_fsname, error);
		goto fail_quotad;
	}
	sdp->sd_inoded_process = p;

	return 0;

 fail_inoded:
	kthread_stop(sdp->sd_inoded_process);

 fail_quotad:
	kthread_stop(sdp->sd_quotad_process);

 fail:
	kthread_stop(sdp->sd_logd_process);
	
	return error;
}

/**
 * fill_super - Read in superblock
 * @sb: The VFS superblock
 * @data: Mount options
 * @silent: Don't complain if it's not a GFS2 filesystem
 *
 * Returns: errno
 */

static int fill_super(struct super_block *sb, void *data, int silent)
{
	struct gfs2_sbd *sdp;
	struct gfs2_holder mount_gh;
	int error;

	sdp = init_sbd(sb);
	if (!sdp) {
		printk("GFS2: can't alloc struct gfs2_sbd\n");
		return -ENOMEM;
	}

#ifndef NO_DIAPER
	gfs2_diaper_register_sbd(sb->s_bdev, sdp);
#endif

	error = gfs2_mount_args(sdp, (char *)data, FALSE);
	if (error) {
		printk("GFS2: can't parse mount arguments\n");
		goto fail;
	}

	init_vfs(sdp);

	/* Mount an inter-node lock module, check for local optimizations */
	error = gfs2_lm_mount(sdp, silent);
	if (error)
		goto fail;

	error = init_locking(sdp, &mount_gh, DO);
	if (error)
		goto fail_lm;

	error = init_sb(sdp, silent, DO);
	if (error)
		goto fail_locking;

	error = init_journal(sdp, DO);
	if (error)
		goto fail_sb;

	error = init_inodes(sdp, DO);
	if (error)
		goto fail_journals;

	error = init_per_node(sdp, DO);
	if (error)
		goto fail_inodes;

	error = gfs2_statfs_init(sdp);
	if (error) {
		printk("GFS2: fsid=%s: can't initialize statfs subsystem: %d\n",
		       sdp->sd_fsname, error);
		goto fail_per_node;
	}

	error = init_threads(sdp, DO);
	if (error)
		goto fail_per_node;

	gfs2_proc_fs_add(sdp);

	/* Make the FS read/write */
	if (!(sb->s_flags & MS_RDONLY)) {
		error = gfs2_make_fs_rw(sdp);
		if (error) {
			printk("GFS2: fsid=%s: can't make FS RW: %d\n",
			       sdp->sd_fsname, error);
			goto fail_proc;
		}
	}

	gfs2_glock_dq_uninit(&mount_gh);

	return 0;

 fail_proc:
	gfs2_proc_fs_del(sdp);
	init_threads(sdp, UNDO);

 fail_per_node:
	init_per_node(sdp, UNDO);

 fail_inodes:
	init_inodes(sdp, UNDO);

 fail_journals:
	init_journal(sdp, UNDO);

 fail_sb:
	init_sb(sdp, FALSE, UNDO);

 fail_locking:
	init_locking(sdp, &mount_gh, UNDO);

 fail_lm:
	gfs2_gl_hash_clear(sdp, WAIT);
	gfs2_lm_unmount(sdp);
	while (invalidate_inodes(sb))
		yield();

 fail:
	vfree(sdp);
	set_v2sdp(sb, NULL);

	return error;
}

/**
 * gfs2_test_bdev_super -
 * @sb:
 * @data:
 *
 */

int gfs2_test_bdev_super(struct super_block *sb, void *data)
{
	return (void *)sb->s_bdev == data;
}

/**
 * gfs2_test_bdev_super -
 * @sb:
 * @data:
 *
 */

int gfs2_set_bdev_super(struct super_block *sb, void *data)
{
	sb->s_bdev = data;
	sb->s_dev = sb->s_bdev->bd_dev;
	return 0;
}

#ifdef NO_DIAPER
struct super_block *gfs2_get_sb(struct file_system_type *fs_type, int flags,
				const char *dev_name, void *data)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, fill_super);
}
#else
/**
 * gfs2_get_sb -
 * @fs_type:
 * @flags:
 * @dev_name:
 * @data:
 *
 * Rip off of get_sb_bdev().
 *
 * Returns: the new superblock
 */

struct super_block *gfs2_get_sb(struct file_system_type *fs_type, int flags,
				const char *dev_name, void *data)
{
	struct block_device *real, *diaper;
	struct super_block *sb;
	int error = 0;

	real = open_bdev_excl(dev_name, flags, fs_type);
	if (IS_ERR(real))
		return (struct super_block *)real;

	diaper = gfs2_diaper_get(real, flags);
	if (IS_ERR(diaper)) {
		close_bdev_excl(real);
		return (struct super_block *)diaper;
	}

	down(&diaper->bd_mount_sem);
	sb = sget(fs_type, gfs2_test_bdev_super, gfs2_set_bdev_super, diaper);
	up(&diaper->bd_mount_sem);
	if (IS_ERR(sb))
		goto out;

	if (sb->s_root) {
		if ((flags ^ sb->s_flags) & MS_RDONLY) {
			up_write(&sb->s_umount);
			deactivate_super(sb);
			sb = ERR_PTR(-EBUSY);
		}
		goto out;
	} else {
		char buf[BDEVNAME_SIZE];

		sb->s_flags = flags;
		strlcpy(sb->s_id, bdevname(real, buf), sizeof(sb->s_id));
		sb->s_old_blocksize = block_size(real);
		sb_set_blocksize(sb, sb->s_old_blocksize);
		set_blocksize(real, sb->s_old_blocksize);
		error = fill_super(sb, data, (flags & MS_VERBOSE) ? 1 : 0);
		if (error) {
			up_write(&sb->s_umount);
			deactivate_super(sb);
			sb = ERR_PTR(error);
		} else
			sb->s_flags |= MS_ACTIVE;
	}

	return sb;

 out:
	gfs2_diaper_put(diaper);
	close_bdev_excl(real);
	return sb;
}

/**
 * gfs2_kill_sb -
 * @sb:
 *
 * Rip off of kill_block_super().
 *
 */

void gfs2_kill_sb(struct super_block *sb)
{
	struct block_device *diaper = sb->s_bdev;
	struct block_device *real = gfs2_diaper_2real(diaper);
	unsigned long bsize = sb->s_old_blocksize;

	generic_shutdown_super(sb);
	set_blocksize(diaper, bsize);
	set_blocksize(real, bsize);
	gfs2_diaper_put(diaper);
	close_bdev_excl(real);
}
#endif

struct file_system_type gfs2_fs_type = {
	.name = "gfs2",
	.fs_flags = FS_REQUIRES_DEV,
	.get_sb = gfs2_get_sb,
#ifdef NO_DIAPER
	.kill_sb = kill_block_super,
#else
	.kill_sb = gfs2_kill_sb,
#endif
	.owner = THIS_MODULE,
};

