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
extern int line, termlines;
extern char edit_fmt[80];
extern char estring[1024];
extern int edit_mode INIT(0);
extern int edit_row[DMODES], edit_col[DMODES];
extern int edit_size[DMODES], last_entry_onscreen[DMODES];
extern char edit_fmt[80];
extern enum dsp_mode dmode INIT(HEX_MODE); /* display mode */

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
		printw("%s", string);
	else
		printf("%s", string);
	va_end(args);
}

void check_highlight(int highlight)
{
	if (!termlines || line >= termlines) /* If printing or out of bounds */
		return;
	if (dmode == HEX_MODE) {
		if (line == (edit_row[dmode] * lines_per_row[dmode]) + 4) {
			if (highlight) {
				COLORS_HIGHLIGHT;
				last_entry_onscreen[dmode] = print_entry_ndx;
			}
			else
				COLORS_NORMAL;
		}
	}
	else {
		if ((line * lines_per_row[dmode]) - 4 == 
			(edit_row[dmode] - start_row[dmode]) * lines_per_row[dmode]) {
			if (highlight) {
				COLORS_HIGHLIGHT;
				last_entry_onscreen[dmode] = print_entry_ndx;
			}
			else
				COLORS_NORMAL;
		}
	}
}

void print_it(const char *label, const char *fmt, const char *fmt2, ...)
{
	va_list args;
	char tmp_string[NAME_MAX];
	const char *fmtstring;
	int decimalsize;

	if (!termlines || line < termlines) {
		va_start(args, fmt2);
		check_highlight(TRUE);
		if (termlines) {
			move(line,0);
			printw("%s", label);
			move(line,24);
		}
		else {
			if (!strcmp(label, "  "))
				printf("%-11s", label);
			else
				printf("%-24s", label);
		}
		vsprintf(tmp_string, fmt, args);

		if (termlines)
			printw("%s", tmp_string);
		else
			printf("%s", tmp_string);
		check_highlight(FALSE);

		if (fmt2) {
			decimalsize = strlen(tmp_string);
			va_end(args);
			va_start(args, fmt2);
			vsprintf(tmp_string, fmt2, args);
			check_highlight(TRUE);
			if (termlines) {
				move(line, 50);
				printw("%s", tmp_string);
			}
			else {
				int i;
				for (i=20 - decimalsize; i > 0; i--)
					printf(" ");
				printf("%s", tmp_string);
			}
			check_highlight(FALSE);
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
				printw("%s", fmtstring);
			}
			else
				printf("%s", fmtstring);
		}
		if (termlines) {
			refresh();
			if (line == (edit_row[dmode] * lines_per_row[dmode]) + 4) {
				strcpy(estring, tmp_string);
				strcpy(edit_fmt, fmt);
				edit_size[dmode] = strlen(estring);
				COLORS_NORMAL;
			}
			last_entry_onscreen[dmode] = (line / lines_per_row[dmode]) - 4;
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
		indir->dirent[d].filename[de.de_name_len] = '\0';
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
	int isdir = !!(S_ISDIR(di->di_mode)) || 
		(gfs1 && di->__pad1 == GFS_FILE_DIR);

	indirect_blocks = 0;
	memset(indirect, 0, sizeof(indirect));
	if (di->di_height > 0) {
		/* Indirect pointers */
		for (x = sizeof(struct gfs2_dinode); x < sbd.bsize;
			 x += sizeof(uint64_t)) {
			p = be64_to_cpu(*(uint64_t *)(buf + x));
			if (p) {
				indirect->ii[indirect_blocks].block = p;
				indirect->ii[indirect_blocks].is_dir = FALSE;
				indirect_blocks++;
			}
		}
	}
	else if (isdir && !(di->di_flags & GFS2_DIF_EXHASH)) {
		int skip = 0;

		/* Directory Entries: */
		indirect->ii[0].dirents = 0;
		indirect->ii[0].block = block;
		indirect->ii[0].is_dir = TRUE;
		for (x = sizeof(struct gfs2_dinode); x < sbd.bsize; x += skip) {
			skip = indirect_dirent(indirect->ii,
					       buf + x,
					       indirect->ii[0].dirents);
			if (skip <= 0)
				break;
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

				if (last >= max_block)
					break;
				tmp_bh = bread(&sbd, last);
				gfs2_leaf_in(&leaf, tmp_bh->b_data);
				indirect->ii[indirect_blocks].dirents = 0;
				for (direntcount = 0, bufoffset = sizeof(struct gfs2_leaf);
					 bufoffset < sbd.bsize;
					 direntcount++, bufoffset += skip) {
					skip = indirect_dirent(&indirect->ii[indirect_blocks],
										   tmp_bh->b_data + bufoffset,
										   direntcount);
					if (skip <= 0)
						break;
				}
				brelse(tmp_bh, not_updated);
				indirect->ii[indirect_blocks].block = last;
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
int do_indirect_extended(char *buf, struct iinfo *iinf)
{
	unsigned int x, y;
	uint64_t p;
	int i_blocks;

	i_blocks = 0;
	memset(iinf, 0, sizeof(struct iinfo));
	for (x = (gfs1 ? sizeof(struct gfs_indirect):
			  sizeof(struct gfs2_meta_header)), y = 0;
		 x < sbd.bsize;
		 x += sizeof(uint64_t), y++) {
		p = be64_to_cpu(*(uint64_t *)(buf + x));
		if (p) {
			iinf->ii[i_blocks].block = p;
			iinf->ii[i_blocks].is_dir = FALSE;
			i_blocks++;
		}
	}
	return i_blocks;
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
void do_leaf_extended(char *buf, struct iinfo *indir)
{
	int x, i;
	struct gfs2_dirent de;

	x = 0;
	memset(indir, 0, sizeof(indir));
	/* Directory Entries: */
	for (i = sizeof(struct gfs2_leaf); i < sbd.bsize;
	     i += de.de_rec_len) {
		gfs2_dirent_in(&de, buf + i);
		if (de.de_inum.no_addr) {
			indir->ii[0].block = de.de_inum.no_addr;
			indir->ii[0].dirent[x].block = de.de_inum.no_addr;
			memcpy(&indir->ii[0].dirent[x].dirent,
			       &de, sizeof(struct gfs2_dirent));
			memcpy(&indir->ii[0].dirent[x].filename,
			       buf + i + sizeof(struct gfs2_dirent),
			       de.de_name_len);
			indir->ii[0].dirent[x].filename[de.de_name_len] = '\0';
			indir->ii[0].is_dir = TRUE;
			indir->ii[0].dirents++;
			x++;
		}
		if (de.de_rec_len <= sizeof(struct gfs2_dirent))
			break;
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
	print_gfs2("Eattr Entries:");
	eol(0);

	for (x = sizeof(struct gfs2_meta_header); x < sbd.bsize; x += ea.ea_rec_len)
	{
		eol(0);
		gfs2_ea_header_in(&ea, buf + x);
		gfs2_ea_header_print(&ea, buf + x + sizeof(struct gfs2_ea_header));
	}
}

void gfs2_inum_print2(const char *title,struct gfs2_inum *no)
{
	if (termlines) {
		check_highlight(TRUE);
		move(line,2);
		printw(title);
		check_highlight(FALSE);
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
	if (gfs1) {
		gfs2_inum_print2("jindex ino", &sbd1->sb_jindex_di);
		gfs2_inum_print2("rindex ino", &sbd1->sb_rindex_di);
	}
	else
		gfs2_inum_print2("master dir", &sb->sb_master_dir);
	gfs2_inum_print2("root dir  ", &sb->sb_root_dir);

	pv(sb, sb_lockproto, "%s", NULL);
	pv(sb, sb_locktable, "%s", NULL);
	if (gfs1) {
		gfs2_inum_print2("quota ino ", &gfs1_quota_di);
		gfs2_inum_print2("license   ", &gfs1_license_di);
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
int display_gfs2(void)
{
	struct gfs2_meta_header mh;
	struct gfs2_rgrp rg;
	struct gfs2_leaf lf;
	struct gfs_log_header lh1;
	struct gfs2_log_header lh;
	struct gfs2_log_descriptor ld;
	struct gfs2_quota_change qc;

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
			if (gfs1) {
				gfs_log_header_in(&lh1, buf);
				gfs_log_header_print(&lh1);
			} else {
				gfs2_log_header_in(&lh, buf);
				gfs2_log_header_print(&lh);
			}
			break;
			
		case GFS2_METATYPE_LD:
			print_gfs2("Log descriptor");
			eol(0);
			gfs2_log_descriptor_in(&ld, buf);
			gfs2_log_descriptor_print(&ld);
			break;

		case GFS2_METATYPE_EA:
			print_gfs2("Eattr Block:");
			eol(0);
			do_eattr_extended(buf);
			break;
			
		case GFS2_METATYPE_ED:
			print_gfs2("Eattr Data Block:");
			eol(0);
			gfs2_meta_header_print(&mh);
			break;
			
		case GFS2_METATYPE_LB:
			print_gfs2("Log Buffer");
			eol(0);
			gfs2_meta_header_print(&mh);
			break;

		case GFS2_METATYPE_QC:
			print_gfs2("Quota Change");
			eol(0);
			gfs2_quota_change_in(&qc, buf);
			gfs2_quota_change_print(&qc);
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
