/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <linux/gfs2_ondisk.h>
#include "linux_endian.h"

#include "hexedit.h"
#include "gfs2.h"

/******************************************************************************
*******************************************************************************
**
** do_dinode_extended()
**
** Description:
**
** Input(s):
**
** Output(s):
**
** Returns:
**
*******************************************************************************
******************************************************************************/

void
do_dinode_extended(struct gfs2_dinode *di, char *buf)
{
	unsigned int x, y, count;
	struct gfs2_dirent de;
	uint64_t p, last;

	if (di->di_height > 0) {
		printf("\nIndirect Pointers\n\n");

		for (x = sizeof(struct gfs2_dinode), y = 0;
		     x < bsize; x += sizeof(uint64_t), y++) {
			p = gfs2_64_to_cpu(*(uint64_t *)(buf + x));

			if (p)
				printf("  %u -> %"PRIu64"\n", y, p);
		}
	}

	else if (S_ISDIR(di->di_mode) &&
		 !(di->di_flags & GFS2_DIF_EXHASH)) {
		printf("\nDirectory Entries:\n");

		for (x = sizeof(struct gfs2_dinode); x < bsize;
		     x += de.de_rec_len) {
			printf("\n");
			gfs2_dirent_in(&de, buf + x);
			if (de.de_inum.no_formal_ino)
				gfs2_dirent_print(&de,
						 buf + x +
						 sizeof(struct gfs2_dirent));
		}
	}

	else if (S_ISDIR(di->di_mode) &&
		 (di->di_flags & GFS2_DIF_EXHASH) &&
		 di->di_height == 0) {
		printf("\nLeaf Pointers:\n\n");

		last = gfs2_64_to_cpu(*(uint64_t *)(buf + sizeof(struct gfs2_dinode)));
		count = 0;

		for (x = sizeof(struct gfs2_dinode), y = 0;
		     y < (1 << di->di_depth);
		     x += sizeof(uint64_t), y++) {
			p = gfs2_64_to_cpu(*(uint64_t *) (buf + x));

			if (p != last) {
				printf("  %u:  %"PRIu64"\n", count, last);
				last = p;
				count = 1;
			} else
				count++;

			if ((y + 1) * sizeof(uint64_t) == di->di_size)
				printf("  %u:  %"PRIu64"\n", count, last);
		}
	}
}

/******************************************************************************
*******************************************************************************
**
** do_indirect_extended()
**
** Description:
**
** Input(s):
**
** Output(s):
**
** Returns:
**
*******************************************************************************
******************************************************************************/

void
do_indirect_extended(char *buf)
{
	unsigned int x, y;
	uint64_t p;

	printf("\nPointers\n\n");

	for (x = sizeof(struct gfs2_meta_header), y = 0; x < bsize; x += 8, y++) {
		p = gfs2_64_to_cpu(*(uint64_t *) (buf + x));

		if (p)
			printf("  %u -> %"PRIu64"\n", y, p);
	}
}

/******************************************************************************
*******************************************************************************
**
** do_leaf_extended()
**
** Description:
**
** Input(s):
**
** Output(s):
**
** Returns:
**
*******************************************************************************
******************************************************************************/

void
do_leaf_extended(char *buf)
{
	struct gfs2_dirent de;
	unsigned int x;

	printf("\nDirectory Entries:\n");

	for (x = sizeof(struct gfs2_leaf); x < bsize; x += de.de_rec_len) {
		printf("\n");
		gfs2_dirent_in(&de, buf + x);
		if (de.de_inum.no_formal_ino)
			gfs2_dirent_print(&de,
					 buf + x + sizeof(struct gfs2_dirent));
	}
}

/******************************************************************************
*******************************************************************************
**
** do_eattr_extended()
**
** Description:
**
** Input(s):
**
** Output(s):
**
** Returns:
**
*******************************************************************************
******************************************************************************/

void
do_eattr_extended(char *buf)
{
	struct gfs2_ea_header ea;
	unsigned int x;

	printf("\nEattr Entries:\n");

	for (x = sizeof(struct gfs2_meta_header); x < bsize; x += ea.ea_rec_len) {
		printf("\n");
		gfs2_ea_header_in(&ea, buf + x);
		gfs2_ea_header_print(&ea,
				    buf + x + sizeof(struct gfs2_ea_header));
	}
}

/******************************************************************************
*******************************************************************************
**
** int display_gfs2()
**
** Description:
**   This routine...
**
** Input(s):
**  *buffer   - 
**   extended - 
**
** Returns:
**   0 if OK, 1 on error.
**
*******************************************************************************
******************************************************************************/

int
display_gfs2(int extended)
{
	struct gfs2_meta_header mh;
	struct gfs2_sb sb;
	struct gfs2_rgrp rg;
	struct gfs2_dinode di;
	struct gfs2_leaf lf;
	struct gfs2_log_header lh;
	struct gfs2_log_descriptor ld;
	char *buf;
	uint32_t magic;

	type_alloc(buf, char, bsize);

	do_lseek(fd, block * bsize);
	do_read(fd, buf, bsize);

	magic = gfs2_32_to_cpu(*(uint32_t *) buf);

	switch (magic) {
	case GFS2_MAGIC:
		gfs2_meta_header_in(&mh, buf);

		switch (mh.mh_type) {
		case GFS2_METATYPE_SB:
			printf("Superblock:\n\n");
			gfs2_sb_in(&sb, buf);
			gfs2_sb_print(&sb);

			if (extended)
				printf("\nNo Extended data\n");

			break;

		case GFS2_METATYPE_RG:
			printf("Resource Group Header:\n\n");
			gfs2_rgrp_in(&rg, buf);
			gfs2_rgrp_print(&rg);

			if (extended)
				printf("\nNo Extended data\n");

			break;

		case GFS2_METATYPE_RB:
			printf("Resource Group Bitmap:\n\n");
			gfs2_meta_header_print(&mh);

			if (extended)
				printf("\nNo Extended data\n");

			break;

		case GFS2_METATYPE_DI:
			printf("Dinode:\n\n");
			gfs2_dinode_in(&di, buf);
			gfs2_dinode_print(&di);

			if (extended)
				do_dinode_extended(&di, buf);

			break;

		case GFS2_METATYPE_LF:
			printf("Leaf:\n\n");
			gfs2_leaf_in(&lf, buf);
			gfs2_leaf_print(&lf);

			if (extended)
				do_leaf_extended(buf);

			break;

		case GFS2_METATYPE_IN:
			printf("Indirect Block:\n\n");
			gfs2_meta_header_print(&mh);

			if (extended)
				do_indirect_extended(buf);

			break;

		case GFS2_METATYPE_JD:
			printf("Journaled File Block:\n\n");
			gfs2_meta_header_print(&mh);

			if (extended)
				printf("\nNo Extended data\n");

			break;

		case GFS2_METATYPE_LH:
			printf("Log Header:\n\n");
			gfs2_log_header_in(&lh, buf);
			gfs2_log_header_print(&lh);

			if (extended)
				printf("\nNo Extended data\n");

			break;

		case GFS2_METATYPE_LD:
			printf("Log Descriptor:\n\n");
			gfs2_log_descriptor_in(&ld, buf);
			gfs2_log_descriptor_print(&ld);

			if (extended)
				printf("\nNo Extended data\n");

			break;

		case GFS2_METATYPE_EA:
			printf("Eattr Block:\n\n");
			gfs2_meta_header_print(&mh);

			if (extended)
				do_eattr_extended(buf);

			break;

		case GFS2_METATYPE_ED:
			printf("Eattr Data Block:\n\n");
			gfs2_meta_header_print(&mh);

			if (extended)
				printf("\nNo Extended data\n");

			break;

		default:
			printf("Unknown metadata type\n");
			break;
		}

		break;

	default:
		printf("Unknown block type\n");
		break;
	};

	free(buf);

	return 0;
}

/******************************************************************************
*******************************************************************************
**
** int edit_gfs2()
**
** Description:
**   This routine...
**
** Input(s):
**  *buffer   - 
**   extended - 
**
** Returns:
**   0 if OK, 1 on error.
**
*******************************************************************************
******************************************************************************/

int
edit_gfs2(char *arg1, char *arg2, char *arg3)
{
	char buf[512];
	unsigned int row, col, byte;
	uint64_t dev_offset;
	unsigned int offset;

	if (!strncmp(arg3, "", 1)) {
		fprintf(stderr, "%s:  invalid number of arguments\n",
			prog_name);
		return -EINVAL;
	}

	row = atoi(arg1);
	col = atoi(arg2);
	sscanf(arg3, "%x", &byte);

	if (row >= SCREEN_HEIGHT) {
		fprintf(stderr, "%s:  row is out of range for set\n",
			prog_name);
		return -EINVAL;
	}

	if (col >= SCREEN_WIDTH) {
		fprintf(stderr, "%s:  column is out of range for set\n",
			prog_name);
		return -EINVAL;
	}

	if (byte > 255) {
		fprintf(stderr, "%s:  byte value is out of range for set\n",
			prog_name);
		return -EINVAL;
	}

	/* Make sure all I/O is 512-byte aligned */

	dev_offset = (block * bsize +
		      start * SCREEN_WIDTH +
		      row * SCREEN_WIDTH +
		      col) >> 9 << 9;
	offset = (block * bsize +
		  start * SCREEN_WIDTH +
		  row * SCREEN_WIDTH +
		  col) -
		dev_offset;

	do_lseek(fd, dev_offset);
	do_read(fd, buf, 512);

	buf[offset] = (unsigned char)byte;

	do_lseek(fd, dev_offset);
	do_write(fd, buf, 512);

	fsync(fd);

	return 0;
}
