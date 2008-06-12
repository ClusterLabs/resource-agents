#include <stdio.h>
#include "fsck_incore.h"
#include "fsck.h"
#include "ondisk.h"
#include "fs_bits.h"
#include "bio.h"
#include "util.h"

#ifdef DEBUG
int rgrp_countbits(unsigned char *buffer, unsigned int buflen,
		   uint32_t *bit_array)
{
	unsigned char *byte, *end;
	unsigned int bit;
	unsigned char state;

	byte = buffer;
	bit = 0;
	end = buffer + buflen;

	while (byte < end){
		state = ((*byte >> bit) & GFS_BIT_MASK);
		switch (state) {
		case GFS_BLKST_FREE:
			bit_array[0]++;
			break;
		case GFS_BLKST_USED:
			bit_array[1]++;
			break;
		case GFS_BLKST_FREEMETA:
			bit_array[2]++;
			break;
		case GFS_BLKST_USEDMETA:
			bit_array[3]++;
			break;
		default:
			log_err("Invalid state %d found at byte %u, bit %u\n",
				state, byte, bit);
			return -1;
			break;
		}

		bit += GFS_BIT_SIZE;
		if (bit >= 8){
			bit = 0;
			byte++;
		}
	}
	return 0;
}

int fsck_countbits(struct fsck_sb *sbp, uint64_t start_blk, uint64_t count,
		   uint32_t *bit_array)
{
	uint64_t i;
	struct block_query q;
	for(i = start_blk; i < start_blk+count; i++) {
		block_check(sbp->bl, i, &q);
		switch(q.block_type) {
		case block_free:
			bit_array[0]++;
			break;
		case block_used:
			bit_array[1]++;
			break;
		case meta_free:
		case meta_inval:
			bit_array[2]++;
			break;
		case indir_blk:
		case inode_dir:
		case inode_file:
		case leaf_blk:
		case journal_blk:
		case meta_other:
		case meta_eattr:
			bit_array[3]++;
			break;
		default:
			log_err("Invalid state %d found at block%"PRIu64"\n",
				q.block_type, i);
			return -1;
			break;
		}
	}
	return 0;
}


int count_bmaps(struct fsck_rgrp *rgp)
{
	uint32_t i;
	uint32_t bit_array_rgrp[4] = {0};
	uint32_t bit_array_fsck[4] = {0};
	fs_bitmap_t *bits;

	for(i = 0; i < rgp->rd_ri.ri_length; i++) {
		bits = &rgp->rd_bits[i];
		rgrp_countbits(BH_DATA(rgp->rd_bh[i]) + bits->bi_offset,
			       bits->bi_len, bit_array_rgrp);
	}
	log_err("rgrp: free %u used %u meta_free %u meta_used %u\n",
		bit_array_rgrp[0], bit_array_rgrp[1],
		bit_array_rgrp[2], bit_array_rgrp[3]);
	fsck_countbits(rgp->rd_sbd, rgp->rd_ri.ri_data1,
		       rgp->rd_ri.ri_data, bit_array_fsck);
	log_err("fsck: free %u used %u meta_free %u meta_used %u\n",
		bit_array_fsck[0], bit_array_fsck[1],
		bit_array_fsck[2], bit_array_fsck[3]);

	for(i = 0; i < 4; i++) {
		if(bit_array_rgrp[i] != bit_array_fsck[i]) {
			log_err("Bitmap count in index %d differ: "
				"ondisk %d, fsck %d\n", i,
				bit_array_rgrp[i], bit_array_fsck[i]);
		}
	}
	return 0;
}
#endif /* DEBUG */

int convert_mark(enum mark_block mark, uint32_t *count)
{
	switch(mark) {

	case meta_inval:
		/* Convert invalid metadata to free blocks */
	case block_free:
		count[0]++;
		return GFS_BLKST_FREE;

	case meta_free:
		count[4]++;
		return GFS_BLKST_FREEMETA;

	case block_used:
		return GFS_BLKST_USED;

	case inode_dir:
	case inode_file:
	case inode_lnk:
	case inode_blk:
	case inode_chr:
	case inode_fifo:
	case inode_sock:
		count[1]++;
		return GFS_BLKST_USEDMETA;

	case indir_blk:
	case leaf_blk:
	case journal_blk:
	case meta_other:
	case meta_eattr:
		count[3]++;
		return GFS_BLKST_USEDMETA;

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

	byte = (unsigned char *)buffer;
	bit = 0;
	end = (unsigned char *)buffer + buflen;

	while(byte < end) {
		rg_status = ((*byte >> bit) & GFS_BIT_MASK);
		block = rg_data + *rg_block;
		log_debug("Checking block %" PRIu64 "\n", block);
		warm_fuzzy_stuff(block);
		if (skip_this_pass || fsck_abort) /* if asked to skip the rest */
			return 0;
		block_check(sbp->bl, block, &q);

		block_status = convert_mark(q.block_type, count);

		if(rg_status != block_status) {
			log_debug("Ondisk is %u - FSCK thinks it is %u (%u)\n",
				  rg_status, block_status, q.block_type);
			if((rg_status == GFS_BLKST_FREEMETA) &&
			   (block_status == GFS_BLKST_FREE)) {
				log_info("Converting free metadata block at %"
					 PRIu64" to a free data block\n", block);
				if(!sbp->opts->no) {
					if(fs_set_bitmap(sbp, block, block_status)) {
						log_warn("Failed to convert free metadata block to free data block at %PRIu64.\n", block);
					}
					else {
						log_info("Succeeded.\n");
					}
				}
			}
			else {

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
		}
		(*rg_block)++;
		bit += GFS_BIT_SIZE;
		if (bit >= 8){
			bit = 0;
			byte++;
		}
	}

	return 0;
}

#define FREE_COUNT       1
#define USED_INODE_COUNT 2
#define FREE_INODE_COUNT 4
#define USED_META_COUNT  8
#define FREE_META_COUNT  16
#define CONVERT_FREEMETA_TO_FREE (FREE_COUNT | FREE_META_COUNT)

int update_rgrp(struct fsck_rgrp *rgp, uint32_t *count, int rgcount)
{
	uint32_t i;
	fs_bitmap_t *bits;
	uint64_t rg_block = 0;
	uint8_t bmap = 0;

	for(i = 0; i < rgp->rd_ri.ri_length; i++) {
		bits = &rgp->rd_bits[i];

		/* update the bitmaps */
		check_block_status(rgp->rd_sbd,
				   BH_DATA(rgp->rd_bh[i]) + bits->bi_offset,
				   bits->bi_len, &rg_block,
				   rgp->rd_ri.ri_data1, count);
		if (skip_this_pass || fsck_abort) /* if asked to skip the rest */
			return 0;
	}

	/* Compare the rgrps counters with what we found */
	if(rgp->rd_rg.rg_free != count[0]) {
		bmap |= FREE_COUNT;
	}
	if(rgp->rd_rg.rg_useddi != count[1]) {
		bmap |= USED_INODE_COUNT;
	}
	if(rgp->rd_rg.rg_freedi != count[2]) {
		bmap |= FREE_INODE_COUNT;
	}
	if(rgp->rd_rg.rg_usedmeta != count[3]) {
		bmap |= USED_META_COUNT;
	}
	if(rgp->rd_rg.rg_freemeta != count[4]) {
		bmap |= FREE_META_COUNT;
	}

	if(bmap && !(bmap & ~CONVERT_FREEMETA_TO_FREE)) {
		log_notice("Converting %d unused metadata blocks to free data blocks...\n",
			   rgp->rd_rg.rg_freemeta - count[4]);
		rgp->rd_rg.rg_free = count[0];
		rgp->rd_rg.rg_freemeta = count[4];
		gfs_rgrp_out(&rgp->rd_rg, BH_DATA(rgp->rd_bh[0]));
		if(!rgp->rd_sbd->opts->no) {
			write_buf(rgp->rd_sbd, rgp->rd_bh[0], 0);
		}
	} else if(bmap) {
		/* actually adjust counters and write out to disk */
		if(bmap & FREE_COUNT) {
			log_err("RG #%d free count inconsistent: is %u should be %u\n",
					rgcount, rgp->rd_rg.rg_free, count[0] );
			rgp->rd_rg.rg_free = count[0];
		}
		if(bmap & USED_INODE_COUNT) {
			log_err("RG #%d used inode count inconsistent: is %u should be %u\n",
				rgcount, rgp->rd_rg.rg_useddi, count[1]);
			rgp->rd_rg.rg_useddi = count[1];
		}
		if(bmap & FREE_INODE_COUNT) {
			log_err("RG #%d free inode count inconsistent: is %u should be %u\n",
				rgcount, rgp->rd_rg.rg_freedi, count[2]);
			rgp->rd_rg.rg_freedi = count[2];
		}
		if(bmap & USED_META_COUNT) {
			log_err("RG #%d used meta count inconsistent: is %u should be %u\n",
				rgcount, rgp->rd_rg.rg_usedmeta, count[3]);
			rgp->rd_rg.rg_usedmeta = count[3];
		}
		if(bmap & FREE_META_COUNT) {
			log_err("RG #%d free meta count inconsistent: is %u should be %u\n",
				rgcount, rgp->rd_rg.rg_freemeta, count[4]);
			rgp->rd_rg.rg_freemeta = count[4];
		}

		if(query(rgp->rd_sbd,
			 "Update resource group counts? (y/n) ")) {
			log_warn("Resource group counts updated\n");
			/* write out the rgrp */
			gfs_rgrp_out(&rgp->rd_rg, BH_DATA(rgp->rd_bh[0]));
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
	uint32_t count[5];
	uint64_t rg_count = 1;

	/* Reconcile RG bitmaps with fsck bitmap */
	for(tmp = sbp->rglist.next; tmp != &sbp->rglist; tmp = tmp->next){
		if (skip_this_pass || fsck_abort) /* if asked to skip the rest */
			return 0;
		log_info("Updating Resource Group %"PRIu64"\n", rg_count);
		memset(count, 0, sizeof(*count) * 5);
		rgp = osi_list_entry(tmp, struct fsck_rgrp, rd_list);

		if(fs_rgrp_read(rgp, FALSE)){
			stack;
			return -1;
		}
		/* Compare the bitmaps and report the differences */
		update_rgrp(rgp, count, rg_count);
		rg_count++;
		fs_rgrp_relse(rgp);
	}
	/* Fix up superblock info based on this - don't think there's
	 * anything to do here... */


	return 0;
}
