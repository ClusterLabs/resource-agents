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

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>

#include "fsck_incore.h"
#include "fsck.h"
#include "ondisk.h"
#include "fs_bits.h"
#include "bio.h"


int convert_mark(enum mark_block mark, uint32_t *count)
{
	switch(mark) {

	case meta_inval:
		/* Convert invalid metadata to free blocks */
	case block_free:
		count[0]++;
		return GFS2_BLKST_FREE;

	case block_used:
		count[2]++;
		return GFS2_BLKST_USED;

	case inode_dir:
	case inode_file:
	case inode_lnk:
	case inode_blk:
	case inode_chr:
	case inode_fifo:
	case inode_sock:
		count[1]++;
		return GFS2_BLKST_DINODE;

	case indir_blk:
	case leaf_blk:
	case journal_blk:
	case meta_other:
	case meta_eattr:
		count[2]++;
		return GFS2_BLKST_USED;

	default:
		log_err("Invalid state %d found\n", mark);
		return -1;

	}
	return -1;
}


int check_block_status(struct fsck_sb *sbp, char *buffer, unsigned int buflen,
		       uint64_t *rg_block, uint64_t rg_data, uint32_t *count)
{
	unsigned char *byte, *end;
	unsigned int bit;
	unsigned char rg_status, block_status;
	struct block_query q;
	uint64_t block;

	/* FIXME verify cast */
	byte = (unsigned char *) buffer;
	bit = 0;
	end = (unsigned char *) buffer + buflen;

	while(byte < end) {
		rg_status = ((*byte >> bit) & GFS2_BIT_MASK);
		block = rg_data + *rg_block;
		block_check(sbp->bl, block, &q);

		block_status = convert_mark(q.block_type, count);

		if(rg_status != block_status) {
			log_debug("Ondisk is %u - FSCK thinks it is %u (%u)\n",
				  rg_status, block_status, q.block_type);
			log_err("ondisk and fsck bitmaps differ at"
				" block %"PRIu64"\n", block);

			if(query(sbp, "Fix bitmap for block %"
				 PRIu64"? (y/n) ", block)) {
				if(fs_set_bitmap(sbp, block, block_status)) {
					log_err("Failed.\n");
				}
				else {
					log_err("Succeeded.\n");
				}
			} else {
				log_err("Bitmap at block %"PRIu64
					" left inconsistent\n", block);
			}
		}
		(*rg_block)++;
		bit += GFS2_BIT_SIZE;
		if (bit >= 8){
			bit = 0;
			byte++;
		}
	}

	return 0;
}


int update_rgrp(struct fsck_rgrp *rgp, uint32_t *count)
{
	uint32_t i;
	fs_bitmap_t *bits;
	uint64_t rg_block = 0;
	int update = 0;

	for(i = 0; i < rgp->rd_ri.ri_length; i++) {
		bits = &rgp->rd_bits[i];

		/* update the bitmaps */
		check_block_status(rgp->rd_sbd,
				   BH_DATA(rgp->rd_bh[i]) + bits->bi_offset,
				   bits->bi_len, &rg_block,
				   rgp->rd_ri.ri_data0, count);
	}

	/* actually adjust counters and write out to disk */
	if(rgp->rd_rg.rg_free != count[0]) {
		log_err("free count inconsistent: is %u should be %u\n",
			rgp->rd_rg.rg_free, count[0] );
		rgp->rd_rg.rg_free = count[0];
		update = 1;
	}
	if(rgp->rd_rg.rg_dinodes != count[1]) {
		log_err("inode count inconsistent: is %u should be %u\n",
			rgp->rd_rg.rg_dinodes, count[1]);
		rgp->rd_rg.rg_dinodes = count[1];
		update = 1;
	}
	if((rgp->rd_ri.ri_data - count[0] - count[1]) != count[2]) {
		/* FIXME not sure how to handle this case ATM - it
		 * means that the total number of blocks we've counted
		 * exceeds the blocks in the rg */
		log_err("Internal fsck error - AAHHH!\n");
		exit(1);
	}
	if(update) {
		if(query(rgp->rd_sbd,
			 "Update resource group counts? (y/n) ")) {
			log_warn("Resource group counts updated\n");
			/* write out the rgrp */
			gfs2_rgrp_out(&rgp->rd_rg, BH_DATA(rgp->rd_bh[0]));
			write_buf(rgp->rd_sbd, rgp->rd_bh[0], 0);
		} else {
			log_err("Resource group counts left inconsistent\n");
		}
	}

	return 0;
}

/**
 * pass5 - check resource groups
 *
 * fix free block maps
 * fix used inode maps
 */
int pass5(struct fsck_sb *sbp, struct options *opts)
{
	osi_list_t *tmp;
	struct fsck_rgrp *rgp = NULL;
	uint32_t count[3];
	uint64_t rg_count = 0;

	/* Reconcile RG bitmaps with fsck bitmap */
	for(tmp = sbp->rglist.next; tmp != &sbp->rglist; tmp = tmp->next){
		log_info("Verifying Resource Group %"PRIu64"\n", rg_count);
		memset(count, 0, sizeof(count));
		rgp = osi_list_entry(tmp, struct fsck_rgrp, rd_list);

		if(fs_rgrp_read(rgp)){
			stack;
			return -1;
		}
		/* Compare the bitmaps and report the differences */
		update_rgrp(rgp, count);
		rg_count++;
		fs_rgrp_relse(rgp);
	}
	/* Fix up superblock info based on this - don't think there's
	 * anything to do here... */


	return 0;
}
