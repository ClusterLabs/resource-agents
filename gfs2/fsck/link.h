/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/


#ifndef _LINK_H
#define _LINK_H

int set_link_count(struct fsck_sb *sbp, uint64_t inode_no, uint32_t count);
int increment_link(struct fsck_sb *sbp, uint64_t inode_no);
int decrement_link(struct fsck_sb *sbp, uint64_t inode_no);

#endif /* _LINK_H */
