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

#ifndef __PROC_DOT_H__
#define __PROC_DOT_H__

/* Allow args to be passed to GFS when using an initial ram disk */
extern char *gfs_proc_margs;
extern spinlock_t gfs_proc_margs_lock;

void gfs_proc_fs_add(struct gfs_sbd *sdp);
void gfs_proc_fs_del(struct gfs_sbd *sdp);

int gfs_proc_init(void);
void gfs_proc_uninit(void);

#endif /* __PROC_DOT_H__ */
