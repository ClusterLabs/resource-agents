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

#ifndef __GFS_IOCTL_DOT_H__
#define __GFS_IOCTL_DOT_H__

#define _GFSC_(x)               (('G' << 8) | (x))

/* Ioctls implemented */

#define GFS_IOCTL_IDENTIFY      _GFSC_(35)
#define GFS_IOCTL_SUPER         _GFSC_(45)

struct gfs_ioctl {
	unsigned int gi_argc;
	char **gi_argv;

        char __user *gi_data;
	unsigned int gi_size;
	uint64_t gi_offset;
};

#endif /* ___GFS_IOCTL_DOT_H__ */
