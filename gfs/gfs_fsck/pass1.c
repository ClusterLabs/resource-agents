/* pass1 checks inodes for format & type, duplicate blocks, & incorrect
 * block count.
 *
 * It builds up tables that contains the state of each block (free,
 * block in use, metadata type, etc), as well as bad blocks and
 * duplicate blocks.  (See block_list.[ch] for more info)
 *
 */

#include <stdio.h>

#include "fsck_incore.h"
#include "fsck.h"
#include "bio.h"
#include "fs_dir.h"
#include "fs_inode.h"
#include "util.h"
#include "block_list.h"
#include "log.h"
#include "inode_hash.h"
#include "inode.h"
#include "link.h"
#include "metawalk.h"

struct block_count {
	uint64_t indir_count;
	uint64_t data_count;
	uint64_t ea_count;
};

static int leaf(struct fsck_inode *ip, uint64_t block, osi_buf_t *bh,
		void *private)
{
	struct fsck_sb *sdp = ip->i_sbd;
	struct block_count *bc = (struct block_count *) private;

	log_debug("\tLeaf block at %15"PRIu64"\n", BH_BLKNO(bh));
	block_set(sdp->bl, BH_BLKNO(bh), leaf_blk);
	bc->indir_count++;

	return 0;
}

static int check_metalist(struct fsck_inode *ip, uint64_t block,
			  osi_buf_t **bh, void *private)
{
	struct fsck_sb *sdp = ip->i_sbd;
	struct block_query q = {0};
	int found_dup = 0;
	osi_buf_t *nbh;
	struct block_count *bc = (struct block_count *)private;

	*bh = NULL;

	if (check_range(ip->i_sbd, block)){ /* blk outside of FS */
		block_set(sdp->bl, ip->i_di.di_num.no_addr, bad_block);
		log_debug("Bad indirect block pointer (out of range).\n");

		return 1;
        }
	if(block_check(sdp->bl, block, &q)) {
		stack;
		return -1;
	}
	if(q.block_type != block_free) {
		log_debug("Found duplicate block in indirect block -"
			  " was marked %d\n", q.block_type);
		block_mark(sdp->bl, block, dup_block);
		found_dup = 1;
	}
        get_and_read_buf(ip->i_sbd, block, &nbh, 0);

        /** Attention -- experimental code **/
        if (check_meta(nbh, GFS_METATYPE_IN)){
		log_debug("Bad indirect block pointer "
			"(points to something that is not an indirect block).\n");
		if(!found_dup) {
			block_set(sdp->bl, block, meta_inval);
			relse_buf(ip->i_sbd, nbh);
			return 1;
		}

		relse_buf(ip->i_sbd, nbh);
        }else{  /* blk check ok */
		*bh = nbh;
        }
        /** Attention -- experimental code end **/

	block_set(sdp->bl, block, indir_blk);
	bc->indir_count++;

	return 0;
}



static int check_data(struct fsck_inode *ip, uint64_t block, void *private)
{
	struct fsck_sb *sdp = ip->i_sbd;
	struct block_query q = {0};
	osi_buf_t *data_bh;
	struct block_count *bc = (struct block_count *) private;

	if (check_range(ip->i_sbd, block)) {

		log_err( "Bad data block pointer (out of range)\n");
		/* Mark the owner of this block with the bad_block
		 * designator so we know to check it for out of range
		 * blocks later */
		block_set(ip->i_sbd->bl, ip->i_di.di_num.no_addr, bad_block);

		return 1;
	}

	if (ip->i_di.di_flags & GFS_DIF_JDATA){
		/* Journaled data *is* metadata */
		if(get_and_read_buf(ip->i_sbd, block, &data_bh, 0)) {
			stack;
			block_set(sdp->bl, ip->i_di.di_num.no_addr, meta_inval);
			return 1;
		}
		if(check_meta(data_bh, GFS_METATYPE_JD)) {
			log_err("Block #%"PRIu64" in inode %"PRIu64" does not have "
				"correct meta header. is %u should be %u\n",
				block, ip->i_di.di_num.no_addr,
				gfs32_to_cpu(((struct gfs_meta_header *)
					      BH_DATA((data_bh)))->mh_type),
				GFS_METATYPE_JD);
			relse_buf(sdp, data_bh);
			block_set(sdp->bl, ip->i_di.di_num.no_addr, meta_inval);
			return 1;
		}

		if(block_check(sdp->bl, block, &q)) {
			stack;
			relse_buf(sdp, data_bh);
			return -1;
		}
		if(q.block_type != block_free) {
			log_debug("Found duplicate block at %"
				  PRIu64"\n", block);
			block_mark(sdp->bl, block, dup_block);
			bc->data_count++;
			relse_buf(sdp, data_bh);
			return 1;
		}
		log_debug("Setting %"PRIu64 " to journal block\n", block);
		block_set(sdp->bl, block, journal_blk);
		bc->data_count++;
		relse_buf(sdp, data_bh);
	}
	else {
		if(block_check(sdp->bl, block, &q)) {
			stack;
			return -1;
		}
		if(q.block_type != block_free) {
			log_debug("Found duplicate block at %"
				  PRIu64"\n", block);
			block_mark(sdp->bl, block, dup_block);
			bc->data_count++;
			return 1;
		}
		log_debug("Setting %"PRIu64 " to data block\n", block);
		block_set(sdp->bl, block, block_used);
		bc->data_count++;
	}

	return 0;
}

static int check_eattr_indir(struct fsck_inode *ip, uint64_t indirect,
			     uint64_t parent, osi_buf_t **bh, void *private)
{
	struct fsck_sb *sdp = ip->i_sbd;
	int ret = 0;
	struct block_query q = {0};
	struct block_count *bc = (struct block_count *) private;

	/* This inode contains an eattr - it may be invalid, but the
	 * eattr attributes points to a non-zero block */
	block_set(sdp->bl, ip->i_num.no_addr, eattr_block);

	if(check_range(sdp, indirect)) {
		/*log_warn("EA indirect block #%"PRIu64" is out of range.\n",
			indirect);
			block_set(sdp->bl, parent, bad_block);*/
		/* Doesn't help to mark this here - this gets checked
		 * in pass1c */
		ret = 1;
	}
	else if(block_check(sdp->bl, indirect, &q)) {
		stack;
		ret = -1;
	}
	else if(q.block_type != block_free) {
		log_debug("Duplicate block found at #%"PRIu64".\n",
			  indirect);
		block_set(sdp->bl, indirect, dup_block);
		bc->ea_count++;
		ret = 1;
	}
	else if(get_and_read_buf(sdp, indirect, bh, 0)) {
		log_warn("Unable to read EA indirect block #%"PRIu64".\n",
			indirect);
		block_set(sdp->bl, indirect, meta_inval);
		ret = 1;
	}
	else if(check_meta(*bh, GFS_METATYPE_IN)) {
		log_warn("EA indirect block has incorrect type.\n");
		block_set(sdp->bl, BH_BLKNO(*bh), meta_inval);
		ret = 1;
	}
	else {
		/* FIXME: do i need to differentiate this as an ea_indir? */
		block_set(sdp->bl, BH_BLKNO(*bh), indir_blk);
		bc->ea_count++;
	}
	return ret;
}

/**
 * check_extended_leaf_eattr
 * @ip
 * @el_blk: block number of the extended leaf
 *
 * An EA leaf block can contain EA's with pointers to blocks
 * where the data for that EA is kept.  Those blocks still
 * have the gfs meta header of type GFS_METATYPE_EA
 *
 * Returns: 0 if correct[able], -1 if removal is needed
 */
static int check_extended_leaf_eattr(struct fsck_inode *ip, uint64_t *data_ptr,
				     osi_buf_t *leaf_bh,
				     struct gfs_ea_header *ea_hdr,
				     struct gfs_ea_header *ea_hdr_prev,
				     void *private)
{
	osi_buf_t *el_buf;
	struct fsck_sb *sdp = ip->i_sbd;
	struct block_query q;
	uint64_t el_blk = gfs64_to_cpu(*data_ptr);
	struct block_count *bc = (struct block_count *) private;

	if(check_range(sdp, el_blk)){
		log_err("EA extended leaf block #%"PRIu64" "
			"is out of range.\n",
			el_blk);
		block_set(sdp->bl, ip->i_di.di_eattr, bad_block);
		return 1;
	}

	if(block_check(sdp->bl, el_blk, &q)) {
		stack;
		return -1;
	}
	if(q.block_type != block_free) {
		block_set(sdp->bl, el_blk, dup_block);
		bc->ea_count++;
		return 1;
	}

	if(get_and_read_buf(sdp, el_blk, &el_buf, 0)){
		log_err("Unable to check extended leaf block.\n");
		block_set(sdp->bl, el_blk, meta_inval);
		return 1;
	}

	if(check_meta(el_buf, GFS_METATYPE_ED)) {
		log_err("EA extended leaf block has incorrect type.\n");
		relse_buf(sdp, el_buf);
		block_set(sdp->bl, el_blk, meta_inval);
		return 1;
	}

	block_set(sdp->bl, el_blk, meta_eattr);
	bc->ea_count++;
	relse_buf(sdp, el_buf);
	return 0;
}

static int check_eattr_leaf(struct fsck_inode *ip, uint64_t block,
			    uint64_t parent, osi_buf_t **bh, void *private)
{
	struct fsck_sb *sdp = ip->i_sbd;
	osi_buf_t *leaf_bh;
	int ret = 0;
	struct block_query q = {0};
	struct block_count *bc = (struct block_count *) private;

	/* This inode contains an eattr - it may be invalid, but the
	 * eattr attributes points to a non-zero block */
	block_set(sdp->bl, ip->i_num.no_addr, eattr_block);

	if(check_range(sdp, block)){
		log_warn("EA leaf block #%"PRIu64" in inode %"PRIu64
			 " is out of range.\n",
			 ip->i_num.no_addr, block);
		block_set(sdp->bl, ip->i_di.di_eattr, bad_block);
		ret = 1;
	}
	else if(block_check(sdp->bl, block, &q)) {
		stack;
		return -1;
	}
	else if(q.block_type != block_free) {
		log_debug("Duplicate block found at #%"PRIu64".\n",
			  block);
		block_set(sdp->bl, block, dup_block);
		bc->ea_count++;
	}
	else if(get_and_read_buf(sdp, block, &leaf_bh, 0)){
		log_warn("Unable to read EA leaf block #%"PRIu64".\n",
			 block);
		block_set(sdp->bl, block, meta_inval);
		ret = 1;
	} else if(check_meta(leaf_bh, GFS_METATYPE_EA)) {
		log_warn("EA leaf block has incorrect type.\n");
		block_set(sdp->bl, BH_BLKNO(leaf_bh), meta_inval);
		relse_buf(sdp, leaf_bh);
		ret = 1;
	}
	else {
		block_set(sdp->bl, BH_BLKNO(leaf_bh), meta_eattr);
		bc->ea_count++;
	}

	*bh = leaf_bh;

	return ret;
}

static int check_eattr_entries(struct fsck_inode *ip,
			       osi_buf_t *leaf_bh,
			       struct gfs_ea_header *ea_hdr,
			       struct gfs_ea_header *ea_hdr_prev,
			       void *private)
{
	struct fsck_sb *sdp = ip->i_sbd;
	char ea_name[256];

	if(!ea_hdr->ea_name_len){
		/* Skip this entry for now */
		return 1;
	}

	memset(ea_name, 0, sizeof(ea_name));
	strncpy(ea_name, (char *)ea_hdr + sizeof(struct gfs_ea_header),
		ea_hdr->ea_name_len);

	if(!GFS_EATYPE_VALID(ea_hdr->ea_type) &&
	   ((ea_hdr_prev) || (!ea_hdr_prev && ea_hdr->ea_type))){
		/* Skip invalid entry */
		return 1;
	}

	if(ea_hdr->ea_num_ptrs){
		uint32 avail_size;
		int max_ptrs;

		avail_size = sdp->sb.sb_bsize - sizeof(struct gfs_meta_header);
		max_ptrs = (gfs32_to_cpu(ea_hdr->ea_data_len)+avail_size-1)/avail_size;

		if(max_ptrs > ea_hdr->ea_num_ptrs) {
			return 1;
		} else {
			log_debug("  Pointers Required: %d\n"
				  "  Pointers Reported: %d\n",
				  max_ptrs,
				  ea_hdr->ea_num_ptrs);
		}

	}

	return 0;
}

struct metawalk_fxns pass1_fxns = {
	.private = NULL,
	.check_leaf = leaf,
	.check_metalist = check_metalist,
	.check_data = check_data,
	.check_eattr_indir = check_eattr_indir,
	.check_eattr_leaf = check_eattr_leaf,
	.check_dentry = NULL,
	.check_eattr_entry = check_eattr_entries,
	.check_eattr_extentry = check_extended_leaf_eattr,
};

int clear_metalist(struct fsck_inode *ip, uint64_t block,
		   osi_buf_t **bh, void *private)
{
	struct fsck_sb *sdp = ip->i_sbd;
	struct block_query q = {0};

	*bh = NULL;

	if(block_check(sdp->bl, block, &q)) {
		stack;
		return -1;
	}
	if(!q.dup_block) {
		block_set(sdp->bl, block, block_free);
		return 0;
	}
	return 0;
}

int clear_data(struct fsck_inode *ip, uint64_t block, void *private)
{
	struct fsck_sb *sdp = ip->i_sbd;
	struct block_query q = {0};

	if(block_check(sdp->bl, block, &q)) {
		stack;
		return -1;
	}
	if(!q.dup_block) {
		block_set(sdp->bl, block, block_free);
		return 0;
	}
	return 0;

}

int clear_leaf(struct fsck_inode *ip, uint64_t block,
	       osi_buf_t *bh, void *private)
{

	struct fsck_sb *sdp = ip->i_sbd;
	struct block_query q = {0};
	log_crit("Clearing leaf %"PRIu64"\n", block);

	if(block_check(sdp->bl, block, &q)) {
		stack;
		return -1;
	}
	if(!q.dup_block) {
		log_crit("Setting leaf invalid\n");
		if(block_set(sdp->bl, block, block_free)) {
			stack;
			return -1;
		}
		return 0;
	}
	return 0;

}

int add_to_dir_list(struct fsck_sb *sbp, uint64_t block)
{
	struct dir_info *di = NULL;
	struct dir_info *newdi;

	/* FIXME: This list should probably be a b-tree or
	 * something...but since most of the time we're going to be
	 * tacking the directory onto the end of the list, it doesn't
	 * matter too much */
	find_di(sbp, block, &di);
	if(di) {
		log_err("Attempting to add directory block %"PRIu64
			" which is already in list\n", block);
		return -1;
	}

	if(!(newdi = (struct dir_info *) malloc(sizeof(*newdi)))) {
		log_crit("Unable to allocate dir_info structure\n");
		return -1;
	}
	if(!memset(newdi, 0, sizeof(*newdi))) {
		log_crit("error while zeroing dir_info structure\n");
		return -1;
	}

	newdi->dinode = block;

	dinode_hash_insert(sbp->dir_hash, block, newdi);

	return 0;
}


int handle_di(struct fsck_sb *sdp, osi_buf_t *bh, uint64_t block, int mfree)
{
	struct block_query q = {0};
	struct fsck_inode *ip;
	int error;
	struct block_count bc = {0};
	struct metawalk_fxns invalidate_metatree = {0};
	invalidate_metatree.check_metalist = clear_metalist;
	invalidate_metatree.check_data = clear_data;
	invalidate_metatree.check_leaf = clear_leaf;

	if(copyin_inode(sdp, bh, &ip)) {
		stack;
		return -1;
	}

	if (ip->i_di.di_flags & GFS_DIF_UNUSED){
		if(mfree) {
			if(block_set(sdp->bl, block, meta_free)) {
				stack;
				goto fail;
			}
			goto success;
		} else {
			log_err("Found unused inode marked in-use\n");
			if(query(sdp, "Clear unused inode at block %"
				 PRIu64"? (y/n) ", block)) {
				if(block_set(sdp->bl, block, meta_inval)) {
					stack;
					goto fail;
				}
				goto success;
			} else {
				log_err("Unused inode still marked in-use\n");
			}
		}

	} else {
		if(mfree) {
			if(block_set(sdp->bl, block, meta_free)) {
				stack;
				goto fail;
			}
			goto success;
		}
	}

	if (ip->i_di.di_num.no_addr != block) {
		log_err("Bad dinode Address.  "
			"Found %"PRIu64", "
			"Expected %"PRIu64"\n",
			ip->i_di.di_num.no_addr, block);
		if(query(sdp, "Fix address in inode at block %"
			 PRIu64"? (y/n) ", block)) {
			ip->i_di.di_num.no_addr =
				ip->i_di.di_num.no_formal_ino =
				block;
			if(fs_copyout_dinode(ip)){
				log_crit("Bad dinode address can not be reset.\n");
				goto fail;
			} else {
				log_err("Bad dinode address reset.\n");
			}
		} else {
			log_err("Address in inode at block %"PRIu64
				 " not fixed\n", block);
		}

	}

	if(block_check(sdp->bl, block, &q)) {
		stack;
		goto fail;
	}
	if(q.block_type != block_free) {
		log_debug("Found duplicate block at %"PRIu64"\n",
			  block);
		if(block_mark(sdp->bl, block, dup_block)) {
			stack;
			goto fail;
		}
		goto success;
	}

	switch(ip->i_di.di_type) {

	case GFS_FILE_DIR:
		if(block_set(sdp->bl, block, inode_dir)) {
			stack;
			goto fail;
		}
		if(add_to_dir_list(sdp, block)) {
			stack;
			goto fail;
		}
		break;
	case GFS_FILE_REG:
		if(block_set(sdp->bl, block, inode_file)) {
			stack;
			goto fail;
		}
		break;
	case GFS_FILE_LNK:
		if(block_set(sdp->bl, block, inode_lnk)) {
			stack;
			goto fail;
		}
		break;
	case GFS_FILE_BLK:
		if(block_set(sdp->bl, block, inode_blk)) {
			stack;
			goto fail;
		}
		break;
	case GFS_FILE_CHR:
		if(block_set(sdp->bl, block, inode_chr)) {
			stack;
			goto fail;
		}
		break;
	case GFS_FILE_FIFO:
		if(block_set(sdp->bl, block, inode_fifo)) {
			stack;
			goto fail;
		}
		break;
	case GFS_FILE_SOCK:
		if(block_set(sdp->bl, block, inode_sock)) {
			stack;
			goto fail;
		}
		break;
	default:
		if(block_set(sdp->bl, block, meta_inval)) {
			stack;
			goto fail;
		}
		goto success;
	}
	if(set_link_count(ip->i_sbd, ip->i_num.no_formal_ino,
			  ip->i_di.di_nlink)) {
		stack;
		goto fail;
	}

	/* FIXME: fix height and depth here - wasn't implemented in
	 * old fsck either, so no biggy... */
	if (ip->i_di.di_height < compute_height(sdp, ip->i_di.di_size)){
		log_warn("Dinode #%"PRIu64" has bad height  "
			 "Found %u, Expected >= %u\n",
			 ip->i_di.di_num.no_addr, ip->i_di.di_height,
			 compute_height(sdp, ip->i_di.di_size));
			/* once implemented, remove continue statement */
		log_warn("Marking inode invalid\n");
		if(block_set(sdp->bl, block, meta_inval)) {
			stack;
			goto fail;
		}
		goto success;
	}

	if (ip->i_di.di_type == (GFS_FILE_DIR &&
				 (ip->i_di.di_flags & GFS_DIF_EXHASH)))
	{
		if (((1 << ip->i_di.di_depth) * sizeof(uint64_t)) !=
		    ip->i_di.di_size)
		{
			log_warn("Directory dinode #%"PRIu64" has bad depth.  "
				 "Found %u, Expected %u\n",
				 ip->i_di.di_num.no_addr, ip->i_di.di_depth,
				 (1 >> (ip->i_di.di_size/sizeof(uint64))));
			/* once implemented, remove continue statement */
			log_warn("Marking inode invalid\n");
			if(block_set(sdp->bl, block, meta_inval)) {
				stack;
				goto fail;
			}
			goto success;
		}
	}

	pass1_fxns.private = &bc;

	error = check_metatree(ip, &pass1_fxns);
	if(error < 0) {
		return 0;
	}
	if(error > 0) {
		log_warn("Marking inode invalid\n");
		/* FIXME: Must set all leaves invalid as well */
		check_metatree(ip, &invalidate_metatree);
		block_set(ip->i_sbd->bl, ip->i_di.di_num.no_addr, meta_inval);
		return 0;
	}

	/* FIXME: is this correct? */
	if(check_inode_eattr(ip, &pass1_fxns) < 0){
		osi_buf_t	*di_bh;
		ip->i_di.di_eattr = 0;
		if(get_and_read_buf(sdp, ip->i_di.di_num.no_addr, &di_bh, 0)){
			stack;
			log_crit("Bad EA reference remains.\n");
		} else {
			gfs_dinode_out(&ip->i_di, BH_DATA(di_bh));
			if(write_buf(ip->i_sbd, di_bh, 0) < 0){
				stack;
				log_crit("Bad EA reference remains.\n");
			} else {
				log_warn("Bad EA reference cleared.\n");
			}
			relse_buf(sdp, di_bh);
		}
	}

	if(ip->i_di.di_blocks != (1 + bc.indir_count + bc.data_count + bc.ea_count)) {
		osi_buf_t	*di_bh;
		log_err("Ondisk block count does not match what fsck"
			" found for inode %"PRIu64"\n", ip->i_di.di_num.no_addr);
		if(query(sdp, "Fix ondisk block count? (y/n) ")) {
			ip->i_di.di_blocks = 1 + bc.indir_count +
				bc.data_count +
				bc.ea_count;
			if(get_and_read_buf(sdp, ip->i_di.di_num.no_addr,
					    &di_bh, 0)){
				stack;
				log_crit("Bad block count remains\n");
			} else {
				gfs_dinode_out(&ip->i_di, BH_DATA(di_bh));
				if(write_buf(ip->i_sbd, di_bh, 0) < 0){
					stack;
					log_crit("Bad block count remains\n");
				} else {
					log_warn("Bad block count fixed\n");
				}
				relse_buf(sdp, di_bh);
			}
		} else {
			log_err("Bad block count for %"PRIu64" not fixed\n",
				ip->i_di.di_num.no_addr);
		}
	}

 success:
	free(ip);
	return 0;

 fail:
	free(ip);
	return -1;

}


int scan_meta(struct fsck_sb *sdp, osi_buf_t *bh, uint64_t block, int mfree)
{

	if (check_meta(bh, 0)) {
		log_debug("Found invalid metadata at %"PRIu64"\n", block);
		if(block_set(sdp->bl, block, meta_inval)) {
			stack;
			return -1;
		}
		return 0;
	}

	log_debug("Checking metadata block %"PRIu64"\n", block);

	if (!check_type(bh, GFS_METATYPE_DI)) {
		if(handle_di(sdp, bh, block, mfree)) {
			stack;
			return -1;
		}
	}
	else if (!check_type(bh, GFS_METATYPE_NONE)) {
		if(block_set(sdp->bl, block, meta_free)) {
			stack;
			return -1;
		}
	} else {
		log_debug("Metadata block %"PRIu64
			  " not an inode or free metadata\n",
			  block);
	}
	/* Ignore everything else - they should be hit by the
	 * handle_di step */

	return 0;
}

/**
 * pass1 - walk through inodes and check inode state
 *
 * this walk can be done using root inode and depth first search,
 * watching for repeat inode numbers
 *
 * format & type
 * link count
 * duplicate blocks
 * bad blocks
 * inodes size
 * dir info
 */
int pass1(struct fsck_sb *sbp)
{
	osi_buf_t *bh;
	osi_list_t *tmp;
	uint64_t block;
	struct fsck_rgrp *rgd;
	int first;
	uint64_t i;
	uint64_t j;
	uint64_t blk_count;
	uint64_t offset;
	uint64_t rg_count = 0;
	int mfree = 0;

	/* FIXME: What other metadata should we look for? */

	/* Mark the journal blocks as 'other metadata' */
	for (i = 0; i < sbp->journals; i++) {
		struct gfs_jindex *ji;
		ji = &sbp->jindex[i];
		for(j = ji->ji_addr;
		    j < ji->ji_addr + (ji->ji_nsegment * sbp->sb.sb_seg_size);
		    j++) {
			if(block_set(sbp->bl, j, journal_blk)) {
				stack;
				return -1;
			}
		}
	}


	/* So, do we do a depth first search starting at the root
	 * inode, or use the rg bitmaps, or just read every fs block
	 * to find the inodes?  If we use the depth first search, why
	 * have pass3 at all - if we use the rg bitmaps, pass5 is at
	 * least partially invalidated - if we read every fs block,
	 * things will probably be intolerably slow.  The current fsck
	 * uses the rg bitmaps, so maybe that's the best way to start
	 * things - we can change the method later if necessary.
	 */

	for (tmp = sbp->rglist.next; tmp != &sbp->rglist;
	     tmp = tmp->next, rg_count++){
		log_info("Checking metadata in Resource Group %"PRIu64"\n",
			 rg_count);
		rgd = osi_list_entry(tmp, struct fsck_rgrp, rd_list);
		if(fs_rgrp_read(rgd, FALSE)){
			stack;
			return -1;
		}
		log_debug("RG at %"PRIu64" is %u long\n", rgd->rd_ri.ri_addr,
				  rgd->rd_ri.ri_length);
		for (i = 0; i < rgd->rd_ri.ri_length; i++) {
			if(block_set(sbp->bl, rgd->rd_ri.ri_addr + i,
				     meta_other)){
				stack;
				return -1;
			}
		}

		offset = sizeof(struct gfs_rgrp);
		blk_count = 1;

		first = 1;

		while (1) {

			/* "block" is relative to the entire file system */
			if(next_rg_meta_free(rgd, &block, first, &mfree))
				break;

			warm_fuzzy_stuff(block);
			if (fsck_abort) /* if asked to abort */
				return 0;
			if (skip_this_pass) {
				printf("Skipping pass 1 is not a good idea.\n");
				skip_this_pass = FALSE;
				fflush(stdout);
			}
			if(get_and_read_buf(sbp, block, &bh, 0)){
				stack;
				log_crit("Unable to retrieve block %"PRIu64
					 "\n", block);
				fs_rgrp_relse(rgd);
				return -1;
			}

			if(scan_meta(sbp, bh, block, mfree)) {
				stack;
				relse_buf(sbp, bh);
				fs_rgrp_relse(rgd);
				return -1;
			}
			relse_buf(sbp, bh);
			first = 0;
		}
		fs_rgrp_relse(rgd);
	}

	return 0;
}
