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

#ifndef __FS_RGRP_H__
#define __FS_RGRP_H__

#include "fs_incore.h"

int fs_compute_bitstructs(fs_rgrpd_t *rgd);
fs_rgrpd_t *fs_blk2rgrpd(fs_sbd_t *sdp, uint64 blk);

int fs_rgrp_read(fs_rgrpd_t *rgd);
void fs_rgrp_relse(fs_rgrpd_t *rgd);
int fs_rgrp_verify(fs_rgrpd_t *rgd);
int fs_rgrp_recount(fs_rgrpd_t *rgd);

int clump_alloc(fs_rgrpd_t *rgd, uint32 goal);
int fs_blkalloc(fs_inode_t *ip, uint64 *block);
int fs_metaalloc(fs_inode_t *ip, uint64 *block);

#endif /* __FS_RGRP_H__ */
