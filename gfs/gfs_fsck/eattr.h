/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef _EATTR_H
#define _EATTR_H

int clear_eattr_indir(struct fsck_inode *ip, uint64_t block,
		      uint64_t parent, osi_buf_t **bh,
		      void *private);
int clear_eattr_leaf(struct fsck_inode *ip, uint64_t block,
		     uint64_t parent, osi_buf_t **bh,
		     void *private);
int clear_eattr_entry (struct fsck_inode *ip,
		       osi_buf_t *leaf_bh,
		       struct gfs_ea_header *ea_hdr,
		       struct gfs_ea_header *ea_hdr_prev,
		       void *private);
int clear_eattr_extentry(struct fsck_inode *ip, uint64_t *ea_data_ptr,
			 osi_buf_t *leaf_bh, struct gfs_ea_header *ea_hdr,
			 struct gfs_ea_header *ea_hdr_prev, void *private);



#endif /* _EATTR_H */
