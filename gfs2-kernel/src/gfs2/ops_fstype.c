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

static __inline__ int
do_thread(struct gfs2_sbd *sdp,
	  int (*fn)(void *))
{
	ENTER(G2FN_DO_THREAD)
	int error = kernel_thread(fn, sdp, 0);
	if (error >= 0) {
		wait_for_completion(&sdp->sd_thread_completion);
		error = 0;
	}
	RETURN(G2FN_DO_THREAD, error);
}

static struct gfs2_sbd *
init_sbd(struct super_block *sb)
{
	ENTER(G2FN_INIT_SBD)
	struct gfs2_sbd *sdp;
	unsigned int x;

	sdp = vmalloc(sizeof(struct gfs2_sbd));
	if (!sdp)
		RETURN(G2FN_INIT_SBD, NULL);

	memset(sdp, 0, sizeof(struct gfs2_sbd));

	vfs2sdp(sb) = sdp;
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

	spin_lock_init(&sdp->sd_rindex_spin);
	init_MUTEX(&sdp->sd_rindex_mutex);
	INIT_LIST_HEAD(&sdp->sd_rindex_list);
	INIT_LIST_HEAD(&sdp->sd_rindex_mru_list);
	INIT_LIST_HEAD(&sdp->sd_rindex_recent_list);

	INIT_LIST_HEAD(&sdp->sd_jindex_list);
	spin_lock_init(&sdp->sd_jindex_spin);
	init_MUTEX(&sdp->sd_jindex_mutex);

	init_MUTEX(&sdp->sd_thread_lock);
	init_completion(&sdp->sd_thread_completion);

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

	if (sb->s_flags & (MS_NOATIME | MS_NODIRATIME))
		set_bit(SDF_NOATIME, &sdp->sd_flags);
	if (sb->s_flags & MS_RDONLY)
		set_bit(SDF_ROFS, &sdp->sd_flags);

	RETURN(G2FN_INIT_SBD, sdp);
}

static void
init_vfs(struct gfs2_sbd *sdp)
{
	ENTER(G2FN_INIT_VFS)
	struct super_block *sb = sdp->sd_vfs;

	sb->s_magic = GFS2_MAGIC;
	sb->s_op = &gfs2_super_ops;
	sb->s_export_op = &gfs2_export_ops;

	/* Don't let the VFS update atimes.  GFS2 handles this itself. */
	sb->s_flags |= MS_NOATIME | MS_NODIRATIME;
	sb->s_maxbytes = MAX_LFS_FILESIZE;

	if (sdp->sd_args.ar_spectator) {
		sb->s_flags |= MS_RDONLY;
		set_bit(SDF_ROFS, &sdp->sd_flags);
	}

	/* If we were mounted with -o acl (to support POSIX access control
	   lists), tell VFS */
	if (sdp->sd_args.ar_posix_acl)
		sb->s_flags |= MS_POSIXACL;

	/* Set up the buffer cache and fill in some fake block size values
	   to allow us to read-in the on-disk superblock. */
	sdp->sd_sb.sb_bsize = sb_min_blocksize(sb, GFS2_BASIC_BLOCK);
	sdp->sd_sb.sb_bsize_shift = sb->s_blocksize_bits;
	sdp->sd_fsb2bb_shift = sdp->sd_sb.sb_bsize_shift - GFS2_BASIC_BLOCK_SHIFT;
	sdp->sd_fsb2bb = 1 << sdp->sd_fsb2bb_shift;

	RET(G2FN_INIT_VFS);
}

static int
init_locking(struct gfs2_sbd *sdp, struct gfs2_holder *mount_gh, int undo)
{
	ENTER(G2FN_INIT_LOCKING)
	int error = 0;

	if (undo)
		goto fail_trans;

	/* Start up the scand thread */
	error = do_thread(sdp, gfs2_scand);
	if (error) {
		printk("GFS2: fsid=%s: can't start scand thread: %d\n",
		       sdp->sd_fsname, error);
		RETURN(G2FN_INIT_LOCKING, error);
	}

	/* Start up the glockd thread */
	for (sdp->sd_glockd_num = 0;
	     sdp->sd_glockd_num < sdp->sd_args.ar_num_glockd;
	     sdp->sd_glockd_num++) {
		error = do_thread(sdp, gfs2_glockd);
		if (error) {
			printk("GFS2: fsid=%s: can't start glockd thread: %d\n",
			       sdp->sd_fsname, error);
			goto fail;
		}
	}

	/* Only one node may mount at a time */
	error = gfs2_glock_nq_num(sdp,
				 GFS2_MOUNT_LOCK, &gfs2_nondisk_glops,
				 LM_ST_EXCLUSIVE, LM_FLAG_NOEXP | GL_NOCACHE,
				 mount_gh);
	if (error) {
		printk("GFS2: fsid=%s: can't acquire mount glock: %d\n",
		       sdp->sd_fsname, error);
		goto fail;
	}

	/* Show that cluster is alive */
	error = gfs2_glock_nq_num(sdp,
				 GFS2_LIVE_LOCK, &gfs2_nondisk_glops,
				 LM_ST_SHARED, LM_FLAG_NOEXP | GL_EXACT,
				 &sdp->sd_live_gh);
	if (error) {
		printk("GFS2: fsid=%s: can't acquire live glock: %d\n",
		       sdp->sd_fsname, error);
		goto fail_mount;
	}
	sdp->sd_live_gh.gh_owner = NULL;

	/* Get a handle on the rename lock */
	error = gfs2_glock_get(sdp, GFS2_RENAME_LOCK, &gfs2_nondisk_glops,
			      CREATE, &sdp->sd_rename_gl);
	if (error) {
		printk("GFS2: fsid=%s: can't create rename glock: %d\n",
		       sdp->sd_fsname, error);
		goto fail_live;
	}

	/* Get a handle on the transaction glock; we need this for disk format
	    upgrade and journal replays, as well as normal operation. */
	error = gfs2_glock_get(sdp, GFS2_TRANS_LOCK, &gfs2_trans_glops,
			      CREATE, &sdp->sd_trans_gl);
	if (error) {
		printk("GFS2: fsid=%s: can't create transaction glock: %d\n",
		       sdp->sd_fsname, error);
		goto fail_rename;
	}
	set_bit(GLF_STICKY, &sdp->sd_trans_gl->gl_flags);

	RETURN(G2FN_INIT_LOCKING, 0);

 fail_trans:
	gfs2_glock_put(sdp->sd_trans_gl);

 fail_rename:
	gfs2_glock_put(sdp->sd_rename_gl);

 fail_live:
	gfs2_glock_dq_uninit(&sdp->sd_live_gh);

 fail_mount:
	gfs2_glock_dq_uninit(mount_gh);

 fail:
	clear_bit(SDF_GLOCKD_RUN, &sdp->sd_flags);
	wake_up(&sdp->sd_reclaim_wq);
	while (sdp->sd_glockd_num--)
		wait_for_completion(&sdp->sd_thread_completion);

	down(&sdp->sd_thread_lock);
	clear_bit(SDF_SCAND_RUN, &sdp->sd_flags);
	wake_up_process(sdp->sd_scand_process);
	up(&sdp->sd_thread_lock);
	wait_for_completion(&sdp->sd_thread_completion);

	RETURN(G2FN_INIT_LOCKING, error);
}

static int
init_sb(struct gfs2_sbd *sdp, int silent, int undo)
{
	ENTER(G2FN_INIT_SB)
	struct super_block *sb = sdp->sd_vfs;
	struct gfs2_holder sb_gh;
	int error = 0;

	if (undo) {
		gfs2_inode_put(sdp->sd_master_dir);
		RETURN(G2FN_INIT_SB, 0);
	}
	
	/* Read the SuperBlock from disk, get enough info to enable us
	   to read-in the journal index and replay all journals. */
	error = gfs2_glock_nq_num(sdp,
				 GFS2_SB_LOCK, &gfs2_meta_glops,
				 LM_ST_SHARED, 0, &sb_gh);
	if (error) {
		printk("GFS2: fsid=%s: can't acquire superblock glock: %d\n",
		       sdp->sd_fsname, error);
		RETURN(G2FN_INIT_SB, error);
	}

	error = gfs2_read_sb(sdp, sb_gh.gh_gl, silent);
	if (error) {
		printk("GFS2: fsid=%s: can't read superblock: %d\n",
		       sdp->sd_fsname, error);
		goto out;
	}

	/* Set up the buffer cache and SB for real, now that we know block
	   sizes, version #s, locations of important on-disk inodes, etc. */
	error = -EINVAL;
	if (sdp->sd_sb.sb_bsize < bdev_hardsect_size(sb->s_bdev)) {
		printk("GFS2: fsid=%s: FS block size (%u) is too small for device block size (%u)\n",
		       sdp->sd_fsname,
		       sdp->sd_sb.sb_bsize, bdev_hardsect_size(sb->s_bdev));
		goto out;
	}
	if (sdp->sd_sb.sb_bsize > PAGE_SIZE) {
		printk("GFS2: fsid=%s: FS block size (%u) is too big for machine page size (%u)\n",
		       sdp->sd_fsname,
		       sdp->sd_sb.sb_bsize, (unsigned int)PAGE_SIZE);
		goto out;
	}

	/* Get rid of buffers from the original block size */
	sb_gh.gh_gl->gl_ops->go_inval(sb_gh.gh_gl, DIO_METADATA | DIO_DATA);
	sb_gh.gh_gl->gl_aspace->i_blkbits = sdp->sd_sb.sb_bsize_shift;

	sb_set_blocksize(sb, sdp->sd_sb.sb_bsize);
	set_blocksize(gfs2_diaper_2real(sb->s_bdev), sdp->sd_sb.sb_bsize);

	error = gfs2_lookup_master_dir(sdp);
	if (error)
		printk("GFS2: fsid=%s: can't read in master directory: %d\n",
		       sdp->sd_fsname, error);

 out:
	gfs2_glock_dq_uninit(&sb_gh);

	RETURN(G2FN_INIT_SB, error);
}

static int
init_journal(struct gfs2_sbd *sdp, int undo)
{
	ENTER(G2FN_INIT_JOURNAL)
	struct gfs2_holder ji_gh;
	int jindex = TRUE;
	int error = 0;

	if (undo) {
		jindex = FALSE;
		goto fail_recoverd;
	}

	error = gfs2_lookup_simple(sdp->sd_master_dir, "jindex", &sdp->sd_jindex);
	if (error) {
		printk("GFS2: fsid=%s: can't lookup journal index: %d\n",
		       sdp->sd_fsname, error);
		RETURN(G2FN_INIT_JOURNAL, error);
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
		/* Discover this node's journal number (lock module tells us
		   which one to use), and lock it */
		if (sdp->sd_lockstruct.ls_jid >= gfs2_jindex_size(sdp)) {
			printk("GFS2: fsid=%s: can't mount journal #%u\n",
			       sdp->sd_fsname, sdp->sd_lockstruct.ls_jid);
			printk("GFS2: fsid=%s: there are only %u journals (0 - %u)\n",
			       sdp->sd_fsname,
			       gfs2_jindex_size(sdp), gfs2_jindex_size(sdp) - 1);
			goto fail_jindex;
		}
		sdp->sd_jdesc = gfs2_jdesc_find(sdp, sdp->sd_lockstruct.ls_jid);

		error = gfs2_glock_nq_num(sdp,
					 sdp->sd_lockstruct.ls_jid, &gfs2_journal_glops,
					 LM_ST_EXCLUSIVE, LM_FLAG_NOEXP,
					 &sdp->sd_journal_gh);
		if (error) {
			printk("GFS2: fsid=%s: can't acquire the journal glock: %d\n",
			       sdp->sd_fsname, error);
			goto fail_jindex;
		}

		error = gfs2_glock_nq_init(sdp->sd_jdesc->jd_inode->i_gl,
					  LM_ST_SHARED, LM_FLAG_NOEXP | GL_EXACT,
					  &sdp->sd_jinode_gh);
		if (error) {
			printk("gfs2: fsid=%s: can't acquire out journal inode glock: %d\n",
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
		/* We're first node within cluster to mount this filesystem,
		   replay ALL of the journals, then let lock module know
		   that we're done. */
		unsigned int x;
		for (x = 0; x < sdp->sd_journals; x++) {
			error = gfs2_recover_journal(gfs2_jdesc_find(sdp, x), WAIT);
			if (error) {
				printk("GFS2: fsid=%s: error recovering journal %u: %d\n",
				       sdp->sd_fsname, x, error);
				goto fail_jinode_gh;
			}
		}

		gfs2_lm_others_may_mount(sdp);
	} else if (!sdp->sd_args.ar_spectator) {
		/* We're not the first; replay only our own journal. */
		error = gfs2_recover_journal(sdp->sd_jdesc, WAIT);
		if (error) {
			printk("GFS2: fsid=%s: error recovering my journal: %d\n",
			       sdp->sd_fsname, error);
			goto fail_jinode_gh;
		}
	}

	gfs2_glock_dq_uninit(&ji_gh);
	jindex = FALSE;

	/* Disown my Journal glock */

	sdp->sd_journal_gh.gh_owner = NULL;
	sdp->sd_jinode_gh.gh_owner = NULL;

	/* Start up the journal recovery thread */

	error = do_thread(sdp, gfs2_recoverd);
	if (error) {
		printk("GFS2: fsid=%s: can't start recoverd thread: %d\n",
		       sdp->sd_fsname, error);
		goto fail_jinode_gh;
	}

	/* Throw out of cache any data that we read in before replay. */

	/* Do stuff */

	RETURN(G2FN_INIT_JOURNAL, 0);

 fail_recoverd:
	down(&sdp->sd_thread_lock);
	clear_bit(SDF_RECOVERD_RUN, &sdp->sd_flags);
	wake_up_process(sdp->sd_recoverd_process);
	up(&sdp->sd_thread_lock);
	wait_for_completion(&sdp->sd_thread_completion);

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

	RETURN(G2FN_INIT_JOURNAL, error);
}

static int
init_inodes(struct gfs2_sbd *sdp, int undo)
{
	ENTER(G2FN_INIT_INODES)
	struct inode *inode;
	struct dentry **dentry = &sdp->sd_vfs->s_root;
	int error = 0;

	if (undo)
		goto fail_dput;

	error = gfs2_lookup_simple(sdp->sd_master_dir, "inum", &sdp->sd_inum_inode);
	if (error) {
		printk("GFS2: fsid=%s: can't read in inum inode: %d\n",
		       sdp->sd_fsname, error);
		RETURN(G2FN_INIT_INODES, error);
	}

	/* Read in the resource index inode */

	error = gfs2_lookup_simple(sdp->sd_master_dir, "rindex", &sdp->sd_rindex);
	if (error) {
		printk("GFS2: fsid=%s: can't get resource index inode: %d\n",
		       sdp->sd_fsname, error);
		goto fail;
	}
	set_bit(GLF_STICKY, &sdp->sd_rindex->i_gl->gl_flags);
	sdp->sd_rindex_vn = sdp->sd_rindex->i_gl->gl_vn - 1;

	/* Read in the quota inode */

	error = gfs2_lookup_simple(sdp->sd_master_dir, "quota", &sdp->sd_quota_inode);
	if (error) {
		printk("GFS2: fsid=%s: can't get quota file inode: %d\n",
		       sdp->sd_fsname, error);
		goto fail_rindex;
	}

	/* Get the root inode */

	error = gfs2_lookup_simple(sdp->sd_master_dir, "root", &sdp->sd_root_inode);
	if (error) {
		printk("GFS2: fsid=%s: can't read in root inode: %d\n",
		       sdp->sd_fsname, error);
		goto fail_qinode;
	}

	/* Get the root inode/dentry */

	inode = gfs2_iget(sdp->sd_root_inode, CREATE);
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

	RETURN(G2FN_INIT_INODES, 0);

 fail_dput:
	dput(*dentry);
	*dentry = NULL;

 fail_rooti:
	gfs2_inode_put(sdp->sd_root_inode);

 fail_qinode:
	gfs2_inode_put(sdp->sd_quota_inode);

 fail_rindex:
	gfs2_clear_rgrpd(sdp);
	gfs2_inode_put(sdp->sd_rindex);

 fail:
	gfs2_inode_put(sdp->sd_inum_inode);

	RETURN(G2FN_INIT_INODES, error);
}

static int
init_per_node(struct gfs2_sbd *sdp, int undo)
{
	ENTER(G2FN_INIT_PER_NODE)
       	struct gfs2_inode *pn = NULL;
	char buf[30];
	int error = 0;

	if (sdp->sd_args.ar_spectator)
		RETURN(G2FN_INIT_PER_NODE, 0);

	if (undo)
		goto fail_qc_gh;

	error = gfs2_lookup_simple(sdp->sd_master_dir, "per_node", &pn);
	if (error) {
		printk("GFS2: fsid=%s: can't find per_node directory: %d\n",
		       sdp->sd_fsname, error);
		RETURN(G2FN_INIT_PER_NODE, error);
	}

	sprintf(buf, "inum_range%u", sdp->sd_jdesc->jd_jid);
	error = gfs2_lookup_simple(pn, buf, &sdp->sd_ir_inode);
	if (error) {
		printk("GFS2: fsid=%s: can't find local \"ir\" file: %d\n",
		       sdp->sd_fsname, error);
		goto fail;
	}

	sprintf(buf, "unlinked_tag%u", sdp->sd_jdesc->jd_jid);
	error = gfs2_lookup_simple(pn, buf, &sdp->sd_ut_inode);
	if (error) {
		printk("GFS2: fsid=%s: can't find local \"ut\" file: %d\n",
		       sdp->sd_fsname, error);
		goto fail_inum_i;
	}

	sprintf(buf, "quota_change%u", sdp->sd_jdesc->jd_jid);
	error = gfs2_lookup_simple(pn, buf, &sdp->sd_qc_inode);
	if (error) {
		printk("GFS2: fsid=%s: can't find local \"qc\" file: %d\n",
		       sdp->sd_fsname, error);
		goto fail_unlinked_i;
	}

	gfs2_inode_put(pn);
	pn = NULL;

	error = gfs2_glock_nq_init(sdp->sd_ir_inode->i_gl,
				  LM_ST_EXCLUSIVE, 0,
				  &sdp->sd_ir_gh);
	if (error) {
		printk("GFS2: fsid=%s: can't lock local \"inum\" file: %d\n",
		       sdp->sd_fsname, error);
		goto fail_qc_i;
	}
	sdp->sd_ir_gh.gh_owner = NULL;

	error = gfs2_glock_nq_init(sdp->sd_ut_inode->i_gl,
				  LM_ST_EXCLUSIVE, 0,
				  &sdp->sd_ut_gh);
	if (error) {
		printk("GFS2: fsid=%s: can't lock local \"unlinked\" file: %d\n",
		       sdp->sd_fsname, error);
		goto fail_inum_gh;
	}
	sdp->sd_ut_gh.gh_owner = NULL;

	error = gfs2_glock_nq_init(sdp->sd_qc_inode->i_gl,
				  LM_ST_EXCLUSIVE, 0,
				  &sdp->sd_qc_gh);
	if (error) {
		printk("GFS2: fsid=%s: can't lock local \"qc\" file: %d\n",
		       sdp->sd_fsname, error);
		goto fail_unlinked_gh;
	}
	sdp->sd_qc_gh.gh_owner = NULL;

	RETURN(G2FN_INIT_PER_NODE, 0);

 fail_qc_gh:
	gfs2_glock_dq_uninit(&sdp->sd_qc_gh);

 fail_unlinked_gh:
	gfs2_glock_dq_uninit(&sdp->sd_ut_gh);

 fail_inum_gh:
	gfs2_glock_dq_uninit(&sdp->sd_ir_gh);

 fail_qc_i:
	gfs2_inode_put(sdp->sd_qc_inode);

 fail_unlinked_i:
	gfs2_inode_put(sdp->sd_ut_inode);

 fail_inum_i:
	gfs2_inode_put(sdp->sd_ir_inode);

 fail:
	if (pn)
		gfs2_inode_put(pn);
	RETURN(G2FN_INIT_PER_NODE, error);
}

static int
init_threads(struct gfs2_sbd *sdp, int undo)
{
	ENTER(G2FN_INIT_THREADS)
	int error = 0;

	if (undo)
		goto fail_inoded;

	/* Start up the logd thread */

	sdp->sd_jindex_refresh_time = jiffies;

	error = do_thread(sdp, gfs2_logd);
	if (error) {
		printk("GFS2: fsid=%s: can't start logd thread: %d\n",
		       sdp->sd_fsname, error);
		RETURN(G2FN_INIT_THREADS, error);
	}

	/* Start up the quotad thread */

	error = do_thread(sdp, gfs2_quotad);
	if (error) {
		printk("GFS2: fsid=%s: can't start quotad thread: %d\n",
		       sdp->sd_fsname, error);
		goto fail;
	}

	/* Start up the inoded thread */

	error = do_thread(sdp, gfs2_inoded);
	if (error) {
		printk("GFS2: fsid=%s: can't start inoded thread: %d\n",
		       sdp->sd_fsname, error);
		goto fail_quotad;
	}

	RETURN(G2FN_INIT_THREADS, 0);

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

 fail:
	down(&sdp->sd_thread_lock);
	clear_bit(SDF_LOGD_RUN, &sdp->sd_flags);
	wake_up_process(sdp->sd_logd_process);
	up(&sdp->sd_thread_lock);
	wait_for_completion(&sdp->sd_thread_completion);
	
	RETURN(G2FN_INIT_THREADS, error);
}

/**
 * gfs2_read_super - Read in superblock
 * @sb: The VFS superblock
 * @data: Mount options
 * @silent: Don't complain if it's not a GFS2 filesystem
 *
 * Returns: errno
 *
 * After cross-linking Linux VFS incore superblock and our GFS2 incore superblock
 *   (filesystem instance structures) to one another, we:
 * -- Init some of our GFS2 incore superblock, including some temporary
 *       block-size values (enough to read on-disk superblock).
 * -- Set up some things in Linux VFS superblock.
 * -- Mount a lock module, init glock system (incl. glock reclaim daemons),
 *       and init some important inter-node locks (MOUNT, LIVE, SuperBlock).
 * -- Read-in the GFS2 on-disk superblock (1st time, to get enough info
 *       to do filesystem upgrade and journal replay, incl. journal index).
 * -- Upgrade on-disk filesystem format (rarely needed).
 * -- Replay journal(s) (always; replay *all* journals if we're first-to-mount).
 * -- Read-in on-disk superblock and journal index special file again (2nd time,
 *       assumed 100% valid now after journal replay).
 * -- Read-in info on other special (hidden) files (root inode, resource index,
 *       quota inode, license inode).
 * -- Start other daemons (journal/log recovery, log tail, quota updates, inode
 *       reclaim) for periodic maintenance.
 * 
 */

static int
fill_super(struct super_block *sb, void *data, int silent)
{
	ENTER(G2FN_FILL_SUPER)
	struct gfs2_sbd *sdp;
	struct gfs2_holder mount_gh;
	int error;

	sdp = init_sbd(sb);
	if (!sdp) {
		printk("GFS2: can't alloc struct gfs2_sbd\n");
		RETURN(G2FN_FILL_SUPER, -ENOMEM);
	}

	gfs2_diaper_register_sbd(sb->s_bdev, sdp);

	error = gfs2_make_args((char *)data, &sdp->sd_args);
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

	error = init_threads(sdp, DO);
	if (error)
		goto fail_per_node;

	gfs2_proc_fs_add(sdp);

	/* Make the FS read/write */
	if (!test_bit(SDF_ROFS, &sdp->sd_flags)) {
		error = gfs2_make_fs_rw(sdp);
		if (error) {
			printk("GFS2: fsid=%s: can't make FS RW: %d\n",
			       sdp->sd_fsname, error);
			goto fail_proc;
		}
	}

	gfs2_glock_dq_uninit(&mount_gh);

	{
		void *x;

		x = kmalloc(1, GFP_KERNEL);
		if (!x)
			printk("Failed\n");
		kmalloc_nofail(1, GFP_KERNEL);
	}

	RETURN(G2FN_FILL_SUPER, 0);

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
	vfs2sdp(sb) = NULL;

	RETURN(G2FN_FILL_SUPER, error);
}

/**
 * gfs2_test_bdev_super - 
 * @sb:
 * @data:
 *
 */

int
gfs2_test_bdev_super(struct super_block *sb, void *data)
{
	ENTER(G2FN_TEST_BDEV_SUPER)
	RETURN(G2FN_TEST_BDEV_SUPER,
	       (void *)sb->s_bdev == data);
}

/**
 * gfs2_test_bdev_super -
 * @sb:
 * @data:
 *
 */

int
gfs2_set_bdev_super(struct super_block *sb, void *data)
{
	ENTER(G2FN_SET_BDEV_SUPER)
	sb->s_bdev = data;
	sb->s_dev = sb->s_bdev->bd_dev;
	RETURN(G2FN_SET_BDEV_SUPER, 0);
}

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

struct super_block *
gfs2_get_sb(struct file_system_type *fs_type, int flags,
	   const char *dev_name, void *data)
{
	ENTER(G2FN_GET_SB)
	struct block_device *real, *diaper;
	struct super_block *sb;
	int error = 0;

	real = open_bdev_excl(dev_name, flags, fs_type);
	if (IS_ERR(real))
		RETURN(G2FN_GET_SB, (struct super_block *)real);

	diaper = gfs2_diaper_get(real, flags);
	if (IS_ERR(diaper)) {
		close_bdev_excl(real);
		RETURN(G2FN_GET_SB, (struct super_block *)diaper);
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

	RETURN(G2FN_GET_SB, sb);

 out:
	gfs2_diaper_put(diaper);
	close_bdev_excl(real);
	RETURN(G2FN_GET_SB, sb);
}

/**
 * gfs2_kill_sb - 
 * @sb:
 *
 * Rip off of kill_block_super().
 *
 */

void
gfs2_kill_sb(struct super_block *sb)
{
	ENTER(G2FN_KILL_SB)
	struct block_device *diaper = sb->s_bdev;
	struct block_device *real = gfs2_diaper_2real(diaper);
	unsigned long bsize = sb->s_old_blocksize;

	generic_shutdown_super(sb);
	set_blocksize(diaper, bsize);
	set_blocksize(real, bsize);
	gfs2_diaper_put(diaper);
	close_bdev_excl(real);

	RET(G2FN_KILL_SB);
}

struct file_system_type gfs2_fs_type = {
	.name = "gfs2",
	.fs_flags = FS_REQUIRES_DEV,
	.get_sb = gfs2_get_sb,
	.kill_sb = gfs2_kill_sb,
	.owner = THIS_MODULE,
};
