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

#ifndef __FS_FILE_H__
#define __FS_FILE_H__

#include "fs_incore.h"

int fs_readi(fs_inode_t *ip, void *buf, uint64 offset, unsigned int size);
int fs_writei(fs_inode_t *ip, void *buf, uint64 offset, unsigned int size);

#endif /* __FS_FILE_H__ */
