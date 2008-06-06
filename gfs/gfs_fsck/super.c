#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>

#include "osi_list.h"
#include "osi_user.h"
#include "bio.h"
#include "util.h"
#include "file.h"
#include "rgrp.h"
#include "fsck.h"
#include "ondisk.h"
#include "super.h"
#include "fsck_incore.h"

#ifndef BLKGETSIZE64
#define BLKGETSIZE64 _IOR(0x12, 114, size_t)
#endif

#define DIV_RU(x, y) (((x) + (y) - 1) / (y))
#define ri_compare(rg, ondisk, expected, field, fmt)	\
	if (ondisk.field != expected.field) { \
		log_warn("rgindex #%d " #field " discrepancy: index 0x%" fmt \
				 " != expected: 0x%" fmt "\n", \
				 rg + 1, ondisk.field, expected.field);	\
		ondisk.field = expected.field; \
		rgindex_modified = TRUE; \
	}

static uint64 total_journal_space;

/**
 * check_sb - Check superblock
 * @sdp: the filesystem
 * @sb: The superblock
 *
 * Checks the version code of the FS is one that we understand how to
 * read and that the sizes of the various on-disk structures have not
 * changed.
 *
 * Returns: 0 on success, -1 on failure
 */
static int check_sb(struct fsck_sb *sdp, struct gfs_sb *sb)
{
	int error = 0;
	if (sb->sb_header.mh_magic != GFS_MAGIC ||
	    sb->sb_header.mh_type != GFS_METATYPE_SB){
		log_crit("Either the super block is corrupted, or this "
			 "is not a GFS filesystem\n");
		log_debug("Header magic: %X Header Type: %X\n",
			  sb->sb_header.mh_magic,
			  sb->sb_header.mh_type);
		error = -EINVAL;
		goto out;
	}

	/*  If format numbers match exactly, we're done.  */
	if (sb->sb_fs_format != GFS_FORMAT_FS ||
	    sb->sb_multihost_format != GFS_FORMAT_MULTI){
		log_warn("Old file system detected.\n");
	}

 out:
	return error;
}


/*
 * read_sb: read the super block from disk
 * sdp: in-core super block
 *
 * This function reads in the super block from disk and
 * initializes various constants maintained in the super
 * block
 *
 * Returns: 0 on success, -1 on failure.
 */
int read_sb(struct fsck_sb *sdp)
{
	osi_buf_t *bh;
	uint64 space = 0;
	unsigned int x;
	int error;
	error = get_and_read_buf(sdp, GFS_SB_ADDR >> sdp->fsb2bb_shift, &bh, 0);
	if (error){
		log_crit("Unable to read superblock\n");
		goto out;
	}

	gfs_sb_in(&sdp->sb, BH_DATA(bh));

	relse_buf(sdp, bh);

	error = check_sb(sdp, &sdp->sb);
	if (error)
		goto out;

/* FIXME: Need to verify all this */
	/* FIXME: What's this 9? */
	sdp->fsb2bb_shift = sdp->sb.sb_bsize_shift - 9;
	sdp->diptrs =
		(sdp->sb.sb_bsize - sizeof(struct gfs_dinode)) /
		sizeof(uint64);
	sdp->inptrs =
		(sdp->sb.sb_bsize - sizeof(struct gfs_indirect)) /
		sizeof(uint64);
	sdp->jbsize = sdp->sb.sb_bsize - sizeof(struct gfs_meta_header);
	/* FIXME: Why is this /2 */
	sdp->hash_bsize = sdp->sb.sb_bsize / 2;
	sdp->hash_ptrs = sdp->hash_bsize / sizeof(uint64);
	sdp->heightsize[0] = sdp->sb.sb_bsize -
		sizeof(struct gfs_dinode);
	sdp->heightsize[1] = sdp->sb.sb_bsize * sdp->diptrs;
	for (x = 2; ; x++){
		space = sdp->heightsize[x - 1] * sdp->inptrs;
		/* FIXME: Do we really need this first check?? */
		if (space / sdp->inptrs != sdp->heightsize[x - 1] ||
		    space % sdp->inptrs != 0)
			break;
		sdp->heightsize[x] = space;
	}
	sdp->max_height = x;
	if(sdp->max_height > GFS_MAX_META_HEIGHT){
		log_err("Bad max metadata height.\n");
		error = -1;
		goto out;
	}

	sdp->jheightsize[0] = sdp->sb.sb_bsize -
		sizeof(struct gfs_dinode);
	sdp->jheightsize[1] = sdp->jbsize * sdp->diptrs;
	for (x = 2; ; x++){
		space = sdp->jheightsize[x - 1] * sdp->inptrs;
		if (space / sdp->inptrs != sdp->jheightsize[x - 1] ||
		    space % sdp->inptrs != 0)
			break;
		sdp->jheightsize[x] = space;
	}
	sdp->max_jheight = x;
	if(sdp->max_jheight > GFS_MAX_META_HEIGHT){
		log_err("Bad max jheight.\n");
		error = -1;
	}

 out:

	return error;
}


/*
 * ji_update - fill in journal info
 * ip: the journal index inode
 *
 * Given the inode for the journal index, read in all
 * the journal indexes.
 *
 * Returns: 0 on success, -1 on failure
 */
int ji_update(struct fsck_sb *sdp)
{
	struct fsck_inode *ip = sdp->jiinode;
	char buf[sizeof(struct gfs_jindex)];
	unsigned int j;
	int error=0;


	if(ip->i_di.di_size % sizeof(struct gfs_jindex) != 0){
		log_err("The size reported in the journal index"
			" inode is not a\n"
			 "\tmultiple of the size of a journal index.\n");
		return -1;
	}

	if(!(sdp->jindex = (struct gfs_jindex *)malloc(ip->i_di.di_size))) {
		log_err("Unable to allocate journal index\n");
		return -1;
	}
	if(!memset(sdp->jindex, 0, ip->i_di.di_size)) {
		log_err("Unable to zero journal index\n");
		return -1;
	}
	total_journal_space = 0;

	for (j = 0; ; j++) {
		struct gfs_jindex *journ;
		error = readi(ip, buf, j * sizeof(struct gfs_jindex),
				 sizeof(struct gfs_jindex));
		if(!error)
			break;
		if (error != sizeof(struct gfs_jindex)){
			log_err("An error occurred while reading the"
				" journal index file.\n");
			goto fail;
		}

		journ = sdp->jindex + j;
		gfs_jindex_in(journ, buf);
		total_journal_space += journ->ji_nsegment * sdp->sb.sb_seg_size;
	}


	if(j * sizeof(struct gfs_jindex) != ip->i_di.di_size){
		log_err("journal inode size invalid\n");
		log_debug("j * sizeof(struct gfs_jindex) !="
			  " ip->i_di.di_size\n");
		log_debug("%d != %d\n",
			  j * sizeof(struct gfs_jindex), ip->i_di.di_size);
		goto fail;
	}
	sdp->journals = j;
	log_debug("%d journals found.\n", j);

	return 0;

 fail:
	free(sdp->jindex);
	return -1;
}

/* Print out debugging information in same format as gfs_edit. */
int hexdump(uint64 startaddr, const unsigned char *lpBuffer, int len)
{
	const unsigned char *pointer, *ptr2;
	int i;
	uint64 l;

	pointer = (unsigned char *)lpBuffer;
	ptr2 = (unsigned char *)lpBuffer;
	l = 0;
	while (l < len) {
		log_info("%.8" PRIX64, startaddr + l);
		for (i = 0; i < 16; i++) { /* first print it in hex */
			if (i % 4 == 0)
				log_info(" ");
			log_info("%02X", *pointer);
			pointer++;
		}
		log_info(" [");
		for (i = 0; i < 16; i++) { /* now print it in character format */
			if ((*ptr2 >= ' ') && (*ptr2 <= '~'))
				log_info("%c", *ptr2);
			else
				log_info(".");
			ptr2++;
		}
		log_info("] \n");
		l += 16;
	}
	return (len);
}


/**
 * rgrplength2bitblocks - Stolen from gfs_mkfs.
 *
 * @sdp:    the superblock
 * @length: the number of blocks in a RG
 *
 * Give a number of blocks in a RG, figure out the number of blocks
 * needed for bitmaps.
 *
 * Returns: the number of bitmap blocks
 */

uint32 rgrplength2bitblocks(struct fsck_sb *sdp, uint32 length)
{
	uint32 bitbytes;
	uint32 old_blocks = 0, blocks;
	int tries = 0;
	
	for (;;) {
		bitbytes = (length - old_blocks) / GFS_NBBY;
		blocks = 1;

		if (bitbytes > sdp->sb.sb_bsize - sizeof(struct gfs_rgrp)) {
			bitbytes -= sdp->sb.sb_bsize - sizeof(struct gfs_rgrp);
			blocks += DIV_RU(bitbytes, (sdp->sb.sb_bsize -
					      sizeof(struct gfs_meta_header)));
		}
		if (blocks == old_blocks)
			break;
		old_blocks = blocks;
		if (tries++ > 10) {
			blocks = 0;
			break;
		}
	}
	return blocks;
}

/*
 * gfs_rgindex_rebuild - rebuild a corrupt Resource Group (RG) index manually
 *                       where trust_lvl == distrust
 *
 * If this routine is called, it means we have RGs in odd/unexpected places,
 * and there is a corrupt RG.  In other words, we can't trust the RG index
 * is completely sane, and the RGs don't fit on nice neat fs boundaries.
 * So we have no choice but to go through the count them by hand.
 * We've tried twice to recover the RGs and RG index, and failed.  This is
 * our last chance to remedy the situation.
 *
 * In some cases, we have no choice but to trust the rgindex file.  Since
 * it is completely hidden from the users, if we find an inconsistency,
 * it's safer to assume the index is correct and the RG is corrupt rather
 * than the RG is correct and the index is bad.  This routine goes to great
 * lengths to determine which is the case and figure it out regardless.
 *
 * This routine tries to minimize performance impact by:
 * 1. Skipping through the filesystem at known increments when possible.
 * 2. Shuffle through every block when RGs are not found at the predicted
 *    locations.
 *
 * Note: A GFS filesystem is built by gfs_mkfs into several "subdevices."
 * These are just logical divisions of the logical volume.  The gfs_mkfs
 * program considers two types of subdevices: RG-subdevices and journal
 * subdevices.  RG subdevices contain one or more RGs.  Journal subdevices
 * contain one or more journals.  For the purposes of gfs_fsck, when I talk
 * about subdevices, I'm talking about RG-subdevices only.  For a freshly
 * created GFS filesystem, the logical volume will be broken apart like this:
 *
 * RG-subdevice 0: n Resource Groups
 * Journal subdevice
 * RG-subdevice 1: n Resource Groups (same as RG-subdevice 0)
 *
 * If the filesystem has been resized via gfs_grow, there will be more
 * RG-subdevices containing more RGs.  However, gfs_fsck treats them all
 * as "subdevice 2."
 *
 * NOTE: When giving messages to the users, I am referring to them as
 * "sections" 1, 2, and 3 because "subdevice" sounds too confusing.
 *
 * If an RG is not found at a predicted location, it either means that
 * there is a corrupted RG, or else the RG has been added after the fact
 * by gfs_grow.  We can only predict the locations for RGs within a subdevice,
 * but the subdevice boundaries are not that predictable.  (Actually, they
 * are, but since we're dealing with a likely corrupt filesystem, I don't
 * want to rely on good data too much to do it this way.)
 *
 * I am, however, going to rely on the fact that the first original subdevice
 * will have the same number of RGs as the second original subdevice.
 * Other RGs found after that will be considered "extra."
 */
int gfs_rgindex_rebuild(struct fsck_sb *sdp, osi_list_t *ret_list,
						int *num_rgs)
{
	osi_buf_t *bh; /* buffer handle */
	uint64 subdevice_size, fs_total_size;
	int number_of_rgs; /* #RGs this subdevice.
						  min of 2 per segment * 2 segments = 4 */
	int rg_number; /* real RG number (0 - x) */
	int subd;
	int error, corrupt_rgs;
	int rgi, rgs_per_subd;
	uint64 blok, block_of_last_rg;
	uint64 block_bump;
	uint64 shortest_dist_btwn_rgs[2]; /* one for each subdevice */
	uint64 first_rg_dist[2], initial_first_rg_dist[2];
	struct fsck_rgrp *calc_rgd, *prev_rgd;
	struct gfs_rgrp tmp_rgrp;
	osi_list_t *tmp;
	int rg_was_fnd = FALSE;
	struct gfs_rindex buf, tmpndx;
	uint64 fs_size_from_rgindex = 0;
	int index_entries_per_subd = 0, subd_ndx_entry, rg;
	uint64_t last_known_ri_addr = 0, prev_known_ri_addr = 0;
	uint32_t last_known_ri_length = 0;
	uint32_t last_known_ri_data = 0;

	osi_list_init(ret_list);
	*num_rgs = 0;
	/* Get the total size of the device */
	error = ioctl(sdp->diskfd, BLKGETSIZE64,
				  &fs_total_size); /* Size in bytes */
	fs_total_size /= sdp->sb.sb_bsize;
	log_debug("fs_total_size = 0x%" PRIX64 " blocks.\n", fs_total_size);
	block_of_last_rg = 0;
	subdevice_size = 0;
	rgs_per_subd = 0;
	/* ----------------------------------------------------------------- */
	/* First, figure out the exact end of the second subdevice.          */
	/* That will tell us where the third RG-subdevice should start.      */
	/* We need to keep track of how many entries are in the index before */
	/* we hit the journal blocks.  That will tell us how many index      */
	/* entries will be in the first subdevice, and the second subdevice  */
	/* should have the same number.  After that, we don't care.          */
	/* Note: we're using values in the rgindex, even though we don't     */
	/* trust it.  We're relatively okay because we're just trying to     */
	/* find the highest RG value for the second subdevice.               */
	/* ----------------------------------------------------------------- */
	subd = 0;
	index_entries_per_subd = 0;
	subd_ndx_entry = 0;
	for (rg = 0; ; rg++) {
		uint64 end_of_rg;
		
		error = readi(sdp->riinode,
					  (char *)&buf, rg * sizeof(struct gfs_rindex),
					  sizeof(struct gfs_rindex));
		if (!error) /* if end of file */
			break; /* stop looking */
		gfs_rindex_in(&tmpndx, (char *)&buf); /* read in the index */
		subd_ndx_entry++;
		if (!subd) { /* if we're still in the first subdevice */
			if (tmpndx.ri_addr >= sdp->jindex->ji_addr + total_journal_space) {
				subd++; /* this rgindex belongs to the second subdevice */
				prev_known_ri_addr = 0;
				last_known_ri_addr = 0;
				subd_ndx_entry = 1;
			}
			else {
				index_entries_per_subd++;
				/* Check if this is the last index entry for subdevice */
				if (tmpndx.ri_addr + tmpndx.ri_length + tmpndx.ri_data >= 
					sdp->jindex->ji_addr - GFS_NBBY) {
					subd++; /* NEXT rgindex belongs to the second subdevice */
					subd_ndx_entry = 0;
					prev_known_ri_addr = 0;
					last_known_ri_addr = 0;
					continue;
				}
			}
		}
		end_of_rg = tmpndx.ri_addr + tmpndx.ri_length + tmpndx.ri_data;
		/* ----------------------------------------------------------------- */
		/* Make sure the rgindex looks relatively sane.  After all,          */
		/* at this stage of the game, we don't trust it.                     */
		/* ----------------------------------------------------------------- */
		if (subd && end_of_rg > sdp->jindex->ji_addr + total_journal_space &&
			end_of_rg <= fs_total_size) { /* looks relatively sane */
			/* Save some data values we can fall back on: */
			prev_known_ri_addr = last_known_ri_addr;
			last_known_ri_addr = tmpndx.ri_addr;
			last_known_ri_length = tmpndx.ri_length;
			last_known_ri_data = tmpndx.ri_data;
			if (fs_size_from_rgindex < end_of_rg)
				fs_size_from_rgindex = end_of_rg;
			/* Quit after we hit the same number of entries as 1st subdevice */
			if (subd_ndx_entry >= index_entries_per_subd)
				break;
		}
		else if (!subd && end_of_rg < sdp->jindex->ji_addr &&
				 end_of_rg > 0) { /* looks relatively sane */
			/* Save some data values we can fall back on: */
			prev_known_ri_addr = last_known_ri_addr;
			last_known_ri_addr = tmpndx.ri_addr;
			last_known_ri_length = tmpndx.ri_length;
			last_known_ri_data = tmpndx.ri_data;
			if (fs_size_from_rgindex < end_of_rg)
				fs_size_from_rgindex = end_of_rg;
		}
		else { /* Otherwise we have a corrupt index entry */
			log_debug("Likely damage to rgindex entry %d.\n",
					  subd_ndx_entry + (subd * index_entries_per_subd));
			if (prev_known_ri_addr) {
				/* Try to extrapolate from the previous one */
				tmpndx.ri_addr = last_known_ri_addr + 
					(last_known_ri_addr - prev_known_ri_addr);
				tmpndx.ri_length = last_known_ri_length;
				tmpndx.ri_data = last_known_ri_data;
				log_debug("Extrapolating addr=0x%" PRIx64 ", length=0x%x, "
						  "data=%x\n", tmpndx.ri_addr,
						  tmpndx.ri_length, tmpndx.ri_data);
				end_of_rg = tmpndx.ri_addr + tmpndx.ri_length +
					tmpndx.ri_data;
				if (end_of_rg > sdp->jindex->ji_addr +
					total_journal_space &&
					end_of_rg <= fs_total_size) { /* looks relatively okay */
					/* Adjust data values we can fall back on: */
					last_known_ri_addr = tmpndx.ri_addr;
					if (fs_size_from_rgindex < end_of_rg)
						fs_size_from_rgindex = end_of_rg;
					/* Quit after we hit the same number of entries as
					   the first subdevice/section */
					if (subd_ndx_entry >= index_entries_per_subd)
						break;
				}
			} /* if we have a good previous */
			else {
				log_debug("Not enough data to figure it out--skipped.\n");
			}
		} /* corrupt RG index entry */
	} /* for all RGs in the index */
	log_debug("Index entries/section=%d, third section addr = 0x%"PRIx64"\n",
			  index_entries_per_subd, fs_size_from_rgindex);
	initial_first_rg_dist[0] = first_rg_dist[0] = sdp->jindex->ji_addr -
		((GFS_SB_ADDR >> sdp->fsb2bb_shift) + 1);
	initial_first_rg_dist[1] = first_rg_dist[1] = sdp->jindex->ji_addr;
	/* ----------------------------------------------------------------- */
	/* Now let's figure out the space between RGs for the first subd,    */
	/* and the second subd.  Subsequent RGs will be unpredictable.       */
	/* We need to know the distance between RGs because if one is        */
	/* corrupt or overwritten, we need to salvage it at the correct      */
	/* location.  For example, if RG #2 is nuked, at first glance, it    */
	/* appears as if our RGs are twice as far apart as they should be.   */
	/* So we should chase down a couple to get more than one opinion.    */
	/* If several RGs are nuked, sorry, I'll only go so far to recover.  */
	/* This check will be slower because we have to read blocks 1 by 1.  */
	/* Luckily, we only have to do a few this way.                       */
	/* Later, we can bump ahead by the amount we find here.              */
	/* ----------------------------------------------------------------- */
	subdevice_size = sdp->jindex->ji_addr; /* addr of first journal */;
	for (subd = 0; subd < 2; subd++) {
		uint64 start_block;

		if (!subd)
			start_block = (GFS_SB_ADDR >> sdp->fsb2bb_shift) + 1;
		else
			start_block = sdp->jindex->ji_addr + total_journal_space;
		block_of_last_rg = start_block;
		number_of_rgs = 0;
		shortest_dist_btwn_rgs[subd] = subdevice_size;
		for (blok = start_block; blok < fs_total_size; blok++) {
			error = get_and_read_buf(sdp, blok, &bh, 0);
			if (error){
				log_crit("Unable to read block 0x%" PRIX64 "\n", blok);
				return -1;
			}
			if ((blok == start_block) || /* If first RG block or */
				!check_type(bh, GFS_METATYPE_RG)) { /* we found an RG */
				log_debug("%d:RG found at block 0x%" PRIx64 "\n", subd + 1,
						  blok);
				/* If we spilled into the next subdevice, quit. */
				if (blok + GFS_NBBY >= start_block + subdevice_size) {
					log_debug("This is in the next subdevice--skipping.\n");
					break;
				}
				gfs_rgrp_in(&tmp_rgrp, BH_DATA(bh));
				if (blok == start_block) {
					shortest_dist_btwn_rgs[subd] = subdevice_size;
					log_debug("Start of section %d.\n", subd + 1);
				}
				else {
					uint64 rgdist;

					rgdist = blok - block_of_last_rg;
					log_debug("%d:dist 0x%" PRIx64 " = 0x% " PRIx64
							  " - 0x%" PRIx64, subd + 1, rgdist,
							  blok, block_of_last_rg);
					/* ----------------------------------------------------- */
					/* We found another RG.  Check to see if we need to set  */
					/* the first_rg_dist based on whether it's still at its  */
					/* initial value (i.e. the whole subdevice size).        */
					/* The first rg distance is different from the rest      */
					/* because of the superblock and 64K dead space          */
					/* ----------------------------------------------------- */
					if (first_rg_dist[subd] == initial_first_rg_dist[subd])
						first_rg_dist[subd] = rgdist;
					if (rgdist < shortest_dist_btwn_rgs[subd])
					{
						shortest_dist_btwn_rgs[subd] = rgdist;
						log_debug("(shortest so far)\n");
					}
					else
						log_debug("\n");
				}
				number_of_rgs++; /* number of RGs this subdevice */
				/* --------------------------------------------------------- */
				/* Check to see if we're the last RG we want to examine.     */
				/* If so, forget checking the next index entry and exit.     */
				/* (The next index entry may be for the next RG anyway).     */
				/* --------------------------------------------------------- */
				if (number_of_rgs >= 4 ||
					number_of_rgs >= index_entries_per_subd)
					break;
				/* --------------------------------------------------------- */
				/* Read in the index entry for the NEXT RG in line and       */
				/* compare the RG size difference with what we know.         */
				/* --------------------------------------------------------- */
				rg_number = number_of_rgs + (subd * index_entries_per_subd);
				error = readi(sdp->riinode, (char *)&buf,
							  rg_number * sizeof(struct gfs_rindex),
							  sizeof(struct gfs_rindex));
				if (error) { /* if we read some data (no error really) */
					gfs_rindex_in(&tmpndx, (char *)&buf);
					if (tmpndx.ri_addr > start_block &&
						tmpndx.ri_addr < fs_total_size &&
						tmpndx.ri_addr != blok &&
						tmpndx.ri_addr - blok < 
						shortest_dist_btwn_rgs[subd]) {
						shortest_dist_btwn_rgs[subd] =
							tmpndx.ri_addr - blok;
						log_debug("Section %d RG %d(%d): shortest=0x%"PRIx64
								  "\n", subd + 1, number_of_rgs, rg_number,
								  shortest_dist_btwn_rgs[subd]);
					}
				}
				block_of_last_rg = blok;
				/* --------------------------------------------------------- */
				/* We can't just check every block because some of the files */
				/* in the fs (i.e. inside an RG) might have data that looks  */
				/* exactly like a valid RG. Sounds farfetched, but it's not, */
				/* based on my own experiences.                              */
				/* In my experience, the RG locations are spaced differently */
				/* from their used and free space numbers because of the way */
				/* gfs_mkfs puts them.  In other words, the RG locations     */
				/* according to the index will be different from the sum of  */
				/* the space they take.  Why?  I don't know, but maybe it    */
				/* has to do with the variable size of bitmaps.              */
				/* At any rate, used+free can get us close, but not exact.   */
				/* Therefore, we have to search for the RG after that.       */
				/* --------------------------------------------------------- */
				if (!error &&
					((tmp_rgrp.rg_useddi + tmp_rgrp.rg_free + 1) >> 2) ==
					(tmpndx.ri_addr >> 2)) {
					blok = tmpndx.ri_addr - 1; /* go by the index */
					log_debug("I(0x%" PRIx64 ")\n", blok);
				}
				else {
					blok += tmp_rgrp.rg_useddi + tmp_rgrp.rg_free;
					log_debug("R(0x%" PRIx64 ")\n", blok);
				}
			} /* If first RG block or RG */
			relse_buf(sdp, bh); /* release the read buffer */
		} /* for blok */
		/* -------------------------------------------------------------- */
		/* Sanity-check our first_rg_dist. If RG #2 got nuked, the        */
		/* first_rg_dist would measure from #1 to #3, which would be bad. */
		/* We need to take remedial measures to fix it (from the index).  */
		/* -------------------------------------------------------------- */
		if (first_rg_dist[subd] >= shortest_dist_btwn_rgs[subd] +
			(shortest_dist_btwn_rgs[subd] / 4)) {
			log_debug("%d:Shortest dist is: 0x%" PRIx64 "\n", subd + 1,
					  shortest_dist_btwn_rgs[subd]);
			/* read in the second RG index entry for this subd. */
			readi(sdp->riinode, (char *)&buf,
				  (1 + (subd * index_entries_per_subd)) *
				  sizeof(struct gfs_rindex),
				  sizeof(struct gfs_rindex));
			gfs_rindex_in(&tmpndx, (char *)&buf);
			if (tmpndx.ri_addr > start_block) { /* sanity check */
				log_warn("RG %d is damaged: recomputing RG dist from index: ",
						 2 + (subd * index_entries_per_subd));
				first_rg_dist[subd] = tmpndx.ri_addr - start_block;
				log_warn("0x%" PRIx64 "\n", first_rg_dist[subd]);
			}
			else {
				log_warn("RG index %d is damaged: extrapolating RG dist: ",
						 2 + (subd * index_entries_per_subd));
				first_rg_dist[subd] = (subdevice_size - start_block) %
					((index_entries_per_subd - 1) * 
					 shortest_dist_btwn_rgs[subd]);
				log_warn("0x%" PRIx64 "\n", first_rg_dist[subd]);
			}
		} /* if first RG distance is within tolerance */
		log_debug("First RG distance: 0x%" PRIx64 "\n", first_rg_dist[subd]);
		log_debug("Section %d: distance between RGs: 0x%" PRIx64 "\n",
				 subd + 1, shortest_dist_btwn_rgs[subd]);
		log_debug("Section size: 0x%" PRIx64 "\n", subdevice_size);
	} /* for subd */
	number_of_rgs = 0; /* reset this because it is reused below */
	/* ----------------------------------------------------------------- */
	/* Start reading the filesystem starting with the block after the    */
	/* superblock, which should be the first RG.                         */
	/* The problem is that gfs_grow puts the RGs at unpredictable        */
	/* locations.  If the fs was only grown once, that would be          */
	/* predictable.  But if it grows twice, by different amounts, then   */
	/* our RGs could be anywhere.  After carefully studying the problem  */
	/* I've determined that the best thing we can do is to trust the     */
	/* rgindex and hope to God it's correct.  That's the only way we're  */
	/* going to be able to recover RGs in the third section.             */
	/* ----------------------------------------------------------------- */
	prev_rgd = NULL;
	block_bump = first_rg_dist[0];
	corrupt_rgs = 0;
	for (subd = 0; subd < 3; subd++) { /* third subdevice is for all RGs
										  extended past the normal 2 with
										  gfs_grow, etc. */
		uint64 start_block, end_block;

		if (subd == 0) {
			start_block = (GFS_SB_ADDR >> sdp->fsb2bb_shift) + 1;
			end_block = subdevice_size - 1;
		}
		else if (subd == 1) {
			start_block = sdp->jindex->ji_addr + total_journal_space;
			/* Moral dilemma: should we go to the last block or should */
			/* we trust the index?  If they're close to one another,   */
			/* let's use the index.                                    */
			if ((fs_size_from_rgindex >> 2) ==
				((start_block + subdevice_size - 1) >> 2)) /* if we're close */
				end_block = fs_size_from_rgindex - 1;  /* trust the index */
			else                                       /* otherwise */
				end_block = start_block + subdevice_size - 1; /* go to end */
		}
		else {
			start_block = end_block + 1;
			end_block = fs_total_size;
			if (start_block + GFS_NBBY >= end_block)
				break;
		}
		log_warn("Section %d: 0x%" PRIx64 " - 0x%" PRIx64 "\n", subd + 1,
				 start_block, end_block);
		for (blok = start_block; blok <= end_block; blok += block_bump) {
			uint64 fwd_block;
			int bitmap_was_fnd;

			log_debug("Block 0x%" PRIx64 "\n", blok);
			error = get_and_read_buf(sdp, blok, &bh, 0);
			if (error) {
				log_crit("Unable to read block 0x%" PRIX64 "\n", blok);
				return -1;
			}
			rg_was_fnd = (!check_type(bh, GFS_METATYPE_RG));
			relse_buf(sdp, bh); /* release the read buffer */
			/* ------------------------------------------------------------- */
			/* For the first and second subdevice, we know the RG size.      */
			/* Since we're bumping by that amount, this better be an RG.     */
			/* ------------------------------------------------------------- */
			/* Allocate a new RG and index. */
			calc_rgd = (struct fsck_rgrp *)malloc(sizeof(struct fsck_rgrp));
			memset(calc_rgd, 0, sizeof(struct fsck_rgrp));
			calc_rgd->rd_sbd = sdp; /* hopefully this is not used */
			osi_list_add_prev(&calc_rgd->rd_list, ret_list);
			calc_rgd->rd_ri.ri_length = 1;
			calc_rgd->rd_ri.ri_addr = blok;
			if (!rg_was_fnd) { /* if not an RG */
				/* ----------------------------------------------------- */
				/* This SHOULD be an RG but isn't.                       */
				/* ----------------------------------------------------- */
				corrupt_rgs++;
				if (corrupt_rgs < 5)
					log_debug("Missing or damaged RG at block 0x%" PRIx64 \
							  "\n", blok);
				else {
					log_crit("Error: too many bad RGs.\n");
					return -1;
				}
			}
			/* ------------------------------------------------ */
			/* Now go through and count the bitmaps for this RG */
			/* ------------------------------------------------ */
			bitmap_was_fnd = FALSE;
			for (fwd_block = blok + 1; fwd_block < fs_total_size; 
				 fwd_block++) {
				error = get_and_read_buf(sdp, fwd_block, &bh, 0);
				if (error){
					log_crit("Unable to read block 0x%" PRIX64 "\n",
							 fwd_block);
					return -1;
				}
				bitmap_was_fnd = (!check_type(bh, GFS_METATYPE_RB));
				relse_buf(sdp, bh);
				if (bitmap_was_fnd) /* if a bitmap */
					calc_rgd->rd_ri.ri_length++;
				else
					break; /* end of bitmap, so call it quits. */
			} /* for subsequent bitmaps */
			calc_rgd->rd_ri.ri_data1 = calc_rgd->rd_ri.ri_addr +
				calc_rgd->rd_ri.ri_length;
			if (prev_rgd) {
				prev_rgd->rd_ri.ri_data = block_bump -
					rgrplength2bitblocks(sdp, block_bump);
				prev_rgd->rd_ri.ri_data -= prev_rgd->rd_ri.ri_data %
					GFS_NBBY;
				prev_rgd->rd_ri.ri_bitbytes = prev_rgd->rd_ri.ri_data /
					GFS_NBBY;
				log_debug("Prev ri_data set to: %" PRIx32 ".\n",
						  prev_rgd->rd_ri.ri_data);
				/*prev_rgd->rd_ri.ri_data = block_bump;*/
			}
			number_of_rgs++;
			log_warn("%c RG %d at block 0x%" PRIX64 " %s",
					 (rg_was_fnd ? ' ' : '*'), number_of_rgs, blok,
					 (rg_was_fnd ? "intact" : "*** DAMAGED ***"));
			rgs_per_subd++;
			prev_rgd = calc_rgd;
			block_of_last_rg = blok;
			if (subd == 2) { /* if beyond the normal RGs into gfs_grow RGs  */
				/* -------------------------------------------------------- */
				/* RG location is rounded down to the nearest multiple of   */
				/* GFS_NBBY, so RG location is only known within a 4 block  */
				/* range.  It's better to use the rgindex to figure out     */
				/* the address of the next RG and bump by the difference.   */
				/* However, there's another complication:  gfs_grow has     */
				/* been known to add RGs to the index in a non-ascending    */
				/* order.  Therefore, we can't assume the Nth entry in the  */
				/* index corresponds to the Nth RG on disk.  Wish it was.   */
				/* Instead, we have to read all of the rgindex until we     */
				/* find an entry that has the smallest address greater than */
				/* the block we're on (blok).                               */
				/* -------------------------------------------------------- */
				uint64_t rgndx_next_block;

				rgndx_next_block = end_block;
				for (rgi = 0; ; rgi++) {
					error = readi(sdp->riinode, (char *)&buf,
								  rgi * sizeof(struct gfs_rindex),
								  sizeof(struct gfs_rindex));
					if (!error)      /* if end of the rgindex */
						break;        /* stop processing for more RGs */
					gfs_rindex_in(&tmpndx, (char *)&buf);
					/* if this index entry is the next RG physically */
					if (tmpndx.ri_addr > blok &&
						tmpndx.ri_addr < rgndx_next_block) {
						rgndx_next_block = tmpndx.ri_addr; /* remember it */
					}
				}
				block_bump = rgndx_next_block - blok;
				if (rgndx_next_block == end_block) { /* if no more RGs */
					log_warn(" [length 0x%" PRIx64 "]\n", block_bump);
					break;                 /* stop processing */
				}
			}
			else {
				if (blok == start_block)
					block_bump = first_rg_dist[subd];
				else
					block_bump = shortest_dist_btwn_rgs[subd];
			}
			if (block_bump != 1)
				log_warn(" [length 0x%" PRIx64 "]\n", block_bump);
		} /* for blocks in subdevice */
	} /* for subdevices */
	/* ------------------------------------------------------------------- */
	/* if we got to the end of the fs, we still need to fix the allocation */
	/* information for the very last RG.                                   */
	/* ------------------------------------------------------------------- */
	if (prev_rgd && !prev_rgd->rd_ri.ri_data) {
		log_debug("Prev ri_data set to: %" PRIx32 ".\n", block_bump);
		prev_rgd->rd_ri.ri_data = block_bump -
			rgrplength2bitblocks(sdp, block_bump);
		prev_rgd->rd_ri.ri_data -= prev_rgd->rd_ri.ri_data % GFS_NBBY;
		prev_rgd->rd_ri.ri_bitbytes = prev_rgd->rd_ri.ri_data / GFS_NBBY;
		prev_rgd = NULL; /* make sure we don't use it later */
	}
	/* else No previous to fix. */
	/* ---------------------------- */
	/* Now dump out the information */	
	/* ---------------------------- */
	log_debug("RG index rebuilt as follows:\n");
	for (tmp = ret_list->next, rgi = 0; tmp != ret_list;
		 tmp = tmp->next, rgi++) {
		calc_rgd = osi_list_entry(tmp, struct fsck_rgrp, rd_list);
		log_debug("%d: %x / 0x%" PRIx64 " / 0x%08X / 0x%08X\n",
				 rgi + 1, calc_rgd->rd_ri.ri_length, calc_rgd->rd_ri.ri_data1,
				 calc_rgd->rd_ri.ri_data, calc_rgd->rd_ri.ri_bitbytes);
		/*memset(rgindex_buf_ondisk, 0, sizeof(rgindex_buf_ondisk));*/
		/*gfs_rindex_out(&calc_rgd->rd_ri, rgindex_buf_ondisk);*/
		/* Note: rgindex_buf_ondisk is ONLY used for debug to see what
		   the entry would look like on disk. */
		/*hexdump(rgi*sizeof(struct gfs_rindex), rgindex_buf_ondisk,
		  sizeof(struct gfs_rindex));*/
	}
	*num_rgs = number_of_rgs;
	log_debug("Number of RGs = %d.\n", number_of_rgs);
	return 0;
}

/*
 * gfs_rgindex_calculate - calculate what the rgindex should look like
 *                         in a perfect world (trust_lvl == open_minded)
 *
 * Calculate what the rgindex should look like if no gfs_grow-like operations
 * were performed, so we can later check if all RG index entries are sane.
 *
 * This function goes in blind, assuming the entire rgindex is destroyed.
 * That way, we can rebuild it if really is trashed.
 *
 * However, this won't work if the filesystem has been extended or shrunk.
 * If the RGs aren't where we expect them to be, we have to take more drastic
 * measures to recover them.
 *
 * Assumes: journal index file is minimally sane.
 *
 * We need to check if the address and length values are okay.
 * First RG should start after the superblock at block #x11
 * Number of RGs=subdevice size / (2^rgsize/block size)
 *
 * Returns: 0 on success, -1 on failure
 * Sets:    ret_list to a linked list of fsck_rgrp structs representing
 *          what we think the rgindex should really look like.
 */
int gfs_rgindex_calculate(struct fsck_sb *sdp, osi_list_t *ret_list,
						  int *num_rgs)
{
	osi_buf_t *bh; /* buffer handle */
	uint64 subdevice_size, adjust_subdevice_size, fs_total_size;
	int number_of_rgs; /* min of 4 per segment * 2 segments = 8 */
	int rgnum_within_subdevice;
	int first_half;
	int error;
	int rgi, rgs_per_subd;
	uint64 subdevice_start;
	uint64 addr, prev_addr, length, prev_length;
	uint64 blocks;
	struct fsck_rgrp *calc_rgd;
	char rgindex_buf_ondisk[sizeof(struct gfs_rindex)];
	struct gfs_rindex buf, tmpndx;

	osi_list_init(ret_list);
	*num_rgs = 0;
	/* Get the total size of the device */
	error = ioctl(sdp->diskfd, BLKGETSIZE64,
				  &fs_total_size); /* Size in bytes */
	fs_total_size /= sdp->sb.sb_bsize;
	log_debug("fs_total_size = 0x%" PRIX64 " blocks.\n", fs_total_size);

	/* The end of the first subdevice is also where the first journal is.*/
	subdevice_size = sdp->jindex->ji_addr; /* addr of 1st journal (blks) */
	log_debug("subdevice_size = 0x%" PRIX64 ".\n", subdevice_size);

	/* ----------------------------------------------------------------- */
	/* Read the first block of the subdevice and make sure it's an RG.   */
	/* ----------------------------------------------------------------- */
	subdevice_start = fs_total_size - subdevice_size;
	error = get_and_read_buf(sdp, subdevice_start, &bh, 0);
	if (error){
		log_crit("Unable to read start of last subdevice.\n");
		return -1;
	}
	if(check_type(bh, GFS_METATYPE_RG)){
		log_warn("The middle RG is not on an even boundary (fs has grown?)\n");
		relse_buf(sdp, bh);
		return -1;
	}
	log_debug("First RG is okay.\n");
	/* --------------------------------------------------------------------- */
	/* Calculate how many RGs there are supposed to be based on the          */
	/* rgindex filesize.  Remember that our trust level is open-minded here. */
	/* If the filesize of the rgindex file is not a multiple of our rgindex  */
	/* structures, then something's wrong and we can't trust the index.      */
	/* --------------------------------------------------------------------- */
	number_of_rgs = sdp->riinode->i_di.di_size / sizeof(struct gfs_rindex);
	*num_rgs = number_of_rgs;
	log_warn("number_of_rgs = %d.\n", number_of_rgs);
	if (sdp->riinode->i_di.di_size % sizeof(struct gfs_rindex)) {
		log_warn("WARNING: rgindex file is corrupt.\n");
		return -1;
	}
	/* --------------------------------------------------------------------- */
	/* Check to see if the filesystem has been extended via gfs_grow.        */
	/* If so, our assumptions will be wrong and we can't continue.           */
	/* Instead, we need to progress to level 3 and dig deeper for the RGs.   */
	/* We'll know if the filesystem has been extended by whether or not the  */
	/* RG that's midway is on the wrong side of the journals.                */
	/* For example, if we have 50 RGs, we'd expect 25 to be on one side of   */
	/* the journals, and 25 to be on the other side.  If we find out that    */
	/* RG number 25 (index 1, or 24 index 0) is on the other side, we grew.  */
	/* --------------------------------------------------------------------- */
	rgi = (number_of_rgs / 2) - 1;
	error = readi(sdp->riinode,
				  (char *)&buf, rgi * sizeof(struct gfs_rindex),
				  sizeof(struct gfs_rindex));
	if (!error) { /* if end of file */
		log_warn("Error reading RG index.\n");
		return -1; /* stop looking */
	}
	gfs_rindex_in(&tmpndx, (char *)&buf); /* read in the index entry. */
	if (tmpndx.ri_addr >= sdp->jindex->ji_addr) { /* wrong side of journals */
		log_warn("This filesystem has probably been resized by gfs_grow.\n");
		return -1; /* stop looking */
	}
	/* --------------------------------------------------------------------- */
	/* Now that we know how many RGs there should be, we can calculate       */
	/* exactly where we think they should be and build our index with it.    */
	/* --------------------------------------------------------------------- */
	rgs_per_subd = (number_of_rgs / 2);
	for (rgi = 0; rgi < number_of_rgs; rgi++) {

		first_half = (rgi < rgs_per_subd ? 1 : 0);
		adjust_subdevice_size = subdevice_size;
		if (first_half) {
			adjust_subdevice_size -= ((GFS_SB_ADDR >> sdp->fsb2bb_shift) + 1);
			rgnum_within_subdevice = rgi;
		}
		else
			rgnum_within_subdevice = rgi - rgs_per_subd;
		prev_length = length;
		if (rgnum_within_subdevice)
			length = adjust_subdevice_size / rgs_per_subd;
		else
			length = adjust_subdevice_size - 
				(rgs_per_subd - 1) * (adjust_subdevice_size / rgs_per_subd);
		
		calc_rgd = (struct fsck_rgrp *)malloc(sizeof(struct fsck_rgrp));
		memset(calc_rgd, 0, sizeof(struct fsck_rgrp));
		calc_rgd->rd_sbd = sdp; /* hopefully this is not used */
		osi_list_add_prev(&calc_rgd->rd_list, ret_list);
		prev_addr = addr;
		if (!rgnum_within_subdevice) {
			if (!rgi) {
				/* The first RG immediately follows the superblock */
				addr = (GFS_SB_ADDR >> sdp->fsb2bb_shift) + 1;
			}
			else /* First RG on second subdevice is at the beginning of it */
				addr = subdevice_start;
		}
		else
			addr = prev_addr + prev_length;
		calc_rgd->rd_ri.ri_addr = addr;
		log_debug("ri_addr[%d] = 0x%"PRIX64 " / ", rgi, 
				  calc_rgd->rd_ri.ri_addr);
		blocks = length - rgrplength2bitblocks(sdp, length);
		blocks -= blocks % GFS_NBBY;
		calc_rgd->rd_ri.ri_length = rgrplength2bitblocks(sdp, length);
		calc_rgd->rd_ri.ri_data1 = calc_rgd->rd_ri.ri_addr +
			calc_rgd->rd_ri.ri_length;
		calc_rgd->rd_ri.ri_data = blocks;
		calc_rgd->rd_ri.ri_bitbytes = calc_rgd->rd_ri.ri_data / GFS_NBBY;
		log_info("%d / %08X / %08X / %08X\n", calc_rgd->rd_ri.ri_length,
			   calc_rgd->rd_ri.ri_data1, calc_rgd->rd_ri.ri_data,
			   calc_rgd->rd_ri.ri_bitbytes);
		memset(rgindex_buf_ondisk, 0, sizeof(rgindex_buf_ondisk));
		gfs_rindex_out(&calc_rgd->rd_ri, rgindex_buf_ondisk);
		/* Note: rgindex_buf_ondisk is ONLY used for debug to see what the
		   entry would look like on disk. */
		hexdump(rgi*sizeof(struct gfs_rindex), rgindex_buf_ondisk,
				sizeof(struct gfs_rindex));
	} /* for */
	relse_buf(sdp, bh); /* release the read buffer if we have one */
	return 0;
}

/*
 * ri_cleanup - free up the memory we previously allocated.
 */
void ri_cleanup(osi_list_t *rglist)
{
	struct fsck_rgrp *rgd;

	while(!osi_list_empty(rglist)){
		rgd = osi_list_entry(rglist->next, struct fsck_rgrp, rd_list);
		if(rgd->rd_bits)
			free(rgd->rd_bits);
		if(rgd->rd_bh)
			free(rgd->rd_bh);
		osi_list_del(&rgd->rd_list);
		free(rgd);
	}
}

/**
 * ri_update - attach rgrps to the super block
 * @sdp:
 *
 * Given the rgrp index inode, link in all rgrps into the super block
 * and be sure that they can be read.
 *
 * If we encounter problems with any RGs, it either means we have a corrupt
 * RG or a corrupt RG file entry (which is less likely).  We make up to three
 * attempts to do this.  First, we trust that the RG index is correct and
 * read the RGs.  If that fails, we become a little less trusting and
 * try to calculate what the RG index should look like in a perfect world.
 * If that doesn't work, we become even less trusting and go to great lengths
 * to figure out exactly where those RGs should be and what the index should
 * look like.
 *
 * Returns: 0 on success, -1 on failure.
 */
int ri_update(struct fsck_sb *sdp)
{
	struct fsck_rgrp *rgd, *expected_rgd;
	osi_list_t expected_rglist; /* List of expected resource groups */
	osi_list_t *tmp;
	struct gfs_rindex buf;
	unsigned int rg, calc_rg_count;
	int error, count1 = 0, count2 = 0;
	int fix_grow_problems = 0, grow_problems = 0;
	enum rgindex_trust_level { /* how far can we trust our RG index? */
		blind_faith = 0, /* We'd like to trust the rgindex. We always used to
							before bz 179069. This should cover most cases. */
		open_minded = 1, /* At least 1 RG is corrupt. Try to calculate what it
							should be, in a perfect world where our RGs are all
							on even boundaries. Blue sky. Chirping birds. */
		distrust = 2   /* The world isn't perfect, our RGs are not on nice neat
						  boundaries.  The fs must have been messed with by
						  gfs_grow or something.  Count the RGs by hand. */
	} trust_lvl;

	log_info("Validating Resource Group index.\n");
	for (trust_lvl = blind_faith; trust_lvl <= distrust; trust_lvl++) {
		log_info("Level %d check.\n", trust_lvl + 1);
		count1 = count2 = 0;
		/* ---------------------------------------------------------------- */
		/* Step 1 - Calculate or figure out our own RG index                */
		/* ---------------------------------------------------------------- */
		if (trust_lvl == blind_faith) { /* For now, assume rgindex is gospel */
			osi_list_init(&expected_rglist);
			error = FALSE;
		}
		else if (trust_lvl == open_minded) { /* If we can't trust RG index */
			/* Calculate our own RG index for comparison */
			error = gfs_rgindex_calculate(sdp, &expected_rglist,
										  &calc_rg_count);
			if (error) { /* If calculated RGs don't reasonably match the fs */
				log_info("(failed--trying again at level 3)\n");
				ri_cleanup(&sdp->rglist);
				continue; /* Try again, this time counting them manually */
			}
		}
		else if (trust_lvl == distrust) { /* If we can't trust RG index */
			error = gfs_rgindex_rebuild(sdp, &expected_rglist,
										&calc_rg_count); /* count the RGs. */
			if (error) { /* If calculated RGs don't reasonably match the fs */
				log_info("(failed--giving up)\n");
				goto fail; /* try again, this time counting them manually */
			}
		}
		/* ---------------------------------------------------------------- */
		/* Step 2 - Read the real RG index and check its integrity          */
		/* ---------------------------------------------------------------- */
		for (rg = 0; ; rg++) {
			int rgindex_modified;

			rgindex_modified = FALSE;
			error = readi(sdp->riinode, (char *)&buf,
						  rg * sizeof(struct gfs_rindex),
						  sizeof(struct gfs_rindex));
			if (!error) /* if no data was read */
				break;  /* we found the end of the rg index file */
			if (error != sizeof(struct gfs_rindex)) {
				log_err("Unable to read resource group index #%u.\n", rg);
				goto fail;
			}
			
			rgd = (struct fsck_rgrp *)malloc(sizeof(struct fsck_rgrp));
			memset(rgd, 0, sizeof(struct fsck_rgrp));
			rgd->rd_sbd = sdp;
			osi_list_add_prev(&rgd->rd_list, &sdp->rglist);
			gfs_rindex_in(&rgd->rd_ri, (char *)&buf);
			if (trust_lvl != blind_faith) {
				expected_rgd = osi_list_entry(expected_rglist.next,
											  struct fsck_rgrp, rd_list);
				/* --------------------------------------------------------- */
				/* Now compare the index to the one we calculated / rebuilt  */
				/* Since this is fsck and fsck's job is to fix filesystem    */
				/* corruption, it's probably better to trust the calculated  */
				/* value and discard what's reported on disk.                */
				/* --------------------------------------------------------- */
				ri_compare(rg, rgd->rd_ri, expected_rgd->rd_ri,
						   ri_addr, PRIx64);
				ri_compare(rg, rgd->rd_ri, expected_rgd->rd_ri,
						   ri_length, PRIx32);
				ri_compare(rg, rgd->rd_ri, expected_rgd->rd_ri,
						   ri_data1, PRIx64);
				ri_compare(rg, rgd->rd_ri, expected_rgd->rd_ri,
						   ri_data, PRIx32);
				ri_compare(rg, rgd->rd_ri, expected_rgd->rd_ri,
						   ri_bitbytes, PRIx32);
				/* If we modified the index, write it back to disk. */
				if (rgindex_modified) {
					if(query(sdp, "Fix the index? (y/n)")) {
						gfs_rindex_out(&rgd->rd_ri, (char *)&buf);
						error = writei(sdp->riinode, (char *)&buf,
									   rg * sizeof(struct gfs_rindex),
									   sizeof(struct gfs_rindex));
						if (error != sizeof(struct gfs_rindex)) {
							log_err("Unable to fix resource group index %u.\n",
									rg + 1);
							goto fail;
						}
					}
					else
						log_err("RG index not fixed.\n");
				}
				osi_list_del(&expected_rgd->rd_list);
				free(expected_rgd);
			} /* if we can't trust the rg index */
			else { /* blind faith -- just check for the gfs_grow problem */
				if (rgd->rd_ri.ri_data == 4294967292) {
					if (!fix_grow_problems) {
						log_err("A problem with the rindex file caused by gfs_grow was detected.\n");
						if(query(sdp, "Fix the rindex problem? (y/n)"))
							fix_grow_problems = 1;
					}
					/* Keep a counter in case we hit it more than once. */
					grow_problems++;
					osi_list_del(&rgd->rd_list); /* take it out of the equation */
					free(rgd);
					continue;
				} else if (fix_grow_problems) {
					/* Once we detect the gfs_grow rindex problem, we have to */
					/* rewrite the entire rest of the rindex file, starting   */
					/* with the entry AFTER the one that has the problem.     */
					gfs_rindex_out(&rgd->rd_ri, (char *)&buf);
					error = writei(sdp->riinode, (char *)&buf,
						       (rg - grow_problems) *
						       sizeof(struct gfs_rindex),
						       sizeof(struct gfs_rindex));
					if (error != sizeof(struct gfs_rindex)) {
						log_err("Unable to fix rindex entry %u.\n",
							rg + 1);
						goto fail;
					}
				}
			}
			error = fs_compute_bitstructs(rgd);
			if (error)
				break;
			rgd->rd_open_count = 0;
			count1++;
		} /* for all RGs in the index */
		rg -= grow_problems;
		if (!error) {
			log_info("%u resource groups found.\n", rg);
			if (trust_lvl != blind_faith && rg != calc_rg_count)
				log_warn("Resource group count discrepancy. Index says %d. " \
						 "Should be %d.\n", rg, calc_rg_count);
			/* ------------------------------------------------------------- */
			/* Step 3 - Read the real RGs and check their integrity.         */
			/* Now we can somewhat trust the rgindex and the RG addresses,   */
			/* so let's read them in, check them and optionally fix them.    */
			/* ------------------------------------------------------------- */
			error = FALSE;
			for (tmp = sdp->rglist.next; !error && tmp != &sdp->rglist;
				 tmp = tmp->next) {
				rgd = osi_list_entry(tmp, struct fsck_rgrp, rd_list);
				error = fs_rgrp_read(rgd, trust_lvl);
				if (error)
					log_err("Unable to read in rgrp descriptor.\n");
				else
					fs_rgrp_relse(rgd);
				count2++;
			}
			if (!error && count1 != count2){
				log_err("Rgrps allocated (%d) does not equal"
						" rgrps read (%d).\n", count1, count2);
				error = -1;
			}
			sdp->rgcount = count1;
		}
		if (fix_grow_problems) {
			osi_buf_t *dibh;

			get_and_read_buf(sdp, sdp->sb.sb_rindex_di.no_addr, &dibh, 0);
			sdp->riinode->i_di.di_size = rg * sizeof(struct gfs_rindex);
			gfs_dinode_out(&sdp->riinode->i_di, BH_DATA(dibh));
			write_buf(sdp, dibh, 0);
			grow_problems = fix_grow_problems = 0;
			relse_buf(sdp, dibh);
		}
		if (!error) { /* if no problems encountered with the rgs */
			log_info("(passed)\n");
			break;  /* no reason to distrust what we saw. Otherwise, we
					   reiterate and become a little less trusting. */
		}
		else {
			if (trust_lvl < distrust)
				log_info("(failed--trying again at level 2)\n");
			else
				log_info("(failed--recovery impossible)\n");
		}
		ri_cleanup(&sdp->rglist);
	} /* for trust_lvl */
	return 0;

 fail:
	ri_cleanup(&sdp->rglist);
	return -1;
}

int write_sb(struct fsck_sb *sbp)
{
	int error = 0;
	osi_buf_t *bh;

	error = get_and_read_buf(sbp, GFS_SB_ADDR >> sbp->fsb2bb_shift, &bh, 0);
	if (error){
		log_crit("Unable to read superblock\n");
		goto out;
	}

	gfs_sb_out(&sbp->sb, BH_DATA(bh));

	if((error = write_buf(sbp, bh, BW_WAIT))) {
		stack;
		goto out;
	}

	relse_buf(sbp, bh);
out:
	return error;

}

