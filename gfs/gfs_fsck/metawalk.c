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

#include "fsck_incore.h"
#include "fsck.h"
#include "bio.h"
#include "fs_dir.h"
#include "inode.h"

#include "metawalk.h"

int check_entries(struct fsck_inode *ip, osi_buf_t *bh, int index,
		  int type, int *update, uint16_t *count,
		  struct metawalk_fxns *pass)
{
	struct gfs_leaf *leaf = NULL;
	struct gfs_dirent *dent;
	struct gfs_dirent de, p, *prev;
	int error = 0;
	char *bh_end;
	char *filename;
	int first = 1;

	bh_end = BH_DATA(bh) + BH_SIZE(bh);

	if(type == DIR_LINEAR) {
		dent = (struct gfs_dirent *)(BH_DATA(bh)
					     + sizeof(struct gfs_dinode));
	}
	else if (type == DIR_EXHASH) {
		dent = (struct gfs_dirent *)(BH_DATA(bh)
					     + sizeof(struct gfs_leaf));
		leaf = (struct gfs_leaf *)BH_DATA(bh);
		log_debug("Checking leaf %"PRIu64"\n", BH_BLKNO(bh));
	}
	else {
		log_err("Invalid directory type %d specified\n", type);
		return -1;
	}

	prev = NULL;
	if(!pass->check_dentry) {
		return 0;
	}

	while(1) {
		memset(&de, 0, sizeof(struct gfs_dirent));
		gfs_dirent_in(&de, (char *)dent);
		filename = (char *)dent + sizeof(struct gfs_dirent);

		if (!de.de_inum.no_formal_ino){
			if(first){
				log_debug("First dirent is a sentinel (place holder).\n");
				first = 0;
			} else {
				/* FIXME: Do something about this */
				log_err("Bad entry!\n");
				return 1;
			}
		} else {
			if(first)
				first = 0;

			error = pass->check_dentry(ip, dent, prev, bh,
						   filename, update,
						   count,
						   pass->private);
			if(error < 0) {
				stack;
				return -1;
			}
			/*if(error > 0) {
			  return 1;
			  }*/
		}

		if ((char *)dent + de.de_rec_len >= bh_end){
			log_debug("Last entry processed.\n");
			break;
		}

		if(!error) {
			prev = dent;
		}
		dent = (struct gfs_dirent *)((char *)dent + de.de_rec_len);
	}
	return 0;
}



/* Checks exthash directory entries */
int check_leaf(struct fsck_inode *ip, int *update, struct metawalk_fxns *pass)
{
	int error;
	struct gfs_leaf leaf;
	uint64_t leaf_no, old_leaf;
	osi_buf_t *lbh;
	int index;
	struct fsck_sb *sbp = ip->i_sbd;
	uint16_t count;
	int ref_count = 0, exp_count = 0;

	old_leaf = 0;
	for(index = 0; index < (1 << ip->i_di.di_depth); index++) {
		if(get_leaf_nr(ip, index, &leaf_no)) {
			log_err("Unable to get leaf block number in dir %"
				PRIu64"\n"
				"\tDepth = %u\n"
				"\tindex = %u\n",
				ip->i_num.no_addr,
				ip->i_di.di_depth,
				index);
			return -1;
		}

		/* GFS has multiple indirect pointers to the same leaf
		 * until those extra pointers are needed, so skip the
		 * dups */
		if(old_leaf == leaf_no) {
			ref_count++;
			continue;
		} else {
			if(ref_count != exp_count){
				log_err("Dir #%"PRIu64" has an incorrect number "
					 "of pointers to leaf #%"PRIu64"\n"
					 "\tFound: %u,  Expected: %u\n",
					 ip->i_num.no_addr,
					 old_leaf,
					 ref_count,
					 exp_count);
				return 1;
			}
			ref_count = 1;
		}

		count = 0;
		do {
			/* FIXME: Do other checks (see old
			 * pass3:dir_exhash_scan() */
			lbh = NULL;
			if(pass->check_leaf) {
				error = pass->check_leaf(ip, leaf_no, &lbh,
							 pass->private);
				if(error < 0) {
					stack;
					relse_buf(sbp, lbh);
					return -1;
				}
				if(error > 0) {
					relse_buf(sbp, lbh);
					lbh = NULL;
					return 1;
				}
			}

			if (!lbh){
				if(get_and_read_buf(sbp, leaf_no,
						    &lbh, 0)){
					log_err("Unable to read leaf block #%"
						PRIu64" for "
						"directory #%"PRIu64".\n",
						leaf_no,
						ip->i_di.di_num.no_addr);
					/* FIXME: should i error out
					 * if this fails? */
					break;
				}
			}
			gfs_leaf_in(&leaf, BH_DATA(lbh));

			exp_count = (1 << (ip->i_di.di_depth - leaf.lf_depth));

			if(pass->check_dentry && 
			   ip->i_di.di_type == GFS_FILE_DIR) {
				error = check_entries(ip, lbh, index,
						      DIR_EXHASH, update,
						      &count,
						      pass);

				if(error < 0) {
					stack;
					relse_buf(sbp, lbh);
					return -1;
				}

				if(error > 0) {
					relse_buf(sbp, lbh);
					return 1;
				}

				if(update && (count != leaf.lf_entries)) {
					log_err("Leaf(%"PRIu64") entry count in directory %"PRIu64" doesn't match number of entries found - is %u, found %u\n", leaf_no, ip->i_num.no_addr, leaf.lf_entries, count);
					if(query(sbp, "Update leaf entry count(y/n) ")) {
						leaf.lf_entries = count;
						gfs_leaf_out(&leaf, BH_DATA(lbh));
						write_buf(sbp, lbh, 0);
						log_warn("Leaf entry count updated\n");
					} else {
						log_err("Leaf entry count left in inconsistant state\n");
					}
				}
				/* FIXME: Need to get entry count and
				 * compare it against
				 * leaf->lf_entries */

				relse_buf(sbp, lbh);
				break;
			} else {
				relse_buf(sbp, lbh);
				if(!leaf.lf_next) {
					break;
				}
				leaf_no = leaf.lf_next;
				log_debug("Leaf chain detected.\n");
			}
		} while(1);
		old_leaf = leaf_no;
	}
	return 0;
}

static int check_eattr_entries(struct fsck_inode *ip, osi_buf_t *bh,
			       struct metawalk_fxns *pass)
{
	struct gfs_ea_header *ea_hdr, *ea_hdr_prev = NULL;
	uint64_t *ea_data_ptr = NULL;
	int i;
	int error = 0;

	if(!pass->check_eattr_entry) {
		return 0;
	}

	ea_hdr = (struct gfs_ea_header *)(BH_DATA(bh) +
					  sizeof(struct gfs_meta_header));

	while(1){
		error = pass->check_eattr_entry(ip, bh, ea_hdr, ea_hdr_prev,
						pass->private);
		if(error < 0) {
			stack;
			return -1;
		}
		if(error == 0) {
			if(pass->check_eattr_extentry && ea_hdr->ea_num_ptrs) {
				ea_data_ptr = ((uint64_t *)((char *)ea_hdr +
							    sizeof(struct gfs_ea_header) +
							    ((ea_hdr->ea_name_len + 7) & ~7)));

				/* It is possible when a EA is shrunk
				** to have ea_num_ptrs be greater than
				** the number required for ** data.
				** In this case, the EA ** code leaves
				** the blocks ** there for **
				** reuse...........  */
				for(i = 0; i < ea_hdr->ea_num_ptrs; i++){
					if(pass->check_eattr_extentry(ip,
								      ea_data_ptr,
								      bh, ea_hdr,
								      ea_hdr_prev,
								      pass->private)) {
						stack;
						return -1;
					}
					ea_data_ptr++;
				}
			}
		}
		if(ea_hdr->ea_flags & GFS_EAFLAG_LAST){
			/* FIXME: better equal the end of the block */
			break;
		}
		/* FIXME: be sure this doesn't go beyond the end */
		ea_hdr_prev = ea_hdr;
		ea_hdr = (struct gfs_ea_header *)
			((char *)(ea_hdr) +
			 gfs32_to_cpu(ea_hdr->ea_rec_len));
	}

	return 0;
}

/**
 * check_leaf_eattr
 * @ip: the inode the eattr comes from
 * @leaf_blk: block number of the leaf
 *
 * Returns: 0 on success, -1 if removal is needed
 */
static int check_leaf_eattr(struct fsck_inode *ip, uint64_t leaf_blk,
			    uint64_t parent, struct metawalk_fxns *pass)
{
	osi_buf_t *bh = NULL;
	int error = 0;
	log_debug("Checking EA leaf block #%"PRIu64".\n", leaf_blk);

	if(pass->check_eattr_leaf) {
		error = pass->check_eattr_leaf(ip, leaf_blk, parent,
					       &bh, pass->private);
		if(error < 0) {
			stack;
			return -1;
		}
		if(error > 0) {
			relse_buf(ip->i_sbd, bh);
			return 1;
		}
	}

	check_eattr_entries(ip, bh, pass);

	relse_buf(ip->i_sbd, bh);

	return 0;
}





/**
 * check_indirect_eattr
 * @ip: the inode the eattr comes from
 * @indirect_block
 *
 * Returns: 0 on success -1 on error
 */
static int check_indirect_eattr(struct fsck_inode *ip, uint64_t indirect,
				struct metawalk_fxns *pass){
	int error = 0;
	uint64_t *ea_leaf_ptr, *end;
	uint64_t block;
	osi_buf_t *indirect_buf = NULL;
	struct fsck_sb *sdp = ip->i_sbd;

	log_debug("Checking EA indirect block #%"PRIu64".\n", indirect);

	if (!pass->check_eattr_indir ||
	    !pass->check_eattr_indir(ip, indirect, ip->i_di.di_num.no_addr,
				     &indirect_buf, pass->private)) {
		ea_leaf_ptr = (uint64 *)(BH_DATA(indirect_buf)
					 + sizeof(struct gfs_indirect));
		end = ea_leaf_ptr
			+ ((sdp->sb.sb_bsize
			    - sizeof(struct gfs_indirect)) / 8);

		while(*ea_leaf_ptr && (ea_leaf_ptr < end)){
			block = gfs64_to_cpu(*ea_leaf_ptr);
			/* FIXME: should I call check_leaf_eattr if we
			 * find a dup? */
			error = check_leaf_eattr(ip, block, indirect, pass);
			ea_leaf_ptr++;
		}
	}

	relse_buf(sdp, indirect_buf);
	return error;
}




/**
 * check_inode_eattr - check the EA's for a single inode
 * @ip: the inode whose EA to check
 *
 * Returns: 0 on success, -1 on error
 */
int check_inode_eattr(struct fsck_inode *ip, struct metawalk_fxns *pass)
{
	int error = 0;

	if(!ip->i_di.di_eattr){
		return 0;
	}

	log_debug("Extended attributes exist for inode #%"PRIu64".\n",
		ip->i_num.no_formal_ino);

	if(ip->i_di.di_flags & GFS_DIF_EA_INDIRECT){
		if((error = check_indirect_eattr(ip, ip->i_di.di_eattr, pass)))
			stack;
	} else {
		if((error = check_leaf_eattr(ip, ip->i_di.di_eattr,
					     ip->i_di.di_num.no_addr, pass)))
			stack;
	}

	return error;
}

/**
 * build_metalist
 * @ip:
 * @mlp:
 *
 */

static int build_metalist(struct fsck_inode *ip, osi_list_t *mlp,
			  struct metawalk_fxns *pass)
{
	uint32 height = ip->i_di.di_height;
	osi_buf_t *bh, *nbh;
	osi_list_t *prev_list, *cur_list, *tmp;
	int i, head_size;
	uint64 *ptr, block;
	int err;

	if(get_and_read_buf(ip->i_sbd, ip->i_di.di_num.no_addr, &bh, 0)) {
		stack;
		return -1;
	}

	osi_list_add(&bh->b_list, &mlp[0]);

	/* if(<there are no indirect blocks to check>) */
	if (height < 2) {
		return 0;
	}

	for (i = 1; i < height; i++){
		prev_list = &mlp[i - 1];
		cur_list = &mlp[i];

		for (tmp = prev_list->next; tmp != prev_list; tmp = tmp->next){
			bh = osi_list_entry(tmp, osi_buf_t, b_list);

			head_size = (i > 1 ?
				     sizeof(struct gfs_indirect) :
				     sizeof(struct gfs_dinode));

			for (ptr = (uint64 *)(bh->b_data + head_size);
			     (char *)ptr < (bh->b_data + bh->b_size);
			     ptr++) {
				if (!*ptr)
					continue;

				block = gfs64_to_cpu(*ptr);

				err = pass->check_metalist(ip, block, &nbh,
							   pass->private);
				if(err < 0) {
					stack;
					goto fail;
				}
				if(err > 0) {
					log_debug("Skipping block %"PRIu64
						  "\n", block);
					continue;
				}
				if(!nbh) {
					/* FIXME: error checking */
					get_and_read_buf(ip->i_sbd, block,
							 &nbh, 0);
				}
				osi_list_add(&nbh->b_list, cur_list);
			}
		}
	}
	return 0;

 fail:
	for (i = 0; i < GFS_MAX_META_HEIGHT; i++)
	{
		osi_list_t *list;
		list = &mlp[i];
		while (!osi_list_empty(list))
		{
			bh = osi_list_entry(list->next, osi_buf_t, b_list);
			osi_list_del(&bh->b_list);
			relse_buf(ip->i_sbd, bh);
		}
	}
	return -1;
}

/**
 * check_metatree
 * @ip:
 * @rgd:
 *
 */
int check_metatree(struct fsck_inode *ip, struct metawalk_fxns *pass)
{
	osi_list_t metalist[GFS_MAX_META_HEIGHT];
	osi_list_t *list, *tmp;
	osi_buf_t *bh;
	uint64_t block, *ptr;
	uint32_t height = ip->i_di.di_height;
	int  i, head_size;
	int update = 0;
	int error = 0;

	if (!height)
		goto end;


	for (i = 0; i < GFS_MAX_META_HEIGHT; i++)
		osi_list_init(&metalist[i]);

	/* create metalist for each level */
	if(build_metalist(ip, &metalist[0], pass)){
		stack;
		return -1;
	}

	/* We don't need to record directory blocks - they will be
	 * recorded later...i think... */
	if (ip->i_di.di_type == GFS_FILE_DIR) {
		log_debug("Directory with height > 0 at %"PRIu64"\n",
			  ip->i_di.di_num.no_addr);

	}

	/* check data blocks */
	list = &metalist[height - 1];

	for (tmp = list->next; tmp != list; tmp = tmp->next)
	{
		bh = osi_list_entry(tmp, osi_buf_t, b_list);

		head_size = (height != 1 ? sizeof(struct gfs_indirect) : sizeof(struct gfs_dinode));
		ptr = (uint64 *)(bh->b_data + head_size);

		for ( ; (char *)ptr < (bh->b_data + bh->b_size); ptr++)
		{
			if (!*ptr)
				continue;

			block =  gfs64_to_cpu(*ptr);

			if(pass->check_data &&
			   (pass->check_data(ip, block, pass->private) < 0)) {
				stack;
				return -1;
			}
		}
	}


	/* free metalists */
	for (i = 0; i < GFS_MAX_META_HEIGHT; i++)
	{
		list = &metalist[i];
		while (!osi_list_empty(list))
		{
			bh = osi_list_entry(list->next, osi_buf_t, b_list);
			osi_list_del(&bh->b_list);
			relse_buf(ip->i_sbd, bh);
		}
	}

end:
	if (ip->i_di.di_type == GFS_FILE_DIR) {
		/* check validity of leaf blocks and leaf chains */
		if (ip->i_di.di_flags & GFS_DIF_EXHASH) {
			error = check_leaf(ip, &update, pass);
			if(error < 0)
				return -1;
			if(error > 0)
				return 1;
		}
	}

	return 0;
}


/* Checks stuffed inode directories */
int check_linear_dir(struct fsck_inode *ip, osi_buf_t *bh, int *update,
		     struct metawalk_fxns *pass)
{
	int error = 0;
	uint16_t count = 0;

	error = check_entries(ip, bh, 0, DIR_LINEAR, update, &count, pass);
	if(error < 0) {
		stack;
		return -1;
	}

	return error;
}


int check_dir(struct fsck_sb *sbp, uint64_t block, struct metawalk_fxns *pass)
{
	osi_buf_t *bh;
	struct fsck_inode *ip;
	int update = 0;
	int error = 0;

	if(get_and_read_buf(sbp, block, &bh, 0)){
		log_err("Unable to retrieve block #%"PRIu64"\n",
			block);
		block_set(sbp->bl, block, meta_inval);
		return -1;
	}

	if(copyin_inode(sbp, bh, &ip)) {
		stack;
		relse_buf(sbp, bh);
		return -1;
	}

	if(ip->i_di.di_flags & GFS_DIF_EXHASH) {

		error = check_leaf(ip, &update, pass);
		if(error < 0) {
			stack;
			free_inode(&ip);
			relse_buf(sbp, bh);
			return -1;
		}
	}
	else {
		error = check_linear_dir(ip, bh, &update, pass);
		if(error < 0) {
			stack;
			free_inode(&ip);
			relse_buf(sbp, bh);
			return -1;
		}
	}

	free_inode(&ip);
	relse_buf(sbp, bh);

	return error;

}
