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
#include "block_list.h"
#include "bio.h"
#include "fs_inode.h"
#include "fs_dir.h"
#include "util.h"
#include "log.h"
#include "inode_hash.h"
#include "inode.h"
#include "link.h"
#include "metawalk.h"
#include "eattr.h"

#define MAX_FILENAME 256

struct dir_status {
	uint8_t dotdir:1;
	uint8_t dotdotdir:1;
	struct block_query q;
	uint32_t entry_count;
};


static int check_leaf(struct fsck_inode *ip, uint64_t block, osi_buf_t **lbh,
		      void *private)
{
	uint64_t chain_no;
	struct fsck_sb *sbp = ip->i_sbd;
	struct gfs_leaf leaf;
	osi_buf_t *chain_head = NULL;
	osi_buf_t *bh = NULL;
	int chain=0;
	int error;

	chain_no = block;

	do {
		/* FIXME: check the range of the leaf? */
		/* check the leaf and stuff */

		error = get_and_read_buf(sbp, chain_no, &bh, 0);

		if(error){
			stack;
			goto fail;
		}

		gfs_leaf_in(&leaf, BH_DATA(bh));

		/* Check the leaf headers */

		if(!chain){
			chain = 1;
			chain_head = bh;
			chain_no = leaf.lf_next;
		}
		else {
			relse_buf(sbp, bh);
			bh = NULL;
		}
	} while(chain_no);

	*lbh = chain_head;
	return 0;

 fail:
	/* FIXME: check this error path */
	if(chain_head){
		if(chain_head == bh){ bh = NULL; }
		relse_buf(sbp, chain_head);
	}
	if(bh)
		relse_buf(sbp, bh);

	return -1;

}



/* Set children's parent inode in dir_info structure - ext2 does not set
 * dotdot inode here, but instead in pass3 - should we? */
int set_parent_dir(struct fsck_sb *sbp, uint64_t childblock,
		   uint64_t parentblock)
{
	osi_list_t *tmp;
	struct dir_info *di;

	osi_list_foreach(tmp, &sbp->dir_list) {
		di = osi_list_entry(tmp, struct dir_info, list);
		if(di->dinode == childblock) {
			if (di->treewalk_parent) {
				log_err("Another directory already contains"
					" this child\n");
				return 1;
			}
			di->treewalk_parent = parentblock;
			break;
		}
	}
	if(tmp == &sbp->dir_list) {
		log_err("Unable to find block %"PRIu64" in dir_info list\n",
			childblock);
		return -1;
	}

	return 0;
}

/* Set's the child's '..' directory inode number in dir_info structure */
int set_dotdot_dir(struct fsck_sb *sbp, uint64_t childblock,
		   uint64_t parentblock)
{
	osi_list_t *tmp;
	struct dir_info *di;

	osi_list_foreach(tmp, &sbp->dir_list) {
		di = osi_list_entry(tmp, struct dir_info, list);
		if(di->dinode == childblock) {
			/* Special case for root inode because we set
			 * it earlier */
			if(di->dotdot_parent && sbp->sb.sb_root_di.no_addr
			   != di->dinode) {
				/* This should never happen */
				log_crit("dotdot parent already set for"
					 " block %"PRIu64" -> %"PRIu64"\n",
					 childblock, di->dotdot_parent);
				return -1;
			}
			di->dotdot_parent = parentblock;
			break;
		}
	}
	if(tmp == &sbp->dir_list) {
		log_err("Unable to find block %"PRIu64" in dir_info list\n",
			childblock);
		return -1;
	}

	return 0;

}

static int check_eattr_indir(struct fsck_inode *ip, uint64_t block,
			    uint64_t parent, osi_buf_t **bh, void *private)
{

	return 0;
}
static int check_eattr_leaf(struct fsck_inode *ip, uint64_t block,
			    uint64_t parent, osi_buf_t **bh, void *private)
{
	osi_buf_t *leaf_bh;

	if(get_and_read_buf(ip->i_sbd, block, &leaf_bh, 0)){
		log_warn("Unable to read EA leaf block #%"PRIu64".\n",
			 leaf_blk);
		block_set(ip->i_sbd->bl, leaf_blk, meta_inval);
		return 1;
	}


	return 0;
}

static int check_file_type(uint16_t de_type, uint8_t block_type) {
	switch(block_type) {
	case inode_dir:
		if(de_type != GFS_FILE_DIR)
			return 1;
		break;
	case inode_file:
		if(de_type != GFS_FILE_REG)
			return 1;
		break;
	case inode_lnk:
		if(de_type != GFS_FILE_LNK)
			return 1;
		break;
	case inode_blk:
		if(de_type != GFS_FILE_BLK)
			return 1;
		break;
	case inode_chr:
		if(de_type != GFS_FILE_CHR)
			return 1;
		break;
	case inode_fifo:
		if(de_type != GFS_FILE_FIFO)
			return 1;
		break;
	case inode_sock:
		if(de_type != GFS_FILE_SOCK)
			return 1;
		break;
	default:
		log_err("Invalid block type\n");
		return -1;
		break;
	}
	return 0;
}

/* FIXME: should maybe refactor this a bit - but need to deal with
 * FIXMEs internally first */
int check_dentry(struct fsck_inode *ip, struct gfs_dirent *de,
		 osi_buf_t *bh, char *filename, int *update, void *priv)
{
	struct fsck_sb *sbp = ip->i_sbd;
	struct block_query q = {0};
	char tmp_name[MAX_FILENAME];
	osi_filename_t osifile;
	uint64_t entryblock;
	struct dir_status *ds = (struct dir_status *) priv;
	int error;
	struct fsck_inode *entry_ip = NULL;
	struct metawalk_fxns clear_eattrs = {0};
	clear_eattrs.check_eattr_indir = clear_eattr_indir;
	clear_eattrs.check_eattr_leaf = clear_eattr_leaf;
	clear_eattrs.check_eattr_entry = clear_eattr_entry;
	clear_eattrs.check_eattr_extentry = clear_eattr_extentry;

	entryblock = de->de_inum.no_addr;

	/* Start of checks */
	if(check_range(ip->i_sbd, entryblock)) {
		log_err("Block # referenced by directory entry %s is out of range\n",
			filename);
		if(query(ip->i_sbd, "Clear directory entry tp out of range block? (y/n) ")) {
			log_err("Clearing %s\n", filename);
			*update = 1;
			osifile.name = filename;
			osifile.len = strlen(filename);
			if(fs_dirent_del(ip, bh, &osifile)) {
				stack;
				return -1;
			}
			return 1;
		} else {
			log_err("Directory entry to out of range block remains\n");
			return 1;
		}
	}
	if(block_check(sbp->bl, de->de_inum.no_addr, &q)) {
		stack;
		return -1;
	}
	/* Get the status of the directory inode */
	if(q.bad_block) {
		/* This entry's inode has bad blocks in it */

		/* FIXME: user interface */
		/* FIXME: do i want to kill the inode here? */
		/* Handle bad blocks */
		log_err("Found a bad directory entry: %s\n", filename);

		if(query(sbp, "Clear entry to inode containing bad blocks? (y/n)")) {

			osifile.name = filename;
			osifile.len = strlen(filename);

			load_inode(sbp, de->de_inum.no_addr, &entry_ip);
			check_inode_eattr(entry_ip, &clear_eattrs);
			free_inode(&entry_ip);

			/* FIXME: make sure all blocks referenced by
			 * this inode are cleared in the bitmap */

			fs_dirent_del(ip, bh, &osifile);

			block_set(sbp->bl, de->de_inum.no_addr, meta_inval);
		}

		return 1;
	}
	if(q.block_type != inode_dir && q.block_type != inode_file &&
	   q.block_type != inode_lnk && q.block_type != inode_blk &&
	   q.block_type != inode_chr && q.block_type != inode_fifo &&
	   q.block_type != inode_sock) {
		log_err("Found directory entry '%s' to something"
			" not a file or directory!\n", filename);
		log_debug("block #%"PRIu64" in %"PRIu64"\n",
			  de->de_inum.no_addr, ip->i_num.no_addr);

		if(query(sbp, "Clear directory entry to non-inode block? (y/n) ")) {
			*update = 1;

			/* FIXME: make sure all blocks referenced by
			 * this inode are cleared in the bitmap */

			osifile.name = filename;
			osifile.len = strlen(filename);
			if(fs_dirent_del(ip, bh, &osifile)) {
				stack;
				return -1;
			}
			return 1;
		} else {
			log_err("Directory entry to non-inode block remains\n");
			return 1;
		}
	}


	if (de->de_rec_len < GFS_DIRENT_SIZE(de->de_name_len)){
		log_err("Dir entry with bad record or name length\n"
			"\tRecord length = %u\n"
			"\tName length = %u\n",
			de->de_rec_len,
			de->de_name_len);
		/* FIXME: should probably delete the entry here at the
		 * very least - maybe look at attempting to fix it */
		return -1;
	}


	/* FIXME: This should probably go to the top of the fxn, and
	 * references to filename should be replaced with tmp_name */
	memset(tmp_name, 0, MAX_FILENAME);
	if(de->de_name_len < MAX_FILENAME){
		strncpy(tmp_name, filename, de->de_name_len);
	} else {
		strncpy(tmp_name, filename, MAX_FILENAME - 1);
	}

	error = check_file_type(de->de_type, q.block_type);
	if(error < 0) {
		stack;
		return -1;
	}
	if(error > 0) {
		log_warn("Type in dir entry (%s, %"PRIu64") conflicts with "
			 "type in dinode. (Dir entry is stale.)\n",
			 tmp_name, de->de_inum.no_addr);
		if(query(sbp, "Clear stale directory entry? (y/n) ")) {
			load_inode(sbp, de->de_inum.no_addr, &entry_ip);
			check_inode_eattr(entry_ip, &clear_eattrs);
			free_inode(&entry_ip);

			osifile.name = tmp_name;
			osifile.len = strlen(tmp_name);

			if(fs_dirent_del(ip, bh, &osifile)) {
				stack;
				return -1;
			}
			return 1;
		} else {
			log_err("Stale directory entry remains\n");
			return 1;
		}
	}

	if(!strcmp(".", tmp_name)) {

		if(ds->dotdir) {
			log_err("already found '.' entry\n");
			if(query(sbp, "Clear duplicate '.' entry? (y/n) ")) {
				osifile.name = ".";
				osifile.len = strlen(".");

				load_inode(sbp, de->de_inum.no_addr, &entry_ip);
				check_inode_eattr(entry_ip, &clear_eattrs);
				free_inode(&entry_ip);

				fs_dirent_del(ip, bh, &osifile);
				*update = 1;
				return 1;
			} else {
				log_err("Duplicate '.' entry remains\n");
				/* FIXME: Should we continue on here
				 * and check the rest of the '.'
				 * entry? */
				return 1;
			}
		}

		/* GFS does not rely on '.' being in a certain
		 * location */

		/* check that '.' refers to this inode */
		if(de->de_inum.no_addr != ip->i_num.no_addr) {
			log_err("'.' entry's value incorrect."
				"  Points to %"PRIu64
				" when it should point to %"
				PRIu64".\n",
				de->de_inum.no_addr,
				ip->i_num.no_addr);
			if(query(sbp, "Fix '.' reference? (y/n) ")) {
				de->de_inum.no_addr = ip->i_num.no_addr;
				*update = 1;
			} else {
				log_err("Invalid '.' reference remains\n");
				return 1;
			}
		}

		ds->dotdir = 1;
		increment_link(sbp, de->de_inum.no_addr);
		ds->entry_count++;

		return 0;
	}
	if(!strcmp("..", tmp_name)) {
		if(ds->dotdotdir) {
			log_err("already found '..' entry\n");
			if(query(sbp, "Clear duplicate '..' entry? (y/n) ")) {
				osifile.name = "..";
				osifile.len = strlen("..");

				load_inode(sbp, de->de_inum.no_addr, &entry_ip);
				check_inode_eattr(entry_ip, &clear_eattrs);
				free_inode(&entry_ip);

				fs_dirent_del(ip, bh, &osifile);
				*update = 1;
				return 1;
			} else {
				log_err("Duplicate '..' entry remains\n");
				/* FIXME: Should we continue on here
				 * and check the rest of the '..'
				 * entry? */
				return 1;
			}
		}

		/* GFS does not rely on '..' being in a
		 * certain location */

		/* Add the address this entry is pointing to
		 * to this inode's dotdot_parent in
		 * dir_info */
		if(set_dotdot_dir(sbp, ip->i_num.no_addr,
				  entryblock)) {
			stack;
			return -1;
		}

		ds->dotdotdir = 1;
		increment_link(sbp, de->de_inum.no_addr);
		ds->entry_count++;
		return 0;
	}

	/* After this point we're only concerned with
	 * directories */
	if(q.block_type != inode_dir) {
		increment_link(sbp, de->de_inum.no_addr);
		ds->entry_count++;
		return 0;
	}

	error = set_parent_dir(sbp, entryblock, ip->i_num.no_addr);
	if(error > 0) {
		log_err("Hard link to directory %s detected.\n", filename);

		if(query(sbp, "Clear hard link to directory? (y/n) ")) {
			*update = 1;

			log_err("Clearing %s\n", filename);
			osifile.name = filename;
			osifile.len = strlen(filename);
			fs_dirent_del(ip, bh, &osifile);

			return 1;
		} else {
			log_err("Hard link to directory remains\n");
			return 1;
		}
	}
	else if (error < 0) {
		stack;
		return -1;
	}
	increment_link(sbp, de->de_inum.no_addr);
	ds->entry_count++;
	/* End of checks */
	return 0;
}


struct metawalk_fxns pass2_fxns = {
	.private = NULL,
	.check_leaf = check_leaf,
	.check_metalist = NULL,
	.check_data = NULL,
	.check_eattr_indir = check_eattr_indir,
	.check_eattr_leaf = check_eattr_leaf,
	.check_dentry = check_dentry,
	.check_eattr_entry = NULL,
};




int build_rooti(struct fsck_sb *sbp)
{
	struct fsck_inode *ip;
	osi_buf_t *bh;
	get_and_read_buf(sbp, GFS_SB_ADDR >> sbp->fsb2bb_shift, &bh, 0);
	/* Create a new inode ondisk */
	create_inode(sbp, GFS_FILE_DIR, &ip);
	/* Attach it to the superblock's sb_root_di address */
	sbp->sb.sb_root_di.no_addr =
		sbp->sb.sb_root_di.no_formal_ino = ip->i_num.no_addr;
	/* Write out sb change */
	gfs_sb_out(&sbp->sb, BH_DATA(bh));
	write_buf(sbp, bh, 1);
	relse_buf(sbp, bh);
	sbp->rooti = ip;

	if(fs_dir_add(ip, &(osi_filename_t){".", 1},
		      &(ip->i_num), ip->i_di.di_type)){
		stack;
		log_err("Unable to add \".\" entry to new root inode\n");
		return -1;
	}

	if(fs_dir_add(ip, &(osi_filename_t){"..", 2},
		      &ip->i_num, ip->i_di.di_type)){
		stack;
		log_err("Unable to add \"..\" entry to new root inode\n");
		return -1;
	}

	block_set(sbp->bl, ip->i_num.no_addr, inode_dir);
	add_to_dir_list(sbp, ip->i_num.no_addr);

	/* Attach l+f to it */
	if(fs_mkdir(sbp->rooti, "l+f", 00700, &(sbp->lf_dip))){
		log_err("Unable to create/locate l+f directory.\n");
		return -1;
	}

	if(sbp->lf_dip){
		log_debug("Lost and Found directory inode is at "
			  "block #%"PRIu64".\n",
			  sbp->lf_dip->i_num.no_addr);
	}
	block_set(sbp->bl, sbp->lf_dip->i_num.no_addr, inode_dir);

	add_to_dir_list(sbp, sbp->lf_dip->i_num.no_addr);

	return 0;
}

/* Check root inode and verify it's in the bitmap */
int check_root_dir(struct fsck_sb *sbp)
{
	uint64_t rootblock;
	struct dir_status ds = {0};
	struct fsck_inode *ip;
	osi_buf_t b, *bh = &b;
	osi_filename_t filename;
	char tmp_name[256];
	int update=0;
	/* Read in the root inode, look at its dentries, and start
	 * reading through them */
	rootblock = sbp->sb.sb_root_di.no_addr;

	/* FIXME: check this block's validity */

	if(block_check(sbp->bl, rootblock, &ds.q)) {
		log_crit("Can't get root block %"PRIu64" from block list\n",
			 rootblock);
		/* FIXME: Need to check if the root block is out of
		 * the fs range and if it is, rebuild it.  Still can
		 * error out if the root block number is valid, but
		 * block_check fails */
		return -1;
/*		if(build_rooti(sbp)) {
			stack;
			return -1;
			}*/
	}

	/* if there are errors with the root inode here, we need to
	 * create a new root inode and get it all setup - of course,
	 * everything will be in l+f then, but we *need* a root inode
	 * before we can do any of that.
	 */
	if(ds.q.block_type != inode_dir) {
		log_err("Block %"PRIu64" marked as root inode in"
			" superblock not a directory\n", rootblock);
		if(query(sbp, "Create new root inode? (y/n) ")) {
			if(build_rooti(sbp)) {
				stack;
				return -1;
			}
		} else {
			log_err("Cannot continue without valid root inode\n");
			return -1;
		}
	}

	rootblock = sbp->sb.sb_root_di.no_addr;
	pass2_fxns.private = (void *) &ds;
	if(ds.q.bad_block) {
		/* First check that the directory's metatree is valid */
		load_inode(sbp, rootblock, &ip);
		if(check_metatree(ip, &pass2_fxns)) {
			stack;
			free_inode(&ip);
			return -1;
		}
		free_inode(&ip);
	}
	if(check_dir(sbp, rootblock, &pass2_fxns)) {
		stack;
		return -1;
	}

	if(get_and_read_buf(sbp, rootblock, &bh, 0)){
		log_err("Unable to retrieve block #%"PRIu64"\n",
			rootblock);
		block_set(sbp->bl, rootblock, meta_inval);
		return -1;
	}

	if(copyin_inode(sbp, bh, &ip)) {
		stack;
		relse_buf(sbp, bh);
		return -1;
	}

	if(check_inode_eattr(ip, &pass2_fxns)) {
		stack;
		return -1;
	}
	/* FIXME: Should not have to do this here - fs_dir_add reads
	 * the buffer too though, and commits the change to disk, so I
	 * have to reread the buffer after calling it if I'm going to
	 * make more changes */
	relse_buf(sbp, bh);

	if(!ds.dotdir) {
		log_err("No '.' entry found\n");
		sprintf(tmp_name, ".");
		filename.len = strlen(tmp_name);  /* no trailing NULL */
		filename.name = malloc(sizeof(char) * filename.len);
		memset(filename.name, 0, sizeof(char) * filename.len);
		memcpy(filename.name, tmp_name, filename.len);
		log_warn("Adding '.' entry\n");
		if(fs_dir_add(ip, &filename, &(ip->i_num),
			      ip->i_di.di_type)){
			log_err("Failed to link \".\" entry to directory.\n");
			return -1;
		}

		increment_link(ip->i_sbd, ip->i_num.no_addr);
		ds.entry_count++;
		free(filename.name);
		update = 1;
	}
	free_inode(&ip);
	if(get_and_read_buf(sbp, rootblock, &bh, 0)){
		log_err("Unable to retrieve block #%"PRIu64"\n",
			rootblock);
		block_set(sbp->bl, rootblock, meta_inval);
		return -1;
	}

	if(copyin_inode(sbp, bh, &ip)) {
		stack;
		relse_buf(sbp, bh);
		return -1;
	}

	if(ip->i_di.di_entries != ds.entry_count) {
		log_err("Entries is %d - should be %d for %"PRIu64"\n",
			ip->i_di.di_entries, ds.entry_count, ip->i_di.di_num.no_addr);
		log_warn("Fixing entries\n");
		ip->i_di.di_entries = ds.entry_count;
		update = 1;
	}

	if(update) {
		gfs_dinode_out(&ip->i_di, BH_DATA(bh));
		write_buf(sbp, bh, 0);
	}

	free_inode(&ip);
	relse_buf(sbp, bh);
	return 0;
}

/* What i need to do in this pass is check that the dentries aren't
 * pointing to invalid blocks...and verify the contents of each
 * directory. and start filling in the directory info structure*/

/**
 * pass2 - check pathnames
 *
 * verify root inode
 * directory name length
 * entries in range
 */
int pass2(struct fsck_sb *sbp, struct options *opts)
{
	uint64_t i;
	struct block_query q;
	struct dir_status ds = {0};
	struct fsck_inode *ip;
	osi_buf_t b, *bh = &b;
	osi_filename_t filename;
	char tmp_name[256];
	if(check_root_dir(sbp)) {
		stack;
		return -1;
	}

	/* Grab each directory inode, and run checks on it */
	for(i = 0; i < sbp->last_fs_block; i++) {

		/* Skip the root inode - it's checked above */
		if(i == sbp->sb.sb_root_di.no_addr)
			continue;

		if(block_check(sbp->bl, i, &q)) {
			log_err("Can't get block %"PRIu64 " from block list\n",
				i);
			return -1;
		}

		if(q.block_type != inode_dir)
			continue;

		log_info("Checking directory inode at block %"PRIu64"\n", i);


		memset(&ds, 0, sizeof(ds));
		pass2_fxns.private = (void *) &ds;
		if(ds.q.bad_block) {
			/* First check that the directory's metatree
			 * is valid */
			load_inode(sbp, i, &ip);
			if(check_metatree(ip, &pass2_fxns)) {
				stack;
				free_inode(&ip);
				return -1;
			}
			free_inode(&ip);
		}
		if(check_dir(sbp, i, &pass2_fxns)) {
			stack;
			return -1;
		}
		if(get_and_read_buf(sbp, i, &bh, 0)){
			/* This shouldn't happen since we were able to
			 * read it before */
			log_err("Unable to retrieve block #%"PRIu64
				" for directory\n",
				i);
			return -1;
		}

		if(copyin_inode(sbp, bh, &ip)) {
			stack;
			relse_buf(sbp, bh);
			return -1;
		}
		/* FIXME: Should not have to do this here - fs_dir_add reads
		 * the buffer too though, and commits the change to disk, so I
		 * have to reread the buffer after calling it if I'm going to
		 * make more changes */
		relse_buf(sbp, bh);

		if(!ds.dotdir) {
			log_err("No '.' entry found\n");
			sprintf(tmp_name, ".");
			filename.len = strlen(tmp_name);  /* no trailing NULL */
			filename.name = malloc(sizeof(char) * filename.len);
			memset(filename.name, 0, sizeof(char) * filename.len);
			memcpy(filename.name, tmp_name, filename.len);

			if(fs_dir_add(ip, &filename, &(ip->i_num),
				      ip->i_di.di_type)){
				log_err("Failed to link \".\" entry to directory.\n");
				return -1;
			}

			increment_link(ip->i_sbd, ip->i_num.no_addr);
			ds.entry_count++;
			free(filename.name);

		}
		free_inode(&ip);

		if(get_and_read_buf(sbp, i, &bh, 0)){
			log_err("Unable to retrieve block #%"PRIu64"\n",
				i);
			block_set(sbp->bl, i, meta_inval);
			return -1;
		}

		if(copyin_inode(sbp, bh, &ip)) {
			stack;
			relse_buf(sbp, bh);
			return -1;
		}
		if(ip->i_di.di_entries != ds.entry_count) {
			log_err("Entries is %d - should be %d for %"PRIu64"\n",
				ip->i_di.di_entries, ds.entry_count,
				ip->i_di.di_num.no_addr);
			ip->i_di.di_entries = ds.entry_count;
			gfs_dinode_out(&ip->i_di, BH_DATA(bh));
			write_buf(sbp, bh, 0);
		}
		free_inode(&ip);
		relse_buf(sbp, bh);
	}
	return 0;
}



