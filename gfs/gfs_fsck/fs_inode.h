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

#ifndef __FS_INODE_H__
#define __FS_INODE_H__

#include "fs_incore.h"

int fs_copyin_dinode(fs_inode_t *ip);
int fs_copyout_dinode(fs_inode_t *ip);
int fs_mkdir(fs_inode_t *dip, char *new_dir, int mode, fs_inode_t **nip);
int fs_remove(fs_inode_t *ip);

static __inline__ int fs_is_stuffed(fs_inode_t *ip)
{
  return !ip->i_di.di_height;
}
	
static __inline__ int fs_is_jdata(fs_inode_t *ip)
{
  return ip->i_di.di_flags & GFS_DIF_JDATA;
}
	

#endif /*  __FS_INODE_H__ */
