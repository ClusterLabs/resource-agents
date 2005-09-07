/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "gfs2_mkfs.h"

struct gfs2_inode *
inode_get(struct gfs2_sbd *sdp, struct buffer_head *bh)
{
	struct gfs2_inode *ip;
	zalloc(ip, sizeof(struct gfs2_inode));
	gfs2_dinode_in(&ip->i_di, bh->b_data);
	ip->i_bh = bh;
	ip->i_sbd = sdp;
	return ip;
}

void
inode_put(struct gfs2_inode *ip)
{
	gfs2_dinode_out(&ip->i_di, ip->i_bh->b_data);
	brelse(ip->i_bh);
	free(ip);
}

static uint64_t
blk_alloc_i(struct gfs2_sbd *sdp, unsigned int type)
{
	osi_list_t *tmp, *head;
	struct rgrp_list *rl = NULL;
	struct gfs2_rindex *ri;
	struct gfs2_rgrp *rg;
	unsigned int block, bn = 0, x = 0, y = 0;
	struct buffer_head *bh;
	unsigned int state;

	for (head = &sdp->rglist, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		rl = osi_list_entry(tmp, struct rgrp_list, list);
		if (rl->rg.rg_free)
			break;
	}

	if (tmp == head)
		die("out of space\n");

	ri = &rl->ri;
	rg = &rl->rg;

	for (block = 0; block < ri->ri_length; block++) {
		bh = bread(sdp, ri->ri_addr + block);
		x = (block) ? sizeof(struct gfs2_meta_header) : sizeof(struct gfs2_rgrp);

		for (; x < sdp->bsize; x++)
			for (y = 0; y < GFS2_NBBY; y++) {
				state = (bh->b_data[x] >> (GFS2_BIT_SIZE * y)) & 0x03;
				if (state == GFS2_BLKST_FREE)
					goto found;
				bn++;
			}

		brelse(bh);
	}

	die("allocation is broken (1): %"PRIu64" %u\n",
	    rl->ri.ri_addr, rl->rg.rg_free);

 found:
	if (bn >= ri->ri_bitbytes * GFS2_NBBY)
		die("allocation is broken (2): %u %u %"PRIu64" %u\n",
		    bn, ri->ri_bitbytes * GFS2_NBBY,
		    rl->ri.ri_addr, rl->rg.rg_free);

	switch (type) {
	case DATA:
	case META:
		state = GFS2_BLKST_USED;
		break;
	case DINODE:
		state = GFS2_BLKST_DINODE;
		rg->rg_dinodes++;
		break;
	default:
		die("bad state\n");
	}

	bh->b_data[x] &= ~(0x03 << (GFS2_BIT_SIZE * y));
	bh->b_data[x] |= state << (GFS2_BIT_SIZE * y);
	rg->rg_free--;

	brelse(bh);

	bh = bread(sdp, ri->ri_addr);
	gfs2_rgrp_out(rg, bh->b_data);
	brelse(bh);

	sdp->blks_alloced++;

	return ri->ri_data0 + bn;
}

uint64_t
data_alloc(struct gfs2_inode *ip)
{
	uint64_t x;
	x = blk_alloc_i(ip->i_sbd, DATA);
	ip->i_di.di_goal_data = x;
	return x;
}

uint64_t
meta_alloc(struct gfs2_inode *ip)
{
	uint64_t x;
	x = blk_alloc_i(ip->i_sbd, META);
	ip->i_di.di_goal_meta = x;
	return x;
}

uint64_t
dinode_alloc(struct gfs2_sbd *sdp)
{
	sdp->dinodes_alloced++;
	return blk_alloc_i(sdp, DINODE);
}

static __inline__ void
buffer_clear_tail(struct buffer_head *bh, int head)
{
	memset(bh->b_data + head, 0, bh->b_size - head);
}

static __inline__ void
buffer_copy_tail(struct buffer_head *to_bh, int to_head,
		 struct buffer_head *from_bh, int from_head)
{
	memcpy(to_bh->b_data + to_head,
	       from_bh->b_data + from_head,
	       from_bh->b_size - from_head);
	memset(to_bh->b_data + to_bh->b_size + to_head - from_head,
	       0,
	       from_head - to_head);
}

static void
unstuff_dinode(struct gfs2_inode *ip)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct buffer_head *bh;
	uint64_t block = 0;
	int journaled = !!(ip->i_di.di_flags & GFS2_DIF_JDATA);

	if (ip->i_di.di_size) {
		if (journaled) {
			block = meta_alloc(ip);
			bh = bget(sdp, block);
			{
				struct gfs2_meta_header mh;
				mh.mh_magic = GFS2_MAGIC;
				mh.mh_type = GFS2_METATYPE_JD;
				mh.mh_blkno = block;
				mh.mh_format = GFS2_FORMAT_JD;
				gfs2_meta_header_out(&mh, bh->b_data);
			}

			buffer_copy_tail(bh, sizeof(struct gfs2_meta_header),
					 ip->i_bh, sizeof(struct gfs2_dinode));

			brelse(bh);
		} else {
			block = data_alloc(ip);
			bh = bget(sdp, block);

			buffer_copy_tail(bh, 0,
					 ip->i_bh, sizeof(struct gfs2_dinode));

			brelse(bh);
		}
	}

	buffer_clear_tail(ip->i_bh, sizeof(struct gfs2_dinode));

	if (ip->i_di.di_size) {
		*(uint64_t *)(ip->i_bh->b_data + sizeof(struct gfs2_dinode)) = cpu_to_le64(block);
		ip->i_di.di_blocks++;
	}

	ip->i_di.di_height = 1;
}

static unsigned int
calc_tree_height(struct gfs2_inode *ip, uint64_t size)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	uint64_t *arr;
	unsigned int max, height;

	if (ip->i_di.di_size > size)
		size = ip->i_di.di_size;

	if (ip->i_di.di_flags & GFS2_DIF_JDATA) {
		arr = sdp->sd_jheightsize;
		max = sdp->sd_max_jheight;
	} else {
		arr = sdp->sd_heightsize;
		max = sdp->sd_max_height;
	}

	for (height = 0; height < max; height++)
		if (arr[height] >= size)
			break;

	return height;
}

static void
build_height(struct gfs2_inode *ip, int height)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct buffer_head *bh;
	uint64_t block = 0, *bp;
	unsigned int x;
	int new_block;

	while (ip->i_di.di_height < height) {
		new_block = FALSE;
		bp = (uint64_t *)(ip->i_bh->b_data + sizeof(struct gfs2_dinode));
		for (x = 0; x < sdp->sd_diptrs; x++, bp++)
			if (*bp) {
				new_block = TRUE;
				break;
			}

		if (new_block) {
			block = meta_alloc(ip);
			bh = bget(sdp, block);
			{
				struct gfs2_meta_header mh;
				mh.mh_magic = GFS2_MAGIC;
				mh.mh_type = GFS2_METATYPE_IN;
				mh.mh_blkno = block;
				mh.mh_format = GFS2_FORMAT_IN;
				gfs2_meta_header_out(&mh, bh->b_data);
			}
			buffer_copy_tail(bh, sizeof(struct gfs2_meta_header),
					 ip->i_bh, sizeof(struct gfs2_dinode));

			brelse(bh);
		}

		buffer_clear_tail(ip->i_bh, sizeof(struct gfs2_dinode));

		if (new_block) {
			*(uint64_t *)(ip->i_bh->b_data + sizeof(struct gfs2_dinode)) = cpu_to_le64(block);
			ip->i_di.di_blocks++;
		}

		ip->i_di.di_height++;
	}
}

struct metapath {
	unsigned int mp_list[GFS2_MAX_META_HEIGHT];
};

static struct metapath *
find_metapath(struct gfs2_inode *ip, uint64_t block)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct metapath *mp;
	uint64_t b = block;
	unsigned int i;

	zalloc(mp, sizeof(struct metapath));

	for (i = ip->i_di.di_height; i--;)
		mp->mp_list[i] = do_div(b, sdp->sd_inptrs);

	return mp;
}

static __inline__ uint64_t *
metapointer(struct buffer_head *bh, unsigned int height, struct metapath *mp)
{
	unsigned int head_size = (height > 0) ?
		sizeof(struct gfs2_meta_header) : sizeof(struct gfs2_dinode);

	return ((uint64_t *)(bh->b_data + head_size)) + mp->mp_list[height];
}

static void
lookup_block(struct gfs2_inode *ip,
	     struct buffer_head *bh, unsigned int height, struct metapath *mp,
	     int create, int *new, uint64_t *block)
{
	uint64_t *ptr = metapointer(bh, height, mp);

	if (*ptr) {
		*block = le64_to_cpu(*ptr);
		return;
	}

	*block = 0;

	if (!create)
		return;

	if (height == ip->i_di.di_height - 1&&
	    !(ip->i_di.di_flags & GFS2_DIF_JDATA))
		*block = data_alloc(ip);
	else
		*block = meta_alloc(ip);

	*ptr = cpu_to_le64(*block);
	ip->i_di.di_blocks++;

	*new = 1;
}

void
block_map(struct gfs2_inode *ip,
	  uint64_t lblock, int *new,
	  uint64_t *dblock, uint32_t *extlen)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct buffer_head *bh;
	struct metapath *mp;
	int create = *new;
	unsigned int bsize;
	unsigned int height;
	unsigned int end_of_metadata;
	unsigned int x;

	*new = 0;
	*dblock = 0;
	if (extlen)
		*extlen = 0;

	if (!ip->i_di.di_height) {
		if (!lblock) {
			*dblock = ip->i_di.di_num.no_addr;
			if (extlen)
				*extlen = 1;
		}
		return;
	}

	bsize = (ip->i_di.di_flags & GFS2_DIF_JDATA) ? sdp->sd_jbsize : sdp->bsize;

	height = calc_tree_height(ip, (lblock + 1) * bsize);
	if (ip->i_di.di_height < height) {
		if (!create)
			return;

		build_height(ip, height);
	}

	mp = find_metapath(ip, lblock);
	end_of_metadata = ip->i_di.di_height - 1;

	bh = bhold(ip->i_bh);

	for (x = 0; x < end_of_metadata; x++) {
		lookup_block(ip, bh, x, mp, create, new, dblock);
		brelse(bh);
		if (!*dblock)
			goto out;

		if (*new) {
			struct gfs2_meta_header mh;
			bh = bget(sdp, *dblock);
			mh.mh_magic = GFS2_MAGIC;
			mh.mh_type = GFS2_METATYPE_IN;
			mh.mh_blkno = *dblock;
			mh.mh_format = GFS2_FORMAT_IN;
			gfs2_meta_header_out(&mh, bh->b_data);
		} else
			bh = bread(sdp, *dblock);
	}

	lookup_block(ip, bh, end_of_metadata, mp, create, new, dblock);

	if (extlen && *dblock) {
		*extlen = 1;

		if (!*new) {
			uint64_t tmp_dblock;
			int tmp_new;
			unsigned int nptrs;

			nptrs = (end_of_metadata) ? sdp->sd_inptrs : sdp->sd_diptrs;

			while (++mp->mp_list[end_of_metadata] < nptrs) {
				lookup_block(ip, bh, end_of_metadata, mp,
					     FALSE, &tmp_new,
					     &tmp_dblock);

				if (*dblock + *extlen != tmp_dblock)
					break;

				(*extlen)++;
			}
		}
	}

	brelse(bh);

 out:
	free(mp);
}

static void
copy2mem(struct buffer_head *bh, void **buf, unsigned int offset,
	 unsigned int size)
{
	char **p = (char **)buf;

	if (bh)
		memcpy(*p, bh->b_data + offset, size);
	else
		memset(*p, 0, size);

	*p += size;
}

int
readi(struct gfs2_inode *ip, void *buf,
      uint64_t offset, unsigned int size)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct buffer_head *bh;
	uint64_t lblock, dblock;
	unsigned int o;
	uint32_t extlen = 0;
	unsigned int amount;
	int not_new = 0;
	int journaled = !!(ip->i_di.di_flags & GFS2_DIF_JDATA);
	int copied = 0;

	if (offset >= ip->i_di.di_size)
		return 0;

	if ((offset + size) > ip->i_di.di_size)
		size = ip->i_di.di_size - offset;

	if (!size)
		return 0;

	if (journaled) {
		lblock = offset;
		o = do_div(lblock, sdp->sd_jbsize);
	} else {
		lblock = offset >> sdp->bsize_shift;
		o = offset & (sdp->bsize - 1);
	}

	if (!ip->i_di.di_height)
		o += sizeof(struct gfs2_dinode);
	else if (journaled)
		o += sizeof(struct gfs2_meta_header);

	while (copied < size) {
		amount = size - copied;
		if (amount > sdp->bsize - o)
			amount = sdp->bsize - o;

		if (!extlen)
			block_map(ip, lblock, &not_new, &dblock, &extlen);

		if (dblock) {
			bh = bread(sdp, dblock);
			dblock++;
			extlen--;
		} else
			bh = NULL;

		copy2mem(bh, &buf, o, amount);
		if (bh)
			brelse(bh);

		copied += amount;
		lblock++;

		o = (journaled) ? sizeof(struct gfs2_meta_header) : 0;
	}

	return copied;
}

static void
copy_from_mem(struct buffer_head *bh, void **buf,
	      unsigned int offset, unsigned int size)
{
	char **p = (char **)buf;

	memcpy(bh->b_data + offset, *p, size);

	*p += size;
}

int
writei(struct gfs2_inode *ip, void *buf,
       uint64_t offset, unsigned int size)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct buffer_head *bh;
	uint64_t lblock, dblock;
	unsigned int o;
	uint32_t extlen = 0;
	unsigned int amount;
	int new;
	int journaled = !!(ip->i_di.di_flags & GFS2_DIF_JDATA);
	const uint64_t start = offset;
	int copied = 0;

	if (!size)
		return 0;

	if (!ip->i_di.di_height &&
	    ((start + size) > (sdp->bsize - sizeof(struct gfs2_dinode))))
		unstuff_dinode(ip);

	if (journaled) {
		lblock = offset;
		o = do_div(lblock, sdp->sd_jbsize);
	} else {
		lblock = offset >> sdp->bsize_shift;
		o = offset & (sdp->bsize - 1);
	}

	if (!ip->i_di.di_height)
		o += sizeof(struct gfs2_dinode);
	else if (journaled)
		o += sizeof(struct gfs2_meta_header);

	while (copied < size) {
		amount = size - copied;
		if (amount > sdp->bsize - o)
			amount = sdp->bsize - o;

		if (!extlen) {
			new = TRUE;
			block_map(ip, lblock, &new, &dblock, &extlen);
		}

		if (new) {
			bh = bget(sdp, dblock);
			if (journaled) {
				struct gfs2_meta_header mh;
				mh.mh_magic = GFS2_MAGIC;
				mh.mh_type = GFS2_METATYPE_JD;
				mh.mh_blkno = dblock;
				mh.mh_format = GFS2_FORMAT_JD;
				gfs2_meta_header_out(&mh, bh->b_data);
			}
		} else
			bh = bread(sdp, dblock);
		copy_from_mem(bh, &buf, o, amount);
		brelse(bh);

		copied += amount;
		lblock++;
		dblock++;
		extlen--;

		o = (journaled) ? sizeof(struct gfs2_meta_header) : 0;
	}

	if (ip->i_di.di_size < start + copied)
		ip->i_di.di_size = start + copied;

	return copied;
}

struct buffer_head *
get_file_buf(struct gfs2_inode *ip, uint64_t lbn)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	uint64_t dbn;
	int new = TRUE;

	if (!ip->i_di.di_height)
		unstuff_dinode(ip);

	block_map(ip, lbn, &new, &dbn, NULL);
	if (!dbn)
		die("get_file_buf\n");

	if (new &&
	    ip->i_di.di_size < (lbn + 1) << sdp->bsize_shift)
		ip->i_di.di_size = (lbn + 1) << sdp->bsize_shift;

	if (new)
		return bget(sdp, dbn);
	else
		return bread(sdp, dbn);
}

#define IS_LEAF     (1)
#define IS_DINODE   (2)

static int
dirent_first(struct gfs2_inode *dip, struct buffer_head *bh,
	     struct gfs2_dirent **dent)
{
	struct gfs2_meta_header *h = (struct gfs2_meta_header *)bh->b_data;

	if (le32_to_cpu(h->mh_type) == GFS2_METATYPE_LF) {
		*dent = (struct gfs2_dirent *)(bh->b_data + sizeof(struct gfs2_leaf));
		return IS_LEAF;
	} else {
		*dent = (struct gfs2_dirent *)(bh->b_data + sizeof(struct gfs2_dinode));
		return IS_DINODE;
	}
}

static int
dirent_next(struct gfs2_inode *dip, struct buffer_head *bh,
	    struct gfs2_dirent **dent)
{
	char *bh_end;
	uint32_t cur_rec_len;

	bh_end = bh->b_data + bh->b_size;
	cur_rec_len = le32_to_cpu((*dent)->de_rec_len);

	if ((char *)(*dent) + cur_rec_len >= bh_end)
		return -ENOENT;

	*dent = (struct gfs2_dirent *)((char *)(*dent) + cur_rec_len);

	return 0;
}

static int
dirent_alloc(struct gfs2_inode *dip, struct buffer_head *bh, int name_len,
	     struct gfs2_dirent **dent_out)
{
	struct gfs2_dirent *dent, *new;
	unsigned int rec_len = GFS2_DIRENT_SIZE(name_len);
	unsigned int entries = 0, offset = 0;
	int type;

	type = dirent_first(dip, bh, &dent);

	if (type == IS_LEAF) {
		struct gfs2_leaf *leaf = (struct gfs2_leaf *)bh->b_data;
		entries = le16_to_cpu(leaf->lf_entries);
		offset = sizeof(struct gfs2_leaf);
	} else {
		struct gfs2_dinode *dinode = (struct gfs2_dinode *)bh->b_data;
		entries = le32_to_cpu(dinode->di_entries);
		offset = sizeof(struct gfs2_dinode);
	}

	if (!entries) {
		dent->de_rec_len = bh->b_size - offset;
		dent->de_rec_len = cpu_to_le32(dent->de_rec_len);
		dent->de_name_len = name_len;

		*dent_out = dent;
		return 0;
	}

	do {
		uint32_t cur_rec_len, cur_name_len;

		cur_rec_len = le32_to_cpu(dent->de_rec_len);
		cur_name_len = dent->de_name_len;

		if ((!dent->de_inum.no_formal_ino && cur_rec_len >= rec_len) ||
		    (cur_rec_len >= GFS2_DIRENT_SIZE(cur_name_len) + rec_len)) {

			if (dent->de_inum.no_formal_ino) {
				new = (struct gfs2_dirent *)((char *)dent +
							    GFS2_DIRENT_SIZE(cur_name_len));
				memset(new, 0, sizeof(struct gfs2_dirent));

				new->de_rec_len = cur_rec_len - GFS2_DIRENT_SIZE(cur_name_len);
				new->de_rec_len = cpu_to_le32(new->de_rec_len);
				new->de_name_len = name_len;

				dent->de_rec_len = cur_rec_len - le32_to_cpu(new->de_rec_len);
				dent->de_rec_len = cpu_to_le32(dent->de_rec_len);

				*dent_out = new;
				return 0;
			}

			dent->de_name_len = name_len;

			*dent_out = dent;
			return 0;
		}
	} while (dirent_next(dip, bh, &dent) == 0);

	return -ENOSPC;
}

static void
dirent_del(struct gfs2_inode *dip, struct buffer_head *bh,
	   struct gfs2_dirent *prev, struct gfs2_dirent *cur)
{
	uint32_t cur_rec_len, prev_rec_len;

	if (!prev) {
		cur->de_inum.no_formal_ino = 0;
		return;
	}

	prev_rec_len = le32_to_cpu(prev->de_rec_len);
	cur_rec_len = le32_to_cpu(cur->de_rec_len);

	prev_rec_len += cur_rec_len;
	prev->de_rec_len = cpu_to_le32(prev_rec_len);
}

static void
get_leaf_nr(struct gfs2_inode *dip, uint32_t index, uint64_t *leaf_out)
{
	uint64_t leaf_no;
	int count;

	count = readi(dip, (char *)&leaf_no,
		      index * sizeof(uint64_t),
		      sizeof(uint64_t));
	if (count != sizeof(uint64_t))
		die("get_leaf_nr\n");

	*leaf_out = le64_to_cpu(leaf_no);
}

static void
dir_split_leaf(struct gfs2_inode *dip, uint32_t index, uint64_t leaf_no)
{
	struct buffer_head *nbh, *obh;
	struct gfs2_leaf *nleaf, *oleaf;
	struct gfs2_dirent *dent, *prev = NULL, *next = NULL, *new;
	uint32_t start, len, half_len, divider;
	uint64_t bn, *lp;
	uint32_t name_len;
	int x, moved = FALSE;
	int count;

	bn = meta_alloc(dip);
	nbh = bget(dip->i_sbd, bn);
	{
		struct gfs2_meta_header mh;
		mh.mh_magic = GFS2_MAGIC;
		mh.mh_type = GFS2_METATYPE_LF;
		mh.mh_blkno = bn;
		mh.mh_format = GFS2_FORMAT_LF;
		gfs2_meta_header_out(&mh, nbh->b_data);
	}

	nleaf = (struct gfs2_leaf *)nbh->b_data;
	nleaf->lf_dirent_format = cpu_to_le32(GFS2_FORMAT_DE);

	obh = bread(dip->i_sbd, leaf_no);
	oleaf = (struct gfs2_leaf *)obh->b_data;

	len = 1 << (dip->i_di.di_depth - le16_to_cpu(oleaf->lf_depth));
	half_len = len >> 1;

	start = (index & ~(len - 1));

	zalloc(lp, half_len * sizeof(uint64_t));

	count = readi(dip, (char *)lp, start * sizeof(uint64_t),
		      half_len * sizeof(uint64_t));
	if (count != half_len * sizeof(uint64_t))
		die("dir_split_leaf (1)\n");

	for (x = 0; x < half_len; x++)
		lp[x] = cpu_to_le64(bn);

	count = writei(dip, (char *)lp, start * sizeof(uint64_t),
		       half_len * sizeof(uint64_t));
	if (count != half_len * sizeof(uint64_t))
		die("dir_split_leaf (2)\n");

	free(lp);

	divider = (start + half_len) << (32 - dip->i_di.di_depth);

	dirent_first(dip, obh, &dent);

	do {
		next = dent;
		if (dirent_next(dip, obh, &next))
			next = NULL;

		if (dent->de_inum.no_formal_ino &&
		    le32_to_cpu(dent->de_hash) < divider) {
			name_len = dent->de_name_len;

			dirent_alloc(dip, nbh, name_len, &new);

			new->de_inum = dent->de_inum;
			new->de_hash = dent->de_hash;
			new->de_type = dent->de_type;
			memcpy((char *)(new + 1), (char *)(dent + 1),
			       name_len);

			nleaf->lf_entries = le16_to_cpu(nleaf->lf_entries) + 1;
			nleaf->lf_entries = cpu_to_le16(nleaf->lf_entries);

			dirent_del(dip, obh, prev, dent);

			oleaf->lf_entries = le16_to_cpu(oleaf->lf_entries) - 1;
			oleaf->lf_entries = cpu_to_le16(oleaf->lf_entries);

			if (!prev)
				prev = dent;

			moved = TRUE;
		} else
			prev = dent;

		dent = next;
	} while (dent);

	if (!moved) {
		dirent_alloc(dip, nbh, 0, &new);
		new->de_inum.no_formal_ino = 0;
	}

	oleaf->lf_depth = le16_to_cpu(oleaf->lf_depth) + 1;
	oleaf->lf_depth = cpu_to_le16(oleaf->lf_depth);
	nleaf->lf_depth = oleaf->lf_depth;

	dip->i_di.di_blocks++;

	brelse(obh);
	brelse(nbh);
}

static void
dir_double_exhash(struct gfs2_inode *dip)
{
	struct gfs2_sbd *sdp = dip->i_sbd;
	uint64_t *buf;
	uint64_t *from, *to;
	uint64_t block;
	int x;
	int count;

	zalloc(buf, 3 * sdp->sd_hash_bsize);

	for (block = dip->i_di.di_size >> sdp->sd_hash_bsize_shift; block--;) {
		count = readi(dip, (char *)buf,
			      block * sdp->sd_hash_bsize,
			      sdp->sd_hash_bsize);
		if (count != sdp->sd_hash_bsize)
			die("dir_double_exhash (1)\n");

		from = buf;
		to = (uint64_t *)((char *)buf + sdp->sd_hash_bsize);

		for (x = sdp->sd_hash_ptrs; x--; from++) {
			*to++ = *from;
			*to++ = *from;
		}

		count = writei(dip, (char *)buf + sdp->sd_hash_bsize,
			       block * sdp->bsize,
			       sdp->bsize);
		if (count != sdp->bsize)
			die("dir_double_exhash (2)\n");

	}

	free(buf);

	dip->i_di.di_depth++;
}

static void
dir_e_add(struct gfs2_inode *dip,
	  char *filename, struct gfs2_inum *inum, unsigned int type)
{
	struct buffer_head *bh, *nbh;
	struct gfs2_leaf *leaf, *nleaf;
	struct gfs2_dirent *dent;
	uint32_t index;
	uint32_t hash;
	uint64_t leaf_no, bn;

 restart:
	hash = gfs2_disk_hash(filename, strlen(filename));
	index = hash >> (32 - dip->i_di.di_depth);

	get_leaf_nr(dip, index, &leaf_no);

	for (;;) {
		bh = bread(dip->i_sbd, leaf_no);
		leaf = (struct gfs2_leaf *)bh->b_data;

		if (dirent_alloc(dip, bh, strlen(filename), &dent)) {

			if (le16_to_cpu(leaf->lf_depth) < dip->i_di.di_depth) {
				brelse(bh);
				dir_split_leaf(dip, index, leaf_no);
				goto restart;

			} else if (dip->i_di.di_depth < GFS2_DIR_MAX_DEPTH) {
				brelse(bh);
				dir_double_exhash(dip);
				goto restart;

			} else if (leaf->lf_next) {
				leaf_no = le64_to_cpu(leaf->lf_next);
				brelse(bh);
				continue;

			} else {
				bn = meta_alloc(dip);
				nbh = bget(dip->i_sbd, bn);
				{
					struct gfs2_meta_header mh;
					mh.mh_magic = GFS2_MAGIC;
					mh.mh_type = GFS2_METATYPE_LF;
					mh.mh_blkno = bn;
					mh.mh_format = GFS2_FORMAT_LF;
					gfs2_meta_header_out(&mh, nbh->b_data);
				}

				leaf->lf_next = cpu_to_le64(bn);

				nleaf = (struct gfs2_leaf *)nbh->b_data;
				nleaf->lf_depth = leaf->lf_depth;
				nleaf->lf_dirent_format = cpu_to_le32(GFS2_FORMAT_DE);

				dirent_alloc(dip, nbh, strlen(filename), &dent);

				dip->i_di.di_blocks++;

				brelse(bh);

				bh = nbh;
				leaf = nleaf;
			}
		}

		gfs2_inum_out(inum, (char *)&dent->de_inum);
		dent->de_hash = cpu_to_le32(hash);
		dent->de_type = type;
		memcpy((char *)(dent + 1), filename, strlen(filename));

		leaf->lf_entries = le16_to_cpu(leaf->lf_entries) + 1;
		leaf->lf_entries = cpu_to_le16(leaf->lf_entries);

		brelse(bh);

		dip->i_di.di_entries++;

		return;
	}
}

static void
dir_make_exhash(struct gfs2_inode *dip)
{
	struct gfs2_sbd *sdp = dip->i_sbd;
	struct gfs2_dirent *dent;
	struct buffer_head *bh;
	struct gfs2_leaf *leaf;
	int y;
	uint32_t x;
	uint64_t *lp, bn;

	bn = meta_alloc(dip);
	bh = bget(sdp, bn);
	{
		struct gfs2_meta_header mh;
		mh.mh_magic = GFS2_MAGIC;
		mh.mh_type = GFS2_METATYPE_LF;
		mh.mh_blkno = bn;
		mh.mh_format = GFS2_FORMAT_LF;
		gfs2_meta_header_out(&mh, bh->b_data);
	}

	leaf = (struct gfs2_leaf *)bh->b_data;
	leaf->lf_dirent_format = cpu_to_le32(GFS2_FORMAT_DE);
	leaf->lf_entries = cpu_to_le16(dip->i_di.di_entries);

	buffer_copy_tail(bh, sizeof(struct gfs2_leaf),
			 dip->i_bh, sizeof(struct gfs2_dinode));

	x = 0;
	dirent_first(dip, bh, &dent);

	do {
		if (!dent->de_inum.no_formal_ino)
			continue;
		if (++x == dip->i_di.di_entries)
			break;
	} while (dirent_next(dip, bh, &dent) == 0);

	dent->de_rec_len = le32_to_cpu(dent->de_rec_len) +
		sizeof(struct gfs2_dinode) -
		sizeof(struct gfs2_leaf);
	dent->de_rec_len = cpu_to_le32(dent->de_rec_len);

	brelse(bh);

	buffer_clear_tail(dip->i_bh, sizeof(struct gfs2_dinode));

	lp = (uint64_t *)(dip->i_bh->b_data + sizeof(struct gfs2_dinode));

	for (x = sdp->sd_hash_ptrs; x--; lp++)
		*lp = cpu_to_le64(bn);

	dip->i_di.di_size = sdp->bsize / 2;
	dip->i_di.di_blocks++;
	dip->i_di.di_flags |= GFS2_DIF_EXHASH;
	dip->i_di.di_payload_format = 0;

	for (x = sdp->sd_hash_ptrs, y = -1; x; x >>= 1, y++) ;
	dip->i_di.di_depth = y;
}

static void
dir_l_add(struct gfs2_inode *dip,
	  char *filename, struct gfs2_inum *inum, unsigned int type)
{
	struct gfs2_dirent *dent;

	if (dirent_alloc(dip, dip->i_bh, strlen(filename), &dent)) {
		dir_make_exhash(dip);
		dir_e_add(dip, filename, inum, type);
		return;
	}

	gfs2_inum_out(inum, (char *)&dent->de_inum);
	dent->de_hash = gfs2_disk_hash(filename, strlen(filename));
	dent->de_hash = cpu_to_le32(dent->de_hash);
	dent->de_type = type;
	memcpy((char *)(dent + 1), filename, strlen(filename));

	dip->i_di.di_entries++;
}

static void
dir_add(struct gfs2_inode *dip,
	char *filename, struct gfs2_inum *inum, unsigned int type)
{
	if (dip->i_di.di_flags & GFS2_DIF_EXHASH)
		dir_e_add(dip, filename, inum, type);
	else
		dir_l_add(dip, filename, inum, type);
}

struct buffer_head *
init_dinode(struct gfs2_sbd *sdp, struct gfs2_inum *inum,
	    unsigned int mode, uint32_t flags,
	    struct gfs2_inum *parent)
{
	struct buffer_head *bh;
	struct gfs2_dinode di;

	bh = bget(sdp, inum->no_addr);

	memset(&di, 0, sizeof(struct gfs2_dinode));
	di.di_header.mh_magic = GFS2_MAGIC;
	di.di_header.mh_type = GFS2_METATYPE_DI;
	di.di_header.mh_blkno = inum->no_addr;
	di.di_header.mh_format = GFS2_FORMAT_DI;
	di.di_num = *inum;
	di.di_mode = mode;
	di.di_nlink = 1;
	di.di_blocks = 1;
	di.di_atime = di.di_mtime = di.di_ctime = sdp->time;
	di.di_goal_meta = di.di_goal_data = bh->b_blocknr;
	di.di_flags = flags;

	if (S_ISDIR(mode)) {
		struct gfs2_dirent de1, de2;

		memset(&de1, 0, sizeof(struct gfs2_dirent));
		de1.de_inum = di.di_num;
		de1.de_hash = gfs2_disk_hash(".", 1);
		de1.de_rec_len = GFS2_DIRENT_SIZE(1);
		de1.de_name_len = 1;
		de1.de_type = IF2DT(S_IFDIR);

		memset(&de2, 0, sizeof(struct gfs2_dirent));
		de2.de_inum = *parent;
		de2.de_hash = gfs2_disk_hash("..", 2);
		de2.de_rec_len = sdp->bsize - sizeof(struct gfs2_dinode) - de1.de_rec_len;
		de2.de_name_len = 2;
		de2.de_type = IF2DT(S_IFDIR);

		gfs2_dirent_out(&de1, bh->b_data + sizeof(struct gfs2_dinode));
		memcpy(bh->b_data +
		       sizeof(struct gfs2_dinode) +
		       sizeof(struct gfs2_dirent),
		       ".", 1);
		gfs2_dirent_out(&de2, bh->b_data + sizeof(struct gfs2_dinode) + de1.de_rec_len);
		memcpy(bh->b_data +
		       sizeof(struct gfs2_dinode) +
		       de1.de_rec_len +
		       sizeof(struct gfs2_dirent),
		       "..", 2);

		di.di_nlink = 2;
		di.di_size = sdp->bsize - sizeof(struct gfs2_dinode);
		di.di_flags |= GFS2_DIF_JDATA;
		di.di_payload_format = GFS2_FORMAT_DE;
		di.di_entries = 2;
	}

	gfs2_dinode_out(&di, bh->b_data);

	return bh;
}

struct gfs2_inode *
createi(struct gfs2_inode *dip, char *filename,
	unsigned int mode, uint32_t flags)
{
	struct gfs2_sbd *sdp = dip->i_sbd;
	uint64_t bn;
	struct gfs2_inum inum;
	struct buffer_head *bh;

	bn = dinode_alloc(sdp);

	inum.no_formal_ino = sdp->next_inum++;
	inum.no_addr = bn;

	dir_add(dip, filename, &inum, IF2DT(mode));

	bh = init_dinode(sdp, &inum, mode, flags,
			 &dip->i_di.di_num);

	return inode_get(sdp, bh);
}

