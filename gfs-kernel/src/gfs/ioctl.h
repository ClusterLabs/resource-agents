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

#ifndef __IOCTL_DOT_H__
#define __IOCTL_DOT_H__

int gfs_add_bh_to_ub(struct gfs_user_buffer *ub, struct buffer_head *bh);

int gfs_ioctli(struct gfs_inode *ip, unsigned int cmd, void *arg);

#endif /* __IOCTL_DOT_H__ */
