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
	error = get_and_read_buf(sdp, GFS_SB_ADDR >> sdp->fsb2bb_shift,
				    &bh, 0);
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
		(sdp->sb.sb_bsize-sizeof(struct gfs_dinode)) /
		sizeof(uint64);
	sdp->inptrs =
		(sdp->sb.sb_bsize-sizeof(struct gfs_indirect)) /
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

	for (j = 0; ; j++){
		error = readi(ip, buf, j * sizeof(struct gfs_jindex),
				 sizeof(struct gfs_jindex));
		if(!error)
			break;
		if (error != sizeof(struct gfs_jindex)){
			log_err("An error occurred while reading the"
				" journal index file.\n");
			goto fail;
		}

		gfs_jindex_in(sdp->jindex + j, buf);
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
	struct gfs_rindex buf;
	unsigned int rg;
	int error, count1 = 0, count2 = 0;

	for (rg = 0; ; rg++)
	{
		error = readi(sdp->riinode, (char *)&buf,
				 rg * sizeof(struct gfs_rindex),
				 sizeof(struct gfs_rindex));
		if (!error)
			break;
		if (error != sizeof(struct gfs_rindex)){
			log_err("Unable to read resource group index #%u.\n",
				rg);
			goto fail;
		}

		rgd = (struct fsck_rgrp *)malloc(sizeof(struct fsck_rgrp));

		rgd->rd_sbd = sdp;

		osi_list_add_prev(&rgd->rd_list, &sdp->rglist);

		gfs_rindex_in(&rgd->rd_ri, (char *)&buf);

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
