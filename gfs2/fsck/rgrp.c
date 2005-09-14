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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "bio.h"
#include "fs_bits.h"
#include "fs_inode.h"
#include "fsck_incore.h"
#include "fsck.h"
#include "rgrp.h"
#include "inode.h"

/**
 * fs_compute_bitstructs - Compute the bitmap sizes
 * @rgd: The resource group descriptor
 *
 * Returns: 0 on success, -1 on error
 */
int fs_compute_bitstructs(struct fsck_rgrp *rgd)
{
	struct fsck_sb *sdp = rgd->rd_sbd;
	fs_bitmap_t *bits;
	uint32_t length = rgd->rd_ri.ri_length;
	uint32_t bytes_left, bytes;
	int x;

	if(!(rgd->rd_bits = (fs_bitmap_t *)malloc(length * sizeof(fs_bitmap_t)))) {
		log_err("Unable to allocate bitmap structure\n");
		stack;
		return -1;
	}
	if(!memset(rgd->rd_bits, 0, length * sizeof(fs_bitmap_t))) {
		log_err("Unable to zero bitmap structure\n");
		stack;
		return -1;
	}
	
	bytes_left = rgd->rd_ri.ri_bitbytes;

	for (x = 0; x < length; x++){
		bits = &rgd->rd_bits[x];

		if (length == 1){
			bytes = bytes_left;
			bits->bi_offset = sizeof(struct gfs2_rgrp);
			bits->bi_start = 0;
			bits->bi_len = bytes;
		}
		else if (x == 0){
			bytes = sdp->sb.sb_bsize - sizeof(struct gfs2_rgrp);
			bits->bi_offset = sizeof(struct gfs2_rgrp);
			bits->bi_start = 0;
			bits->bi_len = bytes;
		}
		else if (x + 1 == length){
			bytes = bytes_left;
			bits->bi_offset = sizeof(struct gfs2_meta_header);
			bits->bi_start = rgd->rd_ri.ri_bitbytes - bytes_left;
			bits->bi_len = bytes;
		}
		else{
			bytes = sdp->sb.sb_bsize - sizeof(struct gfs2_meta_header);
			bits->bi_offset = sizeof(struct gfs2_meta_header);
			bits->bi_start = rgd->rd_ri.ri_bitbytes - bytes_left;
			bits->bi_len = bytes;
		}

		bytes_left -= bytes;
	}

	if(bytes_left){
		log_err( "fs_compute_bitstructs:  Too many blocks in rgrp to "
			"fit into available bitmap.\n");
		return -1;
	}

	if((rgd->rd_bits[length - 1].bi_start +
	    rgd->rd_bits[length - 1].bi_len) * GFS2_NBBY != rgd->rd_ri.ri_data){
		log_err( "fs_compute_bitstructs:  # of blks in rgrp do not equal "
			"# of blks represented in bitmap.\n"
			"\tbi_start = %u\n"
			"\tbi_len   = %u\n"
			"\tGFS2_NBBY = %u\n"
			"\tri_data  = %u\n",
			rgd->rd_bits[length - 1].bi_start,
			rgd->rd_bits[length - 1].bi_len,
			GFS2_NBBY,
			rgd->rd_ri.ri_data);
		return -1;
	}


	if(!(rgd->rd_bh = (struct buffer_head **)malloc(length * sizeof(struct buffer_head *)))) {
		log_err("Unable to allocate osi_buf structure\n");
		stack;
		return -1;
	}
	if(!memset(rgd->rd_bh, 0, length * sizeof(struct buffer_head *))) {
		log_err("Unable to zero osi_buf structure\n");
		stack;
		return -1;
	}

	return 0;
}


/**
 * blk2rgrpd - Find resource group for a given data block number
 * @sdp: The GFS superblock
 * @n: The data block number
 *
 * Returns: Ths resource group, or NULL if not found
 */
struct fsck_rgrp *fs_blk2rgrpd(struct fsck_sb *sdp, uint64_t blk)
{
	osi_list_t *tmp;
	struct fsck_rgrp *rgd = NULL;
	struct gfs2_rindex *ri;

	for(tmp = sdp->rglist.next; tmp != &sdp->rglist; tmp = tmp->next){
		rgd = osi_list_entry(tmp, struct fsck_rgrp, rd_list);
		ri = &rgd->rd_ri;

		if (ri->ri_data0 <= blk && blk < ri->ri_data0 + ri->ri_data){
			break;
		} else
			rgd = NULL;
	}
	return rgd;
}


int fs_rgrp_read(struct fsck_rgrp *rgd)
{
	struct fsck_sb *sdp = rgd->rd_sbd;
	unsigned int x, length = rgd->rd_ri.ri_length;
	int error;

	if(rgd->rd_open_count){
		log_debug("rgrp already read...\n");
		rgd->rd_open_count++;
		return 0;
	}

	for (x = 0; x < length; x++){
		if(rgd->rd_bh[x]){
			log_err("Programmer error!  Bitmaps are already present in rgrp.\n");
			exit(1);
		}
		error = get_and_read_buf(sdp, rgd->rd_ri.ri_addr + x,
					 &(rgd->rd_bh[x]), 0);
		if(check_meta(rgd->rd_bh[x], (x) ? GFS2_METATYPE_RB : GFS2_METATYPE_RG)){
			log_err("Buffer #%"PRIu64" (%d of %d) is neither"
				" GFS2_METATYPE_RB nor GFS2_METATYPE_RG.\n",
				BH_BLKNO(rgd->rd_bh[x]),
				(int)x+1,
				(int)length);
			error = -1;
			goto fail;
		}
	}

	gfs2_rgrp_in(&rgd->rd_rg, BH_DATA(rgd->rd_bh[0]));
	rgd->rd_open_count = 1;

	return 0;

 fail:
	for (x = 0; x < length; x++){
		relse_buf(sdp, rgd->rd_bh[x]);
		rgd->rd_bh[x] = NULL;
	}

	log_err("Resource group is corrupted.\n");
	return error;
}

void fs_rgrp_relse(struct fsck_rgrp *rgd)
{
	int x, length = rgd->rd_ri.ri_length;

	rgd->rd_open_count--;
	if(rgd->rd_open_count){
		log_debug("rgrp still held...\n");
	} else {
		for (x = 0; x < length; x++){
			relse_buf(rgd->rd_sbd, rgd->rd_bh[x]);
			rgd->rd_bh[x] = NULL;
		}
	}
}



/**
 * fs_rgrp_recount - adjust block tracking numbers
 * rgd: resource group
 *
 * The resource groups keep track of how many free blocks, used blocks,
 * etc there are.  This function readjusts those numbers based on the
 * current state of the bitmap.
 *
 * Returns: 0 on success, -1 on failure
 */
int fs_rgrp_recount(struct fsck_rgrp *rgd){
	int i,j;
	fs_bitmap_t *bits = NULL;
	uint32_t length = rgd->rd_ri.ri_length;
	uint32_t count[4], tmp;

	for(i=0; i < 4; i++){
		count[i] = 0;
		for(j = 0; j < length; j++){
			bits = &rgd->rd_bits[j];
			count[i] += fs_bitcount(BH_DATA(rgd->rd_bh[j]) + bits->bi_offset,
						bits->bi_len, i);
		}
	}
	if(count[0] != rgd->rd_rg.rg_free){
		log_warn("\tAdjusting free block count (%u -> %u).\n",
			rgd->rd_rg.rg_free, count[0]);
		rgd->rd_rg.rg_free = count[0];
	}

	if(count[3] != rgd->rd_rg.rg_dinodes){
		log_warn("\tAdjusting dinode block count (%u -> %u).\n",
			 rgd->rd_rg.rg_dinodes, count[3]);
		rgd->rd_rg.rg_dinodes = count[3];
	}

	/* FIXME should i be subtracting out the invalid bits (count[2])? */
	tmp = rgd->rd_ri.ri_data - (rgd->rd_rg.rg_free + rgd->rd_rg.rg_dinodes)
		- count[2];

	if(count[1] != tmp) {
		log_err("Could not reconcile rgrp block counts.\n");
		return -1;
	}
	return 0;
}


/**
 * fs_blkalloc - Allocate a data block
 * @ip: the inode to allocate the data block for
 * @block: the block allocated
 *
 * Returns: 0 on success, -1 on failure
 */
int fs_blkalloc(struct fsck_inode *ip, uint64_t *block)
{
	osi_list_t *tmp;
	struct fsck_sb *sdp = ip->i_sbd;
	struct fsck_rgrp *rgd;
	uint32_t goal;
	int same;

	for(tmp = sdp->rglist.next; tmp != &sdp->rglist; tmp = tmp->next){
		rgd = osi_list_entry(tmp, struct fsck_rgrp, rd_list);

		if(!rgd){
			log_err( "fs_blkalloc:  Bad rgrp list!\n");
			return -1;
		}

		if(fs_rgrp_read(rgd)){
			log_err( "fs_blkalloc:  Unable to read rgrp.\n");
			return -1;
		}

		if(!rgd->rd_rg.rg_free){
			fs_rgrp_relse(rgd);
			continue;
		}

		same = (rgd->rd_ri.ri_addr == ip->i_di.di_goal_meta);
		goal = (same) ? ip->i_di.di_goal_data : 0;

		*block = fs_blkalloc_internal(rgd, goal,
					      GFS2_BLKST_FREE,
					      GFS2_BLKST_USED, 1);

		log_debug("Got block %"PRIu64"\n", *block);
		if(*block == BFITNOENT) {
			fs_rgrp_relse(rgd);
			continue;
		}
		if (!same){
			ip->i_di.di_goal_meta = rgd->rd_ri.ri_addr;
			ip->i_di.di_goal_data = 0;
		}

		*block += rgd->rd_ri.ri_data0;
		ip->i_di.di_goal_data = *block;

		rgd->rd_rg.rg_free--;

		gfs2_rgrp_out(&rgd->rd_rg, BH_DATA(rgd->rd_bh[0]));
		if(write_buf(sdp, rgd->rd_bh[0], 0)){
			log_err( "Unable to write out rgrp block #%"
				PRIu64".\n",
				BH_BLKNO(rgd->rd_bh[0]));
			fs_rgrp_relse(rgd);
			return -1;
		}
		fs_rgrp_relse(rgd);
		return 0;
	}

	return 1;
}


/**
 * fs_metaalloc - Allocate a metadata block to a file
 * @ip:  the file
 * @block: the block allocated
 *
 * Returns: 0 on success, -1 on failure
 */
int fs_metaalloc(struct fsck_inode *ip, uint64_t *block)
{
	osi_list_t *tmp;
	struct fsck_sb *sdp = ip->i_sbd;
	struct fsck_rgrp *rgd;
	uint32_t goal;
	int same;

	/* ATTENTION -- maybe we should try to allocate from goal rgrp first */
	for(tmp = sdp->rglist.next; tmp != &sdp->rglist; tmp = tmp->next){
		rgd = osi_list_entry(tmp, struct fsck_rgrp, rd_list);

		if(!rgd){
			log_err( "fs_metaalloc:  Bad rgrp list!\n");
			return -1;
		}

		if(fs_rgrp_read(rgd)){
			log_err( "fs_metaalloc:  Unable to read rgrp.\n");
			return -1;
		}

		same = (rgd->rd_ri.ri_addr == ip->i_di.di_goal_meta);
		goal = (same) ? ip->i_di.di_goal_data : 0;

		*block = fs_blkalloc_internal(rgd, goal,
					      GFS2_BLKST_FREE,
					      GFS2_BLKST_USED, 1);
		log_debug("Got block %"PRIu64"\n", *block);
		if(*block == BFITNOENT) {
			fs_rgrp_relse(rgd);
			continue;
		}
		if (!same){
			ip->i_di.di_goal_meta = rgd->rd_ri.ri_addr;
			ip->i_di.di_goal_data = 0;
		}
		*block += rgd->rd_ri.ri_data0;
		ip->i_di.di_goal_data = *block;

		rgd->rd_rg.rg_free--;

		gfs2_rgrp_out(&rgd->rd_rg, BH_DATA(rgd->rd_bh[0]));
		write_buf(sdp, rgd->rd_bh[0], 0);
		fs_rgrp_relse(rgd);
		/* if we made it this far, then we are ok */
		return 0;
	}

	return -1;
}
