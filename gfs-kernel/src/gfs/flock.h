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

#ifndef __FLOCK_DOT_H__
#define __FLOCK_DOT_H__

int gfs_flock(struct gfs_file *fp, int ex, int wait);
int gfs_funlock(struct gfs_file *fp);

#endif /* __FLOCK_DOT_H__ */
