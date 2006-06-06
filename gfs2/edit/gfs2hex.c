/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made availale to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/types.h>
#include <linux/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <curses.h>

#include "hexedit.h"
#include "linux_endian.h"

#define WANT_GFS_CONVERSION_FUNCTIONS
#include <linux/gfs2_ondisk.h>

#include "gfs2hex.h"
/* from libgfs2: */
#include "libgfs2.h"
#include "ondisk.h"


extern struct gfs2_sb sb;
extern char *buf;
extern struct gfs2_dinode di;
extern uint64_t bufsize;
extern int line, termlines;
extern char edit_fmt[80];
extern char edit_string[1024];
extern int edit_mode INIT(0);
extern int edit_row[DISPLAY_MODES], edit_col[DISPLAY_MODES];
extern int edit_size[DISPLAY_MODES], edit_last[DISPLAY_MODES];
extern char edit_string[1024], edit_fmt[80];
extern enum dsp_mode display_mode INIT(HEX_MODE);

void eol(int col) /* end of line */
{
	if (termlines) {
		line++;
		move(line, col);
	}
	else {
		printf("\n");
		for (; col > 0; col--)
			printf(" ");
	}
}

void print_gfs2(const char *fmt, ...)
{
	va_list args;
	char string[NAME_MAX];
	
	memset(string, 0, sizeof(string));
	va_start(args, fmt);
	vsprintf(string, fmt, args);
	if (termlines)
		printw(string);
	else
		printf(string);
	va_end(args);
}

void print_it(const char *label, const char *fmt, const char *fmt2, ...)
{
	va_list args;
	char tmp_string[NAME_MAX];
	const char *fmtstring;
	int decimalsize;

	if (!termlines || line < termlines) {
		va_start(args, fmt2);
		if (termlines) {
			if (line == edit_row[display_mode] + 4)
				COLORS_HIGHLIGHT;
			move(line,0);
			printw("%s", label);
			if (line == edit_row[display_mode] + 4)
				COLORS_NORMAL;
			move(line,24);
		}
		else {
			if (!strcmp(label, "  "))
				printf("%-11s", label);
			else
				printf("%-24s", label);
		}
		vsprintf(tmp_string, fmt, args);

		if (termlines) {
			if (line == edit_row[display_mode] + 4)
				COLORS_HIGHLIGHT;
			printw(tmp_string);
			if (line == edit_row[display_mode] + 4)
				COLORS_NORMAL;
		}
		else
			printf(tmp_string);

		if (fmt2) {
			decimalsize = strlen(tmp_string);
			vsprintf(tmp_string, fmt2, args);
			if (termlines) {
				move(line, 50);
				if (line == edit_row[display_mode] + 4)
					COLORS_HIGHLIGHT;
				printw("%s", tmp_string);
				if (line == edit_row[display_mode] + 4)
					COLORS_NORMAL;
			}
			else {
				int i;
				for (i=20 - decimalsize; i > 0; i--)
					printf(" ");
				printf("%s", tmp_string);
			}
		}
		else {
			if (strstr(fmt,"X") || strstr(fmt,"x"))
				fmtstring="(hex)";
			else if (strstr(fmt,"s"))
				fmtstring="";
			else
				fmtstring="(decimal)";
			if (termlines) {
				move(line, 50);
				printw(fmtstring);
			}
			else
				printf(fmtstring);
		}
		if (termlines) {
			refresh();
			if (line == edit_row[display_mode] + 4) {
				strcpy(edit_string, tmp_string);
				strcpy(edit_fmt, fmt);
				edit_size[display_mode] = strlen(edit_string);
				COLORS_NORMAL;
			}
			if (line - 3 > edit_last[display_mode])
				edit_last[display_mode] = line - 4;
		}
		eol(0);
		va_end(args);
	}
}

int indirect_dirent(struct indirect_info *indir, char *ptr, int d)
{
	struct gfs2_dirent de;

	gfs2_dirent_in(&de, ptr);
	if (de.de_rec_len < sizeof(struct gfs2_dirent) ||
		de.de_rec_len > 4096 - sizeof(struct gfs2_dirent))
		return -1;
	if (de.de_inum.no_addr) {
		indir->block = de.de_inum.no_addr;
		memcpy(&indir->dirent[d].dirent, &de, sizeof(struct gfs2_dirent));
		memcpy(&indir->dirent[d].filename,
			   ptr + sizeof(struct gfs2_dirent), de.de_name_len);
		indir->dirent[d].block = de.de_inum.no_addr;
		indir->is_dir = TRUE;
		indir->dirents++;
	}
	return de.de_rec_len;
}

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
void do_dinode_extended(struct gfs2_dinode *di, char *buf)
{
	unsigned int x, y;
	uint64_t p, last;
	int isdir = !!(S_ISDIR(di->di_mode));

	indirect_blocks = 0;
	memset(&indirect, 0, sizeof(indirect));
	if (di->di_height > 0) {
		/* Indirect pointers */
		for (x = sizeof(struct gfs2_dinode), y = 0;
			 x < bufsize;
			 x += sizeof(uint64_t), y++) {
			p = be64_to_cpu(*(uint64_t *)(buf + x));
			if (p) {
				indirect[indirect_blocks].block = p;
				indirect[indirect_blocks].is_dir = FALSE;
				indirect_blocks++;
			}
		}
	}
	else if (isdir &&
			 !(di->di_flags & GFS2_DIF_EXHASH)) {
		int skip = 0;
		/* Directory Entries: */
		indirect[indirect_blocks].dirents = 0;
		for (x = sizeof(struct gfs2_dinode); x < bufsize; x += skip) {
			skip = indirect_dirent(&indirect[indirect_blocks], buf + x, 0);
			if (skip <= 0)
				break;
			indirect_blocks++;
		}
	}
	else if (isdir &&
			 (di->di_flags & GFS2_DIF_EXHASH) &&
			 di->di_height == 0) {
		/* Leaf Pointers: */
		
		last = be64_to_cpu(*(uint64_t *)(buf + sizeof(struct gfs2_dinode)));
    
		for (x = sizeof(struct gfs2_dinode), y = 0;
			 y < (1 << di->di_depth);
			 x += sizeof(uint64_t), y++) {
			p = be64_to_cpu(*(uint64_t *)(buf + x));

			if (p != last || ((y + 1) * sizeof(uint64_t) == di->di_size)) {
				struct gfs2_buffer_head *tmp_bh;
				int skip = 0, direntcount = 0;
				struct gfs2_leaf leaf;
				unsigned int bufoffset;

				tmp_bh = bread(&sbd, last);
				gfs2_leaf_in(&leaf, tmp_bh->b_data);
				indirect[indirect_blocks].dirents = 0;
				for (direntcount = 0, bufoffset = sizeof(struct gfs2_leaf);
					 bufoffset < bufsize;
					 direntcount++, bufoffset += skip) {
					skip = indirect_dirent(&indirect[indirect_blocks],
										   tmp_bh->b_data + bufoffset,
										   direntcount);
					if (skip <= 0)
						break;
				}
				brelse(tmp_bh, not_updated);
				indirect[indirect_blocks].block = last;
				indirect_blocks++;
				last = p;
			} /* if not duplicate pointer */
		} /* for indirect pointers found */
	} /* if exhash */
}/* do_dinode_extended */

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
void do_indirect_extended(char *buf)
{
	unsigned int x, y;
	uint64_t p;

	eol(0);
	printf("Pointers");
	eol(0);
	eol(0);

	for (x = sizeof(struct gfs2_meta_header), y = 0; x < bufsize; x += 8, y++)
	{
		p = be64_to_cpu(*(uint64_t *)(buf + x));
		
		if (p) {
			printf("  %u -> %" PRIu64, y, p);
			eol(0);
		}
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
void do_leaf_extended(char *buf)
{
	struct gfs2_dirent de;
	unsigned int x;

	eol(0);
	printf("Directory Entries:");
	eol(0);

	for (x = sizeof(struct gfs2_leaf); x < bufsize; x += de.de_rec_len) {
		eol(0);
		gfs2_dirent_in(&de, buf + x);
		if (de.de_inum.no_addr)
			gfs2_dirent_print(&de, buf + x + sizeof(struct gfs2_dirent));
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

void do_eattr_extended(char *buf)
{
	struct gfs2_ea_header ea;
	unsigned int x;

	eol(0);
	printf("Eattr Entries:");
	eol(0);

	for (x = sizeof(struct gfs2_meta_header); x < bufsize; x += ea.ea_rec_len)
	{
		eol(0);
		gfs2_ea_header_in(&ea, buf + x);
		gfs2_ea_header_print(&ea, buf + x + sizeof(struct gfs2_ea_header));
	}
}

void gfs2_inum_print2(const char *title,struct gfs2_inum *no)
{
	if (termlines) {
		move(line,2);
		printw(title);
	}
	else
		printf("  %s:",title);
	pv2(no, no_formal_ino, "%lld", "0x%"PRIx64);
	if (!termlines)
		printf("        addr:");
	pv2(no, no_addr, "%lld", "0x%"PRIx64);
}

/**
 * gfs2_sb_print2 - Print out a superblock
 * @sb: the cpu-order buffer
 */
void gfs2_sb_print2(struct gfs2_sb *sb)
{
	gfs2_meta_header_print(&sb->sb_header);

	pv(sb, sb_fs_format, "%u", "0x%x");
	pv(sb, sb_multihost_format, "%u", "0x%x");

	pv(sb, sb_bsize, "%u", "0x%x");
	pv(sb, sb_bsize_shift, "%u", "0x%x");
	gfs2_inum_print2("master dir", &sb->sb_master_dir);
	gfs2_inum_print2("root dir  ", &sb->sb_root_dir);

	pv(sb, sb_lockproto, "%s", NULL);
	pv(sb, sb_locktable, "%s", NULL);
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
int display_gfs2(void)
{
	struct gfs2_meta_header mh;
	struct gfs2_rgrp rg;
	struct gfs2_leaf lf;
	struct gfs2_log_header lh;

	uint32_t magic;

	magic = be32_to_cpu(*(uint32_t *)buf);

	switch (magic)
	{
	case GFS2_MAGIC:
		gfs2_meta_header_in(&mh, buf);
		
		switch (mh.mh_type)
		{
		case GFS2_METATYPE_SB:
			print_gfs2("Superblock:");
			eol(0);
			gfs2_sb_in(&sbd.sd_sb, buf);
			gfs2_sb_print2(&sbd.sd_sb);
			break;
			
		case GFS2_METATYPE_RG:
			print_gfs2("Resource Group Header:");
			eol(0);
			gfs2_rgrp_in(&rg, buf);
			gfs2_rgrp_print(&rg);
			break;
			
		case GFS2_METATYPE_RB:
			print_gfs2("Resource Group Bitmap:");
			eol(0);
			gfs2_meta_header_print(&mh);
			break;
			
		case GFS2_METATYPE_DI:
			print_gfs2("Dinode:");
			eol(0);
			gfs2_dinode_print(&di);
			break;
			
		case GFS2_METATYPE_LF:
			print_gfs2("Leaf:");
			eol(0);
			gfs2_leaf_in(&lf, buf);
			gfs2_leaf_print(&lf);
			break;
			
		case GFS2_METATYPE_IN:
			print_gfs2("Indirect Block:");
			eol(0);
			gfs2_meta_header_print(&mh);
			break;
			
		case GFS2_METATYPE_JD:
			print_gfs2("Journaled File Block:");
			eol(0);
			gfs2_meta_header_print(&mh);
			break;
			
		case GFS2_METATYPE_LH:
			print_gfs2("Log Header:");
			eol(0);
			gfs2_log_header_in(&lh, buf);
			gfs2_log_header_print(&lh);
			break;
			
		case GFS2_METATYPE_EA:
			print_gfs2("Eattr Block:");
			eol(0);
			gfs2_meta_header_print(&mh);
			break;
			
		case GFS2_METATYPE_ED:
			print_gfs2("Eattr Data Block:");
			eol(0);
			gfs2_meta_header_print(&mh);
			break;
			
		default:
			print_gfs2("Unknown metadata type");
			eol(0);
			break;
		}
		break;
		
	default:
		print_gfs2("Unknown block type");
		eol(0);
		break;
	};
	return(0);
}
