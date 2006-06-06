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
#include <stdlib.h>
#include <unistd.h>

#include "util.h"
#include "libgfs2.h"
#include "fs_bits.h"
#include "log.h"
#include "block_list.h"

/**
 * fs_setbit - Set a bit in the bitmaps
 * @buffer: the buffer that holds the bitmaps
 * @buflen: the length (in bytes) of the buffer
 * @block: the block to set
 * @new_state: the new state of the block
 *
 */
static void gfs2_setbit(unsigned char *buffer, unsigned int buflen,
						uint32_t block, unsigned char new_state)
{
	unsigned char *byte, *end, cur_state;
	unsigned int bit;

	byte = buffer + (block / GFS2_NBBY);
	bit = (block % GFS2_NBBY) * GFS2_BIT_SIZE;
	end = buffer + buflen;

	if(byte >= end){
		log_err("fs_setbit:  byte >= end\n");
		exit(1);
	}
	cur_state = (*byte >> bit) & GFS2_BIT_MASK;

	*byte ^= cur_state << bit;
	*byte |= new_state << bit;
}

uint32_t gfs2_bitfit_core(struct gfs2_sbd *sbp, uint64_t goal, uint64_t start,
						  uint64_t len, unsigned char old_state)
{
	uint64_t block;
	struct block_query q;

	log_debug("Goal: %"PRIu64", Start: %"PRIu64" len: %"PRIu64"\n",
		  goal, start, len);
	for(block = start+goal; block < start+len; block++) {
		block_check(bl, block, &q);
		switch(old_state) {
			/* FIXME Make sure these are handled correctly */
		case GFS2_BLKST_FREE:
			switch(q.block_type) {
			case block_free:
				return block - start;
			}
			break;
		case GFS2_BLKST_DINODE:
			switch(q.block_type) {
			case inode_dir:
			case inode_file:
			case inode_lnk:
			case inode_blk:
			case inode_chr:
			case inode_fifo:
			case inode_sock:
				return block - start;
			}
			break;
		case GFS2_BLKST_USED:
			switch(q.block_type) {
			case indir_blk:
			case leaf_blk:
			case journal_blk:
			case meta_other:
			case meta_eattr:
			case block_used:
				return block - start;
			}
			break;
		case GFS2_BLKST_INVALID:
		default:
			log_err("Invalid type");
			break;
		}
	}
	return BFITNOENT;
}
/**
 * gfs2_bitfit - Find a free block in the bitmaps
 * @buffer: the buffer that holds the bitmaps
 * @buflen: the length (in bytes) of the buffer
 * @goal: the block to try to allocate
 * @old_state: the state of the block we're looking for
 *
 * Return: the block number that was allocated
 */
uint32_t gfs2_bitfit(unsigned char *buffer, unsigned int buflen,
					 uint32_t goal, unsigned char old_state)
{
	unsigned char *byte, *end, alloc;
	uint32_t blk = goal;
	unsigned int bit;

	byte = buffer + (goal / GFS2_NBBY);
	bit = (goal % GFS2_NBBY) * GFS2_BIT_SIZE;
	end = buffer + buflen;
	alloc = (old_state & 1) ? 0 : 0x55;

	while (byte < end){
		if ((*byte & 0x55) == alloc){
			blk += (8 - bit) >> 1;
			bit = 0;
			byte++;
			continue;
		}

		if (((*byte >> bit) & GFS2_BIT_MASK) == old_state)
			return blk;

		bit += GFS2_BIT_SIZE;
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
uint32_t gfs2_bitcount(unsigned char *buffer, unsigned int buflen,
		     unsigned char state)
{
	unsigned char *byte, *end;
	unsigned int bit;
	uint32_t count = 0;

	byte = buffer;
	bit = 0;
	end = buffer + buflen;

	while (byte < end){
		if (((*byte >> bit) & GFS2_BIT_MASK) == state)
			count++;

		bit += GFS2_BIT_SIZE;
		if (bit >= 8){
			bit = 0;
			byte++;
		}
	}
	return count;
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
 *  GFS_BLKST_INVALID  (2)
 *  GFS_BLKST_DINODE   (3)
 *
 * Returns: state on success, -1 on error
 */
int gfs2_get_bitmap(struct gfs2_sbd *sdp, uint64_t blkno,
					struct rgrp_list *rgd)
{
	int           buf, val;
	uint32_t        rgrp_block;
	struct gfs2_bitmap	*bits = NULL;
	unsigned int  bit;
	unsigned char *byte;
	int local_rgd = 0;

	if(check_range(sdp, blkno)){
		log_warn("Block #%"PRIu64" is out of range.\n", blkno);
		return -1;
	}
	if(rgd == NULL) {
		local_rgd = 1;
		rgd = gfs2_blk2rgrpd(sdp, blkno);
	}
	if(rgd == NULL){
		log_err( "Unable to get rgrp for block #%"PRIu64"\n", blkno);
		return -1;
	}
	if(gfs2_rgrp_read(sdp, rgd)){
		log_err( "Unable to read rgrp.\n");
		return -1;
	}

	rgrp_block = (uint32_t)(blkno - rgd->ri.ri_data0);

	for(buf= 0; buf < rgd->ri.ri_length; buf++){
		bits = &(rgd->bits[buf]);
		if(rgrp_block < ((bits->bi_start + bits->bi_len)*GFS2_NBBY)){
			break;
		}
	}

	if(buf >= rgd->ri.ri_length){
		log_err( "Unable to locate bitmap entry for block #%"PRIu64"\n",
			blkno);
		gfs2_rgrp_relse(rgd);
		return -1;
	}

	byte = (unsigned char *)(rgd->bh[buf]->b_data + bits->bi_offset) +
		(rgrp_block/GFS2_NBBY - bits->bi_start);
	bit = (rgrp_block % GFS2_NBBY) * GFS2_BIT_SIZE;

	val = ((*byte >> bit) & GFS2_BIT_MASK);
	if(local_rgd) {
		gfs2_rgrp_relse(rgd);
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
int gfs2_set_bitmap(struct gfs2_sbd *sdp, uint64_t blkno, int state)
{
	int           buf;
	uint32_t        rgrp_block;
	struct gfs2_bitmap *bits = NULL;
	struct rgrp_list *rgd;

	/* FIXME: should GFS2_BLKST_INVALID be allowed */
	if((state != GFS2_BLKST_FREE) && (state != GFS2_BLKST_USED) &&
	   (state != GFS2_BLKST_DINODE)){
		return -1;
	}

	rgd = gfs2_blk2rgrpd(sdp, blkno);

	if(!rgd) {
		log_err("Unable to get resource group for blkno %"PRIu64"\n",
			blkno);
		return -1;
	}

	if(gfs2_rgrp_read(sdp, rgd)) {
		stack;
		return -1;
	}
	rgrp_block = (uint32_t)(blkno - rgd->ri.ri_data0);
	for(buf= 0; buf < rgd->ri.ri_length; buf++){
		bits = &(rgd->bits[buf]);
		if(rgrp_block < ((bits->bi_start + bits->bi_len)*GFS2_NBBY))
			break;
	}

	gfs2_setbit((unsigned char *)rgd->bh[buf]->b_data + bits->bi_offset,
				bits->bi_len, (rgrp_block - (bits->bi_start * GFS2_NBBY)),
				state);
	gfs2_rgrp_relse(rgd);
	return 0;
}
