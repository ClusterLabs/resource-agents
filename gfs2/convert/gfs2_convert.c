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
#include <dirent.h>

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
void convert_bitmaps(struct gfs2_sbd *sdp, struct rgrp_list *rgd2,
					 int read_disk)
{
	uint32_t blk;
	int x, y;
	struct gfs2_rindex *ri;
	unsigned char state;

	ri = &rgd2->ri;
	gfs2_compute_bitstructs(sdp, rgd2); /* mallocs bh as array */
	for (blk = 0; blk < ri->ri_length; blk++) {
		rgd2->bh[blk] = bget_generic(sdp, ri->ri_addr + blk, read_disk,
									 read_disk);
		x = (blk) ? sizeof(struct gfs2_meta_header) : sizeof(struct gfs2_rgrp);

		for (; x < sdp->bsize; x++)
			for (y = 0; y < GFS2_NBBY; y++) {
				state = (rgd2->bh[blk]->b_data[x] >>
						 (GFS2_BIT_SIZE * y)) & 0x03;
				if (state == 0x02) /* unallocated metadata state invalid */
					rgd2->bh[blk]->b_data[x] &= ~(0x02 << (GFS2_BIT_SIZE * y));
			}
		brelse(rgd2->bh[blk], updated);
	}
}/* convert_bitmaps */

/* ------------------------------------------------------------------------- */
/* superblock_cvt - Convert gfs1 superblock and the existing rgs to gfs2.    */
/* Returns: 0 on success, -1 on failure                                      */
/* ------------------------------------------------------------------------- */
static int superblock_cvt(int disk_fd, const struct gfs_sbd *sb1,
						  struct gfs2_sbd *sb2)
{
	int x;
	struct gfs_rgrpd *rgd;
	struct rgrp_list *rgd2;
	osi_list_t *tmp;
	struct gfs2_buffer_head *bh;

	/* --------------------------------- */
	/* convert the incore sb structure   */
	/* --------------------------------- */
	memset(sb2, 0, sizeof(sb2));
	sb2->bsize = sb1->sd_sb.sb_bsize; /* block size */
	sb2->md.journals = sb1->sd_journals; /* number of journals */
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
		rgd2->rg.rg_header.mh_magic = GFS_MAGIC;
		rgd2->rg.rg_header.mh_type = GFS_METATYPE_RG;
		rgd2->rg.rg_header.mh_format = GFS_FORMAT_RG;
		rgd2->rg.rg_flags = rgd->rd_rg.rg_flags;
		rgd2->rg.rg_free = rgd->rd_rg.rg_free + rgd->rd_rg.rg_freemeta;
		rgd2->rg.rg_dinodes = rgd->rd_rg.rg_useddi;

		sb2->blks_total += rgd->rd_ri.ri_data;
		sb2->blks_alloced += (rgd->rd_ri.ri_data - rgd2->rg.rg_free);
		sb2->dinodes_alloced += rgd->rd_rg.rg_useddi;

		rgd2->ri.ri_addr = rgd->rd_ri.ri_addr;
		rgd2->ri.ri_length = rgd->rd_ri.ri_length;
		rgd2->ri.ri_data0 = rgd->rd_ri.ri_data1;
		rgd2->ri.ri_data = rgd->rd_ri.ri_data;
		rgd2->ri.ri_bitbytes = rgd->rd_ri.ri_bitbytes;
		/* commit the changes to a gfs2 buffer */
		bh = bread(sb2, rgd2->ri.ri_addr); /* get a gfs2 buffer for the rg */
		gfs2_rgrp_out(&rgd2->rg, bh->b_data);
		brelse(bh, updated); /* release the buffer */
		/* Add the new gfs2 rg to our list: We'll output the index later. */
		osi_list_add_prev((osi_list_t *)&rgd2->list,
						  (osi_list_t *)&sb2->rglist);
		convert_bitmaps(sb2, rgd2, TRUE);
	}
	return 0;
}/* superblock_cvt */

/* ------------------------------------------------------------------------- */
/* adjust_inode - change an inode from gfs1 to gfs2                          */
/*                                                                           */
/* Returns: 0 on success, -1 on failure                                      */
/* ------------------------------------------------------------------------- */
int adjust_inode(struct gfs2_sbd *sbp, struct gfs2_buffer_head *bh)
{
	struct gfs2_inode *inode;
	struct inode_block *fixdir;

	inode = inode_get(sbp, bh);
	/* Fix the inode number: */
	inode->i_di.di_num.no_formal_ino = sbp->md.next_inum;           ;
	
	/* Fix the inode type: gfs1 uses di_type, gfs2 uses di_mode. */
	switch (inode->i_di.__pad1) { /* formerly di_type */
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
	inode->i_di.di_goal_meta = inode->i_di.di_goal_data;
	inode->i_di.di_goal_data = 0; /* make sure the upper 32b are 0 */
	inode->i_di.di_goal_data = inode->i_di.__pad[0];
	inode->i_di.__pad[1] = 0;
	
	gfs2_dinode_out(&inode->i_di, bh->b_data);
	sbp->md.next_inum++; /* update inode count */
	return 0;
} /* adjust_inode */

/* ------------------------------------------------------------------------- */
/* inode_renumber - renumber the inodes                                      */
/*                                                                           */
/* In gfs1, the inode number WAS the inode address.  In gfs2, the inodes are */
/* numbered sequentially.                                                    */
/*                                                                           */
/* Returns: 0 on success, -1 on failure                                      */
/* ------------------------------------------------------------------------- */
int inode_renumber(struct gfs2_sbd *sbp, uint64_t root_inode_addr)
{
	struct rgrp_list *rgd;
	osi_list_t *tmp;
	uint64_t block;
	struct gfs2_buffer_head *bh;
	int first;
	int error = 0;

	printf("Converting inodes.\n");
	sbp->md.next_inum = 1; /* starting inode numbering */
	gettimeofday(&tv, NULL);
	seconds = tv.tv_sec;

	/* ---------------------------------------------------------------- */
	/* Traverse the resource groups to figure out where the inodes are. */
	/* ---------------------------------------------------------------- */
	osi_list_foreach(tmp, &sbp->rglist) {
		rgd = osi_list_entry(tmp, struct rgrp_list, list);
		first = 1;
		if (gfs2_rgrp_read(sbp, rgd)) {
			log_crit("Unable to read rgrp.\n");
			return -1;
		}
		while (1) {    /* for all inodes in the resource group */
			gettimeofday(&tv, NULL);
			/* Put out a warm, fuzzy message every second so the customer */
			/* doesn't think we hung.  (This may take a long time).       */
			if (tv.tv_sec - seconds) {
				seconds = tv.tv_sec;
				printf("\r%" PRIu64" inodes converted.", sbp->md.next_inum);
				fflush(stdout);
			}
			/* Get the next metadata block.  Break out if we reach the end. */
            /* We have to check all metadata blocks because the bitmap may  */
			/* be "11" (used meta) for both inodes and indirect blocks.     */
			/* We need to process the inodes and change the indirect blocks */
			/* to have a bitmap type of "01" (data).                        */
			if (gfs2_next_rg_metatype(sbp, rgd, &block, 0, first))
				break;
			/* If this is the root inode block, remember it for later: */
			if (block == root_inode_addr) {
				sbp->sd_sb.sb_root_dir.no_addr = block;
				sbp->sd_sb.sb_root_dir.no_formal_ino = sbp->md.next_inum;
			}
			bh = bread(sbp, block);
			if (!gfs2_check_meta(bh, GFS_METATYPE_DI)) /* if it is an dinode */
				error = adjust_inode(sbp, bh);
			else { /* It's metadata, but not an inode, so fix the bitmap. */
				int blk, buf_offset;
				int bitmap_byte; /* byte within the bitmap to fix */
				int byte_bit; /* bit within the byte */

				/* Figure out the absolute bitmap byte we need to fix.   */
				/* ignoring structure offsets and bitmap blocks for now. */
				bitmap_byte = (block - rgd->ri.ri_data0) / GFS2_NBBY;
				byte_bit = (block - rgd->ri.ri_data0) % GFS2_NBBY;
				/* Now figure out which bitmap block the byte is on */
				for (blk = 0; blk < rgd->ri.ri_length; blk++) {
                    /* figure out offset of first bitmap byte for this map: */
					buf_offset = (blk) ? sizeof(struct gfs2_meta_header) :
						sizeof(struct gfs2_rgrp);
					if (bitmap_byte < sbp->bsize) { /* if it's on this page */
						rgd->bh[blk]->b_data[buf_offset + bitmap_byte] &=
							~(0x03 << (GFS2_BIT_SIZE * byte_bit));
						rgd->bh[blk]->b_data[buf_offset + bitmap_byte] |=
							(0x01 << (GFS2_BIT_SIZE * byte_bit));
						break;
					}
					bitmap_byte -= (sbp->bsize - buf_offset);
				}
			}
			brelse(bh, updated);
			first = 0;
		} /* while 1 */
		gfs2_rgrp_relse(rgd, updated);
	} /* for all rgs */
	printf("\r%" PRIu64" inodes converted.", sbp->md.next_inum);
	fflush(stdout);
	return 0;
}/* inode_renumber */

/* ------------------------------------------------------------------------- */
/* fetch_inum - fetch an inum entry from disk, given its block               */
/* ------------------------------------------------------------------------- */
int fetch_and_fix_inum(struct gfs2_sbd *sbp, uint64_t iblock,
					   struct gfs2_inum *inum)
{
	struct gfs2_buffer_head *bh_fix;
	struct gfs2_inode *fix_inode;

	bh_fix = bread(sbp, iblock);
	fix_inode = inode_get(sbp, bh_fix);
	inum->no_formal_ino = fix_inode->i_di.di_num.no_formal_ino;
	inum->no_addr = fix_inode->i_di.di_num.no_addr;
	brelse(bh_fix, updated);
	return 0;
}/* fetch_and_fix_inum */

/* ------------------------------------------------------------------------- */
/* process_dirent_info - fix one dirent (directory entry) buffer             */
/*                                                                           */
/* We changed inode numbers, so we must update that number into the          */
/* directory entries themselves.                                             */
/*                                                                           */
/* Returns: 0 on success, -1 on failure                                      */
/* ------------------------------------------------------------------------- */
int process_dirent_info(struct gfs2_inode *dip, struct gfs2_sbd *sbp,
						struct gfs2_buffer_head *bh, int dir_entries)
{
	int error;
	struct gfs2_dirent *dent;
	int de; /* directory entry index */
	
	error = gfs2_dirent_first(dip, bh, &dent);
	if (error != IS_LEAF && error != IS_DINODE) {
		printf("Error retrieving directory.\n");
		return -1;
	}
	/* Go through every dirent in the buffer and process it. */
	/* Turns out you can't trust dir_entries is correct.     */
	for (de = 0; ; de++) {
		struct gfs2_inum inum;
		
		gettimeofday(&tv, NULL);
		/* Do more warm fuzzy stuff for the customer. */
		dirents_fixed++;
		if (tv.tv_sec - seconds) {
			seconds = tv.tv_sec;
			printf("\r%" PRIu64 " directories, %" PRIu64 " dirents fixed.",
				   dirs_fixed, dirents_fixed);
			fflush(stdout);
		}
		/* fix the dirent's inode number based on the inode */
		gfs2_inum_in(&inum, (char *)&dent->de_inum);
		if (inum.no_formal_ino) { /* if not a sentinel (placeholder) */
			error = fetch_and_fix_inum(sbp, inum.no_addr, &inum);
			if (error) {
				printf("Error retrieving inode %" PRIx64 "\n", inum.no_addr);
				break;
			}
		}
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

		error = gfs2_dirent_next(dip, bh, &dent);
		if (error)
			break;
	} /* for every directory entry */
	return 0;
}/* process_dirent_info */

/* ------------------------------------------------------------------------- */
/* fix_one_directory_exhash - fix one directory's inode numbers.             */
/*                                                                           */
/* This is for exhash directories, where the inode has a list of "leaf"      */
/* blocks, each of which is a buffer full of dirents that must be processed. */
/*                                                                           */
/* Returns: 0 on success, -1 on failure                                      */
/* ------------------------------------------------------------------------- */
int fix_one_directory_exhash(struct gfs2_sbd *sbp, struct gfs2_inode *dip)
{
	struct gfs2_buffer_head *bh_leaf;
	int error;
	uint64_t leaf_block, prev_leaf_block;
	uint32_t leaf_num;
	
	prev_leaf_block = 0;
	/* for all the leafs, get the leaf block and process the dirents inside */
	for (leaf_num = 0; ; leaf_num++) {
		uint64 buf;
		struct gfs2_leaf leaf;

		error = gfs2_readi(dip, (char *)&buf, leaf_num * sizeof(uint64),
						   sizeof(uint64));
		if (!error) /* end of file */
			return 0; /* success */
		else if (error != sizeof(uint64)) {
			log_crit("fix_one_directory_exhash: error reading directory.\n");
			return -1;
		}
		else {
			leaf_block = be64_to_cpu(buf);
			error = 0;
		}
		/* leaf blocks may be repeated, so skip the duplicates: */
		if (leaf_block == prev_leaf_block) /* same block? */
			continue;                      /* already converted */
		prev_leaf_block = leaf_block;
		/* read the leaf buffer in */
		error = gfs2_get_leaf(dip, leaf_block, &bh_leaf);
		if (error) {
			printf("Error reading leaf %" PRIx64 "\n", leaf_block);
			break;
		}
		gfs2_leaf_in(&leaf, (char *)bh_leaf->b_data); /* buffer to structure */
		error = process_dirent_info(dip, sbp, bh_leaf, leaf.lf_entries);
		brelse(bh_leaf, updated);
	} /* for leaf_num */
	return 0;
}/* fix_one_directory_exhash */

/* ------------------------------------------------------------------------- */
/* fix_directory_info - sync new inode numbers with directory info           */
/* Returns: 0 on success, -1 on failure                                      */
/* ------------------------------------------------------------------------- */
int fix_directory_info(struct gfs2_sbd *sbp, osi_list_t *dirs_to_fix)
{
	osi_list_t *tmp, *fix;
	struct inode_block *dir_iblk;
	uint64_t offset, dirblock;
	struct gfs2_inode *dip;
	struct gfs2_buffer_head *bh_dir;

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
		bh_dir = bread(sbp, dirblock);
		dip = inode_get(sbp, bh_dir);
		/* fix the directory: either exhash (leaves) or linear (stuffed) */
		if (dip->i_di.di_flags & GFS_DIF_EXHASH) {
			if (fix_one_directory_exhash(sbp, dip)) {
				log_crit("Error fixing exhash directory.\n");
				brelse(bh_dir, updated);
				return -1;
			}
		}
		else {
			if (process_dirent_info(dip, sbp, bh_dir, dip->i_di.di_entries)) {
				log_crit("Error fixing linear directory.\n");
				brelse(bh_dir, updated);
				return -1;
			}
		}
		brelse(bh_dir, updated);
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
		if(!check_inode(ip))
			sdp->sd_rooti = ip;
		else
			free(ip);
	} else
		log_warn("Unable to load root inode\n");
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
		rgd2->rg.rg_header.mh_magic = GFS2_MAGIC;
		rgd2->rg.rg_header.mh_type = GFS2_METATYPE_RG;
		rgd2->rg.rg_header.mh_format = GFS2_FORMAT_RG;
		rgd2->rg.rg_flags = 0;
		rgd2->rg.rg_free = rgd->rd_ri.ri_data;
		rgd2->rg.rg_dinodes = 0;

		rgd2->ri.ri_addr = rgd->rd_ri.ri_addr;
		rgd2->ri.ri_length = rgd->rd_ri.ri_length;
		rgd2->ri.ri_data0 = rgd->rd_ri.ri_data1;
		rgd2->ri.ri_data = rgd->rd_ri.ri_data;
		rgd2->ri.ri_bitbytes = rgd->rd_ri.ri_bitbytes;
		convert_bitmaps(sdp2, rgd2, FALSE); /* allocates rgd2->bh */
		for (x = 0; x < rgd2->ri.ri_length; x++) {
			if (x)
				gfs2_meta_header_out(&mh, rgd2->bh[x]->b_data);
			else
				gfs2_rgrp_out(&rgd2->rg, rgd2->bh[x]->b_data);
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
	struct gfs2_inode *ip = sdp->md.inum;
	uint64_t buf;
	int count;
	
	buf = cpu_to_be64(sdp->md.next_inum);
	count = gfs2_writei(ip, &buf, 0, sizeof(uint64_t));
	if (count != sizeof(uint64_t))
		die("update_inode_file\n");
	
	if (sdp->debug)
		printf("\nNext Inum: %"PRIu64"\n", sdp->md.next_inum);
}/* update_inode_file */

/* ------------------------------------------------------------------------- */
/* write_statfs_file - write the statfs file                                 */
/* ------------------------------------------------------------------------- */
void write_statfs_file(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *ip = sdp->md.statfs;
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
	/* Convert incore gfs1 sb to gfs2 sb              */
	/* ---------------------------------------------- */
	if (!error) {
		printf("Converting superblock.\n");
		error = superblock_cvt(disk_fd, &sb, &sb2);
		if (error)
			fprintf(stderr, "%s: Unable to convert superblock.\n", device);
		bcommit(&sb2); /* write the buffers to disk */
	}
	/* ---------------------------------------------- */
	/* Renumber the inodes consecutively.             */
	/* ---------------------------------------------- */
	if (!error) {
		error = inode_renumber(&sb2, sb.sd_sb.sb_root_di.no_addr);
		if (error)
			fprintf(stderr, "\n%s: Error renumbering inodes.\n", device);
		bcommit(&sb2); /* write the buffers to disk */
	}
	/* ---------------------------------------------- */
	/* Fix the directories to match the new numbers.  */
	/* ---------------------------------------------- */
	if (!error) {
		error = fix_directory_info(&sb2, (osi_list_t *)&dirs_to_fix);
		printf("\r%" PRIu64 " directories, %" PRIu64 " dirents fixed.",
			   dirs_fixed, dirents_fixed);
		fflush(stdout);
		if (error)
			fprintf(stderr, "\n%s: Error fixing directories.\n", device);
	}
	/* ---------------------------------------------- */
	/* Convert journal space to rg space              */
	/* ---------------------------------------------- */
	if (!error) {
		printf("\nConverting journals.\n");
		error = journ_space_to_rg(disk_fd, &sb, &sb2);
		if (error)
			fprintf(stderr, "%s: Error converting journal space.\n", device);
		bcommit(&sb2); /* write the buffers to disk */
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

		inode_put(sb2.master_dir, updated);
		inode_put(sb2.md.inum, updated);
		inode_put(sb2.md.statfs, updated);

		bh = bread(&sb2, sb2.sb_addr);
		gfs2_sb_out(&sb2.sd_sb, bh->b_data);
		brelse(bh, updated);
		bcommit(&sb2); /* write the buffers to disk */

		/* Now delete the now-obsolete gfs1 files: */
		printf("Removing obsolete gfs1 structures.\n");
		fflush(stdout);
		/* Delete the Journal index: */
		gfs2_freedi(&sb2, sb.sd_sb.sb_jindex_di.no_addr);
		/* Delete the rgindex: */
		gfs2_freedi(&sb2, sb.sd_sb.sb_rindex_di.no_addr);
		/* Delete the Quota file: */
		gfs2_freedi(&sb2, sb.sd_sb.sb_quota_di.no_addr);
		/* Delete the License file: */
		gfs2_freedi(&sb2, sb.sd_sb.sb_license_di.no_addr);
		/* Now free all the rgrps */
		gfs2_rgrp_free(&sb2, updated);
		printf("Committing changes to disk.\n");
		fflush(stdout);
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
