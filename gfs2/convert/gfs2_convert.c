/*****************************************************************************
******************************************************************************
**
**  gfs2_convert - convert a gfs1 filesystem into a gfs2 filesystem.
**
**  Copyright (C) 2006 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
******************************************************************************
*****************************************************************************/

#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>

#include "libgfs.h"
#include "linux_endian.h"
#include <linux/types.h>
#include <linux/gfs2_ondisk.h>
#include "osi_list.h"
#include "copyright.cf"
#include "ondisk.h"
#include "libgfs2.h"
#include "incore.h"

#include "global.h"

#define RGRP_STUFFED_BLKS(sb) (((sb)->sb_bsize - sizeof(struct gfs_rgrp)) * GFS_NBBY)
#define RGRP_BITMAP_BLKS(sb) (((sb)->sb_bsize - sizeof(struct gfs_meta_header)) * GFS_NBBY)

struct inode_block {
	osi_list_t list;
	uint64_t di_addr;
};

struct gfs_sbd sb;
struct gfs2_sbd sb2;
char device[256];
struct inode_block dirs_to_fix;  /* linked list of directories to fix */
int seconds;
struct timeval tv;
uint64_t inode_count;
uint64_t dirs_fixed;
uint64_t dirents_fixed;
char *prog_name = "gfs2_convert"; /* needed by libgfs2 */

/* ------------------------------------------------------------------------- */
/* This function is for libgfs's sake.                                       */
/* ------------------------------------------------------------------------- */
void print_it(const char *label, const char *fmt, const char *fmt2, ...)
{
	va_list args;

	va_start(args, fmt2);
	printf("%s: ", label);
	vprintf(fmt, args);
	va_end(args);
}

/* ------------------------------------------------------------------------- */
/* convert_bitmaps - Convert gfs1 bitmaps to gfs2 bitmaps.                   */
/*                   Fixes all unallocated metadata bitmap states (which are */
/*                   valid in gfs1 but invalid in gfs2).                     */
/* ------------------------------------------------------------------------- */
void convert_bitmaps(int disk_fd, struct gfs2_sbd *sdp, struct rgrp_list *rgd2)
{
	uint32_t block;
	int x, y;
	struct gfs2_rindex *ri;
	struct gfs2_buffer_head *bh;
	unsigned char state;

	ri = &rgd2->ri;
	for (block = 0; block < ri->ri_length; block++) {
		bh = bread(sdp, ri->ri_addr + block);
		x = (block) ? sizeof(struct gfs2_meta_header) : 
			sizeof(struct gfs2_rgrp);

		for (; x < sdp->bsize; x++)
			for (y = 0; y < GFS2_NBBY; y++) {
				state = (bh->b_data[x] >> (GFS2_BIT_SIZE * y)) & 0x03;
				if (state == 0x02) /* unallocated metadata state invalid */
					bh->b_data[x] &= ~(0x02 << (GFS2_BIT_SIZE * y));
			}
		brelse(bh);
	}
}/* convert_bitmaps */

/* ------------------------------------------------------------------------- */
/* superblock_cvt - Convert gfs1 superblock and the existing rgs to gfs2.    */
/* Returns: 0 on success, -1 on failure                                      */
/* ------------------------------------------------------------------------- */
static int superblock_cvt(int disk_fd, const struct gfs_sbd *sb1,
						  struct gfs2_sbd *sb2, uint64_t inode_count)
{
	int x;
	struct gfs_rgrpd *rgd;
	struct rgrp_list *rgd2;
	osi_list_t *tmp;

	/* --------------------------------- */
	/* convert the incore sb structure   */
	/* --------------------------------- */
	memset(sb2, 0, sizeof(sb2));
	sb2->bsize = sb1->sd_sb.sb_bsize; /* block size */
	sb2->journals = sb1->sd_journals; /* number of journals */
	sb2->jsize = GFS2_DEFAULT_JSIZE;
	sb2->rgsize = GFS2_DEFAULT_RGSIZE;
	sb2->utsize = GFS2_DEFAULT_UTSIZE;
	sb2->qcsize = GFS2_DEFAULT_QCSIZE;
	sb2->time = time(NULL);
	sb2->device_fd = disk_fd;
	sb2->blks_total = 0;   /* total blocks         - total them up later */
	sb2->blks_alloced = 0; /* blocks allocated     - total them up later */
	sb2->dinodes_alloced = 0; /* dinodes allocated - total them up later */
	osi_list_init(&sb2->rglist);
	osi_list_init(&sb2->buf_list);
	for (x = 0; x < BUF_HASH_SIZE; x++)
		osi_list_init(&sb2->buf_hash[x]);
	compute_constants(sb2);
	sb2->next_inum = inode_count; /* next inode number available for use */

	/* --------------------------------- */
	/* convert the ondisk sb structure   */
	/* --------------------------------- */
	sb2->sd_sb.sb_header.mh_magic = GFS2_MAGIC;
	sb2->sd_sb.sb_fs_format = GFS2_FORMAT_FS;
	sb2->sd_sb.sb_header.mh_type = GFS2_METATYPE_SB;
	sb2->sd_sb.sb_header.mh_format = GFS2_FORMAT_SB;
	sb2->sd_sb.sb_multihost_format = GFS2_FORMAT_MULTI;
	sb2->sd_sb.sb_bsize = sb1->sd_sb.sb_bsize;
	sb2->sd_sb.sb_bsize_shift = sb1->sd_sb.sb_bsize_shift;
	strcpy(sb2->sd_sb.sb_lockproto, sb1->sd_sb.sb_lockproto);
	strcpy(sb2->sd_sb.sb_locktable, sb1->sd_sb.sb_locktable);

	/* --------------------------------- */
	/* Now convert its rgs into gfs2 rgs */
	/* --------------------------------- */
	for (tmp = (osi_list_t *)sb1->sd_rglist.next;
		 tmp != (osi_list_t *)&sb1->sd_rglist; tmp = tmp->next) {
		rgd = osi_list_entry(tmp, struct gfs_rgrpd, rd_list);
		/* convert the gfs1 rgrp into a new gfs2 rgrp */
		rgd2 = malloc(sizeof(struct rgrp_list));
		if (!rgd2) {
			log_crit("Error: unable to allocate memory for rg conversion.\n");
			return -1;
		}
		memset(rgd2, 0, sizeof(struct rgrp_list));

		rgd2->length = rgd->rd_ri.ri_data;
		sb2->blks_total += rgd->rd_ri.ri_data;
		sb2->blks_alloced += (rgd->rd_ri.ri_data - rgd->rd_rg.rg_free);
		sb2->dinodes_alloced += rgd->rd_rg.rg_useddi;

		rgd2->rg.rg_header.mh_magic = GFS_MAGIC;
		rgd2->rg.rg_header.mh_type = GFS_METATYPE_RG;
		rgd2->rg.rg_header.mh_format = GFS_FORMAT_RG;
		rgd2->rg.rg_flags = rgd->rd_rg.rg_flags;
		rgd2->rg.rg_free = rgd->rd_rg.rg_free;
		rgd2->rg.rg_dinodes = rgd->rd_rg.rg_useddi;

		rgd2->ri.ri_addr = rgd->rd_ri.ri_addr;
		rgd2->ri.ri_length = rgd->rd_ri.ri_length;
		rgd2->ri.ri_data0 = rgd->rd_ri.ri_data1;
		rgd2->ri.ri_data = rgd->rd_ri.ri_data;
		rgd2->ri.ri_bitbytes = rgd->rd_ri.ri_bitbytes;
		/* Add the new gfs2 rg to our list: We'll output the index later. */
		osi_list_add_prev((osi_list_t *)&rgd2->list,
						  (osi_list_t *)&sb2->rglist);
		for (x = 0; x < rgd->rd_ri.ri_length; x++)
			convert_bitmaps(disk_fd, sb2, rgd2);
	}
	return 0;
}/* superblock_cvt */

/* ------------------------------------------------------------------------- */
/* inode_renumber - renumber the inodes                                      */
/*                                                                           */
/* In gfs1, the inode number WAS the inode address.  In gfs2, the inodes are */
/* numbered sequentially.                                                    */
/*                                                                           */
/* Returns: 0 on success, -1 on failure                                      */
/* ------------------------------------------------------------------------- */
int inode_renumber(int disk_fd, struct gfs_sbd *sbp,
				   struct gfs2_sbd *sb2, uint64_t *inode_count)
{
	struct gfs_rgrpd *rgd;
	osi_list_t *tmp;
	uint64_t block;
	osi_buf_t *bh;
	struct gfs_inode *inode;
	int first;
	struct inode_block *fixdir;

	printf("Converting inodes.\n");
	*inode_count = 1; /* starting inode number */
	gettimeofday(&tv, NULL);
	seconds = tv.tv_sec;

	/* ---------------------------------------------------------------- */
	/* Traverse the resource groups to figure out where the inodes are. */
	/* ---------------------------------------------------------------- */
	for (tmp = (osi_list_t *)sbp->sd_rglist.next;
		 tmp != (osi_list_t *)&sbp->sd_rglist; tmp = tmp->next) {
		rgd = osi_list_entry(tmp, struct gfs_rgrpd, rd_list);
		first = 1;
		if (fs_rgrp_read(disk_fd, rgd, FALSE)) {
			log_crit("Unable to read rgrp.\n");
			return -1;
		}
		while (1) {    /* for all inodes in the resource group */
			gettimeofday(&tv, NULL);
			/* Put out a warm, fuzzy message every second so the customer */
			/* doesn't think we hung.  (This may take a long time).       */
			if (tv.tv_sec - seconds) {
				seconds = tv.tv_sec;
				printf("\r%" PRIu64" inodes converted.", *inode_count);
				fflush(stdout);
			}
			/* Get the next disk inode.  Break out if we reach the end. */
			if (next_rg_metatype(disk_fd, rgd, &block, GFS_METATYPE_DI, first))
				break;
			/* If this is the root inode block, remember it for later: */
			if (block == sbp->sd_sb.sb_root_di.no_addr) {
				sb2->sd_sb.sb_root_dir.no_addr = block;
				sb2->sd_sb.sb_root_dir.no_formal_ino = *inode_count;
			}
			if (get_and_read_buf(disk_fd, sbp->sd_sb.sb_bsize, block, &bh, 0)){
				log_crit("Unable to retrieve block %" PRIu64 "\n", block);
				return -1;
			}
			if (copyin_inode(sbp, bh, &inode)) {
				log_crit("Error fetching inode.\n");
				relse_buf(bh);
				return -1;
			}
			/* Fix the inode number: */
			inode->i_di.di_num.no_formal_ino = *inode_count;

			/* Fix the inode type: gfs1 uses di_type, gfs2 uses di_mode. */
			switch (inode->i_di.di_type) {
			case GFS_FILE_DIR:           /* directory        */
				inode->i_di.di_mode |= S_IFDIR;
				/* Add this directory to the list of dirs to fix later. */
				fixdir = malloc(sizeof(struct inode_block));
				if (!fixdir) {
					fprintf(stderr,"Error: out of memory.\n");
					return -1;
				}
				memset(fixdir, 0, sizeof(struct inode_block));
				fixdir->di_addr = inode->i_di.di_num.no_addr;
				osi_list_add_prev((osi_list_t *)&fixdir->list,
								  (osi_list_t *)&dirs_to_fix);
				break;
			case GFS_FILE_REG:           /* regular file     */
				inode->i_di.di_mode |= S_IFREG;
				break;
			case GFS_FILE_LNK:           /* symlink          */
				inode->i_di.di_mode |= S_IFLNK;
				break;
			case GFS_FILE_BLK:           /* block device     */
				inode->i_di.di_mode |= S_IFBLK;
				break;
			case GFS_FILE_CHR:           /* character device */
				inode->i_di.di_mode |= S_IFCHR;
				break;
			case GFS_FILE_FIFO:          /* fifo / pipe      */
				inode->i_di.di_mode |= S_IFIFO;
				break;
			case GFS_FILE_SOCK:          /* socket           */
				inode->i_di.di_mode |= S_IFSOCK;
				break;
			}
			
			/* ----------------------------------------------------------- */
			/* gfs2 inodes are slightly different from gfs1 inodes in that */
			/* di_goal_meta has shifted locations and di_goal_data has     */
			/* changed from 32-bits to 64-bits.  The following code        */
			/* adjusts for the shift.                                      */
			/* ----------------------------------------------------------- */
			inode->i_di.di_rgrp = inode->i_di.di_goal_rgrp;
			inode->i_di.di_goal_rgrp = 0; /* make sure the upper 32b are 0 */
			inode->i_di.di_goal_rgrp = inode->i_di.di_goal_dblk;
			inode->i_di.di_goal_mblk = 0;

			*inode_count = *inode_count + 1; /* update inode count */
			gfs_dinode_out(&inode->i_di, BH_DATA(bh));
			if (write_buf(disk_fd, bh, 0) < 0) { /* write out inode */
				log_crit("Error updating inode.\n");
				relse_buf(bh);
				return -1;
			}
			relse_buf(bh);
			first = 0;
		} /* while 1 */
		fs_rgrp_relse(rgd);
	} /* for all rgs */
	fsync(disk_fd);
	return 0;
}/* inode_renumber */

/* ------------------------------------------------------------------------- */
/* fetch_inum - fetch an inum entry from disk, given its block               */
/* ------------------------------------------------------------------------- */
int fetch_inum(int disk_fd, struct gfs_sbd *sbp, uint64_t iblock,
			   struct gfs_inum *inum)
{
	osi_buf_t *bh_fix;
	int error;
	struct gfs_inode *fix_inode;

	error = 0;
	if (get_and_read_buf(disk_fd, sbp->sd_sb.sb_bsize,
						 iblock, &bh_fix, 0)) {
		log_crit("Unable to retrieve block %" PRIu64 "\n", iblock);
		error = -1;
	}
	else if (copyin_inode(sbp, bh_fix, &fix_inode)) {
		log_crit("Error fetching inode.\n");
		error = -1;
	}
	else {
		inum->no_formal_ino = fix_inode->i_di.di_num.no_formal_ino;
		inum->no_addr = fix_inode->i_di.di_num.no_addr;
	}
	relse_buf(bh_fix);
	return error;
}/* fetch_inum */

/* ------------------------------------------------------------------------- */
/* process_dirent_info - fix one dirent (directory entry) buffer             */
/*                                                                           */
/* We changed inode numbers, so we must update that number into the          */
/* directory entries themselves.                                             */
/*                                                                           */
/* Returns: 0 on success, -1 on failure                                      */
/* ------------------------------------------------------------------------- */
int process_dirent_info(int disk_fd, struct gfs_sbd *sbp, osi_buf_t *bh,
						int dir_entries)
{
	int error;
	struct gfs_dirent *dent;
	int de; /* directory entry index */

	error = dirent_first(bh, &dent);
	if (error != IS_LEAF && error != IS_DINODE) {
		printf("Error retrieving directory.\n");
		return -1;
	}
	/* go through every dirent in the buffer and process it */
	for (de = 0; de < dir_entries; de++) {
		struct gfs_inum inum;
		
		/* Do more warm fuzzy stuff for the customer. */
		dirents_fixed++;
		if (tv.tv_sec - seconds) {
			seconds = tv.tv_sec;
			printf("\r%" PRIu64 " directories, %" PRIu64 " dirents fixed.",
				   dirs_fixed, dirents_fixed);
			fflush(stdout);
		}
		/* fix the dirent's inode number based on the inode */
		gfs_inum_in(&inum, (char *)&dent->de_inum);
		error = fetch_inum(disk_fd, sbp, inum.no_addr, &inum);
		if (error) {
			printf("Error retrieving inode %" PRIx64 "\n", inum.no_addr);
			break;
		}
		gfs_inum_out(&inum, (char *)&dent->de_inum);
		/* Fix the dirent's filename hash: They are the same as gfs1 */
		/* dent->de_hash = cpu_to_be32(gfs2_disk_hash((char *)(dent + 1), */
		/*                             be16_to_cpu(dent->de_name_len))); */
		/* Fix the dirent's file type.  Gfs1 used home-grown values.  */
		/* Gfs2 uses standard values from include/linux/fs.h          */
		switch be16_to_cpu(dent->de_type) {
		case GFS_FILE_NON:
			dent->de_type = cpu_to_be16(DT_UNKNOWN);
			break;
		case GFS_FILE_REG:    /* regular file */
			dent->de_type = cpu_to_be16(DT_REG);
			break;
		case GFS_FILE_DIR:    /* directory */
			dent->de_type = cpu_to_be16(DT_DIR);
			break;
		case GFS_FILE_LNK:    /* link */
			dent->de_type = cpu_to_be16(DT_LNK);
			break;
		case GFS_FILE_BLK:    /* block device node */
			dent->de_type = cpu_to_be16(DT_BLK);
			break;
		case GFS_FILE_CHR:    /* character device node */
			dent->de_type = cpu_to_be16(DT_CHR);
			break;
		case GFS_FILE_FIFO:   /* fifo/pipe */
			dent->de_type = cpu_to_be16(DT_FIFO);
			break;
		case GFS_FILE_SOCK:   /* socket */
			dent->de_type = cpu_to_be16(DT_SOCK);
			break;
		}

		error = dirent_next(bh, &dent);
		if (error)
			break;
	}
	return 0;
}/* process_dirent_info */

/* ------------------------------------------------------------------------- */
/* fix_one_directory_linear - fix one directory's inode numbers.             */
/*                                                                           */
/* This is for linear (stuffed) directories (data is in the inode itself).   */
/*                                                                           */
/* Returns: 0 on success, -1 on failure                                      */
/* ------------------------------------------------------------------------- */
int fix_one_directory_linear(int disk_fd, struct gfs_sbd *sbp,
							 struct gfs_inode *dip, osi_buf_t *bh)
{
	int error;

	error = process_dirent_info(disk_fd, sbp, bh, dip->i_di.di_entries);
	if (write_buf(disk_fd, bh, 0) < 0) {
		log_crit("Error updating linear directory.\n");
		return -1;
	}
	return 0;
}/* fix_one_directory_linear */

/* ------------------------------------------------------------------------- */
/* fix_one_directory_exhash - fix one directory's inode numbers.             */
/*                                                                           */
/* This is for exhash directories, where the inode has a list of "leaf"      */
/* blocks, each of which is a buffer full of dirents that must be processed. */
/*                                                                           */
/* Returns: 0 on success, -1 on failure                                      */
/* ------------------------------------------------------------------------- */
int fix_one_directory_exhash(int disk_fd, struct gfs_sbd *sbp,
							 struct gfs_inode *dip)
{
	osi_buf_t *bh_leaf;
	int error;
	uint64_t leaf_block, prev_leaf_block;
	uint32_t leaf_num;
	
	prev_leaf_block = 0;
	/* for all the leafs, get the leaf block and process the dirents inside */
	for (leaf_num = 0; ; leaf_num++) {
		uint64 buf;
		struct gfs_leaf leaf;

		error = readi(disk_fd, dip, (char *)&buf,
					  leaf_num * sizeof(uint64), sizeof(uint64));
		if (!error) /* end of file */
			return 0; /* success */
		else if (error != sizeof(uint64)) {
			log_crit("fix_one_directory_exhash: error reading directory.\n");
			return -1;
		}
		else {
			leaf_block = gfs64_to_cpu(buf);
			error = 0;
		}
		/* leaf blocks may be repeated, so skip the duplicates: */
		if (leaf_block == prev_leaf_block) /* same block? */
			continue;                      /* already converted */
		prev_leaf_block = leaf_block;
		/* read the leaf buffer in */
		error = get_leaf(disk_fd, dip, leaf_block, &bh_leaf);
		if (error) {
			printf("Error reading leaf %" PRIx64 "\n", leaf_block);
			break;
		}
		gfs_leaf_in(&leaf, (char *)BH_DATA(bh_leaf)); /* buffer to structure */
		error = process_dirent_info(disk_fd, sbp, bh_leaf, leaf.lf_entries);
		if (bh_leaf) {
			if (write_buf(disk_fd, bh_leaf, 0) < 0) {
				log_crit("Error updating directory.\n");
				relse_buf(bh_leaf);
				return -1;
			}
			relse_buf(bh_leaf);
		}
	} /* for leaf_num */
	return 0;
}/* fix_one_directory_exhash */

/* ------------------------------------------------------------------------- */
/* fix_directory_info - sync new inode numbers with directory info           */
/* Returns: 0 on success, -1 on failure                                      */
/* ------------------------------------------------------------------------- */
int fix_directory_info(int disk_fd, struct gfs_sbd *sbp,
					   osi_list_t *dirs_to_fix)
{
	osi_list_t *tmp, *fix;
	struct inode_block *dir_iblk;
	uint64_t offset, dirblock;
	struct gfs_inode *dip;
	osi_buf_t *bh_dir;

	dirs_fixed = 0;
	dirents_fixed = 0;
	gettimeofday(&tv, NULL);
	seconds = tv.tv_sec;
	printf("\nFixing file and directory information.\n");
	offset = 0;
	tmp = NULL;
	/* for every directory in the list */
	for (fix = dirs_to_fix->next; fix != dirs_to_fix; fix = fix->next) {
		if (tmp) {
			osi_list_del(tmp);
			free(tmp);
		}
		tmp = fix; /* remember the addr to free next time */
		dirs_fixed++;
		/* figure out the directory inode block and read it in */
		dir_iblk = (struct inode_block *)fix;
		dirblock = dir_iblk->di_addr; /* addr of dir inode */
		/* read in the directory inode */
		if (get_and_read_buf(disk_fd, sbp->sd_sb.sb_bsize, dirblock,
							 &bh_dir, 0)){
			log_crit("Unable to retrieve block %" PRIu64 " (%" PRIx64
					 ")\n", dirblock, dirblock);
			return -1;
		}
		if (copyin_inode(sbp, bh_dir, &dip)) {
			log_crit("Error fetching inode.\n");
			relse_buf(bh_dir);
			return -1;
		}
		/* fix the directory: either exhash (leaves) or linear (stuffed) */
		if (dip->i_di.di_flags & GFS_DIF_EXHASH) {
			if (fix_one_directory_exhash(disk_fd, sbp, dip)) {
				log_crit("Error fixing exhash directory.\n");
				relse_buf(bh_dir);
				return -1;
			}
		}
		else {
			if (fix_one_directory_linear(disk_fd, sbp, dip, bh_dir)) {
				log_crit("Error fixing linear directory.\n");
				relse_buf(bh_dir);
				return -1;
			}
		}
		relse_buf(bh_dir);
	}
	/* Free the last entry in memory: */
	if (tmp) {
		osi_list_del(tmp);
		free(tmp);
	}
	return 0;
}/* fix_directory_info */

/* ------------------------------------------------------------------------- */
/* fill_super_block                                                          */
/* Returns: 0 on success, -1 on failure                                      */
/* ------------------------------------------------------------------------- */
static int fill_super_block(int disk_fd, struct gfs_sbd *sdp)
{
	struct gfs_inode *ip = NULL;

	log_info("Reading old filesystem information.\n");
	/* get ri inode */
	if(load_inode(disk_fd, sdp, sdp->sd_sb.sb_rindex_di.no_addr, &ip))
		return -1;
	sdp->sd_riinode = ip;

	/* get ji inode */
	if(load_inode(disk_fd, sdp, sdp->sd_sb.sb_jindex_di.no_addr, &ip)) {
		stack;
		return -1;
	}
	sdp->sd_jiinode = ip;
	/* get root dinode */
	if(!load_inode(disk_fd, sdp, sdp->sd_sb.sb_root_di.no_addr, &ip)) {
		if(!check_inode(ip)) {
			sdp->sd_rooti = ip;
		}
		else {
			free(ip);
		}
	} else {
		log_warn("Unable to load root inode\n");
	}
	/* read in the journal index data */
	if (ji_update(disk_fd, sdp)){
		log_err("Unable to read in journal index inode.\n");
		return -1;
	}
	/* read in the resource group index data */
	if(ri_update(disk_fd, sdp)){
		log_err("Unable to fill in resource group information.\n");
		return -1;
	}
	printf("%d rgs found.\n", sdp->sd_rgcount);
	return 0;
}/* fill_super_block */

/* ------------------------------------------------------------------------- */
/* give_warning - give the all-important warning message.                    */
/* ------------------------------------------------------------------------- */
void give_warning(void)
{
	printf("This program will convert a gfs1 filesystem to a "	\
		   "gfs2 filesystem.\n");
	printf("WARNING: This can't be undone.  It is strongly advised "	\
		   "that you:\n\n");
	printf("   1. Back up your entire filesystem first.\n");
	printf("   2. Run gfs_fsck first to ensure filesystem integrity.\n");
	printf("   3. Make sure the filesystem is NOT mounted from any node.\n");
	printf("   4. Make sure you have the latest software versions.\n");
}/* give_warning */

/* ------------------------------------------------------------------------- */
/* usage - print usage information                                           */
/* ------------------------------------------------------------------------- */
void usage(const char *name)
{
	give_warning();
	printf("\nFormat is:\n");
	printf("%s [--verbose] [-y] /dev/your/device\n\n", name);
}/* usage */

/* ------------------------------------------------------------------------- */
/* process_parameters                                                        */
/* ------------------------------------------------------------------------- */
void process_parameters(int argc, char **argv, struct options *opts)

{
	int i;

	opts->yes = 0;
	opts->no = 0;
	if (argc == 1) {
		usage(argv[0]);
		exit(0);
	}
	memset(device, 0, sizeof(device));
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--verbose"))
			increase_verbosity();
		else if (!strcmp(argv[i], "-y"))
			opts->yes = 1;
		else if (argv[i][0] == '-') {
			usage(argv[0]);
			fprintf(stderr, "Error: parameter %s not understood.\n", argv[i]);
			exit(-1);
		}
		else
			strcpy(device, argv[i]);
	}
	opts->device = device;
} /* process_parameters */

/* ------------------------------------------------------------------------- */
/* rgrp_length - Calculate the length of a resource group                    */
/* @size: The total size of the resource group                               */
/* ------------------------------------------------------------------------- */
uint64_t rgrp_length(uint64_t size, struct gfs_sbd *sdp)
{
	uint64_t bitbytes = RGRP_BITMAP_BLKS(&sdp->sd_sb) + 1;
	uint64_t stuff = RGRP_STUFFED_BLKS(&sdp->sd_sb) + 1;
	uint64_t blocks = 1;

	if (size >= stuff) {
		size -= stuff;
		while (size > bitbytes) {
			blocks++;
			size -= bitbytes;
		}
		if (size)
			blocks++;
	}
	return blocks;
}/* rgrp_length */

/* ------------------------------------------------------------------------- */
/* journ_space_to_rg - convert gfs1 journal space to gfs2 rg space.          */
/*                                                                           */
/* In gfs1, the journals were kept separate from the files and directories.  */
/* They had a dedicated section of the fs carved out for them.               */
/* In gfs2, the journals are just files like any other, (but still hidden).  */
/* Therefore, the old journal space has to be converted to normal resource   */
/* group space.                                                              */
/*                                                                           */
/* Returns: 0 on success, -1 on failure                                      */
/* ------------------------------------------------------------------------- */
int journ_space_to_rg(int disk_fd, struct gfs_sbd *sdp, struct gfs2_sbd *sdp2)
{
	int error = 0;
	int j, x;
	struct gfs_jindex *jndx;
	struct gfs_rgrpd *rgd, *rgdhigh;
	struct rgrp_list *rgd2;
	osi_list_t *tmp;
	struct gfs2_buffer_head *bh;
	struct gfs2_meta_header mh;

	mh.mh_magic = GFS2_MAGIC;
	mh.mh_type = GFS2_METATYPE_RB;
	mh.mh_format = GFS2_FORMAT_RB;
	log_info("Converting journal space to rg space.\n");
	/* Go through each journal, converting them one by one */
	for (j = 0; j < sdp->sd_journals; j++) { /* for each journal */
		uint64_t size;

		log_info("Processing journal %d.\n", j + 1);
		jndx = &sdp->sd_jindex[j];
		/* go through all rg index entries, keeping track of the highest */
		/* that's still in the first subdevice.                          */
		/* Note: we really should go through all of the rgindex because  */
		/* we might have had rg's added by gfs_grow, and journals added  */
		/* by jadd.  gfs_grow adds rgs out of order, so we can't count   */
		/* on them being in ascending order.                             */
		rgdhigh = NULL;
		for (tmp = (osi_list_t *)sdp->sd_rglist.next;
			 tmp != (osi_list_t *)&sdp->sd_rglist; tmp = tmp->next) {
			rgd = osi_list_entry(tmp, struct gfs_rgrpd, rd_list);
			if (rgd->rd_ri.ri_addr < jndx->ji_addr &&
				((rgdhigh == NULL) ||
				 (rgd->rd_ri.ri_addr > rgdhigh->rd_ri.ri_addr)))
				rgdhigh = rgd;
		} /* for each rg */
		log_info("Addr %" PRIx64 " comes after rg at addr %" PRIx64 "\n",
				 jndx->ji_addr, rgdhigh->rd_ri.ri_addr);
		if (!rgdhigh) { /* if we somehow didn't find one. */
			log_crit("Error: No suitable rg found for journal.\n");
			return -1;
		}
		/* Allocate a new rgd entry which includes rg and ri. */
		rgd = malloc(sizeof(struct gfs_rgrpd));
		if (!rgd) {
			log_crit("Error: out of memory creating new rg entry.\n");
			free(rgd);
			return -1;
		}
		memset(rgd, 0, sizeof(struct gfs_rgrpd));
		rgd->rd_sbd = sdp;
		size = jndx->ji_nsegment * sdp->sd_sb.sb_seg_size;

		rgd->rd_ri.ri_addr = jndx->ji_addr; /* new rg addr becomes ji addr */
		rgd->rd_ri.ri_length = rgrp_length(size, sdp); /* aka bitblocks */
		rgd->rd_ri.ri_data1 = jndx->ji_addr + rgd->rd_ri.ri_length;
		rgd->rd_ri.ri_data = size - rgd->rd_ri.ri_length;

		sdp2->blks_total += rgd->rd_ri.ri_data; /* For statfs file update */

		/* Round down to nearest multiple of GFS_NBBY */
		while (rgd->rd_ri.ri_data & 0x03)
			rgd->rd_ri.ri_data--;
		rgd->rd_ri.ri_bitbytes = rgd->rd_ri.ri_data / GFS_NBBY;

		rgd->rd_rg.rg_header.mh_magic = GFS_MAGIC;
		rgd->rd_rg.rg_header.mh_type = GFS_METATYPE_RG;
		rgd->rd_rg.rg_header.mh_format = GFS_FORMAT_RG;
		rgd->rd_rg.rg_free = rgd->rd_ri.ri_data;

		/* convert the gfs1 rgrp into a new gfs2 rgrp */
		rgd2 = malloc(sizeof(struct rgrp_list));
		if (!rgd2) {
			log_crit("Error: unable to allocate memory for rg conversion.\n");
			return -1;
		}
		memset(rgd2, 0, sizeof(struct rgrp_list));
		rgd2->rg.rg_header.mh_magic = GFS_MAGIC;
		rgd2->rg.rg_header.mh_type = GFS_METATYPE_RG;
		rgd2->rg.rg_header.mh_format = GFS_FORMAT_RG;
		rgd2->rg.rg_flags = 0;
		rgd2->rg.rg_free = rgd->rd_ri.ri_data;
		rgd2->rg.rg_dinodes = 0;

		rgd2->ri.ri_addr = rgd->rd_ri.ri_addr;
		rgd2->ri.ri_length = rgd->rd_ri.ri_length;
		rgd2->ri.ri_data0 = rgd->rd_ri.ri_data1;
		rgd2->ri.ri_data = rgd->rd_ri.ri_data;
		rgd2->ri.ri_bitbytes = rgd->rd_ri.ri_bitbytes;
		for (x = 0; x < rgd->rd_ri.ri_length; x++) {
			bh = bread(sdp2, rgd->rd_ri.ri_addr + x);
			convert_bitmaps(disk_fd, sdp2, rgd2);
			if (x)
				gfs2_meta_header_out(&mh, bh->b_data);
			else
				gfs2_rgrp_out(&rgd2->rg, bh->b_data);
			brelse(bh);
		}
		/* Add the new rg to our list: We'll output the rg index later. */
		osi_list_add_prev((osi_list_t *)&rgd->rd_list,
						  (osi_list_t *)&sdp->sd_rglist);
		/* Add the new gfs2 rg to our list: We'll output the rg index later. */
		osi_list_add_prev((osi_list_t *)&rgd2->list,
						  (osi_list_t *)&sdp2->rglist);
	} /* for each journal */
	return error;
}/* journ_space_to_rg */

/* ------------------------------------------------------------------------- */
/* update_inode_file - update the inode file with the new next_inum          */
/* ------------------------------------------------------------------------- */
void update_inode_file(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *ip = sdp->inum_inode;
	uint64_t buf;
	int count;
	
	buf = cpu_to_be64(sdp->next_inum);
	count = gfs2_writei(ip, &buf, 0, sizeof(uint64_t));
	if (count != sizeof(uint64_t))
		die("update_inode_file\n");
	
	if (sdp->debug)
		printf("\nNext Inum: %"PRIu64"\n", sdp->next_inum);
}/* update_inode_file */

/* ------------------------------------------------------------------------- */
/* write_statfs_file - write the statfs file                                 */
/* ------------------------------------------------------------------------- */
void write_statfs_file(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *ip = sdp->statfs_inode;
	struct gfs2_statfs_change sc;
	char buf[sizeof(struct gfs2_statfs_change)];
	int count;
	
	sc.sc_total = sdp->blks_total;
	sc.sc_free = sdp->blks_total - sdp->blks_alloced;
	sc.sc_dinodes = sdp->dinodes_alloced;

	gfs2_statfs_change_out(&sc, buf);
	count = gfs2_writei(ip, buf, 0, sizeof(struct gfs2_statfs_change));
	if (count != sizeof(struct gfs2_statfs_change))
		die("do_init (2)\n");
	
	if (sdp->debug) {
		printf("\nStatfs:\n");
		gfs2_statfs_change_print(&sc);
	}
}/* write_statfs_file */

/* ------------------------------------------------------------------------- */
/* main - mainline code                                                      */
/* ------------------------------------------------------------------------- */
int main(int argc, char **argv)
{	
	int disk_fd;
	int error;
	struct gfs2_buffer_head *bh;
	struct options opts;

	printf("gfs2_convert version %s (built %s %s)\n", GFS2_RELEASE_NAME,
		   __DATE__, __TIME__);
	printf("%s\n\n", REDHAT_COPYRIGHT);
	process_parameters(argc, argv, &opts);
	memset(&sb, 0, sizeof(sb));
	if ((disk_fd = open(device, O_RDWR)) < 0){
		perror(device);
		exit(-1);
	}
	osi_list_init((osi_list_t *)&dirs_to_fix);
	/* ---------------------------------------------- */
	/* Initialize lists and read in the superblock.   */
	/* ---------------------------------------------- */
	error = read_super_block(disk_fd, &sb);
	if (error)
		fprintf(stderr, "%s: Unable to read superblock.\n", device);
	/* ---------------------------------------------- */
	/* Make sure we're really gfs1                    */
	/* ---------------------------------------------- */
	if (sb.sd_sb.sb_fs_format != GFS_FORMAT_FS ||
		sb.sd_sb.sb_header.mh_type != GFS_METATYPE_SB ||
		sb.sd_sb.sb_header.mh_format != GFS_FORMAT_SB ||
		sb.sd_sb.sb_multihost_format != GFS_FORMAT_MULTI) {
		fprintf(stderr, "Error: %s does not look like a gfs1 filesystem.\n",
				device);
		close(disk_fd);
		exit(-1);
	}
	/* ---------------------------------------------- */
	/* Make them seal their fate.                     */
	/* ---------------------------------------------- */
	give_warning();
	if (!query(&opts, "Convert %s from GFS1 to GFS2? (y/n)", device)) {
		fprintf(stderr, "%s not converted.\n", device);
		close(disk_fd);
		exit(0);
	}
	printf("Initializing...");
	if (!error) {
		error = fill_super_block(disk_fd, &sb);
		if (error)
			fprintf(stderr, "%s: Unable to fill superblock.\n", device);
	}
	/* ---------------------------------------------- */
	/* Renumber the inodes consecutively.             */
	/* ---------------------------------------------- */
	if (!error) {
		error = inode_renumber(disk_fd, &sb, &sb2, &inode_count);
		if (error)
			fprintf(stderr, "\n%s: Error renumbering inodes.\n", device);
	}
	/* ---------------------------------------------- */
	/* Fix the directories to match the new numbers.  */
	/* ---------------------------------------------- */
	if (!error) {
		error = fix_directory_info(disk_fd, &sb, (osi_list_t *)&dirs_to_fix);
		printf("\r%" PRIu64 " directories, %" PRIu64 " dirents fixed.",
			   dirs_fixed, dirents_fixed);
		fflush(stdout);
		if (error)
			fprintf(stderr, "\n%s: Error fixing directories.\n", device);
	}
	/* ---------------------------------------------- */
	/* Convert incore gfs1 sb to gfs2 sb              */
	/* ---------------------------------------------- */
	if (!error) {
		printf("\nConverting superblock.\n");
		error = superblock_cvt(disk_fd, &sb, &sb2, inode_count);
		if (error)
			fprintf(stderr, "%s: Unable to convert superblock.\n", device);
		bsync(&sb2); /* write the buffers to disk */
	}
	/* ---------------------------------------------- */
	/* Convert journal space to rg space              */
	/* ---------------------------------------------- */
	if (!error) {
		printf("Converting journals.\n");
		error = journ_space_to_rg(disk_fd, &sb, &sb2);
		if (error)
			fprintf(stderr, "%s: Error converting journal space.\n", device);
		bsync(&sb2); /* write the buffers to disk */
	}
	/* ---------------------------------------------- */
	/* Create our system files and directories.       */
	/* ---------------------------------------------- */
	if (!error) {
		printf("Building system structures.\n");
		/* Build the master subdirectory. */
		build_master(&sb2); /* Does not do inode_put */
		sb2.sd_sb.sb_master_dir = sb2.master_dir->i_di.di_num;
		/* Build empty journal index file. */
		build_jindex(&sb2);
		/* Build out per-node directories */
		build_per_node(&sb2);
		/* Create the empty inode number file */
		build_inum(&sb2); /* Does not do inode_put */
		/* Create the statfs file */
		build_statfs(&sb2); /* Does not do inode_put */
		/* Create the resource group index file */
		build_rindex(&sb2);
		/* Create the quota file */
		build_quota(&sb2);

		update_inode_file(&sb2);
		write_statfs_file(&sb2);

		inode_put(sb2.master_dir);
		inode_put(sb2.inum_inode);
		inode_put(sb2.statfs_inode);

		bh = bread(&sb2, sb2.sb_addr);
		gfs2_sb_out(&sb2.sd_sb, bh->b_data);
		brelse(bh);
		bsync(&sb2); /* write the buffers to disk */
		error = fsync(disk_fd);
		if (error)
			die("can't fsync device (%d): %s\n",
				error, strerror(errno));
		else
			printf("%s: filesystem converted successfully to gfs2.\n", device);
	}
	close(disk_fd);
	exit(0);
}
