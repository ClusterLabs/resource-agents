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

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <linux_endian.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "util.h"
#include "file.h"
#include "rgrp.h"
#include "fsck_incore.h"
#include "fs_inode.h"
#include "bio.h"
#include "link.h"

#include "fs_dir.h"

#define IS_LEAF     (1)
#define IS_DINODE   (2)

#define dir_hash(qstr) (gfs2_disk_hash((qstr)->name, (qstr)->len))

/* Detect directory is a stuffed inode */
int fsck_inode_is_stuffed(struct fsck_inode *ip)
{
	return !ip->i_di.di_height;
}

/**
 * dirent_first - Return the first dirent
 * @bh: The buffer
 * @dent: Pointer to list of dirents
 *
 * return first dirent whether bh points to leaf or stuffed dinode
 *
 * Returns: IS_LEAF or IS_DINODE
 */
int dirent_first(struct buffer_head *bh, struct gfs2_dirent **dent)
{
	struct gfs2_leaf *leaf;
	struct gfs2_dinode *dinode;

	leaf = (struct gfs2_leaf *)BH_DATA(bh);

	if (gfs2_32_to_cpu(leaf->lf_header.mh_type) == GFS2_METATYPE_LF)
	{
		*dent = (struct gfs2_dirent *)(BH_DATA(bh) + sizeof(struct gfs2_leaf));

		return IS_LEAF;
	}
	else
	{
		dinode = (struct gfs2_dinode *)BH_DATA(bh);
		if(gfs2_32_to_cpu(dinode->di_header.mh_type) != GFS2_METATYPE_DI){
			log_err("buffer is not GFS2_METATYPE_[DI | LF]\n");
			return -1;
		}

		*dent = (struct gfs2_dirent *)(BH_DATA(bh) + sizeof(struct gfs2_dinode));

		return IS_DINODE;
	}
}


/**
 * dirent_next - Next dirent
 * @bh: The buffer
 * @dent: Pointer to list of dirents
 *
 * Returns: 0 on success, error code otherwise
 */
int dirent_next(struct buffer_head *bh, struct gfs2_dirent **dent)
{
	struct gfs2_dirent *tmp, *cur;
	char *bh_end;
	uint32_t cur_rec_len;

	cur = *dent;
	bh_end = BH_DATA(bh) + BH_SIZE(bh);

	cur_rec_len = gfs2_16_to_cpu(cur->de_rec_len);

	if ((char *)cur + cur_rec_len >= bh_end){
		if((char *)cur + cur_rec_len != bh_end){
			log_err("Bad record length causing failure in dirent_next()\n");
			return -1;
		}
		return -ENOENT;
	}

	tmp = (struct gfs2_dirent *)((char *)cur + cur_rec_len);

	if((char *)tmp + gfs2_16_to_cpu(tmp->de_rec_len) > bh_end){
		log_err("Bad record length causing failure in dirent_next\n");
		return -1;
	}

	/*  only the first dent could ever have de_ino == 0  */
	if(!tmp->de_inum.no_formal_ino){
		char tmp_name[256];

		memcpy(tmp_name, cur+sizeof(struct gfs2_dirent), gfs2_16_to_cpu(cur->de_name_len));
		tmp_name[gfs2_16_to_cpu(cur->de_name_len)] = '\0';
		log_err("dirent_next:  "
			"A non-first dir entry has zero formal inode.\n");
		log_err("\tFaulty dirent after (%s) in block #%"PRIu64".\n",
			tmp_name, BH_BLKNO(bh));

		return -1;
	}

	*dent = tmp;

	return 0;
}


/**
 * dirent_del - Delete a dirent
 * @dip: The GFS2 inode
 * @bh: The buffer
 * @prev: The previous dirent
 * @cur: The current dirent
 *
 * Returns: 0 on success, error code otherwise
 */

int dirent_del(struct fsck_inode *dip, struct buffer_head *bh,
	       struct gfs2_dirent *prev, struct gfs2_dirent *cur){
	uint32_t cur_rec_len, prev_rec_len;

	dip->i_di.di_entries--;
	if(!cur->de_inum.no_formal_ino){
		log_err("dirent_del:  "
			"Can not delete dirent with !no_formal_ino.\n");
		return -1;
	}

	/*  If there is no prev entry, this is the first entry in the block.
	    The de_rec_len is already as big as it needs to be.  Just zero
	    out the inode number and return.  */

	if (!prev){
		cur->de_inum.no_formal_ino = 0;  /*  No endianess worries  */
		if(write_buf(dip->i_sbd, bh, 0)){
			log_err("dirent_del: Bad write_buf.\n");
			return -EIO;
		}
		return 0;
	}

	/*  Combine this dentry with the previous one.  */

	prev_rec_len = gfs2_16_to_cpu(prev->de_rec_len);
	cur_rec_len = gfs2_16_to_cpu(cur->de_rec_len);

	if((char *)prev + prev_rec_len != (char *)cur){
		log_err("dirent_del: Bad bounds for directory entries.\n");
		return -1;
	}

	if((char *)cur + cur_rec_len > BH_DATA(bh) + BH_SIZE(bh)){
		log_err("dirent_del: Directory entry has record length"
			" longer than buffer.\n");
		return -1;
	}

	log_debug("Updating previous record from %u to %u\n",
		  prev_rec_len, prev_rec_len+cur_rec_len);
	prev_rec_len += cur_rec_len;
	prev->de_rec_len = cpu_to_gfs2_16(prev_rec_len);

	if(write_buf(dip->i_sbd, bh, 0)){
		log_err("dirent_del: Bad write_buf.\n");
		return -EIO;
	}

	return 0;
}


/**
 * get_leaf - Get leaf
 * @dip:
 * @leaf_no:
 * @bh_out:
 *
 * Returns: 0 on success, error code otherwise
 */

static int get_leaf(struct fsck_inode *dip, uint64_t leaf_no, struct buffer_head **bhp)
{
	int error;

	error = get_and_read_buf(dip->i_sbd, leaf_no, bhp, 0);

	if (error) {
		log_err("Unable to read leaf buffer #%"PRIu64"\n", leaf_no);
		return error;
	}

	error = check_meta(*bhp, GFS2_METATYPE_LF);

	if(error) {
		log_err("Metatype for block #%"PRIu64" is not type 'leaf'\n",
			leaf_no);
		relse_buf(dip->i_sbd, *bhp);
	}
	return error;
}


/**
 * get_first_leaf - Get first leaf
 * @dip: The GFS2 inode
 * @index:
 * @bh_out:
 *
 * Returns: 0 on success, error code otherwise
 */

static int get_first_leaf(struct fsck_inode *dip, uint32_t index, struct buffer_head **bh_out)
{
	uint64_t leaf_no;
	int error;

	error = get_leaf_nr(dip, index, &leaf_no);
	if (!error)
		error = get_leaf(dip, leaf_no, bh_out);

	return error;
}


/**
 * get_next_leaf - Get next leaf
 * @dip: The GFS2 inode
 * @bh_in: The buffer
 * @bh_out:
 *
 * Returns: 0 on success, error code otherwise
 */

static int get_next_leaf(struct fsck_inode *dip,struct buffer_head *bh_in,struct buffer_head **bh_out)
{
	struct gfs2_leaf *leaf;
	int error;

	leaf = (struct gfs2_leaf *)BH_DATA(bh_in);

	if (!leaf->lf_next)
		error = -ENOENT;
	else
		error = get_leaf(dip, gfs2_64_to_cpu(leaf->lf_next), bh_out);
	return error;
}

/**
 * leaf_search
 * @bh:
 * @id:
 * @dent_out:
 * @dent_prev:
 *
 * Returns:
 */
static int leaf_search(struct buffer_head *bh, identifier_t *id,
                       struct gfs2_dirent **dent_out,
		       struct gfs2_dirent **dent_prev)
{
	uint32_t hash;
	struct gfs2_dirent *dent, *prev = NULL;
	unsigned int entries = 0, x = 0;
	int type;

	type = dirent_first(bh, &dent);

	if (type == IS_LEAF){
		struct gfs2_leaf *leaf = (struct gfs2_leaf *)BH_DATA(bh);
		entries = gfs2_16_to_cpu(leaf->lf_entries);
	} else if (type == IS_DINODE) {
		struct gfs2_dinode *dinode = (struct gfs2_dinode *)(BH_DATA(bh));
		entries = gfs2_32_to_cpu(dinode->di_entries);
	} else {
		log_err("type != IS_LEAF && type != IS_DINODE\n");
		return -1;
	}

	if(id->type == ID_FILENAME){
		hash = dir_hash(id->filename);

		do{
			if (!dent->de_inum.no_formal_ino){
				prev = dent;
				continue;
			}

			if (gfs2_32_to_cpu(dent->de_hash) == hash &&
			    fs_filecmp(id->filename, (char *)(dent + 1),
				       gfs2_16_to_cpu(dent->de_name_len))){
				*dent_out = dent;
				if (dent_prev)
					*dent_prev = prev;
				return 0;
			}

			if(x >= entries){
				log_err("x >= entries (%u >= %u)\n", x, entries);
				return -1;
			}
			x++;
			prev = dent;
		} while (dirent_next(bh, &dent) == 0);
	} else if(id->type == ID_INUM){
		struct gfs2_inum inum;

		do{
			if (!dent->de_inum.no_formal_ino){
				prev = dent;
				continue;
			}

			gfs2_inum_in(&inum, (char *)&dent->de_inum);

			if(inum.no_addr == id->inum->no_addr){
				*dent_out = dent;
				if(dent_prev)
					*dent_prev = prev;
				return 0;
			}

			if(x >= entries){
				log_err("x >= entries (%u >= %u)\n", x, entries);
				return -1;
			}
			x++;
			prev = dent;
		} while (dirent_next(bh, &dent) == 0);
	} else {
		log_err("leaf_search:  Invalid type for identifier.\n");
		exit(1);
	}

	return -ENOENT;
}


/**
 * linked_leaf_search - Linked leaf search
 * @dip: The GFS2 inode
 * @id:
 * @dent_out:
 * @dent_prev:
 * @bh_out:
 *
 * Returns: 0 on sucess, error code otherwise
 */

static int linked_leaf_search(struct fsck_inode *dip, identifier_t *id,
                              struct gfs2_dirent **dent_out,
			      struct gfs2_dirent **dent_prev, struct buffer_head **bh_out)
{
	struct buffer_head *bh = NULL, *bh_next;
	uint32_t hsize, index;
	uint32_t hash;
	int error = 0;

	hsize = 1 << dip->i_di.di_depth;
	if(hsize * sizeof(uint64_t) != dip->i_di.di_size){
		log_err("hsize * sizeof(uint64_t) != dip->i_di.di_size\n");
		return -1;
	}

	/*  Figure out the address of the leaf node.  */

	if(id->type == ID_FILENAME){
		hash = dir_hash(id->filename);
		index = hash >> (32 - dip->i_di.di_depth);

		error = get_first_leaf(dip, index, &bh_next);
		if (error){
			return error;
		}

		/*  Find the entry  */
		do{
			if (bh)
				relse_buf(dip->i_sbd, bh);

			bh = bh_next;

			error = leaf_search(bh, id, dent_out, dent_prev);
			switch (error){
			case 0:
				*bh_out = bh;
				return 0;

			case -ENOENT:
				break;

			default:
				relse_buf(dip->i_sbd, bh);
				return error;
			}

			error = get_next_leaf(dip, bh, &bh_next);
		}while (!error);

		relse_buf(dip->i_sbd, bh);
	} else if(id->type == ID_INUM){
		for(index=0; index < (1 << dip->i_di.di_depth); index++){
			error = get_first_leaf(dip, index, &bh_next);
			if (error){
				return error;
			}

			/*  Find the entry  */
			do{
				if (bh)
					relse_buf(dip->i_sbd, bh);

				bh = bh_next;

				error = leaf_search(bh, id, dent_out, dent_prev);
				switch (error){
				case 0:
					*bh_out = bh;
					return 0;

				case -ENOENT:
					break;

				default:
					relse_buf(dip->i_sbd, bh);
					return error;
				}

				error = get_next_leaf(dip, bh, &bh_next);
			}while (!error);
		}
	} else {
		log_err("linked_leaf_search:  Invalid type for identifier.\n");
		exit(1);
	}
	return error;
}


/**
 * dir_e_search -
 * @dip: The GFS2 inode
 * @id:
 * @inode:
 *
 * Returns:
 */
static int dir_e_search(struct fsck_inode *dip, identifier_t *id, unsigned int *type)
{
	struct buffer_head *bh;
	struct gfs2_dirent *dent;
	int error;

	error = linked_leaf_search(dip, id, &dent, NULL, &bh);
	if (error){
		return error;
	}

	if(id->type == ID_FILENAME){
		if(id->inum){
			log_err("dir_e_search:  Illegal parameter.  inum must be NULL.\n");
			exit(1);
		}
		if(!(id->inum = (struct gfs2_inum *)malloc(sizeof(struct gfs2_inum)))) {
			log_err("Unable to allocate inum structure\n");
			return -1;
		}
		if(!memset(id->inum, 0, sizeof(struct gfs2_inum))) {
			log_err("Unable to zero inum structure\n");
			return -1;
		}

		gfs2_inum_in(id->inum, (char *)&dent->de_inum);
	} else {
		if(id->filename){
			log_err("dir_e_search:  Illegal parameter.  name must be NULL.\n");
			exit(1);
		}
		if(!(id->filename = (osi_filename_t *)malloc(sizeof(osi_filename_t)))) {
			log_err("Unable to allocate osi_filename structure\n");
			return -1;
		}
		if(!(memset(id->filename, 0, sizeof(osi_filename_t)))) {
			log_err("Unable to zero osi_filename structure\n");
			return -1;
		}

		id->filename->len = gfs2_16_to_cpu(dent->de_name_len);
		if(!(id->filename->name = malloc(id->filename->len))) {
			log_err("Unable to allocate name in osi_filename structure\n");
			free(id->filename);
			return -1;
		}
		if(!(memset(id->filename->name, 0, id->filename->len))) {
			log_err("Unable to zero name in osi_filename structure\n");
			free(id->inum);
			free(id->filename);
			return -1;
		}

		memcpy(id->filename->name, (char *)dent+sizeof(struct gfs2_dirent),
		       id->filename->len);
	}
	if (type)
		*type = gfs2_16_to_cpu(dent->de_type);

	relse_buf(dip->i_sbd, bh);

	return 0;
}


/**
 * dir_l_search -
 * @dip: The GFS2 inode
 * @id:
 * @inode:
 *
 * Returns:
 */
static int dir_l_search(struct fsck_inode *dip, identifier_t *id, unsigned int *type)
{
	struct buffer_head *dibh;
	struct gfs2_dirent *dent;
	int error;

	if(!fs_is_stuffed(dip)){
		log_err("A linear search was attempted on a directory "
			"that is not stuffed.\n");
		return -1;
	}

	error = get_and_read_buf(dip->i_sbd, dip->i_num.no_addr, &dibh, 0);
	if (error)
		goto out;


	error = leaf_search(dibh, id, &dent, NULL);
	if (error)
		goto out_drelse;

	if(id->type == ID_FILENAME){
		if(id->inum){
			log_err("dir_l_search:  Illegal parameter.  inum must be NULL.\n");
			exit(1);
		}
		id->inum = (struct gfs2_inum *)malloc(sizeof(struct gfs2_inum));
		memset(id->inum, 0, sizeof(struct gfs2_inum));

		gfs2_inum_in(id->inum, (char *)&dent->de_inum);
	} else {
		if(id->filename){
			log_err("dir_l_search:  Illegal parameter.  name must be NULL.\n");
			exit(1);
		}
		id->filename = (osi_filename_t *)malloc(sizeof(osi_filename_t));
		memset(id->filename, 0, sizeof(osi_filename_t));

		id->filename->len = gfs2_16_to_cpu(dent->de_name_len);
		id->filename->name = malloc(id->filename->len);
		memset(id->filename->name, 0, id->filename->len);

		memcpy(id->filename->name, (char *)dent+sizeof(struct gfs2_dirent),
		       id->filename->len);
	}
	if(type)
		*type = gfs2_16_to_cpu(dent->de_type);


 out_drelse:
	relse_buf(dip->i_sbd, dibh);

 out:
	return error;
}


/**
 * dir_make_exhash - Convet a stuffed directory into an ExHash directory
 * @dip: The GFS inode
 *
 * Returns: 0 on success, error code otherwise
 */

static int dir_make_exhash(struct fsck_inode *dip)
{
	struct fsck_sb *sdp = dip->i_sbd;
	struct gfs2_dirent *dent;
	struct buffer_head *bh = NULL, *dibh = NULL;
	struct gfs2_leaf *leaf;
	int y;
	uint32_t x;
	uint64_t *lp, bn;
	int error;

	/*  Sanity checks  */

	if(sizeof(struct gfs2_leaf) > sizeof(struct gfs2_dinode)){
		log_err(
			"dir_make_exhash:  on-disk leaf is larger than on-disk dinode.\n"
			"                  Unable to expand directory.\n");
		return -1;
	}

	error = get_and_read_buf(dip->i_sbd, dip->i_num.no_addr, &dibh, 0);
	if (error)
		goto fail;


	error = fs_metaalloc(dip, &bn);

	if (error)
		goto fail_drelse;


	/*  Turn over a new leaf  */

	error = get_and_read_buf(sdp, bn, &bh, 0);
	if (error)
		goto fail_drelse;

	if(check_meta(bh, 0)){
		log_err("dir_make_exhash:  Buffer has bad meta header.\n");
		goto fail_drelse;
	}

	set_meta(bh, GFS2_METATYPE_LF, GFS2_FORMAT_LF);
	memset(BH_DATA(bh) + sizeof(struct gfs2_meta_header), 0,
	       BH_SIZE(bh) - sizeof(struct gfs2_meta_header));

	/*  Fill in the leaf structure  */

	leaf = (struct gfs2_leaf *)BH_DATA(bh);

	if(dip->i_di.di_entries >= (1 << 16)){
		log_err(
			"dir_make_exhash:  Too many directory entries.\n"
			"                  Unable to expand directory.\n");
		goto fail_drelse;
	}
	leaf->lf_dirent_format = cpu_to_gfs2_32(GFS2_FORMAT_DE);
	leaf->lf_entries = cpu_to_gfs2_16(dip->i_di.di_entries);


	/*  Copy dirents  */
	memset(BH_DATA(bh)+sizeof(struct gfs2_leaf), 0, BH_SIZE(bh)-sizeof(struct gfs2_leaf));
	memcpy(BH_DATA(bh)+sizeof(struct gfs2_leaf),
	       BH_DATA(dibh)+sizeof(struct gfs2_dinode),
	       BH_SIZE(dibh)-sizeof(struct gfs2_dinode));

	/*  Find last entry  */

	x = 0;
	dirent_first(bh, &dent);

	do
	{
		if (!dent->de_inum.no_formal_ino)
			continue;

		if (++x == dip->i_di.di_entries)
			break;
	}
	while (dirent_next(bh, &dent) == 0);


	/*  Adjust the last dirent's record length
	    (Remember that dent still points to the last entry.)  */

	dent->de_rec_len = gfs2_16_to_cpu(dent->de_rec_len) +
		sizeof(struct gfs2_dinode) - sizeof(struct gfs2_leaf);
	dent->de_rec_len = cpu_to_gfs2_16(dent->de_rec_len);

	if(write_buf(dip->i_sbd, bh, 0)){
		log_err("dir_make_exhash:  bad write_buf()\n");
		goto fail_drelse;
	}
	relse_buf(dip->i_sbd, bh); bh=NULL;

	log_debug("Created a new leaf block at %"PRIu64"\n", bn);

	block_set(dip->i_sbd->bl, bn, leaf_blk);
	/*  We're done with the new leaf block, now setup the new
	    hash table.  */

	memset(BH_DATA(dibh) + sizeof(struct gfs2_dinode), 0,
	       BH_SIZE(dibh) - sizeof(struct gfs2_dinode));

	lp = (uint64_t *)(BH_DATA(dibh) + sizeof(struct gfs2_dinode));

	for (x = sdp->hash_ptrs; x--; lp++)
		*lp = cpu_to_gfs2_64(bn);

	dip->i_di.di_size = sdp->sb.sb_bsize / 2;
	dip->i_di.di_blocks++;
	dip->i_di.di_flags |= GFS2_DIF_EXHASH;
	dip->i_di.di_payload_format = 0;

	for (x = sdp->hash_ptrs, y = -1; x; x >>= 1, y++) ;
	dip->i_di.di_depth = y;

	gfs2_dinode_out(&dip->i_di, BH_DATA(dibh));

	if(write_buf(dip->i_sbd, dibh, 0)){
		log_err("dir_make_exhash: bad write_buf()\n");
		goto fail_drelse;
	}
	relse_buf(dip->i_sbd, dibh); dibh = NULL;

	return 0;



 fail_drelse:
	if(bh)
		relse_buf(dip->i_sbd, bh);
	if(dibh)
		relse_buf(dip->i_sbd, dibh);

 fail:
	return error;
}


/**
 * dir_split_leaf - Split a leaf block into two
 * @dip: The GFS inode
 * @index:
 * @leaf_no:
 *
 * Returns: 0 on success, error code on failure
 */
static int dir_split_leaf(struct fsck_inode *dip, uint32_t index, uint64_t leaf_no)
{
	struct fsck_sb *sdp = dip->i_sbd;
	struct buffer_head *nbh, *obh, *dibh;
	struct gfs2_leaf *nleaf, *oleaf;
	struct gfs2_dirent *dent, *prev = NULL, *next = NULL, *new;
	uint32_t start, len, half_len, divider;
	uint64_t bn, *lp;
	uint32_t name_len;
	int x, moved = 0;
	int error;

	/*  Allocate the new leaf block  */

	error = fs_metaalloc(dip, &bn);
	if (error)
		goto fail;


	/*  Get the new leaf block  */
	error = get_and_read_buf(sdp, bn, &nbh, 0);
	if (error)
		goto fail;

	if(check_meta(nbh, 0)){
		log_err("dir_split_leaf:  Buffer is not a meta buffer.\n");
		relse_buf(sdp, nbh);
		return -1;
	}

	set_meta(nbh, GFS2_METATYPE_LF, GFS2_FORMAT_LF);

	memset(BH_DATA(nbh)+sizeof(struct gfs2_meta_header), 0,
	       BH_SIZE(nbh)-sizeof(struct gfs2_meta_header));

	nleaf = (struct gfs2_leaf *)BH_DATA(nbh);

	nleaf->lf_dirent_format = cpu_to_gfs2_32(GFS2_FORMAT_DE);


	/*  Get the old leaf block  */

	error = get_leaf(dip, leaf_no, &obh);
	if (error)
		goto fail_nrelse;

	oleaf = (struct gfs2_leaf *)BH_DATA(obh);


	/*  Compute the start and len of leaf pointers in the hash table.  */

	len = 1 << (dip->i_di.di_depth - gfs2_16_to_cpu(oleaf->lf_depth));
	if(len == 1){
		log_err("dir_split_leaf:  Corrupted leaf block encountered.\n");
		goto fail_orelse;
	}
	half_len = len >> 1;

	start = (index & ~(len - 1));

	log_debug("Splitting leaf: len = %u, half_len = %u\n", len, half_len);

	/*  Change the pointers.
	    Don't bother distinguishing stuffed from non-stuffed.
	    This code is complicated enough already.  */

	lp = (uint64_t *)malloc(half_len * sizeof(uint64_t));
	memset(lp, 0, half_len * sizeof(uint64_t));

	error = readi(dip, (char *)lp, start * sizeof(uint64_t),
		      half_len * sizeof(uint64_t));
	if (error != half_len * sizeof(uint64_t)){
		if (error >= 0)
			error = -EIO;
		goto fail_lpfree;
	}

	/*  Change the pointers  */

	for (x = 0; x < half_len; x++)
		lp[x] = cpu_to_gfs2_64(bn);

	error = writei(dip, (char *)lp, start * sizeof(uint64_t),
		       half_len * sizeof(uint64_t));

	if (error != half_len * sizeof(uint64_t)){
		if (error >= 0)
			error = -EIO;
		goto fail_lpfree;
	}

	free(lp); lp = NULL;  /* need to set lp for failure cases */


	/*  Compute the divider  */

	divider = (start + half_len) << (32 - dip->i_di.di_depth);

	/*  Copy the entries  */

	dirent_first(obh, &dent);

	do{
		next = dent;
		if (dirent_next(obh, &next))
			next = NULL;

		if (dent->de_inum.no_formal_ino &&
		    (gfs2_32_to_cpu(dent->de_hash) < divider)){
			name_len = gfs2_16_to_cpu(dent->de_name_len);

			error = fs_dirent_alloc(dip, nbh, name_len, &new);
			if(error){
				log_err("dir_split_leaf:  fs_dirent_alloc failed.\n");
				goto fail_orelse;
			}

			new->de_inum = dent->de_inum;  /*  No endianness worries  */
			new->de_hash = dent->de_hash;  /*  No endianness worries  */
			new->de_type = dent->de_type;  /*  No endianness worries  */
			memcpy((char *)(new + 1), (char *)(dent + 1), name_len);

			nleaf->lf_entries = gfs2_16_to_cpu(nleaf->lf_entries) + 1;
			nleaf->lf_entries = cpu_to_gfs2_16(nleaf->lf_entries);

			dirent_del(dip, obh, prev, dent);
			/* Dirent del decrements entries, but we're
			 * just shifting entries around, so increment
			 * it again */
			dip->i_di.di_entries++;

			if(!gfs2_16_to_cpu(oleaf->lf_entries)){
				log_err("dir_split_leaf:  old leaf contains no entries.\n");
				goto fail_orelse;
			}
			oleaf->lf_entries = gfs2_16_to_cpu(oleaf->lf_entries) - 1;
			oleaf->lf_entries = cpu_to_gfs2_16(oleaf->lf_entries);

			if (!prev)
				prev = dent;

			moved = 1;
		}
		else
			prev = dent;

		dent = next;
	}
	while (dent);


	/*  If none of the entries got moved into the new leaf,
	    artificially fill in the first entry.  */

	if (!moved){
		error = fs_dirent_alloc(dip, nbh, 0, &new);
		if(error){
			log_err("dir_split_leaf:  fs_dirent_alloc failed..\n");
			goto fail_orelse;
		}
		new->de_inum.no_formal_ino = 0;
	}


	oleaf->lf_depth = gfs2_16_to_cpu(oleaf->lf_depth) + 1;
	oleaf->lf_depth = cpu_to_gfs2_16(oleaf->lf_depth);
	nleaf->lf_depth = oleaf->lf_depth;


	error = get_and_read_buf(dip->i_sbd, dip->i_num.no_addr, &dibh, 0);
	if(error){
		log_err("dir_split_leaf:  Unable to get inode buffer.\n");
		goto fail_orelse;
	}

	error = check_meta(dibh, GFS2_METATYPE_DI);
	if(error){
		log_err("dir_split_leaf:  Buffer #%"PRIu64" is not a directory "
			"inode.\n", BH_BLKNO(dibh));
		goto fail_drelse;
	}

	dip->i_di.di_blocks++;

	gfs2_dinode_out(&dip->i_di, BH_DATA(dibh));
	if(write_buf(sdp, dibh, 0)){
		log_err("dir_split_leaf:  Failed to write new directory inode.\n");
		goto fail_drelse;
	}
	relse_buf(sdp, dibh);


	if(write_buf(sdp, obh, 0)){
		log_err("dir_split_leaf:  Failed to write back old leaf block.\n");
		goto fail_orelse;
	}
	relse_buf(sdp, obh);
	if(write_buf(sdp, nbh, 0)){
		log_err("dir_split_leaf:  Failed to write new leaf block.\n");
		goto fail_nrelse;
	}

	log_debug("Created a new leaf block at %"PRIu64"\n", BH_BLKNO(nbh));

	block_set(dip->i_sbd->bl, BH_BLKNO(nbh), leaf_blk);

	relse_buf(sdp, nbh);

	return 0;



 fail_drelse:
	relse_buf(sdp, dibh);

 fail_lpfree:
	if(lp) free(lp);

 fail_orelse:
	relse_buf(sdp, obh);

 fail_nrelse:
	relse_buf(sdp, nbh);

 fail:
	return -1;
}


/**
 * dir_double_exhash - Double size of ExHash table
 * @dip: The GFS dinode
 *
 * Returns: 0 on success, -1 on failure
 */
static int dir_double_exhash(struct fsck_inode *dip)
{
	struct fsck_sb *sdp = dip->i_sbd;
	struct buffer_head *dibh;
	uint32_t hsize;
	uint64_t *buf;
	uint64_t *from, *to;
	uint64_t block;
	int x;
	int error = 0;

	/*  Sanity Checks  */

	hsize = 1 << dip->i_di.di_depth;
	if(hsize * sizeof(uint64_t) != dip->i_di.di_size){
		log_err("dir_double_exhash:  "
			"hash size does not correspond to di_size.\n");
		return -1;
	}


	/*  Allocate both the "from" and "to" buffers in one big chunk  */

	buf = (uint64_t *)malloc(3 * sdp->hash_bsize);
	if(!buf){
		log_err("dir_double_exhash:  "
			"Unable to allocate memory for blk ptr list.\n");
		return -1;
	}
	memset(buf, 0, 3 * sdp->hash_bsize);

	for (block = dip->i_di.di_size / sdp->hash_bsize; block--;){
		error = readi(dip, (char *)buf, block * sdp->hash_bsize,
			      sdp->hash_bsize);
		if (error != sdp->hash_bsize){
			if (error >= 0)
				error = -EIO;
			goto out;
		}

		from = buf;
		to = (uint64_t *)((char *)buf + sdp->hash_bsize);

		for (x = sdp->hash_ptrs; x--; from++){
			*to++ = *from;  /*  No endianess worries  */
			*to++ = *from;
		}

		error = writei(dip, (char *)buf + sdp->hash_bsize,
			       block * sdp->sb.sb_bsize, sdp->sb.sb_bsize);
		if (error != sdp->sb.sb_bsize){
			if (error >= 0)
				error = -EIO;
			goto out;
		}
	}

	free(buf); buf=NULL;


	error = get_and_read_buf(sdp, dip->i_num.no_addr, &dibh, 0);
	if(error){
		log_err("dir_double_exhash:  "
			"Unable to get inode buffer.\n");
		return -1;
	}

	error = check_meta(dibh, GFS2_METATYPE_DI);
	if(error){
		log_err("dir_double_exhash:  "
			"Buffer does not contain directory inode.\n");
		relse_buf(sdp, dibh);
		return -1;
	}

	dip->i_di.di_depth++;

	gfs2_dinode_out(&dip->i_di, BH_DATA(dibh));
	if(write_buf(sdp, dibh, 0)){
		log_err("dir_double_exhash:  "
			"Unable to write out directory inode.\n");
		relse_buf(sdp, dibh);
		return -1;
	}

	relse_buf(sdp, dibh);

	return 0;


 out:
	if(buf) free(buf);

	return error;
}


static int dir_e_del(struct fsck_inode *dip, osi_filename_t *filename){
	int index;
	int error;
	int found = 0;
	uint64_t leaf_no;
	struct buffer_head *bh;
	identifier_t id;
	struct gfs2_dirent *cur, *prev;

	id.type = ID_FILENAME;
	id.filename = filename;
	id.inum = NULL;

	index = (1 << (dip->i_di.di_depth))-1;

	for(; (index >= 0) && !found; index--){
		error = get_leaf_nr(dip, index, &leaf_no);
		if (error){
			log_err("dir_e_del:  Unable to get leaf number.\n");
			return error;
		}

		while(leaf_no && !found){
			if(get_leaf(dip, leaf_no, &bh)){
				stack;
				return -1;
			}

			error = leaf_search(bh, &id, &cur, &prev);
			if(id.inum) free(id.inum);

			if(error){
				if(error != -ENOENT){
					log_err("dir_e_del:  leaf_search failed.\n");
					relse_buf(dip->i_sbd, bh);
					return -1;
				}
				leaf_no = gfs2_64_to_cpu(((struct gfs2_leaf *)BH_DATA(bh))->lf_next);
				relse_buf(dip->i_sbd, bh);
			} else {
				found = 1;
			}
		}
	}

	if(!found)
		return 1;

	if(dirent_del(dip, bh, prev, cur)){
		log_err("dir_e_del:  dirent_del failed.\n");
		relse_buf(dip->i_sbd, bh);
		return -1;
	}

	relse_buf(dip->i_sbd, bh);
	return 0;
}


static int dir_l_del(struct fsck_inode *dip, struct buffer_head *dibh,
		     osi_filename_t *filename){
	int error=0;
	int got_buf = 0;
	struct gfs2_dirent *cur, *prev;
	identifier_t id;

	id.type = ID_FILENAME;
	id.filename = filename;
	id.inum = NULL;

	if(!fs_is_stuffed(dip)){
		log_crit("dir_l_del: Attempting linear delete on unstuffed"
			 " dinode.\n");
		return -1;
	}

	if(!dibh) {
		error = get_and_read_buf(dip->i_sbd, dip->i_num.no_addr,
					 &dibh, 0);
		if (error){
			log_err("dir_l_del:  Failed to read in dinode buffer.\n");
			return -1;
		}
		got_buf = 1;
	}

	error = leaf_search(dibh, &id, &cur, &prev);
	if(id.inum) free(id.inum);

	if(error){
		if(error == -ENOENT){
			log_debug("dir_l_del found no entry\n");
			if(got_buf)
				relse_buf(dip->i_sbd, dibh);
			return 1;
		} else {
			log_err("dir_l_del:  leaf_search failed.\n");
			if(got_buf)
				relse_buf(dip->i_sbd, dibh);
			return -1;
		}
	}

	if(dirent_del(dip, dibh, prev, cur)){
		stack;
		if(got_buf)
			relse_buf(dip->i_sbd, dibh);
		return -1;
	}

	if(got_buf)
		relse_buf(dip->i_sbd, dibh);
	return 0;
}


/*
 * fs_dirent_del
 * @dip
 * filename
 *
 * Delete a directory entry from a directory.  This _only_
 * removes the directory entry - leaving the dinode in
 * place.  (Likely without a link.)
 *
 * Returns: 0 on success (or if it doesn't already exist), -1 on failure
 */
int fs_dirent_del(struct fsck_inode *dip, struct buffer_head *bh, osi_filename_t *filename){
	int error;

	if(!S_ISDIR(dip->i_di.di_mode)){
		log_err("fs_dirent_del:  parent inode is not a directory.\n");
		return -1;
	}

	if (dip->i_di.di_flags & GFS2_DIF_EXHASH)
		error = dir_e_del(dip, filename);
	else
		error = dir_l_del(dip, bh, filename);

	return error;

}


/**
 * dir_e_add -
 * @dip: The GFS inode
 * @filename:
 * @inode:
 * @type:
 *
 */
static int dir_e_add(struct fsck_inode *dip, osi_filename_t *filename,
                     struct gfs2_inum *inum, uint8_t type)
{
	struct fsck_sb *sdp = dip->i_sbd;
	struct buffer_head *bh, *nbh, *dibh;
	struct gfs2_leaf *leaf, *nleaf;
	struct gfs2_dirent *dent;
	uint32_t hsize, index;
	uint32_t hash;
	uint64_t leaf_no, bn;
	int error;

 restart:

	/*  Sanity Checks  */

	hsize = 1 << dip->i_di.di_depth;
	if(hsize * sizeof(uint64_t) != dip->i_di.di_size){
		log_err("dir_e_add:  hash size and di_size do not correspond.\n");
		return -1;
	}

	/*  Figure out the address of the leaf node.  */

	hash = dir_hash(filename);
	index = hash >> (32 - dip->i_di.di_depth);


	error = get_leaf_nr(dip, index, &leaf_no);
	if (error){
		log_err("dir_e_add:  Unable to get leaf number.\n");
		return error;
	}


	/*  Add entry to the leaf  */

	while (1){
		error = get_leaf(dip, leaf_no, &bh);
		if (error){
			log_err("dir_e_add:  Unable to get leaf #%"PRIu64"\n", leaf_no);
			return error;
		}

		leaf = (struct gfs2_leaf *)BH_DATA(bh);


		if (fs_dirent_alloc(dip, bh, filename->len, &dent)){
			if (gfs2_16_to_cpu(leaf->lf_depth) < dip->i_di.di_depth){
				/*  Can we split the leaf?  */
				relse_buf(sdp, bh);

				error = dir_split_leaf(dip, index, leaf_no);
				if (error){
					log_err("dir_e_add:  Unable to split leaf.\n");
					return error;
				}

				goto restart;
			}
			else if (dip->i_di.di_depth < GFS2_DIR_MAX_DEPTH){
				/*  Can we double the hash table?  */
				relse_buf(sdp, bh);

				error = dir_double_exhash(dip);
				if (error){
					log_err("dir_e_add:  Unable to double exhash.\n");
					return error;
				}

				goto restart;
			}
			else if (leaf->lf_next){
				/*  Can we try the next leaf in the list?  */
				leaf_no = gfs2_64_to_cpu(leaf->lf_next);
				relse_buf(sdp, bh);
				continue;
			}
			else {
				/*  Create a new leaf and add it to the list.  */
				error = fs_metaalloc(dip, &bn);
				if (error){
					relse_buf(sdp, bh);
					log_err("dir_e_add:  "
						"Unable to allocate space for meta block.\n");
					return error;
				}

				error = get_and_read_buf(sdp, bn, &nbh, 0);
				if (error){
					relse_buf(sdp, bh);
					return error;
				}

				/*gfs2_trans_add_bh(sdp, dip->i_gl, nbh);*/
				if(check_meta(nbh, 0)){
					log_err("dir_e_add:  Buffer is not a meta buffer.\n");
					relse_buf(sdp, bh);
					relse_buf(sdp, nbh);
					return -1;
				}
				set_meta(nbh, GFS2_METATYPE_LF, GFS2_FORMAT_LF);
				/* Make sure the bitmap is updated */
				log_debug("Setting leaf block at %"PRIu64"\n",
					  bn);
				block_set(dip->i_sbd->bl, bn, leaf_blk);
				memset(BH_DATA(nbh)+sizeof(struct gfs2_meta_header), 0,
				       BH_SIZE(nbh)-sizeof(struct gfs2_meta_header));

				/*gfs2_trans_add_bh(sdp, dip->i_gl, bh);*/
				leaf->lf_next = cpu_to_gfs2_64(bn);

				nleaf = (struct gfs2_leaf *)BH_DATA(nbh);
				nleaf->lf_depth = leaf->lf_depth;
				nleaf->lf_dirent_format = cpu_to_gfs2_32(GFS2_FORMAT_DE);

				if (fs_dirent_alloc(dip, nbh, filename->len, &dent)){
					log_err("dir_e_add:  Uncircumventible error!\n");
					exit(EXIT_FAILURE);
				}

				dip->i_di.di_blocks++;

				/* ATTENTION -- check for errors */
				write_buf(sdp, nbh, BW_WAIT);
				write_buf(sdp, bh, 0);
				relse_buf(sdp, bh);

				bh = nbh;
				leaf = nleaf;
			}
		}


		gfs2_inum_out(inum, (char *)&dent->de_inum);
		dent->de_hash = cpu_to_gfs2_32(hash);
		dent->de_type = type;
		memcpy((char *)(dent + 1), filename->name, filename->len);

		leaf->lf_entries = gfs2_16_to_cpu(leaf->lf_entries) + 1;
		leaf->lf_entries = cpu_to_gfs2_16(leaf->lf_entries);

		write_buf(sdp, bh, 0);
		relse_buf(sdp, bh);

		error = get_and_read_buf(dip->i_sbd, dip->i_num.no_addr, &dibh, 0);
		if(error){
			log_err("dir_e_add:  Unable to get inode buffer.\n");
			return error;
		}

		error = check_meta(dibh, GFS2_METATYPE_DI);
		if(error){
			log_err("dir_e_add:  Buffer #%"PRIu64" is not a directory "
				"inode.\n", BH_BLKNO(dibh));
			relse_buf(sdp, dibh);
			return error;
		}

		dip->i_di.di_entries++;
		dip->i_di.di_mtime = dip->i_di.di_ctime = time(NULL);
		log_debug("Entries for %"PRIu64" is %u\n", dip->i_di.di_num.no_addr,
			dip->i_di.di_entries);

		gfs2_dinode_out(&dip->i_di, BH_DATA(dibh));
		write_buf(sdp, dibh, 0);
		relse_buf(sdp, dibh);

		return 0;
	}

	return -ENOENT;
}


/**
 * dir_l_add -
 * @dip: The GFS inode
 * @filename:
 * @inode:
 * @type:
 *
 * Returns:
 */

static int dir_l_add(struct fsck_inode *dip, osi_filename_t *filename,
                     struct gfs2_inum *inum, uint8_t type)
{
	struct buffer_head *dibh;
	struct gfs2_dirent *dent;
	int error;

	/*  Sanity checks  */

	if(!fs_is_stuffed(dip)){
		log_err("dir_l_add:  Attempting linear add on unstuffed dinode.\n");
		return -1;
	}

	error = get_and_read_buf(dip->i_sbd, dip->i_num.no_addr, &dibh, 0);
	if (error)
		goto out;


	if (fs_dirent_alloc(dip, dibh, filename->len, &dent))
	{
		/* no need to write buffer, it hasn't changed. */
		relse_buf(dip->i_sbd, dibh);

		error = dir_make_exhash(dip);
		/* DEBUG */
		log_debug("Changing Linear dir to Exhash dir - %s\n",
			  (error)? "UNSUCCESSFUL": "SUCCESSFUL");
		if (!error)
			error = dir_e_add(dip, filename, inum, type);

		goto out;
	}


	gfs2_inum_out(inum, (char *)&dent->de_inum);
	dent->de_hash = dir_hash(filename);
	dent->de_hash = cpu_to_gfs2_32(dent->de_hash);
	dent->de_type = type;
	memcpy((char *)(dent + 1), filename->name, filename->len);


	dip->i_di.di_entries++;
	dip->i_di.di_mtime = dip->i_di.di_ctime = time(NULL);
	log_debug("Entries for %"PRIu64" is %u\n", dip->i_di.di_num.no_addr,
		dip->i_di.di_entries);
	gfs2_dinode_out(&dip->i_di, BH_DATA(dibh));
	if(write_buf(dip->i_sbd, dibh, 0)){
		log_err("dir_l_add:  bad write_buf()\n");
		error = -EIO;
	}

	relse_buf(dip->i_sbd, dibh);

 out:
	if(error){
		char tmp_name[256];
		memset(tmp_name, 0, sizeof(tmp_name));
		memcpy(tmp_name, filename->name, filename->len);
		log_err("Unable to add \"%s\" to directory #%"PRIu64"\n",
			tmp_name, dip->i_num.no_addr);
	}
	return error;
}

uint8_t convert_to_type(int32_t mode)
{
	switch(mode & S_IFMT) {
	case S_IFSOCK:
		return DT_SOCK;
	case S_IFLNK:
		return DT_LNK;
	case S_IFREG:
		return DT_REG;
	case S_IFBLK:
		return DT_BLK;
	case S_IFDIR:
		return DT_DIR;
	case S_IFCHR:
		return DT_CHR;
	case S_IFIFO:
		return DT_FIFO;
	default:
		return DT_UNKNOWN;
	}
}

/**
 * fs_dir_add - Add new filename into directory
 * @dip: The GFS inode
 * @filename: The new name
 * @inode: The inode number of the entry
 * @type: The type of the entry
 *
 * Returns: 0 on success, error code on failure
 */
int fs_dir_add(struct fsck_inode *dip, osi_filename_t *filename,
	       struct gfs2_inum *inum, uint32_t mode)
{
	int error;
	uint8_t type;

	if(!S_ISDIR(dip->i_di.di_mode)){
		log_err("fs_dir_add:  parent inode is not a directory.\n");
		return -1;
	}

	type = convert_to_type(mode);

	if (dip->i_di.di_flags & GFS2_DIF_EXHASH)
		error = dir_e_add(dip, filename, inum, type);
	else
		error = dir_l_add(dip, filename, inum, type);

	return error;
}


/**
 * fs_dirent_alloc - Allocate a directory entry
 * @dip: The GFS inode
 * @bh: The buffer
 * @name_len: The length of the name
 * @dent_out: Pointer to list of dirents
 *
 * Returns: 0 on success, error code otherwise
 */
int fs_dirent_alloc(struct fsck_inode *dip, struct buffer_head *bh,
		    int name_len, struct gfs2_dirent **dent_out)
{
	struct fsck_sb *sdp = dip->i_sbd;
	struct gfs2_dirent *dent, *new;
	struct gfs2_leaf *leaf;
	struct gfs2_dinode *dinode;
	unsigned int rec_len = GFS2_DIRENT_SIZE(name_len);
	unsigned int entries = 0, offset = 0, x = 0;
	int type;

	type = dirent_first(bh, &dent);

	if (type == IS_LEAF){
		leaf = (struct gfs2_leaf *)BH_DATA(bh);
		entries = gfs2_16_to_cpu(leaf->lf_entries);
		offset = sizeof(struct gfs2_leaf);
	}
	else if (type == IS_DINODE) {
		dinode = (struct gfs2_dinode *)BH_DATA(bh);
		entries = gfs2_32_to_cpu(dinode->di_entries);
		offset = sizeof(struct gfs2_dinode);
	} else {
		log_err("fs_dirent_alloc:  Buffer has bad metatype.\n");
		return -1;
	}

	if (!entries){
		dent->de_rec_len = BH_SIZE(bh) - offset;
		dent->de_rec_len = cpu_to_gfs2_16(dent->de_rec_len);
		dent->de_name_len = cpu_to_gfs2_16(name_len);

		*dent_out = dent;
		write_buf(sdp, bh, 0);
		goto success;
	}


	do{
		uint32_t cur_rec_len, cur_name_len;

		cur_rec_len = gfs2_16_to_cpu(dent->de_rec_len);
		cur_name_len = gfs2_16_to_cpu(dent->de_name_len);

		if ((!dent->de_inum.no_formal_ino && cur_rec_len >= rec_len) ||
		    (cur_rec_len >= GFS2_DIRENT_SIZE(cur_name_len) + rec_len)){
			if (dent->de_inum.no_formal_ino){
				new = (struct gfs2_dirent *)((char *)dent + GFS2_DIRENT_SIZE(cur_name_len));
				memset(new, 0, sizeof(struct gfs2_dirent));

				new->de_rec_len = cpu_to_gfs2_16(cur_rec_len - GFS2_DIRENT_SIZE(cur_name_len));
				new->de_name_len = cpu_to_gfs2_16(name_len);

				dent->de_rec_len = cur_rec_len - gfs2_16_to_cpu(new->de_rec_len);
				dent->de_rec_len = cpu_to_gfs2_16(dent->de_rec_len);

				*dent_out = new;
				write_buf(sdp, bh, 0);
				goto success;
			}

			dent->de_name_len = cpu_to_gfs2_16(name_len);

			*dent_out = dent;
			write_buf(sdp, bh, 0);
			goto success;
		}

		if(x >= entries){
			log_err("fs_dirent_alloc:  dirents contain bad length information.\n");
			return -1;
		}

		if (dent->de_inum.no_formal_ino)
			x++;
	}
	while(dirent_next(bh, &dent) == 0);

	return -ENOSPC;

 success:
	return 0;
}


/**
 * get_leaf_nr - Get a leaf number associated with the index
 * @dip: The GFS inode
 * @index:
 * @leaf_out:
 *
 * Returns: 0 on success, error code otherwise
 */

int get_leaf_nr(struct fsck_inode *dip, uint32_t index, uint64_t *leaf_out)
{
	uint64_t leaf_no;
	int error = -1;
	error = readi(dip, (char *)&leaf_no,
		      index * sizeof(uint64_t), sizeof(uint64_t));
	if (error != sizeof(uint64_t)){
		log_debug("get_leaf_nr:  Bad internal read.  (rtn = %d)\n",
			  error);
		return (error < 0) ? error : -EIO;
	}

	*leaf_out = gfs2_64_to_cpu(leaf_no);

	return 0;
}


/**
 * fs_filecmp - Compare two filenames
 * @file1: The first filename
 * @file2: The second filename
 * @len_of_file2: The length of the second file
 *
 * This routine compares two filenames and returns 1 if they are equal.
 *
 * Returns: 1 if the files are the same, otherwise 0.
 */
int fs_filecmp(osi_filename_t *file1, char *file2, int len_of_file2)
{
	if (file1->len != len_of_file2){
		return 0;
	}

	if (memcmp(file1->name, file2, file1->len)){
		return 0;
	}
	return 1;
}


/**
 * fs_dir_search - Search a directory
 * @dip: The GFS inode
 * @id
 * @type:
 *
 * This routine searches a directory for a file or another directory
 * given its identifier.  The component of the identifier that is
 * not being used to search will be filled in and must be freed by
 * the caller.
 *
 * Returns: 0 if found, -1 on failure, -ENOENT if not found.
 */
int fs_dir_search(struct fsck_inode *dip, identifier_t *id,  unsigned int *type)
{
	int error;

	if(!S_ISDIR(dip->i_di.di_mode)) {
		log_err("An attempt was made to search an inode "
			"that is not a directory.\n");
		return -1;
	}

	if (dip->i_di.di_flags & GFS2_DIF_EXHASH)
		error = dir_e_search(dip, id, type);
	else
		error = dir_l_search(dip, id, type);

	return error;
}
