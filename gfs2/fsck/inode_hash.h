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

#ifndef _INODE_HASH_H
#define _INODE_HASH_H

struct inode_info *inode_hash_search(osi_list_t *buckets, uint64_t block_no);
int inode_hash_insert(osi_list_t *buckets, uint64_t key,
					  struct inode_info *ii);
int inode_hash_remove(osi_list_t *buckets, uint64_t key);

#endif /* _INODE_HASH_H */
