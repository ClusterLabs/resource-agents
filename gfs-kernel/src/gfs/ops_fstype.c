#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/vmalloc.h>
#include <linux/blkdev.h>
#include <linux/kthread.h>

#include "gfs.h"
#include "daemon.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "lm.h"
#include "mount.h"
#include "ops_export.h"
#include "ops_fstype.h"
#include "ops_super.h"
#include "proc.h"
#include "quota.h"
#include "recovery.h"
#include "rgrp.h"
#include "super.h"
#include "sys.h"
#include "unlinked.h"

#define DO 0
#define UNDO 1

extern struct dentry_operations gfs_dops;

static struct gfs_sbd *init_sbd(struct super_block *sb)
{
	struct gfs_sbd *sdp;
	unsigned int x;

	sdp = vmalloc(sizeof(struct gfs_sbd));
	if (!sdp)
		return NULL;

	memset(sdp, 0, sizeof(struct gfs_sbd));

	set_v2sdp(sb, sdp);
	sdp->sd_vfs = sb;
	gfs_tune_init(&sdp->sd_tune);

	/*  Init rgrp variables  */

	INIT_LIST_HEAD(&sdp->sd_rglist);
	init_MUTEX(&sdp->sd_rindex_lock);
	INIT_LIST_HEAD(&sdp->sd_rg_mru_list);
	spin_lock_init(&sdp->sd_rg_mru_lock);
	INIT_LIST_HEAD(&sdp->sd_rg_recent);
	spin_lock_init(&sdp->sd_rg_recent_lock);
	spin_lock_init(&sdp->sd_rg_forward_lock);

	spin_lock_init(&sdp->sd_statfs_spin);

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

	spin_lock_init(&sdp->sd_log_seg_lock);
	INIT_LIST_HEAD(&sdp->sd_log_seg_list);
	init_waitqueue_head(&sdp->sd_log_seg_wait);
	INIT_LIST_HEAD(&sdp->sd_log_ail);
	INIT_LIST_HEAD(&sdp->sd_log_incore);
	init_rwsem(&sdp->sd_log_lock);
	INIT_LIST_HEAD(&sdp->sd_unlinked_list);
	spin_lock_init(&sdp->sd_unlinked_lock);
	INIT_LIST_HEAD(&sdp->sd_quota_list);
	spin_lock_init(&sdp->sd_quota_lock);

	INIT_LIST_HEAD(&sdp->sd_dirty_j);
	spin_lock_init(&sdp->sd_dirty_j_lock);

	spin_lock_init(&sdp->sd_ail_lock);
	INIT_LIST_HEAD(&sdp->sd_recovery_bufs);

	return sdp;
}

static void init_vfs(struct super_block *sb, unsigned noatime)
{
	struct gfs_sbd *sdp = sb->s_fs_info;

	/*  Set up Linux Virtual (VFS) Super Block  */

	sb->s_magic = GFS_MAGIC;
	sb->s_op = &gfs_super_ops;
	sb->s_export_op = &gfs_export_ops;

	/*  Don't let the VFS update atimes.  GFS handles this itself. */
	sb->s_flags |= MS_NOATIME | MS_NODIRATIME;
	sb->s_maxbytes = MAX_LFS_FILESIZE;

	/*  If we were mounted with -o acl (to support POSIX access control
	    lists), tell VFS */
	if (sdp->sd_args.ar_posix_acls)
		sb->s_flags |= MS_POSIXACL;
}

int init_names(struct gfs_sbd *sdp, int silent)
{
	struct gfs_sb *sb = NULL;
	char *proto, *table;
	int error = 0;

	proto = sdp->sd_args.ar_lockproto;
	table = sdp->sd_args.ar_locktable;

	/*  Try to autodetect  */

	if (!proto[0] || !table[0]) {
		struct buffer_head *bh;

		bh = sb_getblk(sdp->sd_vfs,
			       GFS_SB_ADDR >> sdp->sd_fsb2bb_shift);
		lock_buffer(bh);
		clear_buffer_uptodate(bh);
		clear_buffer_dirty(bh);
		unlock_buffer(bh);
		ll_rw_block(READ, 1, &bh);
		wait_on_buffer(bh);

		if (!buffer_uptodate(bh)) {
			brelse(bh);
			return -EIO;
		}

		sb = kmalloc(sizeof(struct gfs_sb), GFP_KERNEL);
		if (!sb) {
			brelse(bh);
			return -ENOMEM;
		}
		gfs_sb_in(sb, bh->b_data); 
		brelse(bh);

		error = gfs_check_sb(sdp, sb, silent);
		if (error)
			goto out;

		if (!proto[0])
			proto = sb->sb_lockproto;
		if (!table[0])
			table = sb->sb_locktable;
	}

	if (!table[0])
		table = sdp->sd_vfs->s_id;

	snprintf(sdp->sd_proto_name, 256, "%s", proto);
	snprintf(sdp->sd_table_name, 256, "%s", table);

	while ((table = strchr(sdp->sd_table_name, '/')))
		*table = '_';

 out:
	kfree(sb);

	return error;
}

static int init_locking(struct gfs_sbd *sdp, struct gfs_holder *mount_gh,
						int undo)
{
	struct task_struct *p;
	int error = 0;

	if (undo)
		goto fail_live;

	if ((sdp->sd_lockstruct.ls_flags & LM_LSFLAG_LOCAL) &&
	    !sdp->sd_args.ar_ignore_local_fs) {
		/* Force local [p|f]locks */
		sdp->sd_args.ar_localflocks = TRUE;

		/* Force local read ahead and caching */
		sdp->sd_args.ar_localcaching = TRUE;

		/* Allow the machine to oops */
		sdp->sd_args.ar_oopses_ok = TRUE;
	}

	/*  Start up the scand thread  */

	p = kthread_run(gfs_scand, sdp, "gfs_scand");
	error = IS_ERR(p);
	if (error) {
		printk("GFS: fsid=%s: can't start scand thread: %d\n",
		       sdp->sd_fsname, error);
		return error;
	}
	sdp->sd_scand_process = p;

	/*  Start up the glockd thread  */

	for (sdp->sd_glockd_num = 0;
	     sdp->sd_glockd_num < sdp->sd_args.ar_num_glockd;
	     sdp->sd_glockd_num++) {
		p = kthread_run(gfs_glockd, sdp, "gfs_glockd");
		error = IS_ERR(p);
		if (error) {
			printk("GFS: fsid=%s: can't start glockd thread: %d\n",
			       sdp->sd_fsname, error);
			goto fail;
		}
		sdp->sd_glockd_process[sdp->sd_glockd_num] = p;
	}

	/*  Only one node may mount at a time */
	error = gfs_glock_nq_num(sdp,
				 GFS_MOUNT_LOCK, &gfs_nondisk_glops,
				 LM_ST_EXCLUSIVE, LM_FLAG_NOEXP | GL_NOCACHE,
				 mount_gh);
	if (error) {
		printk("GFS: fsid=%s: can't acquire mount glock: %d\n",
		       sdp->sd_fsname, error);
		goto fail;
	}

	/*  Show that cluster is alive */
	error = gfs_glock_nq_num(sdp,
				 GFS_LIVE_LOCK, &gfs_nondisk_glops,
				 LM_ST_SHARED, LM_FLAG_NOEXP | GL_EXACT,
				 &sdp->sd_live_gh);
	if (error) {
		printk("GFS: fsid=%s: can't acquire live glock: %d\n",
		       sdp->sd_fsname, error);
		goto fail_mount;
	}

	sdp->sd_live_gh.gh_owner = NULL;
	return 0;

fail_live:
	gfs_glock_dq_uninit(&sdp->sd_live_gh);

fail_mount:
	gfs_glock_dq_uninit(mount_gh);

fail:
	while (sdp->sd_glockd_num--)
		kthread_stop(sdp->sd_glockd_process[sdp->sd_glockd_num]);

	kthread_stop(sdp->sd_scand_process);

	return error;
}

static int init_sb(struct gfs_sbd *sdp, int silent, int undo)
{
	struct super_block *sb = sdp->sd_vfs;
	struct gfs_holder sb_gh;
	int error = 0;
	struct inode *inode;

	if (undo)
		goto fail_dput;

	/*  Read the SuperBlock from disk, get enough info to enable us
	    to read-in the journal index and replay all journals. */

	error = gfs_glock_nq_num(sdp,
				 GFS_SB_LOCK, &gfs_meta_glops,
				 (sdp->sd_args.ar_upgrade) ? LM_ST_EXCLUSIVE : LM_ST_SHARED,
				 0, &sb_gh);
	if (error) {
		printk("GFS: fsid=%s: can't acquire superblock glock: %d\n",
		       sdp->sd_fsname, error);
		return error;
	}

	error = gfs_read_sb(sdp, sb_gh.gh_gl, silent);
	if (error) {
		printk("GFS: fsid=%s: can't read superblock: %d\n",
		       sdp->sd_fsname, error);
		goto out;
	}

	/*  Set up the buffer cache and SB for real, now that we know block
	      sizes, version #s, locations of important on-disk inodes, etc.  */

	error = -EINVAL;
	if (sdp->sd_sb.sb_bsize < bdev_hardsect_size(sb->s_bdev)) {
		printk("GFS: fsid=%s: FS block size (%u) is too small for device block size (%u)\n",
		       sdp->sd_fsname, sdp->sd_sb.sb_bsize, bdev_hardsect_size(sb->s_bdev));
		goto fail;
	}
	if (sdp->sd_sb.sb_bsize > PAGE_SIZE) {
		printk("GFS: fsid=%s: FS block size (%u) is too big for machine page size (%u)\n",
		       sdp->sd_fsname, sdp->sd_sb.sb_bsize,
		       (unsigned int)PAGE_SIZE);
		goto fail;
	}

	/*  Get rid of buffers from the original block size  */
	sb_gh.gh_gl->gl_ops->go_inval(sb_gh.gh_gl, DIO_METADATA | DIO_DATA);
	sb_gh.gh_gl->gl_aspace->i_blkbits = sdp->sd_sb.sb_bsize_shift;

	sb_set_blocksize(sb, sdp->sd_sb.sb_bsize);

	/*  Read in the resource index inode  */

	error = gfs_get_riinode(sdp);
	if (error) {
		printk("GFS: fsid=%s: can't get resource index inode: %d\n",
		       sdp->sd_fsname, error);
		goto fail;
	}

	/*  Get the root inode  */
	error = gfs_get_rootinode(sdp);
	if (error) {
		printk("GFS: fsid=%s: can't read in root inode: %d\n",
		       sdp->sd_fsname, error);
		goto fail_ri_free;
	}
	/*  Get the root inode/dentry  */

	inode = gfs_iget(sdp->sd_rooti, CREATE);
	if (!inode) {
		printk("GFS: fsid=%s: can't get root inode\n", sdp->sd_fsname);
		error = -ENOMEM;
		goto fail_ri_free;
	}
	sb->s_root = d_alloc_root(inode);
	if (!sb->s_root) {
		iput(inode);
		printk("GFS: fsid=%s: can't get root dentry\n", sdp->sd_fsname);
		error = -ENOMEM;
		goto fail_root_free;
	}
	sb->s_root->d_op = &gfs_dops;

	/*  Read in the quota inode  */
	error = gfs_get_qinode(sdp);
	if (error) {
		printk("GFS: fsid=%s: can't get quota file inode: %d\n",
		       sdp->sd_fsname, error);
		goto fail_root_free;
	}

	/* Implement fast statfs on the unused license inode location.
	 * sb->sb_quota_di.no_formal_ino = jindex_dinode + 2;
	 * sb->sb_quota_di.no_addr = jindex_dinode + 2;
	 * sb->sb_license_di.no_formal_ino = jindex_dinode + 3;
	 * sb->sb_license_di.no_addr = jindex_dinode + 3;
	 */
	error = gfs_get_linode(sdp);
	if (error) {
		printk("GFS: fsid=%s: can't get statfs file inode: %d\n",
				sdp->sd_fsname, error);
		goto fail_qi_free;
	}

	/*  We're through with the superblock lock  */
out:
	gfs_glock_dq_uninit(&sb_gh);
	return error;

fail_dput:
	gfs_inode_put(sdp->sd_linode);
	if (sb->s_root) {
		dput(sb->s_root);
		sb->s_root = NULL;
	}
fail_qi_free:
	gfs_inode_put(sdp->sd_qinode);
fail_root_free:
	gfs_inode_put(sdp->sd_rooti);
fail_ri_free:
	gfs_inode_put(sdp->sd_riinode);
	gfs_clear_rgrpd(sdp);
fail:
	if (!undo)
		gfs_glock_dq_uninit(&sb_gh);
	return error;
}

static int init_journal(struct gfs_sbd *sdp, int undo)
{
	struct gfs_holder ji_gh;
	int error = 0;
	unsigned int x;
	int jindex = TRUE;
	struct task_struct *p;

	if (undo) {
		jindex = 0;
		goto fail_recoverd;
	}

	init_MUTEX(&sdp->sd_jindex_lock);

	/*  Get a handle on the transaction glock; we need this for disk format
	    upgrade and journal replays, as well as normal operation.  */

	error = gfs_glock_get(sdp, GFS_TRANS_LOCK, &gfs_trans_glops,
			      CREATE, &sdp->sd_trans_gl);
	if (error)
		return error;
	set_bit(GLF_STICKY, &sdp->sd_trans_gl->gl_flags);

	/*  Load in the journal index special file */

	error = gfs_jindex_hold(sdp, &ji_gh);
	if (error) {
		printk("GFS: fsid=%s: can't read journal index: %d\n",
		       sdp->sd_fsname, error);
		goto fail_jhold;
	}

	if (sdp->sd_args.ar_spectator) {
		sdp->sd_jdesc = sdp->sd_jindex[0];
		sdp->sd_log_seg_free = sdp->sd_jdesc.ji_nsegment;
		sdp->sd_log_seg_ail2 = 0;
	}
	else {
		/*  Discover this node's journal number (lock module tells us
		    which one to use), and lock it */
		error = -EINVAL;
		if (sdp->sd_lockstruct.ls_jid >= sdp->sd_journals) {
			printk("GFS: fsid=%s: can't mount journal #%u\n",
			       sdp->sd_fsname, sdp->sd_lockstruct.ls_jid);
			printk("GFS: fsid=%s: there are only %u journals (0 - %u)\n",
			       sdp->sd_fsname, sdp->sd_journals, sdp->sd_journals - 1);
			goto fail_jindex;
		}
		sdp->sd_jdesc = sdp->sd_jindex[sdp->sd_lockstruct.ls_jid];
		sdp->sd_log_seg_free = sdp->sd_jdesc.ji_nsegment;
		sdp->sd_log_seg_ail2 = 0;

		error = gfs_glock_nq_num(sdp,
					 sdp->sd_jdesc.ji_addr, &gfs_meta_glops,
					 LM_ST_EXCLUSIVE, LM_FLAG_NOEXP,
					 &sdp->sd_journal_gh);
		if (error) {
			printk("GFS: fsid=%s: can't acquire the journal glock: %d\n",
			       sdp->sd_fsname, error);
			goto fail_jindex;
		}
	}

	if (sdp->sd_lockstruct.ls_first) {
		/*  We're first node within cluster to mount this filesystem,
		    replay ALL of the journals, then let lock module know
		    that we're done. */
		for (x = 0; x < sdp->sd_journals; x++) {
			error = gfs_recover_journal(sdp,
						    x, sdp->sd_jindex + x,
						    FALSE);
			if (error) {
				printk("GFS: fsid=%s: error recovering journal %u: %d\n",
				       sdp->sd_fsname, x, error);
				goto fail_journal_gh;
			}
		}

		gfs_lm_others_may_mount(sdp);
	} else if (!sdp->sd_args.ar_spectator) {
		/*  We're not the first; replay only our own journal. */
		error = gfs_recover_journal(sdp, sdp->sd_lockstruct.ls_jid,
									&sdp->sd_jdesc, TRUE);
		if (error) {
			printk("GFS: fsid=%s: error recovering my journal: %d\n",
			       sdp->sd_fsname, error);
			goto fail_journal_gh;
		}
	}

	gfs_glock_dq_uninit(&ji_gh);
	jindex = FALSE;

	/*  Disown my Journal glock  */
	sdp->sd_journal_gh.gh_owner = NULL;

	/*  Make the FS read/write  */

	if (!test_bit(SDF_ROFS, &sdp->sd_flags)) {
		error = gfs_make_fs_rw(sdp);
		if (error) {
			printk("GFS: fsid=%s: can't make file system RW: %d\n",
			       sdp->sd_fsname, error);
			goto fail_journal_gh;
		}
	}

	/*  Start up the journal recovery thread  */

	p = kthread_run(gfs_recoverd, sdp, "gfs_recoverd");
	error = IS_ERR(p);
	if (error) {
		printk("GFS: fsid=%s: can't start recoverd thread: %d\n",
		       sdp->sd_fsname, error);
		goto fail_journal_gh;
	}
	sdp->sd_recoverd_process = p;

	return 0;

fail_recoverd:
	kthread_stop(sdp->sd_recoverd_process);
	sdp->sd_recoverd_process = NULL;

fail_journal_gh:
	if (!sdp->sd_args.ar_spectator)
		gfs_glock_dq_uninit(&sdp->sd_journal_gh);

fail_jindex:
	if (jindex)
		gfs_glock_dq_uninit(&ji_gh);

fail_jhold:
	gfs_glock_put(sdp->sd_trans_gl);
	return error;
}

static int init_threads(struct gfs_sbd *sdp, int undo)
{
	struct task_struct *p;
	int error = 0;

	if (undo)
		goto fail_logd;

	sdp->sd_jindex_refresh_time = jiffies;

	/*  Start up the logd thread  */
	p = kthread_run(gfs_logd, sdp, "gfs_logd");
	error = IS_ERR(p);
	if (error) {
		printk("GFS: fsid=%s: can't start logd thread: %d\n",
		       sdp->sd_fsname, error);
		goto fail;
	}
	sdp->sd_logd_process = p;

	/*  Start up the quotad thread  */

	p = kthread_run(gfs_quotad, sdp, "gfs_quotad");
	if (error < 0) {
		printk("GFS: fsid=%s: can't start quotad thread: %d\n",
		       sdp->sd_fsname, error);
		goto fail_quotad;
	}
	sdp->sd_quotad_process = p;

	/*  Start up the inoded thread  */

	p = kthread_run(gfs_inoded, sdp, "gfs_inoded");
	if (error < 0) {
		printk("GFS: fsid=%s: can't start inoded thread: %d\n",
		       sdp->sd_fsname, error);
		goto fail_inoded;
	}
	sdp->sd_inoded_process = p;
	return 0;

fail_logd:
	kthread_stop(sdp->sd_inoded_process);
fail_inoded:
	kthread_stop(sdp->sd_quotad_process);
fail_quotad:
	kthread_stop(sdp->sd_logd_process);
fail:
	return error;
}

/**
 * fill_super - Read in superblock
 * @sb: The VFS superblock
 * @data: Mount options
 * @silent: Don't complain if it's not a GFS filesystem
 *
 * Returns: errno
 *
 * After cross-linking Linux VFS incore superblock and our GFS incore
 *   superblock (filesystem instance structures) to one another, we:
 * -- Init some of our GFS incore superblock, including some temporary
 *       block-size values (enough to read on-disk superblock).
 * -- Set up some things in Linux VFS superblock.
 * -- Mount a lock module, init glock system (incl. glock reclaim daemons),
 *       and init some important inter-node locks (MOUNT, LIVE, SuperBlock).
 * -- Read-in the GFS on-disk superblock (1st time, to get enough info
 *       to do filesystem upgrade and journal replay, incl. journal index).
 * -- Upgrade on-disk filesystem format (rarely needed).
 * -- Replay journals (always; replay *all* journals if we're first-to-mount).
 * -- Read-in on-disk superblock and journal index special file again
 *       (2nd time, assumed 100% valid now after journal replay).
 * -- Read-in info on other special (hidden) files (root inode, resource index,
 *       quota inode, license inode).
 * -- Start other daemons (journal/log recovery, log tail, quota updates, inode
 *       reclaim) for periodic maintenance.
 * 
 */

static int fill_super(struct super_block *sb, void *data, int silent)
{
	struct gfs_sbd *sdp;
	struct gfs_holder mount_gh;
	int error;

	sdp = init_sbd(sb);
	if (!sdp) {
		printk(KERN_WARNING "GFS: can't alloc struct gfs_sbd\n");
		return -ENOMEM;
	}

	error = gfs_make_args((char *)data, &sdp->sd_args, FALSE);
	if (error) {
		printk("GFS: can't parse mount arguments\n");
		goto fail;
	}

	if (sdp->sd_args.ar_spectator) {
		sb->s_flags |= MS_RDONLY;
		set_bit(SDF_ROFS, &sdp->sd_flags);
	}

	/*  Copy VFS mount flags  */

	if (sb->s_flags & (MS_NOATIME | MS_NODIRATIME))
		set_bit(SDF_NOATIME, &sdp->sd_flags);
	if (sb->s_flags & MS_RDONLY)
		set_bit(SDF_ROFS, &sdp->sd_flags);

	init_vfs(sb, SDF_NOATIME);

	/*  Turn off quota stuff if we get the noquota mount option, don't 
	    need to grab the sd_tune lock here since its before anything 
	    touches the sd_tune values */
	if (sdp->sd_args.ar_noquota) {
		sdp->sd_tune.gt_quota_enforce = 0;
		sdp->sd_tune.gt_quota_account = 0;
	}

	/*  Set up the buffer cache and fill in some fake block size values
	   to allow us to read-in the on-disk superblock.  */

	sdp->sd_sb.sb_bsize = sb_min_blocksize(sb, GFS_BASIC_BLOCK);
	sdp->sd_sb.sb_bsize_shift = sb->s_blocksize_bits;
	sdp->sd_fsb2bb_shift = sdp->sd_sb.sb_bsize_shift - GFS_BASIC_BLOCK_SHIFT;
	sdp->sd_fsb2bb = 1 << sdp->sd_fsb2bb_shift;

	if (sizeof(struct gfs_sb) > sdp->sd_sb.sb_bsize) {
		printk("GFS: sizeof(struct gfs_sb) > sdp->sd_sb.sb_bsize\n"
		       "GFS: %u > %u\n",
		       (unsigned int)sizeof(struct gfs_sb), sdp->sd_sb.sb_bsize);
		error = -EINVAL;
		goto fail;
	}
	error = init_names(sdp, silent);
	if (error)
		goto fail;

	error = gfs_sys_fs_add(sdp);
	if (error)
		goto fail;

	/*  Mount an inter-node lock module, check for local optimizations */

	error = gfs_lm_mount(sdp, silent);
	if (error)
		goto fail_sys;

	error = init_locking(sdp, &mount_gh, DO);
	if (error)
		goto fail_lm;

	error = init_sb(sdp, silent, DO);
	if (error)
		goto fail_locking;

	/*  Read-in journal index inode (but not the file contents, yet)  */

	error = gfs_get_jiinode(sdp);
	if (error) {
		printk("GFS: fsid=%s: can't get journal index inode: %d\n",
		       sdp->sd_fsname, error);
		goto fail_jiinode;
	}

	error = init_journal(sdp, DO);
	if (error)
		goto fail_sb;
	/*  Get a handle on the rename lock  */

	error = gfs_glock_get(sdp, GFS_RENAME_LOCK, &gfs_nondisk_glops,
						  CREATE, &sdp->sd_rename_gl);
	if (error)
		goto fail_journal;

	error = init_threads(sdp, DO);
	if (error)
		goto fail_journal;

	gfs_proc_fs_add(sdp);
	gfs_glock_dq_uninit(&mount_gh);

	return 0;

fail_journal:
	init_journal(sdp, UNDO);

fail_sb:
	gfs_inode_put(sdp->sd_jiinode);

fail_jiinode:
	init_sb(sdp, 0, UNDO);

fail_locking:
	init_locking(sdp, &mount_gh, UNDO);

fail_lm:
	gfs_gl_hash_clear(sdp, TRUE);
	gfs_lm_unmount(sdp);
	gfs_clear_dirty_j(sdp);
	while (invalidate_inodes(sb))
		yield();

fail_sys:
	gfs_sys_fs_del(sdp);

fail:
	vfree(sdp);
	sb->s_fs_info = NULL;

	return error;
}

/**
 * gfs_test_bdev_super - 
 * @sb:
 * @data:
 *
 */

int
gfs_test_bdev_super(struct super_block *sb, void *data)
{
	return (void *)sb->s_bdev == data;
}

/**
 * gfs_test_bdev_super -
 * @sb:
 * @data:
 *
 */

int
gfs_set_bdev_super(struct super_block *sb, void *data)
{
	sb->s_bdev = data;
	sb->s_dev = sb->s_bdev->bd_dev;
	return 0;
}

/**
 * gfs_get_sb - 
 * @fs_type:
 * @flags:
 * @dev_name:
 * @data:
 *
 * Rip off of get_sb_bdev().
 *
 * Returns: the new superblock
 */

static int gfs_get_sb(struct file_system_type *fs_type, int flags,
					  const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, fill_super, mnt);
}

/**
 * gfs_kill_sb - 
 * @sb:
 *
 * Rip off of kill_block_super().
 *
 */

void
gfs_kill_sb(struct super_block *sb)
{
	kill_block_super(sb);
}

struct file_system_type gfs_fs_type = {
	.name = "gfs",
	.fs_flags = FS_REQUIRES_DEV,
	.get_sb = gfs_get_sb,
	.kill_sb = gfs_kill_sb,
	.owner = THIS_MODULE,
};
