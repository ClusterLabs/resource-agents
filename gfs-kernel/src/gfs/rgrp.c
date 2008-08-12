#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>

#include "gfs.h"
#include "bits.h"
#include "dio.h"
#include "file.h"
#include "glock.h"
#include "glops.h"
#include "rgrp.h"
#include "super.h"
#include "trans.h"

/**
 * mhc_hash: find the mhc hash bucket for a buffer
 * @bh: the buffer
 *
 * Returns: The bucket number
 */

static unsigned int
mhc_hash(struct buffer_head *bh)
{
	uint64_t blkno;
	unsigned int h;

	blkno = bh->b_blocknr;
	h = gfs_hash(&blkno, sizeof(uint64_t)) & GFS_MHC_HASH_MASK;

	return h;
}

/**
 * mhc_trim - Throw away cached meta-headers, if there are too many of them
 * @sdp:  The filesystem instance
 * @max:  Max # of cached meta-headers allowed to survive
 *
 * Walk filesystem's list of cached meta-headers, in least-recently-used order,
 *   and keep throwing them away until we're under the max threshold. 
 */

static void
mhc_trim(struct gfs_sbd *sdp, unsigned int max)
{
	struct gfs_meta_header_cache *mc;

	for (;;) {
		spin_lock(&sdp->sd_mhc_lock);
		if (list_empty(&sdp->sd_mhc_single)) {
			spin_unlock(&sdp->sd_mhc_lock);
			return;
		} else {
			mc = list_entry(sdp->sd_mhc_single.prev,
					struct gfs_meta_header_cache,
					mc_list_single);
			list_del(&mc->mc_list_hash);
			list_del(&mc->mc_list_single);
			list_del(&mc->mc_list_rgd);
			spin_unlock(&sdp->sd_mhc_lock);

			kmem_cache_free(gfs_mhc_cachep, mc);
			atomic_dec(&sdp->sd_mhc_count);

			if (atomic_read(&sdp->sd_mhc_count) <= max)
				return;
		}
	}
}

/**
 * gfs_mhc_add - add buffer(s) to the cache of metadata headers
 * @rgd: Resource Group in which the buffered block(s) reside
 * @bh: an array of buffer_head pointers
 * @num: the number of bh pointers in the array
 *
 * Increment each meta-header's generation # by 2.
 * Alloc and add each gfs_meta-header_cache to 3 lists/caches:
 *   Filesystem's meta-header cache (hash)
 *   Filesystem's list of cached meta-headers
 *   Resource Group's list of cached meta-headers
 * If we now have too many cached, throw some older ones away
 */

void
gfs_mhc_add(struct gfs_rgrpd *rgd,
	    struct buffer_head **bh, unsigned int num)
{
	struct gfs_sbd *sdp = rgd->rd_sbd;
	unsigned int x;

	for (x = 0; x < num; x++) {
		struct gfs_meta_header_cache *mc;
		struct list_head *head;
		uint64_t gen;

		if (gfs_meta_check(sdp, bh[x]))
			return;

		mc = kmem_cache_alloc(gfs_mhc_cachep, GFP_KERNEL);
		if (!mc)
			return;
		memset(mc, 0, sizeof(struct gfs_meta_header_cache));

		mc->mc_block = bh[x]->b_blocknr;
		memcpy(&mc->mc_mh, bh[x]->b_data,
		       sizeof(struct gfs_meta_header));

		gen = gfs64_to_cpu(mc->mc_mh.mh_generation) + 2;
		mc->mc_mh.mh_generation = cpu_to_gfs64(gen);

		head = &sdp->sd_mhc[mhc_hash(bh[x])];

		spin_lock(&sdp->sd_mhc_lock);
		list_add(&mc->mc_list_hash, head);
		list_add(&mc->mc_list_single, &sdp->sd_mhc_single);
		list_add(&mc->mc_list_rgd, &rgd->rd_mhc);
		spin_unlock(&sdp->sd_mhc_lock);

		atomic_inc(&sdp->sd_mhc_count);
	}

	x = gfs_tune_get(sdp, gt_max_mhc);

	/* If we've got too many cached, throw some older ones away */
	if (atomic_read(&sdp->sd_mhc_count) > x)
		mhc_trim(sdp, x);
}

/**
 * gfs_mhc_fish - Try to fill in a meta buffer with meta-header from the cache
 * @sdp: the filesystem
 * @bh: the buffer to fill in
 *
 * Returns: TRUE if the buffer was cached, FALSE otherwise
 *
 * If buffer is referenced in meta-header cache (search using hash):
 *   Copy the cached meta-header into the buffer (instead of reading from disk).
 *     Note that only the meta-header portion of the buffer will have valid data
 *     (as would be on disk), rest of buffer does *not* reflect disk contents.
 *   Remove cached gfs_meta_header_cache from all cache lists, free its memory.
 */

int
gfs_mhc_fish(struct gfs_sbd *sdp, struct buffer_head *bh)
{
	struct list_head *tmp, *head;
	struct gfs_meta_header_cache *mc;

	head = &sdp->sd_mhc[mhc_hash(bh)];

	spin_lock(&sdp->sd_mhc_lock);

	for (tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		mc = list_entry(tmp, struct gfs_meta_header_cache, mc_list_hash);
		if (mc->mc_block != bh->b_blocknr)
			continue;

		list_del(&mc->mc_list_hash);
		list_del(&mc->mc_list_single);
		list_del(&mc->mc_list_rgd);
		spin_unlock(&sdp->sd_mhc_lock);

		gfs_prep_new_buffer(bh);
		memcpy(bh->b_data, &mc->mc_mh,
		       sizeof(struct gfs_meta_header));

		kmem_cache_free(gfs_mhc_cachep, mc);
		atomic_dec(&sdp->sd_mhc_count);

		return TRUE;
	}

	spin_unlock(&sdp->sd_mhc_lock);

	return FALSE;
}

/**
 * gfs_mhc_zap - Throw away an RG's list of cached metadata headers
 * @rgd: The resource group whose list we want to clear
 *
 * Simply throw away all cached metadata headers on RG's list,
 *   and free their memory.
 */

void
gfs_mhc_zap(struct gfs_rgrpd *rgd)
{
	struct gfs_sbd *sdp = rgd->rd_sbd;
	struct gfs_meta_header_cache *mc;

	spin_lock(&sdp->sd_mhc_lock);

	while (!list_empty(&rgd->rd_mhc)) {
		mc = list_entry(rgd->rd_mhc.next,
				struct gfs_meta_header_cache,
				mc_list_rgd);

		list_del(&mc->mc_list_hash);
		list_del(&mc->mc_list_single);
		list_del(&mc->mc_list_rgd);
		spin_unlock(&sdp->sd_mhc_lock);

		kmem_cache_free(gfs_mhc_cachep, mc);
		atomic_dec(&sdp->sd_mhc_count);

		spin_lock(&sdp->sd_mhc_lock);
	}

	spin_unlock(&sdp->sd_mhc_lock);
}

/**
 * depend_hash() - Turn glock number into hash bucket number
 * @formal_ino:
 *
 * Returns: The number of the corresponding hash bucket
 */

static unsigned int
depend_hash(uint64_t formal_ino)
{
	unsigned int h;

	h = gfs_hash(&formal_ino, sizeof(uint64_t));
	h &= GFS_DEPEND_HASH_MASK;

	return h;
}

/**
 * depend_sync_one - Sync metadata (not data) for a dependency inode
 * @sdp: filesystem instance
 * @gd: dependency descriptor
 *
 * Remove dependency from superblock's hash table and rgrp's list.
 * Sync dependency inode's metadata to log and in-place location.
 */

static void
depend_sync_one(struct gfs_sbd *sdp, struct gfs_depend *gd)
{
	struct gfs_glock *gl;

	spin_lock(&sdp->sd_depend_lock);
	list_del(&gd->gd_list_hash);
	spin_unlock(&sdp->sd_depend_lock);
	list_del(&gd->gd_list_rgd);

	gl = gfs_glock_find(sdp,
			    &(struct lm_lockname){gd->gd_formal_ino,
						  LM_TYPE_INODE});
	if (gl) {
		if (gl->gl_ops->go_sync)
			gl->gl_ops->go_sync(gl,
					    DIO_METADATA |
					    DIO_INVISIBLE);
		gfs_glock_put(gl);
	}

	kfree(gd);
	atomic_dec(&sdp->sd_depend_count);
}

/**
 * depend_sync_old - Sync older rgrp-dependent inodes to disk.
 * @rgd: Resource group containing dependent inodes
 *
 * Look at oldest entries in resource group's dependency list,
 *   sync 'em if they're older than timeout threshold.
 */

static void
depend_sync_old(struct gfs_rgrpd *rgd)
{
	struct gfs_sbd *sdp = rgd->rd_sbd;
	struct gfs_depend *gd;

	while (!list_empty(&rgd->rd_depend)) {
		/* Oldest entries are in prev direction */
		gd = list_entry(rgd->rd_depend.prev,
				struct gfs_depend,
				gd_list_rgd);

		if (time_before(jiffies,
				gd->gd_time +
				gfs_tune_get(sdp, gt_depend_secs) * HZ))
			return;

		depend_sync_one(sdp, gd);
	}
}

/**
 * gfs_depend_add - Add a dependent inode to rgrp's and filesystem's list
 * @rgd: Resource group containing blocks associated with inode
 * @formal_ino: inode
 *
 * Dependent inodes must be flushed to log and in-place blocks before
 *   releasing an EXCLUSIVE rgrp lock.
 * Find pre-existing dependency for this inode/rgrp combination in
 *   incore superblock struct's sd_depend hash table, or create a new one.
 * Either way, move or attach dependency to head of superblock's hash bucket
 *   and top of rgrp's list.
 * If we create a new one, take a moment to sync older dependencies to disk.
 */

void
gfs_depend_add(struct gfs_rgrpd *rgd, uint64_t formal_ino)
{
	struct gfs_sbd *sdp = rgd->rd_sbd;
	struct list_head *head, *tmp;
	struct gfs_depend *gd;

	head = &sdp->sd_depend[depend_hash(formal_ino)];

	spin_lock(&sdp->sd_depend_lock);

	for (tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		gd = list_entry(tmp, struct gfs_depend, gd_list_hash);
		if (gd->gd_rgd == rgd &&
		    gd->gd_formal_ino == formal_ino) {
			list_move(&gd->gd_list_hash, head);
			spin_unlock(&sdp->sd_depend_lock);
			list_move(&gd->gd_list_rgd, &rgd->rd_depend);
			gd->gd_time = jiffies;
			return;
		}
	}

	spin_unlock(&sdp->sd_depend_lock);

	gd = gmalloc(sizeof(struct gfs_depend));
	memset(gd, 0, sizeof(struct gfs_depend));

	gd->gd_rgd = rgd;
	gd->gd_formal_ino = formal_ino;
	gd->gd_time = jiffies;

	spin_lock(&sdp->sd_depend_lock);
	list_add(&gd->gd_list_hash, head);
	spin_unlock(&sdp->sd_depend_lock);
	list_add(&gd->gd_list_rgd, &rgd->rd_depend);

	atomic_inc(&sdp->sd_depend_count);

	depend_sync_old(rgd);
}

/**
 * gfs_depend_sync - Sync metadata (not data) for an rgrp's dependent inodes
 * @rgd: Resource group containing the dependent inodes
 *
 * As long as this node owns an EXCLUSIVE lock on the rgrp, we can keep
 *   rgrp's modified metadata blocks in buffer cache.
 *
 * When this node releases the EX lock, we must flush metadata, so other
 *   nodes can read the modified content from disk.
 */

void
gfs_depend_sync(struct gfs_rgrpd *rgd)
{
	struct gfs_sbd *sdp = rgd->rd_sbd;
	struct gfs_depend *gd;

	while (!list_empty(&rgd->rd_depend)) {
		gd = list_entry(rgd->rd_depend.next,
				struct gfs_depend,
				gd_list_rgd);
		depend_sync_one(sdp, gd);
	}
}

/**
 * rgrp_verify - Verify that a resource group is consistent
 * @sdp: the filesystem
 * @rgd: the rgrp
 *
 * Somebody should have already called gfs_glock_rg() on this RG.
 */

static void
rgrp_verify(struct gfs_rgrpd *rgd)
{
	struct gfs_sbd *sdp = rgd->rd_sbd;
	struct gfs_bitmap *bits = NULL;
	uint32_t length = rgd->rd_ri.ri_length;
	uint32_t count[4], tmp;
	int buf, x;

	memset(count, 0, 4 * sizeof(uint32_t));

	/* Count # blocks in each of 4 possible allocation states */
	for (buf = 0; buf < length; buf++) {
		bits = &rgd->rd_bits[buf];
		for (x = 0; x < 4; x++)
			count[x] += gfs_bitcount(rgd,
						 rgd->rd_bh[buf]->b_data +
						 bits->bi_offset,
						 bits->bi_len, x);
	}

	if (count[0] != rgd->rd_rg.rg_free) {
		if (gfs_consist_rgrpd(rgd))
			printk("GFS: fsid=%s: free data mismatch:  %u != %u\n",
			       sdp->sd_fsname, count[0], rgd->rd_rg.rg_free);
		return;
	}

	tmp = rgd->rd_ri.ri_data -
		(rgd->rd_rg.rg_usedmeta + rgd->rd_rg.rg_freemeta) -
		(rgd->rd_rg.rg_useddi + rgd->rd_rg.rg_freedi) -
		rgd->rd_rg.rg_free;
	if (count[1] != tmp) {
		if (gfs_consist_rgrpd(rgd))
			printk("GFS: fsid=%s: used data mismatch:  %u != %u\n",
			       sdp->sd_fsname, count[1], tmp);
		return;
	}

	if (count[2] != rgd->rd_rg.rg_freemeta) {
		if (gfs_consist_rgrpd(rgd))
			printk("GFS: fsid=%s: free metadata mismatch:  %u != %u\n",
			       sdp->sd_fsname, count[2], rgd->rd_rg.rg_freemeta);
		return;
	}

	tmp = rgd->rd_rg.rg_usedmeta +
		(rgd->rd_rg.rg_useddi + rgd->rd_rg.rg_freedi);
	if (count[3] != tmp) {
		if (gfs_consist_rgrpd(rgd))
			printk("GFS: fsid=%s: used metadata mismatch:  %u != %u\n",
			       sdp->sd_fsname, count[3], tmp);
		return;
	}
}

/**
 * gfs_blk2rgrpd - Find resource group for a given data/meta block number
 * @sdp: The GFS superblock
 * @n: The data block number
 *
 * Returns: The resource group, or NULL if not found
 *
 * Don't try to use this for non-allocatable block numbers (i.e. rgrp header
 *   or bitmap blocks); it's for allocatable (data/meta) blocks only.
 */

struct gfs_rgrpd *
gfs_blk2rgrpd(struct gfs_sbd *sdp, uint64_t blk)
{
	struct list_head *tmp, *head;
	struct gfs_rgrpd *rgd = NULL;
	struct gfs_rindex *ri;

	spin_lock(&sdp->sd_rg_mru_lock);

	for (head = &sdp->sd_rg_mru_list, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		rgd = list_entry(tmp, struct gfs_rgrpd, rd_list_mru);
		ri = &rgd->rd_ri;

		if (ri->ri_data1 <= blk && blk < ri->ri_data1 + ri->ri_data) {
			list_move(&rgd->rd_list_mru, &sdp->sd_rg_mru_list);
			spin_unlock(&sdp->sd_rg_mru_lock);
			return rgd;
		}
	}

	spin_unlock(&sdp->sd_rg_mru_lock);

	return NULL;
}

/**
 * gfs_rgrpd_get_first - get the first Resource Group in the filesystem
 * @sdp: The GFS superblock
 *
 * Returns: The first rgrp in the filesystem
 */

struct gfs_rgrpd *
gfs_rgrpd_get_first(struct gfs_sbd *sdp)
{
	gfs_assert(sdp, !list_empty(&sdp->sd_rglist),);
	return list_entry(sdp->sd_rglist.next, struct gfs_rgrpd, rd_list);
}

/**
 * gfs_rgrpd_get_next - get the next RG
 * @rgd: A RG
 *
 * Returns: The next rgrp
 */

struct gfs_rgrpd *
gfs_rgrpd_get_next(struct gfs_rgrpd *rgd)
{
	if (rgd->rd_list.next == &rgd->rd_sbd->sd_rglist)
		return NULL;
	return list_entry(rgd->rd_list.next, struct gfs_rgrpd, rd_list);
}

/**
 * clear_rgrpdi - Clear up rgrps
 * @sdp: The GFS superblock
 *
 */

void
clear_rgrpdi(struct gfs_sbd *sdp)
{
	struct gfs_rgrpd *rgd;
	struct gfs_glock *gl;

	spin_lock(&sdp->sd_rg_forward_lock);
	sdp->sd_rg_forward = NULL;
	spin_unlock(&sdp->sd_rg_forward_lock);

	spin_lock(&sdp->sd_rg_recent_lock);
	while (!list_empty(&sdp->sd_rg_recent)) {
		rgd = list_entry(sdp->sd_rg_recent.next,
				 struct gfs_rgrpd, rd_recent);
		list_del(&rgd->rd_recent);
	}
	spin_unlock(&sdp->sd_rg_recent_lock);

	while (!list_empty(&sdp->sd_rglist)) {
		rgd = list_entry(sdp->sd_rglist.next,
				 struct gfs_rgrpd, rd_list);
		gl = rgd->rd_gl;

		list_del(&rgd->rd_list);
		list_del(&rgd->rd_list_mru);

		if (gl) {
			gfs_glock_force_drop(gl);
			if (atomic_read(&gl->gl_lvb_count))
				gfs_lvb_unhold(gl);
			set_gl2rgd(gl, NULL);
			gfs_glock_put(gl);
		}

		if (rgd->rd_bits)
			kfree(rgd->rd_bits);
		if (rgd->rd_bh)
			kfree(rgd->rd_bh);

		kfree(rgd);
	}
}

/**
 * gfs_clear_rgrpd - Clear up rgrps
 * @sdp: The GFS superblock
 *
 */

void
gfs_clear_rgrpd(struct gfs_sbd *sdp)
{
	down(&sdp->sd_rindex_lock);
	clear_rgrpdi(sdp);
	up(&sdp->sd_rindex_lock);
}

/**
 * gfs_compute_bitstructs - Compute the bitmap sizes
 * @rgd: The resource group descriptor
 *
 * Calculates bitmap descriptors, one for each block that contains bitmap data
 *
 * Returns: errno
 */

static int
compute_bitstructs(struct gfs_rgrpd *rgd)
{
	struct gfs_sbd *sdp = rgd->rd_sbd;
	struct gfs_bitmap *bits;
	uint32_t length = rgd->rd_ri.ri_length; /* # blocks in hdr & bitmap */
	uint32_t bytes_left, bytes;
	int x;

	rgd->rd_bits = kmalloc(length * sizeof(struct gfs_bitmap), GFP_KERNEL);
	if (!rgd->rd_bits)
		return -ENOMEM;
	memset(rgd->rd_bits, 0, length * sizeof(struct gfs_bitmap));

	bytes_left = rgd->rd_ri.ri_bitbytes;

	for (x = 0; x < length; x++) {
		bits = &rgd->rd_bits[x];

		/* small rgrp; bitmap stored completely in header block */
		if (length == 1) {
			bytes = bytes_left;
			bits->bi_offset = sizeof(struct gfs_rgrp);
			bits->bi_start = 0;
			bits->bi_len = bytes;
		/* header block */
		} else if (x == 0) {
			bytes = sdp->sd_sb.sb_bsize - sizeof(struct gfs_rgrp);
			bits->bi_offset = sizeof(struct gfs_rgrp);
			bits->bi_start = 0;
			bits->bi_len = bytes;
		/* last block */
		} else if (x + 1 == length) {
			bytes = bytes_left;
			bits->bi_offset = sizeof(struct gfs_meta_header);
			bits->bi_start = rgd->rd_ri.ri_bitbytes - bytes_left;
			bits->bi_len = bytes;
		/* other blocks */
		} else {
			bytes = sdp->sd_sb.sb_bsize - sizeof(struct gfs_meta_header);
			bits->bi_offset = sizeof(struct gfs_meta_header);
			bits->bi_start = rgd->rd_ri.ri_bitbytes - bytes_left;
			bits->bi_len = bytes;
		}

		bytes_left -= bytes;
	}

	if (bytes_left) {
		gfs_consist_rgrpd(rgd);
		return -EIO;
	}
        if ((rgd->rd_bits[length - 1].bi_start +
	     rgd->rd_bits[length - 1].bi_len) * GFS_NBBY !=
	    rgd->rd_ri.ri_data) {
		if (gfs_consist_rgrpd(rgd)) {
			gfs_rindex_print(&rgd->rd_ri);
			printk("GFS: fsid=%s: start=%u len=%u offset=%u\n",
			       sdp->sd_fsname,
			       rgd->rd_bits[length - 1].bi_start,
			       rgd->rd_bits[length - 1].bi_len,
			       rgd->rd_bits[length - 1].bi_offset);
		}
		return -EIO;
	}

	rgd->rd_bh = kmalloc(length * sizeof(struct buffer_head *), GFP_KERNEL);
	if (!rgd->rd_bh) {
		kfree(rgd->rd_bits);
		rgd->rd_bits = NULL;
		return -ENOMEM;
	}
	memset(rgd->rd_bh, 0, length * sizeof(struct buffer_head *));

	return 0;
}

/**
 * gfs_ri_update - Pull in a new resource index from the disk
 * @gl: The glock covering the rindex inode
 *
 * Returns: 0 on successful update, error code otherwise
 */

static int
gfs_ri_update(struct gfs_inode *ip)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_rgrpd *rgd;
	char buf[sizeof(struct gfs_rindex)];
	int error;

	if (do_mod(ip->i_di.di_size, sizeof(struct gfs_rindex))) {
		gfs_consist_inode(ip);
		return -EIO;
	}

	clear_rgrpdi(sdp);

	for (sdp->sd_rgcount = 0;; sdp->sd_rgcount++) {
		error = gfs_internal_read(ip, buf,
					  sdp->sd_rgcount *
					  sizeof(struct gfs_rindex),
					  sizeof(struct gfs_rindex));
		if (!error)
			break;
		if (error != sizeof(struct gfs_rindex)) {
			if (error > 0)
				error = -EIO;
			goto fail;
		}

		rgd = kmalloc(sizeof(struct gfs_rgrpd), GFP_KERNEL);
		error = -ENOMEM;
		if (!rgd)
			goto fail;
		memset(rgd, 0, sizeof(struct gfs_rgrpd));

		INIT_LIST_HEAD(&rgd->rd_mhc);
		INIT_LIST_HEAD(&rgd->rd_depend);
		rgd->rd_sbd = sdp;

		list_add_tail(&rgd->rd_list, &sdp->sd_rglist);
		list_add_tail(&rgd->rd_list_mru, &sdp->sd_rg_mru_list);

		gfs_rindex_in(&rgd->rd_ri, buf);

		error = compute_bitstructs(rgd);
		if (error)
			goto fail;

		error = gfs_glock_get(sdp, rgd->rd_ri.ri_addr, &gfs_rgrp_glops,
				      CREATE, &rgd->rd_gl);
		if (error)
			goto fail;

		error = gfs_lvb_hold(rgd->rd_gl);
		if (error)
			goto fail;

		set_gl2rgd(rgd->rd_gl, rgd);
		rgd->rd_rg_vn = rgd->rd_gl->gl_vn - 1;
	}

	sdp->sd_riinode_vn = ip->i_gl->gl_vn;

	return 0;

 fail:
	clear_rgrpdi(sdp);

	return error;
}

/**
 * gfs_rindex_hold - Grab a lock on the rindex
 * @sdp: The GFS superblock
 * @ri_gh: the glock holder
 *
 * We grab a lock on the rindex inode to make sure that it doesn't
 * change whilst we are performing an operation. We keep this lock
 * for quite long periods of time compared to other locks. This
 * doesn't matter, since it is shared and it is very, very rarely
 * accessed in the exclusive mode (i.e. only when expanding the filesystem).
 *
 * This makes sure that we're using the latest copy of the resource index
 *   special file, which might have been updated if someone expanded the
 *   filesystem (via gfs_grow utility), which adds new resource groups.
 *
 * Returns: 0 on success, error code otherwise
 */

int
gfs_rindex_hold(struct gfs_sbd *sdp, struct gfs_holder *ri_gh)
{
	struct gfs_inode *ip = sdp->sd_riinode;
	struct gfs_glock *gl = ip->i_gl;
	int error;

	error = gfs_glock_nq_init(gl, LM_ST_SHARED, 0, ri_gh);
	if (error)
		return error;

	/* Read new copy from disk if we don't have the latest */
	if (sdp->sd_riinode_vn != gl->gl_vn) {
		down(&sdp->sd_rindex_lock);
		if (sdp->sd_riinode_vn != gl->gl_vn) {
			error = gfs_ri_update(ip);
			if (error)
				gfs_glock_dq_uninit(ri_gh);
		}
		up(&sdp->sd_rindex_lock);
	}

	return error;
}

/**
 * gfs_rgrp_read - Read in a RG's header and bitmaps
 * @rgd: the struct gfs_rgrpd describing the RG to read in
 *
 * Read in all of a Resource Group's header and bitmap blocks.
 * Caller must eventually call gfs_rgrp_relse() to free the bitmaps.
 *
 * Returns: errno
 */

int
gfs_rgrp_read(struct gfs_rgrpd *rgd)
{
	struct gfs_sbd *sdp = rgd->rd_sbd;
	struct gfs_glock *gl = rgd->rd_gl;
	unsigned int x, length = rgd->rd_ri.ri_length;
	int error;

	for (x = 0; x < length; x++) {
		gfs_assert_warn(sdp, !rgd->rd_bh[x]);
		rgd->rd_bh[x] = gfs_dgetblk(gl, rgd->rd_ri.ri_addr + x);
	}

	for (x = 0; x < length; x++) {
		error = gfs_dreread(sdp, rgd->rd_bh[x], DIO_START);
		if (error)
			goto fail;
	}

	for (x = length; x--;) {
		error = gfs_dreread(sdp, rgd->rd_bh[x], DIO_WAIT);
		if (error)
			goto fail;
		if (gfs_metatype_check(sdp, rgd->rd_bh[x],
				       (x) ? GFS_METATYPE_RB : GFS_METATYPE_RG)) {
			error = -EIO;
			goto fail;
		}
	}

	if (rgd->rd_rg_vn != gl->gl_vn) {
		gfs_rgrp_in(&rgd->rd_rg, (rgd->rd_bh[0])->b_data);
		rgd->rd_rg_vn = gl->gl_vn;
	}

	return 0;

 fail:
	for (x = 0; x < length; x++) {
		brelse(rgd->rd_bh[x]);
		rgd->rd_bh[x] = NULL;
	}

	return error;
}

/**
 * gfs_rgrp_relse - Release RG bitmaps read in with gfs_rgrp_read()
 * @rgd: the struct gfs_rgrpd describing the RG to read in
 *
 */

void
gfs_rgrp_relse(struct gfs_rgrpd *rgd)
{
	int x, length = rgd->rd_ri.ri_length;

	for (x = 0; x < length; x++) {
		brelse(rgd->rd_bh[x]);
		rgd->rd_bh[x] = NULL;
	}
}

/**
 * gfs_rgrp_lvb_fill - copy RG usage data out of the struct gfs_rgrp into the struct gfs_rgrp_lvb
 * @rgd: the resource group data structure
 *
 */

void
gfs_rgrp_lvb_fill(struct gfs_rgrpd *rgd)
{
	struct gfs_rgrp *rg = &rgd->rd_rg;
	struct gfs_rgrp_lvb *rb = (struct gfs_rgrp_lvb *)rgd->rd_gl->gl_lvb;

	rb->rb_magic = cpu_to_gfs32(GFS_MAGIC);
	rb->rb_free = cpu_to_gfs32(rg->rg_free);
	rb->rb_useddi = cpu_to_gfs32(rg->rg_useddi);
	rb->rb_freedi = cpu_to_gfs32(rg->rg_freedi);
	rb->rb_usedmeta = cpu_to_gfs32(rg->rg_usedmeta);
	rb->rb_freemeta = cpu_to_gfs32(rg->rg_freemeta);
}

/**
 * gfs_rgrp_lvb_init - Init the data of a RG LVB
 * @rgd: the resource group data structure
 *
 * Returns:  errno
 */

int
gfs_rgrp_lvb_init(struct gfs_rgrpd *rgd)
{
	struct gfs_glock *gl = rgd->rd_gl;
	struct gfs_holder rgd_gh;
	int error;

	error = gfs_glock_nq_init(gl, LM_ST_EXCLUSIVE, 0, &rgd_gh);
	if (!error) {
		gfs_rgrp_lvb_fill(rgd);
		gfs_glock_dq_uninit(&rgd_gh);
	}

	return error;
}

/**
 * gfs_alloc_get - allocate a struct gfs_alloc structure for an inode
 * @ip: the incore GFS inode structure
 *
 * Alloc and zero an in-place reservation structure,
 *   and attach it to the GFS incore inode.
 *
 * FIXME: Don't use gmalloc()
 *
 * Returns: the struct gfs_alloc
 */

struct gfs_alloc *
gfs_alloc_get(struct gfs_inode *ip)
{
	struct gfs_alloc *al = ip->i_alloc;

	gfs_assert_warn(ip->i_sbd, !al);

	al = gmalloc(sizeof(struct gfs_alloc));
	memset(al, 0, sizeof(struct gfs_alloc));

	ip->i_alloc = al;

	return al;
}

/**
 * gfs_alloc_put - throw away the struct gfs_alloc for an inode
 * @ip: the inode
 *
 */

void
gfs_alloc_put(struct gfs_inode *ip)
{
	struct gfs_alloc *al = ip->i_alloc;

	if (gfs_assert_warn(ip->i_sbd, al))
		return;

	ip->i_alloc = NULL;
	kfree(al);
}

/**
 * try_rgrp_fit - See if a given reservation will fit in a given RG
 * @rgd: the RG data
 * @al: the struct gfs_alloc structure describing the reservation
 *
 * If there's room for the requested blocks to be allocated from the RG:
 *   Sets the $al_reserved_data field in @al.
 *   Sets the $al_reserved_meta field in @al.
 *   Sets the $al_rgd field in @al.
 *
 * Returns: 1 on success (it fits), 0 on failure (it doesn't fit)
 */

static int
try_rgrp_fit(struct gfs_rgrpd *rgd, struct gfs_alloc *al)
{
	uint32_t freeblks = rgd->rd_rg.rg_free;
	uint32_t freemeta = rgd->rd_rg.rg_freemeta;
	uint32_t metares = al->al_requested_meta;
	uint32_t datares = al->al_requested_data;

	/* First take care of the data blocks required */

	if (freeblks < al->al_requested_data)
		return 0;

	freeblks -= al->al_requested_data;

	/* Then take care of the dinodes */

	metares += al->al_requested_di;

	/* Then take care of the metadata blocks */

	while (freemeta < metares) {
		if (freeblks < GFS_META_CLUMP)
			return 0;

		freeblks -= GFS_META_CLUMP;
		freemeta += GFS_META_CLUMP;

		datares += GFS_META_CLUMP;
	}

	al->al_rgd = rgd;
	al->al_reserved_meta = metares;
	al->al_reserved_data = datares;

	return 1;
}

/**
 * recent_rgrp_first - get first RG from "recent" list
 * @sdp: The GFS superblock
 * @rglast: address of the rgrp used last
 *
 * Returns: The first rgrp in the recent list
 */

static struct gfs_rgrpd *
recent_rgrp_first(struct gfs_sbd *sdp, uint64_t rglast)
{
	struct list_head *tmp, *head;
	struct gfs_rgrpd *rgd = NULL;

	spin_lock(&sdp->sd_rg_recent_lock);

	if (list_empty(&sdp->sd_rg_recent))
		goto out;

	if (!rglast)
		goto first;

	for (head = &sdp->sd_rg_recent, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		rgd = list_entry(tmp, struct gfs_rgrpd, rd_recent);
		if (rgd->rd_ri.ri_addr == rglast)
			goto out;
	}

 first:
	rgd = list_entry(sdp->sd_rg_recent.next, struct gfs_rgrpd, rd_recent);

 out:
	spin_unlock(&sdp->sd_rg_recent_lock);

	return rgd;
}

/**
 * recent_rgrp_next - get next RG from "recent" list
 * @cur_rgd: current rgrp
 * @remove:
 *
 * Returns: The next rgrp in the recent list
 */

static struct gfs_rgrpd *
recent_rgrp_next(struct gfs_rgrpd *cur_rgd, int remove)
{
	struct gfs_sbd *sdp = cur_rgd->rd_sbd;
	struct list_head *tmp, *head;
	struct gfs_rgrpd *rgd;

	spin_lock(&sdp->sd_rg_recent_lock);

	for (head = &sdp->sd_rg_recent, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		rgd = list_entry(tmp, struct gfs_rgrpd, rd_recent);
		if (rgd == cur_rgd) {
			if (cur_rgd->rd_recent.next != head)
				rgd = list_entry(cur_rgd->rd_recent.next,
						 struct gfs_rgrpd, rd_recent);
			else
				rgd = NULL;

			if (remove)
				list_del(&cur_rgd->rd_recent);

			goto out;
		}
	}

	rgd = NULL;
	if (!list_empty(head))
		rgd = list_entry(head->next, struct gfs_rgrpd, rd_recent);

 out:
	spin_unlock(&sdp->sd_rg_recent_lock);

	return rgd;
}

/**
 * recent_rgrp_add - add an RG to tail of "recent" list
 * @new_rgd: The rgrp to add
 *
 * Before adding, make sure that:
 *   1) it's not already on the list
 *   2) there's still room for more entries
 * The capacity limit imposed on the "recent" list is basically a node's "share"
 *   of rgrps within a cluster, i.e. (total # rgrps) / (# nodes (journals))
 */

static void
recent_rgrp_add(struct gfs_rgrpd *new_rgd)
{
	struct gfs_sbd *sdp = new_rgd->rd_sbd;
	struct list_head *tmp, *head;
	struct gfs_rgrpd *rgd = NULL;
	unsigned int count = 0;
	unsigned int max = sdp->sd_rgcount / gfs_num_journals(sdp);

	spin_lock(&sdp->sd_rg_recent_lock);

	for (head = &sdp->sd_rg_recent, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		rgd = list_entry(tmp, struct gfs_rgrpd, rd_recent);
		if (rgd == new_rgd)
			goto out;

		if (++count >= max)
			goto out;
	}
	new_rgd->rd_try_counter = 0;
	list_add_tail(&new_rgd->rd_recent, &sdp->sd_rg_recent);

 out:
	spin_unlock(&sdp->sd_rg_recent_lock);
}

/**
 * forward_rgrp_get - get an rgrp to try next from full list
 * @sdp: The GFS superblock
 *
 * Returns: The rgrp to try next
 */

static struct gfs_rgrpd *
forward_rgrp_get(struct gfs_sbd *sdp)
{
	struct gfs_rgrpd *rgd;
	unsigned int journals = gfs_num_journals(sdp);
	unsigned int rg = 0, x;

	spin_lock(&sdp->sd_rg_forward_lock);

	rgd = sdp->sd_rg_forward;
	if (!rgd) {
		if (sdp->sd_rgcount >= journals)
			rg = sdp->sd_rgcount *
				sdp->sd_lockstruct.ls_jid /
				journals;

		for (x = 0, rgd = gfs_rgrpd_get_first(sdp);
		     x < rg;
		     x++, rgd = gfs_rgrpd_get_next(rgd))
			/* Do Nothing */;

		sdp->sd_rg_forward = rgd;
	}

	spin_unlock(&sdp->sd_rg_forward_lock);

	return rgd;
}

/**
 * forward_rgrp_set - set the forward rgrp pointer
 * @sdp: the filesystem
 * @rgd: The new forward rgrp
 *
 */

static void
forward_rgrp_set(struct gfs_sbd *sdp, struct gfs_rgrpd *rgd)
{
	spin_lock(&sdp->sd_rg_forward_lock);
	sdp->sd_rg_forward = rgd;
	spin_unlock(&sdp->sd_rg_forward_lock);
}

/**
 * get_local_rgrp - Choose and lock a rgrp for allocation
 * @ip: the inode to reserve space for
 * @rgp: the chosen and locked rgrp
 *
 * Try to acquire rgrp in way which avoids contending with others.
 *
 * Returns: errno
 */

static int
get_local_rgrp(struct gfs_inode *ip)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_rgrpd *rgd, *begin = NULL;
	struct gfs_alloc *al = ip->i_alloc;
	int flags = LM_FLAG_TRY;
	int skipped = 0;
	int loops = 0;
	int error;
	int try_flag;
	unsigned int try_threshold = gfs_tune_get(sdp, gt_rgrp_try_threshold);

	/* Try recently successful rgrps */

	rgd = recent_rgrp_first(sdp, ip->i_last_rg_alloc);

	while (rgd) {
		try_flag = (rgd->rd_try_counter >= try_threshold) ?
			0: LM_FLAG_TRY;
		error = gfs_glock_nq_init(rgd->rd_gl,
					  LM_ST_EXCLUSIVE, try_flag,
					  &al->al_rgd_gh);
		switch (error) {
		case 0:
			if (try_rgrp_fit(rgd, al)) {
				rgd->rd_try_counter = 0;
				goto out;
			}
			gfs_glock_dq_uninit(&al->al_rgd_gh);
			rgd = recent_rgrp_next(rgd, TRUE);
			break;

		case GLR_TRYFAILED:
			rgd->rd_try_counter++;
			rgd = recent_rgrp_next(rgd, FALSE);
			break;

		default:
			return error;
		}
	}

	/* Go through full list of rgrps */

	begin = rgd = forward_rgrp_get(sdp);

	for (;;) {
		error = gfs_glock_nq_init(rgd->rd_gl,
					  LM_ST_EXCLUSIVE, flags,
					  &al->al_rgd_gh);
		switch (error) {
		case 0:
			if (try_rgrp_fit(rgd, al))
				goto out;
			gfs_glock_dq_uninit(&al->al_rgd_gh);
			break;

		case GLR_TRYFAILED:
			skipped++;
			break;

		default:
			return error;
		}

		rgd = gfs_rgrpd_get_next(rgd);
		if (!rgd)
			rgd = gfs_rgrpd_get_first(sdp);

		if (rgd == begin) {
			if (++loops >= 2 || !skipped)
				return -ENOSPC;
			flags = 0;
		}
	}

 out:
	ip->i_last_rg_alloc = rgd->rd_ri.ri_addr;

	if (begin) {
		recent_rgrp_add(rgd);
		rgd = gfs_rgrpd_get_next(rgd);
		if (!rgd)
			rgd = gfs_rgrpd_get_first(sdp);
		forward_rgrp_set(sdp, rgd);
	}

	return 0;
}

/**
 * gfs_inplace_reserve_i - Reserve space in the filesystem
 * @ip: the inode to reserve space for
 *
 * Acquire resource group locks to allow for the maximum allocation
 * described by "res".
 *
 * This should probably become more complex again, but for now, let's go
 * for simple (one resource group) reservations.
 *
 * Returns: errno
 */

int
gfs_inplace_reserve_i(struct gfs_inode *ip,
		     char *file, unsigned int line)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_alloc *al = ip->i_alloc;
	int error;

        if (gfs_assert_warn(sdp,
			    al->al_requested_di ||
			    al->al_requested_data ||
			    al->al_requested_meta))
		return -EINVAL;

	error = gfs_rindex_hold(sdp, &al->al_ri_gh);
	if (error)
		return error;

	error = get_local_rgrp(ip);
	if (error) {
		gfs_glock_dq_uninit(&al->al_ri_gh);
		return error;
	}

	gfs_depend_sync(al->al_rgd);

	al->al_file = file;
	al->al_line = line;

	return 0;
}

/**
 * gfs_inplace_release - release an inplace reservation
 * @ip: the inode the reservation was taken out on
 *
 * Release a reservation made by gfs_inplace_reserve().
 */

void
gfs_inplace_release(struct gfs_inode *ip)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_alloc *al = ip->i_alloc;

	if (gfs_assert_warn(sdp, al->al_alloced_di <= al->al_requested_di) == -1)
		printk("GFS: fsid=%s: al_alloced_di = %u, al_requested_di = %u\n"
		       "GFS: fsid=%s: al_file = %s, al_line = %u\n",
		       sdp->sd_fsname, al->al_alloced_di, al->al_requested_di,
		       sdp->sd_fsname, al->al_file, al->al_line);
	if (gfs_assert_warn(sdp, al->al_alloced_meta <= al->al_reserved_meta) == -1)
		printk("GFS: fsid=%s: al_alloced_meta = %u, al_reserved_meta = %u\n"
		       "GFS: fsid=%s: al_file = %s, al_line = %u\n",
		       sdp->sd_fsname, al->al_alloced_meta, al->al_reserved_meta,
		       sdp->sd_fsname, al->al_file, al->al_line);
	if (gfs_assert_warn(sdp, al->al_alloced_data <= al->al_reserved_data) == -1)
		printk("GFS: fsid=%s: al_alloced_data = %u, al_reserved_data = %u\n"
		       "GFS: fsid=%s: al_file = %s, al_line = %u\n",
		       sdp->sd_fsname, al->al_alloced_data, al->al_reserved_data,
		       sdp->sd_fsname, al->al_file, al->al_line);

	al->al_rgd = NULL;
	gfs_glock_dq_uninit(&al->al_rgd_gh);
	gfs_glock_dq_uninit(&al->al_ri_gh);
}

/**
 * gfs_get_block_type - Check a block in a RG is of given type
 * @rgd: the resource group holding the block
 * @block: the block number
 *
 * Returns: The block type (GFS_BLKST_*)
 */

unsigned char
gfs_get_block_type(struct gfs_rgrpd *rgd, uint64_t block)
{
	struct gfs_bitmap *bits = NULL;
	uint32_t length, rgrp_block, buf_block;
	unsigned int buf;
	unsigned char type;

	length = rgd->rd_ri.ri_length;
	rgrp_block = block - rgd->rd_ri.ri_data1;

	for (buf = 0; buf < length; buf++) {
		bits = &rgd->rd_bits[buf];
		if (rgrp_block < (bits->bi_start + bits->bi_len) * GFS_NBBY)
			break;
	}

	gfs_assert(rgd->rd_sbd, buf < length,);
	buf_block = rgrp_block - bits->bi_start * GFS_NBBY;

	type = gfs_testbit(rgd,
			   rgd->rd_bh[buf]->b_data + bits->bi_offset,
			   bits->bi_len, buf_block);

	return type;
}

/**
 * blkalloc_internal - find a block in @old_state, change allocation
 *           state to @new_state
 * @rgd: the resource group descriptor
 * @goal: the goal block within the RG (start here to search for avail block)
 * @old_state: GFS_BLKST_XXX the before-allocation state to find
 * @new_state: GFS_BLKST_XXX the after-allocation block state
 *
 * Walk rgrp's bitmap to find bits that represent a block in @old_state.
 * Add the found bitmap buffer to the transaction.
 * Set the found bits to @new_state to change block's allocation state.
 *
 * This function never fails, because we wouldn't call it unless we
 *   know (from reservation results, etc.) that a block is available.
 *
 * Scope of @goal and returned block is just within rgrp (32-bit),
 *   not the whole filesystem (64-bit).
 *
 * Returns:  the block # allocated (32-bit rgrp scope)
 */

static uint32_t
blkalloc_internal(struct gfs_rgrpd *rgd,
		  uint32_t goal,
		  unsigned char old_state, unsigned char new_state)
{
	struct gfs_bitmap *bits = NULL;
	uint32_t length = rgd->rd_ri.ri_length;
	uint32_t blk = 0;
	unsigned int buf, x;

	/* Find bitmap block that contains bits for goal block */
	for (buf = 0; buf < length; buf++) {
		bits = &rgd->rd_bits[buf];
		if (goal < (bits->bi_start + bits->bi_len) * GFS_NBBY)
			break;
	}

	gfs_assert(rgd->rd_sbd, buf < length,);

	/* Convert scope of "goal" from rgrp-wide to within found bit block */
	goal -= bits->bi_start * GFS_NBBY;

	/* Search (up to entire) bitmap in this rgrp for allocatable block.
	   "x <= length", instead of "x < length", because we typically start
	   the search in the middle of a bit block, but if we can't find an
	   allocatable block anywhere else, we want to be able wrap around and
	   search in the first part of our first-searched bit block.  */
	for (x = 0; x <= length; x++) {
		blk = gfs_bitfit(rgd->rd_bh[buf]->b_data + bits->bi_offset,
				 bits->bi_len, goal, old_state);
		if (blk != BFITNOENT)
			break;

		/* Try next bitmap block (wrap back to rgrp header if at end) */
		buf = (buf + 1) % length;
		bits = &rgd->rd_bits[buf];
		goal = 0;
	}

	if (unlikely(x > length)) {
		printk("GFS error: possible RG corruption\n");
		printk("    please run gfs_fsck after withdraw\n");
		dump_stack();
		if (gfs_assert_withdraw(rgd->rd_sbd, x <= length))
			blk = 0;
	}

	/* Attach bitmap buffer to trans, modify bits to do block alloc */
	gfs_trans_add_bh(rgd->rd_gl, rgd->rd_bh[buf]);
	gfs_setbit(rgd,
		   rgd->rd_bh[buf]->b_data + bits->bi_offset,
		   bits->bi_len, blk, new_state);

	/* Return allocated block #, rgrp scope (32-bit) */
	return bits->bi_start * GFS_NBBY + blk;
}

/**
 * blkfree_internal - Change alloc state of given block(s)
 * @sdp: the filesystem
 * @bstart: first block (64-bit filesystem scope) of a run of contiguous blocks
 * @blen: the length of the block run (all must lie within ONE RG!)
 * @new_state: GFS_BLKST_XXX the after-allocation block state
 *
 * Returns:  Resource group containing the block(s)
 *
 * Find rgrp containing @bstart.
 * For each block in run:
 *   Find allocation bitmap buffer.
 *   Add bitmap buffer to transaction.
 *   Set bits to new state.
 * Typically used to free blocks to GFS_BLKST_FREE or GFS_BLKST_FREEMETA,
 *   but @new_state can be any GFS_BLKST_XXX
 * 
 */

static struct gfs_rgrpd *
blkfree_internal(struct gfs_sbd *sdp, uint64_t bstart, uint32_t blen,
		 unsigned char new_state)
{
	struct gfs_rgrpd *rgd;
	struct gfs_bitmap *bits = NULL;
	uint32_t length, rgrp_blk, buf_blk;
	unsigned int buf;

	/* Find rgrp */
	rgd = gfs_blk2rgrpd(sdp, bstart);
	if (!rgd) {
		if (gfs_consist(sdp))
			printk("GFS: fsid=%s: block = %llu\n",
			       sdp->sd_fsname, bstart);
		return NULL;
	}

	length = rgd->rd_ri.ri_length;

	/* Convert blk # from filesystem scope (64-bit) to RG scope (32-bit) */
	rgrp_blk = bstart - rgd->rd_ri.ri_data1;

	while (blen--) {
		/* Find bitmap buffer for this block */
		for (buf = 0; buf < length; buf++) {
			bits = &rgd->rd_bits[buf];
			if (rgrp_blk < (bits->bi_start + bits->bi_len) * GFS_NBBY)
				break;
		}

		gfs_assert(rgd->rd_sbd, buf < length,);

		/* Find bits and set 'em */
		buf_blk = rgrp_blk - bits->bi_start * GFS_NBBY;
		rgrp_blk++;

		gfs_trans_add_bh(rgd->rd_gl, rgd->rd_bh[buf]);
		gfs_setbit(rgd,
			   rgd->rd_bh[buf]->b_data + bits->bi_offset,
			   bits->bi_len, buf_blk, new_state);
	}

	return rgd;
}

/**
 * clump_alloc - Allocate a clump of metadata blocks
 * @rgd: the resource group in which to allocate
 * @first: returns the first block allocated
 *
 * Returns: errno
 *
 * Bitmap-allocate a clump of metadata blocks
 * Write metadata blocks to disk with dummy meta-headers
 * Add meta-headers to incore meta-header cache
 */

static int
clump_alloc(struct gfs_rgrpd *rgd, uint32_t *first)
{
	struct gfs_sbd *sdp = rgd->rd_sbd;
	struct gfs_meta_header mh;
	struct buffer_head **bh;
	uint32_t goal, blk;
	unsigned int x;
	int error = 0;

	/* Dummy meta-header template */
	memset(&mh, 0, sizeof(struct gfs_meta_header));
	mh.mh_magic = GFS_MAGIC;
	mh.mh_type = GFS_METATYPE_NONE;

	/* Array of bh pointers used in several steps */
	bh = gmalloc(GFS_META_CLUMP * sizeof(struct buffer_head *));
	memset(bh, 0, GFS_META_CLUMP * sizeof(struct buffer_head *));

	/* Since we're looking for data blocks to change into meta blocks,
	   use last alloc'd *data* (not meta) block as start point */
	goal = rgd->rd_last_alloc_data;

	for (x = 0; x < GFS_META_CLUMP; x++) {
		blk = blkalloc_internal(rgd, goal, GFS_BLKST_FREE,
					GFS_BLKST_FREEMETA);
		if (!x)
			*first = blk;

		bh[x] = gfs_dgetblk(rgd->rd_gl, rgd->rd_ri.ri_data1 + blk);

		gfs_prep_new_buffer(bh[x]);

		gfs_meta_header_out(&mh, bh[x]->b_data);
		((struct gfs_meta_header *)bh[x]->b_data)->mh_generation = 0;

		/* start write of new meta-buffer to disk */
		error = gfs_dwrite(sdp, bh[x], DIO_DIRTY | DIO_START);
		if (error)
			goto out;

		goal = blk;
	}

	/* Block alloc start point for next time */
	rgd->rd_last_alloc_data = goal;

	/* Wait for all new meta-buffers to get on-disk */
	for (x = 0; x < GFS_META_CLUMP; x++) {
		error = gfs_dwrite(sdp, bh[x], DIO_WAIT);
		if (error)
			goto out;
	}

	/* Add all new meta-headers to meta-header cache */
	gfs_mhc_add(rgd, bh, GFS_META_CLUMP);

	gfs_assert_withdraw(sdp, rgd->rd_rg.rg_free >= GFS_META_CLUMP);
	rgd->rd_rg.rg_free -= GFS_META_CLUMP;
	rgd->rd_rg.rg_freemeta += GFS_META_CLUMP;

 out:
	for (x = 0; x < GFS_META_CLUMP; x++)
		if (bh[x]) {
			gfs_dwrite(sdp, bh[x], DIO_WAIT);
			brelse(bh[x]);
		}
	kfree(bh);

	return error;
}

/**
 * gfs_blkalloc - Allocate a data block
 * @ip: the inode to allocate the data block for
 * @block: the block allocated
 *
 */

void
gfs_blkalloc(struct gfs_inode *ip, uint64_t *block)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_alloc *al = ip->i_alloc;
	struct gfs_rgrpd *rgd = al->al_rgd;
	uint32_t goal, blk;
	int same;

	same = (rgd->rd_ri.ri_addr == ip->i_di.di_goal_rgrp);
	goal = (same) ? ip->i_di.di_goal_dblk : rgd->rd_last_alloc_data;

	blk = blkalloc_internal(rgd, goal,
				GFS_BLKST_FREE, GFS_BLKST_USED);
	rgd->rd_last_alloc_data = blk;

	if (!same) {
		ip->i_di.di_goal_rgrp = rgd->rd_ri.ri_addr;
		ip->i_di.di_goal_mblk = 0;
	}
	ip->i_di.di_goal_dblk = blk;

	*block = rgd->rd_ri.ri_data1 + blk;

	gfs_assert_withdraw(sdp, rgd->rd_rg.rg_free);
	rgd->rd_rg.rg_free--;

	gfs_trans_add_bh(rgd->rd_gl, rgd->rd_bh[0]);
	gfs_rgrp_out(&rgd->rd_rg, rgd->rd_bh[0]->b_data);

	al->al_alloced_data++;

	gfs_trans_add_quota(sdp, +1, ip->i_di.di_uid, ip->i_di.di_gid);

	/* total=0, free=-1, dinodes=0 */
	gfs_statfs_modify(sdp, 0, -1, 0);
}

/**
 * gfs_metaalloc - Allocate a metadata block to a file
 * @ip:  the file
 * @block: the block allocated
 *
 * Returns: errno
 */

int
gfs_metaalloc(struct gfs_inode *ip, uint64_t *block)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_alloc *al = ip->i_alloc;
	struct gfs_rgrpd *rgd = al->al_rgd;
	uint32_t goal, blk;
	int same;
	int error;

	same = (rgd->rd_ri.ri_addr == ip->i_di.di_goal_rgrp);

	if (!rgd->rd_rg.rg_freemeta) {
		error = clump_alloc(rgd, &goal);
		if (error)
			return error;

		al->al_alloced_data += GFS_META_CLUMP;
	} else
		goal = (same) ? ip->i_di.di_goal_mblk : rgd->rd_last_alloc_meta;

	blk = blkalloc_internal(rgd, goal,
				GFS_BLKST_FREEMETA, GFS_BLKST_USEDMETA);
	rgd->rd_last_alloc_meta = blk;

	if (!same) {
		ip->i_di.di_goal_rgrp = rgd->rd_ri.ri_addr;
		ip->i_di.di_goal_dblk = 0;
	}
	ip->i_di.di_goal_mblk = blk;

	*block = rgd->rd_ri.ri_data1 + blk;

	gfs_assert_withdraw(sdp, rgd->rd_rg.rg_freemeta);
	rgd->rd_rg.rg_freemeta--;
	rgd->rd_rg.rg_usedmeta++;

	gfs_trans_add_bh(rgd->rd_gl, rgd->rd_bh[0]);
	gfs_rgrp_out(&rgd->rd_rg, rgd->rd_bh[0]->b_data);

	al->al_alloced_meta++;

	gfs_trans_add_quota(sdp, +1, ip->i_di.di_uid, ip->i_di.di_gid);

	/* total=0, free=-1, dinode=0 */
	gfs_statfs_modify(sdp, 0, -1, 0);

	return 0;
}

/**
 * gfs_dialloc - Allocate a dinode
 * @dip: the directory that the inode is going in
 * @block: the block (result) which this function allocates as the dinode
 *     (64-bit filesystem scope)
 *
 * Returns: errno
 */

int
gfs_dialloc(struct gfs_inode *dip, uint64_t *block)
{
	struct gfs_sbd *sdp = dip->i_sbd;
	struct gfs_alloc *al = dip->i_alloc;
	struct gfs_rgrpd *rgd = al->al_rgd;
	uint32_t goal, blk;
	int error = 0;

	if (rgd->rd_rg.rg_freemeta)
		/* pick up where we left off last time */
		goal = rgd->rd_last_alloc_meta;
	else {
		/* no free meta blocks, allocate a bunch more */
		error = clump_alloc(rgd, &goal);
		if (error)
			return error;

		al->al_alloced_data += GFS_META_CLUMP;
	}

	/* Alloc the dinode; 32-bit "blk" is block offset within rgrp */
	blk = blkalloc_internal(rgd, goal,
				GFS_BLKST_FREEMETA, GFS_BLKST_USEDMETA);

	/* remember where we left off, for next time */
	rgd->rd_last_alloc_meta = blk;

	/* convert from rgrp scope (32-bit) to filesystem scope (64-bit) */
	*block = rgd->rd_ri.ri_data1 + blk;

	gfs_assert_withdraw(rgd->rd_sbd, rgd->rd_rg.rg_freemeta);
	rgd->rd_rg.rg_freemeta--;
	rgd->rd_rg.rg_useddi++;

	/* Attach rgrp header to trans, update freemeta and useddi stats */
	gfs_trans_add_bh(rgd->rd_gl, rgd->rd_bh[0]);
	gfs_rgrp_out(&rgd->rd_rg, rgd->rd_bh[0]->b_data);

	/* Update stats in in-place reservation struct */
	al->al_alloced_di++;
	al->al_alloced_meta++;

	/* total=0, free=-1, dinodes=1 */
	gfs_statfs_modify(sdp, 0, -1, +1);

	return error;
}

/**
 * gfs_blkfree - free a contiguous run of data block(s)
 * @ip: the inode these blocks are being freed from
 * @bstart: first block (64-bit filesystem scope) of a run of contiguous blocks
 * @blen: the length of the block run (all must lie within ONE RG!)
 *
 * Bitmap-deallocate the blocks (to FREE data state), add bitmap blks to trans
 * Update rgrp alloc statistics in rgrp header, add rgrp header buf to trans
 * Update quotas, add to trans.
 */

void
gfs_blkfree(struct gfs_inode *ip, uint64_t bstart, uint32_t blen)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_rgrpd *rgd;

	rgd = blkfree_internal(sdp, bstart, blen, GFS_BLKST_FREE);
	if (!rgd)
		return;

	rgd->rd_rg.rg_free += blen;

	gfs_trans_add_bh(rgd->rd_gl, rgd->rd_bh[0]);
	gfs_rgrp_out(&rgd->rd_rg, rgd->rd_bh[0]->b_data);

	gfs_trans_add_quota(sdp, -(int64_t)blen,
			    ip->i_di.di_uid,
			    ip->i_di.di_gid);

	/* total=0, free=+blen, dinodes=0 */
	gfs_statfs_modify(sdp, 0, blen, 0);
}

/**
 * gfs_metafree - free a contiguous run of metadata block(s)
 * @ip: the inode these blocks are being freed from
 * @bstart: first block (64-bit filesystem scope) of a run of contiguous blocks
 * @blen: the length of the block run (all must lie within ONE RG!)
 *
 * Bitmap-deallocate the blocks (to FREEMETA state), add bitmap blks to trans.
 * Update rgrp alloc statistics in rgrp header, add rgrp header to trans.
 * Update quotas (quotas include metadata, not just data block usage),
 *    add to trans.
 * Release deallocated buffers, add to meta-header cache (we save these in-core
 *    so we don't need to re-read meta blocks if/when they are re-alloc'd).
 */

void
gfs_metafree(struct gfs_inode *ip, uint64_t bstart, uint32_t blen)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_rgrpd *rgd;

	rgd = blkfree_internal(sdp, bstart, blen, GFS_BLKST_FREEMETA);
	if (!rgd)
		return;

	if (rgd->rd_rg.rg_usedmeta < blen)
		gfs_consist_rgrpd(rgd);
	rgd->rd_rg.rg_usedmeta -= blen;
	rgd->rd_rg.rg_freemeta += blen;

	gfs_trans_add_bh(rgd->rd_gl, rgd->rd_bh[0]);
	gfs_rgrp_out(&rgd->rd_rg, rgd->rd_bh[0]->b_data);

	/* total=0, free=blen, dinode=0 */
	gfs_statfs_modify(sdp, 0, blen, 0);

	gfs_trans_add_quota(sdp, -(int64_t)blen,
			    ip->i_di.di_uid,
			    ip->i_di.di_gid);
	gfs_wipe_buffers(ip, rgd, bstart, blen);
}

/**
 * gfs_difree_uninit - free a dinode block
 * @rgd: the resource group that contains the dinode
 * @addr: the dinode address
 *
 * De-allocate the dinode to FREEMETA using block alloc bitmap.
 * Update rgrp's block usage statistics (used dinode--, free meta++).
 * Add rgrp header to transaction.
 */

void
gfs_difree_uninit(struct gfs_rgrpd *rgd, uint64_t addr)
{
	struct gfs_rgrpd *tmp_rgd;

	tmp_rgd = blkfree_internal(rgd->rd_sbd, addr, 1,
				   GFS_BLKST_FREEMETA);
	if (!tmp_rgd)
		return;
	gfs_assert_withdraw(rgd->rd_sbd, rgd == tmp_rgd);

	if (!rgd->rd_rg.rg_useddi)
		gfs_consist_rgrpd(rgd);
	rgd->rd_rg.rg_useddi--;
	rgd->rd_rg.rg_freemeta++;

	gfs_trans_add_bh(rgd->rd_gl, rgd->rd_bh[0]);
	gfs_rgrp_out(&rgd->rd_rg, rgd->rd_bh[0]->b_data);

	/* total=0, free=1, dinodes=-1 */
	gfs_statfs_modify(rgd->rd_sbd, 0, +1, -1);
}

/**
 * gfs_difree - free a dinode block
 * @rgd: the resource group that contains the dinode
 * @ip: the inode representing the dinode to free
 *
 * Free the dinode block to FREEMETA, update rgrp's block usage stats.
 * Update quotas (quotas include metadata, not just data block usage),
 *    add to trans.
 * Release deallocated buffers, add to meta-header cache (we save these in-core
 *    so we don't need to re-read meta blocks if/when they are re-alloc'd).
 */

void
gfs_difree(struct gfs_rgrpd *rgd, struct gfs_inode *ip)
{
	gfs_difree_uninit(rgd, ip->i_num.no_addr);
	gfs_trans_add_quota(ip->i_sbd, -1, ip->i_di.di_uid, ip->i_di.di_gid);
	gfs_wipe_buffers(ip, rgd, ip->i_num.no_addr, 1);
}

/**
 * gfs_rlist_add - add a RG to a list of RGs
 * @sdp: the filesystem
 * @rlist: the list of resource groups
 * @block: the block
 *
 * Figure out what RG a block belongs to and add that RG to the list
 *
 * FIXME: Don't use gmalloc()
 *
 */

void
gfs_rlist_add(struct gfs_sbd *sdp, struct gfs_rgrp_list *rlist, uint64_t block)
{
	struct gfs_rgrpd *rgd;
	struct gfs_rgrpd **tmp;
	unsigned int new_space;
	unsigned int x;

	if (gfs_assert_warn(sdp, !rlist->rl_ghs))
		return;

	rgd = gfs_blk2rgrpd(sdp, block);
	if (!rgd) {
		if (gfs_consist(sdp))
			printk("GFS: fsid=%s: block = %llu\n",
			       sdp->sd_fsname, block);
		return;
	}

	for (x = 0; x < rlist->rl_rgrps; x++)
		if (rlist->rl_rgd[x] == rgd)
			return;

	if (rlist->rl_rgrps == rlist->rl_space) {
		new_space = rlist->rl_space + 10;

		tmp = gmalloc(new_space * sizeof(struct gfs_rgrpd *));

		if (rlist->rl_rgd) {
			memcpy(tmp, rlist->rl_rgd,
			       rlist->rl_space * sizeof(struct gfs_rgrpd *));
			kfree(rlist->rl_rgd);
		}

		rlist->rl_space = new_space;
		rlist->rl_rgd = tmp;
	}

	rlist->rl_rgd[rlist->rl_rgrps++] = rgd;
}

/**
 * gfs_rlist_alloc - all RGs have been added to the rlist, now allocate
 *      and initialize an array of glock holders for them
 * @rlist: the list of resource groups
 * @state: the lock state to acquire the RG lock in
 * @flags: the modifier flags for the holder structures
 *
 * FIXME: Don't use gmalloc()
 *
 */

void
gfs_rlist_alloc(struct gfs_rgrp_list *rlist, unsigned int state, int flags)
{
	unsigned int x;

	rlist->rl_ghs = gmalloc(rlist->rl_rgrps * sizeof(struct gfs_holder));
	for (x = 0; x < rlist->rl_rgrps; x++)
		gfs_holder_init(rlist->rl_rgd[x]->rd_gl,
				state, flags,
				&rlist->rl_ghs[x]);
}

/**
 * gfs_rlist_free - free a resource group list
 * @list: the list of resource groups
 *
 */

void
gfs_rlist_free(struct gfs_rgrp_list *rlist)
{
	unsigned int x;

	if (rlist->rl_rgd)
		kfree(rlist->rl_rgd);

	if (rlist->rl_ghs) {
		for (x = 0; x < rlist->rl_rgrps; x++)
			gfs_holder_uninit(&rlist->rl_ghs[x]);
		kfree(rlist->rl_ghs);
	}
}

/**
 * gfs_reclaim_metadata - reclaims unused metadata
 * @sdp: the file system
 * @inodes:
 * @metadata:
 *
 * This function will look through the resource groups and
 * free the unused metadata.
 *
 * Returns: errno
 */

int
gfs_reclaim_metadata(struct gfs_sbd *sdp, 
		     uint64_t *inodes,
		     uint64_t *metadata)
{
	struct gfs_holder ji_gh, ri_gh, rgd_gh, t_gh;
	struct gfs_rgrpd *rgd;
	struct gfs_rgrp *rg;
	struct gfs_dinode *di;
	struct gfs_inum next;
	struct buffer_head *bh;
	uint32_t flags;
	uint32_t goal;
	unsigned int x;
	int error = 0;

	*inodes = *metadata = 0;

	/* Acquire the jindex lock here so we don't deadlock with a
	   process writing the the jindex inode. :-( */

	error = gfs_jindex_hold(sdp, &ji_gh);
	if (error)
		goto fail;

	error = gfs_rindex_hold(sdp, &ri_gh);
	if (error)
		goto fail_jindex_relse;

	for (rgd = gfs_rgrpd_get_first(sdp);
	     rgd;
	     rgd = gfs_rgrpd_get_next(rgd)) {
		error = gfs_glock_nq_init(rgd->rd_gl,
					  LM_ST_EXCLUSIVE, GL_NOCACHE,
					  &rgd_gh);
		if (error)
			goto fail_rindex_relse;

		rgrp_verify(rgd);

		rg = &rgd->rd_rg;

		if (!rg->rg_freedi && !rg->rg_freemeta) {
			gfs_glock_dq_uninit(&rgd_gh);
			continue;
		}

		gfs_mhc_zap(rgd);
		gfs_depend_sync(rgd);

		error = gfs_lock_fs_check_clean(sdp, LM_ST_EXCLUSIVE, &t_gh);
		if (error)
			goto fail_gunlock_rg;

		error = gfs_trans_begin(sdp, rgd->rd_ri.ri_length, 0);
		if (error)
			goto fail_unlock_fs;

		next = rg->rg_freedi_list;

		for (x = rg->rg_freedi; x--;) {
			if (!next.no_formal_ino || !next.no_addr) {
				gfs_consist_rgrpd(rgd);
				error = -EIO;
				goto fail_end_trans;
			}

			blkfree_internal(sdp, next.no_addr, 1, GFS_BLKST_FREE);

			error = gfs_dread(rgd->rd_gl, next.no_addr,
					  DIO_FORCE | DIO_START | DIO_WAIT, &bh);
			if (error)
				goto fail_end_trans;

			di = (struct gfs_dinode *)bh->b_data;
			flags = di->di_flags;
			flags = gfs32_to_cpu(flags);
			if (!(flags & GFS_DIF_UNUSED)) {
				gfs_consist_rgrpd(rgd);
				brelse(bh);
				error = -EIO;
				goto fail_end_trans;
			}

			gfs_inum_in(&next, (char *)&di->di_next_unused);

			brelse(bh);

			rg->rg_freedi--;
			rg->rg_free++;
			(*inodes)++;
		}

		if (next.no_formal_ino || next.no_addr) {
			gfs_consist_rgrpd(rgd);
			error = -EIO;
			goto fail_end_trans;
		}
		rg->rg_freedi_list = next;

		goal = 0;
		for (x = rg->rg_freemeta; x--;) {
			goal = blkalloc_internal(rgd, goal,
						 GFS_BLKST_FREEMETA, GFS_BLKST_FREE);
			rg->rg_freemeta--;
			rg->rg_free++;
			(*metadata)++;
		}

		gfs_trans_add_bh(rgd->rd_gl, rgd->rd_bh[0]);
		gfs_rgrp_out(rg, rgd->rd_bh[0]->b_data);

		gfs_trans_end(sdp);

		gfs_glock_dq_uninit(&t_gh);

		gfs_glock_dq_uninit(&rgd_gh);
	}

	gfs_glock_dq_uninit(&ri_gh);

	gfs_glock_dq_uninit(&ji_gh);

	return 0;

 fail_end_trans:
	gfs_trans_end(sdp);

 fail_unlock_fs:
	gfs_glock_dq_uninit(&t_gh);

 fail_gunlock_rg:
	gfs_glock_dq_uninit(&rgd_gh);

 fail_rindex_relse:
	gfs_glock_dq_uninit(&ri_gh);

 fail_jindex_relse:
	gfs_glock_dq_uninit(&ji_gh);

 fail:
	return error;
}
