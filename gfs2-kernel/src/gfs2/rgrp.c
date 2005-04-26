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

#include "gfs2.h"
#include "bits.h"
#include "dio.h"
#include "file.h"
#include "glock.h"
#include "glops.h"
#include "lops.h"
#include "quota.h"
#include "rgrp.h"
#include "super.h"
#include "trans.h"

#if 0
/**
 * rgrp_verify - Verify that a resource group is consistent
 * @sdp: the filesystem
 * @rgd: the rgrp
 *
 */

static void
rgrp_verify(struct gfs2_rgrpd *rgd)
{
	ENTER(G2FN_RGRP_VERIFY)
	struct gfs2_sbd *sdp = rgd->rd_sbd;
	struct gfs2_bitmap *bi = NULL;
	uint32_t length = rgd->rd_ri.ri_length;
	uint32_t count[4], tmp;
	int buf, x;

	memset(count, 0, 4 * sizeof(uint32_t));

	/* Count # blocks in each of 4 possible allocation states */
	for (buf = 0; buf < length; buf++) {
		bi = rgd->rd_bits + buf;
		for (x = 0; x < 4; x++)
			count[x] += gfs2_bitcount(rgd,
						  bi->bi_bh->b_data +
						  bi->bi_offset,
						  bi->bi_len, x);
	}

	if (count[0] != rgd->rd_rg.rg_free) {
		if (gfs2_consist_rgrpd(rgd))
			printk("GFS2: fsid=%s: free data mismatch:  %u != %u\n",
			       sdp->sd_fsname, count[0], rgd->rd_rg.rg_free);
		RET(G2FN_RGRP_VERIFY);
	}

	tmp = rgd->rd_ri.ri_data -
		rgd->rd_rg.rg_free -
		rgd->rd_rg.rg_dinodes;
	if (count[1] != tmp) {
		if (gfs2_consist_rgrpd(rgd))
			printk("GFS2: fsid=%s: used data mismatch:  %u != %u\n",
			       sdp->sd_fsname, count[1], tmp);
		RET(G2FN_RGRP_VERIFY);
	}

	if (count[2]) {
		if (gfs2_consist_rgrpd(rgd))
			printk("GFS2: fsid=%s: free metadata mismatch:  %u != 0\n",
			       sdp->sd_fsname, count[2]);
		RET(G2FN_RGRP_VERIFY);
	}

	if (count[3] != rgd->rd_rg.rg_dinodes) {
		if (gfs2_consist_rgrpd(rgd))
			printk("GFS2: fsid=%s: used metadata mismatch:  %u != %u\n",
			       sdp->sd_fsname, count[3], rgd->rd_rg.rg_dinodes);
		RET(G2FN_RGRP_VERIFY);
	}

	RET(G2FN_RGRP_VERIFY);
}
#endif

static __inline__ int
rgrp_contains_block(struct gfs2_rindex *ri, uint64_t block)
{
	uint64_t first = ri->ri_data0;
	uint64_t last = first + ri->ri_data;
	return !!(first <= block && block < last);
}

/**
 * gfs2_blk2rgrpd - Find resource group for a given data/meta block number
 * @sdp: The GFS2 superblock
 * @n: The data block number
 *
 * Returns: The resource group, or NULL if not found
 *
 * Don't try to use this for non-allocatable block numbers (i.e. rgrp header
 *   or bitmap blocks); it's for allocatable (data/meta) blocks only.
 */

struct gfs2_rgrpd *
gfs2_blk2rgrpd(struct gfs2_sbd *sdp, uint64_t blk)
{
	ENTER(G2FN_BLK2RGPRD)
	struct list_head *tmp, *head;
	struct gfs2_rgrpd *rgd = NULL;

	spin_lock(&sdp->sd_rindex_spin);

	for (head = &sdp->sd_rindex_mru_list, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		rgd = list_entry(tmp, struct gfs2_rgrpd, rd_list_mru);
		if (rgrp_contains_block(&rgd->rd_ri, blk)) {
			list_move(&rgd->rd_list_mru, &sdp->sd_rindex_mru_list);
			spin_unlock(&sdp->sd_rindex_spin);
			RETURN(G2FN_BLK2RGPRD, rgd);
		}
	}

	spin_unlock(&sdp->sd_rindex_spin);

	RETURN(G2FN_BLK2RGPRD, NULL);
}

/**
 * gfs2_rgrpd_get_first - get the first Resource Group in the filesystem
 * @sdp: The GFS2 superblock
 *
 * Returns: The first rgrp in the filesystem
 */

struct gfs2_rgrpd *
gfs2_rgrpd_get_first(struct gfs2_sbd *sdp)
{
	ENTER(G2FN_RGRPD_GET_FIRST)
	gfs2_assert(sdp, !list_empty(&sdp->sd_rindex_list),);
	RETURN(G2FN_RGRPD_GET_FIRST,
	       list_entry(sdp->sd_rindex_list.next, struct gfs2_rgrpd, rd_list));
}

/**
 * gfs2_rgrpd_get_next - get the next RG
 * @rgd: A RG
 *
 * Returns: The next rgrp
 */

struct gfs2_rgrpd *
gfs2_rgrpd_get_next(struct gfs2_rgrpd *rgd)
{
	ENTER(G2FN_RGRPD_GET_NEXT)
	if (rgd->rd_list.next == &rgd->rd_sbd->sd_rindex_list)
		RETURN(G2FN_RGRPD_GET_NEXT, NULL);
	RETURN(G2FN_RGRPD_GET_NEXT,
	       list_entry(rgd->rd_list.next, struct gfs2_rgrpd, rd_list));
}

/**
 * clear_rgrpdi - Clear up rgrps
 * @sdp: The GFS2 superblock
 *
 */

void
clear_rgrpdi(struct gfs2_sbd *sdp)
{
	ENTER(G2FN_CLEAR_RGRPDI)
       	struct list_head *head;
	struct gfs2_rgrpd *rgd;
	struct gfs2_glock *gl;

	spin_lock(&sdp->sd_rindex_spin);
	sdp->sd_rindex_forward = NULL;
	head = &sdp->sd_rindex_recent_list;
	while (!list_empty(head)) {
		rgd = list_entry(head->next, struct gfs2_rgrpd, rd_recent);
		list_del(&rgd->rd_recent);
	}
	spin_unlock(&sdp->sd_rindex_spin);

	head = &sdp->sd_rindex_list;
	while (!list_empty(head)) {
		rgd = list_entry(head->next, struct gfs2_rgrpd, rd_list);
		gl = rgd->rd_gl;

		list_del(&rgd->rd_list);
		list_del(&rgd->rd_list_mru);

		if (gl) {
			set_gl2rgd(gl, NULL);
			gfs2_glock_put(gl);
		}

		if (rgd->rd_bits)
			kfree(rgd->rd_bits);

		kfree(rgd);
	}

	RET(G2FN_CLEAR_RGRPDI);
}

/**
 * gfs2_clear_rgrpd - Clear up rgrps
 * @sdp: The GFS2 superblock
 *
 */

void
gfs2_clear_rgrpd(struct gfs2_sbd *sdp)
{
	ENTER(G2FN_CLEAR_RGRPD)
	down(&sdp->sd_rindex_mutex);
	clear_rgrpdi(sdp);
	up(&sdp->sd_rindex_mutex);
	RET(G2FN_CLEAR_RGRPD);
}

/**
 * gfs2_compute_bitstructs - Compute the bitmap sizes
 * @rgd: The resource group descriptor
 *
 * Calculates bitmap descriptors, one for each block that contains bitmap data
 *
 * Returns: errno
 */

static int
compute_bitstructs(struct gfs2_rgrpd *rgd)
{
	ENTER(G2FN_COMPUTE_BITSTRUCTS)
	struct gfs2_sbd *sdp = rgd->rd_sbd;
	struct gfs2_bitmap *bi;
	uint32_t length = rgd->rd_ri.ri_length; /* # blocks in hdr & bitmap */
	uint32_t bytes_left, bytes;
	int x;

	rgd->rd_bits = kmalloc(length * sizeof(struct gfs2_bitmap),
			       GFP_KERNEL);
	if (!rgd->rd_bits)
		RETURN(G2FN_COMPUTE_BITSTRUCTS, -ENOMEM);
	memset(rgd->rd_bits, 0, length * sizeof(struct gfs2_bitmap));

	bytes_left = rgd->rd_ri.ri_bitbytes;

	for (x = 0; x < length; x++) {
		bi = rgd->rd_bits + x;

		/* small rgrp; bitmap stored completely in header block */
		if (length == 1) {
			bytes = bytes_left;
			bi->bi_offset = sizeof(struct gfs2_rgrp);
			bi->bi_start = 0;
			bi->bi_len = bytes;
		/* header block */
		} else if (x == 0) {
			bytes = sdp->sd_sb.sb_bsize - sizeof(struct gfs2_rgrp);
			bi->bi_offset = sizeof(struct gfs2_rgrp);
			bi->bi_start = 0;
			bi->bi_len = bytes;
		/* last block */
		} else if (x + 1 == length) {
			bytes = bytes_left;
			bi->bi_offset = sizeof(struct gfs2_meta_header);
			bi->bi_start = rgd->rd_ri.ri_bitbytes - bytes_left;
			bi->bi_len = bytes;
		/* other blocks */
		} else {
			bytes = sdp->sd_sb.sb_bsize - sizeof(struct gfs2_meta_header);
			bi->bi_offset = sizeof(struct gfs2_meta_header);
			bi->bi_start = rgd->rd_ri.ri_bitbytes - bytes_left;
			bi->bi_len = bytes;
		}

		bytes_left -= bytes;
	}

	if (bytes_left) {
		gfs2_consist_rgrpd(rgd);
		RETURN(G2FN_COMPUTE_BITSTRUCTS, -EIO);
	}
	bi = rgd->rd_bits + (length - 1);
        if ((bi->bi_start + bi->bi_len) * GFS2_NBBY != rgd->rd_ri.ri_data) {
		if (gfs2_consist_rgrpd(rgd)) {
			gfs2_rindex_print(&rgd->rd_ri);
			printk("GFS2: fsid=%s: start=%u len=%u offset=%u\n",
			       sdp->sd_fsname,
			       bi->bi_start, bi->bi_len, bi->bi_offset);
		}
		RETURN(G2FN_COMPUTE_BITSTRUCTS, -EIO);
	}

	RETURN(G2FN_COMPUTE_BITSTRUCTS, 0);
}

/**
 * gfs2_ri_update - Pull in a new resource index from the disk
 * @gl: The glock covering the rindex inode
 *
 * Returns: 0 on successful update, error code otherwise
 */

static int
gfs2_ri_update(struct gfs2_inode *ip)
{
	ENTER(G2FN_RI_UPDATE)
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_rgrpd *rgd;
	char buf[sizeof(struct gfs2_rindex)];
	int error;

	if (do_mod(ip->i_di.di_size, sizeof(struct gfs2_rindex))) {
		gfs2_consist_inode(ip);
		RETURN(G2FN_RI_UPDATE, -EIO);
	}

	clear_rgrpdi(sdp);

	for (sdp->sd_rgrps = 0;; sdp->sd_rgrps++) {
		error = gfs2_internal_read(ip, buf,
					  sdp->sd_rgrps *
					  sizeof(struct gfs2_rindex),
					  sizeof(struct gfs2_rindex));
		if (!error)
			break;
		if (error != sizeof(struct gfs2_rindex)) {
			if (error > 0)
				error = -EIO;
			goto fail;
		}

		rgd = kmalloc(sizeof(struct gfs2_rgrpd), GFP_KERNEL);
		error = -ENOMEM;
		if (!rgd)
			goto fail;
		memset(rgd, 0, sizeof(struct gfs2_rgrpd));

		init_MUTEX(&rgd->rd_mutex);
		INIT_LE(&rgd->rd_le, &gfs2_rg_lops);
		rgd->rd_sbd = sdp;

		list_add_tail(&rgd->rd_list, &sdp->sd_rindex_list);
		list_add_tail(&rgd->rd_list_mru, &sdp->sd_rindex_mru_list);

		gfs2_rindex_in(&rgd->rd_ri, buf);

		error = compute_bitstructs(rgd);
		if (error)
			goto fail;

		error = gfs2_glock_get(sdp, rgd->rd_ri.ri_addr, &gfs2_rgrp_glops,
				      CREATE, &rgd->rd_gl);
		if (error)
			goto fail;

		set_gl2rgd(rgd->rd_gl, rgd);
		rgd->rd_rg_vn = rgd->rd_gl->gl_vn - 1;
	}

	sdp->sd_rindex_vn = ip->i_gl->gl_vn;

	RETURN(G2FN_RI_UPDATE, 0);

 fail:
	clear_rgrpdi(sdp);

	RETURN(G2FN_RI_UPDATE, error);
}

/**
 * gfs2_rindex_hold - Grab a lock on the rindex
 * @sdp: The GFS2 superblock
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
 *   filesystem (via gfs2_grow utility), which adds new resource groups.
 *
 * Returns: 0 on success, error code otherwise
 */

int
gfs2_rindex_hold(struct gfs2_sbd *sdp, struct gfs2_holder *ri_gh)
{
	ENTER(G2FN_RINDEX_HOLD)
	struct gfs2_inode *ip = sdp->sd_rindex;
	struct gfs2_glock *gl = ip->i_gl;
	int error;

	error = gfs2_glock_nq_init(gl, LM_ST_SHARED, 0, ri_gh);
	if (error)
		RETURN(G2FN_RINDEX_HOLD, error);

	/* Read new copy from disk if we don't have the latest */
	if (sdp->sd_rindex_vn != gl->gl_vn) {
		down(&sdp->sd_rindex_mutex);
		if (sdp->sd_rindex_vn != gl->gl_vn) {
			error = gfs2_ri_update(ip);
			if (error)
				gfs2_glock_dq_uninit(ri_gh);
		}
		up(&sdp->sd_rindex_mutex);
	}

	RETURN(G2FN_RINDEX_HOLD, error);
}

/**
 * gfs2_rgrp_bh_get - Read in a RG's header and bitmaps
 * @rgd: the struct gfs2_rgrpd describing the RG to read in
 *
 * Read in all of a Resource Group's header and bitmap blocks.
 * Caller must eventually call gfs2_rgrp_relse() to free the bitmaps.
 *
 * Returns: errno
 */

int
gfs2_rgrp_bh_get(struct gfs2_rgrpd *rgd)
{
	ENTER(G2FN_RGRP_BH_GET)
	struct gfs2_sbd *sdp = rgd->rd_sbd;
	struct gfs2_glock *gl = rgd->rd_gl;
	unsigned int length = rgd->rd_ri.ri_length;
	struct gfs2_bitmap *bi;
	unsigned int x;
	int error;

	down(&rgd->rd_mutex);

	spin_lock(&sdp->sd_rindex_spin);
	if (rgd->rd_bh_count) {
		rgd->rd_bh_count++;
		spin_unlock(&sdp->sd_rindex_spin);
		up(&rgd->rd_mutex);
		RETURN(G2FN_RGRP_BH_GET, 0);
	}
	spin_unlock(&sdp->sd_rindex_spin);

	for (x = 0; x < length; x++) {
		bi = rgd->rd_bits + x;
		bi->bi_bh = gfs2_dgetblk(gl, rgd->rd_ri.ri_addr + x);
	}

	for (x = 0; x < length; x++) {
		bi = rgd->rd_bits + x;
		error = gfs2_dreread(sdp, bi->bi_bh, DIO_START);
		if (error)
			goto fail;
	}

	for (x = length; x--;) {
		bi = rgd->rd_bits + x;
		error = gfs2_dreread(sdp, bi->bi_bh, DIO_WAIT);
		if (error)
			goto fail;
		if (gfs2_metatype_check(sdp, bi->bi_bh,
				       (x) ? GFS2_METATYPE_RB : GFS2_METATYPE_RG)) {
			error = -EIO;
			goto fail;
		}
	}

	if (rgd->rd_rg_vn != gl->gl_vn) {
		gfs2_rgrp_in(&rgd->rd_rg, (rgd->rd_bits[0].bi_bh)->b_data);
		rgd->rd_rg_vn = gl->gl_vn;
	}

	spin_lock(&sdp->sd_rindex_spin);
	rgd->rd_free_clone = rgd->rd_rg.rg_free;
	rgd->rd_bh_count++;
	spin_unlock(&sdp->sd_rindex_spin);

	up(&rgd->rd_mutex);

	RETURN(G2FN_RGRP_BH_GET, 0);

 fail:
	for (x = 0; x < length; x++) {
		bi = rgd->rd_bits + x;
		brelse(bi->bi_bh);
		bi->bi_bh = NULL;
		gfs2_assert_warn(sdp, !bi->bi_clone);
	}
	up(&rgd->rd_mutex);

	RETURN(G2FN_RGRP_BH_GET, error);
}

void
gfs2_rgrp_bh_hold(struct gfs2_rgrpd *rgd)
{
	ENTER(G2FN_RGRP_BH_HOLD)
	struct gfs2_sbd *sdp = rgd->rd_sbd;

	spin_lock(&sdp->sd_rindex_spin);
	gfs2_assert_warn(rgd->rd_sbd, rgd->rd_bh_count);
	rgd->rd_bh_count++;
	spin_unlock(&sdp->sd_rindex_spin);
		
	RET(G2FN_RGRP_BH_HOLD);
}

/**
 * gfs2_rgrp_bh_put - Release RG bitmaps read in with gfs2_rgrp_bh_get()
 * @rgd: the struct gfs2_rgrpd describing the RG to read in
 *
 */

void
gfs2_rgrp_bh_put(struct gfs2_rgrpd *rgd)
{
	ENTER(G2FN_RGRP_BH_PUT)
       	struct gfs2_sbd *sdp = rgd->rd_sbd;
	int x, length = rgd->rd_ri.ri_length;

	spin_lock(&sdp->sd_rindex_spin);
	gfs2_assert_warn(rgd->rd_sbd, rgd->rd_bh_count);
	if (--rgd->rd_bh_count) {
		spin_unlock(&sdp->sd_rindex_spin);
		RET(G2FN_RGRP_BH_PUT);
	}

	for (x = 0; x < length; x++) {
		struct gfs2_bitmap *bi = rgd->rd_bits + x;
		if (bi->bi_clone) {
			kfree(bi->bi_clone);
			bi->bi_clone = NULL;
		}
		brelse(bi->bi_bh);
		bi->bi_bh = NULL;
	}

	spin_unlock(&sdp->sd_rindex_spin);

	RET(G2FN_RGRP_BH_PUT);
}

void
gfs2_rgrp_repolish_clones(struct gfs2_rgrpd *rgd)
{
	ENTER(G2FN_RGRP_REPOLISH_CLONES)
	struct gfs2_sbd *sdp = rgd->rd_sbd;
	unsigned int length = rgd->rd_ri.ri_length;
	unsigned int x;

	for (x = 0; x < length; x++) {
		struct gfs2_bitmap *bi = rgd->rd_bits + x;
		if (!bi->bi_clone)
			continue;
		memcpy(bi->bi_clone + bi->bi_offset,
		       bi->bi_bh->b_data + bi->bi_offset,
		       bi->bi_len);
	}

	spin_lock(&sdp->sd_rindex_spin);
	rgd->rd_free_clone = rgd->rd_rg.rg_free;
	spin_unlock(&sdp->sd_rindex_spin);

	RET(G2FN_RGRP_REPOLISH_CLONES);
}

/**
 * gfs2_alloc_get - allocate a struct gfs2_alloc structure for an inode
 * @ip: the incore GFS2 inode structure
 *
 * Alloc and zero an in-place reservation structure,
 *   and attach it to the GFS2 incore inode.
 *
 * FIXME: Don't use kmalloc_nofail()
 *
 * Returns: the struct gfs2_alloc
 */

struct gfs2_alloc *
gfs2_alloc_get(struct gfs2_inode *ip)
{
	ENTER(G2FN_ALLOC_GET)
	struct gfs2_alloc *al = ip->i_alloc;

	gfs2_assert_warn(ip->i_sbd, !al);

	al = kmalloc_nofail(sizeof(struct gfs2_alloc), GFP_KERNEL);
	memset(al, 0, sizeof(struct gfs2_alloc));

	ip->i_alloc = al;

	RETURN(G2FN_ALLOC_GET, al);
}

/**
 * gfs2_alloc_put - throw away the struct gfs2_alloc for an inode
 * @ip: the inode
 *
 */

void
gfs2_alloc_put(struct gfs2_inode *ip)
{
	ENTER(G2FN_ALLOC_PUT)
	struct gfs2_alloc *al = ip->i_alloc;

	if (gfs2_assert_warn(ip->i_sbd, al))
		RET(G2FN_ALLOC_PUT);

	ip->i_alloc = NULL;
	kfree(al);

	RET(G2FN_ALLOC_PUT);
}

/**
 * try_rgrp_fit - See if a given reservation will fit in a given RG
 * @rgd: the RG data
 * @al: the struct gfs2_alloc structure describing the reservation
 *
 * If there's room for the requested blocks to be allocated from the RG:
 *   Sets the $al_reserved_data field in @al.
 *   Sets the $al_reserved_meta field in @al.
 *   Sets the $al_rgd field in @al.
 *
 * Returns: 1 on success (it fits), 0 on failure (it doesn't fit)
 */

static int
try_rgrp_fit(struct gfs2_rgrpd *rgd, struct gfs2_alloc *al)
{
	ENTER(G2FN_TRY_RGRP_FIT)
       	struct gfs2_sbd *sdp = rgd->rd_sbd;
	int ret = 0;

	spin_lock(&sdp->sd_rindex_spin);
	if (rgd->rd_free_clone >= al->al_requested) {
		al->al_rgd = rgd;
		ret = 1;
	}
	spin_unlock(&sdp->sd_rindex_spin);

	RETURN(G2FN_TRY_RGRP_FIT, ret);
}

/**
 * recent_rgrp_first - get first RG from "recent" list
 * @sdp: The GFS2 superblock
 * @rglast: address of the rgrp used last
 *
 * Returns: The first rgrp in the recent list
 */

static struct gfs2_rgrpd *
recent_rgrp_first(struct gfs2_sbd *sdp, uint64_t rglast)
{
	ENTER(G2FN_RECENT_RGRP_FIRST)
	struct list_head *tmp, *head;
	struct gfs2_rgrpd *rgd = NULL;

	spin_lock(&sdp->sd_rindex_spin);

	if (list_empty(&sdp->sd_rindex_recent_list))
		goto out;

	if (!rglast)
		goto first;

	for (head = &sdp->sd_rindex_recent_list, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		rgd = list_entry(tmp, struct gfs2_rgrpd, rd_recent);
		if (rgd->rd_ri.ri_addr == rglast)
			goto out;
	}

 first:
	rgd = list_entry(sdp->sd_rindex_recent_list.next, struct gfs2_rgrpd, rd_recent);

 out:
	spin_unlock(&sdp->sd_rindex_spin);

	RETURN(G2FN_RECENT_RGRP_FIRST, rgd);
}

/**
 * recent_rgrp_next - get next RG from "recent" list
 * @cur_rgd: current rgrp
 * @remove:
 *
 * Returns: The next rgrp in the recent list
 */

static struct gfs2_rgrpd *
recent_rgrp_next(struct gfs2_rgrpd *cur_rgd, int remove)
{
	ENTER(G2FN_RECENT_RGRP_NEXT)
	struct gfs2_sbd *sdp = cur_rgd->rd_sbd;
	struct list_head *tmp, *head;
	struct gfs2_rgrpd *rgd;

	spin_lock(&sdp->sd_rindex_spin);

	for (head = &sdp->sd_rindex_recent_list, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		rgd = list_entry(tmp, struct gfs2_rgrpd, rd_recent);
		if (rgd == cur_rgd) {
			if (cur_rgd->rd_recent.next != head)
				rgd = list_entry(cur_rgd->rd_recent.next,
						 struct gfs2_rgrpd, rd_recent);
			else
				rgd = NULL;

			if (remove)
				list_del(&cur_rgd->rd_recent);

			goto out;
		}
	}

	rgd = NULL;
	if (!list_empty(head))
		rgd = list_entry(head->next, struct gfs2_rgrpd, rd_recent);

 out:
	spin_unlock(&sdp->sd_rindex_spin);

	RETURN(G2FN_RECENT_RGRP_NEXT, rgd);
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
recent_rgrp_add(struct gfs2_rgrpd *new_rgd)
{
	ENTER(G2FN_RECENT_RGRP_ADD)
	struct gfs2_sbd *sdp = new_rgd->rd_sbd;
	struct list_head *tmp, *head;
	struct gfs2_rgrpd *rgd = NULL;
	unsigned int count = 0;
	unsigned int max = sdp->sd_rgrps / gfs2_jindex_size(sdp);

	spin_lock(&sdp->sd_rindex_spin);

	for (head = &sdp->sd_rindex_recent_list, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		rgd = list_entry(tmp, struct gfs2_rgrpd, rd_recent);
		if (rgd == new_rgd)
			goto out;

		if (++count >= max)
			goto out;
	}
	list_add_tail(&new_rgd->rd_recent, &sdp->sd_rindex_recent_list);

 out:
	spin_unlock(&sdp->sd_rindex_spin);

	RET(G2FN_RECENT_RGRP_ADD);
}

/**
 * forward_rgrp_get - get an rgrp to try next from full list
 * @sdp: The GFS2 superblock
 *
 * Returns: The rgrp to try next
 */

static struct gfs2_rgrpd *
forward_rgrp_get(struct gfs2_sbd *sdp)
{
	ENTER(G2FN_FORWARD_RGRP_GET)
	struct gfs2_rgrpd *rgd;
	unsigned int journals = gfs2_jindex_size(sdp);
	unsigned int rg = 0, x;

	spin_lock(&sdp->sd_rindex_spin);

	rgd = sdp->sd_rindex_forward;
	if (!rgd) {
		if (sdp->sd_rgrps >= journals)
			rg = sdp->sd_rgrps *
				sdp->sd_jdesc->jd_jid /
				journals;

		for (x = 0, rgd = gfs2_rgrpd_get_first(sdp);
		     x < rg;
		     x++, rgd = gfs2_rgrpd_get_next(rgd))
			/* Do Nothing */;

		sdp->sd_rindex_forward = rgd;
	}

	spin_unlock(&sdp->sd_rindex_spin);

	RETURN(G2FN_FORWARD_RGRP_GET, rgd);
}

/**
 * forward_rgrp_set - set the forward rgrp pointer
 * @sdp: the filesystem
 * @rgd: The new forward rgrp
 *
 */

static void
forward_rgrp_set(struct gfs2_sbd *sdp, struct gfs2_rgrpd *rgd)
{
	ENTER(G2FN_FORWARD_RGRP_SET)
	spin_lock(&sdp->sd_rindex_spin);
	sdp->sd_rindex_forward = rgd;
	spin_unlock(&sdp->sd_rindex_spin);
	RET(G2FN_FORWARD_RGRP_SET);
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
get_local_rgrp(struct gfs2_inode *ip)
{
	ENTER(G2FN_GET_LOCAL_RGRP)
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_rgrpd *rgd, *begin = NULL;
	struct gfs2_alloc *al = ip->i_alloc;
	int flags = LM_FLAG_TRY;
	int skipped = 0;
	int loops = 0;
	int error;

	/* Try recently successful rgrps */

	rgd = recent_rgrp_first(sdp, ip->i_last_rg_alloc);

	while (rgd) {
		error = gfs2_glock_nq_init(rgd->rd_gl,
					  LM_ST_EXCLUSIVE, LM_FLAG_TRY,
					  &al->al_rgd_gh);
		switch (error) {
		case 0:
			if (try_rgrp_fit(rgd, al))
				goto out;
			gfs2_glock_dq_uninit(&al->al_rgd_gh);
			rgd = recent_rgrp_next(rgd, TRUE);
			break;

		case GLR_TRYFAILED:
			rgd = recent_rgrp_next(rgd, FALSE);
			break;

		default:
			RETURN(G2FN_GET_LOCAL_RGRP, error);
		}
	}

	/* Go through full list of rgrps */

	begin = rgd = forward_rgrp_get(sdp);

	for (;;) {
		error = gfs2_glock_nq_init(rgd->rd_gl,
					  LM_ST_EXCLUSIVE, flags,
					  &al->al_rgd_gh);
		switch (error) {
		case 0:
			if (try_rgrp_fit(rgd, al))
				goto out;
			gfs2_glock_dq_uninit(&al->al_rgd_gh);
			break;

		case GLR_TRYFAILED:
			skipped++;
			break;

		default:
			RETURN(G2FN_GET_LOCAL_RGRP, error);
		}

		rgd = gfs2_rgrpd_get_next(rgd);
		if (!rgd)
			rgd = gfs2_rgrpd_get_first(sdp);

		if (rgd == begin) {
			if (++loops >= 2 || !skipped)
				RETURN(G2FN_GET_LOCAL_RGRP, -ENOSPC);
			flags = 0;
		}
	}

 out:
	ip->i_last_rg_alloc = rgd->rd_ri.ri_addr;

	if (begin) {
		recent_rgrp_add(rgd);
		rgd = gfs2_rgrpd_get_next(rgd);
		if (!rgd)
			rgd = gfs2_rgrpd_get_first(sdp);
		forward_rgrp_set(sdp, rgd);
	}

	RETURN(G2FN_GET_LOCAL_RGRP, 0);
}

/**
 * gfs2_inplace_reserve_i - Reserve space in the filesystem
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
gfs2_inplace_reserve_i(struct gfs2_inode *ip,
		     char *file, unsigned int line)
{
	ENTER(G2FN_INPLACE_RESERVE_I)
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_alloc *al = ip->i_alloc;
	int error;

        if (gfs2_assert_warn(sdp, al->al_requested))
		RETURN(G2FN_INPLACE_RESERVE_I, -EINVAL);

	error = gfs2_rindex_hold(sdp, &al->al_ri_gh);
	if (error)
		RETURN(G2FN_INPLACE_RESERVE_I, error);

	error = get_local_rgrp(ip);
	if (error) {
		gfs2_glock_dq_uninit(&al->al_ri_gh);
		RETURN(G2FN_INPLACE_RESERVE_I, error);
	}

	al->al_file = file;
	al->al_line = line;

	RETURN(G2FN_INPLACE_RESERVE_I, 0);
}

/**
 * gfs2_inplace_release - release an inplace reservation
 * @ip: the inode the reservation was taken out on
 *
 * Release a reservation made by gfs2_inplace_reserve().
 */

void
gfs2_inplace_release(struct gfs2_inode *ip)
{
	ENTER(G2FN_INPLACE_RELEASE)
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_alloc *al = ip->i_alloc;

	if (gfs2_assert_warn(sdp, al->al_alloced <= al->al_requested) == -1)
		printk("GFS2: fsid=%s: al_alloced = %u, al_requested = %u\n"
		       "GFS2: fsid=%s: al_file = %s, al_line = %u\n",
		       sdp->sd_fsname, al->al_alloced, al->al_requested,
		       sdp->sd_fsname, al->al_file, al->al_line);

	al->al_rgd = NULL;
	gfs2_glock_dq_uninit(&al->al_rgd_gh);
	gfs2_glock_dq_uninit(&al->al_ri_gh);

	RET(G2FN_INPLACE_RELEASE);
}

/**
 * gfs2_get_block_type - Check a block in a RG is of given type
 * @rgd: the resource group holding the block
 * @block: the block number
 *
 * Returns: The block type (GFS2_BLKST_*)
 */

unsigned char
gfs2_get_block_type(struct gfs2_rgrpd *rgd, uint64_t block)
{
	ENTER(G2FN_GET_BLOCK_TYPE)
	struct gfs2_bitmap *bi = NULL;
	uint32_t length, rgrp_block, buf_block;
	unsigned int buf;
	unsigned char type;

	length = rgd->rd_ri.ri_length;
	rgrp_block = block - rgd->rd_ri.ri_data0;

	for (buf = 0; buf < length; buf++) {
		bi = rgd->rd_bits + buf;
		if (rgrp_block < (bi->bi_start + bi->bi_len) * GFS2_NBBY)
			break;
	}

	gfs2_assert(rgd->rd_sbd, buf < length,);
	buf_block = rgrp_block - bi->bi_start * GFS2_NBBY;

	type = gfs2_testbit(rgd,
			   bi->bi_bh->b_data + bi->bi_offset,
			   bi->bi_len, buf_block);

	RETURN(G2FN_GET_BLOCK_TYPE, type);
}

/**
 * rgblk_search - find a block in @old_state, change allocation
 *           state to @new_state
 * @rgd: the resource group descriptor
 * @goal: the goal block within the RG (start here to search for avail block)
 * @old_state: GFS2_BLKST_XXX the before-allocation state to find
 * @new_state: GFS2_BLKST_XXX the after-allocation block state
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
rgblk_search(struct gfs2_rgrpd *rgd,
	     uint32_t goal,
	     unsigned char old_state, unsigned char new_state)
{
	ENTER(G2FN_RGBLK_SEARCH)
	struct gfs2_bitmap *bi = NULL;
	uint32_t length = rgd->rd_ri.ri_length;
	uint32_t blk = 0;
	unsigned int buf, x;

	/* Find bitmap block that contains bits for goal block */
	for (buf = 0; buf < length; buf++) {
		bi = rgd->rd_bits + buf;
		if (goal < (bi->bi_start + bi->bi_len) * GFS2_NBBY)
			break;
	}

	gfs2_assert(rgd->rd_sbd, buf < length,);

	/* Convert scope of "goal" from rgrp-wide to within found bit block */
	goal -= bi->bi_start * GFS2_NBBY;

	/* Search (up to entire) bitmap in this rgrp for allocatable block.
	   "x <= length", instead of "x < length", because we typically start
	   the search in the middle of a bit block, but if we can't find an
	   allocatable block anywhere else, we want to be able wrap around and
	   search in the first part of our first-searched bit block.  */
	for (x = 0; x <= length; x++) {
		if (bi->bi_clone)
			blk = gfs2_bitfit(rgd,
					  bi->bi_clone + bi->bi_offset,
					  bi->bi_len, goal, old_state);
		else
			blk = gfs2_bitfit(rgd,
					  bi->bi_bh->b_data + bi->bi_offset,
					  bi->bi_len, goal, old_state);
		if (blk != BFITNOENT)
			break;

		/* Try next bitmap block (wrap back to rgrp header if at end) */
		buf = (buf + 1) % length;
		bi = rgd->rd_bits + buf;
		goal = 0;
	}

	if (gfs2_assert_withdraw(rgd->rd_sbd, x <= length))
		blk = 0;

	/* Attach bitmap buffer to trans, modify bits to do block alloc */
	gfs2_trans_add_bh(rgd->rd_gl, bi->bi_bh);
	gfs2_setbit(rgd,
		    bi->bi_bh->b_data + bi->bi_offset,
		    bi->bi_len, blk, new_state);
	if (bi->bi_clone)
		gfs2_setbit(rgd,
			    bi->bi_clone + bi->bi_offset,
			    bi->bi_len, blk, new_state);

	/* Return allocated block #, rgrp scope (32-bit) */
	RETURN(G2FN_RGBLK_SEARCH, bi->bi_start * GFS2_NBBY + blk);
}

/**
 * rgblk_free - Change alloc state of given block(s)
 * @sdp: the filesystem
 * @bstart: first block (64-bit filesystem scope) of a run of contiguous blocks
 * @blen: the length of the block run (all must lie within ONE RG!)
 * @new_state: GFS2_BLKST_XXX the after-allocation block state
 *
 * Returns:  Resource group containing the block(s)
 *
 * Find rgrp containing @bstart.
 * For each block in run:
 *   Find allocation bitmap buffer.
 *   Add bitmap buffer to transaction.
 *   Set bits to new state.
 * Typically used to free blocks to GFS2_BLKST_FREE or GFS2_BLKST_FREEMETA,
 *   but @new_state can be any GFS2_BLKST_XXX
 * 
 */

static struct gfs2_rgrpd *
rgblk_free(struct gfs2_sbd *sdp, uint64_t bstart, uint32_t blen,
	   unsigned char new_state)
{
	ENTER(G2FN_RGBLK_FREE)
	struct gfs2_rgrpd *rgd;
	struct gfs2_bitmap *bi = NULL;
	uint32_t length, rgrp_blk, buf_blk;
	unsigned int buf;

	/* Find rgrp */
	rgd = gfs2_blk2rgrpd(sdp, bstart);
	if (!rgd) {
		if (gfs2_consist(sdp))
			printk("GFS2: fsid=%s: block = %"PRIu64"\n",
			       sdp->sd_fsname, bstart);
		RETURN(G2FN_RGBLK_FREE, NULL);
	}

	length = rgd->rd_ri.ri_length;

	/* Convert blk # from filesystem scope (64-bit) to RG scope (32-bit) */
	rgrp_blk = bstart - rgd->rd_ri.ri_data0;

	while (blen--) {
		/* Find bitmap buffer for this block */
		for (buf = 0; buf < length; buf++) {
			bi = rgd->rd_bits + buf;
			if (rgrp_blk < (bi->bi_start + bi->bi_len) * GFS2_NBBY)
				break;
		}

		gfs2_assert(rgd->rd_sbd, buf < length,);

		/* Find bits and set 'em */
		buf_blk = rgrp_blk - bi->bi_start * GFS2_NBBY;
		rgrp_blk++;

		if (!bi->bi_clone) {
			bi->bi_clone = kmalloc_nofail(bi->bi_bh->b_size,
						      GFP_KERNEL);
			memcpy(bi->bi_clone + bi->bi_offset,
			       bi->bi_bh->b_data + bi->bi_offset,
			       bi->bi_len);
		}
		gfs2_trans_add_bh(rgd->rd_gl, bi->bi_bh);
		gfs2_setbit(rgd,
			    bi->bi_bh->b_data + bi->bi_offset,
			    bi->bi_len, buf_blk, new_state);
	}

	RETURN(G2FN_RGBLK_FREE, rgd);
}

/**
 * gfs2_alloc_data - Allocate a data block
 * @ip: the inode to allocate the data block for
 *
 * Returns: the allocated block
 */

uint64_t
gfs2_alloc_data(struct gfs2_inode *ip)
{
	ENTER(G2FN_ALLOC_DATA)
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_alloc *al = ip->i_alloc;
	struct gfs2_rgrpd *rgd = al->al_rgd;
	uint32_t goal, blk;
	uint64_t block;

	if (rgrp_contains_block(&rgd->rd_ri, ip->i_di.di_goal_data))
		goal = ip->i_di.di_goal_data - rgd->rd_ri.ri_data0;
	else
		goal = rgd->rd_last_alloc_data;

	blk = rgblk_search(rgd, goal,
			   GFS2_BLKST_FREE, GFS2_BLKST_USED);
	rgd->rd_last_alloc_data = blk;

	block = rgd->rd_ri.ri_data0 + blk;
	ip->i_di.di_goal_data = block;

	gfs2_assert_withdraw(sdp, rgd->rd_rg.rg_free);
	rgd->rd_rg.rg_free--;

	gfs2_trans_add_bh(rgd->rd_gl, rgd->rd_bits[0].bi_bh);
	gfs2_rgrp_out(&rgd->rd_rg, rgd->rd_bits[0].bi_bh->b_data);

	al->al_alloced++;

	gfs2_quota_change(ip, +1, ip->i_di.di_uid, ip->i_di.di_gid);

	spin_lock(&sdp->sd_rindex_spin);
	rgd->rd_free_clone--;
	spin_unlock(&sdp->sd_rindex_spin);

	RETURN(G2FN_ALLOC_DATA, block);
}

/**
 * gfs2_alloc_meta - Allocate a metadata block
 * @ip: the inode to allocate the metadata block for
 *
 * Returns: the allocated block
 */

uint64_t
gfs2_alloc_meta(struct gfs2_inode *ip)
{
	ENTER(G2FN_ALLOC_META)
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_alloc *al = ip->i_alloc;
	struct gfs2_rgrpd *rgd = al->al_rgd;
	uint32_t goal, blk;
	uint64_t block;

	if (rgrp_contains_block(&rgd->rd_ri, ip->i_di.di_goal_meta))
		goal = ip->i_di.di_goal_meta - rgd->rd_ri.ri_data0;
	else
		goal = rgd->rd_last_alloc_meta;

	blk = rgblk_search(rgd, goal,
			   GFS2_BLKST_FREE, GFS2_BLKST_USED);
	rgd->rd_last_alloc_meta = blk;

	block = rgd->rd_ri.ri_data0 + blk;
	ip->i_di.di_goal_meta = block;

	gfs2_assert_withdraw(sdp, rgd->rd_rg.rg_free);
	rgd->rd_rg.rg_free--;

	gfs2_trans_add_bh(rgd->rd_gl, rgd->rd_bits[0].bi_bh);
	gfs2_rgrp_out(&rgd->rd_rg, rgd->rd_bits[0].bi_bh->b_data);

	al->al_alloced++;

	gfs2_quota_change(ip, +1, ip->i_di.di_uid, ip->i_di.di_gid);
	gfs2_trans_add_unrevoke(sdp, block);

	spin_lock(&sdp->sd_rindex_spin);
	rgd->rd_free_clone--;
	spin_unlock(&sdp->sd_rindex_spin);

	RETURN(G2FN_ALLOC_META, block);
}

/**
 * gfs2_alloc_di - Allocate a dinode
 * @dip: the directory that the inode is going in
 *
 * Returns: the block allocated
 */

uint64_t
gfs2_alloc_di(struct gfs2_inode *dip)
{
	ENTER(G2FN_ALLOC_DI)
       	struct gfs2_sbd *sdp = dip->i_sbd;
	struct gfs2_alloc *al = dip->i_alloc;
	struct gfs2_rgrpd *rgd = al->al_rgd;
	uint32_t blk;
	uint64_t block;

	/* Alloc the dinode; 32-bit "blk" is block offset within rgrp */
	blk = rgblk_search(rgd, rgd->rd_last_alloc_meta,
			   GFS2_BLKST_FREE, GFS2_BLKST_DINODE);

	/* remember where we left off, for next time */
	rgd->rd_last_alloc_meta = blk;

	/* convert from rgrp scope (32-bit) to filesystem scope (64-bit) */
	block = rgd->rd_ri.ri_data0 + blk;

	gfs2_assert_withdraw(sdp, rgd->rd_rg.rg_free);
	rgd->rd_rg.rg_free--;
	rgd->rd_rg.rg_dinodes++;

	/* Attach rgrp header to trans, update freemeta and useddi stats */
	gfs2_trans_add_bh(rgd->rd_gl, rgd->rd_bits[0].bi_bh);
	gfs2_rgrp_out(&rgd->rd_rg, rgd->rd_bits[0].bi_bh->b_data);

	/* Update stats in in-place reservation struct */
	al->al_alloced++;

	gfs2_trans_add_unrevoke(sdp, block);

	spin_lock(&sdp->sd_rindex_spin);
	rgd->rd_free_clone--;
	spin_unlock(&sdp->sd_rindex_spin);

	RETURN(G2FN_ALLOC_DI, block);
}

/**
 * gfs2_free_data - free a contiguous run of data block(s)
 * @ip: the inode these blocks are being freed from
 * @bstart: first block (64-bit filesystem scope) of a run of contiguous blocks
 * @blen: the length of the block run (all must lie within ONE RG!)
 *
 * Bitmap-deallocate the blocks (to FREE data state), add bitmap blks to trans
 * Update rgrp alloc statistics in rgrp header, add rgrp header buf to trans
 * Update quotas, add to trans.
 */

void
gfs2_free_data(struct gfs2_inode *ip, uint64_t bstart, uint32_t blen)
{
	ENTER(G2FN_FREE_DATA)
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_rgrpd *rgd;

	rgd = rgblk_free(sdp, bstart, blen, GFS2_BLKST_FREE);
	if (!rgd)
		RET(G2FN_FREE_DATA);

	rgd->rd_rg.rg_free += blen;

	gfs2_trans_add_bh(rgd->rd_gl, rgd->rd_bits[0].bi_bh);
	gfs2_rgrp_out(&rgd->rd_rg, rgd->rd_bits[0].bi_bh->b_data);

	gfs2_trans_add_rg(rgd);

	gfs2_quota_change(ip, -(int64_t)blen,
			 ip->i_di.di_uid, ip->i_di.di_gid);

	RET(G2FN_FREE_DATA);
}

/**
 * gfs2_free_meta - free a contiguous run of data block(s)
 * @ip: the inode these blocks are being freed from
 * @bstart: first block (64-bit filesystem scope) of a run of contiguous blocks
 * @blen: the length of the block run (all must lie within ONE RG!)
 *
 * Bitmap-deallocate the blocks (to FREE data state), add bitmap blks to trans
 * Update rgrp alloc statistics in rgrp header, add rgrp header buf to trans
 * Update quotas, add to trans.
 */

void
gfs2_free_meta(struct gfs2_inode *ip, uint64_t bstart, uint32_t blen)
{
	ENTER(G2FN_FREE_META)
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_rgrpd *rgd;

	rgd = rgblk_free(sdp, bstart, blen, GFS2_BLKST_FREE);
	if (!rgd)
		RET(G2FN_FREE_META);

	rgd->rd_rg.rg_free += blen;

	gfs2_trans_add_bh(rgd->rd_gl, rgd->rd_bits[0].bi_bh);
	gfs2_rgrp_out(&rgd->rd_rg, rgd->rd_bits[0].bi_bh->b_data);

	gfs2_trans_add_rg(rgd);

	gfs2_quota_change(ip, -(int64_t)blen,
			 ip->i_di.di_uid, ip->i_di.di_gid);
	gfs2_buf_wipe(ip, bstart, blen);

	RET(G2FN_FREE_META);
}

void
gfs2_free_uninit_di(struct gfs2_rgrpd *rgd, uint64_t blkno)
{
	ENTER(G2FN_FREE_UNINIT_DI)
       	struct gfs2_sbd *sdp = rgd->rd_sbd;
	struct gfs2_rgrpd *tmp_rgd;

	tmp_rgd = rgblk_free(sdp, blkno, 1, GFS2_BLKST_FREE);
	if (!tmp_rgd)
		RET(G2FN_FREE_UNINIT_DI);
	gfs2_assert_withdraw(sdp, rgd == tmp_rgd);

	if (!rgd->rd_rg.rg_dinodes)
		gfs2_consist_rgrpd(rgd);
	rgd->rd_rg.rg_dinodes--;
	rgd->rd_rg.rg_free++;

	gfs2_trans_add_bh(rgd->rd_gl, rgd->rd_bits[0].bi_bh);
	gfs2_rgrp_out(&rgd->rd_rg, rgd->rd_bits[0].bi_bh->b_data);

	gfs2_trans_add_rg(rgd);

	RET(G2FN_FREE_UNINIT_DI);
}

/**
 * gfs2_free_uninit_di - free a dinode block
 * @rgd: the resource group that contains the dinode
 * @ip: the inode
 *
 * De-allocate the dinode to FREEMETA using block alloc bitmap.
 * Update rgrp's block usage statistics (used dinode--, free meta++).
 * Add rgrp header to transaction.
 */

void
gfs2_free_di(struct gfs2_rgrpd *rgd, struct gfs2_inode *ip)
{
	ENTER(G2FN_FREE_DI)
       	gfs2_free_uninit_di(rgd, ip->i_num.no_addr);
	gfs2_quota_change(ip, -1, ip->i_di.di_uid, ip->i_di.di_gid);
	gfs2_buf_wipe(ip, ip->i_num.no_addr, 1);
	RET(G2FN_FREE_DI);
}

/**
 * gfs2_rlist_add - add a RG to a list of RGs
 * @sdp: the filesystem
 * @rlist: the list of resource groups
 * @block: the block
 *
 * Figure out what RG a block belongs to and add that RG to the list
 *
 * FIXME: Don't use kmalloc_nofail()
 *
 */

void
gfs2_rlist_add(struct gfs2_sbd *sdp, struct gfs2_rgrp_list *rlist, uint64_t block)
{
	ENTER(G2FN_RLIST_ADD)
	struct gfs2_rgrpd *rgd;
	struct gfs2_rgrpd **tmp;
	unsigned int new_space;
	unsigned int x;

	if (gfs2_assert_warn(sdp, !rlist->rl_ghs))
		RET(G2FN_RLIST_ADD);

	rgd = gfs2_blk2rgrpd(sdp, block);
	if (!rgd) {
		if (gfs2_consist(sdp))
			printk("GFS2: fsid=%s: block = %"PRIu64"\n",
			       sdp->sd_fsname, block);
		RET(G2FN_RLIST_ADD);
	}

	for (x = 0; x < rlist->rl_rgrps; x++)
		if (rlist->rl_rgd[x] == rgd)
			RET(G2FN_RLIST_ADD);

	if (rlist->rl_rgrps == rlist->rl_space) {
		new_space = rlist->rl_space + 10;

		tmp = kmalloc_nofail(new_space * sizeof(struct gfs2_rgrpd *),
				     GFP_KERNEL);

		if (rlist->rl_rgd) {
			memcpy(tmp, rlist->rl_rgd,
			       rlist->rl_space * sizeof(struct gfs2_rgrpd *));
			kfree(rlist->rl_rgd);
		}

		rlist->rl_space = new_space;
		rlist->rl_rgd = tmp;
	}

	rlist->rl_rgd[rlist->rl_rgrps++] = rgd;

	RET(G2FN_RLIST_ADD);
}

/**
 * gfs2_rlist_alloc - all RGs have been added to the rlist, now allocate
 *      and initialize an array of glock holders for them
 * @rlist: the list of resource groups
 * @state: the lock state to acquire the RG lock in
 * @flags: the modifier flags for the holder structures
 *
 * FIXME: Don't use kmalloc_nofail()
 *
 */

void
gfs2_rlist_alloc(struct gfs2_rgrp_list *rlist, unsigned int state, int flags)
{
	ENTER(G2FN_RLIST_ALLOC)
	unsigned int x;

	rlist->rl_ghs = kmalloc_nofail(rlist->rl_rgrps *
				       sizeof(struct gfs2_holder),
				       GFP_KERNEL);
	for (x = 0; x < rlist->rl_rgrps; x++)
		gfs2_holder_init(rlist->rl_rgd[x]->rd_gl,
				state, flags,
				&rlist->rl_ghs[x]);

	RET(G2FN_RLIST_ALLOC);
}

/**
 * gfs2_rlist_free - free a resource group list
 * @list: the list of resource groups
 *
 */

void
gfs2_rlist_free(struct gfs2_rgrp_list *rlist)
{
	ENTER(G2FN_RLIST_FREE)
	unsigned int x;

	if (rlist->rl_rgd)
		kfree(rlist->rl_rgd);

	if (rlist->rl_ghs) {
		for (x = 0; x < rlist->rl_rgrps; x++)
			gfs2_holder_uninit(&rlist->rl_ghs[x]);
		kfree(rlist->rl_ghs);
	}

	RET(G2FN_RLIST_FREE);
}

