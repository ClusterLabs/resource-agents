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

#ifndef _RGRP_H
#define _RGRP_H

struct fsck_sb;
struct fsck_rgrp;
struct fsck_inode;

int fs_compute_bitstructs(struct fsck_rgrp *rgd);
struct fsck_rgrp *fs_blk2rgrpd(struct fsck_sb *sdp, uint64_t blk);

int fs_rgrp_read(struct fsck_rgrp *rgd, int repair_if_corrupted);
void fs_rgrp_relse(struct fsck_rgrp *rgd);
int fs_rgrp_verify(struct fsck_rgrp *rgd);
int fs_rgrp_recount(struct fsck_rgrp *rgd);

int clump_alloc(struct fsck_rgrp *rgd, uint32_t goal);
int fs_blkalloc(struct fsck_inode *ip, uint64_t *block);
int fs_metaalloc(struct fsck_inode *ip, uint64_t *block);

#endif /* _RGRP_H */
