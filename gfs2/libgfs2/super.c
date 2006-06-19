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

#include "libgfs2.h"
#include "osi_list.h"

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
static int check_sb(struct gfs2_sbd *sdp, struct gfs2_sb *sb)
{
	if (sb->sb_header.mh_magic != GFS2_MAGIC ||
	    sb->sb_header.mh_type != GFS2_METATYPE_SB) {
		log_crit("Either the super block is corrupted, or this "
				 "is not a GFS2 filesystem\n");
		log_debug("Header magic: %X Header Type: %X\n",
				  sb->sb_header.mh_magic,
				  sb->sb_header.mh_type);
		return -EINVAL;
	}
	/*  If format numbers match exactly, we're done.  */
	if (sb->sb_fs_format != GFS2_FORMAT_FS ||
	    sb->sb_multihost_format != GFS2_FORMAT_MULTI) {
		log_crit("Old gfs1 file system detected.\n");
		return -EINVAL;
	}
	return 0;
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
int read_sb(struct gfs2_sbd *sdp)
{
	struct gfs2_buffer_head *bh;
	uint64_t space = 0;
	unsigned int x;
	int error;

	bh = bread(sdp, GFS2_SB_ADDR >> sdp->sd_fsb2bb_shift);
	gfs2_sb_in(&sdp->sd_sb, bh->b_data);
	brelse(bh, not_updated);

	error = check_sb(sdp, &sdp->sd_sb);
	if (error)
		goto out;

	sdp->sd_fsb2bb_shift = sdp->sd_sb.sb_bsize_shift - GFS2_BASIC_BLOCK_SHIFT;
	sdp->sd_diptrs =
		(sdp->sd_sb.sb_bsize-sizeof(struct gfs2_dinode)) /
		sizeof(uint64_t);
	sdp->sd_inptrs =
		(sdp->sd_sb.sb_bsize-sizeof(struct gfs2_meta_header)) /
		sizeof(uint64_t);
	sdp->sd_jbsize = sdp->sd_sb.sb_bsize - sizeof(struct gfs2_meta_header);
	sdp->sd_heightsize[0] = sdp->sd_sb.sb_bsize - sizeof(struct gfs2_dinode);
	sdp->sd_heightsize[1] = sdp->sd_sb.sb_bsize * sdp->sd_diptrs;
	for (x = 2; ; x++){
		space = sdp->sd_heightsize[x - 1] * sdp->sd_inptrs;
		/* FIXME: Do we really need this first check?? */
		if (space / sdp->sd_inptrs != sdp->sd_heightsize[x - 1] ||
		    space % sdp->sd_inptrs != 0)
			break;
		sdp->sd_heightsize[x] = space;
	}
	if (x > GFS2_MAX_META_HEIGHT){
		log_err("Bad max metadata height.\n");
		error = -1;
		goto out;
	}

	sdp->sd_jheightsize[0] = sdp->sd_sb.sb_bsize - sizeof(struct gfs2_dinode);
	sdp->sd_jheightsize[1] = sdp->sd_jbsize * sdp->sd_diptrs;
	for (x = 2; ; x++){
		space = sdp->sd_jheightsize[x - 1] * sdp->sd_inptrs;
		if (space / sdp->sd_inptrs != sdp->sd_jheightsize[x - 1] ||
			space % sdp->sd_inptrs != 0)
			break;
		sdp->sd_jheightsize[x] = space;
	}
	sdp->sd_max_jheight = x;
	if(sdp->sd_max_jheight > GFS2_MAX_META_HEIGHT) {
		log_err("Bad max jheight.\n");
		error = -1;
	}
	sdp->fssize = lseek(sdp->device_fd, 0, SEEK_END) / sdp->sd_sb.sb_bsize;

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
int ji_update(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *jip, *ip = sdp->md.jiinode;
	char journal_name[JOURNAL_NAME_SIZE];
	int i;

	if(!ip) {
		log_crit("Journal inode not found.\n");
		return -1;
	}

	if(!(sdp->md.journal = calloc(ip->i_di.di_entries - 2, sizeof(struct gfs2_inode *)))) {
		log_err("Unable to allocate journal index\n");
		return -1;
	}
	sdp->md.journals = 0;
	memset(journal_name, 0, sizeof(*journal_name));
	for(i = 0; i < ip->i_di.di_entries - 2; i++) {
		/* FIXME check snprintf return code */
		snprintf(journal_name, JOURNAL_NAME_SIZE, "journal%u", i);
		gfs2_lookupi(sdp->md.jiinode, journal_name, strlen(journal_name), 
					 &jip);
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
int ri_update(struct gfs2_sbd *sdp, int *rgcount)
{
	struct rgrp_list *rgd;
	osi_list_t *tmp;
	struct gfs2_rindex buf;
	unsigned int rg;
	int error, count1 = 0, count2 = 0;
	uint64_t errblock = 0;

	for (rg = 0; ; rg++) {
		error = gfs2_readi(sdp->md.riinode, (char *)&buf,
						   rg * sizeof(struct gfs2_rindex),
						   sizeof(struct gfs2_rindex));
		if (!error)
			break;
		if (error != sizeof(struct gfs2_rindex))
			goto fail;

		rgd = (struct rgrp_list *)malloc(sizeof(struct rgrp_list));
		memset(rgd, 0, sizeof(struct rgrp_list));
		osi_list_add_prev(&rgd->list, &sdp->rglist);

		gfs2_rindex_in(&rgd->ri, (char *)&buf);

		if(gfs2_compute_bitstructs(sdp, rgd))
			goto fail;

		count1++;
	}

	for (tmp = sdp->rglist.next; tmp != &sdp->rglist; tmp = tmp->next) {
		int i;
		uint64_t prev_err = 0;
		enum update_flags f;

		f = not_updated;
		rgd = osi_list_entry(tmp, struct rgrp_list, list);
		/* If we have errors, we may need to repair and continue.           */
		/* We have multiple bitmaps, and all of them might potentially need */
		/* repair.  So we have to try to read and repair as many times as   */
		/* there are bitmaps.                                               */
		for (i = 0; i < rgd->ri.ri_length; i++) {
			errblock = gfs2_rgrp_read(sdp, rgd);
			if (errblock) {
				if (errblock == prev_err) /* if same block is still bad */
					goto fail;
				prev_err = errblock;
			}
			else
				break;
		} /* for all bitmap structures */
		gfs2_rgrp_relse(rgd, f);
		count2++;
	}

	*rgcount = count1;
	if (count1 != count2)
		goto fail;

	return 0;

 fail:
	gfs2_rgrp_free(sdp, not_updated);
	return -1;
}

int write_sb(struct gfs2_sbd *sbp)
{
	struct gfs2_buffer_head *bh;

	bh = bread(sbp, GFS2_SB_ADDR >> sbp->sd_fsb2bb_shift);
	gfs2_sb_out(&sbp->sd_sb, bh->b_data);
	brelse(bh, updated);
	bcommit(sbp); /* make sure the change gets to disk ASAP */
	return 0;
}

