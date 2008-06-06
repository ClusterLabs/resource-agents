#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "fsck_incore.h"
#include "fsck.h"
#include "util.h"
#include "super.h"
#include "fs_inode.h"
#include "fs_recovery.h"
#include "inode.h"
#include "bio.h"

/**
 * init_journals
 *
 * Go through journals and replay them - then clear them
 */
int init_journals(struct fsck_sb *sbp)
{

	if(!sbp->opts->no) {
		/* Next, Replay the journals */
		if(sbp->flags & SBF_RECONSTRUCT_JOURNALS){
			if(reconstruct_journals(sbp)){
				stack;
				return 1;
			}
		} else {
			/* ATTENTION -- Journal replay is not supported */
			if(reconstruct_journals(sbp)){
				stack;
				return 1;
			}
		}
	}
	return 0;
}

/**
 * block_mounters
 *
 * Change the lock protocol so nobody can mount the fs
 *
 */
int block_mounters(struct fsck_sb *sbp, int block_em)
{
	if(block_em) {
		/* verify it starts with lock_ */
		if(!strncmp(sbp->sb.sb_lockproto, "lock_", 5)) {
			/* Change lock_ to fsck_ */
			memcpy(sbp->sb.sb_lockproto, "fsck_", 5);
		}
		/* FIXME: Need to do other verification in the else
		 * case */
	} else {
		/* verify it starts with fsck_ */
		/* verify it starts with lock_ */
		if(!strncmp(sbp->sb.sb_lockproto, "fsck_", 5)) {
			/* Change fsck_ to lock_ */
			memcpy(sbp->sb.sb_lockproto, "lock_", 5);
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
static void empty_super_block(struct fsck_sb *sdp)
{
	uint32_t i;

	log_info("Freeing buffers.\n");
	if(sdp->riinode){
		free(sdp->riinode);
		sdp->riinode = NULL;
	}
	if(sdp->jiinode){
		free(sdp->jiinode);
		sdp->jiinode = NULL;
	}
	if(sdp->rooti){
		free(sdp->rooti);
		sdp->rooti = NULL;
	}

	if(sdp->jindex){
		free(sdp->jindex);
		sdp->jindex = NULL;
	}
	if(sdp->lf_dip) {
		free(sdp->lf_dip);
		sdp->lf_dip = NULL;
	}
	while(!osi_list_empty(&sdp->rglist)){
		struct fsck_rgrp *rgd;
		unsigned int x;
		rgd = osi_list_entry(sdp->rglist.next,
				     struct fsck_rgrp, rd_list);
		osi_list_del(&rgd->rd_list);
		if(rgd->rd_bits)
			free(rgd->rd_bits);
		if(rgd->rd_bh) {
			for(x = 0; x < rgd->rd_ri.ri_length; x++) {
				if(rgd->rd_bh[x]) {
					if(BH_DATA(rgd->rd_bh[x])) {
						free(BH_DATA(rgd->rd_bh[x]));
					}
					free(rgd->rd_bh[x]);
				}
			}
			free(rgd->rd_bh);
		}
		free(rgd);
	}

	for(i = 0; i < FSCK_HASH_SIZE; i++) {
		while(!osi_list_empty(&sdp->inode_hash[i])) {
			struct inode_info *ii;
			ii = osi_list_entry(sdp->inode_hash[i].next,
					    struct inode_info, list);
			osi_list_del(&ii->list);
			free(ii);
		}
		while(!osi_list_empty(&sdp->dir_hash[i])) {
			struct dir_info *di;
			di = osi_list_entry(sdp->dir_hash[i].next,
					    struct dir_info, list);
			osi_list_del(&di->list);
			free(di);
		}
	}

	block_list_destroy(sdp->bl);
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
static int set_block_ranges(struct fsck_sb *sdp)
{
	struct gfs_jindex *jdesc;
	struct fsck_rgrp *rgd;
	struct gfs_rindex *ri;
	osi_list_t *tmp;
	char buf[sdp->sb.sb_bsize];
	uint64 rmax = 0;
	uint64 jmax = 0;
	uint64 rmin = 0;
	uint64 i;
	int error;

	log_info("Setting block ranges...\n");

	for (tmp = sdp->rglist.next; tmp != &sdp->rglist; tmp = tmp->next)
	{
		rgd = osi_list_entry(tmp, struct fsck_rgrp, rd_list);
		ri = &rgd->rd_ri;
		if (ri->ri_data1 + ri->ri_data - 1 > rmax)
			rmax = ri->ri_data1 + ri->ri_data - 1;
		if (!rmin || ri->ri_data1 < rmin)
			rmin = ri->ri_data1;
	}

	last_fs_block = rmax;

	for (i = 0; i < sdp->journals; i++)
	{
		jdesc = &sdp->jindex[i];

		if ((jdesc->ji_addr+jdesc->ji_nsegment*sdp->sb.sb_seg_size-1)
		    > jmax)
			jmax = jdesc->ji_addr + jdesc->ji_nsegment
				* sdp->sb.sb_seg_size - 1;
	}

	sdp->last_fs_block = (jmax > rmax) ? jmax : rmax;
	if (sdp->last_fs_block > 0xffffffff && sizeof(unsigned long) <= 4) {
		log_crit("This file system is too big for this computer to handle.\n");
		log_crit("Last fs block = 0x%llx, but sizeof(unsigned long) is %d bytes.\n",
				 sdp->last_fs_block, sizeof(unsigned long));
		goto fail;
	}

	sdp->last_data_block = rmax;
	sdp->first_data_block = rmin;

	if(do_lseek(sdp->diskfd, (sdp->last_fs_block * sdp->sb.sb_bsize))){
		log_crit("Can't seek to last block in file system: %"
			 PRIu64"\n", sdp->last_fs_block);
		goto fail;
	}

	memset(buf, 0, sdp->sb.sb_bsize);
	error = read(sdp->diskfd, buf, sdp->sb.sb_bsize);
	if (error != sdp->sb.sb_bsize){
		log_crit("Can't read last block in file system (%u), "
			 "last_fs_block: %"PRIu64"\n",
			 error, sdp->last_fs_block);
		goto fail;
	}

	return 0;

 fail:
	return -1;
}


/**
 * read_super_block
 * @sdp:
 *
 * Returns: 0 on success, -1 on failure
 */
static int read_super_block(struct fsck_sb *sdp)
{
	uint32_t i;

	sync();

	/********************************************************************
	 ***************** First, initialize all lists **********************
	 ********************************************************************/
	log_info("Initializing lists...\n");
	osi_list_init(&sdp->rglist);
	for(i = 0; i < FSCK_HASH_SIZE; i++) {
		osi_list_init(&sdp->dir_hash[i]);
		osi_list_init(&sdp->inode_hash[i]);
	}

	/********************************************************************
	 ************  next, read in on-disk SB and set constants  **********
	 ********************************************************************/
	sdp->sb.sb_bsize = 512;
	if (sdp->sb.sb_bsize < GFS_BASIC_BLOCK)
		sdp->sb.sb_bsize = GFS_BASIC_BLOCK;

	if(sizeof(struct gfs_sb) > sdp->sb.sb_bsize){
		log_crit("GFS superblock is larger than the blocksize!\n");
		log_debug("sizeof(struct gfs_sb) > sdp->sb.sb_bsize\n");
		return -1;
	}

	if(read_sb(sdp) < 0){
		return -1;
	}

	return 0;
}

/**
 * fill_super_block
 * @sdp:
 *
 * Returns: 0 on success, -1 on failure
 */
static int fill_super_block(struct fsck_sb *sdp)
{
        struct fsck_inode *ip = NULL;
	/*******************************************************************
	 ******************  Initialize important inodes  ******************
	 *******************************************************************/

	log_info("Initializing special inodes...\n");
	/* get ri inode */
	if(load_inode(sdp, sdp->sb.sb_rindex_di.no_addr, &ip)) {
		stack;
		return -1;
	}
	sdp->riinode = ip;

	/* get ji inode */
	if(load_inode(sdp, sdp->sb.sb_jindex_di.no_addr, &ip)) {
		stack;
		return -1;
	}
	sdp->jiinode = ip;

	/* get root dinode */
	if(!load_inode(sdp, sdp->sb.sb_root_di.no_addr, &ip)) {
		if(!check_inode(ip)) {
			sdp->rooti = ip;
		}
		else {
			free(ip);
		}
	} else {
		log_warn("Unable to load root inode\n");
	}

	/*******************************************************************
	 *******  Fill in rgrp and journal indexes and related fields  *****
	 *******************************************************************/

	/* read in the ji data */
	if (ji_update(sdp)){
		log_err("Unable to read in ji inode.\n");
		return -1;
	}

	if(ri_update(sdp)){
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

	sdp->bl = block_list_create(sdp->last_fs_block+1, gbmap);
	if (!sdp->bl)
		goto fail;

	return 0;

 fail:
	empty_super_block(sdp);

	return -1;
}

/**
 * init_sbp - initialize superblock pointer
 *
 */
static int init_sbp(struct fsck_sb *sbp)
{
	if(sbp->opts->no) {
		if ((sbp->diskfd = open(sbp->opts->device, O_RDONLY)) < 0) {
			log_crit("Unable to open device: %s\n", sbp->opts->device);
			return -1;
		}
	} else {
		/* read in sb from disk */
		if ((sbp->diskfd = open(sbp->opts->device, O_RDWR)) < 0){
			log_crit("Unable to open device: %s\n", sbp->opts->device);
			return -1;
		}
	}

	/* initialize lists and read in the sb */
	if(read_super_block(sbp)) {
		stack;
		return -1;
	}

	/* Change lock protocol to be fsck_* instead of lock_* */
	if(!sbp->opts->no) {
		if(block_mounters(sbp, 1)) {
			log_err("Unable to block other mounters\n");
			return -1;
		}
	}

	/* initialize important inodes, fill the rgrp and journal indexes, etc */
	if(fill_super_block(sbp)) {
		if(!sbp->opts->no)
			block_mounters(sbp, 0);
		stack;
		return -1;
	}

	/* verify various things */

	if(init_journals(sbp)) {
		if(!sbp->opts->no)
			block_mounters(sbp, 0);
		stack;
		return -1;
	}

	return 0;
}

static void destroy_sbp(struct fsck_sb *sbp)
{
	if(!sbp->opts->no) {
		if(block_mounters(sbp, 0)) {
			log_warn("Unable to unblock other mounters - manual intevention required\n");
			log_warn("Use 'gfs_tool sb <device> proto' to fix\n");
		}
		log_info("Syncing the device.\n");
		fsync(sbp->diskfd);
	}
	empty_super_block(sbp);
	close(sbp->diskfd);
}

int initialize(struct fsck_sb *sbp)
{

	return init_sbp(sbp);

}

void destroy(struct fsck_sb *sbp)
{
	destroy_sbp(sbp);

}
