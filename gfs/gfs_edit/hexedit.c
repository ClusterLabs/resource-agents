#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <curses.h>
#include <term.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mount.h>

#include "global.h"
#include "copyright.cf"

#define EXTERN
#include "hexedit.h"
#include "gfshex.h"
#include "gfs_ondisk.h"
#include "linux_endian.h"

#include <syslog.h>

#define TITLE1 "gfs_edit - Global File System Editor (use with extreme caution)"
#define TITLE2 "Copyright (C) 2006 Red Hat, Inc. - Press H for help"

int display(void);

/* ------------------------------------------------------------------------ */
/* UpdateSize - screen size changed, so update it                           */
/* ------------------------------------------------------------------------ */
void UpdateSize(int sig)
{
  static char term_buffer[2048];
  int rc;

  termlines=30;
  termtype=getenv("TERM");
  if (termtype==NULL)
    return;
  rc=tgetent(term_buffer,termtype);
  if (rc>=0) {
    termlines=tgetnum("li");
    if (termlines<10)
      termlines=30;
  }
  else
	  perror("Error: tgetent failed.");
  termlines--; /* last line is number of lines -1 */
  display();
  signal(SIGWINCH, UpdateSize);
}

/* ------------------------------------------------------------------------- */
/* display_title_lines */
/* ------------------------------------------------------------------------- */
void display_title_lines(void)
{
  clear();
  attrset(COLOR_PAIR(1));
  move(0, 0);
  printw("%-80s",TITLE1);
  move(termlines, 0);
  printw("%-79s",TITLE2);
  attrset(COLOR_PAIR(2));
}

/* ------------------------------------------------------------------------- */
/* bobgets - get a string                                                    */
/* returns: 1 if user exited by hitting enter                                */
/*          0 if user exited by hitting escape                               */
/* ------------------------------------------------------------------------- */
int bobgets(char string[],int x,int y,int sz)
{
	int done,ch,runningy,rc;

	move(x,y);
	done=FALSE;
	attrset(COLOR_PAIR(3));
	move(x,y);
	addstr(string);
	move(x,y);
	curs_set(2);
	refresh();
	runningy=y;
	rc=0;
	while (!done) {
		ch=getch();
		
		if(ch < 0x0100 && isprint(ch)) {
			char *p=string+strlen(string); // end of the string
			*(p+1)='\0';
			while (insert && p > &string[runningy-y]) {
				*p=*(p-1);
				p--;
			}
			string[runningy-y]=ch;
			runningy++;
			move(x,y);
			addstr(string);
		}
		else {
			// special character, is it one we recognize?
			switch(ch)
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
				if (runningy>y)
					runningy--;
				break;
			case(KEY_RIGHT):
				runningy++;
				break;
			case(KEY_DC):
			case(0x07F):
				if (runningy>=y) {
					char *p;
					p = &string[runningy-y];
					while (*p) {
						*p = *(p+1);
						p++;
					}
					*p='\0';
					runningy--;
					// remove the character from the string 
					move(x,y);
					addstr(string);
					attrset(COLOR_PAIR(2));
					addstr(" ");
					attrset(COLOR_PAIR(3));
					runningy++;
				}
				break;
			case(KEY_BACKSPACE):
				if (runningy>y) {
					char *p;

					p = &string[runningy-y-1];
					while (*p) {
						*p = *(p+1);
						p++;
					}
					*p='\0';
					runningy--;
					// remove the character from the string 
					move(x,y);
					addstr(string);
					attrset(COLOR_PAIR(2));
					addstr(" ");
					attrset(COLOR_PAIR(3));
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
				printw("%08X",ch);
				// ignore all other characters
				break;
			} // end switch on non-printable character
		} // end non-printable character
		move(x,runningy);
		refresh();
	} // while !done
	if (sz>0)
		string[sz]='\0';
	attrset(COLOR_PAIR(2));
	return rc;
}/* bobgets */

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
	clear();
	display_title_lines();
	move(line++,0);
	printw("Supported commands: (roughly conforming to the rules of 'less')");
	line++;
	move(line++,0);
	printw("   b           - Backward one 4K block");
	move(line++,0);
	printw("   f           - Forward one 4K block");
	move(line++,0);
	printw("   g           - Goto a given block in hex");
	move(line++,0);
	printw("   h           - This Help display");
	move(line++,0);
	printw("   j           - Jump to the highlighted 64-bit block number.");
	move(line++,0);
	printw("   m           - Switch display mode: hex -> GFS structure -> Extended");
	move(line++,0);
	printw("   q           - Quit (same as hitting <escape> key)");
	move(line++,0);
	printw("<space>        - Forward one 4K block (same as 'f')");
	move(line++,0);
	printw("<pg up>/<down> - move up or down one screen full");
	move(line++,0);
	printw("<up>/<down>    - move up or down one line");
	move(line++,0);
	printw("<left>/<right> - move left or right one byte");
	move(line++,0);
	printw("<home>         - return to the superblock.");
	move(line++,0);
	printw("<enter>        - edit a value (enter to save, esc to discard)");
	move(line++,0);
	printw("<backspace>    - return to previous block");
	move(line++,0);
	printw("<escape>       - Quit the program");
	move(line++,0);
	move(line++,0);
	printw("Notes: Areas shown in red are outside the bounds of the struct.");
	move(line++,0);
	printw("       Fields shown in green are selected for edit on <enter>.");
	move(line++,0);
	move(line++,0);
	printw("Press any key to return.");
	refresh();
	while ((ch=getch()) == 0); // wait for input
	clear();
}

/* ------------------------------------------------------------------------ */
/* display_block_type                                                       */
/* returns: metatype if block is a GFS structure block type                 */
/*          0 if block is not a GFS structure                               */
/* ------------------------------------------------------------------------ */
int display_block_type(const char *lpBuffer)
{
	int ret_type = 0; /* return type */

	/* first, print out the kind of GFS block this is */
	line = 1;
	move(line, 0);
	printw("Block #");
	if (edit_row[display_mode] == -1)
		attrset(COLOR_PAIR(5));;
	printw("%"PRIX64,block);
	if (edit_row[display_mode] == -1)
		attrset(COLOR_PAIR(2));
	move(line,25);
	printw("of %"PRIX64,max_block);
	move(line, 45);

	if (*(lpBuffer+0)==0x01 && *(lpBuffer+1)==0x16 && *(lpBuffer+2)==0x19 &&
		*(lpBuffer+3)==0x70 && *(lpBuffer+4)==0x00 && *(lpBuffer+5)==0x00 &&
		*(lpBuffer+6)==0x00) { /* If magic number appears at the start */
		ret_type = *(lpBuffer+7);
		switch (*(lpBuffer+7)) {
		case GFS_METATYPE_SB:   /* 1 */
			printw("(superblock)");
			struct_len = sizeof(struct gfs_sb);
			break;
		case GFS_METATYPE_RG:   /* 2 */
			printw("(rsrc grp hdr)");
			struct_len = sizeof(struct gfs_rgrp);
			break;
		case GFS_METATYPE_RB:   /* 3 */
			printw("(rsrc grp bitblk)");
			struct_len = 512;
			break;
		case GFS_METATYPE_DI:   /* 4 */
			printw("(disk inode)");
			struct_len = sizeof(struct gfs_dinode);
			break;
		case GFS_METATYPE_IN:   /* 5 */
			printw("(indir inode blklst)");
			struct_len = sizeof(struct gfs_indirect);
			break;
		case GFS_METATYPE_LF:   /* 6 */
			printw("(leaf dinode blklst)");
			struct_len = sizeof(struct gfs_leaf);
			break;
		case GFS_METATYPE_JD:
			printw("(journal data)");
			struct_len = sizeof(struct gfs_meta_header);
			break;
		case GFS_METATYPE_LH:
			printw("(log header)");
			struct_len = sizeof(struct gfs_log_header);
			break;
		case GFS_METATYPE_LD:
			printw("(log descriptor)");
			struct_len = sizeof(struct gfs_log_descriptor);
			break;
		case GFS_METATYPE_EA:
			printw("(extended attr hdr)");
			struct_len = sizeof(struct gfs_ea_header);
			break;
		case GFS_METATYPE_ED:
			printw("(extended attr data)");
			struct_len = 512;
			break;
		default:
			printw("(wtf?)");
			struct_len = 512;
			break;
		}
	}
	else
		struct_len = 512;
	line++;
	move(line, 0);
	if (display_mode == HEX_MODE) {
		/* calculate how much of the buffer we can fit on screen */
		screen_chunk_size = ((termlines - 4) * 16) >> 8 << 8;
		if (!screen_chunk_size)
			screen_chunk_size = 256;
		printw("(%d of %d)", (offset / screen_chunk_size) + 1,
			   (bufsize % screen_chunk_size) > 0 ? 
		       bufsize / screen_chunk_size + 1 : bufsize / screen_chunk_size);
	}
	move(line, 9);
	if (block == sb.sb_jindex_di.no_addr)
		printw("----------------- Journal Index file -----------------");
	else if (block == sb.sb_rindex_di.no_addr)
		printw("-------------- Resource Group Index file -------------");
	else if (block == sb.sb_quota_di.no_addr)
		printw("---------------------- Quota file --------------------");
	else if (block == sb.sb_root_di.no_addr)
		printw("-------------------- Root direcory -------------------");
	else if (block == sb.sb_license_di.no_addr)
		printw("--------------------- License file -------------------");
	line++;
	return ret_type;
}

/* ------------------------------------------------------------------------ */
/* hexdump - hex dump the filesystem block to the screen                    */
/* ------------------------------------------------------------------------ */
int hexdump(uint64 startaddr, const char *lpBuffer, int len)
{
	const unsigned char *pointer,*ptr2;
	int i;
	uint64 l;

	strcpy(edit_fmt,"%02X");
	pointer = (unsigned char *)lpBuffer + offset;
	ptr2 = (unsigned char *)lpBuffer + offset;
	l = offset;
	while (line < termlines &&
		   line <= ((screen_chunk_size / 16) + 2) &&
		   l < bufsize) {
		move(line, 0);
		attrset(COLOR_PAIR(6)); /* yellow for offsets */
		if (startaddr < 0xffffffff)
			printw("%.8"PRIX64,startaddr + l);
		else
			printw("%.16"PRIX64,startaddr + l);
		if (l < struct_len)
			attrset(COLOR_PAIR(2)); /* normal part of the structure */
		else
			attrset(COLOR_PAIR(4)); /* beyond the end of the structure */
		for (i=0; i<16; i++) { /* first print it in hex */
			if (l+i < struct_len)
				attrset(COLOR_PAIR(2)); /* normal part of the structure */
			else
				attrset(COLOR_PAIR(4)); /* beyond the end of the structure */
			if (i%4 == 0)
				printw(" ");
			if (line == edit_row[display_mode] + 3 &&
				i == edit_col[display_mode]) {
				attrset(COLOR_PAIR(5)); /* normal part of the structure */
				memset(edit_string,0,3);
				sprintf(edit_string,"%02X",*pointer);
			}
			printw("%02X",*pointer);
			if (line == edit_row[display_mode] + 3 &&
				i == edit_col[display_mode]) {
				if (l < struct_len + offset)
					attrset(COLOR_PAIR(2)); /* normal part of the structure */
				else
					attrset(COLOR_PAIR(4)); /* beyond end of the structure */
			}
			pointer++;
		}
		printw(" [");
		for (i=0; i<16; i++) { /* now print it in character format */
			if ((*ptr2 >=' ') && (*ptr2 <= '~'))
				printw("%c",*ptr2);
			else
				printw(".");
			ptr2++;
		}
		printw("] ");
		if (line - 3 > edit_last[display_mode])
			edit_last[display_mode] = line - 3;
		line++;
		l+=16;
	}
	return (offset+len);
}

/* ------------------------------------------------------------------------ */
/* display_extended                                                         */
/* ------------------------------------------------------------------------ */
int display_extended(void)
{
	int e;

	edit_last[display_mode] = 0;
	move(line++, 0);
	if (indirect_blocks) {
		printw("This inode contains %d indirect blocks", indirect_blocks);
		line++;
		move(line++, 0);
		printw("Indirect blocks for this inode:");
		for (e = 0; e < termlines && e < indirect_blocks; e++) {
			if (line - 6 == edit_row[display_mode])
				attrset(COLOR_PAIR(5));
			move(line, 5);
			printw("%d => %"PRIX64, e + 1, indirect_block[e]);
			if (line - 6 == edit_row[display_mode]) { 
				sprintf(edit_string, "%"PRIX64, indirect_block[e]);
				strcpy(edit_fmt, "%"PRIX64);
				edit_size[display_mode] = strlen(edit_string);
				attrset(COLOR_PAIR(2));
			}
			line++;
		}
		if (line >= 7) /* 7 because it was bumped at the end */
			edit_last[display_mode] = line - 7;
	}
	else
		printw("This block does not have indirect blocks.");
	return 0;
}

/* ------------------------------------------------------------------------ */
/* display                                                                  */
/* ------------------------------------------------------------------------ */
int display(void)
{
	display_title_lines();
	move(2,0);
	if (block_in_mem != block) { /* If we changed blocks from the last read */
		dev_offset = block * bufsize;
		ioctl(fd, BLKFLSBUF, 0);
		do_lseek(fd, dev_offset);
		do_read(fd, buf, bufsize); /* read in the desired block */
		block_in_mem = block; /* remember which block is in memory */
	}
	line = 1;
	gfs_struct_type = display_block_type(buf);
	indirect_blocks = 0;
	if (gfs_struct_type == GFS_METATYPE_SB || block == 0x10)
		gfs_sb_in(&sb, buf); /* parse it out into the sb structure */
	else if (gfs_struct_type == GFS_METATYPE_DI) {
		gfs_dinode_in(&di, buf); /* parse disk inode into structure */
		do_dinode_extended(&di, buf); /* get extended data, if any */
	}
	edit_last[display_mode] = 0;
	if (display_mode == HEX_MODE)          /* if hex display mode           */
		hexdump(dev_offset, buf, 256);     /* show the block in hex         */
	else if (display_mode == GFS_MODE)     /* if structure display          */
		display_gfs();                     /* display the gfs structure     */
	else                                   /* otherwise                     */
		display_extended();                /* display extended blocks       */
	refresh();
	return(0);
}


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
	char device[STRLEN];
	char string[256];
	int i,ch, left_off;
	int64 temp_blk;

	prog_name = argv[0];

	if (argc < 2)
		die("no device specified\n");

	memset(edit_row, 0, sizeof(edit_row));
	memset(edit_col, 0, sizeof(edit_col));
	memset(edit_size, 0, sizeof(edit_size));
	memset(edit_last, 0, sizeof(edit_last));
	display_mode = HEX_MODE;
	type_alloc(buf, char, bufsize); /* allocate/malloc a new 4K buffer */
	block = 0x10;
	for (i = 1; i < argc; i++) {
		if (!strcasecmp(argv[i], "-verbose"))
			verbose = TRUE;
		else if (!strcasecmp(argv[i], "-V")) {
			printf("%s %s (built %s %s)\n", prog_name, RELEASE_VERSION,
				__DATE__, __TIME__);
			printf("%s\n", REDHAT_COPYRIGHT);
			exit(0);
		}
		else {
			strcpy(device,argv[i]);
		}
	}
	fd = open(device, O_RDWR);
	if (fd < 0)
		die("can't open %s: %s\n", argv[1], strerror(errno));
	max_block = lseek(fd, 0, SEEK_END) / bufsize;

	if (initscr() == NULL) {
		fprintf(stderr, "Error: unable to initialize screen.\n");
		exit(-1);
	}

	signal(SIGWINCH, UpdateSize); /* handle the terminal resize signal */
	UpdateSize(0); /* update screen size based on terminal settings */
	clear();
	start_color();
	noecho();
	keypad(stdscr, TRUE);
	raw();
	curs_set(0);
	init_pair(1, COLOR_BLACK,  COLOR_CYAN);  /* title lines */
	init_pair(2, COLOR_WHITE,  COLOR_BLACK); /* normal text */
	init_pair(3, COLOR_BLACK,  COLOR_WHITE); /* inverse text */
	init_pair(4, COLOR_RED,    COLOR_BLACK); /* special text */
	init_pair(5, COLOR_GREEN,  COLOR_BLACK); /* highlighted text */
	init_pair(6, COLOR_CYAN,   COLOR_BLACK); /* offsets */

	while (!Quit) {
		display();
		while ((ch=getch()) == 0); // wait for input
		switch (ch)
		{
		/* -------------------------------------------------------------- */
		/* escape or 'q' */
        /* -------------------------------------------------------------- */
		case 0x1b:
		case 0x03:
		case 'q':
			Quit=TRUE;
			break;
		/* -------------------------------------------------------------- */
		/* home - return to the superblock */
        /* -------------------------------------------------------------- */
		case KEY_HOME:
			previous_block = block;
			block = 0x10;
			offset = 0;
			break;
		/* -------------------------------------------------------------- */
		/* backspace - return to the previous block */
        /* -------------------------------------------------------------- */
		case KEY_BACKSPACE:
		case 0x7f:
			temp_blk = block;
			block = previous_block;
			previous_block = temp_blk;
			offset = 0;
			break;
		/* -------------------------------------------------------------- */
		/* arrow up */
        /* -------------------------------------------------------------- */
		case KEY_UP:
			if (edit_row[display_mode] >= 0) /* -1 means change block number */
				edit_row[display_mode]--;
			break;
		/* -------------------------------------------------------------- */
		/* arrow down */
        /* -------------------------------------------------------------- */
		case KEY_DOWN:
			if (edit_row[display_mode] < edit_last[display_mode])
				edit_row[display_mode]++;
			break;
		/* -------------------------------------------------------------- */
		/* arrow left */
        /* -------------------------------------------------------------- */
		case KEY_LEFT:
			if (display_mode == HEX_MODE) {
				if (edit_col[display_mode] > 0)
					edit_col[display_mode]--;
				else
					edit_col[display_mode] = 15;
			}
			break;
		/* -------------------------------------------------------------- */
		/* arrow right */
        /* -------------------------------------------------------------- */
		case KEY_RIGHT:
			if (display_mode == HEX_MODE) {
				if (edit_col[display_mode] < 15)
					edit_col[display_mode]++;
				else
					edit_col[display_mode] = 0;
			}
			break;
		/* -------------------------------------------------------------- */
		/* m - change display mode key */
        /* -------------------------------------------------------------- */
		case 'm':
			display_mode = ((display_mode + 1) % DISPLAY_MODES);
			break;
		/* -------------------------------------------------------------- */
		/* J - Jump to highlighted block number */
        /* -------------------------------------------------------------- */
		case 'j':
			if (display_mode == HEX_MODE) {
				unsigned int col2;
				uint64 *b;

				col2 = edit_col[display_mode] & 0x08;/* thus 0-7->0, 8-15->8 */
				b = (uint64 *)&buf[edit_row[display_mode]*16 + offset +	col2];
				temp_blk=gfs64_to_cpu(*b);
			}
			else
				sscanf(edit_string, "%"SCNx64, &temp_blk);/* retrieve in hex */
			if (temp_blk < max_block) { /* if the block number is valid */
				offset = 0;
				display_mode = HEX_MODE;
				previous_block = block;
				block = temp_blk;
			}
			break;
		/* -------------------------------------------------------------- */
		/* g - goto block */
        /* -------------------------------------------------------------- */
		case 'g':
			memset(string, 0, sizeof(string));
			sprintf(string,"%"PRIX64, block);
			if (bobgets(string, 1, 7, 16))
				sscanf(string, "%"SCNx64, &temp_blk); /* retrieve in hex */
			if (temp_blk < max_block) {
				offset = 0;
				previous_block = block;
				block = temp_blk;
			}
			break;
		/* -------------------------------------------------------------- */
		/* h - help key */
        /* -------------------------------------------------------------- */
		case 'h':
			print_usage();
			break;
		/* -------------------------------------------------------------- */
		/* b - Back one 4K block */
        /* -------------------------------------------------------------- */
		case 'b':
			edit_row[display_mode] = 0;
			if (block > 0) {
				previous_block = block;
				block--;
			}
			offset = 0;
			break;
		/* -------------------------------------------------------------- */
		/* page up key */
        /* -------------------------------------------------------------- */
		case 0x19:                    // ctrl-y for vt100
		case KEY_PPAGE:		      // PgUp
		case 0x15:                    // ctrl-u for vi compat.
		case 0x02:                   // ctrl-b for less compat.
			edit_row[display_mode] = 0;
			if (display_mode == GFS_MODE || offset==0) {
				previous_block = block;
				block--;
				if (display_mode == HEX_MODE)
					offset = (bufsize % screen_chunk_size) > 0 ? 
						screen_chunk_size * (bufsize / screen_chunk_size) :
						bufsize - screen_chunk_size;
				else
					offset = 0;
			}
			else
				offset -= screen_chunk_size;
			break;
		/* -------------------------------------------------------------- */
		/* f - Forward one 4K block */
		/* -------------------------------------------------------------- */
		case 'f':
		case ' ': /* space  for less/more compat. */
			previous_block = block;
			edit_row[display_mode] = 0;
			block++;
			offset = 0;
			break;
		/* -------------------------------------------------------------- */
		/* page down key */
		/* -------------------------------------------------------------- */
		case 0x16:                    // ctrl-v for vt100
		case KEY_NPAGE:		      // PgDown
		case 0x04:                    // ctrl-d for vi compat.
			edit_row[display_mode] = 0;
			if (display_mode == GFS_MODE ||
				offset + screen_chunk_size >= bufsize) {
				previous_block = block;
				block++;
				offset = 0;
			}
			else
				offset += screen_chunk_size;
			break;
		/* -------------------------------------------------------------- */
		/* enter key - change a value */
		/* -------------------------------------------------------------- */
		case(KEY_ENTER):
		case('\n'):
		case('\r'):
			if (edit_row[display_mode] == -1) {
				memset(string, 0, sizeof(string));
				sprintf(string,"%"PRIX64, block);
				if (bobgets(string, 1, 7, 16))
					sscanf(string, "%"SCNx64, &temp_blk); /* retrieve in hex */
				if (temp_blk < max_block) {
					offset = 0;
					previous_block = block;
					block = temp_blk;
				}
			}
			else {
				if (display_mode == HEX_MODE) {
					left_off = ((block * bufsize) < 0xffffffff) ? 9 : 17;
					/* 8 and 16 char addresses on screen */
					       
					if (bobgets(edit_string, edit_row[display_mode] + 3,
								(edit_col[display_mode] * 2) + 
								(edit_col[display_mode] / 4) + left_off, 2)) {
						if (strstr(edit_fmt,"X") || strstr(edit_fmt,"x")) {
							int hexoffset;
							unsigned char ch;
							
							hexoffset = (edit_row[display_mode] * 16) +
								edit_col[display_mode];
							ch = 0x00;
							if (isdigit(edit_string[0]))
								ch = (edit_string[0] - '0') * 0x10;
							else if (edit_string[0] >= 'a' &&
									 edit_string[0] <= 'f')
								ch = (edit_string[0] - 'a' + 0x0a) * 0x10;
							else if (edit_string[0] >= 'A' &&
									 edit_string[0] <= 'F')
								ch = (edit_string[0] - 'A' + 0x0a) * 0x10;
							if (isdigit(edit_string[1]))
								ch += (edit_string[1] - '0');
							else if (edit_string[1] >= 'a' &&
									 edit_string[1] <= 'f')
								ch += (edit_string[1] - 'a' + 0x0a);
							else if (edit_string[1] >= 'A' &&
									 edit_string[1] <= 'F')
								ch += (edit_string[1] - 'A' + 0x0a);
							buf[offset + hexoffset] = ch;
							do_lseek(fd, dev_offset);
							do_write(fd, buf, bufsize);
							fsync(fd);
						}
					}
				}
				else if (display_mode == GFS_MODE)
					bobgets(edit_string, edit_row[display_mode] + 3, 24,
							edit_size[display_mode]);
				else
					bobgets(edit_string, edit_row[display_mode] + 6, 24,
							edit_size[display_mode]);
			}
			break;
		default:
			move(termlines - 1, 0);
			printw("Keystroke not understood: %02X",ch);
			refresh();
			sleep(2);
			break;
		} /* switch */
	} /* while !Quit */
    clear();
    refresh();
    endwin();
	close(fd);
	if (buf)
		free(buf);
 	exit(EXIT_SUCCESS);
}
