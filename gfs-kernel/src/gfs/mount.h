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

#ifndef __MOUNT_DOT_H__
#define __MOUNT_DOT_H__

int gfs_make_args(char *data, struct gfs_args *args);

/*  Allow args to be passed to GFS when using an initial ram disk  */

extern char *gfs_mount_args;
extern struct semaphore gfs_mount_args_lock;

int gfs_proc_write(struct file *file, const char *buffer,
		   unsigned long count, void *data);

#endif /* __MOUNT_DOT_H__ */
