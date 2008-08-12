#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/posix_acl.h>

#include "gfs.h"
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
 * @ip: The GFS inode (with embedded disk inode data)
 * @inode:  The Linux VFS inode
 *
 */

static void
inode_attr_in(struct gfs_inode *ip, struct inode *inode)
{
	unsigned int mode;

	inode->i_ino = ip->i_num.no_formal_ino;

	switch (ip->i_di.di_type) {
	case GFS_FILE_REG:
		mode = S_IFREG;
		inode->i_rdev = 0;
		break;
	case GFS_FILE_DIR:
		mode = S_IFDIR;
		inode->i_rdev = 0;
		break;
	case GFS_FILE_LNK:
		mode = S_IFLNK;
		inode->i_rdev = 0;
		break;
	case GFS_FILE_BLK:
		mode = S_IFBLK;
		inode->i_rdev = MKDEV(ip->i_di.di_major, ip->i_di.di_minor);
		break;
	case GFS_FILE_CHR:
		mode = S_IFCHR;
		inode->i_rdev = MKDEV(ip->i_di.di_major, ip->i_di.di_minor);
		break;
	case GFS_FILE_FIFO:
		mode = S_IFIFO;
		inode->i_rdev = 0;
		break;
	case GFS_FILE_SOCK:
		mode = S_IFSOCK;
		inode->i_rdev = 0;
		break;
	default:
		if (gfs_consist_inode(ip))
			printk("GFS: fsid=%s: type = %u\n",
			       ip->i_sbd->sd_fsname, ip->i_di.di_type);
		return;
	};

	inode->i_mode = mode | (ip->i_di.di_mode & S_IALLUGO);
	inode->i_nlink = ip->i_di.di_nlink;
	inode->i_uid = ip->i_di.di_uid;
	inode->i_gid = ip->i_di.di_gid;
	i_size_write(inode, ip->i_di.di_size);
	inode->i_atime.tv_sec = ip->i_di.di_atime;
	inode->i_mtime.tv_sec = ip->i_di.di_mtime;
	inode->i_ctime.tv_sec = ip->i_di.di_ctime;
	inode->i_atime.tv_nsec = inode->i_mtime.tv_nsec = inode->i_ctime.tv_nsec = 0;
	inode->i_blocks = ip->i_di.di_blocks <<
		(ip->i_sbd->sd_sb.sb_bsize_shift - GFS_BASIC_BLOCK_SHIFT);
	inode->i_generation = ip->i_di.di_header.mh_incarn;

	if (ip->i_di.di_flags & GFS_DIF_IMMUTABLE)
		inode->i_flags |= S_IMMUTABLE;
	else
		inode->i_flags &= ~S_IMMUTABLE;

	if (ip->i_di.di_flags & GFS_DIF_APPENDONLY)
		inode->i_flags |= S_APPEND;
	else
		inode->i_flags &= ~S_APPEND;
}

/**
 * gfs_inode_attr_in - Copy attributes from the dinode into the VFS inode
 * @ip: The GFS inode (with embedded disk inode data)
 *
 */

void
gfs_inode_attr_in(struct gfs_inode *ip)
{
	struct inode *inode;

	inode = gfs_iget(ip, NO_CREATE);
	if (inode) {
		inode_attr_in(ip, inode);
		iput(inode);
	}

}

/**
 * gfs_inode_attr_out - Copy attributes from VFS inode into the dinode
 * @ip: The GFS inode
 *
 * Only copy out the attributes that we want the VFS layer
 * to be able to modify.
 */

void
gfs_inode_attr_out(struct gfs_inode *ip)
{
	struct inode *inode = ip->i_vnode;

	ip->i_di.di_mode = inode->i_mode & S_IALLUGO;
	ip->i_di.di_uid = inode->i_uid;
	ip->i_di.di_gid = inode->i_gid;
	ip->i_di.di_atime = inode->i_atime.tv_sec;
	ip->i_di.di_mtime = inode->i_mtime.tv_sec;
	ip->i_di.di_ctime = inode->i_ctime.tv_sec;
}

/**
 * gfs_iget - Get/Create a struct inode for a struct gfs_inode
 * @ip: the struct gfs_inode to get the struct inode for
 * @create: CREATE -- create a new struct inode if one does not already exist
 *          NO_CREATE -- return NULL if inode doesn't exist
 *
 * Returns: A VFS inode, or NULL if NO_CREATE and none in existance
 *
 * If this function creates a new inode, it:
 *   Copies fields from the GFS on-disk (d)inode to the VFS inode
 *   Attaches the appropriate ops vectors to the VFS inode and address_space
 *   Attaches the VFS inode to the gfs_inode
 *   Inserts the new inode in the VFS inode hash, while avoiding races
 */

struct inode *
gfs_iget(struct gfs_inode *ip, int create)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	struct inode *inode = NULL, *tmp;

	spin_lock(&ip->i_spin);
	if (ip->i_vnode)
		inode = igrab(ip->i_vnode);
	spin_unlock(&ip->i_spin);

	if (inode || !create)
		return inode;

	tmp = new_inode(ip->i_sbd->sd_vfs);
	if (!tmp)
		return NULL;

	inode_attr_in(ip, tmp);

	/* Attach GFS-specific ops vectors */
	if (ip->i_di.di_type == GFS_FILE_REG) {
		tmp->i_op = &gfs_file_iops;
		memcpy(&ip->gfs_file_aops, &gfs_file_aops,
			   sizeof(struct address_space_operations));
		tmp->i_mapping->a_ops = &ip->gfs_file_aops;
		if (sdp->sd_args.ar_localflocks)
			tmp->i_fop = &gfs_file_fops_nolock;
		else
			tmp->i_fop = &gfs_file_fops;
	} else if (ip->i_di.di_type == GFS_FILE_DIR) {
		tmp->i_op = &gfs_dir_iops;
		if (sdp->sd_args.ar_localflocks)
			tmp->i_fop = &gfs_dir_fops_nolock;
		else
			tmp->i_fop = &gfs_dir_fops;
	} else if (ip->i_di.di_type == GFS_FILE_LNK) {
		tmp->i_op = &gfs_symlink_iops;
	} else {
		tmp->i_op = &gfs_dev_iops;
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
		spin_lock(&ip->i_spin);
		if (!ip->i_vnode)
			break;
		inode = igrab(ip->i_vnode);
		spin_unlock(&ip->i_spin);

		if (inode) {
			iput(tmp);
			return inode;
		}
		yield();
	}

	inode = tmp;

	gfs_inode_hold(ip);
	ip->i_vnode = inode;
	set_v2ip(inode, ip);

	spin_unlock(&ip->i_spin);

	insert_inode_hash(inode);

	return inode;
}

/**
 * gfs_copyin_dinode - Refresh the incore copy of the dinode
 * @ip: The GFS inode
 *
 * Returns: errno
 */

int
gfs_copyin_dinode(struct gfs_inode *ip)
{
	struct buffer_head *dibh;
	int error;

	error = gfs_get_inode_buffer(ip, &dibh);
	if (error)
		return error;

	if (gfs_metatype_check(ip->i_sbd, dibh, GFS_METATYPE_DI)) {
		brelse(dibh);
		return -EIO;
	}

	gfs_dinode_in(&ip->i_di, dibh->b_data);
	brelse(dibh);

	if (ip->i_num.no_formal_ino != ip->i_di.di_num.no_formal_ino) {
		if (gfs_consist_inode(ip))
			gfs_dinode_print(&ip->i_di);
		return -EIO;
	}

	/* Handle a moved inode (not implemented yet) */
	if (ip->i_num.no_addr != ip->i_di.di_num.no_addr) {
		if (gfs_consist_inode(ip))
			gfs_dinode_print(&ip->i_di);
		return -EIO;
	}

	ip->i_vn = ip->i_gl->gl_vn;

	return 0;
}

/**
 * inode_create - create a struct gfs_inode, acquire Inode-Open (iopen) glock,
 *      read dinode from disk
 * @i_gl: The (already held) glock covering the inode
 * @inum: The inode number
 * @io_gl: the iopen glock to acquire/hold (using holder in new gfs_inode)
 * @io_state: the state the iopen glock should be acquired in
 * @ipp: pointer to put the returned inode in
 *
 * Returns: errno
 */

static int
inode_create(struct gfs_glock *i_gl, struct gfs_inum *inum,
	     struct gfs_glock *io_gl, unsigned int io_state,
	     struct gfs_inode **ipp)
{
	struct gfs_sbd *sdp = i_gl->gl_sbd;
	struct gfs_inode *ip;
	int error = 0;

	RETRY_MALLOC(ip = kmem_cache_alloc(gfs_inode_cachep, GFP_KERNEL), ip);
	memset(ip, 0, sizeof(struct gfs_inode));

	ip->i_num = *inum;

	atomic_set(&ip->i_count, 1);

	ip->i_gl = i_gl;
	ip->i_sbd = sdp;

	spin_lock_init(&ip->i_spin);
	init_rwsem(&ip->i_rw_mutex);

	ip->i_greedy = gfs_tune_get(sdp, gt_greedy_default);

	/* Lock the iopen glock (may be recursive) */
	error = gfs_glock_nq_init(io_gl,
				  io_state, GL_LOCAL_EXCL | GL_EXACT,
				  &ip->i_iopen_gh);
	if (error)
		goto fail;

	ip->i_iopen_gh.gh_owner = NULL;

	/* Assign the inode's glock as this iopen glock's protected object */
	spin_lock(&io_gl->gl_spin);
	gfs_glock_hold(i_gl);
	set_gl2gl(io_gl, i_gl);
	spin_unlock(&io_gl->gl_spin);

	/* Read dinode from disk */
	error = gfs_copyin_dinode(ip);
	if (error)
		goto fail_iopen;

	gfs_glock_hold(i_gl);
	set_gl2ip(i_gl, ip);

	atomic_inc(&sdp->sd_inode_count);

	*ipp = ip;

	return 0;

 fail_iopen:
	spin_lock(&io_gl->gl_spin);
	set_gl2gl(io_gl, NULL);
	gfs_glock_put(i_gl);
	spin_unlock(&io_gl->gl_spin);

	gfs_glock_dq_uninit(&ip->i_iopen_gh);

 fail:
	gfs_flush_meta_cache(ip);
	kmem_cache_free(gfs_inode_cachep, ip);
	*ipp = NULL;

	return error;
}

/**
 * gfs_inode_get - Get an inode given its number
 * @i_gl: The glock covering the inode
 * @inum: The inode number
 * @create: Flag to say if we are allowed to create a new struct gfs_inode
 * @ipp: pointer to put the returned inode in
 *
 * Returns: errno
 *
 * If creating a new gfs_inode structure, reads dinode from disk.
 */

int
gfs_inode_get(struct gfs_glock *i_gl, struct gfs_inum *inum, int create,
		struct gfs_inode **ipp)
{
	struct gfs_glock *io_gl;
	int error = 0;

	*ipp = get_gl2ip(i_gl);
	if (*ipp) {
		atomic_inc(&(*ipp)->i_count);
		gfs_assert_warn(i_gl->gl_sbd, 
				(*ipp)->i_num.no_formal_ino ==
				inum->no_formal_ino);
	} else if (create) {
		error = gfs_glock_get(i_gl->gl_sbd,
				      inum->no_addr, &gfs_iopen_glops,
				      CREATE, &io_gl);
		if (!error) {
			error = inode_create(i_gl, inum, io_gl,
					     LM_ST_SHARED, ipp);
			gfs_glock_put(io_gl);
		}
	}

	return error;
}

/**
 * gfs_inode_hold - hold a struct gfs_inode structure
 * @ip: The GFS inode
 *
 */

void
gfs_inode_hold(struct gfs_inode *ip)
{
	gfs_assert(ip->i_sbd, atomic_read(&ip->i_count) > 0,);
	atomic_inc(&ip->i_count);
}

/**
 * gfs_inode_put - put a struct gfs_inode structure
 * @ip: The GFS inode
 *
 */

void
gfs_inode_put(struct gfs_inode *ip)
{
	gfs_assert(ip->i_sbd, atomic_read(&ip->i_count) > 0,);
	atomic_dec(&ip->i_count);
}

/**
 * gfs_inode_destroy - Destroy a GFS inode structure with no references on it
 * @ip: The GFS inode
 *
 * Also, unhold the iopen glock and release indirect addressing buffers.
 * This function must be called with a glocks held on the inode and 
 *   the associated iopen.
 *
 */

void
gfs_inode_destroy(struct gfs_inode *ip)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_glock *io_gl = ip->i_iopen_gh.gh_gl;
	struct gfs_glock *i_gl = ip->i_gl;

	gfs_assert_warn(sdp, !atomic_read(&ip->i_count));
	gfs_assert(sdp, get_gl2gl(io_gl) == i_gl,);

	/* Unhold the iopen glock */
	spin_lock(&io_gl->gl_spin);
	set_gl2gl(io_gl, NULL);
	gfs_glock_put(i_gl);
	spin_unlock(&io_gl->gl_spin);

	gfs_glock_dq_uninit(&ip->i_iopen_gh);

	/* Release indirect addressing buffers, destroy the GFS inode struct */
	gfs_flush_meta_cache(ip);
	kmem_cache_free(gfs_inode_cachep, ip);

	set_gl2ip(i_gl, NULL);
	gfs_glock_put(i_gl);

	atomic_dec(&sdp->sd_inode_count);
}

/**
 * dinode_mark_unused - Set UNUSED flag in on-disk dinode
 * @ip:
 *
 * Also:
 * --  Increment incarnation number, to indicate that it no longer
 *       represents the old inode.
 * --  Update change time (ctime)
 *
 * Returns: errno
 */

static int
dinode_mark_unused(struct gfs_inode *ip)
{
	struct buffer_head *dibh;
	struct gfs_dinode *di;
	uint32_t incarn;
	uint64_t ctime;
	uint32_t flags;
	int error;

	error = gfs_get_inode_buffer(ip, &dibh);
	if (error)
		return error;

	di = (struct gfs_dinode *)dibh->b_data;

	gfs_trans_add_bh(ip->i_gl, dibh);

	incarn = gfs32_to_cpu(di->di_header.mh_incarn) + 1;
	di->di_header.mh_incarn = cpu_to_gfs32(incarn);

	ctime = get_seconds();
	di->di_ctime = cpu_to_gfs64(ctime);

	flags = (gfs32_to_cpu(di->di_flags)) | GFS_DIF_UNUSED;
	di->di_flags = cpu_to_gfs32(flags);

	brelse(dibh);

	return 0;
}

/**
 * dinode_dealloc - Put deallocate a dinode
 * @ip: The GFS inode
 *
 * Returns: errno
 */

static int
dinode_dealloc(struct gfs_inode *ip)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_alloc *al;
	struct gfs_rgrpd *rgd;
	int error;

	if (ip->i_di.di_blocks != 1) {
		if (gfs_consist_inode(ip))
			gfs_dinode_print(&ip->i_di);
		return -EIO;
	}

	al = gfs_alloc_get(ip);

	error = gfs_quota_hold_m(ip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
	if (error)
		goto out;

	error = gfs_rindex_hold(sdp, &al->al_ri_gh);
	if (error)
		goto out_qs;

	rgd = gfs_blk2rgrpd(sdp, ip->i_num.no_addr);
	if (!rgd) {
		gfs_consist_inode(ip);
		error = -EIO;
		goto out_rindex_relse;
	}

	error = gfs_glock_nq_init(rgd->rd_gl, LM_ST_EXCLUSIVE, 0, &al->al_rgd_gh);
	if (error)
		goto out_rindex_relse;

	/* Trans may require:
	   One block for the RG header.
	   One block for the dinode bit.
	   One block for the dinode.
	   We also need a block for the unlinked change.
	   One block for the quota change. */

	error = gfs_trans_begin(sdp, 3, 2);
	if (error)
		goto out_rg_gunlock;

	/* Set the UNUSED flag in the on-disk dinode block, increment incarn */
	error = dinode_mark_unused(ip);
	if (error)
		goto out_end_trans;

	/* De-allocate on-disk dinode block to FREEMETA */
	gfs_difree(rgd, ip);

	gfs_trans_add_unlinked(sdp, GFS_LOG_DESC_IDA, &ip->i_num);
	clear_bit(GLF_STICKY, &ip->i_gl->gl_flags);

 out_end_trans:
	gfs_trans_end(sdp);

 out_rg_gunlock:
	gfs_glock_dq_uninit(&al->al_rgd_gh);

 out_rindex_relse:
	gfs_glock_dq_uninit(&al->al_ri_gh);

 out_qs:
	gfs_quota_unhold_m(ip);

 out:
	gfs_alloc_put(ip);

	return error;
}

/**
 * inode_dealloc - Deallocate all on-disk blocks for an inode (dinode)
 * @sdp: the filesystem
 * @inum: the inode number to deallocate
 * @io_gh: a holder for the iopen glock for this inode
 *
 * De-allocates all on-disk blocks, data and metadata, associated with an inode.
 * All metadata blocks become GFS_BLKST_FREEMETA.
 * All data blocks become GFS_BLKST_FREE.
 * Also de-allocates incore gfs_inode structure.
 *
 * Returns: errno
 */

static int
inode_dealloc(struct gfs_sbd *sdp, struct gfs_inum *inum,
		struct gfs_holder *io_gh)
{
	struct gfs_inode *ip;
	struct gfs_holder i_gh;
	int error;

	/* Lock the inode as we blow it away */
	error = gfs_glock_nq_num(sdp,
				 inum->no_formal_ino, &gfs_inode_glops,
				 LM_ST_EXCLUSIVE, 0, &i_gh);
	if (error)
		return error;

	/* We reacquire the iopen lock here to avoid a race with the NFS server
	   calling gfs_read_inode() with the inode number of a inode we're in
	   the process of deallocating.  And we can't keep our hold on the lock
	   from inode_dealloc_init() for deadlock reasons.  We do, however,
	   overlap this iopen lock with the one to be acquired EX within
	   inode_create(), below (recursive EX locks will be granted to same
	   holder process, i.e. this process). */

	gfs_holder_reinit(LM_ST_EXCLUSIVE, LM_FLAG_TRY, io_gh);
	error = gfs_glock_nq(io_gh);
	switch (error) {
	case 0:
		break;
	case GLR_TRYFAILED:
		error = 0;
		goto fail;
	default:
		goto fail;
	}

	gfs_assert_warn(sdp, !get_gl2ip(i_gh.gh_gl));
	error = inode_create(i_gh.gh_gl, inum, io_gh->gh_gl, LM_ST_EXCLUSIVE,
			     &ip);

	gfs_glock_dq(io_gh);

	if (error)
		goto fail;

	/* Verify disk (d)inode, gfs inode, and VFS (v)inode are unused */
	if (ip->i_di.di_nlink) {
		if (gfs_consist_inode(ip))
			gfs_dinode_print(&ip->i_di);
		error = -EIO;
		goto fail_iput;
	}
	gfs_assert_warn(sdp, atomic_read(&ip->i_count) == 1);
	gfs_assert_warn(sdp, !ip->i_vnode);

	/* Free all on-disk directory leaves (if any) to FREEMETA state */
	if (ip->i_di.di_type == GFS_FILE_DIR &&
	    (ip->i_di.di_flags & GFS_DIF_EXHASH)) {
		error = gfs_dir_exhash_free(ip);
		if (error)
			goto fail_iput;
	}

	/* Free all on-disk extended attribute blocks to FREEMETA state */
	if (ip->i_di.di_eattr) {
		error = gfs_ea_dealloc(ip);
		if (error)
			goto fail_iput;
	}

	/* Free all data blocks to FREE state, and meta blocks to FREEMETA */
	error = gfs_shrink(ip, 0, NULL);
	if (error)
		goto fail_iput;

	/* Set UNUSED flag and increment incarn # in on-disk dinode block,
	   and de-alloc the block to FREEMETA */
	error = dinode_dealloc(ip);
	if (error)
		goto fail_iput;

	/* Free the GFS inode structure, unhold iopen and inode glocks */
	gfs_inode_put(ip);
	gfs_inode_destroy(ip);

	gfs_glock_dq_uninit(&i_gh);

	return 0;

 fail_iput:
	gfs_inode_put(ip);
	gfs_inode_destroy(ip);

 fail:
	gfs_glock_dq_uninit(&i_gh);

	return error;
}

/**
 * inode_dealloc_init - Try to deallocate an initialized on-disk inode (dinode)
 *      and all of its associated data and meta blocks
 * @sdp: the filesystem
 *
 * Returns: 0 on success, -errno on error, 1 on busy (inode open)
 */

static int
inode_dealloc_init(struct gfs_sbd *sdp, struct gfs_inum *inum)
{
	struct gfs_holder io_gh;
	int error = 0;

	/* If not busy (on this node), de-alloc GFS incore inode, releasing
	   any indirect addressing buffers, and unholding iopen glock */
	gfs_try_toss_inode(sdp, inum);

	/* Does another process (cluster-wide) have this inode open? */
	error = gfs_glock_nq_num(sdp,
				 inum->no_addr, &gfs_iopen_glops,
				 LM_ST_EXCLUSIVE, LM_FLAG_TRY_1CB, &io_gh);
	switch (error) {
	case 0:
		break;
	case GLR_TRYFAILED:
		return 1;
	default:
		return error;
	}

	/* Unlock here to prevent deadlock */
	gfs_glock_dq(&io_gh);

	/* No other process in the entire cluster has this inode open;
	   we can remove it and all of its associated blocks from disk */
	error = inode_dealloc(sdp, inum, &io_gh);
	gfs_holder_uninit(&io_gh);

	return error;
}

/**
 * inode_dealloc_uninit - dealloc an uninitialized on-disk inode (dinode) block
 * @sdp: the filesystem
 *
 * Create a transaction to change dinode block's alloc state to FREEMETA
 *
 * Returns: 0 on success, -errno on error, 1 on busy
 */

static int
inode_dealloc_uninit(struct gfs_sbd *sdp, struct gfs_inum *inum)
{
	struct gfs_rgrpd *rgd;
	struct gfs_holder ri_gh, rgd_gh;
	int error;

	error = gfs_rindex_hold(sdp, &ri_gh);
	if (error)
		return error;

	rgd = gfs_blk2rgrpd(sdp, inum->no_addr);
	if (!rgd) {
		gfs_consist(sdp);
		error = -EIO;
		goto fail;
	}

	error = gfs_glock_nq_init(rgd->rd_gl, LM_ST_EXCLUSIVE, 0, &rgd_gh);
	if (error)
		goto fail;

	/* Trans may require:
	   One block for the RG header.
	   One block for the dinode bit.
	   We also need a block for the unlinked change. */

	error = gfs_trans_begin(sdp, 2, 1);
	if (error)
		goto fail_gunlock;

	gfs_difree_uninit(rgd, inum->no_addr);
	gfs_trans_add_unlinked(sdp, GFS_LOG_DESC_IDA, inum);

	gfs_trans_end(sdp);

	gfs_glock_dq_uninit(&rgd_gh);
	gfs_glock_dq_uninit(&ri_gh);

	return 0;

 fail_gunlock:
	gfs_glock_dq_uninit(&rgd_gh);

 fail:
	gfs_glock_dq_uninit(&ri_gh);

	return error;
}

/**
 * gfs_inode_dealloc - Grab an unlinked inode off the list and try to free it.
 * @sdp: the filesystem
 *
 * Returns: 0 on success, -errno on error, 1 on busy
 */

int
gfs_inode_dealloc(struct gfs_sbd *sdp, struct gfs_inum *inum)
{
	if (inum->no_formal_ino)
		return inode_dealloc_init(sdp, inum);
	else
		return inode_dealloc_uninit(sdp, inum);
}

/**
 * gfs_change_nlink - Change nlink count on inode
 * @ip: The GFS inode
 * @diff: The change in the nlink count required
 *
 * Returns: errno
 */

int
gfs_change_nlink(struct gfs_inode *ip, int diff)
{
	struct buffer_head *dibh;
	uint32_t nlink;
	int error;

	nlink = ip->i_di.di_nlink + diff;

	/* Tricky.  If we are reducing the nlink count,
	   but the new value ends up being bigger than the
	   old one, we must have underflowed. */
	if (diff < 0 && nlink > ip->i_di.di_nlink) {
		if (gfs_consist_inode(ip))
			gfs_dinode_print(&ip->i_di);
		return -EIO;
	}

	error = gfs_get_inode_buffer(ip, &dibh);
	if (error)
		return error;

	ip->i_di.di_nlink = nlink;
	ip->i_di.di_ctime = get_seconds();

	gfs_trans_add_bh(ip->i_gl, dibh);
	gfs_dinode_out(&ip->i_di, dibh->b_data);
	brelse(dibh);

	return 0;
}

/**
 * gfs_lookupi - Look up a filename in a directory and return its inode
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
gfs_lookupi(struct gfs_holder *d_gh, struct qstr *name,
	    int is_root, struct gfs_holder *i_gh)
{
	struct gfs_inode *dip = get_gl2ip(d_gh->gh_gl);
	struct gfs_sbd *sdp = dip->i_sbd;
	struct gfs_glock *gl;
	struct gfs_inode *ip;
	struct gfs_inum inum, inum2;
	unsigned int type;
	int error;

	i_gh->gh_gl = NULL;

	if (!name->len || name->len > GFS_FNAMESIZE)
		return -ENAMETOOLONG;

	if (gfs_filecmp(name, ".", 1) ||
	    (gfs_filecmp(name, "..", 2) && dip == sdp->sd_rooti)) {
		gfs_holder_reinit(LM_ST_SHARED, 0, d_gh);
		error = gfs_glock_nq(d_gh);
		if (!error) {
			error = gfs_glock_nq_init(dip->i_gl,
						  LM_ST_SHARED, 0,
						  i_gh);
			if (error) {
				gfs_glock_dq(d_gh);
				return error;
			}
			gfs_inode_hold(dip);
		}
		return error;
	}

	if (gfs_assert_warn(sdp, !gfs_glock_is_locked_by_me(d_gh->gh_gl)))
		return -EINVAL;

	gfs_holder_reinit(LM_ST_SHARED, 0, d_gh);
	error = gfs_glock_nq(d_gh);
	if (error)
		return error;

	if (!is_root) {
		error = inode_permission(dip->i_vnode, MAY_EXEC);
		if (error) {
			gfs_glock_dq(d_gh);
			return error;
		}
	}

	error = gfs_dir_search(dip, name, &inum, &type);
	if (error) {
		gfs_glock_dq(d_gh);
		if (error == -ENOENT)
			error = 0;
		return error;
	}

 restart:
	error = gfs_glock_get(sdp, inum.no_formal_ino, &gfs_inode_glops,
			      CREATE, &gl);
	if (error) {
		gfs_glock_dq(d_gh);
		return error;
	}

	/*  Acquire the second lock  */

	if (gl->gl_name.ln_number < dip->i_gl->gl_name.ln_number) {
		gfs_glock_dq(d_gh);

		error = gfs_glock_nq_init(gl, LM_ST_SHARED,
					  LM_FLAG_ANY | GL_LOCAL_EXCL,
					  i_gh);
		if (error)
			goto out;

		gfs_holder_reinit(LM_ST_SHARED, 0, d_gh);
		error = gfs_glock_nq(d_gh);
		if (error) {
			gfs_glock_dq_uninit(i_gh);
			goto out;
		}

		if (!is_root) {
			error = inode_permission(dip->i_vnode, MAY_EXEC);
			if (error) {
				gfs_glock_dq(d_gh);
				gfs_glock_dq_uninit(i_gh);
				goto out;
			}
		}

		error = gfs_dir_search(dip, name, &inum2, &type);
		if (error) {
			gfs_glock_dq(d_gh);
			gfs_glock_dq_uninit(i_gh);
			if (error == -ENOENT)
				error = 0;
			goto out;
		}

		if (!gfs_inum_equal(&inum, &inum2)) {
			gfs_glock_dq_uninit(i_gh);
			gfs_glock_put(gl);
			inum = inum2;
			goto restart;
		}
	} else {
		error = gfs_glock_nq_init(gl, LM_ST_SHARED,
					  LM_FLAG_ANY | GL_LOCAL_EXCL,
					  i_gh);
		if (error) {
			gfs_glock_dq(d_gh);
			goto out;
		}
	}

	error = gfs_inode_get(gl, &inum, CREATE, &ip);
	if (error) {
		gfs_glock_dq(d_gh);
		gfs_glock_dq_uninit(i_gh);
	} else if (ip->i_di.di_type != type) {
		gfs_consist_inode(dip);
		gfs_inode_put(ip);
		gfs_glock_dq(d_gh);
		gfs_glock_dq_uninit(i_gh);
		error = -EIO;
	}

 out:
	gfs_glock_put(gl);

	return error;
}

/**
 * create_ok - OK to create a new on-disk inode here?
 * @dip:  Directory in which dinode is to be created
 * @name:  Name of new dinode
 * @type:  GFS_FILE_XXX (regular file, dir, etc.)
 *
 * Returns: errno
 */

static int
create_ok(struct gfs_inode *dip, struct qstr *name, unsigned int type)
{
	int error;

	error = inode_permission(dip->i_vnode, MAY_WRITE | MAY_EXEC);
	if (error)
		return error;

	/*  Don't create entries in an unlinked directory  */

	if (!dip->i_di.di_nlink)
		return -EPERM;

	error = gfs_dir_search(dip, name, NULL, NULL);
	switch (error) {
	case -ENOENT:
		error = 0;
		break;
	case 0:
		return -EEXIST;
	default:
		return error;
	}

	if (dip->i_di.di_entries == (uint32_t)-1)
		return -EFBIG;
	if (type == GFS_FILE_DIR && dip->i_di.di_nlink == (uint32_t)-1)
		return -EMLINK;

	return 0;
}

/**
 * dinode_alloc - Create an on-disk inode
 * @dip:  Directory in which to create the dinode
 * @ul:
 *
 * Since this dinode is not yet linked, we also create an unlinked inode
 *   descriptor.
 *
 * Returns: errno
 */

static int
dinode_alloc(struct gfs_inode *dip, struct gfs_unlinked **ul)
{
	struct gfs_sbd *sdp = dip->i_sbd;
	struct gfs_alloc *al;
	struct gfs_inum inum;
	int error;

	/* Create in-place allocation structure, reserve 1 dinode */
	al = gfs_alloc_get(dip);
	al->al_requested_di = 1;
	error = gfs_inplace_reserve(dip);
	if (error)
		goto out;

	error = gfs_trans_begin(sdp, al->al_rgd->rd_ri.ri_length, 1);
	if (error)
		goto out_inplace;

	inum.no_formal_ino = 0;
	error = gfs_dialloc(dip, &inum.no_addr);
	if (error)
		goto out_end_trans;

	*ul = gfs_trans_add_unlinked(sdp, GFS_LOG_DESC_IUL, &inum);
	gfs_unlinked_lock(sdp, *ul);

	gfs_trans_add_gl(dip->i_gl);

 out_end_trans:
	gfs_trans_end(sdp);

 out_inplace:
	gfs_inplace_release(dip);

 out:
	gfs_alloc_put(dip);

	return error;
}

/**
 * pick_formal_ino - Pick a formal inode number for a given inode
 * @sdp: the filesystem
 * @inum: the inode number structure
 *
 */

static void
pick_formal_ino(struct gfs_sbd *sdp, struct gfs_inum *inum)
{
	/*  This won't always be true  */
	inum->no_formal_ino = inum->no_addr;
}

/**
 * make_dinode - Fill in a new dinode structure
 * @dip: the directory this inode is being created in
 * @gl: The glock covering the new inode
 * @inum: the inode number
 * @type: the file type
 * @mode: the file permissions
 * @uid:
 * @gid:
 *
 */

static int
make_dinode(struct gfs_inode *dip,
	    struct gfs_glock *gl, struct gfs_inum *inum,
	    unsigned int type, unsigned int mode,
	    unsigned int uid, unsigned int gid)
{
	struct gfs_sbd *sdp = dip->i_sbd;
	struct gfs_dinode di;
	struct buffer_head *dibh;
	struct gfs_rgrpd *rgd;
	int error;

	error = gfs_dread(gl, inum->no_addr,
			  DIO_NEW | DIO_START | DIO_WAIT,
			  &dibh);
	if (error)
		return error;

	gfs_trans_add_bh(gl, dibh);
	gfs_metatype_set(dibh, GFS_METATYPE_DI, GFS_FORMAT_DI);
	gfs_buffer_clear_tail(dibh, sizeof(struct gfs_dinode));

	memset(&di, 0, sizeof(struct gfs_dinode));

	gfs_meta_header_in(&di.di_header, dibh->b_data);

	di.di_num = *inum;

	di.di_mode = mode & S_IALLUGO;
	di.di_uid = uid;
	di.di_gid = gid;
	di.di_nlink = 1;
	di.di_blocks = 1;
	di.di_atime = di.di_mtime = di.di_ctime = get_seconds();

	rgd = gfs_blk2rgrpd(sdp, inum->no_addr);
	if (!rgd) {
		if (gfs_consist(sdp))
			printk("GFS: fsid=%s: block = %"PRIu64"\n",
			       sdp->sd_fsname, inum->no_addr);
		brelse(dibh);
		return -EIO;
	}

	di.di_rgrp = rgd->rd_ri.ri_addr;
	di.di_goal_rgrp = di.di_rgrp;
	di.di_goal_dblk = di.di_goal_mblk = inum->no_addr - rgd->rd_ri.ri_data1;

	if (type == GFS_FILE_REG) {
		if ((dip->i_di.di_flags & GFS_DIF_INHERIT_JDATA) ||
		    gfs_tune_get(sdp, gt_new_files_jdata))
			di.di_flags |= GFS_DIF_JDATA;
		if ((dip->i_di.di_flags & GFS_DIF_INHERIT_DIRECTIO) ||
		    gfs_tune_get(sdp, gt_new_files_directio))
			di.di_flags |= GFS_DIF_DIRECTIO;
	} else if (type == GFS_FILE_DIR) {
		di.di_flags |= (dip->i_di.di_flags & GFS_DIF_INHERIT_DIRECTIO);
		di.di_flags |= (dip->i_di.di_flags & GFS_DIF_INHERIT_JDATA);
	}

	di.di_type = type;

	gfs_dinode_out(&di, dibh->b_data);
	brelse(dibh);

	return 0;
}

/**
 * inode_init_and_link -
 * @dip:
 * @name:
 * @inum:
 * @gl:
 * @type:
 * @mode:
 *
 * Returns: errno
 */

static int
inode_init_and_link(struct gfs_inode *dip, struct qstr *name,
		    struct gfs_inum *inum, struct gfs_glock *gl,
		    unsigned int type, mode_t mode)
{
	struct gfs_sbd *sdp = dip->i_sbd;
	struct gfs_alloc *al;
	struct gfs_inode *ip;
	unsigned int uid, gid;
	int alloc_required;
	void *acl_a_data = NULL, *acl_d_data = NULL;
	unsigned int acl_size = 0, acl_blocks = 0;
	int error;

	if (sdp->sd_args.ar_suiddir &&
	    (dip->i_di.di_mode & S_ISUID) &&
	    dip->i_di.di_uid) {
		if (type == GFS_FILE_DIR)
			mode |= S_ISUID;
		else if (dip->i_di.di_uid != current->fsuid)
			mode &= ~07111;
		uid = dip->i_di.di_uid;
	} else
		uid = current->fsuid;

	if (dip->i_di.di_mode & S_ISGID) {
		if (type == GFS_FILE_DIR)
			mode |= S_ISGID;
		gid = dip->i_di.di_gid;
	} else
		gid = current->fsgid;

	error = gfs_acl_new_prep(dip, type, &mode,
				 &acl_a_data, &acl_d_data,
				 &acl_size, &acl_blocks);
	if (error)
		return error;

	al = gfs_alloc_get(dip);

	error = gfs_quota_lock_m(dip, uid, gid);
	if (error)
		goto fail;

	error = gfs_quota_check(dip, uid, gid);
	if (error)
		goto fail_gunlock_q;

	if (acl_blocks)
		alloc_required = TRUE;
	else {
		error = gfs_diradd_alloc_required(dip, name, &alloc_required);
		if (error)
			goto fail_gunlock_q;
	}

	if (alloc_required) {
		error = gfs_quota_check(dip, dip->i_di.di_uid, dip->i_di.di_gid);
		if (error)
			goto fail_gunlock_q;

		al->al_requested_meta = sdp->sd_max_dirres + acl_blocks;

		error = gfs_inplace_reserve(dip);
		if (error)
			goto fail_gunlock_q;

		/* Trans may require:
		   blocks for two dinodes, the directory blocks necessary for
		   a new entry, RG bitmap blocks for an allocation,
		   and one block for a quota change and
		   one block for an unlinked tag. */

		error = gfs_trans_begin(sdp,
					2 + sdp->sd_max_dirres + acl_blocks +
					al->al_rgd->rd_ri.ri_length, 2);
		if (error)
			goto fail_inplace;
	} else {
		error = gfs_rindex_hold(sdp, &al->al_ri_gh);
		if (error)
			goto fail_gunlock_q;

		/* Trans may require:
		   blocks for two dinodes, a leaf block,
		   and one block for a quota change and
		   one block for an unlinked tag. */

		error = gfs_trans_begin(sdp, 3, 2);
		if (error)
			goto fail_inplace;
	}

	error = gfs_dir_add(dip, name, inum, type);
	if (error)
		goto fail_end_trans;

	error = make_dinode(dip, gl, inum, type, mode, uid, gid);
	if (error)
		goto fail_end_trans;

	al->al_ul = gfs_trans_add_unlinked(sdp, GFS_LOG_DESC_IDA,
					   &(struct gfs_inum){0, inum->no_addr});
	gfs_trans_add_quota(sdp, +1, uid, gid);

	error = gfs_inode_get(gl, inum, CREATE, &ip);

	/* This should only fail if we are already shutdown. */
	if (gfs_assert_withdraw(sdp, !error))
		goto fail_end_trans;

	if (acl_blocks)
		error = gfs_acl_new_init(dip, ip,
					 acl_a_data, acl_d_data,
					 acl_size);

	if (!alloc_required)
		gfs_glock_dq_uninit(&al->al_ri_gh);

	return error;

 fail_end_trans:
	gfs_trans_end(sdp);

 fail_inplace:
	if (alloc_required)
		gfs_inplace_release(dip);
	else
		gfs_glock_dq_uninit(&al->al_ri_gh);

 fail_gunlock_q:
	gfs_quota_unlock_m(dip);

 fail:
	gfs_alloc_put(dip);
	if (acl_a_data)
		kfree(acl_a_data);
	else if (acl_d_data)
		kfree(acl_d_data);

	return error;
}

/**
 * gfs_createi - Create a new inode
 * @d_gh: An initialized holder for the directory glock
 * @name: The name of the new file
 * @type: The type of dinode (GFS_FILE_REG, GFS_FILE_DIR, GFS_FILE_LNK, ...)
 * @mode: the permissions on the new inode
 * @i_gh: An uninitialized holder for the new inode glock
 *
 * If the return value is 0, the glocks on both the directory and the new
 * file are held.  A transaction has been started and an inplace reservation
 * is held, as well.
 *
 * Returns: errno
 */

int
gfs_createi(struct gfs_holder *d_gh, struct qstr *name,
	    unsigned int type, unsigned int mode,
	    struct gfs_holder *i_gh)
{
	struct gfs_inode *dip = get_gl2ip(d_gh->gh_gl);
	struct gfs_sbd *sdp = dip->i_sbd;
	struct gfs_unlinked *ul;
	struct gfs_inum inum;
	struct gfs_holder io_gh;
	int error;

	if (!name->len || name->len > GFS_FNAMESIZE)
		return -ENAMETOOLONG;

	gfs_holder_reinit(LM_ST_EXCLUSIVE, 0, d_gh);
	error = gfs_glock_nq(d_gh);
	if (error)
		return error;

	error = create_ok(dip, name, type);
	if (error)
		goto fail;

	error = dinode_alloc(dip, &ul);
	if (error)
		goto fail;

	inum.no_addr = ul->ul_inum.no_addr;
	pick_formal_ino(sdp, &inum);

	if (inum.no_formal_ino < dip->i_num.no_formal_ino) {
		gfs_glock_dq(d_gh);

		error = gfs_glock_nq_num(sdp,
					 inum.no_formal_ino, &gfs_inode_glops,
					 LM_ST_EXCLUSIVE, GL_SKIP, i_gh);
		if (error) {
			gfs_unlinked_unlock(sdp, ul);
			return error;
		}

		gfs_holder_reinit(LM_ST_EXCLUSIVE, 0, d_gh);
		error = gfs_glock_nq(d_gh);
		if (error) {
			gfs_glock_dq_uninit(i_gh);
			gfs_unlinked_unlock(sdp, ul);
			return error;
		}

		error = create_ok(dip, name, type);
		if (error)
			goto fail_gunlock_i;
	} else {
		error = gfs_glock_nq_num(sdp,
					 inum.no_formal_ino, &gfs_inode_glops,
					 LM_ST_EXCLUSIVE, GL_SKIP, i_gh);
		if (error)
			goto fail_ul;
	}

	error = gfs_glock_nq_num(sdp,
				 inum.no_addr, &gfs_iopen_glops,
				 LM_ST_SHARED, GL_LOCAL_EXCL | GL_EXACT,
				 &io_gh);
	if (error)
		goto fail_gunlock_i;

	error = inode_init_and_link(dip, name, &inum, i_gh->gh_gl, type, mode);
	if (error)
		goto fail_gunlock_io;

	gfs_glock_dq_uninit(&io_gh);

	return 0;

 fail_gunlock_io:
	gfs_glock_dq_uninit(&io_gh);

 fail_gunlock_i:
	gfs_glock_dq_uninit(i_gh);

 fail_ul:
	gfs_unlinked_unlock(sdp, ul);

 fail:
	gfs_glock_dq(d_gh);

	return error;
}

/**
 * gfs_unlinki - Unlink a file
 * @dip: The inode of the directory
 * @name: The name of the file to be unlinked
 * @ip: The inode of the file to be removed
 *
 * Assumes Glocks on both dip and ip are held.
 *
 * Returns: errno
 */

int
gfs_unlinki(struct gfs_inode *dip, struct qstr *name, struct gfs_inode *ip)
{
	struct gfs_sbd *sdp = dip->i_sbd;
	int error;

	error = gfs_dir_del(dip, name);
	if (error)
		return error;

	error = gfs_change_nlink(ip, -1);
	if (error)
		return error;

	/* If this inode is being unlinked from the directory structure,
	   we need to mark that in the log so that it isn't lost during
	   a crash. */

	if (!ip->i_di.di_nlink) {
		gfs_trans_add_unlinked(sdp, GFS_LOG_DESC_IUL, &ip->i_num);
		set_bit(GLF_STICKY, &ip->i_gl->gl_flags);
	}

	return 0;
}

/**
 * gfs_rmdiri - Remove a directory
 * @dip: The parent directory of the directory to be removed
 * @name: The name of the directory to be removed
 * @ip: The GFS inode of the directory to be removed
 *
 * Assumes Glocks on dip and ip are held
 *
 * Returns: errno
 */

int
gfs_rmdiri(struct gfs_inode *dip, struct qstr *name, struct gfs_inode *ip)
{
	struct gfs_sbd *sdp = dip->i_sbd;
	struct qstr dotname;
	int error;

	if (ip->i_di.di_entries != 2) {
		if (gfs_consist_inode(ip))
			gfs_dinode_print(&ip->i_di);
		return -EIO;
	}

	error = gfs_dir_del(dip, name);
	if (error)
		return error;

	error = gfs_change_nlink(dip, -1);
	if (error)
		return error;

	dotname.len = 1;
	dotname.name = ".";
	error = gfs_dir_del(ip, &dotname);
	if (error)
		return error;

	dotname.len = 2;
	dotname.name = "..";
	error = gfs_dir_del(ip, &dotname);
	if (error)
		return error;

	error = gfs_change_nlink(ip, -2);
	if (error)
		return error;

	/* This inode is being unlinked from the directory structure and
	   we need to mark that in the log so that it isn't lost during
	   a crash. */

	gfs_trans_add_unlinked(sdp, GFS_LOG_DESC_IUL, &ip->i_num);
	set_bit(GLF_STICKY, &ip->i_gl->gl_flags);

	return 0;
}

/*
 * gfs_unlink_ok - check to see that a inode is still in a directory
 * @dip: the directory
 * @name: the name of the file
 * @ip: the inode
 *
 * Assumes that the lock on (at least) @dip is held.
 *
 * Returns: 0 if the parent/child relationship is correct, errno if it isn't
 */

int
gfs_unlink_ok(struct gfs_inode *dip, struct qstr *name, struct gfs_inode *ip)
{
	struct gfs_inum inum;
	unsigned int type;
	int error;

	if (IS_IMMUTABLE(ip->i_vnode) || IS_APPEND(ip->i_vnode))
		return -EPERM;

	if ((dip->i_di.di_mode & S_ISVTX) &&
	    dip->i_di.di_uid != current->fsuid &&
	    ip->i_di.di_uid != current->fsuid &&
	    !capable(CAP_FOWNER))
		return -EPERM;

	if (IS_APPEND(dip->i_vnode))
		return -EPERM;

	error = inode_permission(dip->i_vnode, MAY_WRITE | MAY_EXEC);
	if (error)
		return error;

	error = gfs_dir_search(dip, name, &inum, &type);
	if (error)
		return error;

	if (inum.no_formal_ino != ip->i_num.no_formal_ino)
		return -ENOENT;

	if (ip->i_di.di_type != type) {
		gfs_consist_inode(dip);
		return -EIO;
	}

	return 0;
}

/*
 * gfs_ok_to_move - check if it's ok to move a directory to another directory
 * @this: move this
 * @to: to here
 *
 * Follow @to back to the root and make sure we don't encounter @this
 * Assumes we already hold the rename lock.
 *
 * Returns: errno
 */

int
gfs_ok_to_move(struct gfs_inode *this, struct gfs_inode *to)
{
	struct gfs_sbd *sdp = this->i_sbd;
	struct gfs_inode *tmp;
	struct gfs_holder to_gh, tmp_gh;
	struct qstr dotdot;
	int error = 0;

	memset(&dotdot, 0, sizeof (struct qstr));
	dotdot.name = "..";
	dotdot.len = 2;

	gfs_inode_hold(to);

	for (;;) {
		if (to == this) {
			error = -EINVAL;
			break;
		}
		if (to == sdp->sd_rooti) {
			error = 0;
			break;
		}

		gfs_holder_init(to->i_gl, 0, 0, &to_gh);

		error = gfs_lookupi(&to_gh, &dotdot, TRUE, &tmp_gh);
		if (error) {
			gfs_holder_uninit(&to_gh);
			break;
		}
		if (!tmp_gh.gh_gl) {
			gfs_holder_uninit(&to_gh);
			error = -ENOENT;
			break;
		}

		tmp = get_gl2ip(tmp_gh.gh_gl);

		gfs_glock_dq_uninit(&to_gh);
		gfs_glock_dq_uninit(&tmp_gh);

		gfs_inode_put(to);
		to = tmp;
	}

	gfs_inode_put(to);

	return error;
}

/**
 * gfs_readlinki - return the contents of a symlink
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
gfs_readlinki(struct gfs_inode *ip, char **buf, unsigned int *len)
{
	struct gfs_holder i_gh;
	struct buffer_head *dibh;
	unsigned int x;
	int error;

	gfs_holder_init(ip->i_gl, LM_ST_SHARED, GL_ATIME, &i_gh);
	error = gfs_glock_nq_atime(&i_gh);
	if (error) {
		gfs_holder_uninit(&i_gh);
		return error;
	}

	if (!ip->i_di.di_size) {
		gfs_consist_inode(ip);
		error = -EIO;
		goto out;
	}

	error = gfs_get_inode_buffer(ip, &dibh);
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

	memcpy(*buf, dibh->b_data + sizeof(struct gfs_dinode), x);
	*len = x;

 out_brelse:
	brelse(dibh);

 out:
	gfs_glock_dq_uninit(&i_gh);

	return error;
}

/**
 * gfs_glock_nq_atime - Acquire a hold on an inode's glock, and
 *       conditionally update the inode's atime
 * @gh: the holder to acquire
 *
 * Tests atime (access time) for gfs_read, gfs_readdir and gfs_mmap
 * Update if the difference between the current time and the inode's current
 * atime is greater than an interval specified at mount (or default).
 *
 * Will not update if GFS mounted NOATIME (this is *the* place where NOATIME
 *   has an effect) or Read-Only.
 *
 * Returns: errno
 */

int
gfs_glock_nq_atime(struct gfs_holder *gh)
{
	struct gfs_glock *gl = gh->gh_gl;
	struct gfs_sbd *sdp = gl->gl_sbd;
	struct gfs_inode *ip = get_gl2ip(gl);
	int64_t curtime, quantum = gfs_tune_get(sdp, gt_atime_quantum);
	unsigned int state;
	int flags;
	int error;

	if (gfs_assert_warn(sdp, gh->gh_flags & GL_ATIME) ||
	    gfs_assert_warn(sdp, !(gh->gh_flags & GL_ASYNC)) ||
	    gfs_assert_warn(sdp, gl->gl_ops == &gfs_inode_glops))
		return -EINVAL;

	/* Save original request state of lock holder */
	state = gh->gh_state;
	flags = gh->gh_flags;

	error = gfs_glock_nq(gh);
	if (error)
		return error;

	if (test_bit(SDF_NOATIME, &sdp->sd_flags) ||
	    test_bit(SDF_ROFS, &sdp->sd_flags))
		return 0;

	curtime = get_seconds();
	if (curtime - ip->i_di.di_atime >= quantum) {
		/* Get EX hold (force EX glock via !ANY) to write the dinode */
		gfs_glock_dq(gh);
		gfs_holder_reinit(LM_ST_EXCLUSIVE,
				  gh->gh_flags & ~LM_FLAG_ANY,
				  gh);
		error = gfs_glock_nq(gh);
		if (error)
			return error;

		/* Verify that atime hasn't been updated while we were
		   trying to get exclusive lock. */

		curtime = get_seconds();
		if (curtime - ip->i_di.di_atime >= quantum) {
			struct buffer_head *dibh;

			error = gfs_trans_begin(sdp, 1, 0);
			if (error == -EROFS)
				return 0;
			if (error)
				goto fail;

			error = gfs_get_inode_buffer(ip, &dibh);
			if (error)
				goto fail_end_trans;

			ip->i_di.di_atime = curtime;

			gfs_trans_add_bh(ip->i_gl, dibh);
			gfs_dinode_out(&ip->i_di, dibh->b_data);
			brelse(dibh);

			gfs_trans_end(sdp);
		}

		/* If someone else has asked for the glock,
		   unlock and let them have it. Then reacquire
		   in the original state. */
		if (gfs_glock_is_blocking(gl)) {
			gfs_glock_dq(gh);
			gfs_holder_reinit(state, flags, gh);
			return gfs_glock_nq(gh);
		}
	}

	return 0;

 fail_end_trans:
	gfs_trans_end(sdp);

 fail:
	gfs_glock_dq(gh);

	return error;
}

/**
 * glock_compare_atime - Compare two struct gfs_glock structures for gfs_sort()
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
	struct gfs_holder *gh_a = *(struct gfs_holder **)arg_a;
	struct gfs_holder *gh_b = *(struct gfs_holder **)arg_b;
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

	return ret;
}

/**
 * gfs_glock_nq_m_atime - acquire multiple glocks where one may need an
 *      atime update
 * @num_gh: the number of structures
 * @ghs: an array of struct gfs_holder structures
 *
 * Returns: 0 on success (all glocks acquired),
 *          errno on failure (no glocks acquired)
 */

int
gfs_glock_nq_m_atime(unsigned int num_gh, struct gfs_holder *ghs)
{
	struct gfs_holder **p;
	unsigned int x;
	int error = 0;

	if (!num_gh)
		return 0;

	if (num_gh == 1) {
		ghs->gh_flags &= ~(LM_FLAG_TRY | GL_ASYNC);
		if (ghs->gh_flags & GL_ATIME)
			error = gfs_glock_nq_atime(ghs);
		else
			error = gfs_glock_nq(ghs);
		return error;
	}

	p = kmalloc(num_gh * sizeof(struct gfs_holder *), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	for (x = 0; x < num_gh; x++)
		p[x] = &ghs[x];

	gfs_sort(p, num_gh, sizeof(struct gfs_holder *), glock_compare_atime);

	for (x = 0; x < num_gh; x++) {
		p[x]->gh_flags &= ~(LM_FLAG_TRY | GL_ASYNC);

		if (p[x]->gh_flags & GL_ATIME)
			error = gfs_glock_nq_atime(p[x]);
		else
			error = gfs_glock_nq(p[x]);

		if (error) {
			while (x--)
				gfs_glock_dq(p[x]);
			break;
		}
	}

	kfree(p);
	return error;
}

/**
 * gfs_try_toss_vnode - See if we can toss a vnode from memory
 * @ip: the inode
 *
 * Returns:  TRUE if the vnode was tossed
 */

void
gfs_try_toss_vnode(struct gfs_inode *ip)
{
	struct inode *inode;

	inode = gfs_iget(ip, NO_CREATE);
	if (!inode)
		return;

	d_prune_aliases(inode);

	if (ip->i_di.di_type == GFS_FILE_DIR) {
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
}


static int
__gfs_setattr_simple(struct gfs_inode *ip, struct iattr *attr)
{
	struct buffer_head *dibh;
	int error;

	error = gfs_get_inode_buffer(ip, &dibh);
	if (!error) {
		error = inode_setattr(ip->i_vnode, attr);
		gfs_assert_warn(ip->i_sbd, !error);
		gfs_inode_attr_out(ip);

		gfs_trans_add_bh(ip->i_gl, dibh);
		gfs_dinode_out(&ip->i_di, dibh->b_data);
		brelse(dibh);
	}

	return error;
}

/**
 * gfs_setattr_simple -
 * @ip:
 * @attr:
 *
 * Called with a reference on the vnode.
 *
 * Returns: errno
 */

int
gfs_setattr_simple(struct gfs_inode *ip, struct iattr *attr)
{
	int error;

	if (get_transaction)
		return __gfs_setattr_simple(ip, attr);

	/* Trans may require:
	   one dinode block. */

	error = gfs_trans_begin(ip->i_sbd, 1, 0);
	if (error)
		return error;

	error = __gfs_setattr_simple(ip, attr);

	gfs_trans_end(ip->i_sbd);

	return error;
}

/**
 * iah_make_jdata -
 * @gl:
 * @inum:
 *
 */

static void
iah_make_jdata(struct gfs_glock *gl, struct gfs_inum *inum)
{
	struct buffer_head *bh;
	struct gfs_dinode *di;
	uint32_t flags;
	int error;

	error = gfs_dread(gl, inum->no_addr, DIO_START | DIO_WAIT, &bh);

	/* This should only fail if we are already shutdown. */
	if (gfs_assert_withdraw(gl->gl_sbd, !error))
		return;

	di = (struct gfs_dinode *)bh->b_data;

	flags = di->di_flags;
	flags = gfs32_to_cpu(flags) | GFS_DIF_JDATA;
	di->di_flags = cpu_to_gfs32(flags);

	brelse(bh);
}

/**
 * iah_super_update - Write superblock to disk
 * @sdp:  filesystem instance structure
 *
 * Returns: errno
 *
 * Update on-disk superblock, using (modified) data in sdp->sd_sb
 */

static int
iah_super_update(struct gfs_sbd *sdp)
{
	struct gfs_glock *gl;
	struct buffer_head *bh;
	int error;

	error = gfs_glock_get(sdp,
			      GFS_SB_LOCK, &gfs_meta_glops,
			      NO_CREATE, &gl);
	if (gfs_assert_withdraw(sdp, !error && gl)) /* This should already be held. */
		return -EINVAL;

	error = gfs_dread(gl, GFS_SB_ADDR >> sdp->sd_fsb2bb_shift,
			  DIO_START | DIO_WAIT, &bh);
	if (!error) {
		gfs_trans_add_bh(gl, bh);
		gfs_sb_out(&sdp->sd_sb, bh->b_data);
		brelse(bh);
	}

	gfs_glock_put(gl);

	return error;
}

/**
 * inode_alloc_hidden - allocate on-disk inode for a special (hidden) file
 * @sdp:  the filesystem instance structure
 * @inum:  new dinode's block # and formal inode #, to be filled
 *         in by this function.
 *
 * Returns: errno
 *
 * This function is called only very rarely, when the first-to-mount
 * node can't find a pre-existing special file (e.g. license or quota file) that
 * it expects to find.  This should happen only when upgrading from an older
 * version of the filesystem.
 *
 * The @inum must be a member of sdp->sd_sb in order to get updated to on-disk
 * superblock properly.
 */

static int
inode_alloc_hidden(struct gfs_sbd *sdp, struct gfs_inum *inum)
{
	struct gfs_inode *dip = sdp->sd_rooti;
	struct gfs_holder d_gh, i_gh;
	struct gfs_unlinked *ul;
	int error;

	error = gfs_glock_nq_init(dip->i_gl, LM_ST_EXCLUSIVE, 0, &d_gh);
	if (error)
		return error;

	error = dinode_alloc(dip, &ul);
	if (error)
		goto fail;

	inum->no_addr = ul->ul_inum.no_addr;
	pick_formal_ino(sdp, inum);

	/* Don't worry about deadlock ordering here.  We're the first
	   mounter and still under the mount lock (i.e. there is no
	   contention). */

	error = gfs_glock_nq_num(sdp,
				 inum->no_formal_ino, &gfs_inode_glops,
				 LM_ST_EXCLUSIVE, GL_SKIP, &i_gh);
	if (error)
		goto fail_ul;

	gfs_alloc_get(dip);

	error = gfs_quota_hold_m(dip, 0, 0);
	if (error)
		goto fail_al;

	/* Trans may require:
	   The new inode, the superblock,
	   and one block for a quota change and
	   one block for an unlinked tag. */
      
	error = gfs_trans_begin(sdp, 2, 2);
	if (error)
		goto fail_unhold;
	
	error = make_dinode(dip, i_gh.gh_gl, inum, GFS_FILE_REG, 0600, 0, 0);
	if (error)
		goto fail_end_trans;

	/* Hidden files get all of their data (not just metadata) journaled */
	iah_make_jdata(i_gh.gh_gl, inum);

	error = iah_super_update(sdp);
	if (error)
		goto fail_end_trans;

	gfs_trans_add_unlinked(sdp, GFS_LOG_DESC_IDA,
			       &(struct gfs_inum){0, inum->no_addr});
	gfs_trans_add_quota(sdp, +1, 0, 0);
	gfs_trans_add_gl(dip->i_gl);

	gfs_trans_end(sdp);
	gfs_quota_unhold_m(dip);
	gfs_alloc_put(dip);

	gfs_glock_dq_uninit(&i_gh);
	gfs_glock_dq_uninit(&d_gh);

	gfs_unlinked_unlock(sdp, ul);

	gfs_log_flush(sdp);

	return 0;

 fail_end_trans:
	gfs_trans_end(sdp);

 fail_unhold:
	gfs_quota_unhold_m(dip);

 fail_al:
	gfs_alloc_put(dip);
	gfs_glock_dq_uninit(&i_gh);

 fail_ul:
	gfs_unlinked_unlock(sdp, ul);

 fail:
	gfs_glock_dq_uninit(&d_gh);

	return error;
}

/**
 * gfs_alloc_qinode - allocate a quota inode
 * @sdp: The GFS superblock
 *
 * Returns: 0 on success, error code otherwise
 */

int
gfs_alloc_qinode(struct gfs_sbd *sdp)
{
	return inode_alloc_hidden(sdp, &sdp->sd_sb.sb_quota_di);
}

/**
 * gfs_alloc_linode - allocate a license inode
 * @sdp: The GFS superblock
 *
 * Returns: 0 on success, error code otherwise
 */

int
gfs_alloc_linode(struct gfs_sbd *sdp)
{
	return inode_alloc_hidden(sdp, &sdp->sd_sb.sb_license_di);
}
