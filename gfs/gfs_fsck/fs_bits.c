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

#include "util.h"
#include "bio.h"
#include "rgrp.h"


#include "fs_bits.h"
/**
 * fs_setbit - Set a bit in the bitmaps
 * @buffer: the buffer that holds the bitmaps
 * @buflen: the length (in bytes) of the buffer
 * @block: the block to set
 * @new_state: the new state of the block
 *
 */
static void fs_setbit(unsigned char *buffer, unsigned int buflen,
		      uint32_t block, unsigned char new_state)
{
	unsigned char *byte, *end, cur_state;
	unsigned int bit;

	byte = buffer + (block / GFS_NBBY);
	bit = (block % GFS_NBBY) * GFS_BIT_SIZE;
	end = buffer + buflen;

	if(byte >= end){
		fprintf(stderr,"fs_setbit:  byte >= end\n");
		exit(1);
	}
	cur_state = (*byte >> bit) & GFS_BIT_MASK;

	*byte ^= cur_state << bit;
	*byte |= new_state << bit;
}


/**
 * fs_bitfit - Find a free block in the bitmaps
 * @buffer: the buffer that holds the bitmaps
 * @buflen: the length (in bytes) of the buffer
 * @goal: the block to try to allocate
 * @old_state: the state of the block we're looking for
 *
 * Return: the block number that was allocated
 */
uint32_t fs_bitfit(unsigned char *buffer, unsigned int buflen,
		   uint32_t goal, unsigned char old_state)
{
	unsigned char *byte, *end, alloc;
	uint32_t blk = goal;
	unsigned int bit;


	byte = buffer + (goal / GFS_NBBY);
	bit = (goal % GFS_NBBY) * GFS_BIT_SIZE;
	end = buffer + buflen;
	alloc = (old_state & 1) ? 0 : 0x55;

	while (byte < end){
		if ((*byte & 0x55) == alloc){
			blk += (8 - bit) >> 1;

			bit = 0;
			byte++;

			continue;
		}

		if (((*byte >> bit) & GFS_BIT_MASK) == old_state){
			return blk;
		}

		bit += GFS_BIT_SIZE;
		if (bit >= 8){
			bit = 0;
			byte++;
		}

		blk++;
	}
	return BFITNOENT;
}

/**
 * fs_bitcount - count the number of bits in a certain state
 * @buffer: the buffer that holds the bitmaps
 * @buflen: the length (in bytes) of the buffer
 * @state: the state of the block we're looking for
 *
 * Returns: The number of bits
 */
uint32_t fs_bitcount(unsigned char *buffer, unsigned int buflen,
		     unsigned char state)
{
	unsigned char *byte, *end;
	unsigned int bit;
	uint32_t count = 0;

	byte = buffer;
	bit = 0;
	end = buffer + buflen;

	while (byte < end){
		if (((*byte >> bit) & GFS_BIT_MASK) == state)
			count++;

		bit += GFS_BIT_SIZE;
		if (bit >= 8){
			bit = 0;
			byte++;
		}
	}
	return count;
}


/**
 * fs_blkalloc_internal - allocate a single block
 * @rgd: the resource group descriptor
 * @goal: the goal block in the RG
 * @old_state: the type of block to find
 * @new_state: the resulting block type
 * @do_it: if FALSE, we just find the block we would allocate
 *
 *
 * Returns:  returns the block allocated, or BFITNOENT on failure
 */
uint32_t fs_blkalloc_internal(struct fsck_rgrp *rgd, uint32_t goal,
			      unsigned char old_state,
			      unsigned char new_state, int do_it)
{
	struct fsck_sb *sdp = rgd->rd_sbd;
	fs_bitmap_t *bits = NULL;
	uint32_t length = rgd->rd_ri.ri_length;
	uint32_t block = 0;
	unsigned int buf, x;

	goal = ((int)(goal - rgd->rd_ri.ri_data1) < 0) ? 0: goal - rgd->rd_ri.ri_data1;

	for (buf = 0; buf < length; buf++){
		bits = &rgd->rd_bits[buf];
		if (goal < (bits->bi_start + bits->bi_len) * GFS_NBBY)
			break;
	}

	if(buf >= length){
		/* ATTENTION */
		fprintf(stderr,
			"fs_blkalloc_internal:  goal is outside rgrp boundaries.\n");
		exit(1);
	}
	goal -= bits->bi_start * GFS_NBBY;


	/*  "x <= length" because we're skipping over some of the first
	    buffer when the goal is non-zero.  */

	for (x = 0; x <= length; x++){
		block = fs_bitfit(BH_DATA(rgd->rd_bh[buf]) + bits->bi_offset,
				  bits->bi_len, goal, old_state);
		if (block != BFITNOENT)
			break;
		buf = (buf + 1) % length;
		bits = &rgd->rd_bits[buf];
		goal = 0;
	}

	if(x > length){
		/* DEBUGGING */
		printf( "fs_blkalloc_internal:  No bits left in old_state?\n"
			"\told_state   = %u\n"
			"\tnew_state   = %u\n"
			"\trg_free     = %u\n"
			"\trg_freemeta = %u\n",
			old_state, new_state,
			rgd->rd_rg.rg_free,
			rgd->rd_rg.rg_freemeta);
		return BFITNOENT;
	}

	if (do_it){
		fs_setbit(BH_DATA(rgd->rd_bh[buf]) + bits->bi_offset,
			  bits->bi_len, block, new_state);
		write_buf(sdp, rgd->rd_bh[buf], 0);
	}

	return bits->bi_start * GFS_NBBY + block;
}


/*
 * fs_get_bitmap - get value of FS bitmap
 * @sdp: super block
 * @blkno: block number relative to file system
 *
 * This function gets the value of a bit of the
 * file system bitmap.
 * Possible state values for a block in the bitmap are:
 *  GFS_BLKST_FREE     (0)
 *  GFS_BLKST_USED     (1)
 *  GFS_BLKST_FREEMETA (2)
 *  GFS_BLKST_USEDMETA (3)
 *
 * Returns: state on success, -1 on error
 */
int fs_get_bitmap(struct fsck_sb *sdp, uint64 blkno, struct fsck_rgrp *rgd){
	int           buf, val;
	uint32_t        rgrp_block;
/*  struct fsck_rgrp	*rgd;*/
	fs_bitmap_t	*bits = NULL;
	unsigned int  bit;
	unsigned char *byte;
	int local_rgd = 0;

	if(check_range(sdp, blkno)){
		printf( "Block #%"PRIu64" is out of range.\n", blkno);
		return -1;
	}
	if(rgd == NULL) {
		local_rgd = 1;
		rgd = fs_blk2rgrpd(sdp, blkno);
	}
	if(rgd == NULL){
		fprintf(stderr, "Unable to get rgrp for block #%"PRIu64"\n", blkno);
		return -1;
	}
	if(fs_rgrp_read(rgd)){
		fprintf(stderr, "Unable to read rgrp.\n");
		return -1;
	}

	rgrp_block = (uint32_t)(blkno - rgd->rd_ri.ri_data1);

	for(buf= 0; buf < rgd->rd_ri.ri_length; buf++){
		bits = &(rgd->rd_bits[buf]);
		if(rgrp_block < ((bits->bi_start + bits->bi_len)*GFS_NBBY)){
			break;
		}
	}

	if(buf >= rgd->rd_ri.ri_length){
		fprintf(stderr, "Unable to locate bitmap entry for block #%"PRIu64"\n",
			blkno);
		fs_rgrp_relse(rgd);
		return -1;
	}

	byte = (BH_DATA(rgd->rd_bh[buf]) + bits->bi_offset) +
		(rgrp_block/GFS_NBBY - bits->bi_start);
	bit = (rgrp_block % GFS_NBBY) * GFS_BIT_SIZE;

	val = ((*byte >> bit) & GFS_BIT_MASK);
	if(local_rgd) {
		fs_rgrp_relse(rgd);
	}

	return val;
}


/*
 * fs_set_bitmap
 * @sdp: super block
 * @blkno: block number relative to file system
 * @state: one of 4 possible states
 *
 * This function sets the value of a bit of the
 * file system bitmap.
 *
 * Returns: 0 on success, -1 on error
 */
int fs_set_bitmap(struct fsck_sb *sdp, uint64 blkno, int state){
	int           buf;
	uint32_t        rgrp_block;
	fs_bitmap_t	*bits = NULL;
	struct fsck_rgrp	*rgd;

	if((state != GFS_BLKST_FREE) && (state != GFS_BLKST_USED) &&
	   (state != GFS_BLKST_FREEMETA) && (state != GFS_BLKST_USEDMETA)){
		return -1;
	}

	rgd = fs_blk2rgrpd(sdp, blkno);

	if(fs_rgrp_read(rgd))
		return -1;
	rgrp_block = (uint32_t)(blkno - rgd->rd_ri.ri_data1);
	for(buf= 0; buf < rgd->rd_ri.ri_length; buf++){
		bits = &(rgd->rd_bits[buf]);
		if(rgrp_block < ((bits->bi_start + bits->bi_len)*GFS_NBBY)){
			break;
		}
	}

	fs_setbit(BH_DATA(rgd->rd_bh[buf]) + bits->bi_offset,
		  bits->bi_len,
		  (rgrp_block - (bits->bi_start*GFS_NBBY)),
		  state);


	if(write_buf(sdp, rgd->rd_bh[buf], 0)){
		fs_rgrp_relse(rgd);
		return -1;
	}

	fs_rgrp_relse(rgd);
	return 0;
}
