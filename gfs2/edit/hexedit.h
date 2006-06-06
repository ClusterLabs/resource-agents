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

#ifndef __HEXVIEW_DOT_H__
#define __HEXVIEW_DOT_H__

#include "linux_endian.h"
#include <sys/types.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/gfs2_ondisk.h>
#include <string.h>

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/*  Extern Macro  */

#ifndef EXTERN
#define EXTERN extern
#define INIT(X)
#else
#undef EXTERN
#define EXTERN
#define INIT(X) =X 
#endif

#define DISPLAY_MODES 3
enum dsp_mode { HEX_MODE = 0, GFS2_MODE = 1, EXTENDED_MODE = 2 };
#define BLOCK_STACK_SIZE 256

EXTERN char *prog_name;
EXTERN int fd;
EXTERN uint64_t block INIT(0);
EXTERN int blockhist INIT(0);
EXTERN uint64_t blockstack[BLOCK_STACK_SIZE];
EXTERN int edit_mode INIT(0);
EXTERN int line;
EXTERN char edit_fmt[80];
EXTERN char edit_string[1024];
EXTERN uint64_t dev_offset INIT(0);
EXTERN uint64_t max_block INIT(0);
EXTERN char *buf INIT(NULL);
EXTERN uint64_t bufsize INIT(4096);
EXTERN int termlines INIT(30);
EXTERN int termcols INIT(80);
EXTERN int insert INIT(0);
EXTERN const char *termtype;
EXTERN int line INIT(1);
EXTERN int struct_len INIT(0);
EXTERN unsigned int offset;
EXTERN int edit_row[DISPLAY_MODES], edit_col[DISPLAY_MODES];
EXTERN int edit_size[DISPLAY_MODES], edit_last[DISPLAY_MODES];
EXTERN char edit_string[1024], edit_fmt[80];
EXTERN struct gfs2_sbd sbd;
EXTERN struct gfs2_dinode di;
EXTERN int screen_chunk_size INIT(512); /* how much of the 4K can fit on screen */
EXTERN int gfs2_struct_type;
EXTERN uint64_t block_in_mem INIT(-1);
EXTERN char device[NAME_MAX];
EXTERN int identify INIT(FALSE);
EXTERN int color_scheme INIT(0);
EXTERN WINDOW *wind;

struct gfs2_dirents {
	uint64_t block;
	struct gfs2_dirent dirent;
	char filename[NAME_MAX];
};

struct indirect_info {
	int is_dir;
	uint64_t block;
	uint32_t dirents;
	struct gfs2_dirents dirent[64];
};

EXTERN struct indirect_info indirect[512]; /* more than the most indirect
											  pointers possible for any given
											  4K block */
EXTERN struct indirect_info masterdir; /* Master directory info */
EXTERN int indirect_blocks INIT(0);  /* count of indirect blocks */
EXTERN enum dsp_mode display_mode INIT(HEX_MODE);

#define SCREEN_HEIGHT   (16)
#define SCREEN_WIDTH    (16)

/*  I/O macros  */

#define do_lseek(fd, off) \
{ \
  if (lseek((fd), (off), SEEK_SET) != (off)) \
    die("bad seek: %s on line %d of file %s\n", \
	strerror(errno),__LINE__, __FILE__); \
}

#define do_read(fd, buff, len) \
{ \
  if (read((fd), (buff), (len)) != (len)) \
    die("bad read: %s on line %d of file %s\n", \
	strerror(errno), __LINE__, __FILE__); \
}

#define do_write(fd, buff, len) \
{ \
  if (write((fd), (buff), (len)) != (len)) \
    die("bad write: %s on line %d of file %s\n", \
	strerror(errno), __LINE__, __FILE__); \
}



/*  Memory macros  */

#define type_zalloc(ptr, type, count) \
{ \
  (ptr) = (type *)malloc(sizeof(type) * (count)); \
  if ((ptr)) \
    memset((char *)(ptr), 0, sizeof(type) * (count)); \
  else \
    die("unable to allocate memory on line %d of file %s\n", \
	__LINE__, __FILE__); \
}

#define type_alloc(ptr, type, count) \
{ \
  (ptr) = (type *)malloc(sizeof(type) * (count)); \
  if (!(ptr)) \
    die("unable to allocate memory on line %d of file %s\n", \
	__LINE__, __FILE__); \
}

#define pa(struct, member, count) print_array(#member, struct->member, count);
#define printk printw

/*  Divide x by y.  Round up if there is a remainder.  */
#define DIV_RU(x, y) (((x) + (y) - 1) / (y))

#define TITLE1 "gfs2_edit - Global File System Editor (use with extreme caution)"
#define TITLE2 "Copyright (C) 2006 Red Hat, Inc. - Press H for help"

#define COLOR_TITLE     1
#define COLOR_NORMAL    2
#define COLOR_INVERSE   3
#define COLOR_SPECIAL   4
#define COLOR_HIGHLIGHT 5
#define COLOR_OFFSETS   6
#define COLOR_CONTENTS  7

#define COLORS_TITLE     do { attrset(COLOR_PAIR(COLOR_TITLE));attron(A_BOLD); } while (0)
#define COLORS_NORMAL    do { attrset(COLOR_PAIR(COLOR_NORMAL));attron(A_BOLD); } while (0)
#define COLORS_INVERSE   do { attrset(COLOR_PAIR(COLOR_INVERSE));attron(A_BOLD); } while (0)
#define COLORS_SPECIAL   do { attrset(COLOR_PAIR(COLOR_SPECIAL));attron(A_BOLD); } while (0)
#define COLORS_HIGHLIGHT do { attrset(COLOR_PAIR(COLOR_HIGHLIGHT));attron(A_BOLD); } while (0)
#define COLORS_OFFSETS   do { attrset(COLOR_PAIR(COLOR_OFFSETS));attron(A_BOLD); } while (0)
#define COLORS_CONTENTS  do { attrset(COLOR_PAIR(COLOR_CONTENTS));attron(A_BOLD); } while (0)

#endif /* __HEXVIEW_DOT_H__ */
