#include <stdio.h>
#include "osi_list.h"
#include "fsck_incore.h"
#include "fsck.h"
#include "inode.h"
#include "lost_n_found.h"
#include "block_list.h"
#include "fs_dir.h"
#include "link.h"
#include "metawalk.h"

static int attach_dotdot_to(struct fsck_sb *sbp, uint64_t newdotdot,
			    uint64_t olddotdot, uint64_t block)
{
	osi_filename_t filename;
	struct fsck_inode *ip, *pip;

	load_inode(sbp, block, &ip);
	load_inode(sbp, newdotdot, &pip);
	/* FIXME: Need to add some interactive
	 * options here and come up with a
	 * good default for non-interactive */
	/* FIXME: do i need to correct the
	 * '..' entry for this directory in
	 * this case? */

	filename.len = strlen("..");
	if(!(filename.name = malloc(sizeof(char) * filename.len))) {
		log_err("Unable to allocate name\n");
		stack;
		return -1;
	}
	if(!memset(filename.name, 0, sizeof(char) * filename.len)) {
		log_err("Unable to zero name\n");
		stack;
		return -1;
	}
	memcpy(filename.name, "..", filename.len);
	if(fs_dirent_del(ip, NULL, &filename)){
		log_warn("Unable to remove \"..\" directory entry.\n");
	}
	else {
		decrement_link(sbp, olddotdot);
	}
	if(fs_dir_add(ip, &filename, &pip->i_num,
		      pip->i_di.di_type)){
		log_err("Failed to link \"..\" entry to directory.\n");
		block_set(ip->i_sbd->bl, ip->i_num.no_addr, meta_inval);
		free_inode(&ip);
		free_inode(&pip);
		return -1;
	}
	increment_link(sbp, newdotdot);
	free_inode(&ip);
	free_inode(&pip);
	return 0;
}

struct dir_info *mark_and_return_parent(struct fsck_sb *sbp,
					struct dir_info *di)
{
	struct dir_info *pdi;

	struct block_query q_dotdot, q_treewalk;

	di->checked = 1;

	if(!di->treewalk_parent) {
		return NULL;
	}

	if(di->dotdot_parent != di->treewalk_parent) {
		log_warn(".. and treewalk conections are not the same for %"PRIu64
			 "\n", di->dinode);
		log_notice("%"PRIu64" %"PRIu64"\n", di->dotdot_parent, di->treewalk_parent);
		if(block_check(sbp->bl, di->dotdot_parent, &q_dotdot)) {
			log_err("Unable to find block %"PRIu64
				" in block map\n",
				di->dotdot_parent);
			return NULL;
		}
		if(block_check(sbp->bl, di->treewalk_parent, &q_treewalk)) {
			log_err("Unable to find block %"PRIu64
				" in block map\n",
				di->treewalk_parent);
			return NULL;
		}
		/* if the dotdot entry isn't a directory, but the
		 * treewalk is, treewalk is correct - if the treewalk
		 * entry isn't a directory, but the dotdot is, dotdot
		 * is correct - if both are directories, which do we
		 * choose? if neither are directories, we have a
		 * problem - need to move this directory into l+f
		 */
		if(q_dotdot.block_type != inode_dir) {
			if(q_treewalk.block_type != inode_dir) {
				log_err( "Orphaned directory, move to l+f\n");
				return NULL;
			}
			else {
				log_warn("Treewalk parent is correct,"
					 " fixing dotdot -> %"PRIu64"\n",
					 di->treewalk_parent);
				attach_dotdot_to(sbp, di->treewalk_parent,
						 di->dotdot_parent, di->dinode);
				di->dotdot_parent = di->treewalk_parent;

			}
		}
		else {
			if(q_treewalk.block_type != inode_dir) {
				int error = 0;
				log_warn(".. parent is valid, but treewalk"
					 "is bad - reattaching to l+f");

				/* FIXME: add a dinode for this entry instead? */
				if(query(sbp, "Remove directory entry for bad"
					 " inode %"PRIu64" in %"PRIu64
					 "? (y/n)", di->dinode,
					 di->treewalk_parent)) {
					error = remove_dentry_from_dir(sbp,
								       di->treewalk_parent,
								       di->dinode);
					if(error < 0) {
						stack;
						return NULL;
					}
					if(error > 0) {
						log_warn("Unable to find dentry for %"
							 PRIu64" in %"PRIu64"\n",
							 di->dinode, di->treewalk_parent);
					}
					log_warn("Directory entry removed\n");
				} else {
					log_err("Directory entry to invalid inode remains\n");
				}
				log_info("Marking directory unlinked\n");

				return NULL;
			}
			else {
				log_err("Both .. and treewalk parents are "
					"directories, going with treewalk for "
					"now...\n");
				attach_dotdot_to(sbp, di->treewalk_parent,
						 di->dotdot_parent, di->dinode);
				di->dotdot_parent = di->treewalk_parent;

			}
		}
	}
	else {
		if(block_check(sbp->bl, di->dotdot_parent, &q_dotdot)) {
			log_err("Unable to find parent block %"PRIu64
				" in block map\n",
				di->dotdot_parent);
			return NULL;
		}
		if(q_dotdot.block_type != inode_dir) {
			log_err("Orphaned directory, move to l+f (Block #%"
				PRIu64")\n", di->dinode);
			return NULL;
		}
	}
	find_di(sbp, di->dotdot_parent, &pdi);

	return pdi;

}

/**
 * pass3 - check connectivity of directories
 *
 * handle disconnected directories
 * handle lost+found directory errors (missing, not a directory, no space)
 */
int pass3(struct fsck_sb *sbp, struct options *opts)
{
	osi_list_t *tmp;
	struct dir_info *di, *tdi;
	struct fsck_inode *ip;
	struct block_query q;
	int i;

	find_di(sbp, sbp->sb.sb_root_di.no_addr, &di);
	if(di) {
		log_info("Marking root inode connected\n");
		di->checked = 1;
	}

	/* Go through the directory list, working up through the parents
	 * until we find one that's been checked already.  If we don't
	 * find a parent, put in lost+found.
	 */
	log_info("Checking directory linkage.\n");
	for(i = 0; i < FSCK_HASH_SIZE; i++) {
	osi_list_foreach(tmp, &sbp->dir_hash[i]) {
		di = osi_list_entry(tmp, struct dir_info, list);
		while(!di->checked) {
			/* FIXME: Change this so it returns success or
			 * failure and put the parent inode in a
			 * param */
			if (skip_this_pass || fsck_abort) /* if asked to skip the rest */
				return 0;
			tdi = mark_and_return_parent(sbp, di);

			/* FIXME: Factor this ? */
			if(!tdi) {
				if(block_check(sbp->bl, di->dinode, &q)) {
					stack;
					return -1;
				}
				if(q.bad_block) {
					log_err("Found unlinked directory containing"
						"bad block\n");
					if(query(sbp, "Clear unlinked directory with bad blocks? (y/n) ")) {
						block_set(sbp->bl, di->dinode, block_free);
						break;
					} else {
						log_err("Unlinked directory with bad blocks remains\n");
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
					block_set(sbp->bl, di->dinode, block_free);
					log_err("Cleared\n");
					break;
				}

				log_err("Found unlinked directory %"PRIu64"\n", di->dinode);
				load_inode(sbp, di->dinode, &ip);
				/* Don't skip zero size directories
				 * with eattrs */
				if(!ip->i_di.di_size && !ip->i_di.di_eattr){
					log_err("Unlinked directory has zero size.\n");
					if(query(sbp, "Remove zero-size unlinked directory? (y/n) ")) {
						block_set(sbp->bl, di->dinode, block_free);
						free_inode(&ip);
						break;
					} else {
						log_err("Zero-size unlinked directory remains\n");
					}
				}
				if(query(sbp, "Add unlinked directory to l+f? (y/n) ")) {
					if(add_inode_to_lf(ip)) {
						stack;
						return -1;
					}
					log_warn("Directory relinked to l+f\n");
				} else {
					log_err("Unlinked directory remains unlinked\n");
				}
				free_inode(&ip);
				break;
			}
			else {
				log_debug("Directory at block %" PRIu64 " connected\n",
						 di->dinode);
			}
			di = tdi;
		}
	}
	}
	if(sbp->lf_dip)
		log_debug("At end of pass3, l+f entries is %u\n",
			  sbp->lf_dip->i_di.di_entries);
	return 0;
}
