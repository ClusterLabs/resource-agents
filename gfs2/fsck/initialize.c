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

#include <linux_endian.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "libgfs2.h"
#include "fsck.h"
#include "util.h"
#include "fs_recovery.h"
#include "linux_endian.h"

#define CLEAR_POINTER(x) \
	if(x) { \
		free(x); \
		x = NULL; \
	}

/**
 * init_journals
 *
 * Go through journals and replay them - then clear them
 */
int init_journals(struct gfs2_sbd *sbp)
{
	if(!opts.no) {
		/* ATTENTION -- Journal replay is not supported */
		if(reconstruct_journals(sbp))
			return 1;
	}
	return 0;
}

/**
 * block_mounters
 *
 * Change the lock protocol so nobody can mount the fs
 *
 */
int block_mounters(struct gfs2_sbd *sbp, int block_em)
{
	if(block_em) {
		/* verify it starts with lock_ */
		if(!strncmp(sbp->sd_sb.sb_lockproto, "lock_", 5)) {
			/* Change lock_ to fsck_ */
			memcpy(sbp->sd_sb.sb_lockproto, "fsck_", 5);
		}
		/* FIXME: Need to do other verification in the else
		 * case */
	} else {
		/* verify it starts with fsck_ */
		/* verify it starts with lock_ */
		if(!strncmp(sbp->sd_sb.sb_lockproto, "fsck_", 5)) {
			/* Change fsck_ to lock_ */
			memcpy(sbp->sd_sb.sb_lockproto, "lock_", 5);
		}
	}

	if(write_sb(sbp)) {
		stack;
		return -1;
	}
	return 0;
}


/*
 * empty_super_block - free all structures in the super block
 * sdp: the in-core super block
 *
 * This function frees all allocated structures within the
 * super block.  It does not free the super block itself.
 *
 * Returns: Nothing
 */
static void empty_super_block(struct gfs2_sbd *sdp)
{
	uint32_t i;

	while(!osi_list_empty(&sdp->rglist)){
		struct rgrp_list *rgd;

		rgd = osi_list_entry(sdp->rglist.next, struct rgrp_list, list);
		osi_list_del(&rgd->list);
		if(rgd->bits)
			free(rgd->bits);
		free(rgd);
	}

	for(i = 0; i < FSCK_HASH_SIZE; i++) {
		while(!osi_list_empty(&inode_hash[i])) {
			struct inode_info *ii;
			ii = osi_list_entry(inode_hash[i].next, struct inode_info, list);
			osi_list_del(&ii->list);
			free(ii);
		}
		while(!osi_list_empty(&dir_hash[i])) {
			struct dir_info *di;
			di = osi_list_entry(dir_hash[i].next, struct dir_info, list);
			osi_list_del(&di->list);
			free(di);
		}
	}

	gfs2_block_list_destroy(bl);
}


/**
 * set_block_ranges
 * @sdp: superblock
 *
 * Uses info in rgrps and jindex to determine boundaries of the
 * file system.
 *
 * Returns: 0 on success, -1 on failure
 */
static int set_block_ranges(struct gfs2_sbd *sdp)
{

	struct rgrp_list *rgd;
	struct gfs2_rindex *ri;
	osi_list_t *tmp;
	char buf[sdp->sd_sb.sb_bsize];
	uint64_t rmax = 0;
	uint64_t rmin = 0;
	int error;

	log_info("Setting block ranges...\n");

	for (tmp = sdp->rglist.next; tmp != &sdp->rglist; tmp = tmp->next)
	{
		rgd = osi_list_entry(tmp, struct rgrp_list, list);
		ri = &rgd->ri;
		if (ri->ri_data0 + ri->ri_data - 1 > rmax)
			rmax = ri->ri_data0 + ri->ri_data - 1;
		if (!rmin || ri->ri_data0 < rmin)
			rmin = ri->ri_data0;
	}

	last_fs_block = rmax;

	last_data_block = rmax;
	first_data_block = rmin;

	if(do_lseek(sdp->device_fd, (last_fs_block * sdp->sd_sb.sb_bsize))){
		log_crit("Can't seek to last block in file system: %"
				 PRIu64" (0x%" PRIx64 ")\n", last_fs_block, last_fs_block);
		goto fail;
	}

	memset(buf, 0, sdp->sd_sb.sb_bsize);
	error = read(sdp->device_fd, buf, sdp->sd_sb.sb_bsize);
	if (error != sdp->sd_sb.sb_bsize){
		log_crit("Can't read last block in file system (error %u), "
				 "last_fs_block: %"PRIu64" (0x%" PRIx64 ")\n", error,
				 last_fs_block, last_fs_block);
		goto fail;
	}

	return 0;

 fail:
	return -1;
}


/**
 * fill_super_block
 * @sdp:
 *
 * Returns: 0 on success, -1 on failure
 */
static int fill_super_block(struct gfs2_sbd *sdp)
{
	uint32_t i;
	char *buf;
	uint64_t inumbuf;
	struct gfs2_statfs_change sc;
	int rgcount;

	sync();

	/********************************************************************
	 ***************** First, initialize all lists **********************
	 ********************************************************************/
	log_info("Initializing lists...\n");
	osi_list_init(&sdp->rglist);
	osi_list_init(&sdp->buf_list);
	for(i = 0; i < FSCK_HASH_SIZE; i++) {
		osi_list_init(&dir_hash[i]);
		osi_list_init(&inode_hash[i]);
		osi_list_init(&sdp->buf_hash[i]);
	}

	/********************************************************************
	 ************  next, read in on-disk SB and set constants  **********
	 ********************************************************************/
	sdp->sd_sb.sb_bsize = GFS2_DEFAULT_BSIZE;
	sdp->bsize = sdp->sd_sb.sb_bsize;

	if(sizeof(struct gfs2_sb) > sdp->sd_sb.sb_bsize){
		log_crit("GFS superblock is larger than the blocksize!\n");
		log_debug("sizeof(struct gfs2_sb) > sdp->sd_sb.sb_bsize\n");
		return -1;
	}

	compute_constants(sdp);
	if(read_sb(sdp) < 0){
		return -1;
	}

	/*******************************************************************
	 ******************  Initialize important inodes  ******************
	 *******************************************************************/

	log_info("Initializing special inodes...\n");

	/* Get master dinode */
	sdp->master_dir = gfs2_load_inode(sdp, sdp->sd_sb.sb_master_dir.no_addr);
	/* Get root dinode */
	sdp->md.rooti = gfs2_load_inode(sdp, sdp->sd_sb.sb_root_dir.no_addr);

	/* Look for "inum" entry in master dinode */
	gfs2_lookupi(sdp->master_dir, "inum", 4, &sdp->md.inum);
	/* Read inum entry into buffer */
	gfs2_readi(sdp->md.inum, &inumbuf, 0, sdp->md.inum->i_di.di_size);
	/* call gfs2_inum_range_in() to retrieve range */
	sdp->md.next_inum = be64_to_cpu(inumbuf);

	gfs2_lookupi(sdp->master_dir, "statfs", 6, &sdp->md.statfs);
	/* Read inum entry into buffer */
	/* FIXME finish this */
	buf = malloc(sdp->md.statfs->i_di.di_size);
	gfs2_readi(sdp->md.statfs, buf, 0, sdp->md.statfs->i_di.di_size);
	/* call gfs2_inum_range_in() to retrieve range */
	gfs2_statfs_change_in(&sc, buf);
	free(buf);


	gfs2_lookupi(sdp->master_dir, "jindex", 6, &sdp->md.jiinode);

	gfs2_lookupi(sdp->master_dir, "rindex", 6, &sdp->md.riinode);

	gfs2_lookupi(sdp->master_dir, "quota", 5, &sdp->md.qinode);

	gfs2_lookupi(sdp->master_dir, "per_node", 8, &sdp->md.pinode);

	/* FIXME fill in per_node structure */

	/*******************************************************************
	 *******  Fill in rgrp and journal indexes and related fields  *****
	 *******************************************************************/

	/* read in the ji data */
	if (ji_update(sdp)){
		log_err("Unable to read in ji inode.\n");
		return -1;
	}

	if(ri_update(sdp, &rgcount)){
		log_err("Unable to fill in resource group information.\n");
		goto fail;
	}

	/*******************************************************************
	 *******  Now, set boundary fields in the super block  *************
	 *******************************************************************/
	if(set_block_ranges(sdp)){
		log_err("Unable to determine the boundaries of the"
			" file system.\n");
		goto fail;
	}

	bl = gfs2_block_list_create(last_fs_block+1);

	return 0;

 fail:
	empty_super_block(sdp);

	return -1;
}

/**
 * init_sbp - initialize superblock pointer
 *
 */
static int init_sbp(struct gfs2_sbd *sbp)
{
	if(opts.no) {
		if ((sbp->device_fd = open(opts.device, O_RDONLY)) < 0) {
			log_crit("Unable to open device: %s\n", opts.device);
			return -1;
		}
	} else {
		/* read in sb from disk */
		if ((sbp->device_fd = open(opts.device, O_RDWR)) < 0){
			log_crit("Unable to open device: %s\n", opts.device);
			return -1;
		}
	}
	if (fill_super_block(sbp)) {
		stack;
		return -1;
	}

	/* Change lock protocol to be fsck_* instead of lock_* */
	if(!opts.no) {
		if(block_mounters(sbp, 1)) {
			log_err("Unable to block other mounters\n");
			return -1;
		}
	}

	/* verify various things */

	if(init_journals(sbp)) {
		stack;
		return -1;
	}

	return 0;
}

static void destroy_sbp(struct gfs2_sbd *sbp)
{
	if(!opts.no) {
		if(block_mounters(sbp, 0)) {
			log_warn("Unable to unblock other mounters - manual intevention required\n");
			log_warn("Use 'gfs_tool sb <device> proto' to fix\n");
		}
		log_info("Syncing the device.\n");
		fsync(sbp->device_fd);
	}
	empty_super_block(sbp);
	close(sbp->device_fd);
}

int initialize(struct gfs2_sbd *sbp)
{

	return init_sbp(sbp);

}

void destroy(struct gfs2_sbd *sbp)
{
	destroy_sbp(sbp);
}
