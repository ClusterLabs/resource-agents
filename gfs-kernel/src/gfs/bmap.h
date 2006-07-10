/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __BMAP_DOT_H__
#define __BMAP_DOT_H__

typedef int (*gfs_unstuffer_t) (struct gfs_inode * ip,
				struct buffer_head * dibh, uint64_t block,
				void *private);

int gfs_unstuffer_sync(struct gfs_inode *ip, struct buffer_head *dibh,
		       uint64_t block, void *private);
int gfs_unstuffer_async(struct gfs_inode *ip, struct buffer_head *dibh,
			uint64_t block, void *private);

int gfs_unstuff_dinode(struct gfs_inode *ip, gfs_unstuffer_t unstuffer,
		       void *private);

int gfs_block_map(struct gfs_inode *ip,
		  uint64_t lblock, int *new,
		  uint64_t *dblock, uint32_t *extlen);

typedef int (*gfs_truncator_t) (struct gfs_inode * ip, uint64_t size);

int gfs_truncator_default(struct gfs_inode *ip, uint64_t size);

int gfs_shrink(struct gfs_inode *ip, uint64_t size, gfs_truncator_t truncator);
int gfs_truncatei(struct gfs_inode *ip, uint64_t size,
		  gfs_truncator_t truncator);

void gfs_write_calc_reserv(struct gfs_inode *ip, unsigned int len,
			   unsigned int *data_blocks, unsigned int *ind_blocks);
int gfs_write_alloc_required(struct gfs_inode *ip, uint64_t offset,
			     unsigned int len, int *alloc_required);

int gfs_get_file_meta(struct gfs_inode *ip, struct gfs_user_buffer *ub);

#endif /* __BMAP_DOT_H__ */
