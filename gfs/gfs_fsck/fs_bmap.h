#ifndef __FS_BMAP_H__
#define __FS_BMAP_H__

#include "fsck_incore.h"

int fs_unstuff_dinode(struct fsck_inode *ip);
int fs_block_map(struct fsck_inode *ip, uint64 lblock, int *new,
		 uint64 *dblock, uint32 *extlen);

#endif /* __FS_BMAP_H__ */
