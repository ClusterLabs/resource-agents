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

#include <stdint.h>
#include "fsck_incore.h"
#include "inode_hash.h"
#include "link.h"

int set_link_count(struct fsck_sb *sbp, uint64_t inode_no, uint32_t count)
{
	struct inode_info *ii = NULL;

	/* If the list has entries, look for one that matches
	 * inode_no */
	ii = inode_hash_search(sbp->inode_hash, inode_no);
	if(ii) {
		if(ii->link_count) {
			log_err("Link count already set for inode #%"
				PRIu64"!\n");
			stack;
			return -1;
		}
		else {
			ii->link_count = count;
		}
	}
	else {
		/* If not match was found, add a new entry and set it's
		 * link count to count*/
		if(!(ii = (struct inode_info *) malloc(sizeof(*ii)))) {
			log_err("Unable to allocate inode_info structure\n");
			stack;
			return -1;
		}
		memset(ii, 0, sizeof(*ii));
		ii->inode = inode_no;
		ii->link_count = count;
		inode_hash_insert(sbp->inode_hash, inode_no, ii);
	}
	return 0;


}

int increment_link(struct fsck_sb *sbp, uint64_t inode_no)
{
	struct inode_info *ii = NULL;

	ii = inode_hash_search(sbp->inode_hash, inode_no);
	/* If the list has entries, look for one that matches
	 * inode_no */
	if(ii) {
		ii->counted_links++;
		return 0;
	}
	log_debug("No match found when incrementing link for %"PRIu64"!\n",
		  inode_no);
	/* If no match was found, add a new entry and set its
	 * counted links to 1 */
	if(!(ii = (struct inode_info *) malloc(sizeof(*ii)))) {
		log_err("Unable to allocate inode_info structure\n");
		stack;
		return -1;
	}
	memset(ii, 0, sizeof(*ii));
	ii->inode = inode_no;
	ii->counted_links = 1;
	inode_hash_insert(sbp->inode_hash, inode_no, ii);

	return 0;
}

int decrement_link(struct fsck_sb *sbp, uint64_t inode_no)
{
	struct inode_info *ii = NULL;

	ii = inode_hash_search(sbp->inode_hash, inode_no);
	/* If the list has entries, look for one that matches
	 * inode_no */
	log_err("Decrementing %"PRIu64"\n", inode_no);
	if(ii) {
		ii->counted_links--;
		return 0;
	}
	log_debug("No match found when decrementing link for %"PRIu64"!\n",
		  inode_no);
	return -1;

}


