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

#ifndef __BITMAP_H__
#define __BITMAP_H__

#include "fs_incore.h"

int allocate_bitmaps(fs_sbd_t *sdp);
int free_bitmaps(fs_sbd_t *sdp);
int get_bitmap(fs_sbd_t *sdp, uint64 blkno, fs_rgrpd_t *rgd);
int set_bitmap(fs_sbd_t *sdp, uint64 blkno, int state);

#endif /* __BITMAP_H__ */
