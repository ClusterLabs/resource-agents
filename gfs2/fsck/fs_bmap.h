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

#ifndef __FS_BMAP_H__
#define __FS_BMAP_H__

#include "fsck_incore.h"

int fs_unstuff_dinode(struct fsck_inode *ip);
int fs_block_map(struct fsck_inode *ip, uint64_t lblock, int *new,
		 uint64_t *dblock, uint32_t *extlen);

#endif /* __FS_BMAP_H__ */
