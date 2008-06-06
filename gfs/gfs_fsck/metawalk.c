#include "fsck_incore.h"
#include "fsck.h"
#include "bio.h"
#include "fs_dir.h"
#include "inode.h"
#include "util.h"
#include "hash.h"

#include "metawalk.h"

int check_entries(struct fsck_inode *ip, osi_buf_t *bh, int index,
		  int type, int *update, uint16_t *count,
		  struct metawalk_fxns *pass)
{
	struct gfs_leaf *leaf = NULL;
	struct gfs_dirent *dent;
	struct gfs_dirent de, *prev;
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

		if (de.de_rec_len < sizeof(struct gfs_dirent) +
		    de.de_name_len || !de.de_name_len) {
			log_err("Directory block %"
				PRIu64 ", entry %d of directory %"
				PRIu64 " is corrupt.\n", BH_BLKNO(bh),
				(*count) + 1, ip->i_di.di_num.no_addr);
			if (query(ip->i_sbd, "Attempt to repair it? (y/n) ")) {
				if (dirent_repair(ip, bh, &de, dent, type,
						  first))
					break;
			}
			else {
				log_err("Corrupt directory entry %d ignored, "
					"stopped after checking %d entries.\n",
					*count);
				break;
			}
		}
		if (!de.de_inum.no_formal_ino){
			if(first){
				log_debug("First dirent is a sentinel (place holder).\n");
				first = 0;
			} else {
				/* FIXME: Do something about this */
				log_err("Directory entry with inode number of zero in leaf %"PRIu64" of directory %"PRIu64"!\n", BH_BLKNO(bh), ip->i_di.di_num.no_addr);
				return 1;
			}
		} else {

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

		/* If we didn't clear the dentry, or if we did, but it
		 * was the first dentry, set prev  */
		if(!error || first) {
			prev = dent;
		}

		first = 0;


		dent = (struct gfs_dirent *)((char *)dent + de.de_rec_len);
	}
	return 0;
}


/* Process a bad leaf pointer and ask to repair the first time.      */
/* The repair process involves extending the previous leaf's entries */
/* so that they replace the bad ones.  We have to hack up the old    */
/* leaf a bit, but it's better than deleting the whole directory,    */
/* which is what used to happen before.                              */
void warn_and_patch(struct fsck_inode *ip, uint64_t *leaf_no, 
		    uint64_t *bad_leaf, uint64_t old_leaf, int index,
		    const char *msg)
{
	if (*bad_leaf != *leaf_no) {
		log_err("Directory Inode %" PRIu64 " points to leaf %"
			PRIu64 " %s.\n", ip->i_di.di_num.no_addr, *leaf_no,
			msg);
	}
	if (*leaf_no == *bad_leaf ||
	    query(ip->i_sbd, "Attempt to patch around it? (y/n) ")) {
		put_leaf_nr(ip, index, old_leaf);
	}
	else
		log_err("Bad leaf left in place.\n");
	*bad_leaf = *leaf_no;
	*leaf_no = old_leaf;
}

/* Checks exthash directory entries */
int check_leaf(struct fsck_inode *ip, int *update, struct metawalk_fxns *pass)
{
	int error;
	struct gfs_leaf leaf, oldleaf;
	uint64_t leaf_no, old_leaf, bad_leaf = -1;
	osi_buf_t *lbh;
	int index;
	struct fsck_sb *sbp = ip->i_sbd;
	uint16_t count;
	int ref_count = 0, exp_count = 0;

	old_leaf = 0;
	memset(&oldleaf, 0, sizeof(oldleaf));
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
		if (leaf_no == bad_leaf) {
			put_leaf_nr(ip, index, old_leaf); /* fill w/old leaf */
			ref_count++;
			continue;
		}
		else if(old_leaf == leaf_no) {
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
				if (query(ip->i_sbd, "Attempt to fix it? (y/n) ")) {
					int factor = 0, divisor = ref_count;

					get_and_read_buf(sbp, old_leaf, &lbh,
							 0);
					while (divisor > 1) {
						factor++;
						divisor /= 2;
					}
					oldleaf.lf_depth = ip->i_di.di_depth -
						factor;
					gfs_leaf_out(&oldleaf, BH_DATA(lbh));
					write_buf(sbp, lbh, 0);
					relse_buf(sbp, lbh);
				}
				else
					return 1;
			}
			ref_count = 1;
		}

		count = 0;
		do {
			/* Make sure the block number is in range. */
			if(check_range(ip->i_sbd, leaf_no)){
				log_err("Leaf block #%"PRIu64" is out of "
					"range for directory #%"PRIu64".\n",
					leaf_no, ip->i_di.di_num.no_addr);
				warn_and_patch(ip, &leaf_no, &bad_leaf,
					       old_leaf, index,
					       "that is out of range");
				memcpy(&leaf, &oldleaf, sizeof(oldleaf));
				break;
			}
			/* Try to read in the leaf block. */
			if(get_and_read_buf(sbp, leaf_no, &lbh, 0)){
				log_err("Unable to read leaf block #%"
					PRIu64" for "
					"directory #%"PRIu64".\n",
					leaf_no, ip->i_di.di_num.no_addr);
				warn_and_patch(ip, &leaf_no, &bad_leaf,
					       old_leaf, index,
					       "that cannot be read");
				memcpy(&leaf, &oldleaf, sizeof(oldleaf));
				relse_buf(sbp, lbh);
				break;
			}
			/* Make sure it's really a valid leaf block. */
			if (check_meta(lbh, GFS_METATYPE_LF)) {
				warn_and_patch(ip, &leaf_no, &bad_leaf,
					       old_leaf, index,
					       "that is not really a leaf");
				memcpy(&leaf, &oldleaf, sizeof(oldleaf));
				relse_buf(sbp, lbh);
				break;
			}
			gfs_leaf_in(&leaf, BH_DATA(lbh));
			if(pass->check_leaf) {
				error = pass->check_leaf(ip, leaf_no, lbh,
							 pass->private);
			}

			exp_count = (1 << (ip->i_di.di_depth - leaf.lf_depth));
			log_debug("expected count %u - %u %u\n", exp_count,
				  ip->i_di.di_depth, leaf.lf_depth);
			if(pass->check_dentry && 
			   ip->i_di.di_type == GFS_FILE_DIR) {
				error = check_entries(ip, lbh, index,
						      DIR_EXHASH, update,
						      &count,
						      pass);

				/* Since the buffer possibly got
				   updated directly, release it now,
				   and grab it again later if we need it */
				relse_buf(sbp, lbh);
				if(error < 0) {
					stack;
					return -1;
				}

				if(error > 0) {
					return 1;
				}

				if(update && (count != leaf.lf_entries)) {

					if(get_and_read_buf(sbp, leaf_no,
							    &lbh, 0)){
						log_err("Unable to read leaf block #%"
							PRIu64" for "
							"directory #%"PRIu64".\n",
							leaf_no,
							ip->i_di.di_num.no_addr);
						return -1;
					}
					gfs_leaf_in(&leaf, BH_DATA(lbh));

					log_err("Leaf(%"PRIu64") entry count in directory %"PRIu64" doesn't match number of entries found - is %u, found %u\n", leaf_no, ip->i_num.no_addr, leaf.lf_entries, count);
					if(query(sbp, "Update leaf entry count? (y/n) ")) {
						leaf.lf_entries = count;
						gfs_leaf_out(&leaf, BH_DATA(lbh));
						write_buf(sbp, lbh, 0);
						log_warn("Leaf entry count updated\n");
					} else {
						log_err("Leaf entry count left in inconsistant state\n");
					}
					relse_buf(sbp, lbh);
				}
				/* FIXME: Need to get entry count and
				 * compare it against
				 * leaf->lf_entries */

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
		memcpy(&oldleaf, &leaf, sizeof(oldleaf));
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
	uint32_t offset = (uint32_t)sizeof(struct gfs_meta_header);

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
		offset += gfs32_to_cpu(ea_hdr->ea_rec_len);
		if(ea_hdr->ea_flags & GFS_EAFLAG_LAST ||
		   offset >= ip->i_sbd->sb.sb_bsize || ea_hdr->ea_rec_len == 0){
			break;
		}
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
 * @block: block number of the leaf
 *
 * Returns: 0 on success, -1 if removal is needed
 */
static int check_leaf_eattr(struct fsck_inode *ip, uint64_t block,
			    uint64_t parent, struct metawalk_fxns *pass)
{
	osi_buf_t *bh = NULL;
	int error = 0;
	log_debug("Checking EA leaf block #%"PRIu64".\n", block);

	if(pass->check_eattr_leaf) {
		error = pass->check_eattr_leaf(ip, block, parent,
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
				nbh = NULL;

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
					if(get_and_read_buf(ip->i_sbd, block,
							    &nbh, 0)) {
						stack;
						goto fail;
					}
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


static int remove_dentry(struct fsck_inode *ip, struct gfs_dirent *dent,
		  struct gfs_dirent *prev_de,
		  osi_buf_t *bh, char *filename, int *update,
		  uint16_t *count,
		  void *private)
{
	/* the metawalk_fxn's private field must be set to the dentry
	 * block we want to clear */
	uint64_t *dentryblock = (uint64_t *) private;
	struct gfs_dirent dentry, *de;

	memset(&dentry, 0, sizeof(struct gfs_dirent));
	gfs_dirent_in(&dentry, (char *)dent);
	de = &dentry;

	if(de->de_inum.no_addr == *dentryblock) {
		*update = 1;
		if(dirent_del(ip, bh, prev_de, dent)) {
			stack;
			return -1;
		}
	}
	else {
		(*count)++;
		*update = 1;
	}

	return 0;

}

int remove_dentry_from_dir(struct fsck_sb *sbp, uint64_t dir,
			   uint64_t dentryblock)
{
	struct metawalk_fxns remove_dentry_fxns = {0};
	struct block_query q;
	int error;

	log_debug("Removing dentry %"PRIu64" from directory %"PRIu64"\n",
		  dentryblock, dir);
	if(check_range(sbp, dir)) {
		log_err("Parent directory out of range\n");
		return 1;
	}
	remove_dentry_fxns.private = &dentryblock;
	remove_dentry_fxns.check_dentry = remove_dentry;

	if(block_check(sbp->bl, dir, &q)) {
		stack;
		return -1;
	}
	if(q.block_type != inode_dir) {
		log_info("Parent block is not a directory...ignoring\n");
		return 1;
	}
	/* Need to run check_dir with a private var of dentryblock,
	 * and fxns that remove that dentry if found */
	error = check_dir(sbp, dir, &remove_dentry_fxns);

	return error;
}

/* FIXME: These should be merged with the hash routines in inode_hash.c */
static uint32_t dinode_hash(uint64_t block_no)
{
	unsigned int h;

	h = fsck_hash(&block_no, sizeof (uint64_t));
	h &= FSCK_HASH_MASK;

	return h;
}

int find_di(struct fsck_sb *sbp, uint64_t childblock, struct dir_info **dip)
{
	osi_list_t *bucket = &sbp->dir_hash[dinode_hash(childblock)];
	osi_list_t *tmp;
	struct dir_info *di = NULL;

	osi_list_foreach(tmp, bucket) {
		di = osi_list_entry(tmp, struct dir_info, list);
		if(di->dinode == childblock) {
			*dip = di;
			return 0;
		}
	}
	*dip = NULL;
	return -1;

}

int dinode_hash_insert(osi_list_t *buckets, uint64_t key, struct dir_info *di)
{
	osi_list_t *tmp;
	osi_list_t *bucket = &buckets[dinode_hash(key)];
	struct dir_info *dtmp = NULL;

	if(osi_list_empty(bucket)) {
		osi_list_add(&di->list, bucket);
		return 0;
	}

	osi_list_foreach(tmp, bucket) {
		dtmp = osi_list_entry(tmp, struct dir_info, list);
		if(dtmp->dinode < key) {
			continue;
		}
		else {
			osi_list_add_prev(&di->list, tmp);
			return 0;
		}
	}
	osi_list_add_prev(&di->list, bucket);
	return 0;
}


int dinode_hash_remove(osi_list_t *buckets, uint64_t key)
{
	osi_list_t *tmp;
	osi_list_t *bucket = &buckets[dinode_hash(key)];
	struct dir_info *dtmp = NULL;

	if(osi_list_empty(bucket)) {
		return -1;
	}
	osi_list_foreach(tmp, bucket) {
		dtmp = osi_list_entry(tmp, struct dir_info, list);
		if(dtmp->dinode == key) {
			osi_list_del(tmp);
			return 0;
		}
	}
	return -1;
}
