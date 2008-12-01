#ifndef __HEXVIEW_DOT_H__
#define __HEXVIEW_DOT_H__

#include "linux_endian.h"
#include <sys/types.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/gfs2_ondisk.h>
#include <string.h>

#include "libgfs2.h"
#include "copyright.cf"

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

/* GFS 1 journal block types: */
#define GFS_LOG_DESC_METADATA   (300)    /* metadata */
#define GFS_LOG_DESC_IUL        (400)    /* unlinked inode */
#define GFS_LOG_DESC_IDA        (401)    /* de-allocated inode */
#define GFS_LOG_DESC_Q          (402)    /* quota */
#define GFS_LOG_DESC_LAST       (500)    /* final in a logged transaction */

EXTERN char *prog_name;
EXTERN uint64_t block INIT(0);
EXTERN int blockhist INIT(0);
EXTERN int edit_mode INIT(0);
EXTERN int line;
EXTERN char edit_fmt[80];
EXTERN char estring[1024]; /* edit string */
EXTERN uint64_t dev_offset INIT(0);
EXTERN uint64_t max_block INIT(0);
EXTERN char *buf INIT(NULL);
EXTERN int termlines INIT(30);
EXTERN int termcols INIT(80);
EXTERN int insert INIT(0);
EXTERN const char *termtype;
EXTERN int line INIT(1);
EXTERN int struct_len INIT(0);
EXTERN unsigned int offset;
EXTERN int edit_row[DMODES], edit_col[DMODES], print_entry_ndx;
EXTERN int start_row[DMODES], end_row[DMODES], lines_per_row[DMODES];
EXTERN int edit_size[DMODES], last_entry_onscreen[DMODES];
EXTERN char edit_fmt[80];
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
EXTERN int editing INIT(0);
EXTERN uint64_t temp_blk;
EXTERN uint64_t starting_blk;

struct gfs_jindex {
        uint64_t ji_addr;       /* starting block of the journal */
        uint32_t ji_nsegment;   /* number (quantity) of segments in journal */
        uint32_t ji_pad;

        char ji_reserved[64];
};

struct gfs_log_descriptor {
	struct gfs2_meta_header ld_header;

	uint32_t ld_type;       /* GFS_LOG_DESC_... Type of this log chunk */
	uint32_t ld_length;     /* Number of buffers in this chunk */
	uint32_t ld_data1;      /* descriptor-specific field */
	uint32_t ld_data2;      /* descriptor-specific field */
	char ld_reserved[64];
};

struct gfs_log_header {
	struct gfs2_meta_header lh_header;

	uint32_t lh_flags;      /* GFS_LOG_HEAD_... */
	uint32_t lh_pad;

	uint64_t lh_first;     /* Block number of first header in this trans */
	uint64_t lh_sequence;   /* Sequence number of this transaction */

	uint64_t lh_tail;       /* Block number of log tail */
	uint64_t lh_last_dump;  /* Block number of last dump */

	char lh_reserved[64];
};

struct gfs_rindex {
	uint64_t ri_addr;     /* block # of 1st block (header) in rgrp */
	uint32_t ri_length;   /* # fs blocks containing rgrp header & bitmap */
	uint32_t ri_pad;

	uint64_t ri_data1;    /* block # of first data/meta block in rgrp */
	uint32_t ri_data;     /* number (qty) of data/meta blocks in rgrp */

	uint32_t ri_bitbytes; /* total # bytes used by block alloc bitmap */

	char ri_reserved[64];
};

struct gfs_rgrp {
	struct gfs2_meta_header rg_header;

	uint32_t rg_flags;      /* ?? */

	uint32_t rg_free;       /* Number (qty) of free data blocks */

	/* Dinodes are USEDMETA, but are handled separately from other METAs */
	uint32_t rg_useddi;     /* Number (qty) of dinodes (used or free) */
	uint32_t rg_freedi;     /* Number (qty) of unused (free) dinodes */
	struct gfs2_inum rg_freedi_list; /* 1st block in chain of free dinodes */

	/* These META statistics do not include dinodes (used or free) */
	uint32_t rg_usedmeta;   /* Number (qty) of used metadata blocks */
	uint32_t rg_freemeta;   /* Number (qty) of unused metadata blocks */

	char rg_reserved[64];
};

EXTERN int block_is_jindex(void);
EXTERN int block_is_rindex(void);
EXTERN int block_is_inum_file(void);
EXTERN int block_is_statfs_file(void);
EXTERN int block_is_quota_file(void);
EXTERN int display_block_type(const char *lpBuffer, int from_restore);
EXTERN void gfs_jindex_in(struct gfs_jindex *jindex, char *buf);
EXTERN void gfs_log_header_in(struct gfs_log_header *head, char *buf);
EXTERN void gfs_log_header_print(struct gfs_log_header *lh);
EXTERN void gfs_dinode_in(struct gfs_dinode *di, char *buf);

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

struct iinfo {
	struct indirect_info ii[512];
};

struct blkstack_info {
	uint64_t block;
	int start_row[DMODES];
	int end_row[DMODES];
	int lines_per_row[DMODES];
	int edit_row[DMODES];
	int edit_col[DMODES];
	enum dsp_mode dmode;
	int gfs2_struct_type;
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
EXTERN struct iinfo *indirect; /* more than the most indirect
			       pointers possible for any given 4K block */
EXTERN struct indirect_info masterdir; /* Master directory info */
EXTERN int indirect_blocks INIT(0);  /* count of indirect blocks */
EXTERN enum dsp_mode dmode INIT(HEX_MODE);

#define SCREEN_HEIGHT   (16)
#define SCREEN_WIDTH    (16)

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

#define printk printw

/*  Divide x by y.  Round up if there is a remainder.  */
#define DIV_RU(x, y) (((x) + (y) - 1) / (y))

#define TITLE1 "gfs2_edit - Global File System Editor (use with extreme caution)"
#define TITLE2 REDHAT_COPYRIGHT " - Press H for help"

#define COLOR_TITLE     1
#define COLOR_NORMAL    2
#define COLOR_INVERSE   3
#define COLOR_SPECIAL   4
#define COLOR_HIGHLIGHT 5
#define COLOR_OFFSETS   6
#define COLOR_CONTENTS  7

#define COLORS_TITLE     \
	do { \
		if (termlines) { \
			attrset(COLOR_PAIR(COLOR_TITLE)); \
			attron(A_BOLD); \
		} \
	} while (0)
#define COLORS_NORMAL    \
	do { \
		if (termlines) { \
			attrset(COLOR_PAIR(COLOR_NORMAL)); \
			attron(A_BOLD); \
		} \
	} while (0)
#define COLORS_INVERSE   \
	do { \
		if (termlines) { \
			attrset(COLOR_PAIR(COLOR_INVERSE)); \
			attron(A_BOLD); \
		} \
	} while (0)
#define COLORS_SPECIAL   \
	do { \
		if (termlines) { \
			attrset(COLOR_PAIR(COLOR_SPECIAL)); \
			attron(A_BOLD); \
		} \
	} while (0)
#define COLORS_HIGHLIGHT \
	do { \
		if (termlines) { \
			attrset(COLOR_PAIR(COLOR_HIGHLIGHT)); \
			attron(A_BOLD); \
		} \
	} while (0)
#define COLORS_OFFSETS   \
	do { \
		if (termlines) { \
			attrset(COLOR_PAIR(COLOR_OFFSETS)); \
			attron(A_BOLD); \
		} \
	} while (0)
#define COLORS_CONTENTS  \
	do { \
		if (termlines) { \
			attrset(COLOR_PAIR(COLOR_CONTENTS)); \
			attron(A_BOLD); \
		} \
	} while (0)

#endif /* __HEXVIEW_DOT_H__ */
