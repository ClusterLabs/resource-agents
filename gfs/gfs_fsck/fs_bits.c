#include "util.h"
#include "bio.h"
#include "rgrp.h"

#include "fsck_incore.h"
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
		log_err("fs_setbit:  byte >= end\n");
		exit(1);
	}
	cur_state = (*byte >> bit) & GFS_BIT_MASK;

	*byte ^= cur_state << bit;
	*byte |= new_state << bit;
}

uint32_t fs_bitfit_core(struct fsck_sb *sbp, uint64_t goal, uint64_t start, uint64_t len,
		   unsigned char old_state)
{
	uint64_t block;
	struct block_query q;

	log_debug("Goal: %"PRIu64", Start: %"PRIu64" len: %"PRIu64"\n",
		  goal, start, len);
	for(block = start+goal; block < start+len; block++) {
		block_check(sbp->bl, block, &q);
		switch(old_state) {
		case GFS_BLKST_FREE:
			switch(q.block_type) {
			case block_free:
				return block - start;
			}
			break;
		case GFS_BLKST_FREEMETA:
			switch(q.block_type) {
			case meta_free:
				return block - start;
			}
			break;
		case GFS_BLKST_USEDMETA:
			switch(q.block_type) {
			case inode_dir:
			case inode_file:
			case inode_lnk:
			case inode_blk:
			case inode_chr:
			case inode_fifo:
			case inode_sock:
			case indir_blk:
			case leaf_blk:
			case journal_blk:
			case meta_other:
			case meta_eattr:
				return block - start;
			}
			break;
		case GFS_BLKST_USED:
			switch(q.block_type) {
			case block_used:
				return block - start;
			}
			break;
		default:
			log_err("Invalid type");
			break;
		}
	}
	return BFITNOENT;
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
	uint32_t block = 0;
	log_debug("fs_blkalloc_internal got %u as goal\n", goal);
	goal = ((int)(goal - rgd->rd_ri.ri_data1) < 0)
		? 0
		: goal - rgd->rd_ri.ri_data1;


	block = fs_bitfit_core(sdp, goal, rgd->rd_ri.ri_data1,
			       rgd->rd_ri.ri_data, old_state);


	if(block == BFITNOENT) {
		log_debug("No bits left in old_state?\n"
			  "\told_state   = %u\n"
			  "\tnew_state   = %u\n"
			  "\trg_free     = %u\n"
			  "\trg_freemeta = %u\n",
			old_state, new_state,
			rgd->rd_rg.rg_free,
			rgd->rd_rg.rg_freemeta);
		return BFITNOENT;
	}

	log_debug("fs_blkalloc_internal found block %u\n", block);
	switch(new_state) {
	case GFS_BLKST_FREE:
		block_set(sdp->bl, block + rgd->rd_ri.ri_data1, block_free);
		break;
	case GFS_BLKST_USED:
		block_set(sdp->bl, block + rgd->rd_ri.ri_data1, block_used);
		break;
	case GFS_BLKST_USEDMETA:
		block_set(sdp->bl, block + rgd->rd_ri.ri_data1, meta_other);
		break;
	case GFS_BLKST_FREEMETA:
		block_set(sdp->bl, block + rgd->rd_ri.ri_data1, meta_free);
		break;
	}
	return  block;
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
		log_warn("Block #%"PRIu64" is out of range.\n", blkno);
		return -1;
	}
	if(rgd == NULL) {
		local_rgd = 1;
		rgd = fs_blk2rgrpd(sdp, blkno);
	}
	if(rgd == NULL){
		log_err( "Unable to get rgrp for block #%"PRIu64"\n", blkno);
		return -1;
	}
	if(fs_rgrp_read(rgd, FALSE)){ /* FALSE:don't try to fix (done elsewhere) */
		log_err( "Unable to read rgrp.\n");
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
		log_err( "Unable to locate bitmap entry for block #%"PRIu64"\n",
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

	if(!rgd) {
		log_err("Unable to get resource group for blkno %"PRIu64"\n",
			blkno);
		return -1;
	}

	if(fs_rgrp_read(rgd, FALSE)) {
		stack;
		return -1;
	}
	rgrp_block = (uint32_t)(blkno - rgd->rd_ri.ri_data1);
	for(buf= 0; buf < rgd->rd_ri.ri_length; buf++){
		bits = &(rgd->rd_bits[buf]);
		if(rgrp_block < ((bits->bi_start + bits->bi_len)*GFS_NBBY)){
			break;
		}
	}
	if (buf < rgd->rd_ri.ri_length) {
		fs_setbit(BH_DATA(rgd->rd_bh[buf]) + bits->bi_offset,
				  bits->bi_len,
				  (rgrp_block - (bits->bi_start*GFS_NBBY)),
				  state);
		if(write_buf(sdp, rgd->rd_bh[buf], 0)){
			fs_rgrp_relse(rgd);
			return -1;
		}
	}
	fs_rgrp_relse(rgd);
	return 0;
}
