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
#include "bmap.h"
#include "dio.h"
#include "glock.h"
#include "inode.h"
#include "quota.h"
#include "rgrp.h"
#include "trans.h"

struct metapath {
	unsigned int mp_list[GFS2_MAX_META_HEIGHT];
};

typedef int (*block_call_t) (struct gfs2_inode *ip, struct buffer_head *dibh,
			     struct buffer_head *bh, uint64_t *top,
			     uint64_t *bottom, unsigned int height,
			     void *data);

struct strip_mine {
	int sm_first;
	unsigned int sm_height;
};

/**
 * gfs2_unstuffer_sync - unstuff a dinode synchronously
 * @ip: the inode
 * @dibh: the dinode buffer
 * @block: the block number that was allocated
 * @private: not used
 *
 * Returns: errno
 */

int
gfs2_unstuffer_sync(struct gfs2_inode *ip, struct buffer_head *dibh,
		   uint64_t block, void *private)
{
	ENTER(G2FN_UNSTUFFER_SYNC)
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct buffer_head *bh;
	int error;

	error = gfs2_get_data_buffer(ip, block, TRUE, &bh);
	if (error)
		RETURN(G2FN_UNSTUFFER_SYNC, error);

	gfs2_buffer_copy_tail(bh, 0, dibh, sizeof(struct gfs2_dinode));

	error = gfs2_dwrite(sdp, bh, DIO_DIRTY | DIO_START | DIO_WAIT);

	brelse(bh);

	RETURN(G2FN_UNSTUFFER_SYNC, error);
}

/**
 * gfs2_unstuffer_async - unstuff a dinode asynchronously
 * @ip: the inode
 * @dibh: the dinode buffer
 * @block: the block number that was allocated
 * @private: not used
 *
 * Returns: errno
 */

int
gfs2_unstuffer_async(struct gfs2_inode *ip, struct buffer_head *dibh,
		    uint64_t block, void *private)
{
	ENTER(G2FN_UNSTUFFER_ASYNC)
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct buffer_head *bh;
	int error;

	error = gfs2_get_data_buffer(ip, block, TRUE, &bh);
	if (error)
		RETURN(G2FN_UNSTUFFER_ASYNC, error);

	gfs2_buffer_copy_tail(bh, 0, dibh, sizeof(struct gfs2_dinode));

	error = gfs2_dwrite(sdp, bh, DIO_DIRTY);

	brelse(bh);

	RETURN(G2FN_UNSTUFFER_ASYNC, error);
}

/**
 * gfs2_unstuff_dinode - Unstuff a dinode when the data has grown too big
 * @ip: The GFS2 inode to unstuff
 * @unstuffer: the routine that handles unstuffing a non-zero length file
 * @private: private data for the unstuffer
 *
 * This routine unstuffs a dinode and returns it to a "normal" state such 
 * that the height can be grown in the traditional way.
 *
 * Returns: errno
 */

int
gfs2_unstuff_dinode(struct gfs2_inode *ip, gfs2_unstuffer_t unstuffer,
		   void *private)
{
	ENTER(G2FN_UNSTUFF_DINODE)
	struct buffer_head *bh, *dibh;
	uint64_t block = 0;
	int journaled = gfs2_is_jdata(ip);
	int error;

	error = gfs2_get_inode_buffer(ip, &dibh);
	if (error)
		RETURN(G2FN_UNSTUFF_DINODE, error);
		
	if (ip->i_di.di_size) {
		/* Get a free block, fill it with the stuffed data,
		   and write it out to disk */

		if (journaled) {
			block = gfs2_alloc_meta(ip);

			error = gfs2_get_data_buffer(ip, block, TRUE, &bh);
			if (error)
				goto fail;

			gfs2_buffer_copy_tail(bh, sizeof(struct gfs2_meta_header),
					     dibh, sizeof(struct gfs2_dinode));

			brelse(bh);
		} else {
			block = gfs2_alloc_data(ip);

			error = unstuffer(ip, dibh, block, private);
			if (error)
				goto fail;
		}
	}

	/*  Set up the pointer to the new block  */

	gfs2_trans_add_bh(ip->i_gl, dibh);

	gfs2_buffer_clear_tail(dibh, sizeof(struct gfs2_dinode));

	if (ip->i_di.di_size) {
		*(uint64_t *)(dibh->b_data + sizeof(struct gfs2_dinode)) = cpu_to_gfs2_64(block);
		ip->i_di.di_blocks++;
	}

	ip->i_di.di_height = 1;

	gfs2_dinode_out(&ip->i_di, dibh->b_data);
	brelse(dibh);

	RETURN(G2FN_UNSTUFF_DINODE, 0);

 fail:
	brelse(dibh);

	RETURN(G2FN_UNSTUFF_DINODE, error);
}

/**
 * calc_tree_height - Calculate the height of a metadata tree
 * @ip: The GFS2 inode
 * @size: The proposed size of the file
 *
 * Work out how tall a metadata tree needs to be in order to accommodate a
 * file of a particular size. If size is less than the current size of
 * the inode, then the current size of the inode is used instead of the
 * supplied one.
 *
 * Returns: the height the tree should be
 */

static unsigned int
calc_tree_height(struct gfs2_inode *ip, uint64_t size)
{
	ENTER(G2FN_CALC_TREE_HEIGHT)
	struct gfs2_sbd *sdp = ip->i_sbd;
	uint64_t *arr;
	unsigned int max, height;

	if (ip->i_di.di_size > size)
		size = ip->i_di.di_size;

	if (gfs2_is_jdata(ip)) {
		arr = sdp->sd_jheightsize;
		max = sdp->sd_max_jheight;
	} else {
		arr = sdp->sd_heightsize;
		max = sdp->sd_max_height;
	}

	for (height = 0; height < max; height++)
		if (arr[height] >= size)
			break;

	RETURN(G2FN_CALC_TREE_HEIGHT, height);
}

/**
 * build_height - Build a metadata tree of the requested height
 * @ip: The GFS2 inode
 * @height: The height to build to
 *
 * This routine makes sure that the metadata tree is tall enough to hold
 * "size" bytes of data.
 *
 * Returns: errno
 */

static int
build_height(struct gfs2_inode *ip, int height)
{
	ENTER(G2FN_BUILD_HEIGHT)
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct buffer_head *bh, *dibh;
	uint64_t block = 0, *bp;
	unsigned int x;
	int new_block;
	int error;

	while (ip->i_di.di_height < height) {
		error = gfs2_get_inode_buffer(ip, &dibh);
		if (error)
			RETURN(G2FN_BUILD_HEIGHT, error);

		new_block = FALSE;
		bp = (uint64_t *)(dibh->b_data + sizeof(struct gfs2_dinode));
		for (x = 0; x < sdp->sd_diptrs; x++, bp++)
			if (*bp) {
				new_block = TRUE;
				break;
			}

		if (new_block) {
			/*  Get a new block, fill it with the old direct pointers,
			    and write it out  */

			block = gfs2_alloc_meta(ip);

			bh = gfs2_dgetblk(ip->i_gl, block);
			gfs2_prep_new_buffer(bh);

			gfs2_trans_add_bh(ip->i_gl, bh);
			gfs2_metatype_set(bh,
					 GFS2_METATYPE_IN,
					 GFS2_FORMAT_IN);
			memset(bh->b_data + sizeof(struct gfs2_meta_header),
			       0,
			       sizeof(struct gfs2_meta_header) -
			       sizeof(struct gfs2_meta_header));
			gfs2_buffer_copy_tail(bh, sizeof(struct gfs2_meta_header),
					     dibh, sizeof(struct gfs2_dinode));

			brelse(bh);
		}

		/*  Set up the new direct pointer and write it out to disk  */

		gfs2_trans_add_bh(ip->i_gl, dibh);

		gfs2_buffer_clear_tail(dibh, sizeof(struct gfs2_dinode));

		if (new_block) {
			*(uint64_t *)(dibh->b_data + sizeof(struct gfs2_dinode)) = cpu_to_gfs2_64(block);
			ip->i_di.di_blocks++;
		}

		ip->i_di.di_height++;

		gfs2_dinode_out(&ip->i_di, dibh->b_data);
		brelse(dibh);
	}

	RETURN(G2FN_BUILD_HEIGHT, 0);
}

/**
 * find_metapath - Find path through the metadata tree
 * @ip: The inode pointer
 * @mp: The metapath to return the result in
 * @block: The disk block to look up
 *
 *   This routine returns a struct metapath structure that defines a path through
 *   the metadata of inode "ip" to get to block "block".
 *
 *   Example:
 *   Given:  "ip" is a height 3 file, "offset" is 101342453, and this is a
 *   filesystem with a blocksize of 4096.
 *
 *   find_metapath() would return a struct metapath structure set to:
 *   mp_offset = 101342453, mp_height = 3, mp_list[0] = 0, mp_list[1] = 48,
 *   and mp_list[2] = 165.
 *
 *   That means that in order to get to the block containing the byte at
 *   offset 101342453, we would load the indirect block pointed to by pointer
 *   0 in the dinode.  We would then load the indirect block pointed to by
 *   pointer 48 in that indirect block.  We would then load the data block
 *   pointed to by pointer 165 in that indirect block.
 *
 *             ----------------------------------------
 *             | Dinode |                             |
 *             |        |                            4|
 *             |        |0 1 2 3 4 5                 9|
 *             |        |                            6|
 *             ----------------------------------------
 *                       |
 *                       |
 *                       V
 *             ----------------------------------------
 *             | Indirect Block                       |
 *             |                                     5|
 *             |            4 4 4 4 4 5 5            1|
 *             |0           5 6 7 8 9 0 1            2|
 *             ----------------------------------------
 *                                |
 *                                |
 *                                V
 *             ----------------------------------------
 *             | Indirect Block                       |
 *             |                         1 1 1 1 1   5|
 *             |                         6 6 6 6 6   1|
 *             |0                        3 4 5 6 7   2|
 *             ----------------------------------------
 *                                           |
 *                                           |
 *                                           V
 *             ----------------------------------------
 *             | Data block containing offset         |
 *             |            101342453                 |
 *             |                                      |
 *             |                                      |
 *             ----------------------------------------
 *
 */

static struct metapath *
find_metapath(struct gfs2_inode *ip, uint64_t block)
{
	ENTER(G2FN_FIND_METAPATH)
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct metapath *mp;
	uint64_t b = block;
	unsigned int i;

	mp = kmalloc_nofail(sizeof(struct metapath), GFP_KERNEL);
	memset(mp, 0, sizeof(struct metapath));

	for (i = ip->i_di.di_height; i--;)
		mp->mp_list[i] = do_div(b, sdp->sd_inptrs);

	RETURN(G2FN_FIND_METAPATH, mp);
}

/**
 * metapointer - Return pointer to start of metadata in a buffer
 * @bh: The buffer
 * @height: The metadata height (0 = dinode)
 * @mp: The metapath 
 *
 * Return a pointer to the block number of the next height of the metadata
 * tree given a buffer containing the pointer to the current height of the
 * metadata tree.
 */

static __inline__ uint64_t *
metapointer(struct buffer_head *bh, unsigned int height, struct metapath *mp)
{
	unsigned int head_size = (height > 0) ?
		sizeof(struct gfs2_meta_header) : sizeof(struct gfs2_dinode);

	return ((uint64_t *)(bh->b_data + head_size)) + mp->mp_list[height];
}

/**
 * lookup_block - Get the next metadata block in metadata tree
 * @ip: The GFS2 inode
 * @bh: Buffer containing the pointers to metadata blocks
 * @height: The height of the tree (0 = dinode)
 * @mp: The metapath
 * @create: Non-zero if we may create a new meatdata block
 * @new: Used to indicate if we did create a new metadata block
 * @block: the returned disk block number
 *
 * Given a metatree, complete to a particular height, checks to see if the next
 * height of the tree exists. If not the next height of the tree is created.
 * The block number of the next height of the metadata tree is returned.
 *
 */

static void
lookup_block(struct gfs2_inode *ip,
	     struct buffer_head *bh, unsigned int height, struct metapath *mp,
	     int create, int *new, uint64_t *block)
{
	ENTER(G2FN_LOOKUP_BLOCK)
	uint64_t *ptr = metapointer(bh, height, mp);

	if (*ptr) {
		*block = gfs2_64_to_cpu(*ptr);
		RET(G2FN_LOOKUP_BLOCK);
	}

	*block = 0;

	if (!create)
		RET(G2FN_LOOKUP_BLOCK);

	if (height == ip->i_di.di_height - 1 &&
	    !gfs2_is_jdata(ip))
		*block = gfs2_alloc_data(ip);
	else
		*block = gfs2_alloc_meta(ip);

	gfs2_trans_add_bh(ip->i_gl, bh);

	*ptr = cpu_to_gfs2_64(*block);
	ip->i_di.di_blocks++;

	*new = 1;

	RET(G2FN_LOOKUP_BLOCK);
}

/**
 * gfs2_block_map - Map a block from an inode to a disk block
 * @ip: The GFS2 inode
 * @lblock: The logical block number
 * @new: Value/Result argument (1 = may create/did create new blocks)
 * @dblock: the disk block number of the start of an extent
 * @extlen: the size of the extent
 *
 * Find the block number on the current device which corresponds to an
 * inode's block. If the block had to be created, "new" will be set.
 *
 * Returns: errno
 */

int
gfs2_block_map(struct gfs2_inode *ip,
	      uint64_t lblock, int *new,
	      uint64_t *dblock, uint32_t *extlen)
{
	ENTER(G2FN_BLOCK_MAP)
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct buffer_head *bh;
	struct metapath *mp;
	int create = *new;
	unsigned int bsize;
	unsigned int height;
	unsigned int end_of_metadata;
	unsigned int x;
	int error;

	*new = 0;
	*dblock = 0;
	if (extlen)
		*extlen = 0;

	if (gfs2_is_stuffed(ip)) {
		if (!lblock) {
			*dblock = ip->i_num.no_addr;
			if (extlen)
				*extlen = 1;
		}
		RETURN(G2FN_BLOCK_MAP, 0);
	}

	bsize = (gfs2_is_jdata(ip)) ? sdp->sd_jbsize : sdp->sd_sb.sb_bsize;

	height = calc_tree_height(ip, (lblock + 1) * bsize);
	if (ip->i_di.di_height < height) {
		if (!create)
			RETURN(G2FN_BLOCK_MAP, 0);

		error = build_height(ip, height);
		if (error)
			RETURN(G2FN_BLOCK_MAP, error);
	}

	mp = find_metapath(ip, lblock);
	end_of_metadata = ip->i_di.di_height - 1;

	error = gfs2_get_inode_buffer(ip, &bh);
	if (error)
		goto out;

	for (x = 0; x < end_of_metadata; x++) {
		lookup_block(ip, bh, x, mp, create, new, dblock);
		brelse(bh);
		if (!*dblock)
			goto out;

		error = gfs2_get_meta_buffer(ip, x + 1, *dblock, *new, &bh);
		if (error)
			goto out;
	}

	lookup_block(ip, bh, end_of_metadata, mp, create, new, dblock);

	if (extlen && *dblock) {
		*extlen = 1;

		if (!*new) {
			uint64_t tmp_dblock;
			int tmp_new;
			unsigned int nptrs;

			nptrs = (end_of_metadata) ? sdp->sd_inptrs : sdp->sd_diptrs;

			while (++mp->mp_list[end_of_metadata] < nptrs) {
				lookup_block(ip, bh, end_of_metadata, mp,
					     FALSE, &tmp_new,
					     &tmp_dblock);

				if (*dblock + *extlen != tmp_dblock)
					break;

				(*extlen)++;
			}
		}
	}

	brelse(bh);

	if (*new) {
		error = gfs2_get_inode_buffer(ip, &bh);
		if (!error) {
			gfs2_trans_add_bh(ip->i_gl, bh);
			gfs2_dinode_out(&ip->i_di, bh->b_data);
			brelse(bh);
		}
	}

 out:
	kfree(mp);

	RETURN(G2FN_BLOCK_MAP, error);
}

/**
 * do_grow - Make a file look bigger than it is
 * @ip: the inode
 * @size: the size to set the file to
 *
 * Called with an exclusive lock on @ip.
 *
 * Returns: errno
 */

static int
do_grow(struct gfs2_inode *ip, uint64_t size)
{
	ENTER(G2FN_DO_GROW)
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_alloc *al;
	struct buffer_head *dibh;
	unsigned int h;
	int error;

	al = gfs2_alloc_get(ip);

	error = gfs2_quota_lock(ip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
	if (error)
		goto out;

	error = gfs2_quota_check(ip, ip->i_di.di_uid, ip->i_di.di_gid);
	if (error)
		goto out_gunlock_q;

	al->al_requested = sdp->sd_max_height + RES_DATA;

	error = gfs2_inplace_reserve(ip);
	if (error)
		goto out_gunlock_q;

	error = gfs2_trans_begin(sdp,
				sdp->sd_max_height + al->al_rgd->rd_ri.ri_length +
				RES_JDATA + RES_DINODE + RES_QUOTA, 0);
	if (error)
		goto out_ipres;

	if (size > sdp->sd_sb.sb_bsize - sizeof(struct gfs2_dinode)) {
		if (gfs2_is_stuffed(ip)) {
			error = gfs2_unstuff_dinode(ip, gfs2_unstuffer_sync, NULL);
			if (error)
				goto out_end_trans;
		}

		h = calc_tree_height(ip, size);
		if (ip->i_di.di_height < h) {
			error = build_height(ip, h);
			if (error)
				goto out_end_trans;
		}
	}

	ip->i_di.di_size = size;
	ip->i_di.di_mtime = ip->i_di.di_ctime = get_seconds();

	error = gfs2_get_inode_buffer(ip, &dibh);
	if (error)
		goto out_end_trans;

	gfs2_trans_add_bh(ip->i_gl, dibh);
	gfs2_dinode_out(&ip->i_di, dibh->b_data);
	brelse(dibh);

 out_end_trans:
	gfs2_trans_end(sdp);

 out_ipres:
	gfs2_inplace_release(ip);

 out_gunlock_q:
	gfs2_quota_unlock(ip);

 out:
	gfs2_alloc_put(ip);

	RETURN(G2FN_DO_GROW, error);
}

/**
 * recursive_scan - recursively scan through the end of a file
 * @ip: the inode
 * @dibh: the dinode buffer
 * @mp: the path through the metadata to the point to start
 * @height: the height the recursion is at
 * @block: the indirect block to look at
 * @first: TRUE if this is the first block
 * @bc: the call to make for each piece of metadata
 * @data: data opaque to this function to pass to @bc
 *
 * When this is first called @height and @block should be zero and
 * @first should be TRUE.
 *
 * Returns: errno
 */

static int
recursive_scan(struct gfs2_inode *ip, struct buffer_head *dibh,
	       struct metapath *mp, unsigned int height, uint64_t block,
	       int first, block_call_t bc, void *data)
{
	ENTER(G2FN_RECURSIVE_SCAN)
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct buffer_head *bh = NULL;
	uint64_t *top, *bottom;
	uint64_t bn;
	int error;

	if (!height) {
		error = gfs2_get_inode_buffer(ip, &bh);
		if (error)
			goto fail;
		dibh = bh;

		top = (uint64_t *)(bh->b_data + sizeof(struct gfs2_dinode)) +
			mp->mp_list[0];
		bottom = (uint64_t *)(bh->b_data + sizeof(struct gfs2_dinode)) +
			sdp->sd_diptrs;
	} else {
		error = gfs2_get_meta_buffer(ip, height, block, FALSE, &bh);
		if (error)
			goto fail;

		top = (uint64_t *)(bh->b_data + sizeof(struct gfs2_meta_header)) +
			((first) ? mp->mp_list[height] : 0);
		bottom = (uint64_t *)(bh->b_data + sizeof(struct gfs2_meta_header)) +
			sdp->sd_inptrs;
	}

	error = bc(ip, dibh, bh, top, bottom, height, data);
	if (error)
		goto fail;

	if (height < ip->i_di.di_height - 1)
		for (; top < bottom; top++, first = FALSE) {
			if (!*top)
				continue;

			bn = gfs2_64_to_cpu(*top);

			error = recursive_scan(ip, dibh, mp,
					       height + 1, bn, first,
					       bc, data);
			if (error)
				goto fail;
		}

	brelse(bh);

	RETURN(G2FN_RECURSIVE_SCAN, 0);

 fail:
	if (bh)
		brelse(bh);

	RETURN(G2FN_RECURSIVE_SCAN, error);
}

/**
 * do_strip - Look for a layer a particular layer of the file and strip it off
 * @ip: the inode
 * @dibh: the dinode buffer
 * @bh: A buffer of pointers
 * @top: The first pointer in the buffer
 * @bottom: One more than the last pointer
 * @height: the height this buffer is at
 * @data: a pointer to a struct strip_mine
 *
 * Returns: errno
 */

static int
do_strip(struct gfs2_inode *ip, struct buffer_head *dibh,
	 struct buffer_head *bh, uint64_t *top, uint64_t *bottom,
	 unsigned int height, void *data)
{
	ENTER(G2FN_DO_STRIP)
	struct strip_mine *sm = (struct strip_mine *)data;
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_rgrp_list rlist;
	uint64_t bn, bstart;
	uint32_t blen;
	uint64_t *p;
	unsigned int rg_blocks = 0;
	int metadata;
	unsigned int revokes = 0;
	int x;
	int error;

	if (!*top)
		sm->sm_first = FALSE;

	if (height != sm->sm_height)
		RETURN(G2FN_DO_STRIP, 0);

	if (sm->sm_first) {
		top++;
		sm->sm_first = FALSE;
	}

	metadata = (height != ip->i_di.di_height - 1) || gfs2_is_jdata(ip);
	if (metadata)
		revokes = (height) ? sdp->sd_inptrs : sdp->sd_diptrs;

	error = gfs2_rindex_hold(sdp, &ip->i_alloc->al_ri_gh);
	if (error)
		RETURN(G2FN_DO_STRIP, error);

	memset(&rlist, 0, sizeof(struct gfs2_rgrp_list));
	bstart = 0;
	blen = 0;

	for (p = top; p < bottom; p++) {
		if (!*p)
			continue;

		bn = gfs2_64_to_cpu(*p);

		if (bstart + blen == bn)
			blen++;
		else {
			if (bstart)
				gfs2_rlist_add(sdp, &rlist, bstart);

			bstart = bn;
			blen = 1;
		}
	}

	if (bstart)
		gfs2_rlist_add(sdp, &rlist, bstart);
	else
		goto out; /* Nothing to do */

	gfs2_rlist_alloc(&rlist, LM_ST_EXCLUSIVE, 0);

	for (x = 0; x < rlist.rl_rgrps; x++) {
		struct gfs2_rgrpd *rgd;
		rgd = gl2rgd(rlist.rl_ghs[x].gh_gl);
		rg_blocks += rgd->rd_ri.ri_length;
	}

	error = gfs2_glock_nq_m(rlist.rl_rgrps, rlist.rl_ghs);
	if (error)
		goto out_rlist;

	error = gfs2_trans_begin(sdp, rg_blocks + RES_DINODE +
				RES_INDIRECT + RES_QUOTA, revokes);
	if (error)
		goto out_rg_gunlock;

	gfs2_trans_add_bh(ip->i_gl, dibh);
	gfs2_trans_add_bh(ip->i_gl, bh);

	bstart = 0;
	blen = 0;

	for (p = top; p < bottom; p++) {
		if (!*p)
			continue;

		bn = gfs2_64_to_cpu(*p);

		if (bstart + blen == bn)
			blen++;
		else {
			if (bstart) {
				if (metadata)
					gfs2_free_meta(ip, bstart, blen);
				else
					gfs2_free_data(ip, bstart, blen);
			}

			bstart = bn;
			blen = 1;
		}

		*p = 0;
		if (!ip->i_di.di_blocks)
			gfs2_consist_inode(ip);
		ip->i_di.di_blocks--;
	}
	if (bstart) {
		if (metadata)
			gfs2_free_meta(ip, bstart, blen);
		else
			gfs2_free_data(ip, bstart, blen);
	}

	ip->i_di.di_mtime = ip->i_di.di_ctime = get_seconds();

	gfs2_dinode_out(&ip->i_di, dibh->b_data);

	gfs2_trans_end(sdp);

 out_rg_gunlock:
	gfs2_glock_dq_m(rlist.rl_rgrps, rlist.rl_ghs);

 out_rlist:
	gfs2_rlist_free(&rlist);

 out:
	gfs2_glock_dq_uninit(&ip->i_alloc->al_ri_gh);

	RETURN(G2FN_DO_STRIP, error);
}

/**
 * gfs2_truncator_default - truncate a partial data block
 * @ip: the inode
 * @size: the size the file should be
 *
 * Returns: errno
 */

int
gfs2_truncator_default(struct gfs2_inode *ip, uint64_t size)
{
	ENTER(G2FN_TRUNCATOR_DEFAULT)
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct buffer_head *bh;
	uint64_t bn;
	int not_new = 0;
	int error;

	error = gfs2_block_map(ip, size >> sdp->sd_sb.sb_bsize_shift, &not_new,
			      &bn, NULL);
	if (error)
		RETURN(G2FN_TRUNCATOR_DEFAULT, error);
	if (!bn)
		RETURN(G2FN_TRUNCATOR_DEFAULT, 0);

	error = gfs2_get_data_buffer(ip, bn, FALSE, &bh);
	if (error)
		RETURN(G2FN_TRUNCATOR_DEFAULT, error);

	gfs2_buffer_clear_tail(bh, size & (sdp->sd_sb.sb_bsize - 1));

	error = gfs2_dwrite(sdp, bh, DIO_DIRTY);

	brelse(bh);

	RETURN(G2FN_TRUNCATOR_DEFAULT, error);
}

/**
 * truncator_journaled - truncate a partial data block
 * @ip: the inode
 * @size: the size the file should be
 *
 * Returns: errno
 */

static int
truncator_journaled(struct gfs2_inode *ip, uint64_t size)
{
	ENTER(G2FN_TRUNCATOR_JOURNALED)
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct buffer_head *bh;
	uint64_t lbn, dbn;
	uint32_t off;
	int not_new = 0;
	int error;

	lbn = size;
	off = do_div(lbn, sdp->sd_jbsize);

	error = gfs2_block_map(ip, lbn, &not_new, &dbn, NULL);
	if (error)
		RETURN(G2FN_TRUNCATOR_JOURNALED, error);
	if (!dbn)
		RETURN(G2FN_TRUNCATOR_JOURNALED, 0);

	error = gfs2_trans_begin(sdp, RES_JDATA, 0);
	if (error)
		RETURN(G2FN_TRUNCATOR_JOURNALED, error);

	error = gfs2_get_data_buffer(ip, dbn, FALSE, &bh);
	if (!error) {
		gfs2_trans_add_bh(ip->i_gl, bh);
		gfs2_buffer_clear_tail(bh,
				      sizeof(struct gfs2_meta_header) +
				      off);
		brelse(bh);
	}

	gfs2_trans_end(sdp);

	RETURN(G2FN_TRUNCATOR_JOURNALED, error);
}

/**
 * gfs2_shrink - make a file smaller
 * @ip: the inode
 * @size: the size to make the file
 * @truncator: function to truncate the last partial block
 *
 * Called with an exclusive lock on @ip.
 *
 * Returns: errno
 */

int
gfs2_shrink(struct gfs2_inode *ip, uint64_t size, gfs2_truncator_t truncator)
{
	ENTER(G2FN_SHRINK)
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct buffer_head *dibh;
	uint64_t block;
	unsigned int height;
	int journaled = gfs2_is_jdata(ip);
	int error;

	if (!size)
		block = 0;
	else if (journaled) {
		block = size - 1;
		do_div(block, sdp->sd_jbsize);
	}
	else
		block = (size - 1) >> sdp->sd_sb.sb_bsize_shift;

	/*  Get rid of all the data/metadata blocks  */

	height = ip->i_di.di_height;
	if (height) {
		struct metapath *mp = find_metapath(ip, block);
		gfs2_alloc_get(ip);

		error = gfs2_quota_hold(ip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
		if (error) {
			gfs2_alloc_put(ip);
			kfree(mp);
			RETURN(G2FN_SHRINK, error);
		}

		while (height--) {
			struct strip_mine sm;

			sm.sm_first = (size) ? TRUE : FALSE;
			sm.sm_height = height;

			error = recursive_scan(ip, NULL, mp, 0, 0, TRUE,
					       do_strip, &sm);
			if (error) {
				gfs2_quota_unhold(ip);
				gfs2_alloc_put(ip);
				kfree(mp);
				RETURN(G2FN_SHRINK, error);
			}
		}

		gfs2_quota_unhold(ip);
		gfs2_alloc_put(ip);
		kfree(mp);
	}

	/*  If we truncated in the middle of a block, zero out the leftovers.  */

	if (gfs2_is_stuffed(ip)) {
		/*  Do nothing  */
	} else if (journaled) {
		if (do_mod(size, sdp->sd_jbsize)) {
			error = truncator_journaled(ip, size);
			if (error)
				RETURN(G2FN_SHRINK, error);
		}
	} else if (size & (uint64_t)(sdp->sd_sb.sb_bsize - 1)) {
		error = truncator(ip, size);
		if (error)
			RETURN(G2FN_SHRINK, error);
	}

	/*  Set the new size (and possibly the height)  */

	error = gfs2_trans_begin(sdp, RES_DINODE, 0);
	if (error)
		RETURN(G2FN_SHRINK, error);

	error = gfs2_get_inode_buffer(ip, &dibh);
	if (error)
		goto out;

	if (!size) {
		ip->i_di.di_height = 0;
		ip->i_di.di_goal_meta =
			ip->i_di.di_goal_data =
			ip->i_num.no_addr;
	}

	ip->i_di.di_size = size;
	ip->i_di.di_mtime = ip->i_di.di_ctime = get_seconds();

	gfs2_trans_add_bh(ip->i_gl, dibh);

	if (!ip->i_di.di_height &&
	    size < sdp->sd_sb.sb_bsize - sizeof(struct gfs2_dinode))
		gfs2_buffer_clear_tail(dibh, sizeof(struct gfs2_dinode) + size);

	gfs2_dinode_out(&ip->i_di, dibh->b_data);
	brelse(dibh);

 out:
	gfs2_trans_end(sdp);

	RETURN(G2FN_SHRINK, error);
}

/**
 * do_same - truncate to same size (update time stamps)
 * @ip: 
 *
 * Returns: errno
 */

static int
do_same(struct gfs2_inode *ip)
{
	ENTER(G2FN_DO_SAME)
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct buffer_head *dibh;
	int error;

	error = gfs2_trans_begin(sdp, RES_DINODE, 0);
	if (error)
		RETURN(G2FN_DO_SAME, error);

	error = gfs2_get_inode_buffer(ip, &dibh);
	if (error)
		goto out;

	ip->i_di.di_mtime = ip->i_di.di_ctime = get_seconds();

	gfs2_trans_add_bh(ip->i_gl, dibh);
	gfs2_dinode_out(&ip->i_di, dibh->b_data);

	brelse(dibh);

 out:
	gfs2_trans_end(sdp);

	RETURN(G2FN_DO_SAME, error);
}

/**
 * gfs2_truncatei - make a file a give size
 * @ip: the inode
 * @size: the size to make the file
 * @truncator: function to truncate the last partial block
 *
 * The file size can grow, shrink, or stay the same size.
 *
 * Returns: errno
 */

int
gfs2_truncatei(struct gfs2_inode *ip, uint64_t size,
	      gfs2_truncator_t truncator)
{
	ENTER(G2FN_TRUNCATEI)

       	if (gfs2_assert_warn(ip->i_sbd, S_ISREG(ip->i_di.di_mode)))
		RETURN(G2FN_TRUNCATEI, -EINVAL);

	if (size == ip->i_di.di_size)
		RETURN(G2FN_TRUNCATEI, do_same(ip));
	else if (size > ip->i_di.di_size)
		RETURN(G2FN_TRUNCATEI, do_grow(ip, size));
	else
		RETURN(G2FN_TRUNCATEI, gfs2_shrink(ip, size, truncator));
}

/**
 * gfs2_write_calc_reserv - calculate the number of blocks needed to write to a file
 * @ip: the file
 * @len: the number of bytes to be written to the file
 * @data_blocks: returns the number of data blocks required
 * @ind_blocks: returns the number of indirect blocks required
 *
 */

void
gfs2_write_calc_reserv(struct gfs2_inode *ip, unsigned int len,
		      unsigned int *data_blocks, unsigned int *ind_blocks)
{
	ENTER(G2FN_WRITE_CALC_RESERV)
	struct gfs2_sbd *sdp = ip->i_sbd;
	unsigned int tmp;

	if (gfs2_is_jdata(ip)) {
		*data_blocks = DIV_RU(len, sdp->sd_jbsize) + 2;
		*ind_blocks = 3 * (sdp->sd_max_jheight - 1);
	} else {
		*data_blocks = (len >> sdp->sd_sb.sb_bsize_shift) + 3;
		*ind_blocks = 3 * (sdp->sd_max_height - 1);
	}

	for (tmp = *data_blocks; tmp > sdp->sd_diptrs;) {
		tmp = DIV_RU(tmp, sdp->sd_inptrs);
		*ind_blocks += tmp;
	}

	RET(G2FN_WRITE_CALC_RESERV);
}

/**
 * gfs2_write_alloc_required - figure out if a write is going to require an allocation
 * @ip: the file being written to
 * @offset: the offset to write to
 * @len: the number of bytes being written
 * @alloc_required: the int is set to TRUE if an alloc is required, FALSE otherwise
 *
 * Returns: errno
 */

int
gfs2_write_alloc_required(struct gfs2_inode *ip,
			 uint64_t offset, unsigned int len,
			 int *alloc_required)
{
	ENTER(G2FN_WRITE_ALLOC_REQUIRED)
	struct gfs2_sbd *sdp = ip->i_sbd;
	uint64_t lblock, lblock_stop, dblock;
	uint32_t extlen;
	int not_new = FALSE;
	int error = 0;

	*alloc_required = FALSE;

	if (!len)
		RETURN(G2FN_WRITE_ALLOC_REQUIRED, 0);

	if (gfs2_is_stuffed(ip)) {
		if (offset + len > sdp->sd_sb.sb_bsize - sizeof(struct gfs2_dinode))
			*alloc_required = TRUE;
		RETURN(G2FN_WRITE_ALLOC_REQUIRED, 0);
	}

	if (gfs2_is_jdata(ip)) {
		unsigned int bsize = sdp->sd_jbsize;
		lblock = offset;
		do_div(lblock, bsize);
		lblock_stop = offset + len + bsize - 1;
		do_div(lblock_stop, bsize);
	} else {
		unsigned int shift = sdp->sd_sb.sb_bsize_shift;
		lblock = offset >> shift;
		lblock_stop = (offset + len + sdp->sd_sb.sb_bsize - 1) >> shift;
	}

	for (; lblock < lblock_stop; lblock += extlen) {
		error = gfs2_block_map(ip, lblock, &not_new, &dblock, &extlen);
		if (error)
			RETURN(G2FN_WRITE_ALLOC_REQUIRED, error);

		if (!dblock) {
			*alloc_required = TRUE;
			RETURN(G2FN_WRITE_ALLOC_REQUIRED, 0);
		}
	}

	RETURN(G2FN_WRITE_ALLOC_REQUIRED, 0);
}

/**
 * do_gfm - Copy out the dinode/indirect blocks of a file
 * @ip: the file
 * @dibh: the dinode buffer
 * @bh: the indirect buffer we're looking at
 * @top: the first pointer in the block
 * @bottom: one more than the last pointer in the block
 * @height: the height the block is at
 * @data: a pointer to a struct gfs2_user_buffer structure
 *
 * If this is a journaled file, copy out the data too.
 *
 * Returns: errno
 */

static int
do_gfm(struct gfs2_inode *ip, struct buffer_head *dibh,
       struct buffer_head *bh, uint64_t *top, uint64_t *bottom,
       unsigned int height, void *data)
{
	ENTER(G2FN_DO_GFM)
	struct gfs2_user_buffer *ub = (struct gfs2_user_buffer *)data;
	int error;

	error = gfs2_add_bh_to_ub(ub, bh);
	if (error)
		RETURN(G2FN_DO_GFM, error);

	if (!S_ISDIR(ip->i_di.di_mode) ||
	    height + 1 != ip->i_di.di_height)
		RETURN(G2FN_DO_GFM, 0);

	for (; top < bottom; top++)
		if (*top) {
			struct buffer_head *data_bh;

			error = gfs2_dread(ip->i_gl, gfs2_64_to_cpu(*top),
					  DIO_START | DIO_WAIT,
					  &data_bh);
			if (error)
				RETURN(G2FN_DO_GFM, error);

			error = gfs2_add_bh_to_ub(ub, data_bh);

			brelse(data_bh);

			if (error)
				RETURN(G2FN_DO_GFM, error);
		}

	RETURN(G2FN_DO_GFM, 0);
}

/**
 * gfs2_get_file_meta - return all the metadata for a file
 * @ip: the file
 * @ub: the structure representing the meta
 *
 * Returns: errno
 */

int
gfs2_get_file_meta(struct gfs2_inode *ip, struct gfs2_user_buffer *ub)
{
	ENTER(G2FN_GET_FILE_META)
	int error;

	if (gfs2_is_stuffed(ip)) {
		struct buffer_head *dibh;
		error = gfs2_get_inode_buffer(ip, &dibh);
		if (!error) {
			error = gfs2_add_bh_to_ub(ub, dibh);
			brelse(dibh);
		}
	} else {
		struct metapath *mp = find_metapath(ip, 0);
		error = recursive_scan(ip, NULL, mp, 0, 0, TRUE, do_gfm, ub);
		kfree(mp);
	}

	RETURN(G2FN_GET_FILE_META, error);
}
