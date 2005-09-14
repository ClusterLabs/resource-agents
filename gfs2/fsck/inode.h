/*****************************************************************************
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

#ifndef _INODE_H
#define _INODE_H


int copyin_inode(struct fsck_sb *sbp, struct buffer_head *bh,
		 struct fsck_inode **ip);
int load_inode(struct fsck_sb *sbp, uint64_t block, struct fsck_inode **ip);
void free_inode(struct fsck_inode **inode);
int check_inode(struct fsck_inode *ip);
int create_inode(struct fsck_sb *sbp, unsigned int type,
		 struct fsck_inode **ip);


#endif /* _INODE_H */
