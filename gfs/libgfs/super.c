#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>

#include "osi_list.h"
#include "osi_user.h"
#include "gfs_ondisk.h"
#include "incore.h"
#include "libgfs.h"

#ifndef BLKGETSIZE64
#define BLKGETSIZE64 _IOR(0x12, 114, size_t)
#endif

#define DIV_RU(x, y) (((x) + (y) - 1) / (y))
#define ri_compare(rg, ondisk, expected, field, fmt)	\
	if (ondisk.field != expected.field) { \
		log_warn("rgindex[%d] " #field " discrepancy: index 0x%" fmt \
				 " != expected: 0x%" fmt "\n", \
				 rg, ondisk.field, expected.field);	\
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
static int check_sb(struct gfs_sbd *sdp, struct gfs_sb *sb)
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
 * compute_constants: compute constants for the superblock
 *
 * assumes:
 *    sb_bsize_shift is set either from the ondisk superblock or otherwise.
 *    sb_bsize is set either from the ondisk superblock or otherwise.
 */
int compute_constants(struct gfs_sbd *sdp)
{
	unsigned int x;
	uint64 space = 0;
	int error = 0;

	sdp->sd_fsb2bb_shift = sdp->sd_sb.sb_bsize_shift - 9;
	sdp->sd_diptrs =
		(sdp->sd_sb.sb_bsize-sizeof(struct gfs_dinode)) /
		sizeof(uint64);
	sdp->sd_inptrs =
		(sdp->sd_sb.sb_bsize-sizeof(struct gfs_indirect)) /
		sizeof(uint64);
	sdp->sd_jbsize = sdp->sd_sb.sb_bsize - sizeof(struct gfs_meta_header);
	sdp->sd_hash_bsize = sdp->sd_sb.sb_bsize / 2;
	sdp->sd_hash_ptrs = sdp->sd_hash_bsize / sizeof(uint64);
	sdp->sd_heightsize[0] = sdp->sd_sb.sb_bsize -
		sizeof(struct gfs_dinode);
	sdp->sd_heightsize[1] = sdp->sd_sb.sb_bsize * sdp->sd_diptrs;
	for (x = 2; ; x++){
		space = sdp->sd_heightsize[x - 1] * sdp->sd_inptrs;
		/* FIXME: Do we really need this first check?? */
		if (space / sdp->sd_inptrs != sdp->sd_heightsize[x - 1] ||
		    space % sdp->sd_inptrs != 0)
			break;
		sdp->sd_heightsize[x] = space;
	}
	sdp->sd_max_height = x;
	if(sdp->sd_max_height > GFS_MAX_META_HEIGHT){
		log_err("Bad max metadata height.\n");
		error = -1;
		return error;
	}

	sdp->sd_jheightsize[0] = sdp->sd_sb.sb_bsize -
		sizeof(struct gfs_dinode);
	sdp->sd_jheightsize[1] = sdp->sd_jbsize * sdp->sd_diptrs;
	for (x = 2; ; x++){
		space = sdp->sd_jheightsize[x - 1] * sdp->sd_inptrs;
		if (space / sdp->sd_inptrs != sdp->sd_jheightsize[x - 1] ||
		    space % sdp->sd_inptrs != 0)
			break;
		sdp->sd_jheightsize[x] = space;
	}
	sdp->sd_max_jheight = x;
	if(sdp->sd_max_jheight > GFS_MAX_META_HEIGHT){
		log_err("Bad max jheight.\n");
		error = -1;
	}
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
int read_sb(int disk_fd, struct gfs_sbd *sdp)
{
	osi_buf_t *bh;
	int error;
	error = get_and_read_buf(disk_fd, 512, /* assume 512 block size at first */
							 GFS_SB_ADDR >> sdp->sd_fsb2bb_shift, &bh, 0);
	if (error){
		log_crit("Unable to read superblock\n");
		goto out;
	}

	gfs_sb_in(&sdp->sd_sb, BH_DATA(bh));

	relse_buf(bh);

	error = check_sb(sdp, &sdp->sd_sb);
	if (error)
		goto out;

	compute_constants(sdp);

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
int ji_update(int disk_fd, struct gfs_sbd *sdp)
{
	struct gfs_inode *ip = sdp->sd_jiinode;
	char buf[sizeof(struct gfs_jindex)];
	unsigned int j;
	int error=0;


	if(ip->i_di.di_size % sizeof(struct gfs_jindex) != 0){
		log_err("The size reported in the journal index"
			" inode is not a\n"
			 "\tmultiple of the size of a journal index.\n");
		return -1;
	}

	if(!(sdp->sd_jindex = (struct gfs_jindex *)malloc(ip->i_di.di_size))) {
		log_err("Unable to allocate journal index\n");
		return -1;
	}
	if(!memset(sdp->sd_jindex, 0, ip->i_di.di_size)) {
		log_err("Unable to zero journal index\n");
		return -1;
	}
	total_journal_space = 0;

	for (j = 0; ; j++) {
		struct gfs_jindex *journ;
		error = readi(disk_fd, ip, buf, j * sizeof(struct gfs_jindex),
				 sizeof(struct gfs_jindex));
		if(!error)
			break;
		if (error != sizeof(struct gfs_jindex)){
			log_err("An error occurred while reading the"
				" journal index file.\n");
			goto fail;
		}

		journ = sdp->sd_jindex + j;
		gfs_jindex_in(journ, buf);
		total_journal_space += journ->ji_nsegment * sdp->sd_sb.sb_seg_size;
	}


	if(j * sizeof(struct gfs_jindex) != ip->i_di.di_size){
		log_err("journal inode size invalid\n");
		log_debug("j * sizeof(struct gfs_jindex) !="
			  " ip->i_di.di_size\n");
		log_debug("%d != %d\n",
			  j * sizeof(struct gfs_jindex), ip->i_di.di_size);
		goto fail;
	}
	sdp->sd_journals = j;
	log_debug("%d journals found.\n", j);

	return 0;

 fail:
	free(sdp->sd_jindex);
	return -1;
}

/* Print out debugging information in same format as gfs_edit. */
int hexdump(uint64 startaddr, const unsigned char *lpBuffer, int len)
{
	const unsigned char *pointer,*ptr2;
	int i;
	uint64 l;

	pointer = (unsigned char *)lpBuffer;
	ptr2 = (unsigned char *)lpBuffer;
	l = 0;
	while (l < len) {
		log_info("%.8"PRIX64,startaddr + l);
		for (i=0; i<16; i++) { /* first print it in hex */
			if (i%4 == 0)
				log_info(" ");
			log_info("%02X",*pointer);
			pointer++;
		}
		log_info(" [");
		for (i=0; i<16; i++) { /* now print it in character format */
			if ((*ptr2 >=' ') && (*ptr2 <= '~'))
				log_info("%c",*ptr2);
			else
				log_info(".");
			ptr2++;
		}
		log_info("] \n");
		l+=16;
	}
	return (len);
}


/**
 * rgrplength2bitblocks - blerg - Stolen by Bob from gfs_mkfs.  Good 
 * candidate for gfslib.
 *
 * @sdp:    the superblock
 * @length: the number of blocks in a RG
 *
 * Give a number of blocks in a RG, figure out the number of blocks
 * needed for bitmaps.
 *
 * Returns: the number of bitmap blocks
 */

uint32 rgrplength2bitblocks(struct gfs_sbd *sdp, uint32 length)
{
	uint32 bitbytes;
	uint32 old_blocks = 0, blocks;
	int tries = 0;
	
	for (;;) {
		bitbytes = (length - old_blocks) / GFS_NBBY;
		blocks = 1;

		if (bitbytes > sdp->sd_sb.sb_bsize - sizeof(struct gfs_rgrp)) {
			bitbytes -= sdp->sd_sb.sb_bsize - sizeof(struct gfs_rgrp);
			blocks += DIV_RU(bitbytes, (sdp->sd_sb.sb_bsize -
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
 * ri_cleanup - free up the memory we previously allocated.
 */
void ri_cleanup(osi_list_t *rglist)
{
	struct gfs_rgrpd *rgd;

	while(!osi_list_empty(rglist)){
		rgd = osi_list_entry(rglist->next, struct gfs_rgrpd, rd_list);
		if(rgd->rd_bits)
			free(rgd->rd_bits);
		if(rgd->rd_bh)
			free(rgd->rd_bh);
		osi_list_del((osi_list_t *)&rgd->rd_list);
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
 * Returns: 0 on success, -1 on failure.
 */
int ri_update(int disk_fd, struct gfs_sbd *sdp)
{
	struct gfs_rgrpd *rgd;
	osi_list_t *tmp;
	struct gfs_rindex buf;
	unsigned int rg;
	int error, count1 = 0, count2 = 0;
	
	for (rg = 0; ; rg++) {
		error = readi(disk_fd, sdp->sd_riinode, (char *)&buf,
					  rg * sizeof(struct gfs_rindex),
					  sizeof(struct gfs_rindex));
		if (!error)
			break;
		if (error != sizeof(struct gfs_rindex)){
			log_err("Unable to read resource group index #%u.\n", rg);
			goto fail;
		}
		rgd = (struct gfs_rgrpd *)malloc(sizeof(struct gfs_rgrpd));
		rgd->rd_sbd = sdp;
		osi_list_add_prev((osi_list_t *)&rgd->rd_list,
						  (osi_list_t *)&sdp->sd_rglist);
		gfs_rindex_in(&rgd->rd_ri, (char *)&buf);
		if(fs_compute_bitstructs(rgd)){
			goto fail;
		}
		rgd->rd_open_count = 0;
		count1++;
	}
	log_debug("%u resource groups found.\n", rg);
	for (tmp = (osi_list_t *)sdp->sd_rglist.next;
		 tmp != (osi_list_t *)&sdp->sd_rglist; tmp = tmp->next) {
		rgd = osi_list_entry(tmp, struct gfs_rgrpd, rd_list);
		error = fs_rgrp_read(disk_fd, rgd, FALSE);
		if (error) {
			log_err("Unable to read in rgrp descriptor.\n");
			goto fail;
		}
		fs_rgrp_relse(rgd);
		count2++;
	}
	if (count1 != count2) {
		log_err("Rgrps allocated (%d) does not equal"
				" rgrps read (%d).\n", count1, count2);
		goto fail;
	}
	sdp->sd_rgcount = count1;
	return 0;

 fail:
	while(!osi_list_empty((osi_list_t *)&sdp->sd_rglist)){
		rgd = osi_list_entry((osi_list_t *)sdp->sd_rglist.next,
							 struct gfs_rgrpd, rd_list);
		if(rgd->rd_bits)
			free(rgd->rd_bits);
		if(rgd->rd_bh)
			free(rgd->rd_bh);
		osi_list_del((osi_list_t *)&rgd->rd_list);
		free(rgd);
	}
	return -1;
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
int set_block_ranges(int disk_fd, struct gfs_sbd *sdp)
{
	struct gfs_jindex *jdesc;
	struct gfs_rgrpd *rgd;
	struct gfs_rindex *ri;
	osi_list_t *tmp;
	char buf[sdp->sd_sb.sb_bsize];
	uint64 rmax = 0;
	uint64 jmax = 0;
	uint64 rmin = 0;
	uint64 i;
	int error;

	log_info("Setting block ranges...\n");

	for (tmp = (osi_list_t *)sdp->sd_rglist.next;
		 tmp != (osi_list_t *)&sdp->sd_rglist; tmp = tmp->next)
	{
		rgd = osi_list_entry(tmp, struct gfs_rgrpd, rd_list);
		ri = &rgd->rd_ri;
		if (ri->ri_data1 + ri->ri_data - 1 > rmax)
			rmax = ri->ri_data1 + ri->ri_data - 1;
		if (!rmin || ri->ri_data1 < rmin)
			rmin = ri->ri_data1;
	}


	for (i = 0; i < sdp->sd_journals; i++)
	{
		jdesc = &sdp->sd_jindex[i];

		if ((jdesc->ji_addr+jdesc->ji_nsegment*sdp->sd_sb.sb_seg_size-1)
		    > jmax)
			jmax = jdesc->ji_addr + jdesc->ji_nsegment
				* sdp->sd_sb.sb_seg_size - 1;
	}

	sdp->last_fs_block = (jmax > rmax) ? jmax : rmax;

	sdp->last_data_block = rmax;
	sdp->first_data_block = rmin;

	if(do_lseek(disk_fd, (sdp->last_fs_block * sdp->sd_sb.sb_bsize))){
		log_crit("Can't seek to last block in file system: %"
			 PRIu64"\n", sdp->last_fs_block);
		goto fail;
	}

	memset(buf, 0, sdp->sd_sb.sb_bsize);
	error = read(disk_fd, buf, sdp->sd_sb.sb_bsize);
	if (error != sdp->sd_sb.sb_bsize){
		log_crit("Can't read last block in file system (%u), "
			 "last_fs_block: %"PRIu64"\n",
			 error, sdp->last_fs_block);
		goto fail;
	}

	return 0;

 fail:
	return -1;
}

int write_sb(int disk_fd, struct gfs_sbd *sbp)
{
	int error = 0;
	osi_buf_t *bh;

	error = get_and_read_buf(disk_fd, sbp->sd_sb.sb_bsize,
							 GFS_SB_ADDR >> sbp->sd_fsb2bb_shift, &bh, 0);
	if (error){
		log_crit("Unable to read superblock\n");
		goto out;
	}

	memset(BH_DATA(bh), 0, sbp->sd_sb.sb_bsize);
	gfs_sb_out(&sbp->sd_sb, BH_DATA(bh));

	/* FIXME: Should this set the BW_WAIT flag? */
	if((error = write_buf(disk_fd, bh, 0))) {
		stack;
		goto out;
	}

	relse_buf(bh);
out:
	return error;

}

/**
 * read_super_block
 * @sdp:
 *
 * Returns: 0 on success, -1 on failure
 */
int read_super_block(int disk_fd, struct gfs_sbd *sdp)
{
	uint32_t i;

	sync();

	/********************************************************************
	 ***************** First, initialize all lists **********************
	 ********************************************************************/
	log_info("Initializing lists...\n");
	osi_list_init((osi_list_t *)&sdp->sd_rglist);
	for(i = 0; i < FSCK_HASH_SIZE; i++) {
		osi_list_init(&sdp->dir_hash[i]);
		osi_list_init(&sdp->inode_hash[i]);
	}

	/********************************************************************
	 ************  next, read in on-disk SB and set constants  **********
	 ********************************************************************/
	sdp->sd_sb.sb_bsize = 512;
	if (sdp->sd_sb.sb_bsize < GFS_BASIC_BLOCK)
		sdp->sd_sb.sb_bsize = GFS_BASIC_BLOCK;

	if(sizeof(struct gfs_sb) > sdp->sd_sb.sb_bsize){
		log_crit("GFS superblock is larger than the blocksize!\n");
		log_debug("sizeof(struct gfs_sb) > sdp->sb.sb_bsize\n");
		return -1;
	}

	if(read_sb(disk_fd, sdp) < 0){
		return -1;
	}

	return 0;
}
