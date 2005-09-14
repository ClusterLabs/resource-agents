/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/



#ifndef _FSCK_INCORE_H
#define _FSCK_INCORE_H

#include <stdint.h>
#include "ondisk.h"
#include "osi_list.h"
/* #include "osi_user.h" */
#include "fs_bits.h"
#include "block_list.h"

#define SBF_RECONSTRUCT_JOURNALS (1)

#define FSCK_HASH_SHIFT         (13)
#define FSCK_HASH_SIZE          (1 << FSCK_HASH_SHIFT)
#define FSCK_HASH_MASK          (FSCK_HASH_SIZE - 1)

struct fsck_sb;

/*  Filename structure  */
/* FIXME would like to find a way to get rid of this */
struct osi_filename
{
  unsigned char *name;
  unsigned int len;
};
typedef struct osi_filename osi_filename_t;
struct osi_cred
{
  uint32_t cr_uid;
  uint32_t cr_gid;
};
typedef struct osi_cred osi_cred_t;
#define osi_cred_to_uid(credp) ((credp) ? (credp)->cr_uid : 0)
#define osi_cred_to_gid(credp) ((credp) ? (credp)->cr_gid : 0)
#define osi_cred_in_group(credp, gid) ((credp) ? ((cred)->cr_gid == (gid)) : FALSE)

struct inode_info
{
	osi_list_t list;
	uint64_t   inode;
	uint32_t   link_count;   /* the number of links the inode
				  * thinks it has */
	uint32_t   counted_links; /* the number of links we've found */
};

struct dir_info
{
	osi_list_t list;
	uint64_t dinode;
	uint64_t treewalk_parent;
	uint64_t dotdot_parent;
	uint8_t  checked:1;

};

/* Clone of include/linux/buffer_header.h - pulled out unnecessary fields */
/* FIXME are all these fields necessary? */
struct buffer_head
{
	unsigned long b_state;		/* buffer state bitmap (see above) */
	struct buffer_head *b_this_page;/* circular list of page's buffers */
	osi_list_t b_list;       /* Added this for simplicity */

	uint64_t b_blocknr;		/* block number */
	uint32_t b_size;			/* block size */
	char *b_data;			/* pointer to data block */

	struct block_device *b_bdev;
 	void *b_private;		/* reserved for b_end_io */
};

/*  Use to manage in core bitmaps  */

struct bitmap_list
{
  osi_list_t list;
  char *bm;
  struct fsck_rgrp *rgd;
};

/* FIXME not sure that i want to keep a record of the inodes or the
 * contents of them, or both ... if I need to write back to them, it
 * would be easier to hold the inode as well  */
struct per_node
{
	struct fsck_inode *inum;
	struct gfs2_inum_range inum_range;
	struct fsck_inode *statfs;
	struct gfs2_statfs_change statfs_change;
	struct fsck_inode *unlinked;
	struct gfs2_unlinked_tag unlinked_tag;
	struct fsck_inode *quota;
	struct gfs2_quota_change quota_change;
};

struct master_dir
{
	struct fsck_inode *inum;
	uint64_t next_inum;
	struct fsck_inode *statfs;
	struct gfs2_statfs_change statfs_change;

	struct gfs2_rindex rindex;
	struct fsck_inode *qinode;
	struct gfs2_quota quotas;

	struct fsck_inode       *jiinode;
        struct fsck_inode       *riinode;
	struct fsck_inode       *rooti;
	struct fsck_inode	*pinode;

	struct fsck_inode **journal;      /* Array of journals */
	uint32_t journals;		  /* Journal count */
	struct per_node *pn;              /* Array of per_node entries */
};

/*
 *  Incore inode structure
 */

struct fsck_inode
{
	struct fsck_sb           *i_sbd;           /* GFS2 superblock pointer */
	struct gfs2_inum          i_num;
	struct gfs2_dinode         i_di;             /* Dinode Structure */
};

struct fsck_rgrp
{
	struct fsck_sb            *rd_sbd;        /* ptr to in-core super block */
	osi_list_t          rd_list;        /* Link with superblock */

	struct gfs2_rindex        rd_ri;          /* Resource Index structure */
	struct gfs2_rgrp          rd_rg;          /* Resource Group structure */

	int32_t               rd_open_count;  /* # of open references on this rgrpd */

	fs_bitmap_t         *rd_bits;
	struct buffer_head  **rd_bh;
};



struct fsck_sb {
	struct gfs2_sb	sb;            /* Super Block */
	int		diskfd;
	uint32_t	flags;
	char		fsname[256];

	struct fsck_inode *sb_master_dir;

	struct master_dir md;

	/* Special inodes */
	struct fsck_inode	*lf_dip;       /* lost-n-found dir inode */

	/* rgrp stuff */
	osi_list_t	rglist;        /* List of resource groups */
	unsigned int	rgcount;       /* Count of resource groups */

	/*  Constants computed on mount  */
	uint32_t fsb2bb_shift;  /* Shift FS Block numbers to the left by
				   this to get buffer cache blocks  */
	uint32_t diptrs;        /* Number of pointers in a dinode */
	uint32_t inptrs;        /* Number of pointers in a indirect block */
	uint32_t jbsize;        /* Size of a journaled data block */
	uint32_t bsize;         /* Size of a fs block */
	uint32_t hash_bsize;    /* sizeof(exhash block) */
	uint32_t hash_ptrs;     /* Number of points in a hash block */
	uint32_t max_height;    /* Maximum height of a file's metadata tree */
	uint64_t heightsize[GFS2_MAX_META_HEIGHT];
	uint32_t max_jheight;   /* Max height of a journaled file's metadata tree */
	uint64_t jheightsize[GFS2_MAX_META_HEIGHT];

	uint64_t last_fs_block;
	uint64_t last_data_block;
	uint64_t first_data_block;

	/* dir_list is used to keep track of directory inodes and
	 * their parents */
	osi_list_t dir_hash[FSCK_HASH_SIZE];

	/* inode_list is used to keep track of the link count of
	 * inodes */
	osi_list_t inode_hash[FSCK_HASH_SIZE];

	/* contains list of data and metadata blocks and various info
	 * about each */
	struct block_list *bl;

	osi_list_t dup_list;

	/* fsck_opts is used to pass command line params around */
	struct options *opts;

};




#endif /* _FSCK_INCORE_H */
