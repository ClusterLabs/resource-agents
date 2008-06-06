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
	log_info("Fixing inode count for %"PRIu64"\n",
		 ip->i_di.di_num.no_addr);
	if(ip->i_di.di_nlink == ii->counted_links)
		return 0;
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
		if (skip_this_pass || fsck_abort) /* if asked to skip the rest */
			return 0;
		if(!(ii = osi_list_entry(tmp, struct inode_info, list))) {
			log_crit("osi_list_foreach broken in scan_info_list!!\n");
			exit(1);
		}
		/* Don't check reference counts on the special gfs files */
		if((ii->inode == sbp->sb.sb_rindex_di.no_addr) ||
		   (ii->inode == sbp->sb.sb_jindex_di.no_addr) ||
		   (ii->inode == sbp->sb.sb_quota_di.no_addr) ||
		   (ii->inode == sbp->sb.sb_license_di.no_addr))
			continue;
		log_debug("Checking reference count on inode at block %"PRIu64
				  "\n", ii->inode);
		if(ii->counted_links == 0) {
			log_err("Found unlinked inode at %"PRIu64"\n",
				ii->inode);
			if(block_check(sbp->bl, ii->inode, &q)) {
				stack;
				return -1;
			}
			if(q.bad_block) {
				log_err("Unlinked inode contains"
					"bad blocks\n",
					ii->inode);
				if(query(sbp, "Clear unlinked inode with bad blocks? (y/n) ")) {
					block_set(sbp->bl, ii->inode, block_free);
					continue;
				} else {
					log_err("Unlinked inode with bad blocks not cleared\n");
				}
			}
			if(q.block_type != inode_dir &&
			   q.block_type != inode_file &&
			   q.block_type != inode_lnk &&
			   q.block_type != inode_blk &&
			   q.block_type != inode_chr &&
			   q.block_type != inode_fifo &&
			   q.block_type != inode_sock) {
				log_err("Unlinked block marked as inode not an inode\n");
				block_set(sbp->bl, ii->inode, block_free);
				log_err("Cleared\n");
				continue;
			}
			if(load_inode(sbp, ii->inode, &ip)) {
				stack;
				return -1;
			}
			/* We don't want to clear zero-size files with
			 * eattrs - there might be relevent info in
			 * them. */
			if(!ip->i_di.di_size && !ip->i_di.di_eattr){
				log_err("Unlinked inode has zero size\n");
				if(query(sbp, "Clear zero-size unlinked inode? (y/n) ")) {
					block_set(sbp->bl, ii->inode, block_free);
					free_inode(&ip);
					continue;
				}

			}
			if(query(sbp, "Add unlinked inode to l+f? (y/n)")) {
				if(add_inode_to_lf(ip)) {
					stack;
					free_inode(&ip);
					return -1;
				}
				else {
					fix_inode_count(sbp, ii, ip);
					lf_addition = 1;
				}
			} else {
				log_err("Unlinked inode left unlinked\n");
			}
			free_inode(&ip);
		}
		else if(ii->link_count != ii->counted_links) {
			log_err("Link count inconsistent for inode %"PRIu64
				" - %u %u\n",
				ii->inode, ii->link_count, ii->counted_links);
			/* Read in the inode, adjust the link count,
			 * and write it back out */
			if(query(sbp, "Update link count for inode %"
				 PRIu64"? (y/n) ", ii->inode)) {
				load_inode(sbp, ii->inode, &ip);
				fix_inode_count(sbp, ii, ip);
				free_inode(&ip);
				log_warn("Link count updated for inode %"
					 PRIu64"\n", ii->inode);
			} else {
				log_err("Link count for inode %"
					PRIu64" still incorrect\n", ii->inode);
			}
		}
		log_debug("block %"PRIu64" has link count %d\n", ii->inode,
			  ii->link_count);
	}

	if (lf_addition) {
		if(!(ii = inode_hash_search(sbp->inode_hash,
					    sbp->lf_dip->i_num.no_addr))) {
			log_crit("Unable to find l+f inode in inode_hash!!\n");
			return -1;
		} else {
			fix_inode_count(sbp, ii, sbp->lf_dip);
		}
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
	if(sbp->lf_dip)
		log_debug("At beginning of pass4, l+f entries is %u\n",
			  sbp->lf_dip->i_di.di_entries);
	log_info("Checking inode reference counts.\n");
	for (i = 0; i < FSCK_HASH_SIZE; i++) {
		if (skip_this_pass || fsck_abort) /* if asked to skip the rest */
			return 0;
		list = &sbp->inode_hash[i];
		if(scan_inode_list(sbp, list)) {
			stack;
			return -1;
		}
	}

	if(sbp->lf_dip)
		log_debug("At end of pass4, l+f entries is %u\n",
			  sbp->lf_dip->i_di.di_entries);
	return 0;
}
