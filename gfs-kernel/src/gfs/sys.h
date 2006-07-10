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

#ifndef __SYS_DOT_H__
#define __SYS_DOT_H__

/* Allow args to be passed to GFS when using an initial ram disk */
extern char *gfs_sys_margs;
extern spinlock_t gfs_sys_margs_lock;

int gfs_sys_fs_add(struct gfs_sbd *sdp);
void gfs_sys_fs_del(struct gfs_sbd *sdp);

int gfs_sys_init(void);
void gfs_sys_uninit(void);

#endif /* __SYS_DOT_H__ */
