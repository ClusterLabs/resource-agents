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
#include <linux/kobject.h>

#include "gfs.h"
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
#include "quota.h"
#include "recovery.h"
#include "rgrp.h"
#include "super.h"
#include "unlinked.h"

static ssize_t id_show(struct gfs_sbd *sdp, char *buf)
{
	return sprintf(buf, "%s\n", sdp->sd_vfs->s_id);
}

static ssize_t fsname_show(struct gfs_sbd *sdp, char *buf)
{
	return sprintf(buf, "%s\n", sdp->sd_fsname);
}

struct gfs_attr {
	struct attribute attr;
	ssize_t (*show)(struct gfs_sbd *, char *);
	ssize_t (*store)(struct gfs_sbd *, const char *, size_t);
};

#define GFS_ATTR(name, mode, show, store) \
static struct gfs_attr gfs_attr_##name = __ATTR(name, mode, show, store)

GFS_ATTR(id,       0444, id_show,       NULL);
GFS_ATTR(fsname,   0444, fsname_show,   NULL);

static struct attribute *gfs_attrs[] = {
	&gfs_attr_id.attr,
	&gfs_attr_fsname.attr,
	NULL,
};

static ssize_t gfs_attr_show(struct kobject *kobj, struct attribute *attr,
			      char *buf)
{
	struct gfs_sbd *sdp = container_of(kobj, struct gfs_sbd, sd_kobj);
	struct gfs_attr *a = container_of(attr, struct gfs_attr, attr);
	return a->show ? a->show(sdp, buf) : 0;
}

static ssize_t gfs_attr_store(struct kobject *kobj, struct attribute *attr,
			       const char *buf, size_t len)
{
	struct gfs_sbd *sdp = container_of(kobj, struct gfs_sbd, sd_kobj);
	struct gfs_attr *a = container_of(attr, struct gfs_attr, attr);
	return a->store ? a->store(sdp, buf, len) : len;
}

static struct sysfs_ops gfs_attr_ops = {
	.show  = gfs_attr_show,
	.store = gfs_attr_store,
};

static struct kobj_type gfs_ktype = {
	.default_attrs = gfs_attrs,
	.sysfs_ops     = &gfs_attr_ops,
};

static struct kset gfs_kset = {
	.subsys = &fs_subsys,
	.kobj   = {.name = "gfs",},
	.ktype  = &gfs_ktype,
};

int gfs_sys_fs_add(struct gfs_sbd *sdp)
{
	int error;

	sdp->sd_kobj.kset = &gfs_kset;
	sdp->sd_kobj.ktype = &gfs_ktype;

	error = kobject_set_name(&sdp->sd_kobj, "%s", sdp->sd_table_name);
	if (error)
		goto fail;

	error = kobject_register(&sdp->sd_kobj);
	if (error)
		goto fail;

	return 0;
 fail:
	return error;
}

void gfs_sys_fs_del(struct gfs_sbd *sdp)
{
	kobject_unregister(&sdp->sd_kobj);
}

int gfs_sys_init(void)
{
	return kset_register(&gfs_kset);
}

void gfs_sys_uninit(void)
{
	kset_unregister(&gfs_kset);
}

static int init_names(struct gfs_sbd *sdp, int silent)
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

 out:
	kfree(sb);

	return error;
}

/**
 * gfs_read_super - Read in superblock
 * @sb: The VFS superblock
 * @data: Mount options
 * @silent: Don't complain if it's not a GFS filesystem
 *
 * Returns: errno
 *
 * After cross-linking Linux VFS incore superblock and our GFS incore superblock
 *   (filesystem instance structures) to one another, we:
 * -- Init some of our GFS incore superblock, including some temporary
 *       block-size values (enough to read on-disk superblock).
 * -- Set up some things in Linux VFS superblock.
 * -- Mount a lock module, init glock system (incl. glock reclaim daemons),
 *       and init some important inter-node locks (MOUNT, LIVE, SuperBlock).
 * -- Read-in the GFS on-disk superblock (1st time, to get enough info
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
	ENTER(GFN_FILL_SUPER)
	struct gfs_sbd *sdp;
	struct gfs_holder mount_gh, sb_gh, ji_gh;
	struct inode *inode;
	int super = TRUE, jindex = TRUE;
	unsigned int x;
	int error;

	sdp = vmalloc(sizeof(struct gfs_sbd));
	if (!sdp) {
		printk("GFS: can't alloc struct gfs_sbd\n");
		error = -ENOMEM;
		goto fail;
	}

	memset(sdp, 0, sizeof(struct gfs_sbd));

	vfs2sdp(sb) = sdp;
	sdp->sd_vfs = sb;
	gfs_diaper_register_sbd(sb->s_bdev, sdp);

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
	init_rwsem(&sdp->sd_log_lock);
	INIT_LIST_HEAD(&sdp->sd_unlinked_list);
	spin_lock_init(&sdp->sd_unlinked_lock);
	INIT_LIST_HEAD(&sdp->sd_quota_list);
	spin_lock_init(&sdp->sd_quota_lock);

	INIT_LIST_HEAD(&sdp->sd_dirty_j);
	spin_lock_init(&sdp->sd_dirty_j_lock);

	spin_lock_init(&sdp->sd_ail_lock);
	INIT_LIST_HEAD(&sdp->sd_recovery_bufs);

	gfs_tune_init(&sdp->sd_tune);

	error = gfs_make_args((char *)data, &sdp->sd_args);
	if (error) {
		printk("GFS: can't parse mount arguments\n");
		goto fail_vfree;
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
		goto fail_vfree;
	}

	error = init_names(sdp, silent);
	if (error)
		goto fail_vfree;

	error = gfs_sys_fs_add(sdp);
	if (error)
		goto fail_vfree;

	/*  Mount an inter-node lock module, check for local optimizations */

	error = gfs_lm_mount(sdp, silent);
	if (error)
		goto fail_sysfs;

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

	/*  Only one node may mount at a time */
	error = gfs_glock_nq_num(sdp,
				 GFS_MOUNT_LOCK, &gfs_nondisk_glops,
				 LM_ST_EXCLUSIVE, LM_FLAG_NOEXP | GL_NOCACHE,
				 &mount_gh);
	if (error) {
		printk("GFS: fsid=%s: can't acquire mount glock: %d\n",
		       sdp->sd_fsname, error);
		goto fail_glockd;
	}

	/*  Show that cluster is alive */
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

	/*  Read the SuperBlock from disk, get enough info to enable us
	    to read-in the journal index and replay all journals. */

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

	/*  Set up the buffer cache and SB for real, now that we know block
	      sizes, version #s, locations of important on-disk inodes, etc.  */

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
	set_blocksize(gfs_diaper_2real(sb->s_bdev), sdp->sd_sb.sb_bsize);

	/*  Read-in journal index inode (but not the file contents, yet)  */

	error = gfs_get_jiinode(sdp);
	if (error) {
		printk("GFS: fsid=%s: can't get journal index inode: %d\n",
		       sdp->sd_fsname, error);
		goto fail_gunlock_sb;
	}

	init_MUTEX(&sdp->sd_jindex_lock);

	/*  Get a handle on the transaction glock; we need this for disk format
	    upgrade and journal replays, as well as normal operation.  */

	error = gfs_glock_get(sdp, GFS_TRANS_LOCK, &gfs_trans_glops,
			      CREATE, &sdp->sd_trans_gl);
	if (error)
		goto fail_ji_free;
	set_bit(GLF_STICKY, &sdp->sd_trans_gl->gl_flags);

	/*  Upgrade GFS on-disk format version numbers if we need to  */

	if (sdp->sd_args.ar_upgrade) {
		error = gfs_do_upgrade(sdp, sb_gh.gh_gl);
		if (error)
			goto fail_trans_gl;
	}

	/*  Load in the journal index special file */

	error = gfs_jindex_hold(sdp, &ji_gh);
	if (error) {
		printk("GFS: fsid=%s: can't read journal index: %d\n",
		       sdp->sd_fsname, error);
		goto fail_trans_gl;
	}

	if (sdp->sd_args.ar_spectator) {
		sdp->sd_jdesc = sdp->sd_jindex[0];
		sdp->sd_log_seg_free = sdp->sd_jdesc.ji_nsegment;
		sdp->sd_log_seg_ail2 = 0;
	} else {
		/*  Discover this node's journal number (lock module tells us
		    which one to use), and lock it */
		error = -EINVAL;
		if (sdp->sd_lockstruct.ls_jid >= sdp->sd_journals) {
			printk("GFS: fsid=%s: can't mount journal #%u\n",
			       sdp->sd_fsname, sdp->sd_lockstruct.ls_jid);
			printk("GFS: fsid=%s: there are only %u journals (0 - %u)\n",
			       sdp->sd_fsname, sdp->sd_journals, sdp->sd_journals - 1);
			goto fail_gunlock_ji;
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
			goto fail_gunlock_ji;
		}
	}

	if (sdp->sd_lockstruct.ls_first) {
		/*  We're first node within cluster to mount this filesystem,
		    replay ALL of the journals, then let lock module know
		    that we're done. */
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

		gfs_lm_others_may_mount(sdp);
	} else if (!sdp->sd_args.ar_spectator) {
		/*  We're not the first; replay only our own journal. */
		error = gfs_recover_journal(sdp,
					    sdp->sd_lockstruct.ls_jid,
					    &sdp->sd_jdesc,
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

	/*  Drop our buffer cache and reread all the things we read before
	    the journal replay, on the unlikely chance that the replay might
	    have affected (corrected/updated) the superblock contents
	    or journal index. */

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

	/*  Start up the journal recovery thread  */

	error = kernel_thread(gfs_recoverd, sdp, 0);
	if (error < 0) {
		printk("GFS: fsid=%s: can't start recoverd thread: %d\n",
		       sdp->sd_fsname, error);
		goto fail_make_ro;
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

	/*  Get the root inode/dentry  */

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

	gfs_proc_fs_add(sdp);

	gfs_glock_dq_uninit(&mount_gh);

	RETURN(GFN_FILL_SUPER, 0);

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

 fail_make_ro:
	gfs_glock_force_drop(sdp->sd_trans_gl);
	clear_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags);
	gfs_unlinked_cleanup(sdp);
	gfs_quota_cleanup(sdp);

 fail_gunlock_journal:
	if (!sdp->sd_args.ar_spectator)
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
	gfs_lm_unmount(sdp);
	gfs_clear_dirty_j(sdp);
	while (invalidate_inodes(sb))
		yield();

 fail_sysfs:
	gfs_sys_fs_del(sdp);

 fail_vfree:
	vfree(sdp);

 fail:
	vfs2sdp(sb) = NULL;
	RETURN(GFN_FILL_SUPER, error);
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
	ENTER(GFN_TEST_BDEV_SUPER)
	RETURN(GFN_TEST_BDEV_SUPER,
	       (void *)sb->s_bdev == data);
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
	ENTER(GFN_SET_BDEV_SUPER)
	sb->s_bdev = data;
	sb->s_dev = sb->s_bdev->bd_dev;
	RETURN(GFN_SET_BDEV_SUPER, 0);
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

struct super_block *
gfs_get_sb(struct file_system_type *fs_type, int flags,
	   const char *dev_name, void *data)
{
	ENTER(GFN_GET_SB)
	struct block_device *real, *diaper;
	struct super_block *sb;
	int error = 0;

	real = open_bdev_excl(dev_name, flags, fs_type);
	if (IS_ERR(real))
		RETURN(GFN_GET_SB, (struct super_block *)real);

	diaper = gfs_diaper_get(real, flags);
	if (IS_ERR(diaper)) {
		close_bdev_excl(real);
		RETURN(GFN_GET_SB, (struct super_block *)diaper);
	}

	down(&diaper->bd_mount_sem);
	sb = sget(fs_type, gfs_test_bdev_super, gfs_set_bdev_super, diaper);
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

	RETURN(GFN_GET_SB, sb);

 out:
	gfs_diaper_put(diaper);
	close_bdev_excl(real);
	RETURN(GFN_GET_SB, sb);
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
	ENTER(GFN_KILL_SB)
	struct block_device *diaper = sb->s_bdev;
	struct block_device *real = gfs_diaper_2real(diaper);
	unsigned long bsize = sb->s_old_blocksize;

	generic_shutdown_super(sb);
	set_blocksize(diaper, bsize);
	set_blocksize(real, bsize);
	gfs_diaper_put(diaper);
	close_bdev_excl(real);

	RET(GFN_KILL_SB);
}

struct file_system_type gfs_fs_type = {
	.name = "gfs",
	.fs_flags = FS_REQUIRES_DEV,
	.get_sb = gfs_get_sb,
	.kill_sb = gfs_kill_sb,
	.owner = THIS_MODULE,
};
