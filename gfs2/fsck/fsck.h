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

#ifndef _FSCK_H
#define _FSCK_H

#include "libgfs2.h"

#define FSCK_HASH_SHIFT         (13)
#define FSCK_HASH_SIZE          (1 << FSCK_HASH_SHIFT)
#define FSCK_HASH_MASK          (FSCK_HASH_SIZE - 1)

struct options {
	char *device;
	int yes:1;
	int no:1;
};

struct inode_info
{
        osi_list_t list;
        uint64_t   inode;
        uint32_t   link_count;   /* the number of links the inode
                                  * thinks it has */
        uint32_t   counted_links; /* the number of links we've found */
};

struct dir_info
{
        osi_list_t list;
        uint64_t dinode;
        uint64_t treewalk_parent;
        uint64_t dotdot_parent;
        uint8_t  checked:1;

};

int initialize(struct gfs2_sbd *sbp);
void destroy(struct gfs2_sbd *sbp);
int block_mounters(struct gfs2_sbd *sbp, int block_em);
int pass1(struct gfs2_sbd *sbp);
int pass1b(struct gfs2_sbd *sbp);
int pass1c(struct gfs2_sbd *sbp);
int pass2(struct gfs2_sbd *sbp);
int pass3(struct gfs2_sbd *sbp);
int pass4(struct gfs2_sbd *sbp);
int pass5(struct gfs2_sbd *sbp);

/* FIXME: Hack to get this going for pass2 - this should be pulled out
 * of pass1 and put somewhere else... */
int add_to_dir_list(struct gfs2_sbd *sbp, uint64_t block);

extern struct options opts;
extern struct gfs2_inode *lf_dip; /* Lost and found directory inode */
extern osi_list_t dir_hash[FSCK_HASH_SIZE];
extern osi_list_t inode_hash[FSCK_HASH_SIZE];
extern struct gfs2_block_list *bl;
extern uint64_t last_fs_block;
extern uint64_t last_data_block;
extern uint64_t first_data_block;
extern osi_list_t dup_list;

#endif /* _FSCK_H */
