#include <unistd.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "libgfs2.h"
#include "osi_list.h"
#include "fsck.h"

int rindex_modified = FALSE;
struct special_blocks false_rgrps;

#define ri_equal(ondisk, expected, field) (ondisk.field == expected.field)

#define ri_compare(rg, ondisk, expected, field, fmt)	\
	if (ondisk.field != expected.field) { \
		log_warn("rindex #%d " #field " discrepancy: index 0x%" fmt \
			 " != expected: 0x%" fmt "\n",			\
			 rg + 1, ondisk.field, expected.field);		\
		ondisk.field = expected.field; \
		rindex_modified = TRUE; \
	}

/*
 * find_journal_entry_rgs - find all RG blocks within all journals
 *
 * Since Resource Groups (RGs) are journaled, it is not uncommon for them
 * to appear inside a journal.  But if there is severe damage to the rindex
 * file or some of the RGs, we may need to hunt and peck for RGs and in that
 * case, we don't want to mistake these blocks that look just a real RG
 * for a real RG block.  These are "fake" RGs that need to be ignored for
 * the purposes of finding where things are.
 */
void find_journaled_rgs(struct gfs2_sbd *sdp)
{
	int j, new = 0;
	unsigned int jblocks;
	uint64_t b, dblock;
	uint32_t extlen;
	struct gfs2_inode *ip;
	struct gfs2_buffer_head *bh;

	osi_list_init(&false_rgrps.list);
	for (j = 0; j < sdp->md.journals; j++) {
		log_debug("Checking for RGs in journal%d.\n", j);
		ip = sdp->md.journal[j];
		jblocks = ip->i_di.di_size / sdp->sd_sb.sb_bsize;
		for (b = 0; b < jblocks; b++) {
			block_map(ip, b, &new, &dblock, &extlen, 0,
				  not_updated);
			if (!dblock)
				break;
			bh = bread(sdp, dblock);
			if (!gfs2_check_meta(bh, GFS2_METATYPE_RG)) {
				log_debug("False RG found at block "
					  "0x%" PRIx64 "\n", dblock);
				gfs2_special_set(&false_rgrps, dblock);
			}
			brelse(bh, not_updated);
		}
	}
}

int is_false_rg(uint64_t block)
{
	if (blockfind(&false_rgrps, block))
		return 1;
	return 0;
}

/*
 * gfs2_rindex_rebuild - rebuild a corrupt Resource Group (RG) index manually
 *                        where trust_lvl == distrust
 *
 * If this routine is called, it means we have RGs in odd/unexpected places,
 * and there is a corrupt RG or RG index entry.  It also means we can't trust
 * the RG index to be sane, and the RGs don't agree with how mkfs would have
 * built them by default.  So we have no choice but to go through and count 
 * them by hand.  We've tried twice to recover the RGs and RG index, and
 * failed, so this is our last chance to remedy the situation.
 *
 * This routine tries to minimize performance impact by:
 * 1. Skipping through the filesystem at known increments when possible.
 * 2. Shuffle through every block when RGs are not found at the predicted
 *    locations.
 *
 * Note: A GFS2 filesystem differs from a GFS1 file system in that there will
 * only be ONE chunk (i.e. no artificial subdevices on either size of the
 * journals).  The journals and even the rindex are kept as part of the file
 * system, so we need to rebuild that information by hand.  Also, with GFS1,
 * the different chunks ("subdevices") could have different RG sizes, which
 * made for quite a mess when trying to recover RGs.  GFS2 always uses the 
 * same RG size determined by the original mkfs, so recovery is easier.
 *
 */
int gfs2_rindex_rebuild(struct gfs2_sbd *sdp, osi_list_t *ret_list,
			 int *num_rgs)
{
	struct gfs2_buffer_head *bh;
	uint64_t shortest_dist_btwn_rgs;
	uint64_t blk, block_of_last_rg;
	uint64_t fwd_block, block_bump;
	uint64_t first_rg_dist, initial_first_rg_dist;
	struct rgrp_list *calc_rgd, *prev_rgd;
	int number_of_rgs, rgi;
	struct gfs2_rindex buf, tmpndx;
	int rg_was_fnd = FALSE, corrupt_rgs = 0, bitmap_was_fnd;
	osi_list_t *tmp;

	/* Figure out if there are any RG-looking blocks in the journal we
	   need to ignore. */
	find_journaled_rgs(sdp);
	osi_list_init(ret_list);
	number_of_rgs = 0;
	initial_first_rg_dist = first_rg_dist = sdp->sb_addr + 1;
	block_of_last_rg = sdp->sb_addr + 1;
	/* ------------------------------------------------------------- */
	/* First, hunt and peck for the shortest distance between RGs.   */
	/* Sample several of them because an RG that's been blasted may  */
	/* look like twice the distance.  If we can find 6 of them, that */
	/* should be enough to figure out the correct layout.            */
	/* ------------------------------------------------------------- */
	shortest_dist_btwn_rgs = sdp->device.length;
	for (blk = sdp->sb_addr + 1;
	     blk < sdp->device.length && number_of_rgs < 6;
	     blk++) {
		bh = bread(sdp, blk);
		if (((blk == sdp->sb_addr + 1) ||
		    (!gfs2_check_meta(bh, GFS2_METATYPE_RG))) &&
		    !is_false_rg(blk)) {
			log_debug("RG found at block 0x%" PRIx64 "\n", blk);
			if (blk > sdp->sb_addr + 1) {
				uint64_t rgdist;
				
				rgdist = blk - block_of_last_rg;
				log_debug("dist 0x%" PRIx64 " = 0x% " PRIx64
					  " - 0x%" PRIx64, rgdist,
					  blk, block_of_last_rg);
				/* ----------------------------------------- */
				/* We found an RG.  Check to see if we need  */
				/* to set the first_rg_dist based on whether */
				/* it's still at its initial value (i.e. the */
				/* fs.)  The first rg distance is different  */
				/* from the rest because of the superblock   */
				/* and 64K dead space.                       */
				/* ----------------------------------------- */
				if (first_rg_dist == initial_first_rg_dist)
					first_rg_dist = rgdist;
				if (rgdist < shortest_dist_btwn_rgs) {
					shortest_dist_btwn_rgs = rgdist;
					log_debug("(shortest so far)\n");
				}
				else
					log_debug("\n");
			}
			block_of_last_rg = blk;
			number_of_rgs++;
			blk += 250; /* skip ahead for performance */
		}
		brelse(bh, not_updated);
	}
	number_of_rgs = 0;
	gfs2_special_free(&false_rgrps);

	/* -------------------------------------------------------------- */
	/* Sanity-check our first_rg_dist. If RG #2 got nuked, the        */
	/* first_rg_dist would measure from #1 to #3, which would be bad. */
	/* We need to take remedial measures to fix it (from the index).  */
	/* -------------------------------------------------------------- */
	log_debug("First RG distance: 0x%" PRIx64 "\n", first_rg_dist);
	log_debug("Distance between RGs: 0x%" PRIx64 "\n",
		  shortest_dist_btwn_rgs);
	if (first_rg_dist >= shortest_dist_btwn_rgs +
	    (shortest_dist_btwn_rgs / 4)) {
		/* read in the second RG index entry for this subd. */
		gfs2_readi(sdp->md.riinode, (char *)&buf,
			   sizeof(struct gfs2_rindex),
			   sizeof(struct gfs2_rindex));
		gfs2_rindex_in(&tmpndx, (char *)&buf);
		if (tmpndx.ri_addr > sdp->sb_addr + 1) { /* sanity check */
			log_warn("RG 2 is damaged: getting dist from index: ");
			first_rg_dist = tmpndx.ri_addr - (sdp->sb_addr + 1);
			log_warn("0x%" PRIx64 "\n", first_rg_dist);
		}
		else {
			log_warn("RG index 2 is damaged: extrapolating dist: ");
			first_rg_dist = sdp->device.length -
				(sdp->rgrps - 1) *
				(sdp->device.length / sdp->rgrps);
			log_warn("0x%" PRIx64 "\n", first_rg_dist);
		}
		log_debug("Adjusted first RG distance: 0x%" PRIx64 "\n",
			  first_rg_dist);
	} /* if first RG distance is within tolerance */
	/* -------------------------------------------------------------- */
	/* Now go through the RGs and verify their integrity, fixing as   */
	/* needed when corruption is encountered.                         */
	/* -------------------------------------------------------------- */
	prev_rgd = NULL;
	block_bump = first_rg_dist;
	for (blk = sdp->sb_addr + 1; blk <= sdp->device.length;
	     blk += block_bump) {
		log_debug("Block 0x%" PRIx64 "\n", blk);
		bh = bread(sdp, blk);
		rg_was_fnd = (!gfs2_check_meta(bh, GFS2_METATYPE_RG));
		brelse(bh, not_updated);
		/* Allocate a new RG and index. */
		calc_rgd = malloc(sizeof(struct rgrp_list));
		if (!calc_rgd) {
			log_crit("Can't allocate memory for rg repair.\n");
			return -1;
		}
		memset(calc_rgd, 0, sizeof(struct rgrp_list));
		osi_list_add_prev(&calc_rgd->list, ret_list);
		calc_rgd->ri.ri_length = 1;
		calc_rgd->ri.ri_addr = blk;
		if (!rg_was_fnd) { /* if not an RG */
			/* ------------------------------------------------- */
			/* This SHOULD be an RG but isn't.                   */
			/* ------------------------------------------------- */
			corrupt_rgs++;
			if (corrupt_rgs < 5)
				log_debug("Missing or damaged RG at block %" 
					  PRIu64 " (0x%" PRIx64 ")\n",
					  blk, blk);
			else {
				log_crit("Error: too many bad RGs.\n");
				return -1;
			}
		}
		/* ------------------------------------------------ */
		/* Now go through and count the bitmaps for this RG */
		/* ------------------------------------------------ */
		bitmap_was_fnd = FALSE;
		for (fwd_block = blk + 1;
		     fwd_block < sdp->device.length; 
		     fwd_block++) {
			bh = bread(sdp, fwd_block);
			bitmap_was_fnd =
				(!gfs2_check_meta(bh, GFS2_METATYPE_RB));
			brelse(bh, not_updated);
			if (bitmap_was_fnd) /* if a bitmap */
				calc_rgd->ri.ri_length++;
			else
				break; /* end of bitmap, so call it quits. */
		} /* for subsequent bitmaps */
		
		gfs2_compute_bitstructs(sdp, calc_rgd);
		log_debug("Memory allocated for rg at 0x%p, bh:\n",
			  calc_rgd->ri.ri_addr, calc_rgd->bh);
		if (!calc_rgd->bh) {
			log_crit("Can't allocate memory for bitmap repair.\n");
			return -1;
		}
		calc_rgd->ri.ri_data0 = calc_rgd->ri.ri_addr +
			calc_rgd->ri.ri_length;
		if (prev_rgd) {
			uint32_t rgblocks, bitblocks;

			rgblocks = block_bump;
			rgblocks2bitblocks(sdp->bsize, &rgblocks, &bitblocks);

			prev_rgd->ri.ri_length = bitblocks;
			prev_rgd->ri.ri_data = rgblocks;
			prev_rgd->ri.ri_data -= prev_rgd->ri.ri_data %
				GFS2_NBBY;
			prev_rgd->ri.ri_bitbytes = prev_rgd->ri.ri_data /
				GFS2_NBBY;
			log_debug("Prev ri_data set to: %" PRIx32 ".\n",
				  prev_rgd->ri.ri_data);
		}
		number_of_rgs++;
		log_warn("%c RG %d at block 0x%" PRIX64 " %s",
			 (rg_was_fnd ? ' ' : '*'), number_of_rgs, blk,
			 (rg_was_fnd ? "intact" : "*** DAMAGED ***"));
		prev_rgd = calc_rgd;
		block_of_last_rg = blk;

		if (blk == sdp->sb_addr + 1)
			block_bump = first_rg_dist;
		else
			block_bump = shortest_dist_btwn_rgs;
		if (block_bump != 1)
			log_warn(" [length 0x%" PRIx64 "]\n", block_bump);
	} /* for each rg block */
	/* ----------------------------------------------------------------- */
	/* If we got to the end of the fs, we still need to fix the          */
	/* allocation information for the very last RG.                      */
	/* ----------------------------------------------------------------- */
	if (prev_rgd && !prev_rgd->ri.ri_data) {
		uint32_t rgblocks, bitblocks;

		rgblocks = block_bump;
		rgblocks2bitblocks(sdp->bsize, &rgblocks, &bitblocks);

		prev_rgd->ri.ri_length = bitblocks;
		prev_rgd->ri.ri_data = rgblocks;
		prev_rgd->ri.ri_data -= prev_rgd->ri.ri_data % GFS2_NBBY;
		prev_rgd->ri.ri_bitbytes = prev_rgd->ri.ri_data / GFS2_NBBY;
		log_debug("Prev ri_data set to: %" PRIx32 ".\n",
			  prev_rgd->ri.ri_data);
		prev_rgd = NULL; /* make sure we don't use it later */
	}
        /* ---------------------------------------------- */
        /* Now dump out the information (if verbose mode) */      
        /* ---------------------------------------------- */
        log_debug("RG index rebuilt as follows:\n");
        for (tmp = ret_list, rgi = 0; tmp != ret_list;
	     tmp = tmp->next, rgi++) {
                calc_rgd = osi_list_entry(tmp, struct rgrp_list, list);
                log_debug("%d: 0x%" PRIx64 " / %x / 0x%"
			  PRIx64 " / 0x%x / 0x%x\n", rgi + 1, 
			  calc_rgd->ri.ri_addr, calc_rgd->ri.ri_length,
			  calc_rgd->ri.ri_data0, calc_rgd->ri.ri_data, 
			  calc_rgd->ri.ri_bitbytes);
        }
	*num_rgs = number_of_rgs;
	return 0;
}

/*
 * gfs2_rindex_calculate - calculate what the rindex should look like
 *                          in a perfect world (trust_lvl == open_minded)
 *
 * Calculate what the rindex should look like, 
 * so we can later check if all RG index entries are sane.
 * This is a lot easier for gfs2 because we can just call the same libgfs2 
 * functions used by mkfs.
 *
 * Returns: 0 on success, -1 on failure
 * Sets:    sdp->rglist to a linked list of fsck_rgrp structs representing
 *          what we think the rindex should really look like.
 */
int gfs2_rindex_calculate(struct gfs2_sbd *sdp, osi_list_t *ret_list,
			   int *num_rgs)
{
	osi_list_init(ret_list);
	sdp->rgsize = GFS2_DEFAULT_RGSIZE; /* compute_rgrp_layout adjusts */
	device_geometry(sdp);
	fix_device_geometry(sdp);
	/* Compute the default resource group layout as mkfs would have done */
	compute_rgrp_layout(sdp, FALSE);
	build_rgrps(sdp, FALSE); /* FALSE = calc but don't write to disk. */
	*num_rgs = 0;
	log_debug("fs_total_size = 0x%" PRIX64 " blocks.\n",
		  sdp->device.length);
	/* ----------------------------------------------------------------- */
	/* Calculate how many RGs there are supposed to be based on the      */
	/* rindex filesize.  Remember that our trust level is open-minded    */
	/* here.  If the filesize of the rindex file is not a multiple of    */
	/* our rindex structures, then something's wrong and we can't trust  */
	/* the index.                                                        */
	/* ----------------------------------------------------------------- */
	*num_rgs = sdp->md.riinode->i_di.di_size / sizeof(struct gfs2_rindex);
	log_warn("L2: number of rgs in the index = %d.\n", *num_rgs);
	return 0;
}

/*
 * rewrite_rg_block - rewrite ("fix") a buffer with rg or bitmap data
 * returns: 0 if the rg was repaired, otherwise 1
 */
int rewrite_rg_block(struct gfs2_sbd *sdp, struct rgrp_list *rg,
		     uint64_t errblock)
{
	int x = errblock - rg->ri.ri_addr;

	log_err("Block #%"PRIu64" (0x%" PRIx64") (%d of %d) is neither"
		" GFS2_METATYPE_RB nor GFS2_METATYPE_RG.\n",
		rg->bh[x]->b_blocknr, rg->bh[x]->b_blocknr,
		(int)x+1, (int)rg->ri.ri_length);
	if (query(&opts, "Fix the RG? (y/n)")) {

		log_err("Attempting to repair the RG.\n");
		rg->bh[x] = bread(sdp, rg->ri.ri_addr + x);
		if (x) {
			struct gfs2_meta_header mh;

			mh.mh_magic = GFS2_MAGIC;
			mh.mh_type = GFS2_METATYPE_RB;
			mh.mh_format = GFS2_FORMAT_RB;
			gfs2_meta_header_out(&mh, rg->bh[x]->b_data);
		} else {
			memset(&rg->rg, 0, sizeof(struct gfs2_rgrp));
			rg->rg.rg_header.mh_magic = GFS2_MAGIC;
			rg->rg.rg_header.mh_type = GFS2_METATYPE_RG;
			rg->rg.rg_header.mh_format = GFS2_FORMAT_RG;
			rg->rg.rg_free = rg->ri.ri_data;
			gfs2_rgrp_out(&rg->rg, rg->bh[x]->b_data);
		}
		brelse(rg->bh[x], updated);
		return 0;
	}
	return 1;
}

/*
 * rg_repair - try to repair a damaged rg index (rindex)
 * trust_lvl - This is how much we trust the rindex file.
 *             blind_faith means we take the rindex at face value.
 *             open_minded means it might be okay, but we should verify it.
 *             distrust means it's not to be trusted, so we should go to
 *             greater lengths to build it from scratch.
 */
int rg_repair(struct gfs2_sbd *sdp, int trust_lvl, int *rg_count)
{
	int error, descrepencies;
	osi_list_t expected_rglist;
	int calc_rg_count, rgcount_from_index, rg;
	osi_list_t *exp, *act; /* expected, actual */
	struct gfs2_rindex buf;

	if (trust_lvl == blind_faith)
		return 0;
	else if (trust_lvl == open_minded) { /* If we can't trust RG index */
		/* Calculate our own RG index for comparison */
		error = gfs2_rindex_calculate(sdp, &expected_rglist,
					       &calc_rg_count);
		if (error) { /* If calculated RGs don't match the fs */
			gfs2_rgrp_free(&expected_rglist, not_updated);
			return -1;
		}
	}
	else if (trust_lvl == distrust) { /* If we can't trust RG index */
		error = gfs2_rindex_rebuild(sdp, &expected_rglist,
					     &calc_rg_count);
		if (error) {
			log_crit("Error rebuilding rg list.\n");
			gfs2_rgrp_free(&expected_rglist, not_updated);
			return -1;
		}
		sdp->rgrps = calc_rg_count;
	}
	/* Read in the rindex */
	osi_list_init(&sdp->rglist); /* Just to be safe */
	rindex_read(sdp, 0, &rgcount_from_index);
	if (sdp->md.riinode->i_di.di_size % sizeof(struct gfs2_rindex)) {
		log_warn("WARNING: rindex file is corrupt.\n");
		gfs2_rgrp_free(&expected_rglist, not_updated);
		gfs2_rgrp_free(&sdp->rglist, not_updated);
		return -1;
	}
	log_warn("L%d: number of rgs expected     = %d.\n", trust_lvl + 1,
		 sdp->rgrps);
	if (calc_rg_count != sdp->rgrps) {
		log_warn("L%d: They don't match; either (1) the fs was extended, (2) an odd\n", trust_lvl + 1);
		log_warn("L%d: rg size was used, or (3) we have a corrupt rg index.\n", trust_lvl + 1);
		gfs2_rgrp_free(&expected_rglist, not_updated);
		gfs2_rgrp_free(&sdp->rglist, not_updated);
		return -1;
	}
	/* ------------------------------------------------------------- */
	/* Now compare the rindex to what we think it should be.         */
	/* See how far off our expected values are.  If too much, abort. */
	/* The theory is: if we calculated the index to have 32 RGs and  */
	/* we have a large number that are completely wrong, we should   */
	/* abandon this method of recovery and try a better one.         */
	/* ------------------------------------------------------------- */
	descrepencies = 0;
	for (rg = 0, act = sdp->rglist.next, exp = expected_rglist.next;
	     act != &sdp->rglist && exp != &expected_rglist;
	     act = act->next, exp = exp->next, rg++) {
		struct rgrp_list *expected, *actual;

		expected = osi_list_entry(exp, struct rgrp_list, list);
		actual = osi_list_entry(act, struct rgrp_list, list);
		if (!ri_equal(actual->ri, expected->ri, ri_addr) ||
		    !ri_equal(actual->ri, expected->ri, ri_length) ||
		    !ri_equal(actual->ri, expected->ri, ri_data0) ||
		    !ri_equal(actual->ri, expected->ri, ri_data) ||
		    !ri_equal(actual->ri, expected->ri, ri_bitbytes)) {
			descrepencies++;
		}
	}
	if (trust_lvl < distrust && descrepencies > (trust_lvl * 8)) {
		log_warn("Level %d didn't work.  Too many descepencies.\n",
			 trust_lvl + 1);
		log_warn("%d out of %d RGs did not match what was expected.\n",
			 descrepencies, rg);
		gfs2_rgrp_free(&expected_rglist, not_updated);
		gfs2_rgrp_free(&sdp->rglist, not_updated);
		return -1;
	}
	/* ------------------------------------------------------------- */
	/* Now compare the rindex to what we think it should be.         */
	/* Our rindex should be pretty predictable unless we've grown    */
	/* so look for index problems first before looking at the rgs.   */
	/* ------------------------------------------------------------- */
	for (rg = 0, act = sdp->rglist.next, exp = expected_rglist.next;
	     act != &sdp->rglist && exp != &expected_rglist;
	     act = act->next, exp = exp->next, rg++) {
		struct rgrp_list *expected, *actual;

		expected = osi_list_entry(exp, struct rgrp_list, list);
		actual = osi_list_entry(act, struct rgrp_list, list);
		ri_compare(rg, actual->ri, expected->ri, ri_addr, PRIx64);
		ri_compare(rg, actual->ri, expected->ri, ri_length, PRIx32);
		ri_compare(rg, actual->ri, expected->ri, ri_data0, PRIx64);
		ri_compare(rg, actual->ri, expected->ri, ri_data, PRIx32);
		ri_compare(rg, actual->ri, expected->ri, ri_bitbytes,
			   PRIx32);
		/* If we modified the index, write it back to disk. */
		if (rindex_modified) {
			if (query(&opts, "Fix the index? (y/n)")) {
				gfs2_rindex_out(&expected->ri, (char *)&buf);
				gfs2_writei(sdp->md.riinode, (char *)&buf,
					    rg * sizeof(struct gfs2_rindex),
					    sizeof(struct gfs2_rindex));
				actual->ri.ri_addr = expected->ri.ri_addr;
				actual->ri.ri_length = expected->ri.ri_length;
				actual->ri.ri_data0 = expected->ri.ri_data0;
				actual->ri.ri_data = expected->ri.ri_data;
				actual->ri.ri_bitbytes =
					expected->ri.ri_bitbytes;
				/* If our rindex was hosed, ri_length is bad */
				/* Therefore, gfs2_compute_bitstructs might  */
				/* have malloced the wrong length for bitmap */
				/* buffers.  So we have to redo it.          */
				if (actual->bh)
					free(actual->bh);
				if (actual->bits)
					free(actual->bits);
				gfs2_compute_bitstructs(sdp, actual);
			}
			else
				log_err("RG index not fixed.\n");
			rindex_modified = FALSE;
			
		}
	}
	/* ------------------------------------------------------------- */
	/* Read the real RGs and check their integrity.                  */
	/* Now we can somewhat trust the rindex and the RG addresses,    */
	/* so let's read them in, check them and optionally fix them.    */
	/* ------------------------------------------------------------- */
	for (rg = 0, act = sdp->rglist.next; act != &sdp->rglist;
	     act = act->next, rg++) {
		struct rgrp_list *rgd;
		uint64_t prev_err = 0, errblock;
		int i;

		/* Now we try repeatedly to read in the rg.  For every block */
		/* we encounter that has errors, repair it and try again.    */
		i = 0;
		do {
			rgd = osi_list_entry(act, struct rgrp_list, list);
			errblock = gfs2_rgrp_read(sdp, rgd);
			if (errblock) {
				if (errblock == prev_err)
					break;
				prev_err = errblock;
				rewrite_rg_block(sdp, rgd, errblock);
			}
			else {
				gfs2_rgrp_relse(rgd, not_updated);
				break;
			}
			i++;
		} while (i < rgd->ri.ri_length);
	}
	*rg_count = rg;
	gfs2_rgrp_free(&expected_rglist, not_updated);
	gfs2_rgrp_free(&sdp->rglist, not_updated);
	return 0;
}
