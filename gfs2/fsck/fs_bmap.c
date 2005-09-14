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

#include <inttypes.h>
#include <linux_endian.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"
#include "rgrp.h"
#include "fs_inode.h"
#include "bio.h"

#include "fs_bmap.h"

typedef struct metapath
{
	uint64_t              mp_list[GFS2_MAX_META_HEIGHT];
}metapath_t;


/**
 * fs_unstuff_dinode - Unstuff a dinode when the data has grown too big
 * @ip: The GFS2 inode to unstuff
 * * This routine unstuffs a dinode and returns it to a "normal" state such
 * that the height can be grown in the traditional way.
 *
 * Returns: 0 on success, -EXXXX on failure
 */
int fs_unstuff_dinode(struct fsck_inode *ip)
{
	struct fsck_sb *sdp = ip->i_sbd;
	struct buffer_head *bh = NULL;
	struct buffer_head *dibh = NULL;
	int journaled = fs_is_jdata(ip);
	uint64_t block = 0;
	int error;

	log_debug("Unstuffing inode %"PRIu64" - %u\n", ip->i_di.di_num.no_addr,
		  journaled);

	if(!fs_is_stuffed(ip)){
		log_err("Trying to unstuff a dinode that is already unstuffed.\n");
		return -1;
	}


	error = get_and_read_buf(sdp, ip->i_num.no_addr, &dibh, 0);
	if (error) {
		stack;
		goto fail;
	}

	error = check_meta(dibh, GFS2_METATYPE_DI);
	if(error) {
		stack;
		goto fail;
	}

	if (ip->i_di.di_size){
		log_err("Allocating new block for unstuffed dinode\n");
		if(journaled){
			error = fs_metaalloc(ip, &block);
			if (error) {
				stack;
				goto fail;
			}
			log_err("Got block %"PRIu64"\n", block);
			error = get_buf(sdp, block, &bh);
			if (error) {
				stack;
				goto fail;
			}

			set_meta(bh, GFS2_METATYPE_JD, GFS2_FORMAT_JD);

			memcpy(BH_DATA(bh)+sizeof(struct gfs2_meta_header),
			       BH_DATA(dibh)+sizeof(struct gfs2_dinode),
			       BH_SIZE(dibh)-sizeof(struct gfs2_dinode));

			error = write_buf(sdp, bh, 0);
			if(error) {
				stack;
				goto fail;
			}
			relse_buf(sdp, bh);
			block_set(sdp->bl, block, journal_blk);
		}
		else{
			error = fs_blkalloc(ip, &block);

			if(error) {
				stack;
				goto fail;
			}

			error = get_buf(sdp, block, &bh);
			if (error) {
				stack;
				goto fail;
			}

			memcpy(BH_DATA(bh)+sizeof(struct gfs2_meta_header),
			       BH_DATA(dibh)+sizeof(struct gfs2_dinode),
			       BH_SIZE(dibh)-sizeof(struct gfs2_dinode));

			error = write_buf(sdp, bh, 0);
			if(error) {
				stack;
				goto fail;
			}
			relse_buf(sdp, bh);
			block_set(sdp->bl, block, block_used);
		}
	}

	bh = NULL;
	/*  Set up the pointer to the new block  */

	memset(BH_DATA(dibh)+sizeof(struct gfs2_dinode), 0,
	       BH_SIZE(dibh)-sizeof(struct gfs2_dinode));

	if (ip->i_di.di_size){
		((uint64_t *)(BH_DATA(dibh) + sizeof(struct gfs2_dinode)))[0] = cpu_to_gfs2_64(block);
		ip->i_di.di_blocks++;
	}

	ip->i_di.di_height = 1;

	gfs2_dinode_out(&ip->i_di, BH_DATA(dibh));
	if(write_buf(sdp, dibh, 0)){
		log_err("Dinode unstuffed, but unable to write back dinode.\n");
		goto fail;
	}
	relse_buf(sdp, dibh);

	return 0;



 fail:
	if(bh) relse_buf(sdp, bh);
	if(dibh) relse_buf(sdp, dibh);

	return error;
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

static unsigned int calc_tree_height(struct fsck_inode *ip, uint64_t size)
{
	struct fsck_sb *sdp = ip->i_sbd;
	uint64_t *arr;
	unsigned int max, height;

	if (ip->i_di.di_size > size)
		size = ip->i_di.di_size;

	if (fs_is_jdata(ip)){
		arr = sdp->jheightsize;
		max = sdp->max_jheight;
	}
	else{
		arr = sdp->heightsize;
		max = sdp->max_height;
	}
	for (height = 0; height < max; height++)
		if (arr[height] >= size)
			break;

	return height;
}


/**
 * build_height - Build a metadata tree of the requested height
 * @ip: The GFS2 inode
 * @height: The height to build to
 *
 *
 * Returns: 0 on success, -EXXXX on failure
 */
static int build_height(struct fsck_inode *ip, int height)
{
	struct fsck_sb *sdp = ip->i_sbd;
	struct buffer_head *bh, *dibh;
	uint64_t block, *bp;
	unsigned int x;
	int new_block;
	int error;

	while (ip->i_di.di_height < height){
		error = get_and_read_buf(ip->i_sbd, ip->i_num.no_addr, &dibh, 0);
		if (error)
			goto fail;

		new_block = 0;
		bp = (uint64_t *)(BH_DATA(dibh) + sizeof(struct gfs2_dinode));
		for (x = 0; x < sdp->diptrs; x++, bp++)
			if (*bp){
				new_block = 1;
				break;
			}


		if (new_block){
			/*  Get a new block, fill it with the old direct pointers and write it out  */
			error = fs_metaalloc(ip, &block);
			if (error)
				goto fail_drelse;

			error = get_and_read_buf(sdp, block, &bh, 0);
			if (error)
				goto fail_drelse;

			set_meta(bh, GFS2_METATYPE_IN, GFS2_FORMAT_IN);
			/*
			  gfs2_buffer_copy_tail(bh, sizeof(struct gfs2_indirect),
			  dibh, sizeof(struct gfs2_dinode));
			*/
			log_err("ATTENTION -- Not doing copy_tail...\n");
			exit(1);
			error = -1;
			goto fail_drelse;
			if((error = write_buf(sdp, bh, 0))){
				log_err( "Unable to write new buffer #%"PRIu64".\n",
					BH_BLKNO(bh));
				goto fail_drelse;
			}
			relse_buf(sdp, bh);
		}


		/*  Set up the new direct pointer and write it out to disk  */

		memset(BH_DATA(dibh)+sizeof(struct gfs2_dinode), 0,
		       BH_SIZE(dibh)-sizeof(struct gfs2_dinode));

		if (new_block){
			((uint64_t *)(BH_DATA(dibh) + sizeof(struct gfs2_dinode)))[0] = cpu_to_gfs2_64(block);
			ip->i_di.di_blocks++;
		}

		ip->i_di.di_height++;

		gfs2_dinode_out(&ip->i_di, BH_DATA(dibh));
		write_buf(sdp, dibh, 0);
		relse_buf(sdp, dibh);
	}

	return 0;



 fail_drelse:
	relse_buf(sdp, dibh);

 fail:
	return error;
}


static void find_metapath(struct fsck_inode *ip, metapath_t *mp, uint64_t block)
{
	struct fsck_sb *sdp = ip->i_sbd;
	unsigned int i;

	for (i = ip->i_di.di_height; i--; ){
		mp->mp_list[i] = block % sdp->inptrs;
		block /= sdp->inptrs;
	}
}


/**
 * metapointer - Return pointer to start of metadata in a buffer
 * @bh: The buffer
 * @level: The metadata level (0 = dinode)
 * @mp: The metapath
 *
 * Return a pointer to the block number of the next level of the metadata
 * tree given a buffer containing the pointer to the current level of the
 * metadata tree.
 */

static uint64_t *metapointer(struct buffer_head *bh, unsigned int level, metapath_t *mp)
{
	int head_size = (level > 0) ? sizeof(struct gfs2_meta_header) : sizeof(struct gfs2_dinode);
	return ((uint64_t *)(BH_DATA(bh) + head_size)) + mp->mp_list[level];
}


/**
 * get_metablock - Get the next metadata block in metadata tree
 * @ip: The GFS2 inode
 * @bh: Buffer containing the pointers to metadata blocks
 * @level: The level of the tree (0 = dinode)
 * @mp: The metapath
 * @create: Non-zero if we may create a new meatdata block
 * @new: Used to indicate if we did create a new metadata block
 * @block: the returned disk block number
 *
 * Given a metatree, complete to a particular level, checks to see if the next
 * level of the tree exists. If not the next level of the tree is created.
 * The block number of the next level of the metadata tree is returned.
 *
 * Returns: 0 on success, -EXXX on failure
 */

static int get_metablock(struct fsck_inode *ip,
			 struct buffer_head *bh, unsigned int level, metapath_t *mp,
			 int create, int *new, uint64_t *block)
{
	uint64_t *ptr = metapointer(bh, level, mp);
	int error = 0;

	*new = 0;
	*block = 0;

	if (*ptr){
		*block = gfs2_64_to_cpu(*ptr);
		goto out;
	}

	if (!create)
		goto out;

	error = fs_metaalloc(ip, block);
	if (error)
		goto out;

	*ptr = cpu_to_gfs2_64(*block);
	ip->i_di.di_blocks++;
	write_buf(ip->i_sbd, bh, 0);

	*new = 1;

 out:
	return error;
}


/**
 * get_datablock - Get datablock number from metadata block
 * @rgd: rgrp to allocate from if necessary
 * @ip: The GFS2 inode
 * @bh: The buffer containing pointers to datablocks
 * @mp: The metapath
 * @create: Non-zero if we may create a new data block
 * @new: Used to indicate if we created a new data block
 * @block: the returned disk block number
 *
 * Given a fully built metadata tree, checks to see if a particular data
 * block exists. It is created if it does not exist and the block number
 * on disk is returned.
 *
 * Returns: 0 on success, -EXXX on failure
 */

static int get_datablock(struct fsck_inode *ip,
			 struct buffer_head *bh, metapath_t *mp,
			 int create, int *new, uint64_t *block)
{
	uint64_t *ptr = metapointer(bh, ip->i_di.di_height - 1, mp);
	int error = 0;

	*new = 0;
	*block = 0;


	if (*ptr){
		*block = gfs2_64_to_cpu(*ptr);
		goto out;
	}

	if (!create)
		goto out;

	if (fs_is_jdata(ip)){
		error = fs_metaalloc(ip, block);
		if (error)
			goto out;
	}
	else {
		error = fs_blkalloc(ip, block);
		if (error)
			goto out;
	}

	*ptr = cpu_to_gfs2_64(*block);
	ip->i_di.di_blocks++;
	write_buf(ip->i_sbd, bh, 0);

	*new = 1;

 out:
	return error;
}


/**
 * fs_block_map - Map a block from an inode to a disk block
 * @ip: The GFS2 inode
 * @lblock: The logical block number
 * @new: Value/Result argument (1 = may create/did create new blocks)
 * @dblock: the disk block number of the start of an extent
 * @extlen: the size of the extent
 *
 * Find the block number on the current device which corresponds to an
 * inode's block. If the block had to be created, "new" will be set.
 *
 * Returns: 0 on success, -EXXX on failure
 */
int fs_block_map(struct fsck_inode *ip, uint64_t lblock, int *new,
		 uint64_t *dblock, uint32_t *extlen)
{
	struct fsck_sb *sdp = ip->i_sbd;
	struct buffer_head *bh = NULL;
	metapath_t mp;
	int create = *new;
	unsigned int bsize;
	unsigned int height;
	unsigned int x, end_of_metadata;
	unsigned int nptrs;
	uint64_t tmp_dblock;
	int tmp_new;
	int error = 0;

	*new = 0;
	*dblock = 0;
	if (extlen)
		*extlen = 0;

	if (fs_is_stuffed(ip)){
		*dblock = ip->i_num.no_addr;
		if (extlen)
			*extlen = 1;
		goto out;
	}

	bsize = (fs_is_jdata(ip)) ? sdp->jbsize : sdp->bsize;

	height = calc_tree_height(ip, (lblock + 1) * bsize);
	if (ip->i_di.di_height < height){
		if (!create){
			error = 0;
			goto fail;
		}

		error = build_height(ip, height);
		if (error)
			goto fail;
	}


	error = get_and_read_buf(ip->i_sbd, ip->i_num.no_addr, &bh, 0);
	if (error)
		goto fail;


	find_metapath(ip, &mp, lblock);
	end_of_metadata = ip->i_di.di_height - 1;

	for (x = 0; x < end_of_metadata; x++){
		error = get_metablock(ip, bh, x, &mp, create, new, dblock);
		relse_buf(ip->i_sbd, bh); bh = NULL;
		if (error)
			goto fail;
		if (!*dblock)
			goto out;

		if (*new) {
			log_err("ATTENTION -- not handling new maps in fs_block_map...\n");
			error = -1;
			exit(1);
			/*struct gfs2_meta_header mh;
			bh = bget(sdp, *dblock);
			mh.mh_magic = GFS2_MAGIC;
			mh.mh_type = GFS2_METATYPE_IN;
			mh.mh_blkno = *dblock;
			mh.mh_format = GFS2_FORMAT_IN;
			gfs2_meta_header_out(&mh, bh->b_data);*/
		} else {
			error = get_and_read_buf(ip->i_sbd, *dblock, &bh, 0);
		}
		
		/*
		  error = gfs2_get_meta_buffer(ip, x + 1, *dblock, *new, &bh);
		*/
		if (error)
			goto fail;
	}


	error = get_datablock(ip, bh, &mp, create, new, dblock);
	if (error)
		goto fail_drelse;

	if (extlen && *dblock){
		*extlen = 1;

		if (!*new){
			nptrs = (end_of_metadata) ? sdp->inptrs : sdp->diptrs;
			while (++mp.mp_list[end_of_metadata] < nptrs){
				error = get_datablock(ip, bh, &mp, 0, &tmp_new,
						      &tmp_dblock);
				if(error){
					log_err( "Unable to perform get_datablock.\n");
					goto fail;
				}

				if (*dblock + *extlen != tmp_dblock)
					break;

				(*extlen)++;
			}
		}
	}


	relse_buf(sdp, bh);


 out:
	if (*new){
		error = get_and_read_buf(sdp, ip->i_num.no_addr, &bh, 0);
		if (error)
			goto fail;
		gfs2_dinode_out(&ip->i_di, BH_DATA(bh));
		write_buf(sdp, bh, 0);
		relse_buf(sdp, bh);
	}
	return 0;



 fail_drelse:
	if(bh)
		relse_buf(sdp, bh);

 fail:
	return error;
}
