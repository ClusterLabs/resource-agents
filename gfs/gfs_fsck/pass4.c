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

#include "stdio.h"
#include "fsck_incore.h"
#include "fsck.h"
#include "bio.h"
#include "fs_inode.h"
#include "inode_hash.h"
#include "inode.h"
#include "lost_n_found.h"

/* Updates the link count of an inode to what the fsck has seen for
 * link count */
int fix_inode_count(struct fsck_sb *sbp, struct inode_info *ii,
		    struct fsck_inode *ip)
{
	ip->i_di.di_nlink = ii->counted_links;

	log_debug("Changing inode %"PRIu64" to have %u links\n",
		  ip->i_di.di_num.no_addr, ii->counted_links);

	fs_copyout_dinode(ip);

	return 0;
}

int scan_inode_list(struct fsck_sb *sbp, osi_list_t *list) {
	osi_list_t *tmp;
	struct inode_info *ii;
	struct fsck_inode *ip;
	int lf_addition = 0;
	struct block_query q;

	/* FIXME: should probably factor this out into a generic
	 * scanning fxn */
	osi_list_foreach(tmp, list) {
		ii = osi_list_entry(tmp, struct inode_info, list);
		/* Don't check reference counts on the special gfs files */
		if((ii->inode == sbp->sb.sb_rindex_di.no_addr) ||
		   (ii->inode == sbp->sb.sb_jindex_di.no_addr) ||
		   (ii->inode == sbp->sb.sb_quota_di.no_addr) ||
		   (ii->inode == sbp->sb.sb_license_di.no_addr))
			break;
		log_info("Checking reference count on inode at block %"PRIu64
			 "\n", ii->inode);
		if(ii->counted_links == 0) {
			if(block_check(sbp->bl, ii->inode, &q)) {
				stack;
				return -1;
			}
			if(q.bad_block) {
				log_err("Found unlinked inode containing"
					"bad block - clearing...\n");
				block_set(sbp->bl, ii->inode, block_free);
			}
			else {
				log_err("Found unlinked inode at %"PRIu64"\n",
					ii->inode);
				load_inode(sbp, ii->inode, &ip);
				/* We don't want to clear zero-size
				 * files with eattrs - they might
				 * relevent * info in them. */
				if(!ip->i_di.di_size && !ip->i_di.di_eattr){
					log_warn("File has zero size,"
						 " skipping l+f addition.\n");
					/* FIXME: User input */
					log_err("Removing zero-size file from bitmaps\n");
					block_set(sbp->bl, ii->inode, block_free);
				} else {
					if(add_inode_to_lf(ip)) {
						stack;
					}
					else {
						lf_addition = 1;
					}
				}
			}
			free_inode(&ip);
		}
		else if(ii->link_count != ii->counted_links) {
			log_err("Link count inconsistent for inode %"PRIu64
				" - %u %u\n",
				ii->inode, ii->link_count, ii->counted_links);
			/* Read in the inode, adjust the link count,
			 * and write it back out */
			/* FIXME: User input */
			load_inode(sbp, ii->inode, &ip);
			fix_inode_count(sbp, ii, ip);
			free_inode(&ip);
		}
		log_debug("block %"PRIu64" has link count %d\n", ii->inode,
			  ii->link_count);
	}

	if (lf_addition) {
		ii = inode_hash_search(sbp->inode_hash,
				       sbp->lf_dip->i_num.no_addr);
		load_inode(sbp, ii->inode, &ip);
		fix_inode_count(sbp, ii, ip);
		free_inode(&ip);
	}


	return 0;
}

/**
 * pass4 - Check reference counts (pass 2 & 6 in current fsck)
 *
 * handle unreferenced files
 * lost+found errors (missing, not a directory, no space)
 * adjust link count
 * handle unreferenced inodes of other types
 * handle bad blocks
 */
int pass4(struct fsck_sb *sbp, struct options *opts)
{
	uint32_t i;
	osi_list_t *list;

	for (i = 0; i < FSCK_HASH_SIZE; i++) {
		list = &sbp->inode_hash[i];
		if(scan_inode_list(sbp, list)) {
			stack;
			return -1;
		}
	}

	return 0;
}
