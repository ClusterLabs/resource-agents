#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <dirent.h>

#include "libgfs2.h"
#include "osi_list.h"
#include "fsck.h"
#include "lost_n_found.h"
#include "link.h"
#include "metawalk.h"

static int attach_dotdot_to(struct gfs2_sbd *sbp, uint64_t newdotdot,
							uint64_t olddotdot, uint64_t block)
{
	char *filename;
	int filename_len;
	struct gfs2_inode *ip, *pip;

	ip = fsck_load_inode(sbp, block);
	pip = fsck_load_inode(sbp, newdotdot);
	/* FIXME: Need to add some interactive
	 * options here and come up with a
	 * good default for non-interactive */
	/* FIXME: do i need to correct the
	 * '..' entry for this directory in
	 * this case? */

	filename_len = strlen("..");
	if(!(filename = malloc((sizeof(char) * filename_len) + 1))) {
		log_err("Unable to allocate name\n");
		fsck_inode_put(ip, not_updated);
		fsck_inode_put(pip, not_updated);
		stack;
		return -1;
	}
	if(!memset(filename, 0, (sizeof(char) * filename_len) + 1)) {
		log_err("Unable to zero name\n");
		fsck_inode_put(ip, not_updated);
		fsck_inode_put(pip, not_updated);
		stack;
		return -1;
	}
	memcpy(filename, "..", filename_len);
	if(gfs2_dirent_del(ip, NULL, filename, filename_len))
		log_warn("Unable to remove \"..\" directory entry.\n");
	else
		decrement_link(sbp, olddotdot);
	dir_add(ip, filename, filename_len, &pip->i_di.di_num, DT_DIR);
	increment_link(sbp, newdotdot);
	fsck_inode_put(ip, updated);
	fsck_inode_put(pip, updated);
	return 0;
}

struct dir_info *mark_and_return_parent(struct gfs2_sbd *sbp,
										struct dir_info *di)
{
	struct dir_info *pdi;
	struct gfs2_block_query q_dotdot, q_treewalk;

	di->checked = 1;

	if(!di->treewalk_parent)
		return NULL;

	if(di->dotdot_parent != di->treewalk_parent) {
		log_warn("Directory '..' and treewalk connections disagree for inode %"
				 PRIu64 " (0x%" PRIx64 ")\n", di->dinode, di->dinode);
		log_notice("'..' has %" PRIu64" (0x%" PRIx64 "), treewalk has %"
				   PRIu64" (0x%" PRIx64 ")\n", di->dotdot_parent,
				   di->dotdot_parent, di->treewalk_parent,
				   di->treewalk_parent);
		if(gfs2_block_check(sbp, bl, di->dotdot_parent, &q_dotdot)) {
			log_err("Unable to find block %"PRIu64
					" (0x%" PRIx64 ") in block map.\n",
					di->dotdot_parent, di->dotdot_parent);
			return NULL;
		}
		if(gfs2_block_check(sbp, bl, di->treewalk_parent,
				    &q_treewalk)) {
			log_err("Unable to find block %"PRIu64
					" (0x%" PRIx64 ") in block map\n",
					di->treewalk_parent, di->treewalk_parent);
			return NULL;
		}
		/* if the dotdot entry isn't a directory, but the
		 * treewalk is, treewalk is correct - if the treewalk
		 * entry isn't a directory, but the dotdot is, dotdot
		 * is correct - if both are directories, which do we
		 * choose? if neither are directories, we have a
		 * problem - need to move this directory into lost+found
		 */
		if(q_dotdot.block_type != gfs2_inode_dir) {
			if(q_treewalk.block_type != gfs2_inode_dir) {
				log_err( "Orphaned directory, move to lost+found\n");
				return NULL;
			}
			else {
				log_warn("Treewalk parent is correct,"
						 " fixing dotdot -> %"PRIu64" (0x%" PRIx64 ")\n",
						 di->treewalk_parent, di->treewalk_parent);
				attach_dotdot_to(sbp, di->treewalk_parent,
								 di->dotdot_parent, di->dinode);
				di->dotdot_parent = di->treewalk_parent;
			}
		}
		else {
			if(q_treewalk.block_type != gfs2_inode_dir) {
				int error = 0;
				log_warn(".. parent is valid, but treewalk"
						 "is bad - reattaching to lost+found");

				/* FIXME: add a dinode for this entry instead? */
				if(query(&opts, "Remove directory entry for bad"
						 " inode %"PRIu64" (0x%" PRIx64 ") in %"PRIu64
						 " (0x%" PRIx64 ")? (y/n)", di->dinode, di->dinode,
						 di->treewalk_parent, di->treewalk_parent)) {
					error = remove_dentry_from_dir(sbp, di->treewalk_parent,
												   di->dinode);
					if(error < 0) {
						stack;
						return NULL;
					}
					if(error > 0) {
						log_warn("Unable to find dentry for block %"
								 PRIu64" (0x%" PRIx64 ") in %" PRIu64 " (0x%"
								 PRIx64 ")\n",di->dinode, di->dinode,
								 di->treewalk_parent, di->treewalk_parent);
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
		if(gfs2_block_check(sbp, bl, di->dotdot_parent, &q_dotdot)) {
			log_err("Unable to find parent block %"PRIu64
					" (0x%" PRIx64 ")  in block map\n",
					di->dotdot_parent, di->dotdot_parent);
			return NULL;
		}
		if(q_dotdot.block_type != gfs2_inode_dir) {
			log_err("Orphaned directory at block %" PRIu64 " (0x%" PRIx64
					") moved to lost+found\n", di->dinode, di->dinode);
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
int pass3(struct gfs2_sbd *sbp)
{
	osi_list_t *tmp;
	struct dir_info *di, *tdi;
	struct gfs2_inode *ip;
	struct gfs2_block_query q;
	int i;

	find_di(sbp, sbp->md.rooti->i_di.di_num.no_addr, &di);
	if(di) {
		log_info("Marking root inode connected\n");
		di->checked = 1;
	}
	find_di(sbp, sbp->master_dir->i_di.di_num.no_addr, &di);
	if(di) {
		log_info("Marking master directory inode connected\n");
		di->checked = 1;
	}

	/* Go through the directory list, working up through the parents
	 * until we find one that's been checked already.  If we don't
	 * find a parent, put in lost+found.
	 */
	log_info("Checking directory linkage.\n");
	for(i = 0; i < FSCK_HASH_SIZE; i++) {
	osi_list_foreach(tmp, &dir_hash[i]) {
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
				if(gfs2_block_check(sbp, bl, di->dinode, &q)) {
					stack;
					return -1;
				}
				if(q.bad_block) {
					log_err("Found unlinked directory containing bad block\n");
					if(query(&opts,
					   "Clear unlinked directory with bad blocks? (y/n) ")) {
						gfs2_block_set(sbp, bl,
							       di->dinode,
							       gfs2_block_free);
						break;
					} else
						log_err("Unlinked directory with bad block remains\n");
				}
				if(q.block_type != gfs2_inode_dir &&
				   q.block_type != gfs2_inode_file &&
				   q.block_type != gfs2_inode_lnk &&
				   q.block_type != gfs2_inode_blk &&
				   q.block_type != gfs2_inode_chr &&
				   q.block_type != gfs2_inode_fifo &&
				   q.block_type != gfs2_inode_sock) {
					log_err("Unlinked block marked as inode not an inode\n");
					gfs2_block_set(sbp, bl, di->dinode,
						       gfs2_block_free);
					log_err("Cleared\n");
					break;
				}

				log_err("Found unlinked directory at block %" PRIu64
						" (0x%" PRIx64 ")\n", di->dinode, di->dinode);
				ip = fsck_load_inode(sbp, di->dinode);
				/* Don't skip zero size directories
				 * with eattrs */
				if(!ip->i_di.di_size && !ip->i_di.di_eattr){
					log_err("Unlinked directory has zero size.\n");
					if(query(&opts, "Remove zero-size unlinked directory? (y/n) ")) {
						gfs2_block_set(sbp, bl,
							       di->dinode,
							       gfs2_block_free);
						fsck_inode_put(ip, not_updated);
						break;
					} else {
						log_err("Zero-size unlinked directory remains\n");
					}
				}
				if(query(&opts, "Add unlinked directory to lost+found? (y/n) ")) {
					if(add_inode_to_lf(ip)) {
						fsck_inode_put(ip, not_updated);
						stack;
						return -1;
					}
					log_warn("Directory relinked to lost+found\n");
				} else {
					log_err("Unlinked directory remains unlinked\n");
				}
				fsck_inode_put(ip, not_updated);
				break;
			}
			else {
				log_debug("Directory at block %" PRIu64 " (0x%" 
						  PRIx64 ") connected\n", di->dinode, di->dinode);
			}
			di = tdi;
		}
	}
	}
	if(lf_dip)
		log_debug("At end of pass3, lost+found entries is %u\n",
				  lf_dip->i_di.di_entries);
	return 0;
}
