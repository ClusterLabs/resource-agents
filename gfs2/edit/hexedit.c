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
#include <ctype.h>
#include <string.h>
#include <inttypes.h>
#include <sys/types.h>
#include <linux/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <curses.h>
#include <term.h>
#include <time.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <dirent.h>

#include "linux_endian.h"
#include <linux/gfs2_ondisk.h>
#include "copyright.cf"

#define EXTERN
#include "hexedit.h"
#include "linux_endian.h"
#include "libgfs2.h"
#include "gfs2hex.h"

#include <syslog.h>

#define RGLIST_DUMMY_BLOCK -2

int display(int identify_only);
extern void eol(int col);
extern void do_indirect_extended(char *buf);

/* ------------------------------------------------------------------------ */
/* UpdateSize - screen size changed, so update it                           */
/* ------------------------------------------------------------------------ */
void UpdateSize(int sig)
{
	static char term_buffer[2048];
	int rc;

	termlines = 30;
	termtype = getenv("TERM");
	if (termtype == NULL)
		return;
	rc=tgetent(term_buffer,termtype);
	if (rc>=0) {
		termlines = tgetnum("li");
		if (termlines < 10)
			termlines = 30;
		termcols = tgetnum("co");
		if (termcols < 80)
			termcols = 80;
	}
	else
		perror("Error: tgetent failed.");
	termlines--; /* last line is number of lines -1 */
	display(FALSE);
	signal(SIGWINCH, UpdateSize);
}

/* ------------------------------------------------------------------------- */
/* erase - clear the screen */
/* ------------------------------------------------------------------------- */
void Erase(void)
{
	int i;
	char spaces[256];

	memset(spaces, ' ', sizeof(spaces));
	spaces[termcols] = '\0';
	for (i = 0; i < termlines; i++) {
		move(i, 0);
		printw(spaces);
	}
	/*clear(); doesn't set background correctly */
	/*erase();*/
	/*bkgd(bg);*/
}

/* ------------------------------------------------------------------------- */
/* display_title_lines */
/* ------------------------------------------------------------------------- */
void display_title_lines(void)
{
	Erase();
	COLORS_TITLE;
	move(0, 0);
	printw("%-80s",TITLE1);
	move(termlines, 0);
	printw("%-79s",TITLE2);
	COLORS_NORMAL;
}

/* ------------------------------------------------------------------------- */
/* bobgets - get a string                                                    */
/* returns: 1 if user exited by hitting enter                                */
/*          0 if user exited by hitting escape                               */
/* ------------------------------------------------------------------------- */
int bobgets(char string[],int x,int y,int sz,int *ch)
{
	int done,runningy,rc;

	move(x,y);
	done=FALSE;
	COLORS_INVERSE;
	move(x,y);
	addstr(string);
	move(x,y);
	curs_set(2);
	refresh();
	runningy=y;
	rc=0;
	while (!done) {
		*ch = getch();
		
		if(*ch < 0x0100 && isprint(*ch)) {
			char *p=string+strlen(string); // end of the string

			*(p+1)='\0';
			while (insert && p > &string[runningy-y]) {
				*p=*(p-1);
				p--;
			}
			string[runningy-y]=*ch;
			runningy++;
			move(x,y);
			addstr(string);
			if (runningy-y >= sz) {
				rc=1;
				*ch = KEY_RIGHT;
				done = TRUE;
			}
		}
		else {
			// special character, is it one we recognize?
			switch(*ch)
			{
			case(KEY_ENTER):
			case('\n'):
			case('\r'):
				rc=1;
				done=TRUE;
				break;
			case(KEY_CANCEL):
			case(0x01B):
				rc=0;
				done=TRUE;
				break;
			case(KEY_LEFT):
				if (dmode == HEX_MODE) {
					done = TRUE;
					rc = 1;
				}
				else
					runningy--;
				break;
			case(KEY_RIGHT):
				if (dmode == HEX_MODE) {
					done = TRUE;
					rc = 1;
				}
				else
					runningy++;
				break;
			case(KEY_DC):
			case(0x07F):
				if (runningy>=y) {
					char *p;
					p = &string[runningy - y];
					while (*p) {
						*p = *(p + 1);
						p++;
					}
					*p = '\0';
					runningy--;
					// remove the character from the string 
					move(x,y);
					addstr(string);
					COLORS_NORMAL;
					addstr(" ");
					COLORS_INVERSE;
					runningy++;
				}
				break;
			case(KEY_BACKSPACE):
				if (runningy>y) {
					char *p;

					p = &string[runningy - y - 1];
					while (*p) {
						*p = *(p + 1);
						p++;
					}
					*p='\0';
					runningy--;
					// remove the character from the string 
					move(x,y);
					addstr(string);
					COLORS_NORMAL;
					addstr(" ");
					COLORS_INVERSE;
				}
				break;
			case KEY_DOWN:	// Down
				rc=0x5000U;
				done=TRUE;
				break;
			case KEY_UP:	// Up
				rc=0x4800U;
				done=TRUE;
				break;
			case 0x014b:
				insert=!insert;
				move(0,68);
				if (insert)
					printw("insert ");
				else
					printw("replace");
				break;
			default:
				move(0,70);
				printw("%08X",*ch);
				// ignore all other characters
				break;
			} // end switch on non-printable character
		} // end non-printable character
		move(x,runningy);
		refresh();
	} // while !done
	if (sz>0)
		string[sz]='\0';
	COLORS_NORMAL;
	return rc;
}/* bobgets */

/******************************************************************************
** instr - instructions
******************************************************************************/
void gfs2instr(const char *s1, const char *s2)
{
	COLORS_HIGHLIGHT;
	move(line,0);
	printw(s1);
	COLORS_NORMAL;
	move(line,17);
	printw(s2);
	line++;
}

/******************************************************************************
*******************************************************************************
**
** void print_usage()
**
** Description:
**   This routine prints out the appropriate commands for this application.
**
*******************************************************************************
******************************************************************************/

void print_usage(void)
{
	int ch;

	line = 2;
	Erase();
	display_title_lines();
	move(line++,0);
	printw("Supported commands: (roughly conforming to the rules of 'less')");
	line++;
	move(line++,0);
	printw("Navigation:");
	gfs2instr("<pg up>/<down>","Move up or down one screen full");
	gfs2instr("<up>/<down>","Move up or down one line");
	gfs2instr("<left>/<right>","Move left or right one byte");
	gfs2instr("<home>","Return to the superblock.");
	gfs2instr("   f","Forward one 4K block");
	gfs2instr("   b","Backward one 4K block");
	gfs2instr("   g","Goto a given block (number, master, root, rindex, jindex, etc)");
	gfs2instr("   j","Jump to the highlighted 64-bit block number.");
	gfs2instr("    ","(You may also arrow up to the block number and hit enter)");
	gfs2instr("<backspace>","Return to a previous block (a block stack is kept)");
	gfs2instr("<space>","Jump forward to block before backspace (opposite of backspace)");
	line++;
	move(line++, 0);
	printw("Other commands:");
	gfs2instr("   h","This Help display");
	gfs2instr("   c","Toggle the color scheme");
	gfs2instr("   m","Switch display mode: hex -> GFS2 structure -> Extended");
	gfs2instr("   q","Quit (same as hitting <escape> key)");
	gfs2instr("<enter>","Edit a value (enter to save, esc to discard)");
	gfs2instr("       ","(Currently only works on the hex display)");
	gfs2instr("<escape>","Quit the program");
	line++;
	move(line++, 0);
	printw("Notes: Areas shown in red are outside the bounds of the struct/file.");
	move(line++, 0);
	printw("       Areas shown in blue are file contents.");
	move(line++, 0);
	printw("       Characters shown in green are selected for edit on <enter>.");
	move(line++, 0);
	move(line++, 0);
	printw("Press any key to return.");
	refresh();
	while ((ch=getch()) == 0); // wait for input
	Erase();
}

/* ------------------------------------------------------------------------ */
/* display_block_type                                                       */
/* returns: metatype if block is a GFS2 structure block type                */
/*          0 if block is not a GFS2 structure                              */
/* ------------------------------------------------------------------------ */
int display_block_type(const char *lpBuffer)
{
	int ret_type = 0; /* return type */

	/* first, print out the kind of GFS2 block this is */
	if (termlines) {
		line = 1;
		move(line, 0);
	}
	print_gfs2("Block #");
	if (termlines) {
		if (edit_row[dmode] == -1)
			COLORS_HIGHLIGHT;
	}
	if (block == RGLIST_DUMMY_BLOCK)
		print_gfs2("RG List       ");
	else
		print_gfs2("%lld    (0x%"PRIx64")", block, block);
	if (termlines) {
		if (edit_row[dmode] == -1)
			COLORS_NORMAL;
		move(line,30);
	}
	else
		print_gfs2(" ");
	print_gfs2("of %" PRIu64 " (0x%" PRIX64 ")", max_block, max_block);
	if (termlines)
		move(line, 55);
	else
		printf(" ");

	if (block == RGLIST_DUMMY_BLOCK) {
		ret_type = GFS2_METATYPE_RG;
		struct_len = sizeof(struct gfs2_rgrp);
	}
	else if (*(lpBuffer+0)==0x01 && *(lpBuffer+1)==0x16 &&
	    *(lpBuffer+2)==0x19 && *(lpBuffer+3)==0x70 &&
	    *(lpBuffer+4)==0x00 && *(lpBuffer+5)==0x00 &&
	    *(lpBuffer+6)==0x00) { /* If magic number appears at the start */
		ret_type = *(lpBuffer+7);
		switch (*(lpBuffer+7)) {
		case GFS2_METATYPE_SB:   /* 1 */
			print_gfs2("(superblock)");
			if (gfs1)
				struct_len = sizeof(struct gfs_sb);
			else
				struct_len = sizeof(struct gfs2_sb);
			break;
		case GFS2_METATYPE_RG:   /* 2 */
			print_gfs2("(rsrc grp hdr)");
			struct_len = sizeof(struct gfs2_rgrp);
			break;
		case GFS2_METATYPE_RB:   /* 3 */
			print_gfs2("(rsrc grp bitblk)");
			struct_len = 512;
			break;
		case GFS2_METATYPE_DI:   /* 4 */
			print_gfs2("(disk inode)");
			struct_len = sizeof(struct gfs2_dinode);
			break;
		case GFS2_METATYPE_IN:   /* 5 */
			print_gfs2("(indir inode blklst)");
			struct_len = sizeof(struct gfs2_meta_header);
			break;
		case GFS2_METATYPE_LF:   /* 6 */
			print_gfs2("(leaf dinode blklst)");
			struct_len = sizeof(struct gfs2_leaf);
			break;
		case GFS2_METATYPE_JD:
			print_gfs2("(journal data)");
			struct_len = sizeof(struct gfs2_meta_header);
			break;
		case GFS2_METATYPE_LH:
			print_gfs2("(log header)");
			struct_len = sizeof(struct gfs2_log_header);
			break;
		case GFS2_METATYPE_LD:
			print_gfs2("(log descriptor)");
			struct_len = sizeof(struct gfs2_log_descriptor);
			break;
		case GFS2_METATYPE_EA:
			print_gfs2("(extended attr hdr)");
			struct_len = sizeof(struct gfs2_ea_header);
			break;
		case GFS2_METATYPE_ED:
			print_gfs2("(extended attr data)");
			struct_len = 512;
			break;
		default:
			print_gfs2("(wtf?)");
			struct_len = 512;
			break;
		}
	}
	else
		struct_len = 512;
	eol(0);
	if (termlines && dmode == HEX_MODE) {
		/* calculate how much of the buffer we can fit on screen */
		screen_chunk_size = ((termlines - 4) * 16) >> 8 << 8;
		if (!screen_chunk_size)
			screen_chunk_size = 256;
		print_gfs2("(p.%d of %d)", (offset / screen_chunk_size) + 1,
				   (bufsize % screen_chunk_size) > 0 ? 
				   bufsize / screen_chunk_size + 1 : bufsize / 
				   screen_chunk_size);
		/*eol(9);*/
	}
	if (block == sbd.sd_sb.sb_root_dir.no_addr)
		print_gfs2("-------------------- Root directory ------------------");
	else if (!gfs1 && block == sbd.sd_sb.sb_master_dir.no_addr)
		print_gfs2("------------------- Master directory -----------------");
	else if (!gfs1 && block == RGLIST_DUMMY_BLOCK)
		print_gfs2("----------------------- RG List ----------------------");
	else {
		if (gfs1) {
			if (block == sbd1->sb_rindex_di.no_addr)
				print_gfs2("--------------------- rindex file -------------------");
			else if (block == gfs1_quota_di.no_addr)
				print_gfs2("--------------------- Quota file --------------------");
			else if (block == sbd1->sb_jindex_di.no_addr)
				print_gfs2("-------------------- Journal Index ------------------");
			else if (block == gfs1_license_di.no_addr)
				print_gfs2("-------------------- License file -------------------");
		}
		else {
			int d;

			for (d = 2; d < 8; d++) {
				if (block == masterdir.dirent[d].block) {
					if (!strncmp(masterdir.dirent[d].filename, "jindex", 6))
						print_gfs2("-------------------- Journal Index ------------------");
					else if (!strncmp(masterdir.dirent[d].filename, "per_node", 8))
						print_gfs2("-------------------- Per-node Dir -------------------");
					else if (!strncmp(masterdir.dirent[d].filename, "inum", 4))
						print_gfs2("--------------------- Inum file ---------------------");
					else if (!strncmp(masterdir.dirent[d].filename, "statfs", 6))
						print_gfs2("--------------------- statfs file -------------------");
					else if (!strncmp(masterdir.dirent[d].filename, "rindex", 6))
						print_gfs2("--------------------- rindex file -------------------");
					else if (!strncmp(masterdir.dirent[d].filename, "quota", 5))
						print_gfs2("--------------------- Quota file --------------------");
				}
			}
		}
	}
	eol(0);
	return ret_type;
}

/* ------------------------------------------------------------------------ */
/* hexdump - hex dump the filesystem block to the screen                    */
/* ------------------------------------------------------------------------ */
int hexdump(uint64_t startaddr, const char *lpBuffer, int len)
{
	const unsigned char *pointer,*ptr2;
	int i;
	uint64_t l;

	strcpy(edit_fmt,"%02X");
	pointer = (unsigned char *)lpBuffer + offset;
	ptr2 = (unsigned char *)lpBuffer + offset;
	l = offset;
	print_entry_ndx = 0;
	while (((termlines &&
			line < termlines &&
			line <= ((screen_chunk_size / 16) + 2)) ||
			(!termlines && l < len)) &&
		   l < bufsize) {
		if (termlines) {
			move(line, 0);
			COLORS_OFFSETS; /* cyan for offsets */
		}
		if (startaddr < 0xffffffff)
			print_gfs2("%.8"PRIX64, startaddr + l);
		else
			print_gfs2("%.16"PRIX64, startaddr + l);
		if (termlines) {
			if (l < struct_len)
				COLORS_NORMAL; /* normal part of the structure */
			else if (gfs2_struct_type == GFS2_METATYPE_DI && 
					 l < struct_len + di.di_size)
				COLORS_CONTENTS; /* after struct but not eof */
			else
				COLORS_SPECIAL; /* beyond the end of the structure */
		}
		for (i = 0; i < 16; i++) { /* first print it in hex */
			if (termlines) {
				if (l + i < struct_len)
					COLORS_NORMAL; /* normal part of the structure */
				else if (gfs2_struct_type == GFS2_METATYPE_DI && 
						 l + i < struct_len + di.di_size)
					COLORS_CONTENTS; /* beyond structure but not eof */
				else
					COLORS_SPECIAL; /* past end of the structure */
			}
			if (i%4 == 0)
				print_gfs2(" ");
			if (termlines && line == edit_row[dmode] + 3 &&
				i == edit_col[dmode]) {
				COLORS_HIGHLIGHT; /* normal part of the structure */
				memset(estring,0,3);
				sprintf(estring,"%02X",*pointer);
			}
			print_gfs2("%02X",*pointer);
			if (termlines && line == edit_row[dmode] + 3 &&
				i == edit_col[dmode]) {
				if (l < struct_len + offset)
					COLORS_NORMAL; /* normal part of the structure */
				else
					COLORS_SPECIAL; /* beyond end of the structure */
			}
			pointer++;
		}
		print_gfs2(" [");
		for (i=0; i<16; i++) { /* now print it in character format */
			if ((*ptr2 >=' ') && (*ptr2 <= '~'))
				print_gfs2("%c",*ptr2);
			else
				print_gfs2(".");
			ptr2++;
		}
		print_gfs2("] ");
		if (line - 3 > last_entry_onscreen[dmode])
			last_entry_onscreen[dmode] = line - 3;
		eol(0);
		l+=16;
		print_entry_ndx++;
	} /* while */
	if (gfs1) {
		COLORS_NORMAL;
		print_gfs2("         *** This seems to be a GFS-1 file system ***");
		eol(0);
	}
	return (offset+len);
}/* hexdump */

/* ------------------------------------------------------------------------ */
/* masterblock - find a file (by name) in the master directory and return   */
/*               its block number.                                          */
/* ------------------------------------------------------------------------ */
uint64_t masterblock(const char *fn)
{
	int d;
	
	for (d = 2; d < 8; d++)
		if (!strncmp(masterdir.dirent[d].filename, fn, strlen(fn)))
			return (masterdir.dirent[d].block);
	return 0;
}

/* ------------------------------------------------------------------------ */
/* parse_rindex - print the rgindex file.                                   */
/* ------------------------------------------------------------------------ */
int parse_rindex(struct gfs2_inode *di, int print_rindex)
{
	int error, start_line;
	struct gfs2_rindex ri;
	char buf[sizeof(struct gfs2_rindex)];
	char highlighted_addr[32];

	start_line = line;
	error = 0;
	print_gfs2("RG index entries found: %d.",
			   di->i_di.di_size / sizeof(struct gfs2_rindex));
	eol(0);
	lines_per_row[dmode] = 6;
	memset(highlighted_addr, 0, sizeof(highlighted_addr));
	for (print_entry_ndx=0; ; print_entry_ndx++) {
		error = gfs2_readi(di, (void *)&buf,
				   print_entry_ndx * sizeof(struct gfs2_rindex),
				   sizeof(struct gfs2_rindex));
		gfs2_rindex_in(&ri, buf);
		if (!error) /* end of file */
			break;
		if (!termlines ||
			(print_entry_ndx >= start_row[dmode] &&
			 ((print_entry_ndx - start_row[dmode])+1) * lines_per_row[dmode] <=
			 termlines - start_line - 2)) {
			if (edit_row[dmode] == print_entry_ndx) {
				COLORS_HIGHLIGHT;
				sprintf(highlighted_addr, "%" PRIx64,
					ri.ri_addr);
			}
			print_gfs2("RG #%d", print_entry_ndx);
			if (!print_rindex)
				print_gfs2(" located at: %llu (0x%llx)",
					   ri.ri_addr, ri.ri_addr);
			eol(0);
			if (edit_row[dmode] == print_entry_ndx)
				COLORS_NORMAL;
			if(print_rindex)
				gfs2_rindex_print(&ri);
			else {
				struct gfs2_rgrp rg;
				struct gfs2_buffer_head *tmp_bh;

				tmp_bh = bread(&sbd, ri.ri_addr);
				gfs2_rgrp_in(&rg, tmp_bh->b_data);
				gfs2_rgrp_print(&rg);
				brelse(tmp_bh, not_updated);
			}
			last_entry_onscreen[dmode] = print_entry_ndx;
		}
	}
	strcpy(estring, highlighted_addr);
	end_row[dmode] = print_entry_ndx;
	return error;
}

/* ------------------------------------------------------------------------ */
/* gfs_jindex_in - read in a gfs1 jindex structure.                         */
/* ------------------------------------------------------------------------ */
void gfs_jindex_in(struct gfs_jindex *jindex, char *buf)
{
        struct gfs_jindex *str = (struct gfs_jindex *) buf;

        jindex->ji_addr = be64_to_cpu(str->ji_addr);
        jindex->ji_nsegment = be32_to_cpu(str->ji_nsegment);
        jindex->ji_pad = be32_to_cpu(str->ji_pad);
        memcpy(jindex->ji_reserved, str->ji_reserved, 64);
}

/* ------------------------------------------------------------------------ */
/* gfs_jindex_print - print an jindex entry.                                */
/* ------------------------------------------------------------------------ */
void gfs_jindex_print(struct gfs_jindex *ji)
{
        pv(ji, ji_addr, "%llu", "0x%llx");
        pv(ji, ji_nsegment, "%u", "0x%x");
        pv(ji, ji_pad, "%u", "0x%x");
}

/* ------------------------------------------------------------------------ */
/* print_jindex - print the jindex file.                                    */
/* ------------------------------------------------------------------------ */
int print_jindex(struct gfs2_inode *di)
{
	int error, start_line;
	struct gfs_jindex ji;
	char buf[sizeof(struct gfs_jindex)];

	start_line = line;
	error = 0;
	print_gfs2("Journal index entries found: %d.",
		   di->i_di.di_size / sizeof(struct gfs_jindex));
	eol(0);
	lines_per_row[dmode] = 6;
	for (print_entry_ndx=0; ; print_entry_ndx++) {
		error = gfs2_readi(di, (void *)&buf,
				   print_entry_ndx*sizeof(struct gfs_jindex),
				   sizeof(struct gfs_jindex));
		gfs_jindex_in(&ji, buf);
		if (!error) /* end of file */
			break;
		if (!termlines ||
		    (print_entry_ndx >= start_row[dmode] &&
		     ((print_entry_ndx - start_row[dmode])+1) *
		     lines_per_row[dmode] <= termlines - start_line - 2)) {
			if (edit_row[dmode] == print_entry_ndx) {
				COLORS_HIGHLIGHT;
				sprintf(estring, "%" PRIx64, ji.ji_addr);
			}
			print_gfs2("Journal #%d", print_entry_ndx);
			eol(0);
			if (edit_row[dmode] == print_entry_ndx)
				COLORS_NORMAL;
			gfs_jindex_print(&ji);
			last_entry_onscreen[dmode] = print_entry_ndx;
		}
	}
	end_row[dmode] = print_entry_ndx;
	return error;
}

/* ------------------------------------------------------------------------ */
/* print_inum - print the inum file.                                        */
/* ------------------------------------------------------------------------ */
int print_inum(struct gfs2_inode *di)
{
	uint64_t buf, inodenum;
	
	if (gfs2_readi(di, (void *)&buf, 0, sizeof(buf)) != sizeof(buf)) {
		print_gfs2("Error reading inum file.");
		eol(0);
		return -1;
	}
	inodenum = be64_to_cpu(buf);
	print_gfs2("Next inode num = %lld (0x%llx)", inodenum, inodenum);
	eol(0);
	return 0;
}

/* ------------------------------------------------------------------------ */
/* print_statfs - print the statfs file.                                    */
/* ------------------------------------------------------------------------ */
int print_statfs(struct gfs2_inode *di)
{
	struct gfs2_statfs_change buf, sfc;
	
	if (gfs2_readi(di, (void *)&buf, 0, sizeof(buf)) !=	sizeof(buf)) {
		print_gfs2("Error reading statfs file.");
		eol(0);
		return -1;
	}
	gfs2_statfs_change_in(&sfc, (char *)&buf);
	print_gfs2("statfs file contents:");
	eol(0);
	gfs2_statfs_change_print(&sfc);
	return 0;
}

/* ------------------------------------------------------------------------ */
/* print_quota - print the quota file.                                      */
/* ------------------------------------------------------------------------ */
int print_quota(struct gfs2_inode *di)
{
	struct gfs2_quota buf, q;
	int i, error;
	
	print_gfs2("quota file contents:");
	eol(0);
	print_gfs2("quota entries found: %d.", di->i_di.di_size / sizeof(q));
	eol(0);
	for (i=0; ; i++) {
		error = gfs2_readi(di, (void *)&buf, i * sizeof(q), sizeof(buf));
		if (!error)
			break;
		if (error != sizeof(buf)) {
			print_gfs2("Error reading quota file.");
			eol(0);
			return -1;
		}
		gfs2_quota_in(&q, (char *)&buf);
		print_gfs2("Entry #%d", i + 1);
		eol(0);
		gfs2_quota_print(&q);
	}
	return 0;
}

/* ------------------------------------------------------------------------ */
/* has_indirect_blocks                                                      */
/* ------------------------------------------------------------------------ */
int has_indirect_blocks(void)
{
	if (indirect_blocks || gfs2_struct_type == GFS2_METATYPE_SB ||
		(gfs2_struct_type == GFS2_METATYPE_DI &&
		 (S_ISDIR(di.di_mode) || (gfs1 && di.__pad1 == GFS_FILE_DIR))))
		return TRUE;
	return FALSE;
}

/* ------------------------------------------------------------------------ */
/* print_inode_type                                                         */
/* ------------------------------------------------------------------------ */
void print_inode_type(__be16 de_type)
{
	switch(de_type) {
	case DT_UNKNOWN:
		print_gfs2("Unknown");
		break;
	case DT_REG:
		print_gfs2("File   ");
		break;
	case DT_DIR:
		print_gfs2("Dir    ");
		break;
	case DT_LNK:
		print_gfs2("Symlink");
		break;
	case DT_BLK:
		print_gfs2("BlkDev ");
		break;
	case DT_CHR:
		print_gfs2("ChrDev ");
		break;
	case DT_FIFO:
		print_gfs2("Fifo   ");
		break;
	case DT_SOCK:
		print_gfs2("Socket ");
		break;
	default:
		print_gfs2("%04x   ", de_type);
		break;
	}
}

/* ------------------------------------------------------------------------ */
/* display_indirect                                                         */
/* ------------------------------------------------------------------------ */
int display_indirect(void)
{
	int start_line, total_dirents, indir_blocks;
	int i, cur_height = -1;
	uint64_t factor[5];
	int offsets[5];

	last_entry_onscreen[dmode] = 0;
	eol(0);
	start_line = line;
	if (!has_indirect_blocks())
		return -1;

	indir_blocks = indirect_blocks;
	if (!indirect_blocks) {
		if (gfs2_struct_type == GFS2_METATYPE_SB)
			print_gfs2("The superblock has 2 directories");
		else
			print_gfs2("This directory contains %d directory entries.",
					   indirect[0].dirents);
		indir_blocks = 1; /* not really an indirect block, but treat it as one */
	}
	else {
		if (gfs2_struct_type == GFS2_METATYPE_DI) {
			if (S_ISDIR(di.di_mode))
				print_gfs2("This directory contains %d indirect blocks",
						   indirect_blocks);
			else
				print_gfs2("This inode contains %d indirect blocks",
						   indirect_blocks);
		}
		else
			print_gfs2("This indirect block contains %d indirect blocks",
					   indirect_blocks);
	}
	total_dirents = 0;
	/* Figure out multiplication factors for indirect pointers. */
	if ((indir_blocks == indirect_blocks) && !S_ISDIR(di.di_mode)) {
		memset(&offsets, 0, sizeof(offsets));
		/* See if we are on an inode or have one in history. */
		cur_height = 0;
		if (gfs2_struct_type != GFS2_METATYPE_DI) {
			cur_height = 0;
			for (i = 0; i <= blockhist && i < 5; i++) {
				offsets[i] = blockstack[(blockhist - i) % BLOCK_STACK_SIZE].edit_row[dmode];
				if (blockstack[(blockhist - i) % BLOCK_STACK_SIZE].gfs2_struct_type == GFS2_METATYPE_DI)
					break;
				cur_height++;
			}
		}
		if (cur_height >= 0) {
			/* Multiply out the max factor based on inode height.*/
			/* This is how much data is represented by each      */
			/* indirect pointer at each height.                  */
			factor[0] = 1ull;
			for (i = 0; i < di.di_height; i++)
				factor[i + 1] = factor[i] * (gfs1 ? 501 : 509);
		}
		print_gfs2("  (at height=%d)", cur_height);
	}
	if (indirect_blocks) {
		eol(0);
		print_gfs2("Indirect blocks:");
	}
	eol(0);
	for (print_entry_ndx = start_row[dmode];
		 (!termlines || print_entry_ndx < termlines - start_line - 2
		  + start_row[dmode]) && print_entry_ndx < indir_blocks;
		 print_entry_ndx++) {
		if (termlines) {
			if (edit_row[dmode] >= 0 &&
				line - start_line - 2 == edit_row[dmode] -
				start_row[dmode])
				COLORS_HIGHLIGHT;
			move(line, 1);
		}
		if (indir_blocks == indirect_blocks) {
			print_gfs2("%d => ", print_entry_ndx);
			if (termlines)
				move(line,9);
			print_gfs2("0x%llx / %lld", indirect[print_entry_ndx].block,
					   indirect[print_entry_ndx].block);
			if (termlines) {
				if (edit_row[dmode] >= 0 &&
					line - start_line - 2 == edit_row[dmode] -
					start_row[dmode]) { 
					sprintf(estring, "%"PRIx64,
							indirect[print_entry_ndx].block);
					strcpy(edit_fmt, "%"PRIx64);
					edit_size[dmode] = strlen(estring);
					COLORS_NORMAL;
				}
			}
			if (!S_ISDIR(di.di_mode)) {
				int hgt;
				uint64_t file_offset = 0ull;
				float human_off;
				char h;
				
				/* Now divide by how deep we are at the moment.      */
				/* This is how much data is represented by each      */
				/* indirect pointer for each height we've traversed. */
				offsets[0] = print_entry_ndx;
				for (hgt = cur_height; hgt >= 0; hgt--)
					file_offset += offsets[cur_height - hgt] *
						factor[di.di_height - hgt - 1] * bufsize;
				print_gfs2("     ");
				h = 'K';
				human_off = (file_offset / 1024.0);
				if (human_off > 1024.0) { h = 'M'; human_off /= 1024.0; }
				if (human_off > 1024.0) { h = 'G'; human_off /= 1024.0; }
				if (human_off > 1024.0) { h = 'T'; human_off /= 1024.0; }
				if (human_off > 1024.0) { h = 'P'; human_off /= 1024.0; }
				if (human_off > 1024.0) { h = 'E'; human_off /= 1024.0; }
				print_gfs2("(data offset 0x%llx / %lld / %6.2f%c)",
						   file_offset, file_offset, human_off, h);
				print_gfs2("   ");
			}
		}
		if (indirect[print_entry_ndx].is_dir) {
			int d;
			
			if (indirect[print_entry_ndx].dirents > 1 &&
				indir_blocks == indirect_blocks)
				print_gfs2("(directory leaf with %d entries)",
						   indirect[print_entry_ndx].dirents);
			for (d = 0; d < indirect[print_entry_ndx].dirents; d++) {
				total_dirents++;
				if (indirect[print_entry_ndx].dirents > 1) {
					eol(5);
					if (termlines) {
						if (edit_row[dmode] >=0 &&
							line - start_line - 2 == 
							edit_row[dmode] -
							start_row[dmode]) {
							COLORS_HIGHLIGHT;
							sprintf(estring, "%"PRIx64,
									indirect[print_entry_ndx].dirent[d].block);
							strcpy(edit_fmt, "%"PRIx64);
						}
					}
					print_gfs2("%d. (%d). %lld (0x%llx) / %lld (0x%llx): ",
							   total_dirents, d + 1,
							   indirect[print_entry_ndx].dirent[d].dirent.de_inum.no_formal_ino,
							   indirect[print_entry_ndx].dirent[d].dirent.de_inum.no_formal_ino,
							   indirect[print_entry_ndx].dirent[d].block,
							   indirect[print_entry_ndx].dirent[d].block);
				}
				print_inode_type(indirect[print_entry_ndx].dirent[d].dirent.de_type);
				print_gfs2(" %s", indirect[print_entry_ndx].dirent[d].filename);
				if (termlines) {
					if (edit_row[dmode] >= 0 &&
						line - start_line - 2 == edit_row[dmode] -
						start_row[dmode])
						COLORS_NORMAL;
				}
			}
		} /* if isdir */
		eol(0);
	} /* for each display row */
	if (line >= 7) /* 7 because it was bumped at the end */
		last_entry_onscreen[dmode] = line - 7;
	eol(0);
	end_row[dmode] = (indirect_blocks ? indirect_blocks:indirect[0].dirents);
	if (end_row[dmode] < last_entry_onscreen[dmode])
		end_row[dmode] = last_entry_onscreen[dmode];
	lines_per_row[dmode] = 1;
	return 0;
}

/* ------------------------------------------------------------------------ */
/* block_is_rindex                                                          */
/* ------------------------------------------------------------------------ */
int block_is_rindex(void)
{
	if ((gfs1 && block == sbd1->sb_rindex_di.no_addr) ||
	    (block == masterblock("rindex")))
		return TRUE;
	return FALSE;
}

/* ------------------------------------------------------------------------ */
/* block_is_rglist - there's no such block as the rglist.  This is a        */
/*                   special case meant to parse the rindex and follow the  */
/*                   blocks to the real rgs.                                */
/* ------------------------------------------------------------------------ */
int block_is_rglist(void)
{
	if (block == RGLIST_DUMMY_BLOCK)
		return TRUE;
	return FALSE;
}

/* ------------------------------------------------------------------------ */
/* block_is_jindex                                                          */
/* ------------------------------------------------------------------------ */
int block_is_jindex(void)
{
	if ((gfs1 && block == sbd1->sb_jindex_di.no_addr))
		return TRUE;
	return FALSE;
}

/* ------------------------------------------------------------------------ */
/* block_is_inum_file                                                       */
/* ------------------------------------------------------------------------ */
int block_is_inum_file(void)
{
	if (!gfs1 && block == masterblock("inum"))
		return TRUE;
	return FALSE;
}

/* ------------------------------------------------------------------------ */
/* block_is_statfs_file                                                     */
/* ------------------------------------------------------------------------ */
int block_is_statfs_file(void)
{
	if (!gfs1 && block == masterblock("statfs"))
		return TRUE;
	return FALSE;
}

/* ------------------------------------------------------------------------ */
/* block_is_quota_file                                                      */
/* ------------------------------------------------------------------------ */
int block_is_quota_file(void)
{
	if ((gfs1 && block == gfs1_quota_di.no_addr) ||
	    (block == masterblock("quota")))
		return TRUE;
	return FALSE;
}

/* ------------------------------------------------------------------------ */
/* block_has_extended_info                                                  */
/* ------------------------------------------------------------------------ */
int block_has_extended_info(void)
{
	if (has_indirect_blocks() ||
	    block_is_rindex() ||
	    block_is_rglist() ||
	    block_is_jindex() ||
	    block_is_inum_file() ||
	    block_is_statfs_file() ||
	    block_is_quota_file())
		return TRUE;
	return FALSE;
}

/* ------------------------------------------------------------------------ */
/* display_extended                                                         */
/* ------------------------------------------------------------------------ */
int display_extended(void)
{
	struct gfs2_inode *tmp_inode;
	struct gfs2_buffer_head *tmp_bh;

	/* Display any indirect pointers that we have. */
	if (display_indirect() == 0)
		return -1;
	else if (block_is_rindex()) {
		tmp_bh = bread(&sbd, block);
		tmp_inode = inode_get(&sbd, tmp_bh);
		parse_rindex(tmp_inode, TRUE);
		brelse(tmp_bh, not_updated);
	}
	else if (block_is_rglist()) {
		tmp_bh = bread(&sbd, masterblock("rindex"));
		tmp_inode = inode_get(&sbd, tmp_bh);
		parse_rindex(tmp_inode, FALSE);
		brelse(tmp_bh, not_updated);
	}
	else if (block_is_jindex()) {
		tmp_bh = bread(&sbd, block);
		tmp_inode = inode_get(&sbd, tmp_bh);
		print_jindex(tmp_inode);
		brelse(tmp_bh, not_updated);
	}
	else if (block_is_inum_file()) {
		tmp_bh = bread(&sbd, block);
		tmp_inode = inode_get(&sbd, tmp_bh);
		print_inum(tmp_inode);
		brelse(tmp_bh, not_updated);
	}
	else if (block_is_statfs_file()) {
		tmp_bh = bread(&sbd, block);
		tmp_inode = inode_get(&sbd, tmp_bh);
		print_statfs(tmp_inode);
		brelse(tmp_bh, not_updated);
	}
	else if (block_is_quota_file()) {
		tmp_bh = bread(&sbd, block);
		tmp_inode = inode_get(&sbd, tmp_bh);
		print_quota(tmp_inode);
		brelse(tmp_bh, not_updated);
	}
	return 0;
}

/* ------------------------------------------------------------------------ */
/* read_superblock - read the superblock                                    */
/* ------------------------------------------------------------------------ */
void read_superblock(void)
{
	int x;

	sbd1 = (struct gfs_sb *)&sbd.sd_sb;
	ioctl(fd, BLKFLSBUF, 0);
	do_lseek(fd, 0x10 * 4096);
	do_read(fd, buf, bufsize); /* read in the desired block */
	memset(&sbd, 0, sizeof(struct gfs2_sbd));
	sbd.device_fd = fd;
	sbd.bsize = GFS2_DEFAULT_BSIZE;
	sbd.jsize = GFS2_DEFAULT_JSIZE;
	sbd.rgsize = GFS2_DEFAULT_RGSIZE;
	sbd.utsize = GFS2_DEFAULT_UTSIZE;
	sbd.qcsize = GFS2_DEFAULT_QCSIZE;
	sbd.time = time(NULL);
	osi_list_init(&sbd.rglist);
	osi_list_init(&sbd.buf_list);
	for (x = 0; x < BUF_HASH_SIZE; x++)
		osi_list_init(&sbd.buf_hash[x]);
	compute_constants(&sbd);
	gfs2_sb_in(&sbd.sd_sb, buf); /* parse it out into the sb structure */
	/* Check to see if this is really gfs1 */
	if (sbd1->sb_fs_format == GFS_FORMAT_FS &&
		sbd1->sb_header.mh_type == GFS_METATYPE_SB &&
		sbd1->sb_header.mh_format == GFS_FORMAT_SB &&
		sbd1->sb_multihost_format == GFS_FORMAT_MULTI) {
		struct gfs_sb *sbbuf = (struct gfs_sb *)buf;

		gfs1 = TRUE;
		gfs2_inum_in(&sbd1->sb_rindex_di, (void *)&sbbuf->sb_rindex_di);
		gfs2_inum_in(&gfs1_quota_di, (void *)&sbbuf->sb_quota_di);
		gfs2_inum_in(&gfs1_license_di, (void *)&sbbuf->sb_license_di);
	}
	else
		gfs1 = FALSE;
	bufsize = sbd.sd_sb.sb_bsize;
	block = 0x10 * (4096 / bufsize);
}

/* ------------------------------------------------------------------------ */
/* read_master_dir - read the master directory                              */
/* ------------------------------------------------------------------------ */
void read_master_dir(void)
{
	ioctl(fd, BLKFLSBUF, 0);
	do_lseek(fd, sbd.sd_sb.sb_master_dir.no_addr * bufsize);
	do_read(fd, buf, bufsize); /* read in the desired block */
	gfs2_dinode_in(&di, buf); /* parse disk inode into structure */
	do_dinode_extended(&di, buf); /* get extended data, if any */
	memcpy(&masterdir, &indirect[0], sizeof(struct indirect_info));
}

/* ------------------------------------------------------------------------ */
/* display                                                                  */
/* ------------------------------------------------------------------------ */
int display(int identify_only)
{
	uint64_t blk;

	if (block == RGLIST_DUMMY_BLOCK)
		blk = masterblock("rindex");
	else
		blk = block;
	if (termlines) {
		display_title_lines();
		move(2,0);
	}
	if (block_in_mem != blk) { /* If we changed blocks from the last read */
		dev_offset = blk * bufsize;
		ioctl(fd, BLKFLSBUF, 0);
		do_lseek(fd, dev_offset);
		do_read(fd, buf, bufsize); /* read in the desired block */
		block_in_mem = blk; /* remember which block is in memory */
	}
	line = 1;
	gfs2_struct_type = display_block_type(buf);
	if (identify_only)
		return 0;
	indirect_blocks = 0;
	lines_per_row[dmode] = 1;
	if (gfs2_struct_type == GFS2_METATYPE_SB || blk == 0x10 * (4096 / bufsize)) {
		gfs2_sb_in(&sbd.sd_sb, buf); /* parse it out into the sb structure */
		memset(&indirect, 0, sizeof(indirect));
		indirect[0].block = sbd.sd_sb.sb_master_dir.no_addr;
		indirect[0].is_dir = TRUE;
		indirect[0].dirents = 2;

		memcpy(&indirect[0].dirent[0].filename, "root", 4);
		indirect[0].dirent[0].dirent.de_inum.no_formal_ino =
			sbd.sd_sb.sb_root_dir.no_formal_ino;
		indirect[0].dirent[0].dirent.de_inum.no_addr =
			sbd.sd_sb.sb_root_dir.no_addr;
		indirect[0].dirent[0].block = sbd.sd_sb.sb_root_dir.no_addr;
		indirect[0].dirent[0].dirent.de_type = DT_DIR;

		memcpy(&indirect[0].dirent[1].filename, "master", 7);
		indirect[0].dirent[1].dirent.de_inum.no_formal_ino = 
			sbd.sd_sb.sb_master_dir.no_formal_ino;
		indirect[0].dirent[1].dirent.de_inum.no_addr =
			sbd.sd_sb.sb_master_dir.no_addr;
		indirect[0].dirent[1].block = sbd.sd_sb.sb_master_dir.no_addr;
		indirect[0].dirent[1].dirent.de_type = DT_DIR;
	}
	else if (gfs2_struct_type == GFS2_METATYPE_DI) {
		gfs2_dinode_in(&di, buf); /* parse disk inode into structure */
		do_dinode_extended(&di, buf); /* get extended data, if any */
	}
	else if (gfs2_struct_type == GFS2_METATYPE_IN) { /* indirect block list */
		do_indirect_extended(buf);
	}
	else if (gfs2_struct_type == GFS2_METATYPE_LF) { /* directory leaf */
		int x;
		struct gfs2_dirent de;

		indirect_blocks = 1;
		memset(&indirect, 0, sizeof(indirect));
		/* Directory Entries: */
		for (x = sizeof(struct gfs2_leaf); x < bufsize;
		     x += de.de_rec_len) {
			gfs2_dirent_in(&de, buf + x);
			if (de.de_inum.no_addr) {
				indirect[indirect_blocks].block = de.de_inum.no_addr;
				indirect[indirect_blocks].dirent[x].block = de.de_inum.no_addr;
				memcpy(&indirect[indirect_blocks].dirent[x].dirent, &de,
					   sizeof(struct gfs2_dirent));
				memcpy(&indirect[indirect_blocks].dirent[x].filename,
					   buf + x + sizeof(struct gfs2_dirent), de.de_name_len);
				indirect[indirect_blocks].is_dir = TRUE;
				indirect[indirect_blocks].dirents++;
			}
			if (de.de_rec_len <= sizeof(struct gfs2_dirent))
				break;
		}
	}
	last_entry_onscreen[dmode] = 0;
	if (dmode == EXTENDED_MODE && !block_has_extended_info())
		dmode = HEX_MODE;
	if (termlines) {
		move(termlines, 63);
		if (dmode==HEX_MODE)
			printw("Mode: Hex %s", (editing?"edit ":"view "));
		else
			printw("Mode: %s", (dmode==GFS2_MODE?"Structure":
					    "Pointers "));
		move(line, 0);
	}
	if (dmode == HEX_MODE)          /* if hex display mode           */
		hexdump(dev_offset, buf,
			(gfs2_struct_type == GFS2_METATYPE_DI)?
			struct_len + di.di_size:bufsize);
	else if (dmode == GFS2_MODE)    /* if structure display          */
		display_gfs2();            /* display the gfs2 structure    */
	else
		display_extended();        /* display extended blocks       */
	/* No else here because display_extended can switch back to hex mode */
	if (termlines)
		refresh();
	return(0);
}

/* ------------------------------------------------------------------------ */
/* push_block - push a block onto the block stack                           */
/* ------------------------------------------------------------------------ */
void push_block(uint64_t blk)
{
	int i, bhst;

	bhst = blockhist % BLOCK_STACK_SIZE;
	if (blk) {
		blockstack[bhst].dmode = dmode;
		for (i = 0; i < DMODES; i++) {
			blockstack[bhst].start_row[i] = start_row[i];
			blockstack[bhst].end_row[i] = end_row[i];
			blockstack[bhst].edit_row[i] = edit_row[i];
			blockstack[bhst].edit_col[i] = edit_col[i];
			blockstack[bhst].lines_per_row[i] = lines_per_row[i];
		}
		blockstack[bhst].gfs2_struct_type = gfs2_struct_type;
		blockhist++;
		blockstack[blockhist % BLOCK_STACK_SIZE].block = blk;
	}
}

/* ------------------------------------------------------------------------ */
/* pop_block - pop a block off the block stack                              */
/* ------------------------------------------------------------------------ */
uint64_t pop_block(void)
{
	int i, bhst;

	if (!blockhist)
		return block;
	blockhist--;
	bhst = blockhist % BLOCK_STACK_SIZE;
	dmode = blockstack[bhst].dmode;
	for (i = 0; i < DMODES; i++) {
		start_row[i] = blockstack[bhst].start_row[i];
		end_row[i] = blockstack[bhst].end_row[i];
		edit_row[i] = blockstack[bhst].edit_row[i];
		edit_col[i] = blockstack[bhst].edit_col[i];
		lines_per_row[i] = blockstack[bhst].lines_per_row[i];
	}
	gfs2_struct_type = blockstack[bhst].gfs2_struct_type;
	return blockstack[bhst].block;
}

/* ------------------------------------------------------------------------ */
/* goto_block - go to a desired block entered by the user                   */
/* ------------------------------------------------------------------------ */
uint64_t goto_block(void)
{
	char string[256];
	int ch;

	memset(string, 0, sizeof(string));
	sprintf(string,"%"PRId64, block);
	if (bobgets(string, 1, 7, 16, &ch)) {
		if (!strcmp(string,"root"))
			temp_blk = sbd.sd_sb.sb_root_dir.no_addr;
		else if (!strcmp(string,"master")) {
			if (!gfs1)
				temp_blk = sbd.sd_sb.sb_master_dir.no_addr;
			else
				; /* maybe put out an error message at some point */
		}
		else if (isalpha(string[0])) {
			if (gfs1) {
				if (!strcmp(string, "jindex"))
					temp_blk = sbd1->sb_jindex_di.no_addr;
				else if (!strcmp(string, "rindex"))
					temp_blk = sbd1->sb_rindex_di.no_addr;
				else if (!strcmp(string, "quota"))
					temp_blk = gfs1_quota_di.no_addr;
			}
			else {
				if (!strcmp(string, "rgs"))
					temp_blk = RGLIST_DUMMY_BLOCK;
				else
					temp_blk = masterblock(string);
			}
		}
		else if (string[0] == '0' && string[1] == 'x')
			sscanf(string, "%"SCNx64, &temp_blk); /* retrieve in hex */
		else
			sscanf(string, "%" PRIu64, &temp_blk); /* retrieve decimal */

		if (temp_blk == RGLIST_DUMMY_BLOCK || temp_blk < max_block) {
			offset = 0;
			block = temp_blk;
			push_block(block);
		}
	}
	return block;
}

/* ------------------------------------------------------------------------ */
/* init_colors                                                              */
/* ------------------------------------------------------------------------ */
void init_colors()
{

	if (color_scheme) {
		init_pair(COLOR_TITLE, COLOR_BLACK,  COLOR_CYAN);
		init_pair(COLOR_NORMAL, COLOR_WHITE,  COLOR_BLACK);
		init_pair(COLOR_INVERSE, COLOR_BLACK,  COLOR_WHITE);
		init_pair(COLOR_SPECIAL, COLOR_RED,    COLOR_BLACK);
		init_pair(COLOR_HIGHLIGHT, COLOR_GREEN, COLOR_BLACK);
		init_pair(COLOR_OFFSETS, COLOR_CYAN,   COLOR_BLACK);
		init_pair(COLOR_CONTENTS, COLOR_YELLOW, COLOR_BLACK);
	}
	else {
		init_pair(COLOR_TITLE, COLOR_BLACK,  COLOR_CYAN);
		init_pair(COLOR_NORMAL, COLOR_BLACK,  COLOR_WHITE);
		init_pair(COLOR_INVERSE, COLOR_WHITE,  COLOR_BLACK);
		init_pair(COLOR_SPECIAL, COLOR_RED,    COLOR_WHITE);
		init_pair(COLOR_HIGHLIGHT, COLOR_MAGENTA, COLOR_WHITE);
		init_pair(COLOR_OFFSETS, COLOR_CYAN,   COLOR_WHITE);
		init_pair(COLOR_CONTENTS, COLOR_BLUE, COLOR_WHITE);
	}
}

/* ------------------------------------------------------------------------ */
/* hex_edit - Allow the user to edit the page by entering hex digits        */
/* ------------------------------------------------------------------------ */
void hex_edit(int *exitch)
{
	int left_off;
	int ch;

	left_off = ((block * bufsize) < 0xffffffff) ? 9 : 17;
	/* 8 and 16 char addresses on screen */
	
	if (bobgets(estring, edit_row[dmode] + 3,
		    (edit_col[dmode] * 2) + (edit_col[dmode] / 4) + left_off,
		    2, exitch)) {
		if (strstr(edit_fmt,"X") || strstr(edit_fmt,"x")) {
			int hexoffset;
			int i, sl = strlen(estring);
			
			for (i = 0; i < sl; i+=2) {
				hexoffset = (edit_row[dmode] * 16) +
					edit_col[dmode] + (i / 2);
				ch = 0x00;
				if (isdigit(estring[i]))
					ch = (estring[i] - '0') * 0x10;
				else if (estring[i] >= 'a' &&
					 estring[i] <= 'f')
					ch = (estring[i]-'a' + 0x0a)*0x10;
				else if (estring[i] >= 'A' &&
					 estring[i] <= 'F')
					ch = (estring[i] - 'A' + 0x0a) * 0x10;
				if (isdigit(estring[i+1]))
					ch += (estring[i+1] - '0');
				else if (estring[i+1] >= 'a' &&
					 estring[i+1] <= 'f')
					ch += (estring[i+1] - 'a' + 0x0a);
				else if (estring[i+1] >= 'A' &&
					 estring[i+1] <= 'F')
					ch += (estring[i+1] - 'A' + 0x0a);
				buf[offset + hexoffset] = ch;
			}
			do_lseek(fd, dev_offset);
			do_write(fd, buf, bufsize);
			fsync(fd);
		}
	}
}

/* ------------------------------------------------------------------------ */
/* page up                                                                  */
/* ------------------------------------------------------------------------ */
void pageup(void)
{
	if (dmode == EXTENDED_MODE) {
		int dsplines = termlines - 6;
		
		if (edit_row[dmode] - (dsplines / lines_per_row[dmode]) > 0) {
			start_row[dmode] -= (dsplines / lines_per_row[dmode]);
			edit_row[dmode] -= (dsplines / lines_per_row[dmode]);
		}
		else {
			start_row[dmode] = 0;
			edit_row[dmode] = 0;
		}
	}
	else {
		start_row[dmode] = edit_row[dmode] = 0;
		if (dmode == GFS2_MODE || offset==0) {
			block--;
			if (dmode == HEX_MODE)
				offset = (bufsize % screen_chunk_size) > 0 ? 
					screen_chunk_size *
					(bufsize / screen_chunk_size) :
					bufsize - screen_chunk_size;
			else
				offset = 0;
		}
		else
			offset -= screen_chunk_size;
	}
}

/* ------------------------------------------------------------------------ */
/* page down                                                                */
/* ------------------------------------------------------------------------ */
void pagedn(void)
{
	if (dmode == EXTENDED_MODE) {
		int dsplines = termlines - 6;

		if ((edit_row[dmode] + dsplines) / lines_per_row[dmode] + 1 <
		    end_row[dmode]) {
			start_row[dmode] += dsplines / lines_per_row[dmode];
			edit_row[dmode] += dsplines / lines_per_row[dmode];
		}
		else
			edit_row[dmode] = end_row[dmode] - 1;
	}
	else {
		start_row[dmode] = edit_row[dmode] = 0;
		if (dmode == GFS2_MODE ||
		    offset + screen_chunk_size >= bufsize) {
			block++;
			offset = 0;
		}
		else
			offset += screen_chunk_size;
	}
}

/* ------------------------------------------------------------------------ */
/* jump - jump to the address the cursor is on                              */
/* ------------------------------------------------------------------------ */
void jump(void)
{
	if (dmode == HEX_MODE) {
		unsigned int col2;
		uint64_t *b;
		
		col2 = edit_col[dmode] & 0x08;/* thus 0-7->0, 8-15->8 */
		b = (uint64_t *)&buf[edit_row[dmode]*16 + offset + col2];
		temp_blk=be64_to_cpu(*b);
	}
	else
		sscanf(estring, "%"SCNx64, &temp_blk);/* retrieve in hex */
	if (temp_blk < max_block) { /* if the block number is valid */
		int i;
		
		offset = 0;
		block = temp_blk;
		push_block(block);
		for (i = 0; i < DMODES; i++) {
			start_row[i] = end_row[i] = edit_row[i] = 0;
			edit_col[i] = 0;
		}
	}
}

/* ------------------------------------------------------------------------ */
/* interactive_mode - accept keystrokes from user and display structures    */
/* ------------------------------------------------------------------------ */
void interactive_mode(void)
{
	int ch, Quit;

	if ((wind = initscr()) == NULL) {
		fprintf(stderr, "Error: unable to initialize screen.");
		eol(0);
		exit(-1);
	}

	/* Do our initial screen stuff: */
	signal(SIGWINCH, UpdateSize); /* handle the terminal resize signal */
	UpdateSize(0); /* update screen size based on terminal settings */
	clear(); /* don't use Erase */
	start_color();
	noecho();
	keypad(stdscr, TRUE);
	raw();
	curs_set(0);
	init_colors();
	/* Accept keystrokes and act on them accordingly */
	Quit = FALSE;
	editing = FALSE;
	while (!Quit) {
		display(FALSE);
		if (editing) {
			if (edit_row[dmode] == -1)
				block = goto_block();
			else {
				if (dmode == HEX_MODE)
					hex_edit(&ch);
				else if (dmode == GFS2_MODE)
					bobgets(estring, edit_row[dmode]+4, 24,
						edit_size[dmode], &ch);
				else
					bobgets(estring, edit_row[dmode]+6, 14,
						edit_size[dmode], &ch);
			}
		}
		else
			while ((ch=getch()) == 0); // wait for input

		switch (ch)
		{
		/* --------------------------------------------------------- */
		/* escape or 'q' */
		/* --------------------------------------------------------- */
		case 0x1b:
		case 0x03:
		case 'q':
			if (editing)
				editing = FALSE;
			else
				Quit=TRUE;
			break;
		/* --------------------------------------------------------- */
		/* home - return to the superblock                           */
		/* --------------------------------------------------------- */
		case KEY_HOME:
			if (dmode == EXTENDED_MODE) {
				start_row[dmode] = end_row[dmode] = 0;
				edit_row[dmode] = 0;
			}
			else {
				block = 0x10 * (4096 / bufsize);
				push_block(block);
				offset = 0;
			}
			break;
		/* --------------------------------------------------------- */
		/* backspace - return to the previous block on the stack     */
		/* --------------------------------------------------------- */
		case KEY_BACKSPACE:
		case 0x7f:
			block = pop_block();
			offset = 0;
			break;
		/* --------------------------------------------------------- */
		/* space - go down the block stack (opposite of backspace)   */
		/* --------------------------------------------------------- */
		case ' ':
			blockhist++;
			block = blockstack[blockhist % BLOCK_STACK_SIZE].block;
			offset = 0;
			break;
		/* --------------------------------------------------------- */
		/* arrow up */
		/* --------------------------------------------------------- */
		case KEY_UP:
			if (dmode == EXTENDED_MODE) {
				if (edit_row[dmode] > 0)
					edit_row[dmode]--;
				if (edit_row[dmode] < start_row[dmode])
					start_row[dmode] = edit_row[dmode];
			}
			else {
				if (edit_row[dmode] >= 0)
					edit_row[dmode]--;
			}
			break;
		/* --------------------------------------------------------- */
		/* arrow down */
		/* --------------------------------------------------------- */
		case KEY_DOWN:
			if (dmode == EXTENDED_MODE) {
				if (edit_row[dmode] + 1 < end_row[dmode]) {
					if (edit_row[dmode] >= last_entry_onscreen[dmode])
						start_row[dmode]++;
					edit_row[dmode]++;
				}
			}
			else {
				if (edit_row[dmode] < last_entry_onscreen[dmode])
					edit_row[dmode]++;
			}
			break;
		/* --------------------------------------------------------- */
		/* arrow left */
		/* --------------------------------------------------------- */
		case KEY_LEFT:
			if (dmode == HEX_MODE) {
				if (edit_col[dmode] > 0)
					edit_col[dmode]--;
				else
					edit_col[dmode] = 15;
			}
			break;
		/* --------------------------------------------------------- */
		/* arrow right */
		/* --------------------------------------------------------- */
		case KEY_RIGHT:
			if (dmode == HEX_MODE) {
				if (edit_col[dmode] < 15)
					edit_col[dmode]++;
				else
					edit_col[dmode] = 0;
			}
			break;
		/* --------------------------------------------------------- */
		/* m - change display mode key */
		/* --------------------------------------------------------- */
		case 'm':
			dmode = ((dmode + 1) % DMODES);
			break;
		/* --------------------------------------------------------- */
		/* J - Jump to highlighted block number */
		/* --------------------------------------------------------- */
		case 'j':
			jump();
			break;
		/* --------------------------------------------------------- */
		/* g - goto block */
		/* --------------------------------------------------------- */
		case 'g':
			block = goto_block();
			break;
		/* --------------------------------------------------------- */
		/* h - help key */
		/* --------------------------------------------------------- */
		case 'h':
			print_usage();
			break;
		/* --------------------------------------------------------- */
		/* e - change to extended mode */
		/* --------------------------------------------------------- */
		case 'e':
			dmode = EXTENDED_MODE;
			break;
		/* --------------------------------------------------------- */
		/* b - Back one 4K block */
		/* --------------------------------------------------------- */
		case 'b':
			start_row[dmode] = end_row[dmode] = edit_row[dmode] = 0;
			if (block > 0)
				block--;
			offset = 0;
			break;
		/* --------------------------------------------------------- */
		/* c - Change color scheme */
		/* --------------------------------------------------------- */
		case 'c':
			color_scheme = !color_scheme;
			init_colors();
			break;
		/* --------------------------------------------------------- */
		/* page up key */
		/* --------------------------------------------------------- */
		case 0x19:                    // ctrl-y for vt100
		case KEY_PPAGE:		      // PgUp
		case 0x15:                    // ctrl-u for vi compat.
		case 0x02:                   // ctrl-b for less compat.
			pageup();
			break;
		/* --------------------------------------------------------- */
		/* end - Jump to the end of the list */
		/* --------------------------------------------------------- */
		case 0x168:
			if (dmode == EXTENDED_MODE) {
				int dsplines = termlines - 6;
				int ents_per_screen = dsplines /
					lines_per_row[dmode];

				edit_row[dmode] = end_row[dmode] - 1;
				if ((edit_row[dmode] - ents_per_screen)+1 > 0)
					start_row[dmode] = edit_row[dmode] - 
						ents_per_screen + 1;
				else
					start_row[dmode] = 0;
			}
			/* TODO: Make end key work for other display modes. */
			break;
		/* --------------------------------------------------------- */
		/* f - Forward one 4K block */
		/* --------------------------------------------------------- */
		case 'f':
			start_row[dmode]=end_row[dmode]=edit_row[dmode] = 0;
			lines_per_row[dmode] = 1;
			block++;
			offset = 0;
			break;
		/* --------------------------------------------------------- */
		/* page down key */
		/* --------------------------------------------------------- */
		case 0x16:                    // ctrl-v for vt100
		case KEY_NPAGE:		      // PgDown
		case 0x04:                    // ctrl-d for vi compat.
			pagedn();
			break;
		/* --------------------------------------------------------- */
		/* enter key - change a value */
		/* --------------------------------------------------------- */
		case(KEY_ENTER):
		case('\n'):
		case('\r'):
			editing = !editing;
			break;
		default:
			move(termlines - 1, 0);
			printw("Keystroke not understood: 0x%03X",ch);
			refresh();
			sleep(1);
			break;
		} /* switch */
	} /* while !Quit */

    Erase();
    refresh();
    endwin();
}/* interactive_mode */

/* ------------------------------------------------------------------------ */
/* usage - print command line usage                                         */
/* ------------------------------------------------------------------------ */
void usage(void)
{
	fprintf(stderr,"\nFormat is: gfs2_edit [-c 1] [-V] [-x] [-h] [identify] [-p structures|blocks] /dev/device\n\n");
	fprintf(stderr,"If only the device is specified, it enters into hexedit mode.\n");
	fprintf(stderr,"identify - prints out only the block type, not the details.\n");
	fprintf(stderr,"-V   prints version number.\n");
	fprintf(stderr,"-c 1 selects alternate color scheme 1\n");
	fprintf(stderr,"-p   prints GFS2 structures or blocks to stdout.\n");
	fprintf(stderr,"     sb - prints the superblock.\n");
	fprintf(stderr,"     size - prints the filesystem size.\n");
	fprintf(stderr,"     master - prints the master directory.\n");
	fprintf(stderr,"     root - prints the root directory.\n");
	fprintf(stderr,"     jindex - prints the journal index directory.\n");
	fprintf(stderr,"     per_node - prints the per_node directory.\n");
	fprintf(stderr,"     inum - prints the inum file.\n");
	fprintf(stderr,"     statfs - prints the statfs file.\n");
	fprintf(stderr,"     rindex - prints the rindex file.\n");
	fprintf(stderr,"     rgs - prints all the resource groups (rgs).\n");
	fprintf(stderr,"     quota - prints the quota file.\n");
	fprintf(stderr,"-x   print in hexmode.\n");
	fprintf(stderr,"-h   prints this help.\n\n");
	fprintf(stderr,"Examples:\n");
	fprintf(stderr,"   To run in interactive mode:\n");
	fprintf(stderr,"     gfs2_edit /dev/bobs_vg/lvol0\n");
	fprintf(stderr,"   To print out the superblock and master directory:\n");
	fprintf(stderr,"     gfs2_edit -p sb master /dev/bobs_vg/lvol0\n");
	fprintf(stderr,"   To print out the master directory in hex:\n");
	fprintf(stderr,"     gfs2_edit -x -p master /dev/bobs_vg/lvol0\n");
	fprintf(stderr,"   To print out the block-type for block 0x27381:\n");
	fprintf(stderr,"     gfs2_edit identify -p 0x27381 /dev/bobs_vg/lvol0\n");
}/* usage */

/* ------------------------------------------------------------------------ */
/* process_parameters - process commandline parameters                      */
/* pass - we make two passes through the parameters; the first pass gathers */
/*        normals parameters, device name, etc.  The second pass is for     */
/*        figuring out what structures to print out.                        */
/* ------------------------------------------------------------------------ */
void process_parameters(int argc, char *argv[], int pass)
{
	int i;

	if (argc < 2) {
		usage();
		die("no device specified\n");
	}
	for (i = 1; i < argc; i++) {
		if (!pass) {
			if (!strcasecmp(argv[i], "-V")) {
				printf("%s version %s (built %s %s)\n", prog_name,
					   GFS2_RELEASE_NAME, __DATE__, __TIME__);
				printf("%s\n", REDHAT_COPYRIGHT);
				exit(0);
			}
			else if (!strcasecmp(argv[i], "-h") ||
					 !strcasecmp(argv[i], "-help") ||
					 !strcasecmp(argv[i], "-usage")) {
				usage();
				exit(0);
			}
			else if (!strcasecmp(argv[i], "-c")) {
				i++;
				color_scheme = atoi(argv[i]);
			}
			else if (!strcasecmp(argv[i], "-p") ||
					 !strcasecmp(argv[i], "-print")) {
				termlines = 0; /* initial value--we'll figure it out later */
				dmode = GFS2_MODE;
			}
			else if (strchr(argv[i],'/'))
				strcpy(device, argv[i]);
		}
		else { /* second pass */
			if (!termlines && !strchr(argv[i],'/')) { /* if print, no slash */
				if (!strcasecmp(argv[i], "-x"))
					dmode = HEX_MODE;
				else if (argv[i][0] == '-') /* if it starts with a dash */
					; /* ignore it--meant for pass == 0 */
				else if (!strcmp(argv[i], "identify"))
					identify = TRUE;
				else if (!strcmp(argv[i], "size"))
					printf("Device size: %" PRIu64 " (0x%" PRIx64 ")\n",
						   max_block, max_block);
				else if (!strcmp(argv[i], "sb") ||
						 !strcmp(argv[i], "superblock"))
					push_block(0x10 * (4096 / bufsize)); /* superblock */
				else if (!strcmp(argv[i], "root") ||
						 !strcmp(argv[i], "rootdir"))
					push_block(sbd.sd_sb.sb_root_dir.no_addr);
				else if (!strcmp(argv[i], "master")) {
					if (!gfs1)
						push_block(sbd.sd_sb.sb_master_dir.no_addr);
					else
						fprintf(stderr, "This is GFS1; there's no master directory.\n");
				}
				else if (!strcmp(argv[i], "jindex")) {
					if (gfs1)
						push_block(sbd1->sb_jindex_di.no_addr);
					else
						push_block(masterblock("jindex"));/* journal index */
				}
				else if (!gfs1 && !strcmp(argv[i], "per_node"))
					push_block(masterblock("per_node"));
				else if (!gfs1 && !strcmp(argv[i], "inum"))
					push_block(masterblock("inum"));
				else if (!gfs1 && !strcmp(argv[i], "statfs"))
					push_block(masterblock("statfs"));
				else if (!strcmp(argv[i], "rindex") ||
						 !strcmp(argv[i], "rgindex")) {
					if (gfs1)
						push_block(sbd1->sb_rindex_di.no_addr);
					else
						push_block(masterblock("rindex"));
				}
				else if (!strcmp(argv[i], "rgs")) {
					if (!gfs1)
						push_block(RGLIST_DUMMY_BLOCK);
				}
				else if (!strcmp(argv[i], "quota")) {
					if (gfs1)
						push_block(gfs1_quota_di.no_addr);
					else
						push_block(masterblock("quota"));
				}
				else if (argv[i][0]=='0' && argv[i][1]=='x') { /* hex addr */
					sscanf(argv[i], "%"SCNx64, &temp_blk);/* retrieve in hex */
					push_block(temp_blk);
				}
				else if (isdigit(argv[i][0])) { /* decimal addr */
					sscanf(argv[i], "%"SCNd64, &temp_blk);
					push_block(temp_blk);
				}
				else {
					fprintf(stderr,"I don't know what '%s' means.\n", argv[i]);
					usage();
					exit(0);
				}
			}
		}
	} /* for */
}/* process_parameters */

/******************************************************************************
*******************************************************************************
**
** main()
**
** Description:
**   Do everything
**
*******************************************************************************
******************************************************************************/
int main(int argc, char *argv[])
{
	int i, j;

	prog_name = argv[0];

	memset(start_row, 0, sizeof(start_row));
	memset(lines_per_row, 0, sizeof(lines_per_row));
	memset(end_row, 0, sizeof(end_row));
	memset(edit_row, 0, sizeof(edit_row));
	memset(edit_col, 0, sizeof(edit_col));
	memset(edit_size, 0, sizeof(edit_size));
	memset(last_entry_onscreen, 0, sizeof(last_entry_onscreen));
	dmode = HEX_MODE;
	type_alloc(buf, char, bufsize); /* allocate/malloc a new 4K buffer */
	block = 0x10;
	for (i = 0; i < BLOCK_STACK_SIZE; i++) {
		blockstack[i].dmode = dmode;
		blockstack[i].block = block;
		for (j = 0; j < DMODES; j++) {
			blockstack[i].start_row[j] = 0;
			blockstack[i].end_row[j] = 0;
			blockstack[i].edit_row[j] = 0;
			blockstack[i].edit_col[j] = 0;
			blockstack[i].lines_per_row[j] = 0;
		}
	}

	memset(device, 0, sizeof(device));
	termlines = 30;  /* assume interactive mode until we find -p */
	process_parameters(argc, argv, 0);

	fd = open(device, O_RDWR);
	if (fd < 0)
		die("can't open %s: %s\n", argv[1], strerror(errno));
	max_block = lseek(fd, 0, SEEK_END) / bufsize;

	read_superblock();
	if (!gfs1)
		read_master_dir();
	block_in_mem = -1;
	if (!termlines)    /* if printing to stdout */
		process_parameters(argc, argv, 1); /* get what to print from cmdline */

	if (termlines)
		interactive_mode();
	else { /* print all the structures requested */
		for (i = 0; i <= blockhist; i++) {
			block = blockstack[i + 1].block;
			display(identify);
			if (!identify) {
				display_extended();
				printf("-------------------------------------" \
					   "-----------------");
				eol(0);
			}
			block = pop_block();
		}
	}
	close(fd);
	if (buf)
		free(buf);
 	exit(EXIT_SUCCESS);
}
