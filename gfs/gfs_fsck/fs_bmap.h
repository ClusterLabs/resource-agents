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

#ifndef __FS_BMAP_H__
#define __FS_BMAP_H__

#include "fs_incore.h"

int fs_unstuff_dinode(fs_inode_t *ip);
int fs_block_map(fs_inode_t *ip, uint64 lblock, int *new,
		 uint64 *dblock, uint32 *extlen);

#endif /* __FS_BMAP_H__ */
