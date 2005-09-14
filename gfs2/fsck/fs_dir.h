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

#ifndef __FS_DIR_H__
#define __FS_DIR_H__

#include "fsck_incore.h"

#define ID_FILENAME 0
#define ID_INUM     1
typedef struct identifier_s {
	int type;

	osi_filename_t *filename;
	struct gfs2_inum *inum;
} identifier_t;

int dirent_del(struct fsck_inode *dip, struct buffer_head *bh,
	       struct gfs2_dirent *prev, struct gfs2_dirent *cur);
int fsck_inode_is_stuffed(struct fsck_inode *ip);
int dirent_first(struct buffer_head *bh, struct gfs2_dirent **dent);
int get_leaf_nr(struct fsck_inode *dip, uint32_t index, uint64_t *leaf_out);
int fs_filecmp(osi_filename_t *file1, char *file2, int len_of_file2);
int fs_dirent_del(struct fsck_inode *dip, struct buffer_head *bh, osi_filename_t *filename);
int fs_dir_add(struct fsck_inode *dip, osi_filename_t *filename,
	       struct gfs2_inum *inum, unsigned int type);
int fs_dirent_alloc(struct fsck_inode *dip, struct buffer_head *bh,
		    int name_len, struct gfs2_dirent **dent_out);

int fs_dir_search(struct fsck_inode *dip, identifier_t *id, unsigned int *type);

#endif /* __FS_DIR_H__ */
