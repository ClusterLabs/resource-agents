/*
* Implements Extendible Hashing as described in:
*   "Extendible Hashing" by Fagin, et al in
*     __ACM Trans. on Database Systems__, Sept 1979.
*
*
* Here's the layout of dirents which is essentially the same as that of ext2 
* within a single block. The field de_name_len is the number of bytes
* actually required for the name (no null terminator). The field de_rec_len
* is the number of bytes allocated to the dirent. The offset of the next
* dirent in the block is (dirent + dirent->de_rec_len). When a dirent is
* deleted, the preceding dirent inherits its allocated space, ie
* prev->de_rec_len += deleted->de_rec_len. Since the next dirent is obtained
* by adding de_rec_len to the current dirent, this essentially causes the
* deleted dirent to get jumped over when iterating through all the dirents.
*
* When deleting the first dirent in a block, there is no previous dirent so
* the field de_ino is set to zero to designate it as deleted. When allocating
* a dirent, gfs_dirent_alloc iterates through the dirents in a block. If the
* first dirent has (de_ino == 0) and de_rec_len is large enough, this first
* dirent is allocated. Otherwise it must go through all the 'used' dirents
* searching for one in which the amount of total space minus the amount of
* used space will provide enough space for the new dirent.
*
* There are two types of blocks in which dirents reside. In a stuffed dinode,
* the dirents begin at offset sizeof(struct gfs_dinode) from the beginning of
* the block.  In leaves, they begin at offset sizeof (struct gfs_leaf) from the
* beginning of the leaf block. The dirents reside in leaves when 
* 
* dip->i_di.di_flags & GFS_DIF_EXHASH is true
* 
* Otherwise, the dirents are "linear", within a single stuffed dinode block.
*
* When the dirents are in leaves, the actual contents of the directory file are
* used as an array of 64-bit block pointers pointing to the leaf blocks. The
* dirents are NOT in the directory file itself. There can be more than one block
* pointer in the array that points to the same leaf. In fact, when a directory
* is first converted from linear to exhash, all of the pointers point to the
* same leaf. 
*
* When a leaf is completely full, the size of the hash table can be
* doubled unless it is already at the maximum size which is hard coded into 
* GFS_DIR_MAX_DEPTH. After that, leaves are chained together in a linked list,
* but never before the maximum hash table size has been reached.
*/

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/vmalloc.h>

#include "gfs.h"
#include "dio.h"
#include "dir.h"
#include "file.h"
#include "glock.h"
#include "inode.h"
#include "quota.h"
#include "rgrp.h"
#include "trans.h"

#if 1
#define gfs_dir_hash2offset(h) (((uint64_t)(h)) >> 1)
#define gfs_dir_offset2hash(p) ((uint32_t)(((uint64_t)(p)) << 1))
#else
#define gfs_dir_hash2offset(h) (((uint64_t)(h)))
#define gfs_dir_offset2hash(p) ((uint32_t)(((uint64_t)(p))))
#endif

typedef int (*leaf_call_t) (struct gfs_inode *dip,
			    uint32_t index, uint32_t len, uint64_t leaf_no,
			    void *data);

/**
 * int gfs_filecmp - Compare two filenames
 * @file1: The first filename
 * @file2: The second filename
 * @len_of_file2: The length of the second file
 *
 * This routine compares two filenames and returns TRUE if they are equal.
 *
 * Returns: TRUE (!=0) if the files are the same, otherwise FALSE (0).
 */

int
gfs_filecmp(struct qstr *file1, char *file2, int len_of_file2)
{
	if (file1->len != len_of_file2)
		return FALSE;
	if (memcmp(file1->name, file2, file1->len))
		return FALSE;
	return TRUE;
}

/**
 * dirent_first - Return the first dirent
 * @dip: the directory
 * @bh: The buffer
 * @dent: Pointer to list of dirents
 *
 * return first dirent whether bh points to leaf or stuffed dinode
 *
 * Returns: IS_LEAF, IS_DINODE, or -errno
 */

static int
dirent_first(struct gfs_inode *dip, struct buffer_head *bh,
	     struct gfs_dirent **dent)
{
	struct gfs_meta_header *h = (struct gfs_meta_header *)bh->b_data;

	if (gfs32_to_cpu(h->mh_type) == GFS_METATYPE_LF) {
		if (gfs_meta_check(dip->i_sbd, bh))
			return -EIO;
		*dent = (struct gfs_dirent *)(bh->b_data + sizeof(struct gfs_leaf));
		return IS_LEAF;
	} else {
		if (gfs_metatype_check(dip->i_sbd, bh, GFS_METATYPE_DI))
			return -EIO;
		*dent = (struct gfs_dirent *)(bh->b_data + sizeof(struct gfs_dinode));
		return IS_DINODE;
	}
}

/**
 * dirent_next - Next dirent
 * @dip: the directory
 * @bh: The buffer
 * @dent: Pointer to list of dirents
 *
 * Returns: 0 on success, error code otherwise
 */

static int
dirent_next(struct gfs_inode *dip, struct buffer_head *bh,
	    struct gfs_dirent **dent)
{
	struct gfs_dirent *tmp, *cur;
	char *bh_end;
	uint32_t cur_rec_len;

	cur = *dent;
	bh_end = bh->b_data + bh->b_size;
	cur_rec_len = gfs16_to_cpu(cur->de_rec_len);

	if ((char *)cur + cur_rec_len >= bh_end) {
		if ((char *)cur + cur_rec_len > bh_end) {
			gfs_consist_inode(dip);
			return -EIO;
		}
		return -ENOENT;
	}

	tmp = (struct gfs_dirent *)((char *)cur + cur_rec_len);

	if ((char *)tmp + gfs16_to_cpu(tmp->de_rec_len) > bh_end) {
		gfs_consist_inode(dip);
		return -EIO;
	}
        /* Only the first dent could ever have de_ino == 0 */
	if (!tmp->de_inum.no_formal_ino) {
		gfs_consist_inode(dip);
		return -EIO;
	}

	*dent = tmp;

	return 0;
}

/**
 * dirent_del - Delete a dirent
 * @dip: The GFS inode
 * @bh: The buffer
 * @prev: The previous dirent
 * @cur: The current dirent
 *
 */

static void
dirent_del(struct gfs_inode *dip, struct buffer_head *bh,
	   struct gfs_dirent *prev, struct gfs_dirent *cur)
{
	uint32_t cur_rec_len, prev_rec_len;

	if (!cur->de_inum.no_formal_ino) {
		gfs_consist_inode(dip);
		return;
	}

	gfs_trans_add_bh(dip->i_gl, bh);

	/* If there is no prev entry, this is the first entry in the block.
	   The de_rec_len is already as big as it needs to be.  Just zero
	   out the inode number and return.  */

	if (!prev) {
		cur->de_inum.no_formal_ino = 0;	/* No endianess worries */
		return;
	}

	/*  Combine this dentry with the previous one.  */

	prev_rec_len = gfs16_to_cpu(prev->de_rec_len);
	cur_rec_len = gfs16_to_cpu(cur->de_rec_len);

	if ((char *)prev + prev_rec_len != (char *)cur)
		gfs_consist_inode(dip);
	if ((char *)cur + cur_rec_len > bh->b_data + bh->b_size)
		gfs_consist_inode(dip);

	prev_rec_len += cur_rec_len;
	prev->de_rec_len = cpu_to_gfs16(prev_rec_len);
}

/**
 * gfs_dirent_alloc - Allocate a directory entry
 * @dip: The GFS inode
 * @bh: The buffer
 * @name_len: The length of the name
 * @dent_out: Pointer to list of dirents
 *
 * Returns: 0 on success, error code otherwise
 */

int
gfs_dirent_alloc(struct gfs_inode *dip, struct buffer_head *bh, int name_len,
		 struct gfs_dirent **dent_out)
{
	struct gfs_dirent *dent, *new;
	unsigned int rec_len = GFS_DIRENT_SIZE(name_len);
	unsigned int entries = 0, offset = 0;
	int type;

	type = dirent_first(dip, bh, &dent);
	if (type < 0)
		return type;

	if (type == IS_LEAF) {
		struct gfs_leaf *leaf = (struct gfs_leaf *)bh->b_data;
		entries = gfs16_to_cpu(leaf->lf_entries);
		offset = sizeof(struct gfs_leaf);
	} else {
		struct gfs_dinode *dinode = (struct gfs_dinode *)bh->b_data;
		entries = gfs32_to_cpu(dinode->di_entries);
		offset = sizeof(struct gfs_dinode);
	}

	if (!entries) {
		if (dent->de_inum.no_formal_ino) {
			gfs_consist_inode(dip);
			return -EIO;
		}

		gfs_trans_add_bh(dip->i_gl, bh);

		dent->de_rec_len = bh->b_size - offset;
		dent->de_rec_len = cpu_to_gfs16(dent->de_rec_len);
		dent->de_name_len = cpu_to_gfs16(name_len);

		*dent_out = dent;
		return 0;
	}

	do {
		uint32_t cur_rec_len, cur_name_len;

		cur_rec_len = gfs16_to_cpu(dent->de_rec_len);
		cur_name_len = gfs16_to_cpu(dent->de_name_len);

		if ((!dent->de_inum.no_formal_ino && cur_rec_len >= rec_len) ||
		    (cur_rec_len >= GFS_DIRENT_SIZE(cur_name_len) + rec_len)) {
			gfs_trans_add_bh(dip->i_gl, bh);

			if (dent->de_inum.no_formal_ino) {
				new = (struct gfs_dirent *)((char *)dent +
							    GFS_DIRENT_SIZE(cur_name_len));
				memset(new, 0, sizeof(struct gfs_dirent));

				new->de_rec_len = cpu_to_gfs16(cur_rec_len -
							       GFS_DIRENT_SIZE(cur_name_len));
				new->de_name_len = cpu_to_gfs16(name_len);

				dent->de_rec_len = cur_rec_len - gfs16_to_cpu(new->de_rec_len);
				dent->de_rec_len = cpu_to_gfs16(dent->de_rec_len);

				*dent_out = new;
				return 0;
			}

			dent->de_name_len = cpu_to_gfs16(name_len);

			*dent_out = dent;
			return 0;
		}
	} while (dirent_next(dip, bh, &dent) == 0);

	return -ENOSPC;
}

/**
 * dirent_fits - See if we can fit a entry in this buffer
 * @dip: The GFS inode
 * @bh: The buffer
 * @name_len: The length of the name
 *
 * Returns: TRUE if it can fit, FALSE otherwise
 */

static int
dirent_fits(struct gfs_inode *dip, struct buffer_head *bh, int name_len)
{
	struct gfs_dirent *dent;
	unsigned int rec_len = GFS_DIRENT_SIZE(name_len);
	unsigned int entries = 0;
	int type;

	type = dirent_first(dip, bh, &dent);
	if (type < 0)
		return type;

	if (type == IS_LEAF) {
		struct gfs_leaf *leaf = (struct gfs_leaf *)bh->b_data;
		entries = gfs16_to_cpu(leaf->lf_entries);
	} else {
		struct gfs_dinode *dinode = (struct gfs_dinode *)bh->b_data;
		entries = gfs32_to_cpu(dinode->di_entries);
	}

	if (!entries)
		return TRUE;

	do {
		uint32_t cur_rec_len, cur_name_len;

		cur_rec_len = gfs16_to_cpu(dent->de_rec_len);
		cur_name_len = gfs16_to_cpu(dent->de_name_len);

		if ((!dent->de_inum.no_formal_ino && cur_rec_len >= rec_len) ||
		    (cur_rec_len >= GFS_DIRENT_SIZE(cur_name_len) + rec_len))
			return TRUE;
	} while (dirent_next(dip, bh, &dent) == 0);

	return FALSE;
}

/**
 * leaf_search
 * @bh:
 * @filename:
 * @dent_out:
 * @dent_prev:
 *
 * Returns:
 */

static int
leaf_search(struct gfs_inode *dip,
	    struct buffer_head *bh, struct qstr *filename,
	    struct gfs_dirent **dent_out, struct gfs_dirent **dent_prev)
{
	uint32_t hash;
	struct gfs_dirent *dent, *prev = NULL;
	unsigned int entries = 0;
	int type;

	type = dirent_first(dip, bh, &dent);
	if (type < 0)
		return type;

	if (type == IS_LEAF) {
		struct gfs_leaf *leaf = (struct gfs_leaf *)bh->b_data;
		entries = gfs16_to_cpu(leaf->lf_entries);
	} else if (type == IS_DINODE) {
		struct gfs_dinode *dinode = (struct gfs_dinode *)bh->b_data;
		entries = gfs32_to_cpu(dinode->di_entries);
	}

	hash = gfs_dir_hash(filename->name, filename->len);

	do {
		if (!dent->de_inum.no_formal_ino) {
			prev = dent;
			continue;
		}

		if (gfs32_to_cpu(dent->de_hash) == hash &&
		    gfs_filecmp(filename, (char *)(dent + 1),
				gfs16_to_cpu(dent->de_name_len))) {
			*dent_out = dent;
			if (dent_prev)
				*dent_prev = prev;

			return 0;
		}

		prev = dent;
	} while (dirent_next(dip, bh, &dent) == 0);

	return -ENOENT;
}

/**
 * get_leaf - Get leaf
 * @dip:
 * @leaf_no:
 * @bh_out:
 *
 * Returns: 0 on success, error code otherwise
 */

static int
get_leaf(struct gfs_inode *dip, uint64_t leaf_no, struct buffer_head **bhp)
{
	int error;

	error = gfs_dread(dip->i_gl, leaf_no, DIO_START | DIO_WAIT, bhp);
	if (!error && gfs_metatype_check(dip->i_sbd, *bhp, GFS_METATYPE_LF))
		error = -EIO;

	return error;
}

/**
 * get_leaf_nr - Get a leaf number associated with the index
 * @dip: The GFS inode
 * @index:
 * @leaf_out:
 *
 * Returns: 0 on success, error code otherwise
 */

static int
get_leaf_nr(struct gfs_inode *dip, uint32_t index, uint64_t *leaf_out)
{
	uint64_t leaf_no;
	int error;

	error = gfs_internal_read(dip, (char *)&leaf_no,
				  index * sizeof(uint64_t),
				  sizeof(uint64_t));
	if (error != sizeof(uint64_t))
		return (error < 0) ? error : -EIO;

	*leaf_out = gfs64_to_cpu(leaf_no);

	return 0;
}

/**
 * get_first_leaf - Get first leaf
 * @dip: The GFS inode
 * @index:
 * @bh_out:
 *
 * Returns: 0 on success, error code otherwise
 */

static int
get_first_leaf(struct gfs_inode *dip, uint32_t index,
	       struct buffer_head **bh_out)
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
 * @dip: The GFS inode
 * @bh_in: The buffer
 * @bh_out:
 *
 * Returns: 0 on success, error code otherwise
 */

static int
get_next_leaf(struct gfs_inode *dip, struct buffer_head *bh_in,
	      struct buffer_head **bh_out)
{
	struct gfs_leaf *leaf;
	int error;

	leaf = (struct gfs_leaf *)bh_in->b_data;

	if (!leaf->lf_next)
		error = -ENOENT;
	else
		error = get_leaf(dip, gfs64_to_cpu(leaf->lf_next), bh_out);

	return error;
}

/**
 * linked_leaf_search - Linked leaf search
 * @dip: The GFS inode
 * @filename: The filename to search for
 * @dent_out:
 * @dent_prev:
 * @bh_out:
 *
 * Returns: 0 on sucess, error code otherwise
 */

static int
linked_leaf_search(struct gfs_inode *dip, struct qstr *filename,
		   struct gfs_dirent **dent_out, struct gfs_dirent **dent_prev,
		   struct buffer_head **bh_out)
{
	struct buffer_head *bh = NULL, *bh_next;
	uint32_t hsize, index;
	uint32_t hash;
	int error;

	hsize = 1 << dip->i_di.di_depth;
	if (hsize * sizeof(uint64_t) != dip->i_di.di_size) {
		gfs_consist_inode(dip);
		return -EIO;
	}

	/*  Figure out the address of the leaf node.  */

	hash = gfs_dir_hash(filename->name, filename->len);
	index = hash >> (32 - dip->i_di.di_depth);

	error = get_first_leaf(dip, index, &bh_next);
	if (error)
		return error;

	/*  Find the entry  */

	do {
		if (bh)
			brelse(bh);

		bh = bh_next;

		error = leaf_search(dip, bh, filename, dent_out, dent_prev);
		switch (error) {
		case 0:
			*bh_out = bh;
			return 0;

		case -ENOENT:
			break;

		default:
			brelse(bh);
			return error;
		}

		error = get_next_leaf(dip, bh, &bh_next);
	}
	while (!error);

	brelse(bh);

	return error;
}

/**
 * dir_make_exhash - Convert a stuffed directory into an ExHash directory
 * @dip: The GFS inode
 *
 * Returns: 0 on success, error code otherwise
 */

static int
dir_make_exhash(struct gfs_inode *dip)
{
	struct gfs_sbd *sdp = dip->i_sbd;
	struct gfs_dirent *dent;
	struct buffer_head *bh, *dibh;
	struct gfs_leaf *leaf;
	int y;
	uint32_t x;
	uint64_t *lp, bn;
	int error;

	error = gfs_get_inode_buffer(dip, &dibh);
	if (error)
		return error;

	/*  Allocate a new block for the first leaf node  */

	error = gfs_metaalloc(dip, &bn);
	if (error)
		goto fail;

	/*  Turn over a new leaf  */

	error = gfs_dread(dip->i_gl, bn, DIO_NEW | DIO_START | DIO_WAIT, &bh);
	if (error)
		goto fail;

	gfs_trans_add_bh(dip->i_gl, bh);
	gfs_metatype_set(bh, GFS_METATYPE_LF, GFS_FORMAT_LF);
	gfs_buffer_clear_tail(bh, sizeof(struct gfs_meta_header));

	/*  Fill in the leaf structure  */

	leaf = (struct gfs_leaf *)bh->b_data;

	gfs_assert(sdp, dip->i_di.di_entries < (1 << 16),);

	leaf->lf_dirent_format = cpu_to_gfs32(GFS_FORMAT_DE);
	leaf->lf_entries = cpu_to_gfs16(dip->i_di.di_entries);

	/*  Copy dirents  */

	gfs_buffer_copy_tail(bh, sizeof(struct gfs_leaf), dibh,
			     sizeof(struct gfs_dinode));

	/*  Find last entry  */

	x = 0;
	dirent_first(dip, bh, &dent);

	do {
		if (!dent->de_inum.no_formal_ino)
			continue;
		if (++x == dip->i_di.di_entries)
			break;
	}
	while (dirent_next(dip, bh, &dent) == 0);

	/*  Adjust the last dirent's record length
	   (Remember that dent still points to the last entry.)  */

	dent->de_rec_len = gfs16_to_cpu(dent->de_rec_len) +
		sizeof(struct gfs_dinode) -
		sizeof(struct gfs_leaf);
	dent->de_rec_len = cpu_to_gfs16(dent->de_rec_len);

	brelse(bh);

	/*  We're done with the new leaf block, now setup the new
	    hash table.  */

	gfs_trans_add_bh(dip->i_gl, dibh);
	gfs_buffer_clear_tail(dibh, sizeof (struct gfs_dinode));

	lp = (uint64_t *)(dibh->b_data + sizeof(struct gfs_dinode));

	for (x = sdp->sd_hash_ptrs; x--; lp++)
		*lp = cpu_to_gfs64(bn);

	dip->i_di.di_size = sdp->sd_sb.sb_bsize / 2;
	dip->i_di.di_blocks++;
	dip->i_di.di_flags |= GFS_DIF_EXHASH;
	dip->i_di.di_payload_format = 0;

	for (x = sdp->sd_hash_ptrs, y = -1; x; x >>= 1, y++) ;
	dip->i_di.di_depth = y;

	gfs_dinode_out(&dip->i_di, dibh->b_data);

	brelse(dibh);

	return 0;

 fail:
	brelse(dibh);
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

static int
dir_split_leaf(struct gfs_inode *dip, uint32_t index, uint64_t leaf_no)
{
	struct buffer_head *nbh, *obh, *dibh;
	struct gfs_leaf *nleaf, *oleaf;
	struct gfs_dirent *dent, *prev = NULL, *next = NULL, *new;
	uint32_t start, len, half_len, divider;
	uint64_t bn, *lp;
	uint32_t name_len;
	int x, moved = FALSE;
	int error, lp_vfree=0;

	/*  Allocate the new leaf block  */

	error = gfs_metaalloc(dip, &bn);
	if (error)
		return error;

	/*  Get the new leaf block  */

	error = gfs_dread(dip->i_gl, bn,
			  DIO_NEW | DIO_START | DIO_WAIT, &nbh);
	if (error)
		return error;

	gfs_trans_add_bh(dip->i_gl, nbh);
	gfs_metatype_set(nbh, GFS_METATYPE_LF, GFS_FORMAT_LF);
	gfs_buffer_clear_tail(nbh, sizeof (struct gfs_meta_header));

	nleaf = (struct gfs_leaf *)nbh->b_data;

	nleaf->lf_dirent_format = cpu_to_gfs32(GFS_FORMAT_DE);

	/*  Get the old leaf block  */

	error = get_leaf(dip, leaf_no, &obh);
	if (error)
		goto fail;

	gfs_trans_add_bh(dip->i_gl, obh);

	oleaf = (struct gfs_leaf *)obh->b_data;

	/*  Compute the start and len of leaf pointers in the hash table.  */

	len = 1 << (dip->i_di.di_depth - gfs16_to_cpu(oleaf->lf_depth));
	half_len = len >> 1;
	if (!half_len) {
		gfs_consist_inode(dip);
		error = -EIO;
		goto fail_brelse;
	}

	start = (index & ~(len - 1));

	/* Change the pointers.
	   Don't bother distinguishing stuffed from non-stuffed.
	   This code is complicated enough already. */

	lp = kmalloc(half_len * sizeof (uint64_t), GFP_KERNEL);
	if (unlikely(!lp)) {
		lp = vmalloc(half_len * sizeof (uint64_t));
		if (!lp) {
			printk("GFS: dir_split_leaf vmalloc fail - half_len=%d\n", half_len);
			error = -ENOMEM;
			goto fail_brelse;
		} else
			lp_vfree = 1;
	}

	error = gfs_internal_read(dip, (char *)lp, start * sizeof(uint64_t),
				  half_len * sizeof(uint64_t));
	if (error != half_len * sizeof(uint64_t)) {
		if (error >= 0)
			error = -EIO;
		goto fail_lpfree;
	}

	/*  Change the pointers  */

	for (x = 0; x < half_len; x++)
		lp[x] = cpu_to_gfs64(bn);

	error = gfs_internal_write(dip, (char *)lp, start * sizeof(uint64_t),
				   half_len * sizeof(uint64_t));
	if (error != half_len * sizeof(uint64_t)) {
		if (error >= 0)
			error = -EIO;
		goto fail_lpfree;
	}

	if (unlikely(lp_vfree))
		vfree(lp);
	else
		kfree(lp);

	/*  Compute the divider  */

	divider = (start + half_len) << (32 - dip->i_di.di_depth);

	/*  Copy the entries  */

	dirent_first(dip, obh, &dent);

	do {
		next = dent;
		if (dirent_next(dip, obh, &next))
			next = NULL;

		if (dent->de_inum.no_formal_ino &&
		    gfs32_to_cpu(dent->de_hash) < divider) {
			name_len = gfs16_to_cpu(dent->de_name_len);

			gfs_dirent_alloc(dip, nbh, name_len, &new);

			new->de_inum = dent->de_inum; /* No endianness worries */
			new->de_hash = dent->de_hash; /* No endianness worries */
			new->de_type = dent->de_type; /* No endianness worries */
			memcpy((char *)(new + 1), (char *)(dent + 1),
			       name_len);

			nleaf->lf_entries = gfs16_to_cpu(nleaf->lf_entries) + 1;
			nleaf->lf_entries = cpu_to_gfs16(nleaf->lf_entries);

			dirent_del(dip, obh, prev, dent);

			if (!oleaf->lf_entries)
				gfs_consist_inode(dip);
			oleaf->lf_entries = gfs16_to_cpu(oleaf->lf_entries) - 1;
			oleaf->lf_entries = cpu_to_gfs16(oleaf->lf_entries);

			if (!prev)
				prev = dent;

			moved = TRUE;
		} else
			prev = dent;

		dent = next;
	}
	while (dent);

	/* If none of the entries got moved into the new leaf,
	   artificially fill in the first entry. */

	if (!moved) {
		gfs_dirent_alloc(dip, nbh, 0, &new);
		new->de_inum.no_formal_ino = 0;
	}

	oleaf->lf_depth = gfs16_to_cpu(oleaf->lf_depth) + 1;
	oleaf->lf_depth = cpu_to_gfs16(oleaf->lf_depth);
	nleaf->lf_depth = oleaf->lf_depth;

	error = gfs_get_inode_buffer(dip, &dibh);
	if (!gfs_assert_withdraw(dip->i_sbd, !error)) {
		dip->i_di.di_blocks++;
		gfs_dinode_out(&dip->i_di, dibh->b_data);
		brelse(dibh);
	}

	brelse(obh);
	brelse(nbh);

	return error;

 fail_lpfree:
	if (unlikely(lp_vfree))
		vfree(lp);
	else
		kfree(lp);

 fail_brelse:
	brelse(obh);

 fail:
	brelse(nbh);
	return error;
}

/**
 * dir_double_exhash - Double size of ExHash table
 * @dip: The GFS dinode
 *
 * Returns: 0 on success, error code on failure
 */

static int
dir_double_exhash(struct gfs_inode *dip)
{
	struct gfs_sbd *sdp = dip->i_sbd;
	struct buffer_head *dibh;
	uint32_t hsize;
	uint64_t *buf;
	uint64_t *from, *to;
	uint64_t block;
	int x;
	int error = 0;

	hsize = 1 << dip->i_di.di_depth;
	if (hsize * sizeof(uint64_t) != dip->i_di.di_size) {
		gfs_consist_inode(dip);
		return -EIO;
	}

	/*  Allocate both the "from" and "to" buffers in one big chunk  */

	buf = gmalloc(3 * sdp->sd_hash_bsize);

	for (block = dip->i_di.di_size >> sdp->sd_hash_bsize_shift; block--;) {
		error = gfs_internal_read(dip, (char *)buf,
					  block * sdp->sd_hash_bsize,
					  sdp->sd_hash_bsize);
		if (error != sdp->sd_hash_bsize) {
			if (error >= 0)
				error = -EIO;
			goto fail;
		}

		from = buf;
		to = (uint64_t *)((char *)buf + sdp->sd_hash_bsize);

		for (x = sdp->sd_hash_ptrs; x--; from++) {
			*to++ = *from;	/*  No endianess worries  */
			*to++ = *from;
		}

		error = gfs_internal_write(dip, (char *)buf + sdp->sd_hash_bsize,
					   block * sdp->sd_sb.sb_bsize,
					   sdp->sd_sb.sb_bsize);
		if (error != sdp->sd_sb.sb_bsize) {
			if (error >= 0)
				error = -EIO;
			goto fail;
		}
	}

	kfree(buf);

	error = gfs_get_inode_buffer(dip, &dibh);
	if (!gfs_assert_withdraw(sdp, !error)) {
		dip->i_di.di_depth++;
		gfs_dinode_out(&dip->i_di, dibh->b_data);
		brelse(dibh);
	}

	return error;

 fail:
	kfree(buf);

	return error;
}

/**
 * compare_dents - compare directory entries by hash value
 * @a: first dent
 * @b: second dent
 *
 * When comparing the hash entries of @a to @b:
 *   gt: returns 1
 *   lt: returns -1
 *   eq: returns 0
 */

static int
compare_dents(const void *a, const void *b)
{
	struct gfs_dirent *dent_a, *dent_b;
	uint32_t hash_a, hash_b;
	int ret = 0;

	dent_a = *(struct gfs_dirent **)a;
	hash_a = dent_a->de_hash;
	hash_a = gfs32_to_cpu(hash_a);

	dent_b = *(struct gfs_dirent **)b;
	hash_b = dent_b->de_hash;
	hash_b = gfs32_to_cpu(hash_b);

	if (hash_a > hash_b)
		ret = 1;
	else if (hash_a < hash_b)
		ret = -1;
	else {
		unsigned int len_a = gfs16_to_cpu(dent_a->de_name_len);
		unsigned int len_b = gfs16_to_cpu(dent_b->de_name_len);

		if (len_a > len_b)
			ret = 1;
		else if (len_a < len_b)
			ret = -1;
		else
			ret = memcmp((char *)(dent_a + 1),
				     (char *)(dent_b + 1),
				     len_a);
	}

	return ret;
}

/**
 * do_filldir_main - read out directory entries
 * @dip: The GFS inode
 * @offset: The offset in the file to read from
 * @opaque: opaque data to pass to filldir
 * @filldir: The function to pass entries to 
 * @darr: an array of struct gfs_dirent pointers to read
 * @entries: the number of entries in darr
 * @copied: pointer to int that's non-zero if a entry has been copied out
 *
 * Jump through some hoops to make sure that if there are hash collsions,
 * they are read out at the beginning of a buffer.  We want to minimize
 * the possibility that they will fall into different readdir buffers or
 * that someone will want to seek to that location.
 *
 * Returns: errno, >0 on exception from filldir
 */

static int
do_filldir_main(struct gfs_inode *dip, uint64_t *offset,
		void *opaque, gfs_filldir_t filldir,
		struct gfs_dirent **darr, uint32_t entries, int *copied)
{
	struct gfs_dirent *dent, *dent_next;
	struct gfs_inum inum;
	uint64_t off, off_next;
	unsigned int x, y;
	int run = FALSE;
	int error = 0;

	gfs_sort(darr, entries, sizeof(struct gfs_dirent *), compare_dents);

	dent_next = darr[0];
	off_next = gfs32_to_cpu(dent_next->de_hash);
	off_next = gfs_dir_hash2offset(off_next);

	for (x = 0, y = 1; x < entries; x++, y++) {
		dent = dent_next;
		off = off_next;

		if (y < entries) {
			dent_next = darr[y];
			off_next = gfs32_to_cpu(dent_next->de_hash);
			off_next = gfs_dir_hash2offset(off_next);

			if (off < *offset)
				continue;
			*offset = off;

			if (off_next == off) {
				if (*copied && !run)
					return 1;
				run = TRUE;
			} else
				run = FALSE;
		} else {
			if (off < *offset)
				continue;
			*offset = off;
		}

		gfs_inum_in(&inum, (char *)&dent->de_inum);

		error = filldir(opaque, (char *)(dent + 1),
				gfs16_to_cpu(dent->de_name_len),
				off, &inum,
				gfs16_to_cpu(dent->de_type));
		if (error)
			return 1;

		*copied = TRUE;
	}

	/* Increment the *offset by one, so the next time we come into the do_filldir fxn, 
	   we get the next entry instead of the last one in the current leaf */

	(*offset)++;

	return 0;
}

/**
 * do_filldir_single - Read directory entries out of a single block
 * @dip: The GFS inode
 * @offset: The offset in the file to read from
 * @opaque: opaque data to pass to filldir
 * @filldir: The function to pass entries to 
 * @bh: the block
 * @entries: the number of entries in the block
 * @copied: pointer to int that's non-zero if a entry has been copied out
 *
 * Returns: errno, >0 on exception from filldir
 */

static int
do_filldir_single(struct gfs_inode *dip, uint64_t *offset,
		  void *opaque, gfs_filldir_t filldir,
		  struct buffer_head *bh, uint32_t entries, int *copied)
{
	struct gfs_dirent **darr;
	struct gfs_dirent *de;
	unsigned int e = 0;
	int error, do_vfree=0;

	if (!entries)
		return 0;

	darr = kmalloc(entries * sizeof(struct gfs_dirent *), GFP_KERNEL);
	if (unlikely(!darr)) {
		darr = vmalloc(entries * sizeof (struct gfs_dirent *));
		if (!darr) {
			printk("GFS: do_filldir_single vmalloc fails, entries=%d\n", entries);
			return -ENOMEM;
		}
	else
		do_vfree = 1;
	}

	dirent_first(dip, bh, &de);
	do {
		if (!de->de_inum.no_formal_ino)
			continue;
		if (e >= entries) {
			gfs_consist_inode(dip);
			error = -EIO;
			goto out;
		}
		darr[e++] = de;
	}
	while (dirent_next(dip, bh, &de) == 0);

	if (e != entries) {
		gfs_consist_inode(dip);
		error = -EIO;
		goto out;
	}

	error = do_filldir_main(dip, offset, opaque, filldir, darr,
				entries, copied);

 out:
	if (unlikely(do_vfree))
		vfree(darr);
	else
		kfree(darr);

	return error;
}

/**
 * do_filldir_multi - Read directory entries out of a linked leaf list
 * @dip: The GFS inode
 * @offset: The offset in the file to read from
 * @opaque: opaque data to pass to filldir
 * @filldir: The function to pass entries to 
 * @bh: the first leaf in the list
 * @copied: pointer to int that's non-zero if a entry has been copied out
 *
 * Returns: errno, >0 on exception from filldir
 */

static int
do_filldir_multi(struct gfs_inode *dip, uint64_t *offset,
		 void *opaque, gfs_filldir_t filldir,
		 struct buffer_head *bh, int *copied)
{
	struct buffer_head **larr = NULL;
	struct gfs_dirent **darr;
	struct gfs_leaf *leaf;
	struct buffer_head *tmp_bh;
	struct gfs_dirent *de;
	unsigned int entries, e = 0;
	unsigned int leaves = 0, l = 0;
	unsigned int x;
	uint64_t ln;
	int error = 0, leaves_vfree=0, entries_vfree=0;

	/*  Count leaves and entries  */

	leaf = (struct gfs_leaf *)bh->b_data;
	entries = gfs16_to_cpu(leaf->lf_entries);
	ln = leaf->lf_next;

	while (ln) {
		ln = gfs64_to_cpu(ln);

		error = get_leaf(dip, ln, &tmp_bh);
		if (error)
			return error;

		leaf = (struct gfs_leaf *)tmp_bh->b_data;
		if (leaf->lf_entries) {
			entries += gfs16_to_cpu(leaf->lf_entries);
			leaves++;
		}
		ln = leaf->lf_next;

		brelse(tmp_bh);
	}

	/*  Bail out if there's nothing to do  */

	if (!entries)
		return 0;

	/*  Alloc arrays  */

	if (leaves) {
		larr = kmalloc(leaves * sizeof(struct buffer_head *), GFP_KERNEL);
		if (unlikely(!larr)) {
			larr = vmalloc(leaves * sizeof (struct buffer_head *));
			if (!larr) {
				printk("GFS: do_filldir_multi vmalloc fails leaves=%d\n", leaves);
				return -ENOMEM;
			} else
				leaves_vfree = 1;
		}
	}

	darr = kmalloc(entries * sizeof(struct gfs_dirent *), GFP_KERNEL);
	if (unlikely(!darr)) {
		darr = vmalloc(entries * sizeof (struct gfs_dirent *));
		if (!darr) {
			printk("GFS: do_filldir_multi vmalloc fails entries=%d\n", entries);
			if (larr) {
				if (leaves_vfree) 
					vfree(larr);
				else 
					kfree(larr);
			}
			return -ENOMEM;
		} else
			entries_vfree = 1;
	} 
	if (!darr) {
		if (larr)
			kfree(larr);
		return -ENOMEM;
	}

	/*  Fill in arrays  */

	leaf = (struct gfs_leaf *)bh->b_data;
	if (leaf->lf_entries) {
		dirent_first(dip, bh, &de);
		do {
			if (!de->de_inum.no_formal_ino)
				continue;
			if (e >= entries) {
				gfs_consist_inode(dip);
				error = -EIO;
				goto out;
			}
			darr[e++] = de;
		}
		while (dirent_next(dip, bh, &de) == 0);
	}
	ln = leaf->lf_next;

	while (ln) {
		ln = gfs64_to_cpu(ln);

		error = get_leaf(dip, ln, &tmp_bh);
		if (error)
			goto out;

		leaf = (struct gfs_leaf *)tmp_bh->b_data;
		if (leaf->lf_entries) {
			dirent_first(dip, tmp_bh, &de);
			do {
				if (!de->de_inum.no_formal_ino)
					continue;
				if (e >= entries) {
					gfs_consist_inode(dip);
					error = -EIO;
					goto out;
				}
				darr[e++] = de;
			}
			while (dirent_next(dip, tmp_bh, &de) == 0);

			larr[l++] = tmp_bh;

			ln = leaf->lf_next;
		} else {
			ln = leaf->lf_next;
			brelse(tmp_bh);
		}
	}

	if (gfs_assert_withdraw(dip->i_sbd, l == leaves)) {
		error = -EIO;
		goto out;
	}
	if (e != entries) {
		gfs_consist_inode(dip);
		error = -EIO;
		goto out;
	}

	/*  Do work  */

	error = do_filldir_main(dip, offset, opaque, filldir, darr,
				entries, copied);

	/*  Clean up  */

 out:
	if (unlikely(entries_vfree))
		vfree(darr);
	else
		kfree(darr);

	for (x = 0; x < l; x++)
		brelse(larr[x]);

	if (leaves) {
		if (unlikely(leaves_vfree))
			vfree(larr);
		else
			kfree(larr);
	}

	return error;
}

/**
 * dir_e_search - Search exhash (leaf) dir for inode matching name
 * @dip: The GFS inode
 * @filename: Filename string
 * @inode: If non-NULL, function fills with formal inode # and block address
 * @type: If non-NULL, function fills with GFS_FILE_... dinode type
 *
 * Returns:
 */

static int
dir_e_search(struct gfs_inode *dip, struct qstr *filename,
	     struct gfs_inum *inum, unsigned int *type)
{
	struct buffer_head *bh;
	struct gfs_dirent *dent;
	int error;

	error = linked_leaf_search(dip, filename, &dent, NULL, &bh);
	if (error)
		return error;

	if (inum)
		gfs_inum_in(inum, (char *)&dent->de_inum);
	if (type)
		*type = gfs16_to_cpu(dent->de_type);

	brelse(bh);

	return 0;
}

/**
 * dir_e_add -
 * @dip: The GFS inode
 * @filename:
 * @inode:
 * @type:
 *
 */

static int
dir_e_add(struct gfs_inode *dip, struct qstr *filename,
	  struct gfs_inum *inum, unsigned int type)
{
	struct buffer_head *bh, *nbh, *dibh;
	struct gfs_leaf *leaf, *nleaf;
	struct gfs_dirent *dent;
	uint32_t hsize, index;
	uint32_t hash;
	uint64_t leaf_no, bn;
	int error;

 restart:
	hsize = 1 << dip->i_di.di_depth;
	if (hsize * sizeof(uint64_t) != dip->i_di.di_size) {
		gfs_consist_inode(dip);
		return -EIO;
	}

	/*  Figure out the address of the leaf node.  */

	hash = gfs_dir_hash(filename->name, filename->len);
	index = hash >> (32 - dip->i_di.di_depth);

	error = get_leaf_nr(dip, index, &leaf_no);
	if (error)
		return error;

	/*  Add entry to the leaf  */

	for (;;) {
		error = get_leaf(dip, leaf_no, &bh);
		if (error)
			return error;

		leaf = (struct gfs_leaf *)bh->b_data;

		if (gfs_dirent_alloc(dip, bh, filename->len, &dent)) {

			if (gfs16_to_cpu(leaf->lf_depth) < dip->i_di.di_depth) {
				/* Can we split the leaf? */

				brelse(bh);

				error = dir_split_leaf(dip, index, leaf_no);
				if (error)
					return error;

				goto restart;

			} else if (dip->i_di.di_depth < GFS_DIR_MAX_DEPTH) {
				/* Can we double the hash table? */

				brelse(bh);

				error = dir_double_exhash(dip);
				if (error)
					return error;

				goto restart;

			} else if (leaf->lf_next) {
				/* Can we try the next leaf in the list? */
				leaf_no = gfs64_to_cpu(leaf->lf_next);
				brelse(bh);
				continue;

			} else {
				/* Create a new leaf and add it to the list. */

				error = gfs_metaalloc(dip, &bn);
				if (error) {
					brelse(bh);
					return error;
				}

				error = gfs_dread(dip->i_gl, bn,
						  DIO_NEW | DIO_START | DIO_WAIT,
						  &nbh);
				if (error) {
					brelse(bh);
					return error;
				}

				gfs_trans_add_bh(dip->i_gl, nbh);
				gfs_metatype_set(nbh,
						 GFS_METATYPE_LF,
						 GFS_FORMAT_LF);
				gfs_buffer_clear_tail(nbh,
						      sizeof(struct gfs_meta_header));

				gfs_trans_add_bh(dip->i_gl, bh);
				leaf->lf_next = cpu_to_gfs64(bn);

				nleaf = (struct gfs_leaf *)nbh->b_data;
				nleaf->lf_depth = leaf->lf_depth;
				nleaf->lf_dirent_format = cpu_to_gfs32(GFS_FORMAT_DE);

				gfs_dirent_alloc(dip, nbh, filename->len, &dent);

				dip->i_di.di_blocks++;

				brelse(bh);

				bh = nbh;
				leaf = nleaf;
			}
		}

		/*  If the gfs_dirent_alloc() succeeded, it pinned the "bh".  */

		gfs_inum_out(inum, (char *)&dent->de_inum);
		dent->de_hash = cpu_to_gfs32(hash);
		dent->de_type = cpu_to_gfs16(type);
		memcpy((char *)(dent + 1), filename->name, filename->len);

		leaf->lf_entries = gfs16_to_cpu(leaf->lf_entries) + 1;
		leaf->lf_entries = cpu_to_gfs16(leaf->lf_entries);

		brelse(bh);

		error = gfs_get_inode_buffer(dip, &dibh);
		if (error)
			return error;

		dip->i_di.di_entries++;
		dip->i_di.di_mtime = dip->i_di.di_ctime = get_seconds();

		gfs_trans_add_bh(dip->i_gl, dibh);
		gfs_dinode_out(&dip->i_di, dibh->b_data);
		brelse(dibh);

		return 0;
	}

	return -ENOENT;
}

/**
 * dir_e_del - 
 * @dip: The GFS inode
 * @filename:
 *
 * Returns:
 */

static int
dir_e_del(struct gfs_inode *dip, struct qstr *filename)
{
	struct buffer_head *bh, *dibh;
	struct gfs_dirent *dent, *prev;
	struct gfs_leaf *leaf;
	unsigned int entries;
	int error;

	error = linked_leaf_search(dip, filename, &dent, &prev, &bh);
	if (error == -ENOENT) {
		gfs_consist_inode(dip);
		return -EIO;
	}
	if (error)
		return error;

	dirent_del(dip, bh, prev, dent); /* Pins bh */

	leaf = (struct gfs_leaf *)bh->b_data;
	entries = gfs16_to_cpu(leaf->lf_entries);
	if (!entries)
		gfs_consist_inode(dip);
	entries--;
	leaf->lf_entries = cpu_to_gfs16(entries);

	brelse(bh);

	error = gfs_get_inode_buffer(dip, &dibh);
	if (error)
		return error;

	if (!dip->i_di.di_entries)
		gfs_consist_inode(dip);
	dip->i_di.di_entries--;
	dip->i_di.di_mtime = dip->i_di.di_ctime = get_seconds();

	gfs_trans_add_bh(dip->i_gl, dibh);
	gfs_dinode_out(&dip->i_di, dibh->b_data);
	brelse(dibh);

	return 0;
}

/**
 * dir_e_read - Reads the entries from a directory into a filldir buffer 
 * @dip: dinode pointer
 * @offset: the hash of the last entry read shifted to the right once
 * @opaque: buffer for the filldir function to fill 
 * @filldir: points to the filldir function to use
 *
 * Returns: errno
 */

static int
dir_e_read(struct gfs_inode *dip, uint64_t *offset, void *opaque,
	   gfs_filldir_t filldir)
{
	struct gfs_sbd *sdp = dip->i_sbd;
	struct buffer_head *bh;
	struct gfs_leaf leaf;
	uint32_t hsize, len;
	uint32_t ht_offset, lp_offset, ht_offset_cur = -1;
	uint32_t hash, index;
	uint64_t *lp;
	int copied = FALSE;
	int error = 0;

	hsize = 1 << dip->i_di.di_depth;
	if (hsize * sizeof(uint64_t) != dip->i_di.di_size) {
		gfs_consist_inode(dip);
		return -EIO;
	}

	hash = gfs_dir_offset2hash(*offset);
	index = hash >> (32 - dip->i_di.di_depth);

	lp = kmalloc(sdp->sd_hash_bsize, GFP_KERNEL);
	if (!lp)
		return -ENOMEM;

	while (index < hsize) {
		lp_offset = index & (sdp->sd_hash_ptrs - 1);
		ht_offset = index - lp_offset;

		if (ht_offset_cur != ht_offset) {
			error = gfs_internal_read(dip, (char *)lp,
						  ht_offset * sizeof(uint64_t),
						  sdp->sd_hash_bsize);
			if (error != sdp->sd_hash_bsize) {
				if (error >= 0)
					error = -EIO;
				goto out;
			}
			ht_offset_cur = ht_offset;
		}

		error = get_leaf(dip, gfs64_to_cpu(lp[lp_offset]), &bh);
		if (error)
			goto out;

		gfs_leaf_in(&leaf, bh->b_data);

		if (leaf.lf_next)
			error = do_filldir_multi(dip, offset,
						 opaque, filldir,
						 bh, &copied);
		else
			error = do_filldir_single(dip, offset,
						  opaque, filldir,
						  bh, leaf.lf_entries,
						  &copied);

		brelse(bh);

		if (error) {
			if (error > 0)
				error = 0;
			goto out;
		}

		len = 1 << (dip->i_di.di_depth - leaf.lf_depth);
		index = (index & ~(len - 1)) + len;
	}

 out:
	kfree(lp);

	return error;
}

/**
 * dir_e_mvino -
 * @dip: The GFS inode
 * @filename:
 * @new_inode:
 *
 * Returns:
 */

static int
dir_e_mvino(struct gfs_inode *dip, struct qstr *filename,
	    struct gfs_inum *inum, unsigned int new_type)
{
	struct buffer_head *bh, *dibh;
	struct gfs_dirent *dent;
	int error;

	error = linked_leaf_search(dip, filename, &dent, NULL, &bh);
	if (error == -ENOENT) {
		gfs_consist_inode(dip);
		return -EIO;
	}
	if (error)
		return error;

	gfs_trans_add_bh(dip->i_gl, bh);

	gfs_inum_out(inum, (char *)&dent->de_inum);
	dent->de_type = cpu_to_gfs16(new_type);

	brelse(bh);

	error = gfs_get_inode_buffer(dip, &dibh);
	if (error)
		return error;

	dip->i_di.di_mtime = dip->i_di.di_ctime = get_seconds();

	gfs_trans_add_bh(dip->i_gl, dibh);
	gfs_dinode_out(&dip->i_di, dibh->b_data);
	brelse(dibh);

	return 0;
}

/**
 * dir_l_search - Search linear (stuffed dinode) dir for inode matching name
 * @dip: The GFS inode
 * @filename: Filename string
 * @inode: If non-NULL, function fills with formal inode # and block address
 * @type: If non-NULL, function fills with GFS_FILE_... dinode type
 *
 * Returns:
 */

static int
dir_l_search(struct gfs_inode *dip, struct qstr *filename,
	     struct gfs_inum *inum, unsigned int *type)
{
	struct buffer_head *dibh;
	struct gfs_dirent *dent;
	int error;

	if (!gfs_is_stuffed(dip)) {
		gfs_consist_inode(dip);
		return -EIO;
	}

	error = gfs_get_inode_buffer(dip, &dibh);
	if (error)
		return error;

	error = leaf_search(dip, dibh, filename, &dent, NULL);
	if (!error) {
		if (inum)
			gfs_inum_in(inum, (char *)&dent->de_inum);
		if (type)
			*type = gfs16_to_cpu(dent->de_type);
	}

	brelse(dibh);

	return error;
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

static int
dir_l_add(struct gfs_inode *dip, struct qstr *filename,
	  struct gfs_inum *inum, unsigned int type)
{
	struct buffer_head *dibh;
	struct gfs_dirent *dent;
	int error;

	if (!gfs_is_stuffed(dip)) {
		gfs_consist_inode(dip);
		return -EIO;
	}

	error = gfs_get_inode_buffer(dip, &dibh);
	if (error)
		return error;

	if (gfs_dirent_alloc(dip, dibh, filename->len, &dent)) {
		brelse(dibh);

		error = dir_make_exhash(dip);
		if (!error)
			error = dir_e_add(dip, filename, inum, type);

		return error;
	}

	/*  gfs_dirent_alloc() pins  */

	gfs_inum_out(inum, (char *)&dent->de_inum);
	dent->de_hash = gfs_dir_hash(filename->name, filename->len);
	dent->de_hash = cpu_to_gfs32(dent->de_hash);
	dent->de_type = cpu_to_gfs16(type);
	memcpy((char *)(dent + 1), filename->name, filename->len);

	dip->i_di.di_entries++;
	dip->i_di.di_mtime = dip->i_di.di_ctime = get_seconds();

	gfs_dinode_out(&dip->i_di, dibh->b_data);
	brelse(dibh);

	return 0;
}

/**
 * dir_l_del - 
 * @dip: The GFS inode
 * @filename:
 *
 * Returns:
 */

static int
dir_l_del(struct gfs_inode *dip, struct qstr *filename)
{
	struct buffer_head *dibh;
	struct gfs_dirent *dent, *prev;
	int error;

	if (!gfs_is_stuffed(dip)) {
		gfs_consist_inode(dip);
		return -EIO;
	}

	error = gfs_get_inode_buffer(dip, &dibh);
	if (error)
		return error;

	error = leaf_search(dip, dibh, filename, &dent, &prev);
	if (error == -ENOENT) {
		gfs_consist_inode(dip);
		error = -EIO;
		goto out;
	}
	if (error)
		goto out;

	dirent_del(dip, dibh, prev, dent);

	/*  dirent_del() pins  */

	if (!dip->i_di.di_entries)
		gfs_consist_inode(dip);
	dip->i_di.di_entries--;

	dip->i_di.di_mtime = dip->i_di.di_ctime = get_seconds();

	gfs_dinode_out(&dip->i_di, dibh->b_data);

 out:
	brelse(dibh);

	return error;
}

/**
 * dir_l_read -
 * @dip:
 * @offset:
 * @opaque:
 * @filldir:
 *
 * Returns:
 */

static int
dir_l_read(struct gfs_inode *dip, uint64_t *offset, void *opaque,
	   gfs_filldir_t filldir)
{
	struct buffer_head *dibh;
	int copied = FALSE;
	int error;

	if (!gfs_is_stuffed(dip)) {
		gfs_consist_inode(dip);
		return -EIO;
	}

	if (!dip->i_di.di_entries)
		return 0;

	error = gfs_get_inode_buffer(dip, &dibh);
	if (error)
		return error;

	error = do_filldir_single(dip, offset,
				  opaque, filldir,
				  dibh, dip->i_di.di_entries,
				  &copied);
	if (error > 0)
		error = 0;

	brelse(dibh);

	return error;
}

/**
 * dir_l_mvino -
 * @dip:
 * @filename:
 * @new_inode:
 *
 * Returns:
 */

static int
dir_l_mvino(struct gfs_inode *dip, struct qstr *filename,
	    struct gfs_inum *inum, unsigned int new_type)
{
	struct buffer_head *dibh;
	struct gfs_dirent *dent;
	int error;

	if (!gfs_is_stuffed(dip)) {
		gfs_consist_inode(dip);
		return -EIO;
	}

	error = gfs_get_inode_buffer(dip, &dibh);
	if (error)
		return error;

	error = leaf_search(dip, dibh, filename, &dent, NULL);
	if (error == -ENOENT) {
		gfs_consist_inode(dip);
		error = -EIO;
		goto out;
	}
	if (error)
		goto out;

	gfs_trans_add_bh(dip->i_gl, dibh);

	gfs_inum_out(inum, (char *)&dent->de_inum);
	dent->de_type = cpu_to_gfs16(new_type);

	dip->i_di.di_mtime = dip->i_di.di_ctime = get_seconds();

	gfs_dinode_out(&dip->i_di, dibh->b_data);

 out:
	brelse(dibh);

	return error;
}

/**
 * gfs_dir_search - Search a directory
 * @dip: The GFS inode
 * @filename:
 * @inode:
 *
 * This routine searches a directory for a file or another directory.
 * Assumes a glock is held on dip.
 *
 * Returns: errno
 */

int
gfs_dir_search(struct gfs_inode *dip, struct qstr *filename,
	       struct gfs_inum *inum, unsigned int *type)
{
	int error;

	if (dip->i_di.di_flags & GFS_DIF_EXHASH)
		error = dir_e_search(dip, filename, inum, type);
	else
		error = dir_l_search(dip, filename, inum, type);

	return error;
}

/**
 * gfs_dir_add - Add new filename into directory
 * @dip: The GFS inode
 * @filename: The new name
 * @inode: The inode number of the entry
 * @type: The type of the entry
 *
 * Returns: 0 on success, error code on failure
 */

int
gfs_dir_add(struct gfs_inode *dip, struct qstr *filename,
	    struct gfs_inum *inum, unsigned int type)
{
	int error;

	if (dip->i_di.di_flags & GFS_DIF_EXHASH)
		error = dir_e_add(dip, filename, inum, type);
	else
		error = dir_l_add(dip, filename, inum, type);

	return error;
}

/**
 * gfs_dir_del - Delete a directory entry
 * @dip: The GFS inode
 * @filename: The filename
 *
 * Returns: 0 on success, error code on failure
 */

int
gfs_dir_del(struct gfs_inode *dip, struct qstr *filename)
{
	int error;

	if (dip->i_di.di_flags & GFS_DIF_EXHASH)
		error = dir_e_del(dip, filename);
	else
		error = dir_l_del(dip, filename);

	return error;
}

/**
 * gfs_dir_read - Translate a GFS filename
 * @dip: The GFS inode
 * @offset:
 * @opaque:
 * @filldir:
 *
 * Returns: 0 on success, error code otherwise
 */

int
gfs_dir_read(struct gfs_inode *dip, uint64_t * offset, void *opaque,
	     gfs_filldir_t filldir)
{
	int error;

	if (dip->i_di.di_flags & GFS_DIF_EXHASH)
		error = dir_e_read(dip, offset, opaque, filldir);
	else
		error = dir_l_read(dip, offset, opaque, filldir);

	return error;
}

/**
 * gfs_dir_mvino - Change inode number of directory entry
 * @dip: The GFS inode
 * @filename:
 * @new_inode:
 *
 * This routine changes the inode number of a directory entry.  It's used
 * by rename to change ".." when a directory is moved.
 * Assumes a glock is held on dvp.
 *
 * Returns: errno
 */

int
gfs_dir_mvino(struct gfs_inode *dip, struct qstr *filename,
	      struct gfs_inum *inum, unsigned int new_type)
{
	int error;

	if (dip->i_di.di_flags & GFS_DIF_EXHASH)
		error = dir_e_mvino(dip, filename, inum, new_type);
	else
		error = dir_l_mvino(dip, filename, inum, new_type);

	return error;
}

/**
 * foreach_leaf - call a function for each leaf in a directory
 * @dip: the directory
 * @lc: the function to call for each each
 * @data: private data to pass to it
 *
 * Returns: errno
 */

static int
foreach_leaf(struct gfs_inode *dip, leaf_call_t lc, void *data)
{
	struct gfs_sbd *sdp = dip->i_sbd;
	struct buffer_head *bh;
	struct gfs_leaf leaf;
	uint32_t hsize, len;
	uint32_t ht_offset, lp_offset, ht_offset_cur = -1;
	uint32_t index = 0;
	uint64_t *lp;
	uint64_t leaf_no;
	int error = 0;

	hsize = 1 << dip->i_di.di_depth;
	if (hsize * sizeof(uint64_t) != dip->i_di.di_size) {
		gfs_consist_inode(dip);
		return -EIO;
	}

	lp = kmalloc(sdp->sd_hash_bsize, GFP_KERNEL);
	if (!lp)
		return -ENOMEM;

	while (index < hsize) {
		lp_offset = index & (sdp->sd_hash_ptrs - 1);
		ht_offset = index - lp_offset;

		if (ht_offset_cur != ht_offset) {
			error = gfs_internal_read(dip, (char *)lp,
						  ht_offset * sizeof(uint64_t),
						  sdp->sd_hash_bsize);
			if (error != sdp->sd_hash_bsize) {
				if (error >= 0)
					error = -EIO;
				goto out;
			}
			ht_offset_cur = ht_offset;
		}

		leaf_no = gfs64_to_cpu(lp[lp_offset]);
		if (leaf_no) {
			error = get_leaf(dip, leaf_no, &bh);
			if (error)
				goto out;
			gfs_leaf_in(&leaf, bh->b_data);
			brelse(bh);

			len = 1 << (dip->i_di.di_depth - leaf.lf_depth);

			error = lc(dip, index, len, leaf_no, data);
			if (error)
				goto out;

			index = (index & ~(len - 1)) + len;
		} else
			index++;
	}

	if (index != hsize) {
		gfs_consist_inode(dip);
		error = -EIO;
	}

 out:
	kfree(lp);

	return error;
}

/**
 * leaf_free - Deallocate a directory leaf
 * @dip: the directory
 * @index: the hash table offset in the directory
 * @len: the number of pointers to this leaf
 * @leaf_no: the leaf number
 * @data: not used
 *
 * Returns: errno
 */

static int
leaf_free(struct gfs_inode *dip,
	  uint32_t index, uint32_t len,
	  uint64_t leaf_no, void *data)
{
	struct gfs_sbd *sdp = dip->i_sbd;
	struct gfs_leaf tmp_leaf;
	struct gfs_rgrp_list rlist;
	struct buffer_head *bh, *dibh;
	uint64_t blk;
	unsigned int rg_blocks = 0;
	char *ht=0;
	unsigned int x, size = len * sizeof(uint64_t);
	int error;

	memset(&rlist, 0, sizeof(struct gfs_rgrp_list));

	gfs_alloc_get(dip);

	error = gfs_quota_hold_m(dip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
	if (error)
		goto out;

	error = gfs_rindex_hold(sdp, &dip->i_alloc->al_ri_gh);
	if (error)
		goto out_qs;

	/*  Count the number of leaves  */

	for (blk = leaf_no; blk; blk = tmp_leaf.lf_next) {
		error = get_leaf(dip, blk, &bh);
		if (error)
			goto out_rlist;
		gfs_leaf_in(&tmp_leaf, (bh)->b_data);
		brelse(bh);

		gfs_rlist_add(sdp, &rlist, blk);
	}

	gfs_rlist_alloc(&rlist, LM_ST_EXCLUSIVE, 0);

	for (x = 0; x < rlist.rl_rgrps; x++) {
		struct gfs_rgrpd *rgd;
		rgd = get_gl2rgd(rlist.rl_ghs[x].gh_gl);
		rg_blocks += rgd->rd_ri.ri_length;
	}

	error = gfs_glock_nq_m(rlist.rl_rgrps, rlist.rl_ghs);
	if (error)
		goto out_rlist;

	/* Trans may require:
	   All the bitmaps that were reserved.
	   One block for the dinode.
	   All the hash blocks that will be changed.
	   One block for a quota change. */

	error = gfs_trans_begin(sdp,
				rg_blocks + 1 + (DIV_RU(size, sdp->sd_jbsize) + 1),
				1);
	if (error)
		goto out_rg_gunlock;

	for (blk = leaf_no; blk; blk = tmp_leaf.lf_next) {
		error = get_leaf(dip, blk, &bh);
		if (error)
			goto out_end_trans;
		gfs_leaf_in(&tmp_leaf, bh->b_data);
		brelse(bh);

		gfs_metafree(dip, blk, 1);

		if (!dip->i_di.di_blocks)
			gfs_consist_inode(dip);
		dip->i_di.di_blocks--;
	}

	error = gfs_writei(dip, ht, index * sizeof (uint64_t), size, gfs_zero_blocks, NULL);

	if (error != size) {
		if (error >= 0)
			error = -EIO;
		goto out_end_trans;
	}

	error = gfs_get_inode_buffer(dip, &dibh);
	if (error)
		goto out_end_trans;

	gfs_trans_add_bh(dip->i_gl, dibh);
	gfs_dinode_out(&dip->i_di, dibh->b_data);
	brelse(dibh);

 out_end_trans:
	gfs_trans_end(sdp);

 out_rg_gunlock:
	gfs_glock_dq_m(rlist.rl_rgrps, rlist.rl_ghs);

 out_rlist:
	gfs_rlist_free(&rlist);
	gfs_glock_dq_uninit(&dip->i_alloc->al_ri_gh);

 out_qs:
	gfs_quota_unhold_m(dip);

 out:
	gfs_alloc_put(dip);

	return error;
}

/**
 * gfs_dir_exhash_free - free all the leaf blocks in a directory
 * @dip: the directory
 *
 * Dealloc all on-disk directory leaves to FREEMETA state
 * Change on-disk inode type to "regular file"
 *
 * Returns: errno
 */

int
gfs_dir_exhash_free(struct gfs_inode *dip)
{
	struct gfs_sbd *sdp = dip->i_sbd;
	struct buffer_head *bh;
	int error;

	/* Dealloc on-disk leaves to FREEMETA state */
	error = foreach_leaf(dip, leaf_free, NULL);
	if (error)
		return error;

	/*  Make this a regular file in case we crash.
	   (We don't want to free these blocks a second time.)  */

	error = gfs_trans_begin(sdp, 1, 0);
	if (error)
		return error;

	error = gfs_get_inode_buffer(dip, &bh);
	if (!error) {
		gfs_trans_add_bh(dip->i_gl, bh);
		((struct gfs_dinode *)bh->b_data)->di_type = cpu_to_gfs16(GFS_FILE_REG);
		brelse(bh);
	}

	gfs_trans_end(sdp);

	return error;
}

/**
 * gfs_diradd_alloc_required - figure out if an entry addition is going to require an allocation
 * @ip: the file being written to
 * @filname: the filename that's going to be added
 * @alloc_required: the int is set to TRUE if an alloc is required, FALSE otherwise
 *
 * Returns: errno
 */

int
gfs_diradd_alloc_required(struct gfs_inode *dip, struct qstr *filename,
			  int *alloc_required)
{
	struct buffer_head *bh = NULL, *bh_next;
	uint32_t hsize, hash, index;
	int error = 0;

	*alloc_required = FALSE;

	if (dip->i_di.di_flags & GFS_DIF_EXHASH) {
		hsize = 1 << dip->i_di.di_depth;
		if (hsize * sizeof(uint64_t) != dip->i_di.di_size) {
			gfs_consist_inode(dip);
			return -EIO;
		}

		hash = gfs_dir_hash(filename->name, filename->len);
		index = hash >> (32 - dip->i_di.di_depth);

		error = get_first_leaf(dip, index, &bh_next);
		if (error)
			return error;

		do {
			if (bh)
				brelse(bh);

			bh = bh_next;

			if (dirent_fits(dip, bh, filename->len))
				break;

			error = get_next_leaf(dip, bh, &bh_next);
			if (error == -ENOENT) {
				*alloc_required = TRUE;
				error = 0;
				break;
			}
		}
		while (!error);

		brelse(bh);
	} else {
		error = gfs_get_inode_buffer(dip, &bh);
		if (error)
			return error;

		if (!dirent_fits(dip, bh, filename->len))
			*alloc_required = TRUE;

		brelse(bh);
	}

	return error;
}

/**
 * do_gdm - copy out one leaf (or list of leaves)
 * @dip: the directory
 * @index: the hash table offset in the directory
 * @len: the number of pointers to this leaf
 * @leaf_no: the leaf number
 * @data: a pointer to a struct gfs_user_buffer structure
 *
 * Returns: errno
 */

static int
do_gdm(struct gfs_inode *dip,
       uint32_t index, uint32_t len, uint64_t leaf_no,
       void *data)
{
	struct gfs_user_buffer *ub = (struct gfs_user_buffer *)data;
	struct gfs_leaf leaf;
	struct buffer_head *bh;
	uint64_t blk;
	int error = 0;

	for (blk = leaf_no; blk; blk = leaf.lf_next) {
		error = get_leaf(dip, blk, &bh);
		if (error)
			break;

		gfs_leaf_in(&leaf, bh->b_data);

		error = gfs_add_bh_to_ub(ub, bh);

		brelse(bh);

		if (error)
			break;
	}

	return error;
}

/**
 * gfs_get_dir_meta - return all the leaf blocks of a directory
 * @dip: the directory
 * @ub: the structure representing the meta
 *
 * Returns: errno
 */

int
gfs_get_dir_meta(struct gfs_inode *dip, struct gfs_user_buffer *ub)
{
	return foreach_leaf(dip, do_gdm, ub);
}
