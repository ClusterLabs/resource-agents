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

#ifndef __DIAPER_DOT_H__
#define __DIAPER_DOT_H__

struct block_device *gfs_diaper_get(struct block_device *real, int flags);
void gfs_diaper_put(struct block_device *diaper);

void gfs_diaper_register_sbd(struct block_device *diaper, struct gfs_sbd *sdp);
struct block_device *gfs_diaper_2real(struct block_device *diaper);

int gfs_diaper_init(void);
void gfs_diaper_uninit(void);

#endif /* __DIAPER_DOT_H__ */
