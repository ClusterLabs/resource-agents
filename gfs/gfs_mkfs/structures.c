#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <time.h>

#include "global.h"
#include "gfs_ondisk.h"
#include "osi_list.h"
#include "linux_endian.h"
#include "libgfs.h"
#include "mkfs_gfs.h"


#define MKFS_ROOT_MODE              (0755)
#define MKFS_HIDDEN_MODE            (0600)

/**
 * rgblocks2bitblocks - blerg
 * @bsize: the FS block size
 * @rgblocks: The total number of the blocks in the RG
 *            Also, returns the number of allocateable blocks
 * @bitblocks: Returns the number of bitmap blocks
 *
 * Give a number of blocks in a RG, figure out the number of blocks
 * needed for bitmaps.
 *
 */

static void
rgblocks2bitblocks(unsigned int bsize,
		   uint32_t *rgblocks,
		   uint32_t *bitblocks)
{
	unsigned int bitbytes_provided, last = 0;
	unsigned int bitbytes_needed;

	*bitblocks = 1;
	bitbytes_provided = bsize - sizeof(struct gfs_rgrp);

	for (;;) {
	        bitbytes_needed = (*rgblocks - *bitblocks) / GFS_NBBY;

		if (bitbytes_provided >= bitbytes_needed) {
			if (last >= bitbytes_needed)
				(*bitblocks)--;
			break;
		}

		last = bitbytes_provided;
		(*bitblocks)++;
		bitbytes_provided += bsize - sizeof(struct gfs_meta_header);
	}

	*rgblocks = bitbytes_needed * GFS_NBBY;
}


/**
 * write_mkfs_sb - write the superblock
 * @comline: the command line
 * @rlist: the list of RGs
 *
 */

void write_mkfs_sb(commandline_t *comline, osi_list_t *rlist)
{
	struct gfs_sbd *sbd;
	uint64 jindex_dinode;
	char buf[comline->bsize];
	int x;

	memset(buf, 0, comline->bsize);

	for (x = 0; x < comline->sb_addr; x++) {
		do_lseek(comline->fd, x * comline->bsize);
		do_write(comline->fd, buf, comline->bsize);
	}

	/*  Figure out the location of the journal index inode  */
	{
		rgrp_list_t *rl = osi_list_entry(rlist->next, rgrp_list_t, list);
		uint32_t rgblocks, bitblocks;
		
		rgblocks = rl->rg_length;
		rgblocks2bitblocks(comline->bsize, &rgblocks, &bitblocks);
		
		jindex_dinode = rl->rg_offset + bitblocks;
	}

	/*  Now, fill in the superblock  */

	type_zalloc(sbd, struct gfs_sbd, 1);
	comline->sbd = sbd;

	sbd->sd_sb.sb_header.mh_magic = GFS_MAGIC;
	sbd->sd_sb.sb_header.mh_type = GFS_METATYPE_SB;
	sbd->sd_sb.sb_header.mh_format = GFS_FORMAT_SB;

	sbd->sd_sb.sb_fs_format = GFS_FORMAT_FS;
	sbd->sd_sb.sb_multihost_format = GFS_FORMAT_MULTI;

	sbd->sd_sb.sb_bsize = comline->bsize;
	sbd->sd_sb.sb_bsize_shift = ffs(comline->bsize) - 1;
	sbd->sd_sb.sb_seg_size = comline->seg_size;
	
	compute_constants(sbd);

	sbd->sd_sb.sb_jindex_di.no_formal_ino = jindex_dinode;
	sbd->sd_sb.sb_jindex_di.no_addr = jindex_dinode;
	sbd->sd_sb.sb_rindex_di.no_formal_ino = jindex_dinode + 1;
	sbd->sd_sb.sb_rindex_di.no_addr = jindex_dinode + 1;
	sbd->sd_sb.sb_root_di.no_formal_ino = jindex_dinode + 4;
	sbd->sd_sb.sb_root_di.no_addr = jindex_dinode + 4;

	strcpy(sbd->sd_sb.sb_lockproto, comline->lockproto);
	strcpy(sbd->sd_sb.sb_locktable, comline->locktable);

	sbd->sd_sb.sb_quota_di.no_formal_ino = jindex_dinode + 2;
	sbd->sd_sb.sb_quota_di.no_addr = jindex_dinode + 2;
	sbd->sd_sb.sb_license_di.no_formal_ino = jindex_dinode + 3;
	sbd->sd_sb.sb_license_di.no_addr = jindex_dinode + 3;
	sbd->sd_fsb2bb_shift = sbd->sd_sb.sb_bsize_shift - GFS_BASIC_BLOCK_SHIFT;

	write_sb(comline->fd, sbd);
	if (comline->debug) {
		printf("\nSuperblock:\n");
		gfs_sb_print(&sbd->sd_sb);
	}
}

/**
 * build_tree - build the pointers and indirect blocks for a file
 * @comline: the command line
 * @di: the dinode
 * @addr: the start of the file
 * @blocks: the number of blocks in the file
 *
 */

static void build_tree(commandline_t *comline, struct gfs_dinode *di, uint64 addr, unsigned int blocks)
{
	struct gfs_indirect ind;
	char *buf;
	unsigned int x, offset;
	unsigned int height;
	unsigned int indblocks;
	uint64 tmp_addr;
	unsigned int tmp_blocks;
  
	di->di_height = compute_height(comline->sbd, di->di_size);

	if (di->di_height == 1) {
		type_zalloc(buf, char, comline->bsize);

		for (x = 0; x < blocks; x++)
			((uint64 *)(buf + sizeof(struct gfs_dinode)))[x] = cpu_to_gfs64(addr + x);

		gfs_dinode_out(di, buf);

		do_lseek(comline->fd, di->di_num.no_addr * comline->bsize);
		do_write(comline->fd, buf, comline->bsize);

		free(buf);
	}
	else {
		tmp_addr = addr;
		tmp_blocks = blocks;

		for (height = di->di_height; height > 1; height--) {
			memset(&ind, 0, sizeof(struct gfs_indirect));
			ind.in_header.mh_magic = GFS_MAGIC;
			ind.in_header.mh_type = GFS_METATYPE_IN;
			ind.in_header.mh_format = GFS_FORMAT_IN;

			indblocks = DIV_RU((tmp_blocks * sizeof(uint64)),
							   (comline->bsize - sizeof(struct gfs_indirect)));

			type_zalloc(buf, char, indblocks * comline->bsize);

			offset = 0;
			for (x = 0; x < tmp_blocks; x++) {
				if (!(offset % comline->bsize)) {
					gfs_indirect_out(&ind, buf + offset);
					offset += sizeof(struct gfs_indirect);
				}
				
				*((uint64 *)(buf + offset)) = cpu_to_gfs64(tmp_addr + x);
				offset += sizeof(uint64);
			}

			do_lseek(comline->fd, comline->rgrp0_next * comline->bsize);
			do_write(comline->fd, buf, indblocks * comline->bsize);

			free(buf);

			tmp_addr = comline->rgrp0_next;
			tmp_blocks = indblocks;

			di->di_blocks += indblocks;
			
			comline->rgrp0_next += indblocks;
		}
		type_zalloc(buf, char, comline->bsize);

		for (x = 0; x < tmp_blocks; x++)
			((uint64 *)(buf + sizeof(struct gfs_dinode)))[x] = cpu_to_gfs64(tmp_addr + x);

		gfs_dinode_out(di, buf);

		do_lseek(comline->fd, di->di_num.no_addr * comline->bsize);
		do_write(comline->fd, buf, comline->bsize);    

		free(buf);
	}
}


/**
 * fill_jindex - create the journal index data
 * @comline: the command line
 * @jlist: the list of journals
 *
 * Returns: a pointer data for the jindex
 */

static char *fill_jindex(commandline_t *comline, osi_list_t *jlist)
{
	journal_list_t *jl;
	struct gfs_jindex ji;
	osi_list_t *tmp;
	char *buf;
	unsigned int j = 0;
	
	type_alloc(buf, char, comline->journals * sizeof(struct gfs_jindex));

	for (tmp = jlist->next; tmp != jlist; tmp = tmp->next) {
		jl = osi_list_entry(tmp, journal_list_t, list);

		memset(&ji, 0, sizeof(struct gfs_jindex));

		ji.ji_addr = jl->start;
		ji.ji_nsegment = jl->segments;

		gfs_jindex_out(&ji, buf + j * sizeof(struct gfs_jindex));

		j++;
	}

	if (comline->debug) {
		printf("\nJournal Index data:\n");

		for (j = 0; j < comline->journals; j++) {
			gfs_jindex_in(&ji, buf + j * sizeof(struct gfs_jindex));
			
			printf("\n  Journal %d\n", j);
			gfs_jindex_print(&ji);
		}
	}

	return buf;
}

/**
 * write_jindex - write out the journal index
 * @comline: the command line
 * @jlist: the list of journals
 *
 */

void write_jindex(commandline_t *comline, osi_list_t *jlist)
{
	struct gfs_dinode di;
	struct gfs_meta_header jd;
	char *buf, *data;
	uint64 addr;
	unsigned int blocks;
	unsigned int x, left, jbsize = comline->bsize - sizeof(struct gfs_meta_header);

	memset(&di, 0, sizeof(struct gfs_dinode));
 
	di.di_header.mh_magic = GFS_MAGIC;
	di.di_header.mh_type = GFS_METATYPE_DI;
	di.di_header.mh_format = GFS_FORMAT_DI;

	di.di_num = comline->sbd->sd_sb.sb_jindex_di;

	di.di_mode = MKFS_HIDDEN_MODE;
	di.di_nlink = 1;
	di.di_size = comline->journals * sizeof(struct gfs_jindex);
	di.di_blocks = 1;
	di.di_atime = di.di_mtime = di.di_ctime = time(NULL);

	di.di_flags = GFS_DIF_JDATA;
	di.di_payload_format = GFS_FORMAT_JI;
	di.di_type = GFS_FILE_REG;

	data = fill_jindex(comline, jlist);

	if (di.di_size < comline->bsize - sizeof(struct gfs_dinode)) {
		type_zalloc(buf, char, comline->bsize);

		gfs_dinode_out(&di, buf);

		memcpy(buf + sizeof(struct gfs_dinode), data, comline->journals * sizeof(struct gfs_jindex));

		do_lseek(comline->fd, di.di_num.no_addr * comline->bsize);
		do_write(comline->fd, buf, comline->bsize);

		free(buf);
	}
	else {
		blocks = DIV_RU(di.di_size, (comline->bsize - sizeof(struct gfs_meta_header)));
		di.di_blocks += blocks;

		addr = comline->rgrp0_next;
		memset(&jd, 0, sizeof(struct gfs_meta_header));
		jd.mh_magic = GFS_MAGIC;
		jd.mh_type = GFS_METATYPE_JD;
		jd.mh_format = GFS_FORMAT_JD;

		type_zalloc(buf, char, blocks * comline->bsize);

		left = comline->journals * sizeof(struct gfs_jindex);
		for (x = 0; x < blocks; x++) {
			gfs_meta_header_out(&jd, buf + x * comline->bsize);
			memcpy(buf + x * comline->bsize + sizeof(struct gfs_meta_header),
				   data + x * jbsize,
				   (left > jbsize) ? jbsize : left);
			left -= jbsize;
		}

		do_lseek(comline->fd, addr * comline->bsize);
		do_write(comline->fd, buf, blocks * comline->bsize);

		free(buf);

		comline->rgrp0_next += blocks;

		build_tree(comline, &di, addr, blocks);
	}

	free(data);

	if (comline->debug) {
		printf("\nJournal index dinode:\n");
		gfs_dinode_print(&di);
	}
}


/**
 * fill_rindex - create the resource group index data
 * @comline: the command line
 * @rlist: the list of RGs
 *
 * Returns: a pointer data for the rindex
 */

static char *fill_rindex(commandline_t *comline, osi_list_t *rlist)
{
	struct gfs_rindex *ri, rindex;
	rgrp_list_t *rl;
	osi_list_t *tmp;
	char *buf;
	unsigned int r = 0;
	uint32 rgblocks, bitblocks;

	type_alloc(buf, char, comline->rgrps * sizeof(struct gfs_rindex));

	for (tmp = rlist->next; tmp != rlist; tmp = tmp->next) {
		rl = osi_list_entry(tmp, rgrp_list_t, list);
	
		rgblocks = rl->rg_length;
		rgblocks2bitblocks(comline->bsize, &rgblocks, &bitblocks);

		type_zalloc(ri, struct gfs_rindex, 1);
		rl->ri = ri;
		
		ri->ri_addr = rl->rg_offset;
		ri->ri_length = bitblocks;

		ri->ri_data1 = rl->rg_offset + bitblocks;
		ri->ri_data = rgblocks;

		ri->ri_bitbytes = rgblocks / GFS_NBBY;

		gfs_rindex_out(ri, buf + r * sizeof(struct gfs_rindex));
		
		comline->fssize += rgblocks;

		r++;
	}

	if (comline->debug) {
		printf("\nResource Index data:\n");

		for (r = 0; r < comline->rgrps; r++) {
			gfs_rindex_in(&rindex, buf + r * sizeof(struct gfs_rindex));

			printf("\n  Resource index %d\n", r);
			gfs_rindex_print(&rindex);
		}
	}
	return buf;
}

/**
 * write_rindex - write out the resource group index
 * @comline: the command line
 * @jlist: the list of RGs
 *
 */

void write_rindex(commandline_t *comline, osi_list_t *rlist)
{
	struct gfs_dinode di;
	struct gfs_meta_header jd;
	char *buf, *data;
	uint64 addr;
	unsigned int blocks;
	unsigned int x, left, jbsize = comline->bsize - sizeof(struct gfs_meta_header);

	memset(&di, 0, sizeof(struct gfs_dinode));

	di.di_header.mh_magic = GFS_MAGIC;
	di.di_header.mh_type = GFS_METATYPE_DI;
	di.di_header.mh_format = GFS_FORMAT_DI;

	di.di_num = comline->sbd->sd_sb.sb_rindex_di;

	di.di_mode = MKFS_HIDDEN_MODE;
	di.di_nlink = 1;
	di.di_size = comline->rgrps * sizeof(struct gfs_rindex);
	di.di_blocks = 1;
	di.di_atime = di.di_mtime = di.di_ctime = time(NULL);

	di.di_flags = GFS_DIF_JDATA;
	di.di_payload_format = GFS_FORMAT_RI;
	di.di_type = GFS_FILE_REG;

	data = fill_rindex(comline, rlist);

	if (di.di_size < comline->bsize - sizeof(struct gfs_dinode)) {
		type_zalloc(buf, char, comline->bsize);

		gfs_dinode_out(&di, buf);
    
		memcpy(buf + sizeof(struct gfs_dinode), data, comline->rgrps * sizeof(struct gfs_rindex));

		do_lseek(comline->fd, di.di_num.no_addr * comline->bsize);
		do_write(comline->fd, buf, comline->bsize);

		free(buf);
	}
	else {
		blocks = DIV_RU(di.di_size, (comline->bsize - sizeof(struct gfs_meta_header)));
		di.di_blocks += blocks;

		addr = comline->rgrp0_next;

		memset(&jd, 0, sizeof(struct gfs_meta_header));
		jd.mh_magic = GFS_MAGIC;
		jd.mh_type = GFS_METATYPE_JD;
		jd.mh_format = GFS_FORMAT_JD;

		type_zalloc(buf, char, blocks * comline->bsize);

		left = comline->rgrps * sizeof(struct gfs_rindex);
		for (x = 0; x < blocks; x++) {
			gfs_meta_header_out(&jd, buf + x * comline->bsize);
			memcpy(buf + x * comline->bsize + sizeof(struct gfs_meta_header),
				   data + x * jbsize,
				   (left > jbsize) ? jbsize : left);
			left -= jbsize;
		}

		do_lseek(comline->fd, addr * comline->bsize);
		do_write(comline->fd, buf, blocks * comline->bsize);

		free(buf);

		comline->rgrp0_next += blocks;

		build_tree(comline, &di, addr, blocks);
	}

	free(data);

	if (comline->debug) {
		printf("\nResource index dinode:\n");
		gfs_dinode_print(&di);
	}
}


/**
 * write_root - write out the root dinode
 * @comline: the command line
 *
 */

void write_root(commandline_t *comline)
{
	struct gfs_dinode di;
	struct gfs_dirent de;
	char buf[comline->bsize];

	memset(&di, 0, sizeof(struct gfs_dinode));
	memset(buf, 0, comline->bsize);

	di.di_header.mh_magic = GFS_MAGIC;
	di.di_header.mh_type = GFS_METATYPE_DI;
	di.di_header.mh_format = GFS_FORMAT_DI;

	di.di_num = comline->sbd->sd_sb.sb_root_di;

	di.di_mode = MKFS_ROOT_MODE;
	di.di_nlink = 2;
	di.di_size = comline->bsize - sizeof(struct gfs_dinode);
	di.di_blocks = 1;
	di.di_atime = di.di_mtime = di.di_ctime = time(NULL);

	di.di_flags = GFS_DIF_JDATA;
	di.di_payload_format = GFS_FORMAT_DE;
	di.di_type = GFS_FILE_DIR;

	di.di_entries = 2;

	gfs_dinode_out(&di, buf);

	/*  Fill in .  */

	memset(&de, 0, sizeof(struct gfs_dirent));

	de.de_inum = comline->sbd->sd_sb.sb_root_di;
	de.de_hash = gfs_dir_hash(".", 1);
	de.de_rec_len = GFS_DIRENT_SIZE(1);
	de.de_name_len = 1;
	de.de_type = GFS_FILE_DIR;

	gfs_dirent_out(&de, buf + sizeof(struct gfs_dinode));
	memcpy(buf + sizeof(struct gfs_dinode) + sizeof(struct gfs_dirent), ".", 1);

	if (comline->debug) {
		printf("\nRoot Dinode dirent:\n");
		gfs_dirent_print(&de, ".");
	}

	/*  Fill in ..  */

	memset(&de, 0, sizeof(struct gfs_dirent));

	de.de_inum = comline->sbd->sd_sb.sb_root_di;
	de.de_hash = gfs_dir_hash("..", 2);
	de.de_rec_len = comline->bsize - GFS_DIRENT_SIZE(1) - sizeof(struct gfs_dinode);
	de.de_name_len = 2;
	de.de_type = GFS_FILE_DIR;

	gfs_dirent_out(&de, buf + sizeof(struct gfs_dinode) + GFS_DIRENT_SIZE(1));
	memcpy(buf + sizeof(struct gfs_dinode) + GFS_DIRENT_SIZE(1) + sizeof(struct gfs_dirent), "..", 2);

	if (comline->debug) {
		printf("\nRoot Dinode dirent:\n");
		gfs_dirent_print(&de, "..");
	}

	do_lseek(comline->fd, di.di_num.no_addr * comline->bsize);
	do_write(comline->fd, buf, comline->bsize);

	if (comline->debug) {
		printf("\nRoot dinode:\n");
		gfs_dinode_print(&di);
	}
}

/**
 * write_quota - write out the quota dinode
 * @comline: the command line
 *
 */

void write_quota(commandline_t *comline)
{
	struct gfs_dinode di;
	struct gfs_quota qu;

	char buf[comline->bsize];

	memset(&di, 0, sizeof(struct gfs_dinode));
	memset(buf, 0, comline->bsize);

	di.di_header.mh_magic = GFS_MAGIC;
	di.di_header.mh_type = GFS_METATYPE_DI;
	di.di_header.mh_format = GFS_FORMAT_DI;

	di.di_num = comline->sbd->sd_sb.sb_quota_di;

	di.di_mode = MKFS_HIDDEN_MODE;
	di.di_nlink = 1;
	di.di_size = 2 * sizeof(struct gfs_quota);
	di.di_blocks = 1;
	di.di_atime = di.di_mtime = di.di_ctime = time(NULL);

	di.di_flags = GFS_DIF_JDATA;
	di.di_payload_format = GFS_FORMAT_QU;
	di.di_type = GFS_FILE_REG;

	gfs_dinode_out(&di, buf);

	/*  Fill in the root user quota  */

	memset(&qu, 0, sizeof(struct gfs_quota));
	qu.qu_value = comline->rgrp0_next - comline->sbd->sd_sb.sb_jindex_di.no_addr;

	gfs_quota_out(&qu, buf + sizeof(struct gfs_dinode));

	if (comline->debug) {
		printf("\nRoot user quota:\n");
		gfs_quota_print(&qu);
	}

	/*  Fill in the root group quota  */

	memset(&qu, 0, sizeof(struct gfs_quota));
	qu.qu_value = comline->rgrp0_next - comline->sbd->sd_sb.sb_jindex_di.no_addr;

	gfs_quota_out(&qu, buf + sizeof(struct gfs_dinode) + sizeof(struct gfs_quota));

	if (comline->debug) {
		printf("\nRoot group quota:\n");
		gfs_quota_print(&qu);
	}

	do_lseek(comline->fd, di.di_num.no_addr * comline->bsize);
	do_write(comline->fd, buf, comline->bsize);

	if (comline->debug) {
		printf("\nQuota dinode:\n");
		gfs_dinode_print(&di);
	}
}

/**
 * write_license - write out the quota dinode
 * @comline: the command line
 *
 */

void write_license(commandline_t *comline)
{
	struct gfs_dinode di;

	char buf[comline->bsize];

	memset(&di, 0, sizeof(struct gfs_dinode));
	memset(buf, 0, comline->bsize);

	di.di_header.mh_magic = GFS_MAGIC;
	di.di_header.mh_type = GFS_METATYPE_DI;
	di.di_header.mh_format = GFS_FORMAT_DI;

	di.di_num = comline->sbd->sd_sb.sb_license_di;

	di.di_mode = MKFS_HIDDEN_MODE;
	di.di_nlink = 1;
	di.di_size = 0;
	di.di_blocks = 1;
	di.di_atime = di.di_mtime = di.di_ctime = time(NULL);

	di.di_flags = GFS_DIF_JDATA;
	di.di_payload_format = GFS_FORMAT_QU;
	di.di_type = GFS_FILE_REG;

	gfs_dinode_out(&di, buf);

	do_lseek(comline->fd, di.di_num.no_addr * comline->bsize);
	do_write(comline->fd, buf, comline->bsize);


	if (comline->debug) {
		printf("\nLicense dinode:\n");
		gfs_dinode_print(&di);
	}
}

/**
 * write_rgrps - write out the resource group headers
 * @comline: the command line
 * @rlist: the RG list
 *
 */

void write_rgrps(commandline_t *comline, osi_list_t *rlist)
{
	struct gfs_rgrp rg;
	struct gfs_meta_header rb;
	rgrp_list_t *rl;
	osi_list_t *tmp;
	char *buf, *data;
	unsigned int x, offset;
	unsigned int byte, bit;
	unsigned int blk_count;
	unsigned int r = 0;

	memset(&rb, 0, sizeof(struct gfs_meta_header));
	rb.mh_magic = GFS_MAGIC;
	rb.mh_type = GFS_METATYPE_RB;
	rb.mh_format = GFS_FORMAT_RB;

	for (tmp = rlist->next; tmp != rlist; tmp = tmp->next) {
		rl = osi_list_entry(tmp, rgrp_list_t, list);
		
		memset(&rg, 0, sizeof(struct gfs_rgrp));
		rg.rg_header.mh_magic = GFS_MAGIC;
		rg.rg_header.mh_type = GFS_METATYPE_RG;
		rg.rg_header.mh_format = GFS_FORMAT_RG;
		rg.rg_free = rl->ri->ri_data;

		type_zalloc(data, char, rl->ri->ri_bitbytes);
		type_zalloc(buf, char, rl->ri->ri_length * comline->bsize);

		/*  Deal with the special case of the first dinode  */

		if (!r) {
			if (comline->rgrp0_next > rl->ri->ri_data1 + rl->ri->ri_data)
				die("RG 0 is full.\n");
			
			/*  Compensate for the hidden dinodes  */

			*data = (GFS_BLKST_USEDMETA << 3 * GFS_BIT_SIZE) |
				(GFS_BLKST_USEDMETA << 2 * GFS_BIT_SIZE) |
				(GFS_BLKST_USEDMETA << GFS_BIT_SIZE) |
				GFS_BLKST_USEDMETA;
			*(data + 1) = GFS_BLKST_USEDMETA;
			rg.rg_free -= 5;
			rg.rg_useddi = 5;

			/*  Compensate for the data the hidden dinodes point to  */

			for (x = rl->ri->ri_data1 + 5; x < comline->rgrp0_next; x++) {
				byte = (x - rl->ri->ri_data1) / GFS_NBBY;
				bit = (x - rl->ri->ri_data1) % GFS_NBBY;
				
				data[byte] |= GFS_BLKST_USEDMETA << (bit * GFS_BIT_SIZE);
				
				rg.rg_free--;
				rg.rg_usedmeta++;
			}
		}

		gfs_rgrp_out(&rg, buf);
		offset = sizeof(struct gfs_rgrp);
		blk_count = 1;

		for (x = 0; x < rl->ri->ri_bitbytes; x++) {
			if (!(offset % comline->bsize)) {
				gfs_meta_header_out(&rb, buf + offset);
				offset += sizeof(struct gfs_meta_header);
				blk_count++;
			}

			buf[offset] = data[x];
			offset++;
		}

		if (blk_count != rl->ri->ri_length)
			die("RG underflow (rg = %u, blk_count = %u, rl->ri->ri_length = %u)\n",
				r, blk_count, rl->ri->ri_length);
		
		do_lseek(comline->fd, rl->ri->ri_addr * comline->bsize);
		do_write(comline->fd, buf, rl->ri->ri_length * comline->bsize);

		free(buf);
		free(data);

		if (comline->debug) {
			printf("\nResource group header %d\n", r);
			gfs_rgrp_print(&rg);
		}

		r++;
	}
}

/**
 * write_journals - write out the journal log headers
 * @comline: the command line
 * @rlist: the RG list
 *
 */

void write_journals(commandline_t *comline, osi_list_t *jlist)
{
	struct gfs_log_header lh;
	journal_list_t *jl;
	osi_list_t *tmp;
	char buf[comline->bsize];
	uint32 seg, sequence;
	int x = 0;

	srandom(time(NULL));

	for (tmp = jlist->next; tmp != jlist; tmp = tmp->next) {
		jl = osi_list_entry(tmp, journal_list_t, list);

		if (comline->debug)
			printf("Starting journal %d\n", x++);

		sequence = jl->segments / (RAND_MAX + 1.0) * random();

		for (seg = 0; seg < jl->segments; seg++) {
			memset(buf, 0, comline->bsize);
			memset(&lh, 0, sizeof(struct gfs_log_header));

			lh.lh_header.mh_magic = GFS_MAGIC;
			lh.lh_header.mh_type = GFS_METATYPE_LH;
			lh.lh_header.mh_format = GFS_FORMAT_LH;
			lh.lh_flags = GFS_LOG_HEAD_UNMOUNT;
			lh.lh_first = jl->start + seg * comline->seg_size;
			lh.lh_sequence = sequence;
			/*  Don't care about tail  */
			/*  Don't care about log dump  */

			gfs_log_header_out(&lh, buf);
			gfs_log_header_out(&lh, buf + GFS_BASIC_BLOCK - sizeof(struct gfs_log_header));

			do_lseek(comline->fd, lh.lh_first * comline->bsize);
			do_write(comline->fd, buf, comline->bsize);

			if (++sequence == jl->segments)
				sequence = 0;

			if (!(seg % 100))
				if (comline->debug)
					printf("seg #%u\n", seg);
		}
	}
}
