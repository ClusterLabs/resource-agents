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

#define DMODES 3
enum dsp_mode { HEX_MODE = 0, GFS2_MODE = 1, EXTENDED_MODE = 2 };
#define BLOCK_STACK_SIZE 256

#define GFS_FORMAT_SB           (100)  /* Super-Block */
#define GFS_METATYPE_SB         (1)    /* Super-Block */
#define GFS_FORMAT_FS           (1309) /* Filesystem (all-encompassing) */
#define GFS_FORMAT_MULTI        (1401) /* Multi-Host */
/* GFS1 Dinode types  */
#define GFS_FILE_NON            (0)
#define GFS_FILE_REG            (1)    /* regular file */
#define GFS_FILE_DIR            (2)    /* directory */
#define GFS_FILE_LNK            (5)    /* link */
#define GFS_FILE_BLK            (7)    /* block device node */
#define GFS_FILE_CHR            (8)    /* character device node */
#define GFS_FILE_FIFO           (101)  /* fifo/pipe */
#define GFS_FILE_SOCK           (102)  /* socket */

EXTERN char *prog_name;
EXTERN int fd;
EXTERN uint64_t block INIT(0);
EXTERN int blockhist INIT(0);
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
EXTERN int edit_row[DMODES], edit_col[DMODES];
EXTERN int start_row[DMODES];
EXTERN int edit_size[DMODES], edit_last[DMODES];
EXTERN char edit_string[1024], edit_fmt[80];
EXTERN struct gfs2_sbd sbd;
EXTERN struct gfs_sb *sbd1;
EXTERN struct gfs2_inum gfs1_quota_di;   /* kludge because gfs2 sb too small */
EXTERN struct gfs2_inum gfs1_license_di; /* kludge because gfs2 sb too small */
EXTERN struct gfs2_dinode di;
EXTERN int screen_chunk_size INIT(512); /* how much of the 4K can fit on screen */
EXTERN int gfs2_struct_type;
EXTERN uint64_t block_in_mem INIT(-1);
EXTERN char device[NAME_MAX];
EXTERN int identify INIT(FALSE);
EXTERN int color_scheme INIT(0);
EXTERN WINDOW *wind;
EXTERN int gfs1 INIT(0);

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

struct gfs_indirect {
	struct gfs2_meta_header in_header;

	char in_reserved[64];
};

struct blkstack_info {
	uint64_t block;
	int start_row[DMODES];
	int edit_row[DMODES];
	int edit_col[DMODES];
	enum dsp_mode dmode;
	int gfs2_struct_type;
};

struct metapath {
	uint64_t mp_list[GFS2_MAX_META_HEIGHT];
};

struct gfs_sb {
	/*  Order is important; need to be able to read old superblocks
	    in order to support on-disk version upgrades */
	struct gfs2_meta_header sb_header;

	uint32_t sb_fs_format;         /* GFS_FORMAT_FS (on-disk version) */
	uint32_t sb_multihost_format;  /* GFS_FORMAT_MULTI */
	uint32_t sb_flags;             /* ?? */

	uint32_t sb_bsize;             /* fundamental FS block size in bytes */
	uint32_t sb_bsize_shift;       /* log2(sb_bsize) */
	uint32_t sb_seg_size;          /* Journal segment size in FS blocks */

	/* These special inodes do not appear in any on-disk directory. */
	struct gfs2_inum sb_jindex_di;  /* journal index inode */
	struct gfs2_inum sb_rindex_di;  /* resource group index inode */
	struct gfs2_inum sb_root_di;    /* root directory inode */

	/* Default inter-node locking protocol (lock module) and namespace */
	char sb_lockproto[GFS2_LOCKNAME_LEN]; /* lock protocol name */
	char sb_locktable[GFS2_LOCKNAME_LEN]; /* unique name for this FS */

	/* More special inodes */
	struct gfs2_inum sb_quota_di;   /* quota inode */
	struct gfs2_inum sb_license_di; /* license inode */

	char sb_reserved[96];
};

EXTERN struct blkstack_info blockstack[BLOCK_STACK_SIZE];
EXTERN struct indirect_info indirect[512]; /* more than the most indirect
											  pointers possible for any given
											  4K block */
EXTERN struct indirect_info masterdir; /* Master directory info */
EXTERN int indirect_blocks INIT(0);  /* count of indirect blocks */
EXTERN enum dsp_mode dmode INIT(HEX_MODE);

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
