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

#ifndef __FS_INODE_H__
#define __FS_INODE_H__

#include "fsck_incore.h"

int fs_copyin_dinode(struct fsck_inode *ip, struct buffer_head *bh);
int fs_copyout_dinode(struct fsck_inode *ip);
int fs_lookupi(struct fsck_inode *dip, osi_filename_t *name,
	       struct fsck_inode **ipp);
int fs_mkdir(struct fsck_inode *dip, char *new_dir, int mode, struct fsck_inode **nip);
int fs_remove(struct fsck_inode *ip);

static __inline__ int fs_is_stuffed(struct fsck_inode *ip)
{
	return !ip->i_di.di_height;
}

static __inline__ int fs_is_jdata(struct fsck_inode *ip)
{
	return ip->i_di.di_flags & GFS2_DIF_JDATA;
}


#endif /*  __FS_INODE_H__ */
