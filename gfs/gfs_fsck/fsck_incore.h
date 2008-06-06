#ifndef _FSCK_INCORE_H
#define _FSCK_INCORE_H

#include <stdint.h>
#include "ondisk.h"
#include "osi_list.h"
#include "osi_user.h"
#include "fs_bits.h"
#include "block_list.h"

#define SBF_RECONSTRUCT_JOURNALS (1)

#define FSCK_HASH_SHIFT         (13)
#define FSCK_HASH_SIZE          (1 << FSCK_HASH_SHIFT)
#define FSCK_HASH_MASK          (FSCK_HASH_SIZE - 1)

struct fsck_sb;

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

/*  Use to manage in core bitmaps  */

struct bitmap_list
{
  osi_list_t list;
  char *bm;
  struct fsck_rgrp *rgd;
};

/*
 *  Incore inode structure
 */

struct fsck_inode
{
	struct fsck_sb           *i_sbd;           /* GFS superblock pointer */
	struct gfs_inum          i_num;
	struct gfs_dinode         i_di;             /* Dinode Structure */
};

struct fsck_rgrp
{
	struct fsck_sb            *rd_sbd;        /* ptr to in-core super block */
	osi_list_t          rd_list;        /* Link with superblock */

	struct gfs_rindex        rd_ri;          /* Resource Index structure */
	struct gfs_rgrp          rd_rg;          /* Resource Group structure */

	int32_t               rd_open_count;  /* # of open references on this rgrpd */

	fs_bitmap_t         *rd_bits;
	osi_buf_t           **rd_bh;
};



struct fsck_sb {
	struct gfs_sb	sb;            /* Super Block */
	int		diskfd;
	uint32_t	flags;
	char		fsname[256];

	/* Special inodes */
	struct fsck_inode	*lf_dip;       /* lost-n-found dir inode */
	struct fsck_inode       *jiinode;
        struct fsck_inode       *riinode;
	struct fsck_inode       *rooti;

	/* rgrp stuff */
	osi_list_t	rglist;        /* List of resource groups */
	unsigned int	rgcount;       /* Count of resource groups */

	/* journal stuff */
	unsigned int  journals;  /* Number of journals in the FS */
	struct gfs_jindex  *jindex;   /* Array of Jindex structs for
				       * this FS's journals */
	struct gfs_jindex  jdesc;     /* Jindex struct for this
				       * machine's journal */

	/*  Constants computed on mount  */
	uint32_t fsb2bb_shift;  /* Shift FS Block numbers to the left by
				   this to get buffer cache blocks  */
	uint32_t diptrs;        /* Number of pointers in a dinode */
	uint32_t inptrs;        /* Number of pointers in a indirect block */
	uint32_t jbsize;        /* Size of a journaled data block */
	uint32_t hash_bsize;    /* sizeof(exhash block) */
	uint32_t hash_ptrs;     /* Number of points in a hash block */
	uint32_t max_height;    /* Maximum height of a file's metadata tree */
	uint64_t heightsize[GFS_MAX_META_HEIGHT];
	uint32_t max_jheight;   /* Max height of a journaled file's metadata tree */
	uint64_t jheightsize[GFS_MAX_META_HEIGHT];

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
