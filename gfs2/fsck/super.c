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

#include <unistd.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "osi_list.h"
#include "bio.h"
#include "util.h"
#include "file.h"
#include "rgrp.h"
#include "fsck.h"
#include "fs_inode.h"
#include "ondisk.h"
#include "super.h"
#include "fsck_incore.h"


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
static int check_sb(struct fsck_sb *sdp, struct gfs2_sb *sb)
{
	int error = 0;
	if (sb->sb_header.mh_magic != GFS2_MAGIC ||
	    sb->sb_header.mh_type != GFS2_METATYPE_SB){
		log_crit("Either the super block is corrupted, or this "
			 "is not a GFS2 filesystem\n");
		log_debug("Header magic: %X Header Type: %X\n",
			  sb->sb_header.mh_magic,
			  sb->sb_header.mh_type);
		error = -EINVAL;
		goto out;
	}

	/*  If format numbers match exactly, we're done.  */
	if (sb->sb_fs_format != GFS2_FORMAT_FS ||
	    sb->sb_multihost_format != GFS2_FORMAT_MULTI){
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
	struct buffer_head *bh;
	uint64_t space = 0;
	unsigned int x;
	int error;
	error = get_and_read_buf(sdp, GFS2_SB_ADDR >> sdp->fsb2bb_shift,
				    &bh, 0);
	if (error){
		log_crit("Unable to read superblock\n");
		goto out;
	}

	gfs2_sb_in(&sdp->sb, BH_DATA(bh));

	relse_buf(sdp, bh);

	error = check_sb(sdp, &sdp->sb);
	if (error)
		goto out;

/* FIXME: Need to verify all this */
	/* FIXME: What's this 9? */
	sdp->fsb2bb_shift = sdp->sb.sb_bsize_shift - 9;
	sdp->diptrs =
		(sdp->sb.sb_bsize-sizeof(struct gfs2_dinode)) /
		sizeof(uint64_t);
	sdp->inptrs =
		(sdp->sb.sb_bsize-sizeof(struct gfs2_meta_header)) /
		sizeof(uint64_t);
	sdp->jbsize = sdp->sb.sb_bsize - sizeof(struct gfs2_meta_header);
	sdp->bsize = sdp->sb.sb_bsize;
	/* FIXME: Why is this /2 */
	sdp->hash_bsize = sdp->sb.sb_bsize / 2;
	sdp->hash_ptrs = sdp->hash_bsize / sizeof(uint64_t);
	sdp->heightsize[0] = sdp->sb.sb_bsize -
		sizeof(struct gfs2_dinode);
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
	if(sdp->max_height > GFS2_MAX_META_HEIGHT){
		log_err("Bad max metadata height.\n");
		error = -1;
		goto out;
	}

	sdp->jheightsize[0] = sdp->sb.sb_bsize -
		sizeof(struct gfs2_dinode);
	sdp->jheightsize[1] = sdp->jbsize * sdp->diptrs;
	for (x = 2; ; x++){
		space = sdp->jheightsize[x - 1] * sdp->inptrs;
		if (space / sdp->inptrs != sdp->jheightsize[x - 1] ||
		    space % sdp->inptrs != 0)
			break;
		sdp->jheightsize[x] = space;
	}
	sdp->max_jheight = x;
	if(sdp->max_jheight > GFS2_MAX_META_HEIGHT){
		log_err("Bad max jheight.\n");
		error = -1;
	}

 out:

	return error;
}

#define JOURNAL_NAME_SIZE 16

/*
 * ji_update - fill in journal info
 * sdp: the incore superblock pointer
 *
 * Given the inode for the journal index, read in all
 * the journal inodes.
 *
 * Returns: 0 on success, -1 on failure
 */
int ji_update(struct fsck_sb *sdp)
{
	struct fsck_inode *jip, *ip = sdp->md.jiinode;
	char journal_name[JOURNAL_NAME_SIZE];
	int i;

	if(!ip) {
		log_err("Journal index inode not filled in\n");
		return -1;
	}

	if(!(sdp->md.journal = calloc(ip->i_di.di_entries - 2, sizeof(struct fsck_inode *)))) {
		log_err("Unable to allocate journal inodes\n");
		return -1;
	}
	sdp->md.journals = 0;
	memset(journal_name, 0, sizeof(*journal_name));
	for(i = 0; i < ip->i_di.di_entries - 2; i++) {
		/* FIXME check snprintf return code */
		snprintf(journal_name, JOURNAL_NAME_SIZE, "journal%u", i);
		fs_lookupi(sdp->md.jiinode,
			   &(osi_filename_t){journal_name,
					   strlen(journal_name)}, &jip);
		sdp->md.journal[i] = jip;
	}
	sdp->md.journals = ip->i_di.di_entries - 2;
	return 0;

}


/**
 * ri_update - attach rgrps to the super block
 * @sdp:
 *
 * Given the rgrp index inode, link in all rgrps into the super block
 * and be sure that they can be read.
 *
 * Returns: 0 on success, -1 on failure.
 */
int ri_update(struct fsck_sb *sdp)
{
	struct fsck_rgrp *rgd;
	osi_list_t *tmp;
	struct gfs2_rindex buf;
	unsigned int rg;
	int error, count1 = 0, count2 = 0;

	for (rg = 0; ; rg++)
	{
		error = readi(sdp->md.riinode, (char *)&buf,
				 rg * sizeof(struct gfs2_rindex),
				 sizeof(struct gfs2_rindex));
		if (!error)
			break;
		if (error != sizeof(struct gfs2_rindex)){
			log_err("Unable to read resource group index #%u.\n",
				rg);
			goto fail;
		}

		rgd = (struct fsck_rgrp *)malloc(sizeof(struct fsck_rgrp));

		rgd->rd_sbd = sdp;

		osi_list_add_prev(&rgd->rd_list, &sdp->rglist);

		gfs2_rindex_in(&rgd->rd_ri, (char *)&buf);

		if(fs_compute_bitstructs(rgd)){
			goto fail;
		}

		rgd->rd_open_count = 0;

		count1++;
	}

	log_debug("%u resource groups found.\n", rg);

	for (tmp = sdp->rglist.next; tmp != &sdp->rglist; tmp = tmp->next)
	{
		rgd = osi_list_entry(tmp, struct fsck_rgrp, rd_list);

		error = fs_rgrp_read(rgd);
		if (error){
			log_err("Unable to read in rgrp descriptor.\n");
			goto fail;
		}

		fs_rgrp_relse(rgd);
		count2++;
	}

	if (count1 != count2){
		log_err("Rgrps allocated (%d) does not equal"
			" rgrps read (%d).\n", count1, count2);
		goto fail;
	}

	sdp->rgcount = count1;
	return 0;

 fail:
	while(!osi_list_empty(&sdp->rglist)){
		rgd = osi_list_entry(sdp->rglist.next, struct fsck_rgrp, rd_list);
		if(rgd->rd_bits)
			free(rgd->rd_bits);
		if(rgd->rd_bh)
			free(rgd->rd_bh);
		osi_list_del(&rgd->rd_list);
		free(rgd);
	}

	return -1;
}

int write_sb(struct fsck_sb *sbp)
{
	int error = 0;
	struct buffer_head *bh;

	error = get_and_read_buf(sbp, GFS2_SB_ADDR >> sbp->fsb2bb_shift,
				 &bh, 0);
	if (error){
		log_crit("Unable to read superblock\n");
		goto out;
	}

	gfs2_sb_out(&sbp->sb, BH_DATA(bh));

	/* FIXME: Should this set the BW_WAIT flag? */
	if((error = write_buf(sbp, bh, 0))) {
		stack;
		goto out;
	}

	relse_buf(sbp, bh);
out:
	return error;

}

