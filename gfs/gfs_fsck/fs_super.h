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

#ifndef __FS_SUPER_H__
#define __FS_SUPER_H__

#include "fs_incore.h"

int fs_read_sb(fs_sbd_t *sdp);
int fs_ji_update(fs_sbd_t *sdp);
int fs_ri_update(fs_sbd_t *sdp);

#endif /* __FS_SUPER_H__ */
