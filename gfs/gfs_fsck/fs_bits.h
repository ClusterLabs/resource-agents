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

#ifndef __FS_BITS_H__
#define __FS_BITS_H__

#include "global.h"
#include "fs_incore.h"

#define BFITNOENT (0xFFFFFFFF)

/* functions with blk #'s that are buffer relative */
uint32 fs_bitcount(unsigned char *buffer, unsigned int buflen,
		   unsigned char state);
uint32 fs_bitfit(unsigned char *buffer, unsigned int buflen,
		 uint32 goal, unsigned char old_state);

/* functions with blk #'s that are rgrp relative */
uint32 fs_blkalloc_internal(fs_rgrpd_t *rgd, uint32 goal,
			    unsigned char old_state,
			    unsigned char new_state, int do_it);

/* functions with blk #'s that are file system relative */
int fs_get_bitmap(fs_sbd_t *sdp, uint64 blkno, fs_rgrpd_t *rgd);
int fs_set_bitmap(fs_sbd_t *sdp, uint64 blkno, int state);

#endif /* __FS_BITS_H__ */
