/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __GLOPS_DOT_H__
#define __GLOPS_DOT_H__

extern struct gfs_glock_operations gfs_meta_glops;
extern struct gfs_glock_operations gfs_inode_glops;
extern struct gfs_glock_operations gfs_rgrp_glops;
extern struct gfs_glock_operations gfs_trans_glops;
extern struct gfs_glock_operations gfs_iopen_glops;
extern struct gfs_glock_operations gfs_flock_glops;
extern struct gfs_glock_operations gfs_nondisk_glops;
extern struct gfs_glock_operations gfs_quota_glops;

#endif /* __GLOPS_DOT_H__ */
