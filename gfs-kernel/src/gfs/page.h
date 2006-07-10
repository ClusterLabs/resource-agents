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

#ifndef __PAGE_DOT_H__
#define __PAGE_DOT_H__

void gfs_inval_pte(struct gfs_glock *gl);
void gfs_inval_page(struct gfs_glock *gl);
void gfs_sync_page_i(struct inode *inode, int flags);
void gfs_sync_page(struct gfs_glock *gl, int flags);

int gfs_unstuffer_page(struct gfs_inode *ip, struct buffer_head *dibh,
		       uint64_t block, void *private);
int gfs_truncator_page(struct gfs_inode *ip, uint64_t size);

#endif /* __PAGE_DOT_H__ */
