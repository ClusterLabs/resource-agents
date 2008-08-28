#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>

#include "libgfs2.h"
#include "fsck.h"
#include "fs_bits.h"
#include "util.h"

int convert_mark(struct gfs2_block_query *q, uint32_t *count)
{
	if (q->eattr_block) {
		count[2]++;
		return GFS2_BLKST_USED;
	}
	switch(q->block_type) {

	case gfs2_meta_inval:
		/* Convert invalid metadata to free blocks */
	case gfs2_block_free:
		count[0]++;
		return GFS2_BLKST_FREE;

	case gfs2_block_used:
		count[2]++;
		return GFS2_BLKST_USED;

	case gfs2_inode_dir:
	case gfs2_inode_file:
	case gfs2_inode_lnk:
	case gfs2_inode_blk:
	case gfs2_inode_chr:
	case gfs2_inode_fifo:
	case gfs2_inode_sock:
		count[1]++;
		return GFS2_BLKST_DINODE;

	case gfs2_indir_blk:
	case gfs2_leaf_blk:
	case gfs2_journal_blk:
	case gfs2_meta_other:
	case gfs2_meta_eattr:
		count[2]++;
		return GFS2_BLKST_USED;

	default:
		log_err("Invalid state %d found\n", q->block_type);
		return -1;
	}
	return -1;
}

int check_block_status(struct gfs2_sbd *sbp, char *buffer, unsigned int buflen,
					   uint64_t *rg_block, uint64_t rg_data, uint32_t *count)
{
	unsigned char *byte, *end;
	unsigned int bit;
	unsigned char rg_status, block_status;
	struct gfs2_block_query q;
	uint64_t block;

	/* FIXME verify cast */
	byte = (unsigned char *) buffer;
	bit = 0;
	end = (unsigned char *) buffer + buflen;

	while(byte < end) {
		rg_status = ((*byte >> bit) & GFS2_BIT_MASK);
		block = rg_data + *rg_block;
		warm_fuzzy_stuff(block);
		if (skip_this_pass || fsck_abort) /* if asked to skip the rest */
			return 0;
		gfs2_block_check(sbp, bl, block, &q);

		block_status = convert_mark(&q, count);

		/* If one node opens a file and another node deletes it, we
		   may be left with a block that appears to be "unlinked" in
		   the bitmap, but nothing links to it. This is a valid case
		   and should be cleaned up by the file system eventually.
		   So we ignore it. */
		if (rg_status == GFS2_BLKST_UNLINKED &&
		    block_status == GFS2_BLKST_FREE) {
			log_warn("Unlinked block found at block %"
				 PRIu64" (0x%" PRIx64 "), left unchanged.\n",
				 block, block);
		} else if (rg_status != block_status) {
			const char *blockstatus[] = {"Free", "Data", "Unlinked", "inode"};

			log_err("Ondisk and fsck bitmaps differ at"
					" block %"PRIu64" (0x%" PRIx64 ") \n", block, block);
			log_err("Ondisk status is %u (%s) but FSCK thinks it should be ",
					rg_status, blockstatus[rg_status]);
			log_err("%u (%s)\n", block_status, blockstatus[block_status]);
			log_err("Metadata type is %u (%s)\n", q.block_type,
					block_type_string(&q));

			if(query(&opts, "Fix bitmap for block %"
					 PRIu64" (0x%" PRIx64 ") ? (y/n) ", block, block)) {
				if(gfs2_set_bitmap(sbp, block, block_status))
					log_err("Failed.\n");
				else
					log_err("Succeeded.\n");
			} else
				log_err("Bitmap at block %"PRIu64" (0x%" PRIx64
						") left inconsistent\n", block, block);
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

enum update_flags update_rgrp(struct gfs2_sbd *sbp, struct rgrp_list *rgp,
							  uint32_t *count)
{
	uint32_t i;
	struct gfs2_bitmap *bits;
	uint64_t rg_block = 0;
	int update = 0;

	for(i = 0; i < rgp->ri.ri_length; i++) {
		bits = &rgp->bits[i];

		/* update the bitmaps */
		check_block_status(sbp, rgp->bh[i]->b_data + bits->bi_offset,
						   bits->bi_len, &rg_block, rgp->ri.ri_data0, count);
		if (skip_this_pass || fsck_abort) /* if asked to skip the rest */
			return 0;
	}

	/* actually adjust counters and write out to disk */
	if(rgp->rg.rg_free != count[0]) {
		log_err("RG #%" PRIu64 " (0x%" PRIx64
				") free count inconsistent: is %u should be %u\n",
				rgp->ri.ri_addr, rgp->ri.ri_addr, rgp->rg.rg_free, count[0]);
		rgp->rg.rg_free = count[0];
		update = 1;
	}
	if(rgp->rg.rg_dinodes != count[1]) {
		log_err("Inode count inconsistent: is %u should be %u\n",
				rgp->rg.rg_dinodes, count[1]);
		rgp->rg.rg_dinodes = count[1];
		update = 1;
	}
	if((rgp->ri.ri_data - count[0] - count[1]) != count[2]) {
		/* FIXME not sure how to handle this case ATM - it
		 * means that the total number of blocks we've counted
		 * exceeds the blocks in the rg */
		log_err("Internal fsck error - AAHHH!\n");
		exit(1);
	}
	if(update) {
		if(query(&opts, "Update resource group counts? (y/n) ")) {
			log_warn("Resource group counts updated\n");
			/* write out the rgrp */
			gfs2_rgrp_out(&rgp->rg, rgp->bh[0]->b_data);
			return updated;
		} else
			log_err("Resource group counts left inconsistent\n");
	}
	return not_updated;
}

/**
 * pass5 - check resource groups
 *
 * fix free block maps
 * fix used inode maps
 */
int pass5(struct gfs2_sbd *sbp)
{
	osi_list_t *tmp;
	struct rgrp_list *rgp = NULL;
	uint32_t count[3];
	uint64_t rg_count = 0;

	/* Reconcile RG bitmaps with fsck bitmap */
	for(tmp = sbp->rglist.next; tmp != &sbp->rglist; tmp = tmp->next){
		enum update_flags f;

		if (skip_this_pass || fsck_abort) /* if asked to skip the rest */
			return 0;
		log_info("Verifying Resource Group #%" PRIu64 "\n", rg_count);
		memset(count, 0, sizeof(count));
		rgp = osi_list_entry(tmp, struct rgrp_list, list);

		if(gfs2_rgrp_read(sbp, rgp)){
			stack;
			return -1;
		}
		rg_count++;
		/* Compare the bitmaps and report the differences */
		f = update_rgrp(sbp, rgp, count);
		gfs2_rgrp_relse(rgp, f);
	}
	/* Fix up superblock info based on this - don't think there's
	 * anything to do here... */

	return 0;
}
