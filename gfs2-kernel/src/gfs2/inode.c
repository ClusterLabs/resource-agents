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
#include <linux/posix_acl.h>

#include "gfs2.h"
#include "acl.h"
#include "bmap.h"
#include "dio.h"
#include "dir.h"
#include "eattr.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "log.h"
#include "ops_address.h"
#include "ops_file.h"
#include "ops_inode.h"
#include "quota.h"
#include "rgrp.h"
#include "trans.h"
#include "unlinked.h"

/**
 * inode_attr_in - Copy attributes from the dinode into the VFS inode
 * @ip: The GFS2 inode (with embedded disk inode data)
 * @inode:  The Linux VFS inode
 *
 */

static void
inode_attr_in(struct gfs2_inode *ip, struct inode *inode)
{
	ENTER(G2FN_INODE_ATTR_IN2)

	inode->i_ino = ip->i_num.no_formal_ino;

	switch (ip->i_di.di_mode & S_IFMT) {
	case S_IFBLK:
	case S_IFCHR:
		inode->i_rdev = MKDEV(ip->i_di.di_major, ip->i_di.di_minor);
		break;
	default:
		inode->i_rdev = 0;
		break;
	};

	inode->i_mode = ip->i_di.di_mode;
	inode->i_nlink = ip->i_di.di_nlink;
	inode->i_uid = ip->i_di.di_uid;
	inode->i_gid = ip->i_di.di_gid;
	i_size_write(inode, ip->i_di.di_size);
	inode->i_atime.tv_sec = ip->i_di.di_atime;
	inode->i_mtime.tv_sec = ip->i_di.di_mtime;
	inode->i_ctime.tv_sec = ip->i_di.di_ctime;
	inode->i_atime.tv_nsec = inode->i_mtime.tv_nsec = inode->i_ctime.tv_nsec = 0;
	inode->i_blksize = PAGE_SIZE;
	inode->i_blocks = ip->i_di.di_blocks <<
		(ip->i_sbd->sd_sb.sb_bsize_shift - GFS2_BASIC_BLOCK_SHIFT);

	if (ip->i_di.di_flags & GFS2_DIF_IMMUTABLE)
		inode->i_flags |= S_IMMUTABLE;
	else
		inode->i_flags &= ~S_IMMUTABLE;

	if (ip->i_di.di_flags & GFS2_DIF_APPENDONLY)
		inode->i_flags |= S_APPEND;
	else
		inode->i_flags &= ~S_APPEND;

	RET(G2FN_INODE_ATTR_IN2);
}

/**
 * gfs2_inode_attr_in - Copy attributes from the dinode into the VFS inode
 * @ip: The GFS2 inode (with embedded disk inode data)
 *
 */

void
gfs2_inode_attr_in(struct gfs2_inode *ip)
{
	ENTER(G2FN_INODE_ATTR_IN)
	struct inode *inode;

	inode = gfs2_ip2v(ip, NO_CREATE);
	if (inode) {
		inode_attr_in(ip, inode);
		iput(inode);
	}

	RET(G2FN_INODE_ATTR_IN);
}

/**
 * gfs2_inode_attr_out - Copy attributes from VFS inode into the dinode
 * @ip: The GFS2 inode
 *
 * Only copy out the attributes that we want the VFS layer
 * to be able to modify.
 */

void
gfs2_inode_attr_out(struct gfs2_inode *ip)
{
	ENTER(G2FN_INODE_ATTR_OUT)
	struct inode *inode = ip->i_vnode;

	gfs2_assert_withdraw(ip->i_sbd,
			    (ip->i_di.di_mode & S_IFMT) == (inode->i_mode & S_IFMT));
	ip->i_di.di_mode = inode->i_mode;
	ip->i_di.di_uid = inode->i_uid;
	ip->i_di.di_gid = inode->i_gid;
	ip->i_di.di_atime = inode->i_atime.tv_sec;
	ip->i_di.di_mtime = inode->i_mtime.tv_sec;
	ip->i_di.di_ctime = inode->i_ctime.tv_sec;

	RET(G2FN_INODE_ATTR_OUT);
}

/**
 * gfs2_ip2v - Get/Create a struct inode for a struct gfs2_inode
 * @ip: the struct gfs2_inode to get the struct inode for
 * @create: CREATE -- create a new struct inode if one does not already exist
 *          NO_CREATE -- return NULL if inode doesn't exist
 *
 * Returns: A VFS inode, or NULL if NO_CREATE and none in existance
 *
 * If this function creates a new inode, it:
 *   Copies fields from the GFS2 on-disk (d)inode to the VFS inode
 *   Attaches the appropriate ops vectors to the VFS inode and address_space
 *   Attaches the VFS inode to the gfs2_inode
 *   Inserts the new inode in the VFS inode hash, while avoiding races
 */

struct inode *
gfs2_ip2v(struct gfs2_inode *ip, int create)
{
	ENTER(G2FN_IP2V)
	struct inode *inode = NULL, *tmp;

	spin_lock(&ip->i_lock);
	if (ip->i_vnode)
		inode = igrab(ip->i_vnode);
	spin_unlock(&ip->i_lock);

	if (inode || !create)
		RETURN(G2FN_IP2V, inode);

	tmp = new_inode(ip->i_sbd->sd_vfs);
	if (!tmp)
		RETURN(G2FN_IP2V, NULL);

	inode_attr_in(ip, tmp);

	/* Attach GFS2-specific ops vectors */
	if (S_ISREG(ip->i_di.di_mode)) {
		tmp->i_op = &gfs2_file_iops;
		tmp->i_fop = &gfs2_file_fops;
		tmp->i_mapping->a_ops = &gfs2_file_aops;
	} else if (S_ISDIR(ip->i_di.di_mode)) {
		tmp->i_op = &gfs2_dir_iops;
		tmp->i_fop = &gfs2_dir_fops;
	} else if (S_ISLNK(ip->i_di.di_mode)) {
		tmp->i_op = &gfs2_symlink_iops;
	} else {
		tmp->i_op = &gfs2_dev_iops;
		init_special_inode(tmp, tmp->i_mode, tmp->i_rdev);
	}

	set_v2ip(tmp, NULL);

	/* Did another process successfully create an inode while we were
	   preparing this (tmp) one?  If so, we can use that other one, and
	   trash the one we were preparing. 
	   The other process might not be done inserting the inode in the
	   VFS hash table.  If so, we need to wait until it is done, then
	   we can use it.  */
	for (;;) {
		spin_lock(&ip->i_lock);
		if (!ip->i_vnode)
			break;
		inode = igrab(ip->i_vnode);
		spin_unlock(&ip->i_lock);

		if (inode) {
			iput(tmp);
			RETURN(G2FN_IP2V, inode);
		}
		yield();
	}

	inode = tmp;

	gfs2_inode_hold(ip);
	ip->i_vnode = inode;
	set_v2ip(inode, ip);

	spin_unlock(&ip->i_lock);

	insert_inode_hash(inode);

	RETURN(G2FN_IP2V, inode);
}

static int
iget_test(struct inode *inode, void *opaque)
{
	ENTER(G2FN_IGET_TEST)
	struct gfs2_inode *ip = get_v2ip(inode);
	struct gfs2_inum *inum = (struct gfs2_inum *)opaque;

	if (ip && ip->i_num.no_addr == inum->no_addr)
		RETURN(G2FN_IGET_TEST, 1);

	RETURN(G2FN_IGET_TEST, 0);
}

struct inode *
gfs2_iget(struct super_block *sb, struct gfs2_inum *inum)
{
	ENTER(G2FN_IGET)
	RETURN(G2FN_IGET,
	       ilookup5(sb, (unsigned long)inum->no_formal_ino,
			iget_test, inum));
}

/**
 * gfs2_copyin_dinode - Refresh the incore copy of the dinode
 * @ip: The GFS2 inode
 *
 * Returns: errno
 */

int
gfs2_copyin_dinode(struct gfs2_inode *ip)
{
	ENTER(G2FN_COPYIN_DINODE)
	struct buffer_head *dibh;
	int error;

	error = gfs2_get_inode_buffer(ip, &dibh);
	if (error)
		RETURN(G2FN_COPYIN_DINODE, error);

	if (gfs2_metatype_check(ip->i_sbd, dibh, GFS2_METATYPE_DI)) {
		brelse(dibh);
		RETURN(G2FN_COPYIN_DINODE, -EIO);
	}

	gfs2_dinode_in(&ip->i_di, dibh->b_data);
	brelse(dibh);

	if (ip->i_num.no_addr != ip->i_di.di_num.no_addr) {
		if (gfs2_consist_inode(ip))
			gfs2_dinode_print(&ip->i_di);
		RETURN(G2FN_COPYIN_DINODE, -EIO);
	}
	if (ip->i_num.no_formal_ino != ip->i_di.di_num.no_formal_ino)
		RETURN(G2FN_COPYIN_DINODE, -ESTALE);

	ip->i_vn = ip->i_gl->gl_vn;

	RETURN(G2FN_COPYIN_DINODE, 0);
}

/**
 * inode_create - create a struct gfs2_inode, acquire Inode-Open (iopen) glock,
 *      read dinode from disk
 * @i_gl: The (already held) glock covering the inode
 * @inum: The inode number
 * @io_gl: the iopen glock to acquire/hold (using holder in new gfs2_inode)
 * @io_state: the state the iopen glock should be acquired in
 * @ipp: pointer to put the returned inode in
 *
 * Returns: errno
 */

static int
inode_create(struct gfs2_glock *i_gl, struct gfs2_inum *inum,
	     struct gfs2_glock *io_gl, unsigned int io_state,
	     struct gfs2_inode **ipp)
{
	ENTER(G2FN_INODE_CREATE)
	struct gfs2_sbd *sdp = i_gl->gl_sbd;
	struct gfs2_inode *ip;
	int error = 0;

	RETRY_MALLOC(ip = kmem_cache_alloc(gfs2_inode_cachep, GFP_KERNEL), ip);
	gfs2_memory_add(ip);
	memset(ip, 0, sizeof(struct gfs2_inode));

	ip->i_num = *inum;

	atomic_set(&ip->i_count, 1);

	ip->i_gl = i_gl;
	ip->i_sbd = sdp;

	spin_lock_init(&ip->i_lock);

	ip->i_greedy = gfs2_tune_get(sdp, gt_greedy_default);

	/* Lock the iopen glock (may be recursive) */
	error = gfs2_glock_nq_init(io_gl,
				  io_state, GL_LOCAL_EXCL | GL_EXACT,
				  &ip->i_iopen_gh);
	if (error)
		goto fail;

	ip->i_iopen_gh.gh_owner = NULL;

	/* Assign the inode's glock as this iopen glock's protected object */
	spin_lock(&io_gl->gl_spin);
	gfs2_glock_hold(i_gl);
	set_gl2gl(io_gl, i_gl);
	spin_unlock(&io_gl->gl_spin);

	/* Read dinode from disk */
	error = gfs2_copyin_dinode(ip);
	if (error)
		goto fail_iopen;

	gfs2_glock_hold(i_gl);
	set_gl2ip(i_gl, ip);

	atomic_inc(&sdp->sd_inode_count);

	*ipp = ip;

	RETURN(G2FN_INODE_CREATE, 0);

 fail_iopen:
	spin_lock(&io_gl->gl_spin);
	set_gl2gl(io_gl, NULL);
	gfs2_glock_put(i_gl);
	spin_unlock(&io_gl->gl_spin);

	gfs2_glock_dq_uninit(&ip->i_iopen_gh);

 fail:
	gfs2_flush_meta_cache(ip);
	gfs2_memory_rm(ip);
	kmem_cache_free(gfs2_inode_cachep, ip);
	*ipp = NULL;

	RETURN(G2FN_INODE_CREATE, error);
}

/**
 * gfs2_inode_get - Get an inode given its number
 * @i_gl: The glock covering the inode
 * @inum: The inode number
 * @create: Flag to say if we are allowed to create a new struct gfs2_inode
 * @ipp: pointer to put the returned inode in
 *
 * Returns: errno
 *
 * If creating a new gfs2_inode structure, reads dinode from disk.
 */

int
gfs2_inode_get(struct gfs2_glock *i_gl, struct gfs2_inum *inum, int create,
	       struct gfs2_inode **ipp)
{
	ENTER(G2FN_INODE_GET)
	struct gfs2_glock *io_gl;
	int error = 0;

	*ipp = get_gl2ip(i_gl);
	if (*ipp) {
		if (gfs2_assert_withdraw(i_gl->gl_sbd,
					 (*ipp)->i_num.no_addr == inum->no_addr))
			RETURN(G2FN_INODE_GET, -EIO);
		if ((*ipp)->i_num.no_formal_ino != inum->no_formal_ino)
			RETURN(G2FN_INODE_GET, -ESTALE);		
		atomic_inc(&(*ipp)->i_count);
	} else if (create) {
		error = gfs2_glock_get(i_gl->gl_sbd,
				      inum->no_addr, &gfs2_iopen_glops,
				      CREATE, &io_gl);
		if (!error) {
			error = inode_create(i_gl, inum, io_gl,
					     LM_ST_SHARED, ipp);
			gfs2_glock_put(io_gl);
		}
	}

	RETURN(G2FN_INODE_GET, error);
}

/**
 * gfs2_inode_hold - hold a struct gfs2_inode structure
 * @ip: The GFS2 inode
 *
 */

void
gfs2_inode_hold(struct gfs2_inode *ip)
{
	ENTER(G2FN_INODE_HOLD)
	gfs2_assert(ip->i_sbd, atomic_read(&ip->i_count) > 0,);
	atomic_inc(&ip->i_count);
	RET(G2FN_INODE_HOLD);
}

/**
 * gfs2_inode_put - put a struct gfs2_inode structure
 * @ip: The GFS2 inode
 *
 */

void
gfs2_inode_put(struct gfs2_inode *ip)
{
	ENTER(G2FN_INODE_PUT)
	gfs2_assert(ip->i_sbd, atomic_read(&ip->i_count) > 0,);
	atomic_dec(&ip->i_count);
	RET(G2FN_INODE_PUT);
}

/**
 * gfs2_inode_destroy - Destroy a GFS2 inode structure with no references on it
 * @ip: The GFS2 inode
 *
 * Also, unhold the iopen glock and release indirect addressing buffers.
 * This function must be called with a glocks held on the inode and 
 *   the associated iopen.
 *
 */

void
gfs2_inode_destroy(struct gfs2_inode *ip)
{
	ENTER(G2FN_INODE_DESTROY)
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_glock *io_gl = ip->i_iopen_gh.gh_gl;
	struct gfs2_glock *i_gl = ip->i_gl;

	gfs2_assert_warn(sdp, !atomic_read(&ip->i_count));
	gfs2_assert(sdp, get_gl2gl(io_gl) == i_gl,);

	/* Unhold the iopen glock */
	spin_lock(&io_gl->gl_spin);
	set_gl2gl(io_gl, NULL);
	gfs2_glock_put(i_gl);
	spin_unlock(&io_gl->gl_spin);

	gfs2_glock_dq_uninit(&ip->i_iopen_gh);

	/* Release indirect addressing buffers, destroy the GFS2 inode struct */
	gfs2_flush_meta_cache(ip);
	gfs2_memory_rm(ip);
	kmem_cache_free(gfs2_inode_cachep, ip);

	set_gl2ip(i_gl, NULL);
	gfs2_glock_put(i_gl);

	atomic_dec(&sdp->sd_inode_count);

	RET(G2FN_INODE_DESTROY);
}

/**
 * dinode_dealloc - Put deallocate a dinode
 * @ip: The GFS2 inode
 *
 * Returns: errno
 */

static int
dinode_dealloc(struct gfs2_inode *ip, struct gfs2_unlinked *ul)
{
	ENTER(G2FN_DINODE_DEALLOC)
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_alloc *al;
	struct gfs2_rgrpd *rgd;
	int error;

	if (ip->i_di.di_blocks != 1) {
		if (gfs2_consist_inode(ip))
			gfs2_dinode_print(&ip->i_di);
		RETURN(G2FN_DINODE_DEALLOC, -EIO);
	}

	al = gfs2_alloc_get(ip);

	error = gfs2_quota_hold(ip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
	if (error)
		goto out;

	error = gfs2_rindex_hold(sdp, &al->al_ri_gh);
	if (error)
		goto out_qs;

	rgd = gfs2_blk2rgrpd(sdp, ip->i_num.no_addr);
	if (!rgd) {
		gfs2_consist_inode(ip);
		error = -EIO;
		goto out_rindex_relse;
	}

	error = gfs2_glock_nq_init(rgd->rd_gl, LM_ST_EXCLUSIVE, 0, &al->al_rgd_gh);
	if (error)
		goto out_rindex_relse;

	error = gfs2_trans_begin(sdp, RES_RG_BIT + RES_UNLINKED +
				 RES_STATFS + RES_QUOTA, 1);
	if (error)
		goto out_rg_gunlock;

	gfs2_trans_add_gl(ip->i_gl);

	/* De-allocate on-disk dinode block to FREEMETA */
	gfs2_free_di(rgd, ip);

	error = gfs2_unlinked_ondisk_rm(sdp, ul);

	gfs2_trans_end(sdp);
	clear_bit(GLF_STICKY, &ip->i_gl->gl_flags);

 out_rg_gunlock:
	gfs2_glock_dq_uninit(&al->al_rgd_gh);

 out_rindex_relse:
	gfs2_glock_dq_uninit(&al->al_ri_gh);

 out_qs:
	gfs2_quota_unhold(ip);

 out:
	gfs2_alloc_put(ip);

	RETURN(G2FN_DINODE_DEALLOC, error);
}

/**
 * inode_dealloc - Deallocate all on-disk blocks for an inode (dinode)
 * @sdp: the filesystem
 * @inum: the inode number to deallocate
 * @io_gh: a holder for the iopen glock for this inode
 *
 * De-allocates all on-disk blocks, data and metadata, associated with an inode.
 * All metadata blocks become GFS2_BLKST_FREEMETA.
 * All data blocks become GFS2_BLKST_FREE.
 * Also de-allocates incore gfs2_inode structure.
 *
 * Returns: errno
 */

static int
inode_dealloc(struct gfs2_sbd *sdp, struct gfs2_unlinked *ul,
	      struct gfs2_holder *io_gh)
{
	ENTER(G2FN_INODE_DEALLOC2)
	struct gfs2_inode *ip;
	struct gfs2_holder i_gh;
	int error;

	/* Lock the inode as we blow it away */
	error = gfs2_glock_nq_num(sdp,
				 ul->ul_ut.ut_inum.no_addr, &gfs2_inode_glops,
				 LM_ST_EXCLUSIVE, 0, &i_gh);
	if (error)
		RETURN(G2FN_INODE_DEALLOC2, error);

	/* We reacquire the iopen lock here to avoid a race with the NFS server
	   calling gfs2_read_inode() with the inode number of a inode we're in
	   the process of deallocating.  And we can't keep our hold on the lock
	   from inode_dealloc_init() for deadlock reasons.  We do, however,
	   overlap this iopen lock with the one to be acquired EX within
	   inode_create(), below (recursive EX locks will be granted to same
	   holder process, i.e. this process). */

	gfs2_holder_reinit(LM_ST_EXCLUSIVE, LM_FLAG_TRY, io_gh);
	error = gfs2_glock_nq(io_gh);
	switch (error) {
	case 0:
		break;
	case GLR_TRYFAILED:
		error = 1;
	default:
		goto fail;
	}

	gfs2_assert_warn(sdp, !get_gl2ip(i_gh.gh_gl));
	error = inode_create(i_gh.gh_gl, &ul->ul_ut.ut_inum, io_gh->gh_gl,
			     LM_ST_EXCLUSIVE, &ip);

	gfs2_glock_dq(io_gh);

	if (error)
		goto fail;

	/* Verify disk (d)inode, gfs2 inode, and VFS (v)inode are unused */
	if (ip->i_di.di_nlink) {
		if (gfs2_consist_inode(ip))
			gfs2_dinode_print(&ip->i_di);
		error = -EIO;
		goto fail_iput;
	}
	gfs2_assert_warn(sdp, atomic_read(&ip->i_count) == 1);
	gfs2_assert_warn(sdp, !ip->i_vnode);

	/* Free all on-disk directory leaves (if any) */
	if (S_ISDIR(ip->i_di.di_mode) &&
	    (ip->i_di.di_flags & GFS2_DIF_EXHASH)) {
		error = gfs2_dir_exhash_dealloc(ip);
		if (error)
			goto fail_iput;
	}

	/* Free all on-disk extended attribute blocks */
	if (ip->i_di.di_eattr) {
		error = gfs2_ea_dealloc(ip);
		if (error)
			goto fail_iput;
	}

	/* Free all data and meta blocks */
	if (!gfs2_is_stuffed(ip)) {
		error = gfs2_file_dealloc(ip);
		if (error)
			goto fail_iput;
	}

	/* De-alloc the dinode block */
	error = dinode_dealloc(ip, ul);
	if (error)
		goto fail_iput;

	/* Free the GFS2 inode structure, unhold iopen and inode glocks */
	gfs2_inode_put(ip);
	gfs2_inode_destroy(ip);

	gfs2_glock_dq_uninit(&i_gh);

	RETURN(G2FN_INODE_DEALLOC2, 0);

 fail_iput:
	gfs2_inode_put(ip);
	gfs2_inode_destroy(ip);

 fail:
	gfs2_glock_dq_uninit(&i_gh);

	RETURN(G2FN_INODE_DEALLOC2, error);
}

/**
 * try_inode_dealloc - Try to deallocate an initialized on-disk inode (dinode)
 *      and all of its associated data and meta blocks
 * @sdp: the filesystem
 *
 * Returns: 0 on success, -errno on error, 1 on busy (inode open)
 */

int
try_inode_dealloc(struct gfs2_sbd *sdp, struct gfs2_unlinked *ul)
{
	ENTER(G2FN_TRY_INODE_DEALLOC)
	struct gfs2_holder io_gh;
	int error = 0;

	/* If not busy (on this node), de-alloc GFS2 incore inode, releasing
	   any indirect addressing buffers, and unholding iopen glock */
	gfs2_try_toss_inode(sdp, &ul->ul_ut.ut_inum);

	/* Does another process (cluster-wide) have this inode open? */
	error = gfs2_glock_nq_num(sdp,
				 ul->ul_ut.ut_inum.no_addr, &gfs2_iopen_glops,
				 LM_ST_EXCLUSIVE, LM_FLAG_TRY_1CB, &io_gh);
	switch (error) {
	case 0:
		break;
	case GLR_TRYFAILED:
		RETURN(G2FN_TRY_INODE_DEALLOC, 1);
	default:
		RETURN(G2FN_TRY_INODE_DEALLOC, error);
	}

	/* Unlock here to prevent deadlock */
	gfs2_glock_dq(&io_gh);

	/* No other process in the entire cluster has this inode open;
	   we can remove it and all of its associated blocks from disk */
	error = inode_dealloc(sdp, ul, &io_gh);

	gfs2_holder_uninit(&io_gh);

	RETURN(G2FN_TRY_INODE_DEALLOC, error);
}

int
inode_dealloc_uninit(struct gfs2_sbd *sdp, struct gfs2_unlinked *ul)
{
	ENTER(G2FN_INODE_DEALLOC_UNINIT)
	struct gfs2_rgrpd *rgd;
	struct gfs2_holder ri_gh, rgd_gh;
	int error;

	error = gfs2_rindex_hold(sdp, &ri_gh);
	if (error)
		RETURN(G2FN_INODE_DEALLOC_UNINIT, error);

	rgd = gfs2_blk2rgrpd(sdp, ul->ul_ut.ut_inum.no_addr);
	if (!rgd) {
		gfs2_consist(sdp);
		error = -EIO;
		goto out;
	}

	error = gfs2_glock_nq_init(rgd->rd_gl, LM_ST_EXCLUSIVE, 0, &rgd_gh);
	if (error)
		goto out;

	error = gfs2_trans_begin(sdp, RES_RG_BIT + RES_UNLINKED +
				 RES_STATFS, 0);
	if (error)
		goto out_gunlock;

	gfs2_free_uninit_di(rgd, ul->ul_ut.ut_inum.no_addr);
	gfs2_unlinked_ondisk_rm(sdp, ul);

	gfs2_trans_end(sdp);

 out_gunlock:
	gfs2_glock_dq_uninit(&rgd_gh);
 out:
	gfs2_glock_dq_uninit(&ri_gh);

	RETURN(G2FN_INODE_DEALLOC_UNINIT, error);
}

int
gfs2_inode_dealloc(struct gfs2_sbd *sdp, struct gfs2_unlinked *ul)
{
	ENTER(G2FN_INODE_DEALLOC)
	if (ul->ul_ut.ut_flags & GFS2_UTF_UNINIT)
		RETURN(G2FN_INODE_DEALLOC,
		       inode_dealloc_uninit(sdp, ul));
	else
		RETURN(G2FN_INODE_DEALLOC,
		       try_inode_dealloc(sdp, ul));
}

/**
 * gfs2_change_nlink - Change nlink count on inode
 * @ip: The GFS2 inode
 * @diff: The change in the nlink count required
 *
 * Returns: errno
 */

int
gfs2_change_nlink(struct gfs2_inode *ip, int diff)
{
	ENTER(G2FN_CHANGE_NLINK)
	struct buffer_head *dibh;
	uint32_t nlink;
	int error;

	nlink = ip->i_di.di_nlink + diff;

	/* Tricky.  If we are reducing the nlink count,
	   but the new value ends up being bigger than the
	   old one, we must have underflowed. */
	if (diff < 0 && nlink > ip->i_di.di_nlink) {
		if (gfs2_consist_inode(ip))
			gfs2_dinode_print(&ip->i_di);
		RETURN(G2FN_CHANGE_NLINK, -EIO);
	}

	error = gfs2_get_inode_buffer(ip, &dibh);
	if (error)
		RETURN(G2FN_CHANGE_NLINK, error);

	ip->i_di.di_nlink = nlink;
	ip->i_di.di_ctime = get_seconds();

	gfs2_trans_add_bh(ip->i_gl, dibh);
	gfs2_dinode_out(&ip->i_di, dibh->b_data);
	brelse(dibh);

	RETURN(G2FN_CHANGE_NLINK, 0);
}

/**
 * gfs2_lookupi - Look up a filename in a directory and return its inode
 * @d_gh: An initialized holder for the directory glock
 * @name: The name of the inode to look for
 * @is_root: If TRUE, ignore the caller's permissions
 * @i_gh: An uninitialized holder for the new inode glock
 *
 * There will always be a vnode (Linux VFS inode) for the d_gh inode unless
 *   @is_root is true.
 *
 * Returns: errno
 */

int
gfs2_lookupi(struct gfs2_holder *ghs, struct qstr *name, int is_root)
{
	ENTER(G2FN_LOOKUPI)
	struct gfs2_inode *dip = get_gl2ip(ghs->gh_gl);
	struct gfs2_sbd *sdp = dip->i_sbd;
	struct gfs2_glock *gl;
	struct gfs2_inode *ip;
	struct gfs2_inum inum, inum2;
	unsigned int type;
	int error;

	ghs[1].gh_gl = NULL;

	if (!name->len || name->len > GFS2_FNAMESIZE)
		RETURN(G2FN_LOOKUPI, -ENAMETOOLONG);

	if (gfs2_filecmp(name, ".", 1) ||
	    (gfs2_filecmp(name, "..", 2) && dip == sdp->sd_root_inode)) {
		gfs2_holder_reinit(LM_ST_SHARED, 0, ghs);
		error = gfs2_glock_nq(ghs);
		if (!error) {
			error = gfs2_glock_nq_init(dip->i_gl,
						  LM_ST_SHARED, 0,
						  ghs + 1);
			if (error) {
				gfs2_glock_dq(ghs);
				RETURN(G2FN_LOOKUPI, error);
			}
			gfs2_inode_hold(dip);
		}
		RETURN(G2FN_LOOKUPI, error);
	}

	if (gfs2_assert_warn(sdp, !gfs2_glock_is_locked_by_me(ghs->gh_gl)))
		RETURN(G2FN_LOOKUPI, -EINVAL);

	gfs2_holder_reinit(LM_ST_SHARED, 0, ghs);
	error = gfs2_glock_nq(ghs);
	if (error)
		RETURN(G2FN_LOOKUPI, error);

	if (!is_root) {
		error = gfs2_repermission(dip->i_vnode, MAY_EXEC, NULL);
		if (error) {
			gfs2_glock_dq(ghs);
			RETURN(G2FN_LOOKUPI, error);
		}
	}

	error = gfs2_dir_search(dip, name, &inum, &type);
	if (error) {
		gfs2_glock_dq(ghs);
		if (error == -ENOENT)
			error = 0;
		RETURN(G2FN_LOOKUPI, error);
	}

 restart:
	error = gfs2_glock_get(sdp, inum.no_addr, &gfs2_inode_glops,
			      CREATE, &gl);
	if (error) {
		gfs2_glock_dq(ghs);
		RETURN(G2FN_LOOKUPI, error);
	}

	/*  Acquire the second lock  */

	if (gl->gl_name.ln_number < dip->i_gl->gl_name.ln_number) {
		gfs2_glock_dq(ghs);

		error = gfs2_glock_nq_init(gl, LM_ST_SHARED,
					  LM_FLAG_ANY | GL_LOCAL_EXCL,
					  ghs + 1);
		if (error)
			goto out;

		gfs2_holder_reinit(LM_ST_SHARED, 0, ghs);
		error = gfs2_glock_nq(ghs);
		if (error) {
			gfs2_glock_dq_uninit(ghs + 1);
			goto out;
		}

		if (!is_root) {
			error = gfs2_repermission(dip->i_vnode, MAY_EXEC, NULL);
			if (error) {
				gfs2_glock_dq(ghs);
				gfs2_glock_dq_uninit(ghs + 1);
				goto out;
			}
		}

		error = gfs2_dir_search(dip, name, &inum2, &type);
		if (error) {
			gfs2_glock_dq(ghs);
			gfs2_glock_dq_uninit(ghs + 1);
			if (error == -ENOENT)
				error = 0;
			goto out;
		}

		if (!gfs2_inum_equal(&inum, &inum2)) {
			gfs2_glock_dq_uninit(ghs + 1);
			gfs2_glock_put(gl);
			inum = inum2;
			goto restart;
		}
	} else {
		error = gfs2_glock_nq_init(gl, LM_ST_SHARED,
					  LM_FLAG_ANY | GL_LOCAL_EXCL,
					  ghs + 1);
		if (error) {
			gfs2_glock_dq(ghs);
			goto out;
		}
	}

	error = gfs2_inode_get(gl, &inum, CREATE, &ip);
	if (error) {
		gfs2_glock_dq(ghs);
		gfs2_glock_dq_uninit(ghs + 1);
	} else if (IF2DT(ip->i_di.di_mode) != type) {
		gfs2_consist_inode(dip);
		gfs2_inode_put(ip);
		gfs2_glock_dq(ghs);
		gfs2_glock_dq_uninit(ghs + 1);
		error = -EIO;
	}

 out:
	gfs2_glock_put(gl);

	RETURN(G2FN_LOOKUPI, error);
}

/**
 * gfs2_lookup_simple - Read in the special (hidden) resource group index inode
 * @sdp: The GFS2 superblock
 * @filename:
 * @ipp:
 *
 * Returns: errno
 */

int
gfs2_lookup_simple(struct gfs2_inode *dip, char *filename,
		  struct gfs2_inode **ipp)
{
	ENTER(G2FN_LOOKUP_SIMPLE)
	struct qstr name;
	struct gfs2_holder ghs[2];
	int error;

	memset(&name, 0, sizeof(struct qstr));
	name.name = filename;
	name.len = strlen(filename);

	gfs2_holder_init(dip->i_gl, 0, 0, ghs);

	error = gfs2_lookupi(ghs, &name, TRUE);
	if (error) {
		gfs2_holder_uninit(ghs);
		RETURN(G2FN_LOOKUP_SIMPLE, error);
	}
	if (!ghs[1].gh_gl) {
		gfs2_holder_uninit(ghs);
		RETURN(G2FN_LOOKUP_SIMPLE, -ENOENT);
	}

	*ipp = get_gl2ip(ghs[1].gh_gl);

	gfs2_glock_dq_m(2, ghs);

	gfs2_holder_uninit(ghs);
	gfs2_holder_uninit(ghs + 1);

	RETURN(G2FN_LOOKUP_SIMPLE, error);
}

static int
pick_formal_ino_1(struct gfs2_sbd *sdp, uint64_t *formal_ino)
{
	ENTER(G2FN_PICK_FORMAL_INO_1)
       	struct gfs2_inode *ip = sdp->sd_ir_inode;
	struct buffer_head *bh;
	struct gfs2_inum_range ir;
	int error;

	error = gfs2_trans_begin(sdp, RES_DINODE, 0);
	if (error)
		RETURN(G2FN_PICK_FORMAL_INO_1, error);
	down(&sdp->sd_inum_mutex);

	error = gfs2_get_inode_buffer(ip, &bh);
	if (error) {
		up(&sdp->sd_inum_mutex);
		gfs2_trans_end(sdp);
		RETURN(G2FN_PICK_FORMAL_INO_1, error);
	}

	gfs2_inum_range_in(&ir, bh->b_data + sizeof(struct gfs2_dinode));

	if (ir.ir_length) {
		*formal_ino = ir.ir_start++;
		ir.ir_length--;
		gfs2_trans_add_bh(ip->i_gl, bh);
		gfs2_inum_range_out(&ir, bh->b_data + sizeof(struct gfs2_dinode));
		brelse(bh);
		up(&sdp->sd_inum_mutex);
		gfs2_trans_end(sdp);
		RETURN(G2FN_PICK_FORMAL_INO_1, 0);
	}

	brelse(bh);

	up(&sdp->sd_inum_mutex);
	gfs2_trans_end(sdp);

	RETURN(G2FN_PICK_FORMAL_INO_1, 1);
}

static int
pick_formal_ino_2(struct gfs2_sbd *sdp, uint64_t *formal_ino)
{
	ENTER(G2FN_PICK_FORMAL_INO_2)
       	struct gfs2_inode *ip = sdp->sd_ir_inode;
	struct gfs2_inode *m_ip = sdp->sd_inum_inode;
	struct gfs2_holder gh;
	struct buffer_head *bh;
	struct gfs2_inum_range ir;
	int error;

	error = gfs2_glock_nq_init(m_ip->i_gl, LM_ST_EXCLUSIVE, 0, &gh);
	if (error)
		RETURN(G2FN_PICK_FORMAL_INO_2, error);

	error = gfs2_trans_begin(sdp, 2 * RES_DINODE, 0);
	if (error)
		goto out;
	down(&sdp->sd_inum_mutex);

	error = gfs2_get_inode_buffer(ip, &bh);
	if (error)
		goto out_end_trans;
	
	gfs2_inum_range_in(&ir, bh->b_data + sizeof(struct gfs2_dinode));

	if (!ir.ir_length) {
		struct buffer_head *m_bh;
		uint64_t x, y;

		error = gfs2_get_inode_buffer(m_ip, &m_bh);
		if (error)
			goto out_brelse;

		x = *(uint64_t *)(m_bh->b_data + sizeof(struct gfs2_dinode));
		x = y = gfs2_64_to_cpu(x);
		ir.ir_start = x;
		ir.ir_length = GFS2_INUM_QUANTUM;
		x += GFS2_INUM_QUANTUM;
		if (x < y)
			gfs2_consist_inode(m_ip);
		x = cpu_to_gfs2_64(x);
		gfs2_trans_add_bh(m_ip->i_gl, m_bh);
		*(uint64_t *)(m_bh->b_data + sizeof(struct gfs2_dinode)) = x;

		brelse(m_bh);
	}

	*formal_ino = ir.ir_start++;
	ir.ir_length--;

	gfs2_trans_add_bh(ip->i_gl, bh);
	gfs2_inum_range_out(&ir, bh->b_data + sizeof(struct gfs2_dinode));

 out_brelse:
	brelse(bh);

 out_end_trans:
	up(&sdp->sd_inum_mutex);
	gfs2_trans_end(sdp);

 out:
	gfs2_glock_dq_uninit(&gh);

	RETURN(G2FN_PICK_FORMAL_INO_2, error);
}

static int
pick_formal_ino(struct gfs2_sbd *sdp, uint64_t *inum)
{
	ENTER(G2FN_PICK_FORMAL_INO)
       	int error;

	error = pick_formal_ino_1(sdp, inum);
	if (error <= 0)
		RETURN(G2FN_PICK_FORMAL_INO, error);

	error = pick_formal_ino_2(sdp, inum);

	RETURN(G2FN_PICK_FORMAL_INO, error);
}

/**
 * create_ok - OK to create a new on-disk inode here?
 * @dip:  Directory in which dinode is to be created
 * @name:  Name of new dinode
 * @mode:
 *
 * Returns: errno
 */

static int
create_ok(struct gfs2_inode *dip, struct qstr *name, unsigned int mode)
{
	ENTER(G2FN_CREATE_OK)
	int error;

	error = gfs2_repermission(dip->i_vnode, MAY_WRITE | MAY_EXEC, NULL);
	if (error)
		RETURN(G2FN_CREATE_OK, error);

	/*  Don't create entries in an unlinked directory  */
	if (!dip->i_di.di_nlink)
		RETURN(G2FN_CREATE_OK, -EPERM);

	error = gfs2_dir_search(dip, name, NULL, NULL);
	switch (error) {
	case -ENOENT:
		error = 0;
		break;
	case 0:
		RETURN(G2FN_CREATE_OK, -EEXIST);
	default:
		RETURN(G2FN_CREATE_OK, error);
	}

	if (dip->i_di.di_entries == (uint32_t)-1)
		RETURN(G2FN_CREATE_OK, -EFBIG);
	if (S_ISDIR(mode) && dip->i_di.di_nlink == (uint32_t)-1)
		RETURN(G2FN_CREATE_OK, -EMLINK);

	RETURN(G2FN_CREATE_OK, 0);
}

static void
munge_mode_uid_gid(struct gfs2_inode *dip,
		   unsigned int *mode,
		   unsigned int *uid, unsigned int *gid)
{
	ENTER(G2FN_MUNGE_MODE_UID_GID)

	if (dip->i_sbd->sd_args.ar_suiddir &&
	    (dip->i_di.di_mode & S_ISUID) &&
	    dip->i_di.di_uid) {
		if (S_ISDIR(*mode))
			*mode |= S_ISUID;
		else if (dip->i_di.di_uid != current->fsuid)
			*mode &= ~07111;
		*uid = dip->i_di.di_uid;
	} else
		*uid = current->fsuid;

	if (dip->i_di.di_mode & S_ISGID) {
		if (S_ISDIR(*mode))
			*mode |= S_ISGID;
		*gid = dip->i_di.di_gid;
	} else
		*gid = current->fsgid;

	RET(G2FN_MUNGE_MODE_UID_GID);
}

static int
alloc_dinode(struct gfs2_inode *dip, struct gfs2_unlinked *ul)
{
	ENTER(G2FN_ALLOC_DINODE)
	struct gfs2_sbd *sdp = dip->i_sbd;
	int error;

	gfs2_alloc_get(dip);

	dip->i_alloc->al_requested = RES_DINODE;
	error = gfs2_inplace_reserve(dip);
	if (error)
		goto out;

	error = gfs2_trans_begin(sdp, RES_RG_BIT + RES_UNLINKED +
				 RES_STATFS, 0);
	if (error)
		goto out_ipreserv;

	ul->ul_ut.ut_inum.no_addr = gfs2_alloc_di(dip);

	ul->ul_ut.ut_flags = GFS2_UTF_UNINIT;
	error = gfs2_unlinked_ondisk_add(sdp, ul);

	gfs2_trans_end(sdp);

 out_ipreserv:
	gfs2_inplace_release(dip);

 out:
	gfs2_alloc_put(dip);

	RETURN(G2FN_ALLOC_DINODE, error);
}

/**
 * init_dinode - Fill in a new dinode structure
 * @dip: the directory this inode is being created in
 * @gl: The glock covering the new inode
 * @inum: the inode number
 * @mode: the file permissions
 * @uid:
 * @gid:
 *
 */

static void
init_dinode(struct gfs2_inode *dip,
	    struct gfs2_glock *gl, struct gfs2_inum *inum,
	    unsigned int mode,
	    unsigned int uid, unsigned int gid)
{
	ENTER(G2FN_INIT_DINODE)
	struct gfs2_sbd *sdp = dip->i_sbd;
	struct gfs2_dinode di;
	struct buffer_head *dibh;

	dibh = gfs2_dgetblk(gl, inum->no_addr);
	gfs2_prep_new_buffer(dibh);
	gfs2_trans_add_bh(gl, dibh);
	gfs2_metatype_set(dibh, GFS2_METATYPE_DI, GFS2_FORMAT_DI);
	gfs2_buffer_clear_tail(dibh, sizeof(struct gfs2_dinode));

	memset(&di, 0, sizeof(struct gfs2_dinode));
	gfs2_meta_header_in(&di.di_header, dibh->b_data);
	di.di_num = *inum;
	di.di_mode = mode;
	di.di_uid = uid;
	di.di_gid = gid;
	di.di_blocks = 1;
	di.di_atime = di.di_mtime = di.di_ctime = get_seconds();
	di.di_goal_meta = di.di_goal_data = inum->no_addr;

	if (S_ISREG(mode)) {
		if ((dip->i_di.di_flags & GFS2_DIF_INHERIT_JDATA) ||
		    gfs2_tune_get(sdp, gt_new_files_jdata))
			di.di_flags |= GFS2_DIF_JDATA;
		if ((dip->i_di.di_flags & GFS2_DIF_INHERIT_DIRECTIO) ||
		    gfs2_tune_get(sdp, gt_new_files_directio))
			di.di_flags |= GFS2_DIF_DIRECTIO;
	} else if (S_ISDIR(mode)) {
		di.di_flags |= (dip->i_di.di_flags & GFS2_DIF_INHERIT_DIRECTIO);
		di.di_flags |= (dip->i_di.di_flags & GFS2_DIF_INHERIT_JDATA);
	}

	gfs2_dinode_out(&di, dibh->b_data);
	brelse(dibh);

	RET(G2FN_INIT_DINODE);
}

static int
make_dinode(struct gfs2_inode *dip, struct gfs2_glock *gl,
	    unsigned int mode, struct gfs2_unlinked *ul)
{
	ENTER(G2FN_MAKE_DINODE)
       	struct gfs2_sbd *sdp = dip->i_sbd;
	unsigned int uid, gid;
	int error;

	munge_mode_uid_gid(dip, &mode, &uid, &gid);

	gfs2_alloc_get(dip);

	error = gfs2_quota_lock(dip, uid, gid);
	if (error)
		goto out;

	error = gfs2_quota_check(dip, uid, gid);
	if (error)
		goto out_quota;

	error = gfs2_trans_begin(sdp, RES_DINODE + RES_UNLINKED +
				 RES_QUOTA, 0);
	if (error)
		goto out_quota;

	ul->ul_ut.ut_flags = 0;
	error = gfs2_unlinked_ondisk_munge(sdp, ul);

	init_dinode(dip, gl, &ul->ul_ut.ut_inum,
		     mode, uid, gid);

	gfs2_quota_change(dip, +1, uid, gid);

	gfs2_trans_end(sdp);

 out_quota:
	gfs2_quota_unlock(dip);

 out:
	gfs2_alloc_put(dip);

	RETURN(G2FN_MAKE_DINODE, error);
}

static int
link_dinode(struct gfs2_inode *dip, struct qstr *name, struct gfs2_inode *ip,
	    struct gfs2_unlinked *ul)
{
	ENTER(G2FN_LINK_DINODE)
       	struct gfs2_sbd *sdp = dip->i_sbd;
	struct gfs2_alloc *al;
	int alloc_required;
	struct buffer_head *dibh;
	int error;

	al = gfs2_alloc_get(dip);

	error = gfs2_quota_lock(dip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
	if (error)
		goto fail;

	error = gfs2_diradd_alloc_required(dip, name, &alloc_required);
	if (alloc_required) {
		error = gfs2_quota_check(dip, dip->i_di.di_uid, dip->i_di.di_gid);
		if (error)
			goto fail_quota_locks;

		al->al_requested = sdp->sd_max_dirres;

		error = gfs2_inplace_reserve(dip);
		if (error)
			goto fail_quota_locks;

		error = gfs2_trans_begin(sdp,
					 sdp->sd_max_dirres +
					 al->al_rgd->rd_ri.ri_length +
					 2 * RES_DINODE + RES_UNLINKED +
					 RES_STATFS + RES_QUOTA, 0);
		if (error)
			goto fail_ipreserv;
	} else {
		error = gfs2_trans_begin(sdp, RES_LEAF + 2 * RES_DINODE + RES_UNLINKED, 0);
		if (error)
			goto fail_quota_locks;
	}

	error = gfs2_dir_add(dip, name, &ip->i_num, IF2DT(ip->i_di.di_mode));
	if (error)
		goto fail_end_trans;

	error = gfs2_get_inode_buffer(ip, &dibh);
	if (error)
		goto fail_end_trans;
	ip->i_di.di_nlink = 1;
	gfs2_trans_add_bh(ip->i_gl, dibh);
	gfs2_dinode_out(&ip->i_di, dibh->b_data);
	brelse(dibh);

	error = gfs2_unlinked_ondisk_rm(sdp, ul);
	if (error)
		goto fail_end_trans;

	RETURN(G2FN_LINK_DINODE, 0);

 fail_end_trans:
	gfs2_trans_end(sdp);

 fail_ipreserv:
	if (dip->i_alloc->al_rgd)
		gfs2_inplace_release(dip);

 fail_quota_locks:
	gfs2_quota_unlock(dip);

 fail:
	gfs2_alloc_put(dip);

	RETURN(G2FN_LINK_DINODE, error);
}

/**
 * gfs2_createi - Create a new inode
 * @ghs: An array of two holders
 * @name: The name of the new file
 * @mode: the permissions on the new inode
 *
 * @ghs[0] is an initialized holder for the directory
 * @ghs[1] is the holder for the inode lock
 *
 * If the return value is 0, the glocks on both the directory and the new
 * file are held.  A transaction has been started and an inplace reservation
 * is held, as well.
 *
 * Returns: errno
 */

int
gfs2_createi(struct gfs2_holder *ghs, struct qstr *name, unsigned int mode)
{
	ENTER(G2FN_CREATEI)
	struct gfs2_inode *dip = get_gl2ip(ghs->gh_gl);
	struct gfs2_sbd *sdp = dip->i_sbd;
	struct gfs2_unlinked *ul;
	struct gfs2_inode *ip;
	int error;

	if (!name->len || name->len > GFS2_FNAMESIZE)
		RETURN(G2FN_CREATEI, -ENAMETOOLONG);

	error = gfs2_unlinked_get(sdp, &ul);
	if (error)
		RETURN(G2FN_CREATEI, error);

	gfs2_holder_reinit(LM_ST_EXCLUSIVE, 0, ghs);
	error = gfs2_glock_nq(ghs);
	if (error)
		goto fail;

	error = create_ok(dip, name, mode);
	if (error)
		goto fail_gunlock;

	error = pick_formal_ino(sdp, &ul->ul_ut.ut_inum.no_formal_ino);
	if (error)
		goto fail_gunlock;

	error = alloc_dinode(dip, ul);
	if (error)
		goto fail_gunlock;

	if (ul->ul_ut.ut_inum.no_addr < dip->i_num.no_addr) {
		gfs2_glock_dq(ghs);

		error = gfs2_glock_nq_num(sdp,
					 ul->ul_ut.ut_inum.no_addr, &gfs2_inode_glops,
					 LM_ST_EXCLUSIVE, GL_SKIP,
					 ghs + 1);
		if (error) {
			gfs2_unlinked_put(sdp, ul);
			RETURN(G2FN_CREATEI, error);
		}

		gfs2_holder_reinit(LM_ST_EXCLUSIVE, 0, ghs);
		error = gfs2_glock_nq(ghs);
		if (error) {
			gfs2_glock_dq_uninit(ghs + 1);
			gfs2_unlinked_put(sdp, ul);
			RETURN(G2FN_CREATEI, error);
		}

		error = create_ok(dip, name, mode);
		if (error)
			goto fail_gunlock2;
	} else {
		error = gfs2_glock_nq_num(sdp,
					 ul->ul_ut.ut_inum.no_addr, &gfs2_inode_glops,
					 LM_ST_EXCLUSIVE, GL_SKIP,
					 ghs + 1);
		if (error)
			goto fail_gunlock;
	}

	error = make_dinode(dip, ghs[1].gh_gl, mode, ul);
	if (error)
		goto fail_gunlock2;

	error = gfs2_inode_get(ghs[1].gh_gl, &ul->ul_ut.ut_inum, CREATE, &ip);
	if (error)
		goto fail_gunlock2;

	error = gfs2_acl_create(dip, ip);
	if (error)
		goto fail_iput;

	error = link_dinode(dip, name, ip, ul);
	if (error)
		goto fail_iput;

	gfs2_unlinked_put(sdp, ul);

	RETURN(G2FN_CREATEI, 0);

 fail_iput:
	gfs2_inode_put(ip);

 fail_gunlock2:
	gfs2_glock_dq_uninit(ghs + 1);

 fail_gunlock:
	gfs2_glock_dq(ghs);

 fail:
	gfs2_unlinked_put(sdp, ul);

	RETURN(G2FN_CREATEI, error);
}

/**
 * gfs2_unlinki - Unlink a file
 * @dip: The inode of the directory
 * @name: The name of the file to be unlinked
 * @ip: The inode of the file to be removed
 *
 * Assumes Glocks on both dip and ip are held.
 *
 * Returns: errno
 */

int
gfs2_unlinki(struct gfs2_inode *dip, struct qstr *name, struct gfs2_inode *ip,
	    struct gfs2_unlinked *ul)
{
	ENTER(G2FN_UNLINKI)
	struct gfs2_sbd *sdp = dip->i_sbd;
	int error;

	error = gfs2_dir_del(dip, name);
	if (error)
		RETURN(G2FN_UNLINKI, error);

	error = gfs2_change_nlink(ip, -1);
	if (error)
		RETURN(G2FN_UNLINKI, error);

	/* If this inode is being unlinked from the directory structure,
	   we need to mark that in the log so that it isn't lost during
	   a crash. */

	if (!ip->i_di.di_nlink) {
		ul->ul_ut.ut_inum = ip->i_num;
		error = gfs2_unlinked_ondisk_add(sdp, ul);
		if (!error)
			set_bit(GLF_STICKY, &ip->i_gl->gl_flags);
	}

	RETURN(G2FN_UNLINKI, error);
}

/**
 * gfs2_rmdiri - Remove a directory
 * @dip: The parent directory of the directory to be removed
 * @name: The name of the directory to be removed
 * @ip: The GFS2 inode of the directory to be removed
 *
 * Assumes Glocks on dip and ip are held
 *
 * Returns: errno
 */

int
gfs2_rmdiri(struct gfs2_inode *dip, struct qstr *name, struct gfs2_inode *ip,
	   struct gfs2_unlinked *ul)
{
	ENTER(G2FN_RMDIRI)
	struct gfs2_sbd *sdp = dip->i_sbd;
	struct qstr dotname;
	int error;

	if (ip->i_di.di_entries != 2) {
		if (gfs2_consist_inode(ip))
			gfs2_dinode_print(&ip->i_di);
		RETURN(G2FN_RMDIRI, -EIO);
	}

	error = gfs2_dir_del(dip, name);
	if (error)
		RETURN(G2FN_RMDIRI, error);

	error = gfs2_change_nlink(dip, -1);
	if (error)
		RETURN(G2FN_RMDIRI, error);

	dotname.len = 1;
	dotname.name = ".";
	error = gfs2_dir_del(ip, &dotname);
	if (error)
		RETURN(G2FN_RMDIRI, error);

	dotname.len = 2;
	dotname.name = "..";
	error = gfs2_dir_del(ip, &dotname);
	if (error)
		RETURN(G2FN_RMDIRI, error);

	error = gfs2_change_nlink(ip, -2);
	if (error)
		RETURN(G2FN_RMDIRI, error);

	/* This inode is being unlinked from the directory structure and
	   we need to mark that in the log so that it isn't lost during
	   a crash. */

	ul->ul_ut.ut_inum = ip->i_num;
	error = gfs2_unlinked_ondisk_add(sdp, ul);
	if (!error)
		set_bit(GLF_STICKY, &ip->i_gl->gl_flags);

	RETURN(G2FN_RMDIRI, error);
}

/*
 * gfs2_unlink_ok - check to see that a inode is still in a directory
 * @dip: the directory
 * @name: the name of the file
 * @ip: the inode
 *
 * Assumes that the lock on (at least) @dip is held.
 *
 * Returns: 0 if the parent/child relationship is correct, errno if it isn't
 */

int
gfs2_unlink_ok(struct gfs2_inode *dip, struct qstr *name, struct gfs2_inode *ip)
{
	ENTER(G2FN_UNLINK_OK)
	struct gfs2_inum inum;
	unsigned int type;
	int error;

	if (IS_IMMUTABLE(ip->i_vnode) || IS_APPEND(ip->i_vnode))
		RETURN(G2FN_UNLINK_OK, -EPERM);

	if ((dip->i_di.di_mode & S_ISVTX) &&
	    dip->i_di.di_uid != current->fsuid &&
	    ip->i_di.di_uid != current->fsuid &&
	    !capable(CAP_FOWNER))
		RETURN(G2FN_UNLINK_OK, -EPERM);

	if (IS_APPEND(dip->i_vnode))
		RETURN(G2FN_UNLINK_OK, -EPERM);

	error = gfs2_repermission(dip->i_vnode, MAY_WRITE | MAY_EXEC, NULL);
	if (error)
		RETURN(G2FN_UNLINK_OK, error);

	error = gfs2_dir_search(dip, name, &inum, &type);
	if (error)
		RETURN(G2FN_UNLINK_OK, error);

	if (!gfs2_inum_equal(&inum, &ip->i_num))
		RETURN(G2FN_UNLINK_OK, -ENOENT);

	if (IF2DT(ip->i_di.di_mode) != type) {
		gfs2_consist_inode(dip);
		RETURN(G2FN_UNLINK_OK, -EIO);
	}

	RETURN(G2FN_UNLINK_OK, 0);
}

/*
 * gfs2_ok_to_move - check if it's ok to move a directory to another directory
 * @this: move this
 * @to: to here
 *
 * Follow @to back to the root and make sure we don't encounter @this
 * Assumes we already hold the rename lock.
 *
 * Returns: errno
 */

int
gfs2_ok_to_move(struct gfs2_inode *this, struct gfs2_inode *to)
{
	ENTER(G2FN_OK_TO_MOVE)
	struct gfs2_sbd *sdp = this->i_sbd;
	struct gfs2_inode *tmp;
	struct gfs2_holder ghs[2];
	struct qstr dotdot;
	int error = 0;

	memset(&dotdot, 0, sizeof(struct qstr));
	dotdot.name = "..";
	dotdot.len = 2;

	gfs2_inode_hold(to);

	for (;;) {
		if (to == this) {
			error = -EINVAL;
			break;
		}
		if (to == sdp->sd_root_inode) {
			error = 0;
			break;
		}

		gfs2_holder_init(to->i_gl, 0, 0, ghs);

		error = gfs2_lookupi(ghs, &dotdot, TRUE);
		if (error) {
			gfs2_holder_uninit(ghs);
			break;
		}
		if (!ghs[1].gh_gl) {
			gfs2_holder_uninit(ghs);
			error = -ENOENT;
			break;
		}

		tmp = get_gl2ip(ghs[1].gh_gl);

		gfs2_glock_dq_m(2, ghs);

		gfs2_holder_uninit(ghs);
		gfs2_holder_uninit(ghs + 1);

		gfs2_inode_put(to);
		to = tmp;
	}

	gfs2_inode_put(to);

	RETURN(G2FN_OK_TO_MOVE, error);
}

/**
 * gfs2_readlinki - return the contents of a symlink
 * @ip: the symlink's inode
 * @buf: a pointer to the buffer to be filled
 * @len: a pointer to the length of @buf
 *
 * If @buf is too small, a piece of memory is kmalloc()ed and needs
 * to be freed by the caller.
 *
 * Returns: errno
 */

int
gfs2_readlinki(struct gfs2_inode *ip, char **buf, unsigned int *len)
{
	ENTER(G2FN_READLINKI)
	struct gfs2_holder i_gh;
	struct buffer_head *dibh;
	unsigned int x;
	int error;

	gfs2_holder_init(ip->i_gl, LM_ST_SHARED, GL_ATIME, &i_gh);
	error = gfs2_glock_nq_atime(&i_gh);
	if (error) {
		gfs2_holder_uninit(&i_gh);
		RETURN(G2FN_READLINKI, error);
	}

	if (!ip->i_di.di_size) {
		gfs2_consist_inode(ip);
		error = -EIO;
		goto out;
	}

	error = gfs2_get_inode_buffer(ip, &dibh);
	if (error)
		goto out;

	x = ip->i_di.di_size + 1;
	if (x > *len) {
		*buf = kmalloc(x, GFP_KERNEL);
		if (!*buf) {
			error = -ENOMEM;
			goto out_brelse;
		}
	}

	memcpy(*buf, dibh->b_data + sizeof(struct gfs2_dinode), x);
	*len = x;

 out_brelse:
	brelse(dibh);

 out:
	gfs2_glock_dq_uninit(&i_gh);

	RETURN(G2FN_READLINKI, error);
}

/**
 * gfs2_glock_nq_atime - Acquire a hold on an inode's glock, and
 *       conditionally update the inode's atime
 * @gh: the holder to acquire
 *
 * Tests atime (access time) for gfs2_read, gfs2_readdir and gfs2_mmap
 * Update if the difference between the current time and the inode's current
 * atime is greater than an interval specified at mount (or default).
 *
 * Will not update if GFS2 mounted NOATIME (this is *the* place where NOATIME
 *   has an effect) or Read-Only.
 *
 * Returns: errno
 */

int
gfs2_glock_nq_atime(struct gfs2_holder *gh)
{
	ENTER(G2FN_GLOCK_NQ_ATIME)
	struct gfs2_glock *gl = gh->gh_gl;
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct gfs2_inode *ip = get_gl2ip(gl);
	int64_t curtime, quantum = gfs2_tune_get(sdp, gt_atime_quantum);
	unsigned int state;
	int flags;
	int error;

	if (gfs2_assert_warn(sdp, gh->gh_flags & GL_ATIME) ||
	    gfs2_assert_warn(sdp, !(gh->gh_flags & GL_ASYNC)) ||
	    gfs2_assert_warn(sdp, gl->gl_ops == &gfs2_inode_glops))
		RETURN(G2FN_GLOCK_NQ_ATIME, -EINVAL);

	/* Save original request state of lock holder */
	state = gh->gh_state;
	flags = gh->gh_flags;

	error = gfs2_glock_nq(gh);
	if (error)
		RETURN(G2FN_GLOCK_NQ_ATIME, error);

	if (test_bit(SDF_NOATIME, &sdp->sd_flags) ||
	    test_bit(SDF_ROFS, &sdp->sd_flags))
		RETURN(G2FN_GLOCK_NQ_ATIME, 0);

	curtime = get_seconds();
	if (curtime - ip->i_di.di_atime >= quantum) {
		/* Get EX hold (force EX glock via !ANY) to write the dinode */
		gfs2_glock_dq(gh);
		gfs2_holder_reinit(LM_ST_EXCLUSIVE,
				  gh->gh_flags & ~LM_FLAG_ANY,
				  gh);
		error = gfs2_glock_nq(gh);
		if (error)
			RETURN(G2FN_GLOCK_NQ_ATIME, error);

		/* Verify that atime hasn't been updated while we were
		   trying to get exclusive lock. */

		curtime = get_seconds();
		if (curtime - ip->i_di.di_atime >= quantum) {
			struct buffer_head *dibh;

			error = gfs2_trans_begin(sdp, RES_DINODE, 0);
			if (error == -EROFS)
				RETURN(G2FN_GLOCK_NQ_ATIME, 0);
			if (error)
				goto fail;

			error = gfs2_get_inode_buffer(ip, &dibh);
			if (error)
				goto fail_end_trans;

			ip->i_di.di_atime = curtime;

			gfs2_trans_add_bh(ip->i_gl, dibh);
			gfs2_dinode_out(&ip->i_di, dibh->b_data);
			brelse(dibh);

			gfs2_trans_end(sdp);
		}

		/* If someone else has asked for the glock,
		   unlock and let them have it. Then reacquire
		   in the original state. */
		if (gfs2_glock_is_blocking(gl)) {
			gfs2_glock_dq(gh);
			gfs2_holder_reinit(state, flags, gh);
			RETURN(G2FN_GLOCK_NQ_ATIME, gfs2_glock_nq(gh));
		}
	}

	RETURN(G2FN_GLOCK_NQ_ATIME, 0);

 fail_end_trans:
	gfs2_trans_end(sdp);

 fail:
	gfs2_glock_dq(gh);

	RETURN(G2FN_GLOCK_NQ_ATIME, error);
}

/**
 * glock_compare_atime - Compare two struct gfs2_glock structures for gfs2_sort()
 * @arg_a: the first structure
 * @arg_b: the second structure
 *
 * Sort order determined by (in order of priority):
 * -- lock number
 * -- lock state (SHARED > EXCLUSIVE or GL_ATIME, which can demand EXCLUSIVE)
 *
 * Returns: 1 if A > B
 *         -1 if A < B
 *          0 if A = B
 */

static int
glock_compare_atime(const void *arg_a, const void *arg_b)
{
	ENTER(G2FN_GLOCK_COMPARE_ATIME)
	struct gfs2_holder *gh_a = *(struct gfs2_holder **)arg_a;
	struct gfs2_holder *gh_b = *(struct gfs2_holder **)arg_b;
	struct lm_lockname *a = &gh_a->gh_gl->gl_name;
	struct lm_lockname *b = &gh_b->gh_gl->gl_name;
	int ret = 0;

	if (a->ln_number > b->ln_number)
		ret = 1;
	else if (a->ln_number < b->ln_number)
		ret = -1;
	else {
		if (gh_a->gh_state == LM_ST_SHARED &&
		    gh_b->gh_state == LM_ST_EXCLUSIVE)
			ret = 1;
		else if (gh_a->gh_state == LM_ST_SHARED &&
			 (gh_b->gh_flags & GL_ATIME))
			ret = 1;
	}

	RETURN(G2FN_GLOCK_COMPARE_ATIME, ret);
}

/**
 * gfs2_glock_nq_m_atime - acquire multiple glocks where one may need an
 *      atime update
 * @num_gh: the number of structures
 * @ghs: an array of struct gfs2_holder structures
 *
 * Returns: 0 on success (all glocks acquired),
 *          errno on failure (no glocks acquired)
 */

int
gfs2_glock_nq_m_atime(unsigned int num_gh, struct gfs2_holder *ghs)
{
	ENTER(G2FN_GLOCK_NQ_M_ATIME)
	struct gfs2_holder **p;
	unsigned int x;
	int error = 0;

	if (!num_gh)
		RETURN(G2FN_GLOCK_NQ_M_ATIME, 0);

	if (num_gh == 1) {
		ghs->gh_flags &= ~(LM_FLAG_TRY | GL_ASYNC);
		if (ghs->gh_flags & GL_ATIME)
			error = gfs2_glock_nq_atime(ghs);
		else
			error = gfs2_glock_nq(ghs);
		RETURN(G2FN_GLOCK_NQ_M_ATIME, error);
	}

	p = kmalloc(num_gh * sizeof(struct gfs2_holder *), GFP_KERNEL);
	if (!p)
		RETURN(G2FN_GLOCK_NQ_M_ATIME, -ENOMEM);

	for (x = 0; x < num_gh; x++)
		p[x] = &ghs[x];

	gfs2_sort(p, num_gh, sizeof(struct gfs2_holder *), glock_compare_atime);

	for (x = 0; x < num_gh; x++) {
		p[x]->gh_flags &= ~(LM_FLAG_TRY | GL_ASYNC);

		if (p[x]->gh_flags & GL_ATIME)
			error = gfs2_glock_nq_atime(p[x]);
		else
			error = gfs2_glock_nq(p[x]);

		if (error) {
			while (x--)
				gfs2_glock_dq(p[x]);
			break;
		}
	}

	kfree(p);

	RETURN(G2FN_GLOCK_NQ_M_ATIME, error);
}

/**
 * gfs2_try_toss_vnode - See if we can toss a vnode from memory
 * @ip: the inode
 *
 * Returns:  TRUE if the vnode was tossed
 */

void
gfs2_try_toss_vnode(struct gfs2_inode *ip)
{
	ENTER(G2FN_TRY_TOSS_VNODE)
	struct inode *inode;

	inode = gfs2_ip2v(ip, NO_CREATE);
	if (!inode)
		RET(G2FN_TRY_TOSS_VNODE);

	d_prune_aliases(inode);

	if (S_ISDIR(ip->i_di.di_mode)) {
		struct list_head *head = &inode->i_dentry;
		struct dentry *d = NULL;

		spin_lock(&dcache_lock);
		if (list_empty(head))
			spin_unlock(&dcache_lock);
		else {
			d = list_entry(head->next, struct dentry, d_alias);
			dget_locked(d);
			spin_unlock(&dcache_lock);

			if (have_submounts(d))
				dput(d);
			else {
				shrink_dcache_parent(d);
				dput(d);
				d_prune_aliases(inode);
			}
		}
	}

	inode->i_nlink = 0;
	iput(inode);

	RET(G2FN_TRY_TOSS_VNODE);
}

/**
 * gfs2_setattr_simple -
 * @ip:
 * @attr:
 *
 * Called with a reference on the vnode.
 *
 * Returns: errno
 */

int
gfs2_setattr_simple(struct gfs2_inode *ip, struct iattr *attr)
{
	ENTER(G2FN_SETATTR_SIMPLE)
	struct buffer_head *dibh;
	int error;

	error = gfs2_trans_begin(ip->i_sbd, RES_DINODE, 0);
	if (error)
		RETURN(G2FN_SETATTR_SIMPLE, error);

	error = gfs2_get_inode_buffer(ip, &dibh);
	if (!error) {
		inode_setattr(ip->i_vnode, attr);
		gfs2_inode_attr_out(ip);

		gfs2_trans_add_bh(ip->i_gl, dibh);
		gfs2_dinode_out(&ip->i_di, dibh->b_data);
		brelse(dibh);
	}

	gfs2_trans_end(ip->i_sbd);

	RETURN(G2FN_SETATTR_SIMPLE, error);
}

int
gfs2_repermission(struct inode *inode, int mask, struct nameidata *nd)
{
	ENTER(G2FN_REPERMISSION)
	RETURN(G2FN_REPERMISSION,
	       permission(inode, mask, nd));
}

