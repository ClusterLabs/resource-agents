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

#ifndef __INCORE_DOT_H__
#define __INCORE_DOT_H__

#include "osi_user.h"
#include <linux/gfs_ondisk.h>

struct fs_sbd;
struct fs_inode;
struct fs_rgrpd;
struct fs_bitmap;

typedef struct fs_sbd fs_sbd_t;
typedef struct fs_inode fs_inode_t;
typedef struct fs_rgrpd fs_rgrpd_t;
typedef struct fs_bitmap fs_bitmap_t;


/******************************************************************
 *********  structures that are specific to the fsck  *************
 ******************************************************************/
struct di_info
{
  osi_list_t       din_list;
  uint64           din_addr;
  uint32           din_nlink;
  uint32           din_dirents;
};
typedef struct di_info di_info_t;


/*  Use to manage in core bitmaps  */

struct bitmap_list
{
  osi_list_t list;
  char *bm;
  fs_rgrpd_t *rgd;
};
typedef struct bitmap_list bitmap_list_t;


/*  Generally used to keep list of blocks  */

struct blk_list
{
  osi_list_t list;
  uint64 blk;
};
typedef struct blk_list blk_list_t;



/******************************************************************
 *********  structures that are specific to the FS  ***************
 ******************************************************************/

struct fs_bitmap
{
  uint32   bi_offset;        /* The offset in the buffer of the first byte */
  uint32   bi_start;         /* The position of the first byte in this block */
  uint32   bi_len;           /* The number of bytes in this block */
};



/*
 *  Structure containing information Resource Groups
 */
struct fs_rgrpd
{
  fs_sbd_t            *rd_sbd;        /* ptr to in-core super block */
  osi_list_t          rd_list;        /* Link with superblock */

  struct gfs_rindex        rd_ri;          /* Resource Index structure */
  struct gfs_rgrp          rd_rg;          /* Resource Group structure */

  int32               rd_open_count;  /* # of open references on this rgrpd */

  fs_bitmap_t         *rd_bits;
  osi_buf_t           **rd_bh;
};


/*
 *  Incore inode structure
 */

struct fs_inode
{
  fs_sbd_t           *i_sbd;           /* GFS superblock pointer */
  struct gfs_inum          i_num;
  struct gfs_dinode         i_di;             /* Dinode Structure */
};


#define GFS_GHASH_SHIFT         (13)
#define GFS_GHASH_SIZE          (1 << GFS_GHASH_SHIFT)
#define GFS_GHASH_MASK          (GFS_GHASH_SIZE - 1)

/* SBF => SUPER BLOCK FLAG */
#define SBF_RECONSTRUCT_JOURNALS (1)

struct fs_sbd
{
  struct gfs_sb	sd_sb;            /* Super Block */
  int		sd_diskfd;
  uint32	sd_flags;
  char		sd_fsname[256];
  

  /* Special inodes */
  fs_inode_t	*sd_lf_dip;       /* lost-n-found dir inode */
  fs_inode_t	*sd_riinode;      /* rindex inode */
  fs_inode_t	*sd_rooti;        /* FS's root inode */
  fs_inode_t	*sd_jiinode;      /* jindex inode */
  fs_inode_t	*sd_qinode;       /* quota inode */
  fs_inode_t	*sd_linode;       /* license inode */

  /* rgrp stuff */
  osi_list_t	sd_rglist;        /* List of resource groups */
  unsigned int	sd_rgcount;       /* Count of resource groups */

  /* journal stuff */
  unsigned int  sd_journals;  /* Number of journals in the FS */
  struct gfs_jindex  *sd_jindex;   /* Array of Jindex structs for this FS's journals */
  struct gfs_jindex  sd_jdesc;     /* Jindex struct for this machine's journal */

  /*  Constants computed on mount  */
  uint32 sd_fsb2bb_shift;  /* Shift FS Block numbers to the left by
                              this to get buffer cache blocks  */
  uint32 sd_diptrs;        /* Number of pointers in a dinode */
  uint32 sd_inptrs;        /* Number of pointers in a indirect block */
  uint32 sd_jbsize;        /* Size of a journaled data block */
  uint32 sd_hash_bsize;    /* sizeof(exhash block) */
  uint32 sd_hash_ptrs;     /* Number of points in a hash block */
  uint32 sd_max_height;    /* Maximum height of a file's metadata tree */
  uint64 sd_heightsize[GFS_MAX_META_HEIGHT];
  uint32 sd_max_jheight;   /* Max height of a journaled file's metadata tree */
  uint64 sd_jheightsize[GFS_MAX_META_HEIGHT];


  uint64 sd_last_fs_block;
  uint64 sd_last_data_block;
  uint64 sd_first_data_block;

  /* dirent list contains blk #'s of inodes that have more than one dirent **
  ** associated with them.  This does not include . and ..                 */
  osi_list_t sd_dirent_list;

  /* nlink list contains blk #'s of inodes that have link counts greater   **
  ** than 1.  This does not include directories, so it is hard linked files*/
  osi_list_t sd_nlink_list;

  /* bitmaps is an extra bitmap that is tracked by gfsck and compared to   **
  ** what is on disk and maintained in the rgrps.                          */
  osi_list_t sd_bitmaps;
};

#endif  /*  __INCORE_DOT_H__  */
