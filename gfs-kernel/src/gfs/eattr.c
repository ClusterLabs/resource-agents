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

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <asm/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <asm/uaccess.h>
#include <linux/xattr_acl.h>

#include "gfs.h"
#include "acl.h"
#include "dio.h"
#include "eattr.h"
#include "glock.h"
#include "inode.h"
#include "ioctl.h"
#include "quota.h"
#include "rgrp.h"
#include "trans.h"

#define GFS_EA_REC_LEN(x) gfs32_to_cpu((x)->ea_rec_len)
#define GFS_EA_NAME(x) ((char *)(x) + sizeof(struct gfs_ea_header))
#define GFS_EA_DATA_PTRS(x) ((uint64_t *)((char *)(x) + sizeof(struct gfs_ea_header) + (((x)->ea_name_len + 7) & ~7)))

#define GFS_EA_NEXT(x) (struct gfs_ea_header *)((char *)(x) + GFS_EA_REC_LEN(x))
#define GFS_EA_FREESPACE(x) (struct gfs_ea_header *)((char *)(x) + GFS_EA_SIZE(x))

#define GFS_EAREQ_IS_STUFFED(x, y) (((sizeof(struct gfs_ea_header) + (x)->es_data_len + (x)->es_name_len + 7) & ~7) <= y)

#define GFS_EADATA_NUM_PTRS(x, y) (((x) + (y) - 1) / (y))

#define GFS_EA_SIZE(x) ((sizeof(struct gfs_ea_header) + (x)->ea_name_len + (GFS_EA_IS_UNSTUFFED(x)? (8 * (x)->ea_num_ptrs) : GFS_EA_DATA_LEN(x)) + 7) & ~ 7)

#define GFS_EACMD_VALID(x) ((x) <= GFS_EACMD_REMOVE)

#define GFS_EA_IS_LAST(x) ((x)->ea_flags & GFS_EAFLAG_LAST)

#define GFS_EA_STRLEN(x) ((x)->ea_name_len + 1 + (((x)->ea_type == GFS_EATYPE_USR)? 5 : 7))

#define GFS_FIRST_EA(x) ((struct gfs_ea_header *) ((x)->b_data + sizeof(struct gfs_meta_header)))

#define EA_ALLOC 1
#define EA_DEALLOC 2

static struct buffer_head *alloc_eattr_blk(struct gfs_sbd *sdp,
					   struct gfs_inode *alloc_ip,
					   struct gfs_inode *ip,
					   uint64_t * block);

/**
 * can_replace - returns true if ea is large enough to hold the data in
 *               the request
 */

static __inline__ int
can_replace(struct gfs_ea_header *ea, struct gfs_easet_io *req,
	    uint32_t avail_size)
{
	int data_space =
	    GFS_EA_REC_LEN(ea) - sizeof (struct gfs_ea_header) -
	    ea->ea_name_len;

	if (GFS_EAREQ_IS_STUFFED(req, avail_size) && !GFS_EA_IS_UNSTUFFED(ea))
		return (req->es_data_len <= data_space);
	else
		return (GFS_EADATA_NUM_PTRS(req->es_data_len, avail_size) <=
			ea->ea_num_ptrs);
}

/**
 * get_req_size - returns the acutal number of bytes the request will take up
 *                (not counting any unstuffed data blocks)
 */

static __inline__ uint32_t
get_req_size(struct gfs_easet_io *req, uint32_t avail_size)
{
	uint32_t size =
	    ((sizeof (struct gfs_ea_header) + req->es_data_len +
	      req->es_name_len + 7) & ~7);

	if (size <= avail_size)
		return size;

	return ((sizeof (struct gfs_ea_header) + req->es_name_len + 7) & ~7) +
	    (8 * GFS_EADATA_NUM_PTRS(req->es_data_len, avail_size));
}

/**
 * gfs_ea_write_permission - decides if the user has permission to write to 
 *                           the ea
 * @req: the write request
 * @ip: inode of file with the ea
 *
 * Returns: 0 on success, -EXXX on error
 */

int
gfs_ea_write_permission(struct gfs_easet_io *req, struct gfs_inode *ip)
{
	struct inode *inode = gfs_iget(ip, NO_CREATE);
	int error = 0;

	GFS_ASSERT_INODE(inode, ip,);

	if (req->es_type == GFS_EATYPE_USR) {
		if (!S_ISREG(inode->i_mode) &&
		    (!S_ISDIR(inode->i_mode) || inode->i_mode & S_ISVTX))
			error = -EPERM;	
		else {
			error = permission(inode, MAY_WRITE, NULL);
			if (error == -EACCES)
				error = -EPERM;
		}
	} else if (req->es_type == GFS_EATYPE_SYS) {
		if (IS_ACCESS_ACL(req->es_name, req->es_name_len))
			error = gfs_validate_acl(ip, req->es_data,
					req->es_data_len, 1);
		else if (IS_DEFAULT_ACL(req->es_name, req->es_name_len))
			error = gfs_validate_acl(ip, req->es_data, 
					req->es_data_len, 0);
		else {
			if (!capable(CAP_SYS_ADMIN))
				error = -EPERM;
		}
	} else
		error = -EOPNOTSUPP;

	iput(inode);

	return error;
}

/**
 * gfs_ea_read_permission - decides if the user has permission to read from 
 *                          the ea
 * @req: the read request
 * @ip: inode of file with the ea
 *
 * Returns: 0 on success, -EXXX on error
 */

int
gfs_ea_read_permission(struct gfs_eaget_io *req, struct gfs_inode *ip)
{
	struct inode *inode = gfs_iget(ip, NO_CREATE);
	int error = 0;

	GFS_ASSERT_INODE(inode, ip,);

	if (req->eg_type == GFS_EATYPE_USR){ 
		error = permission(inode, MAY_READ, NULL);
		if (error == -EACCES)
			error = -EPERM;
	}
	else if (req->eg_type == GFS_EATYPE_SYS) {
		if (IS_ACCESS_ACL(req->eg_name, req->eg_name_len) ||
		    IS_DEFAULT_ACL(req->eg_name, req->eg_name_len))
			error = 0;
		else{
			if (!capable(CAP_SYS_ADMIN))
				error = -EPERM;
		}
	} else
		error = -EOPNOTSUPP;

	iput(inode);

	return error;
}

/**
 * gfs_es_memcpy - gfs memcpy wrapper with a return value
 *
 */

int
gfs_ea_memcpy(void *dest, void *src, unsigned long size)
{
	memcpy(dest, src, size);
	return 0;
}

/**
 * gfs_ea_copy_to_user - copy_to_user wrapper
 */

int
gfs_ea_copy_to_user(void *dest, void *src, unsigned long size)
{
	int error;
	error = (copy_to_user(dest, src, size)) ? -EFAULT : 0;
	return error;
}

/**
 * Returns: 1 if find_direct_eattr should stop checking (if the eattr was found
 *                                                location will be set)
 *          0 if find_eattr should keep on checking
 *          -EXXX on error
 */
int
find_direct_eattr(struct gfs_inode *ip, uint64_t blkno, char *name,
		  int name_len, int type, struct gfs_ea_location *location)
{
	int err;
	struct buffer_head *bh;
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_ea_header *curr, *prev = NULL;

	err = gfs_dread(sdp, blkno, ip->i_gl, DIO_START | DIO_WAIT, &bh);
	if (err)
		goto out;
	gfs_metatype_check(sdp, bh, GFS_METATYPE_EA);
	curr =
	    (struct gfs_ea_header *) ((bh)->b_data +
				      sizeof (struct gfs_meta_header));
	if (curr->ea_type == GFS_EATYPE_UNUSED) {
		if (GFS_EA_IS_LAST(curr))
			goto out_drelse;
		GFS_ASSERT_INODE(GFS_EA_REC_LEN(curr), ip,);
		prev = curr;
		curr = GFS_EA_NEXT(curr);
	}
	if (type != curr->ea_type && ip->i_di.di_flags & GFS_DIF_EA_INDIRECT) {
		if (type == GFS_EATYPE_SYS)
			err = 1;
		goto out_drelse;
	}
	while (1) {
		GFS_ASSERT_INODE(GFS_EA_REC_LEN(curr), ip,);

		if (type == curr->ea_type && name_len == curr->ea_name_len &&
		    !memcmp(name, GFS_EA_NAME(curr), name_len)) {
			location->bh = bh;
			location->ea = curr;
			location->prev = prev;
			err = 1;
			goto out;
		}
		if (GFS_EA_IS_LAST(curr))
			break;
		prev = curr;
		curr = GFS_EA_NEXT(curr);
	}

      out_drelse:
	brelse(bh);

      out:
	return err;
}

/**
 * find_eattr - find a matching eattr
 *
 * Returns: 1 if ea found, 0 if no ea found, -EXXX on error
 */
int
find_eattr(struct gfs_inode *ip, char *name, int name_len, int type,
	   struct gfs_ea_location *location)
{
	int err;
	struct buffer_head *bh;
	struct gfs_sbd *sdp = ip->i_sbd;
	uint64_t *eablk, *end;

	memset(location, 0, sizeof (struct gfs_ea_location));

	if (ip->i_di.di_flags & GFS_DIF_EA_INDIRECT) {
		err =
		    gfs_dread(sdp, ip->i_di.di_eattr, ip->i_gl,
			      DIO_START | DIO_WAIT, &bh);
		if (err)
			goto fail;
		gfs_metatype_check(sdp, bh, GFS_METATYPE_IN);
		eablk =
		    (uint64_t *) ((bh)->b_data + sizeof (struct gfs_indirect));
		end =
		    eablk +
		    ((sdp->sd_sb.sb_bsize - sizeof (struct gfs_indirect)) / 8);
		while (eablk < end && *eablk) {
			err =
			    find_direct_eattr(ip, gfs64_to_cpu(*eablk), name,
					      name_len, type, location);
			if (err || location->ea)
				break;
			eablk++;
		}
		brelse(bh);
		if (err < 0)
			goto fail;
	} else {
		err =
		    find_direct_eattr(ip, ip->i_di.di_eattr, name, name_len,
				      type, location);
		if (err < 0)
			goto fail;
	}

	return (location->ea != NULL);

      fail:
	return err;
}

static void
make_space(struct gfs_inode *ip, struct buffer_head *bh, uint32_t size,
	   uint64_t blkno, struct gfs_ea_location *avail)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	uint32_t free_size, avail_size;
	struct gfs_ea_header *ea, *new_ea;
	void *buf;

	free_size = 0;
	avail_size = sdp->sd_sb.sb_bsize - sizeof (struct gfs_meta_header);
	ea = GFS_FIRST_EA(bh);
	GFS_ASSERT_INODE(GFS_EA_REC_LEN(ea), ip,);
	if (ea->ea_type == GFS_EATYPE_UNUSED) {
		free_size = GFS_EA_REC_LEN(ea);
		ea = GFS_EA_NEXT(ea);
	}
	while (free_size < size) {
		free_size += (GFS_EA_REC_LEN(ea) - GFS_EA_SIZE(ea));
		if (GFS_EA_IS_LAST(ea))
			break;
		ea = GFS_EA_NEXT(ea);
	}
	if (free_size < size)
		goto out;
	buf = gmalloc(avail_size);

	free_size = avail_size;
	ea = GFS_FIRST_EA(bh);
	if (ea->ea_type == GFS_EATYPE_UNUSED)
		ea = GFS_EA_NEXT(ea);
	new_ea = (struct gfs_ea_header *) buf;
	new_ea->ea_flags = 0;
	new_ea->ea_rec_len = cpu_to_gfs32(size);
	new_ea->ea_num_ptrs = 0;
	new_ea->ea_type = GFS_EATYPE_UNUSED;
	free_size -= size;
	new_ea = GFS_EA_NEXT(new_ea);
	while (1) {
		memcpy(new_ea, ea, GFS_EA_SIZE(ea));
		if (GFS_EA_IS_LAST(ea))
			break;
		new_ea->ea_rec_len = cpu_to_gfs32(GFS_EA_SIZE(ea));
		free_size -= GFS_EA_SIZE(ea);
		ea = GFS_EA_NEXT(ea);
		new_ea = GFS_EA_NEXT(new_ea);
	}
	new_ea->ea_rec_len = cpu_to_gfs32(free_size);
	memcpy(GFS_FIRST_EA(bh), buf, avail_size);
	kfree(buf);
	avail->ea = GFS_FIRST_EA(bh);
	avail->prev = NULL;
	avail->bh = bh;

      out:
	return;
}

static int
expand_to_indirect(struct gfs_inode *alloc_ip, struct gfs_inode *ip,
		   struct buffer_head **bh)
{
	int err;
	struct gfs_sbd *sdp = ip->i_sbd;
	struct buffer_head *bh1 = NULL, *bh2 = NULL, *indbh = NULL;
	uint64_t blkno, *blkptr;
	uint32_t free_size, avail_size;
	struct gfs_ea_header *prev, *curr, *new_ea = NULL;

	avail_size = sdp->sd_sb.sb_bsize - sizeof (struct gfs_meta_header);
	free_size = avail_size;
	ip->i_di.di_flags |= GFS_DIF_EA_INDIRECT;
	blkno = ip->i_di.di_eattr;
	err = gfs_metaalloc(alloc_ip, &ip->i_di.di_eattr);
	if (err)
		goto out;
	ip->i_di.di_blocks++;
	err = gfs_dread(sdp, ip->i_di.di_eattr, ip->i_gl, DIO_NEW | DIO_START |
			DIO_WAIT, &indbh);
	if (err)
		goto out;
	bh1 = *bh;
	*bh = indbh;
	gfs_trans_add_bh(ip->i_gl, indbh);
	gfs_metatype_set(sdp, indbh, GFS_METATYPE_IN, GFS_FORMAT_IN);
	memset((indbh)->b_data + sizeof (struct gfs_meta_header), 0,
	       sdp->sd_sb.sb_bsize - sizeof (struct gfs_meta_header));
	blkptr = (uint64_t *) ((indbh)->b_data + sizeof (struct gfs_indirect));
	*blkptr++ = cpu_to_gfs64(blkno);
	prev = NULL;
	curr = GFS_FIRST_EA(bh1);
	while (curr->ea_type != GFS_EATYPE_USR) {
		if (GFS_EA_IS_LAST(curr))
			goto out_drelse1;
		free_size -= GFS_EA_REC_LEN(curr);
		prev = curr;
		curr = GFS_EA_NEXT(curr);
	}
	if (!prev || prev->ea_type == GFS_EATYPE_UNUSED)
		goto out_drelse1;
	gfs_trans_add_bh(ip->i_gl, bh1);
	prev->ea_rec_len = cpu_to_gfs32(GFS_EA_REC_LEN(prev) + free_size);
	prev->ea_flags |= GFS_EAFLAG_LAST;
	bh2 = alloc_eattr_blk(sdp, alloc_ip, ip, &blkno);
	if (!bh2) {
		err = -EIO;
		goto out_drelse1;
	}
	free_size = avail_size;
	new_ea = GFS_FIRST_EA(bh2);
	while (1) {
		memcpy(new_ea, curr, GFS_EA_SIZE(curr));
		if (GFS_EA_IS_LAST(curr))
			break;
		new_ea->ea_rec_len = cpu_to_gfs32(GFS_EA_SIZE(curr));
		free_size -= GFS_EA_SIZE(curr);
		curr = GFS_EA_NEXT(curr);
		new_ea = GFS_EA_NEXT(new_ea);
	}
	new_ea->ea_rec_len = cpu_to_gfs32(free_size);
	*blkptr = cpu_to_gfs64(blkno);
	brelse(bh2);

      out_drelse1:
	brelse(bh1);

      out:
	return err;
}

static void
find_direct_sys_space(struct gfs_inode *ip, int size, struct buffer_head *bh,
		      struct gfs_ea_location *avail)
{
	struct gfs_ea_header *curr, *prev = NULL;

	curr = GFS_FIRST_EA(bh);
	GFS_ASSERT_INODE(GFS_EA_REC_LEN(curr), ip,);
	if (curr->ea_type == GFS_EATYPE_UNUSED) {
		if (GFS_EA_REC_LEN(curr) >= size) {
			avail->ea = curr;
			avail->prev = NULL;
			avail->bh = bh;
			goto out;
		}
		prev = curr;
		curr = GFS_EA_NEXT(curr);
	}
	while (curr->ea_type == GFS_EATYPE_SYS) {
		GFS_ASSERT_INODE(GFS_EA_REC_LEN(curr), ip,);
		if (GFS_EA_REC_LEN(curr) >= GFS_EA_SIZE(curr) + size) {
			avail->ea = curr;
			avail->prev = prev;
			avail->bh = bh;
			goto out;
		}
		if (GFS_EA_IS_LAST(curr))
			break;
		prev = curr;
		curr = GFS_EA_NEXT(curr);
	}
	make_space(ip, bh, size, ip->i_di.di_eattr, avail);

      out:
	return;
}

/**
 * int find_indirect_space
 *
 * @space:   
 * @blktype: returns the type of block GFS_EATYPE_...
 *
 * returns 0 on success, -EXXX on failure
 */
static int
find_indirect_space(struct gfs_inode *ip, uint64_t blkno, int type,
		    int size, struct gfs_ea_location *avail, int *blktype)
{
	int err;
	struct buffer_head *bh;
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_ea_header *curr, *prev = NULL;

	err = gfs_dread(sdp, blkno, ip->i_gl, DIO_START | DIO_WAIT, &bh);
	if (err)
		goto out;
	gfs_metatype_check(sdp, bh, GFS_METATYPE_EA);
	curr = GFS_FIRST_EA(bh);
	GFS_ASSERT_INODE(GFS_EA_REC_LEN(curr), ip,);
	if (curr->ea_type == GFS_EATYPE_UNUSED) {
		if (GFS_EA_IS_LAST(curr)) {
			avail->ea = curr;
			avail->prev = NULL;
			avail->bh = bh;
			*blktype = GFS_EATYPE_UNUSED;
			goto out;
		}
		prev = curr;
		curr = GFS_EA_NEXT(curr);
	}
	if (type != curr->ea_type) {
		*blktype = curr->ea_type;
		goto out_drelse;
	} else
		*blktype = type;
	if (prev && GFS_EA_REC_LEN(prev) >= size) {
		avail->ea = prev;
		avail->prev = NULL;
		avail->bh = bh;
		goto out;
	}
	while (1) {
		GFS_ASSERT_INODE(GFS_EA_REC_LEN(curr), ip,);
		if (GFS_EA_REC_LEN(curr) >= GFS_EA_SIZE(curr) + size) {
			avail->ea = curr;
			avail->prev = prev;
			avail->bh = bh;
			goto out;
		}
		if (GFS_EA_IS_LAST(curr))
			break;
		prev = curr;
		curr = GFS_EA_NEXT(curr);
	}

      out_drelse:
	brelse(bh);

      out:
	return err;
}

static int
find_indirect_sys_space(struct gfs_inode *alloc_ip, struct gfs_inode *ip,
			int size, struct buffer_head *bh,
			struct gfs_ea_location *avail)
{
	int err = 0;
	struct gfs_sbd *sdp = ip->i_sbd;
	uint64_t *eablk, *end, *first_usr_blk = NULL;
	int blktype;
	uint64_t blkno;

	eablk = (uint64_t *) ((bh)->b_data + sizeof (struct gfs_indirect));
	end =
	    eablk + ((sdp->sd_sb.sb_bsize - sizeof (struct gfs_indirect)) / 8);

	while (eablk < end && *eablk) {
		err =
		    find_indirect_space(ip, gfs64_to_cpu(*eablk),
					GFS_EATYPE_SYS, size, avail, &blktype);
		if (err)
			goto out;
		if (blktype == GFS_EATYPE_USR && !first_usr_blk)
			first_usr_blk = eablk;
		if (avail->ea) {
			if (!first_usr_blk)
				goto out;
			gfs_trans_add_bh(ip->i_gl, bh);
			blkno = *eablk;
			*eablk = *first_usr_blk;
			*first_usr_blk = blkno;
			goto out;
		}
		eablk++;
	}
	if (eablk >= end) {
		err = -ENOSPC;
		goto out;
	}
	avail->bh = alloc_eattr_blk(sdp, alloc_ip, ip, &blkno);
	if (!avail->bh) {
		err = -EIO;
		goto out;
	}
	avail->ea = GFS_FIRST_EA(avail->bh);
	avail->prev = NULL;
	gfs_trans_add_bh(ip->i_gl, bh);
	if (first_usr_blk) {
		*eablk = *first_usr_blk;
		*first_usr_blk = cpu_to_gfs64(blkno);
	} else
		*eablk = cpu_to_gfs64(blkno);

      out:
	return err;
}

int
find_sys_space(struct gfs_inode *alloc_ip, struct gfs_inode *ip, int size,
	       struct gfs_ea_location *avail)
{
	int err;
	struct buffer_head *bh;
	struct gfs_sbd *sdp = ip->i_sbd;

	err =
	    gfs_dread(sdp, ip->i_di.di_eattr, ip->i_gl, DIO_START | DIO_WAIT,
		      &bh);
	if (err)
		goto out;

	if (ip->i_di.di_flags & GFS_DIF_EA_INDIRECT) {
		gfs_metatype_check(sdp, bh, GFS_METATYPE_IN);
		err = find_indirect_sys_space(alloc_ip, ip, size, bh, avail);
	} else {
		gfs_metatype_check(sdp, bh, GFS_METATYPE_EA);
		find_direct_sys_space(ip, size, bh, avail);
		if (!avail->ea) {
			err = expand_to_indirect(alloc_ip, ip, &bh);
			if (err)
				goto out_drelse;
			err =
			    find_indirect_sys_space(alloc_ip, ip, size, bh,
						    avail);
		}
	}

      out_drelse:
	if (avail->bh != bh)
		brelse(bh);

      out:
	return err;
}

static int
get_blk_type(struct gfs_inode *ip, uint64_t blkno, int *blktype)
{
	int err = 0;
	struct gfs_sbd *sdp = ip->i_sbd;
	struct buffer_head *bh;
	struct gfs_ea_header *ea;

	err = gfs_dread(sdp, blkno, ip->i_gl, DIO_START | DIO_WAIT, &bh);
	if (err)
		goto out;
	gfs_metatype_check(sdp, bh, GFS_METATYPE_EA);
	ea = GFS_FIRST_EA(bh);
	GFS_ASSERT_INODE(GFS_EA_REC_LEN(ea), ip,);
	if (ea->ea_type == GFS_EATYPE_UNUSED) {
		if (GFS_EA_IS_LAST(ea)) {
			*blktype = GFS_EATYPE_UNUSED;
			goto out_drelse;
		}
		ea = GFS_EA_NEXT(ea);
		GFS_ASSERT_INODE(GFS_EA_REC_LEN(ea), ip,);
	}
	*blktype = ea->ea_type;

      out_drelse:
	brelse(bh);

      out:
	return err;
}

static void
find_direct_usr_space(struct gfs_inode *ip, int size, struct buffer_head *bh,
		      struct gfs_ea_location *avail)
{
	struct gfs_ea_header *curr, *prev = NULL;

	curr = GFS_FIRST_EA(bh);
	GFS_ASSERT_INODE(GFS_EA_REC_LEN(curr), ip,);
	if (curr->ea_type == GFS_EATYPE_UNUSED) {
		if (GFS_EA_IS_LAST(curr)) {
			avail->ea = curr;
			avail->prev = NULL;
			avail->bh = bh;
			goto out;
		}
		prev = curr;
		curr = GFS_EA_NEXT(curr);
		if (curr->ea_type == GFS_EATYPE_USR
		    && GFS_EA_REC_LEN(prev) >= size) {
			avail->ea = prev;
			avail->prev = NULL;
			avail->bh = bh;
			goto out;
		}
	}
	while (curr->ea_type != GFS_EATYPE_USR) {
		GFS_ASSERT_INODE(GFS_EA_REC_LEN(curr), ip,);
		if (GFS_EA_IS_LAST(curr))
			break;
		prev = curr;
		curr = GFS_EA_NEXT(curr);
	}
	while (1) {
		GFS_ASSERT_INODE(GFS_EA_REC_LEN(curr), ip,);
		if (GFS_EA_REC_LEN(curr) >= GFS_EA_SIZE(curr) + size) {
			avail->ea = curr;
			avail->prev = prev;
			avail->bh = bh;
			goto out;
		}
		if (GFS_EA_IS_LAST(curr))
			break;
		prev = curr;
		curr = GFS_EA_NEXT(curr);
	}

      out:
	return;
}

static int
find_indirect_usr_space(struct gfs_inode *ip, int size, struct buffer_head *bh,
			struct gfs_ea_location *avail)
{
	int err = 0;
	struct gfs_sbd *sdp = ip->i_sbd;
	uint64_t *eablk, *end, *last_sys_blk = NULL, *first_usr_blk = NULL;
	int blktype;
	uint64_t blkno;

	eablk = (uint64_t *) ((bh)->b_data + sizeof (struct gfs_indirect));
	end =
	    eablk + ((sdp->sd_sb.sb_bsize - sizeof (struct gfs_indirect)) / 8);

	while (eablk < end && *eablk) {
		err =
		    find_indirect_space(ip, gfs64_to_cpu(*eablk),
					GFS_EATYPE_USR, size, avail, &blktype);
		if (err)
			goto out;
		if (blktype == GFS_EATYPE_SYS)
			last_sys_blk = eablk;
		if (blktype == GFS_EATYPE_USR && !first_usr_blk)
			first_usr_blk = eablk;
		if (avail->ea) {
			if (first_usr_blk)
				goto out;
			first_usr_blk = eablk + 1;
			while (first_usr_blk < end && *first_usr_blk) {
				err =
				    get_blk_type(ip,
						 gfs64_to_cpu(*first_usr_blk),
						 &blktype);
				if (blktype == GFS_EATYPE_SYS)
					last_sys_blk = first_usr_blk;
				if (blktype == GFS_EATYPE_USR)
					break;
				first_usr_blk++;
			}
			if (last_sys_blk > eablk) {
				gfs_trans_add_bh(ip->i_gl, bh);
				blkno = *eablk;
				*eablk = *last_sys_blk;
				*last_sys_blk = blkno;
			}
			goto out;
		}
		eablk++;
	}

	if (eablk >= end) {
		err = -ENOSPC;
		goto out;
	}
	avail->bh = alloc_eattr_blk(sdp, ip, ip, &blkno);
	if (!avail->bh) {
		err = -EIO;
		goto out;
	}
	avail->ea = GFS_FIRST_EA(avail->bh);
	avail->prev = NULL;
	gfs_trans_add_bh(ip->i_gl, bh);
	*eablk = cpu_to_gfs64(blkno);

      out:
	return err;
}

static int
find_usr_space(struct gfs_inode *ip, int size, struct gfs_ea_location *avail)
{
	int err;
	struct buffer_head *bh;
	struct gfs_sbd *sdp = ip->i_sbd;

	err =
	    gfs_dread(sdp, ip->i_di.di_eattr, ip->i_gl, DIO_START | DIO_WAIT,
		      &bh);
	if (err)
		goto out;

	if (ip->i_di.di_flags & GFS_DIF_EA_INDIRECT) {
		gfs_metatype_check(sdp, bh, GFS_METATYPE_IN);
		err = find_indirect_usr_space(ip, size, bh, avail);
	} else {
		gfs_metatype_check(sdp, bh, GFS_METATYPE_EA);
		find_direct_usr_space(ip, size, bh, avail);
		if (!avail->ea) {
			err = expand_to_indirect(ip, ip, &bh);
			if (err)
				goto out_drelse;
			err = find_indirect_usr_space(ip, size, bh, avail);
		}
	}

      out_drelse:
	if (avail->bh != bh)
		brelse(bh);

      out:
	return err;
}

static int
find_space(struct gfs_inode *ip, int size, int type,
	   struct gfs_ea_location *avail)
{
	int err;

	memset(avail, 0, sizeof (struct gfs_ea_location));

	if (type == GFS_EATYPE_SYS)
		err = find_sys_space(ip, ip, size, avail);
	else
		err = find_usr_space(ip, size, avail);

	return err;
}

static int
can_replace_in_block(struct gfs_inode *ip, int size,
		     struct gfs_ea_location found, struct gfs_ea_header **space)
{
	struct gfs_ea_header *curr, *prev = NULL;

	*space = NULL;
	curr = GFS_FIRST_EA(found.bh);
	GFS_ASSERT_INODE(GFS_EA_REC_LEN(curr), ip,);
	if (curr->ea_type == GFS_EATYPE_UNUSED) {
		if (GFS_EA_REC_LEN(curr) >= size) {
			*space = curr;
			goto out;
		}
		prev = curr;
		curr = GFS_EA_NEXT(curr);
	}
	while (1) {
		GFS_ASSERT_INODE(GFS_EA_REC_LEN(curr), ip,);
		if (curr == found.ea) {
			/*
			 * See if there will be enough space after the old version of the eattr
			 * is deleted.
			 */
			if (prev) {
				if (prev->ea_type == GFS_EATYPE_UNUSED) {
					if (GFS_EA_REC_LEN(prev) +
					    GFS_EA_REC_LEN(curr) >= size) {
						*space = prev;
						goto out;
					}
				} else if (GFS_EA_REC_LEN(prev) +
					   GFS_EA_REC_LEN(curr) >=
					   GFS_EA_SIZE(prev) + size) {
					*space = prev;
					goto out;
				}
			} else if (GFS_EA_REC_LEN(curr) >= size) {
				*space = curr;
				goto out;
			}
		} else if (GFS_EA_REC_LEN(curr) >= GFS_EA_SIZE(curr) + size) {
			*space = curr;
			goto out;
		}
		if (GFS_EA_IS_LAST(curr))
			break;
		prev = curr;
		curr = GFS_EA_NEXT(curr);
	}

      out:
	return (*space != NULL);
}

/**
 * read_unstuffed - actually copies the unstuffed data into the
 *                  request buffer
 */

int
read_unstuffed(void *dest, struct gfs_inode *ip, struct gfs_sbd *sdp,
	       struct gfs_ea_header *ea, uint32_t avail_size,
	       gfs_ea_copy_fn_t copy_fn)
{
	struct buffer_head *bh[66];	/*  This is the maximum number of data ptrs possible  */
	int err = 0;
	int max = GFS_EADATA_NUM_PTRS(GFS_EA_DATA_LEN(ea), avail_size);
	int i, j, left = GFS_EA_DATA_LEN(ea);
	char *outptr, *buf;
	uint64_t *indptr = GFS_EA_DATA_PTRS(ea);

	for (i = 0; i < max; i++) {
		err =
		    gfs_dread(sdp, gfs64_to_cpu(*indptr), ip->i_gl, DIO_START,
			      &bh[i]);
		indptr++;
		if (err) {
			for (j = 0; j < i; j++)
				brelse(bh[j]);
			goto out;
		}
	}

	outptr = dest;

	for (i = 0; i < max; i++) {
		err = gfs_dreread(sdp, bh[i], DIO_WAIT);
		if (err) {
			for (j = i; j < max; j++)
				brelse(bh[j]);
			goto out;
		}
		gfs_metatype_check(sdp, bh[i], GFS_METATYPE_EA);
		buf = (bh[i])->b_data + sizeof (struct gfs_meta_header);
		err =
		    copy_fn(outptr, buf,
			    (avail_size > left) ? left : avail_size);
		if (err) {
			for (j = i; j < max; j++)
				brelse(bh[j]);
			goto out;
		}
		left -= avail_size;
		outptr += avail_size;
		brelse(bh[i]);
	}

      out:

	return err;
}

/**
 * functionname - summary
 * @param1: description
 * @param2: description
 * @param3: description
 *
 * Function description
 *
 * Returns: what is returned
 */
int
get_ea(struct gfs_sbd *sdp, struct gfs_inode *ip, struct gfs_eaget_io *req,
       gfs_ea_copy_fn_t copy_fn)
{
	int err;
	struct gfs_ea_location location;
	uint32_t avail_size;

	avail_size = sdp->sd_sb.sb_bsize - sizeof (struct gfs_meta_header);

	err = find_eattr(ip, req->eg_name, req->eg_name_len, req->eg_type,
			 &location);
	if (err != 1) {
		if (err == 0)
			err = -ENODATA;
		goto out;
	}

	if (req->eg_data_len) {
		if (req->eg_data_len < GFS_EA_DATA_LEN(location.ea))
			err = -ERANGE;
		else if (GFS_EA_IS_UNSTUFFED(location.ea))
			err =
			    read_unstuffed(req->eg_data, ip, sdp, location.ea,
					   avail_size, copy_fn);
		else
			err = copy_fn(req->eg_data, GFS_EA_DATA(location.ea),
				      GFS_EA_DATA_LEN(location.ea));
		if (!err)
			err = GFS_EA_DATA_LEN(location.ea);
	} else
		err = GFS_EA_DATA_LEN(location.ea);

	brelse(location.bh);

      out:
	return err;
}

/**
 * functionname - summary
 * @param1: description
 * @param2: description
 * @param3: description
 *
 * Function description
 *
 * Returns: what is returned
 */

struct gfs_ea_header *
prep_ea(struct gfs_ea_header *ea)
{
	struct gfs_ea_header *new = ea;

	if (ea->ea_type == GFS_EATYPE_UNUSED) {
		if (GFS_EA_IS_LAST(ea))
			ea->ea_flags = GFS_EAFLAG_LAST;
		else
			ea->ea_flags = 0;
	} else {
		new = GFS_EA_FREESPACE(ea);
		new->ea_rec_len =
		    cpu_to_gfs32(GFS_EA_REC_LEN(ea) - GFS_EA_SIZE(ea));
		ea->ea_rec_len = cpu_to_gfs32(GFS_EA_SIZE(ea));
		if (GFS_EA_IS_LAST(ea)) {
			ea->ea_flags &= ~GFS_EAFLAG_LAST;
			new->ea_flags = GFS_EAFLAG_LAST;
		} else
			new->ea_flags = 0;
	}

	return new;
}

/**
 * replace_ea - replaces the existing data with the request data
 */
int
replace_ea(struct gfs_sbd *sdp, struct gfs_inode *ip, struct gfs_ea_header *ea,
	   struct gfs_easet_io *req)
{
	int err = 0;
	int i;
	uint32_t copy_size, data_left = req->es_data_len;
	struct buffer_head *bh;
	uint64_t *datablk = GFS_EA_DATA_PTRS(ea);
	const char *dataptr = req->es_data;
	uint32_t avail_size =
	    sdp->sd_sb.sb_bsize - sizeof (struct gfs_meta_header);

	ea->ea_data_len = cpu_to_gfs32(req->es_data_len);
	if (!GFS_EA_IS_UNSTUFFED(ea))
		memcpy(GFS_EA_DATA(ea), req->es_data, req->es_data_len);
	else {
		for (i = 0; i < ea->ea_num_ptrs && data_left > 0; i++) {
			err = gfs_dread(sdp, gfs64_to_cpu(*datablk), ip->i_gl,
					DIO_START | DIO_WAIT, &bh);
			if (err)
				goto out;
			gfs_trans_add_bh(ip->i_gl, bh);
			gfs_metatype_check(sdp, bh, GFS_METATYPE_EA);
			copy_size =
			    (data_left > avail_size) ? avail_size : data_left;
			memcpy((bh)->b_data + sizeof (struct gfs_meta_header),
			       dataptr, copy_size);
			dataptr += copy_size;
			data_left -= copy_size;
			datablk++;
			brelse(bh);
		}
		GFS_ASSERT_INODE(data_left == 0, ip,
				 printk
				 ("req->es_data_len = %u, ea->ea_num_ptrs = %d\n",
				  req->es_data_len, ea->ea_num_ptrs);
		    );
	}

      out:
	return err;
}

/**
 * write_ea - writes the request info to an ea, creating new blocks if
 *            necessary
 *
 * @sdp: superblock pointer
 * @alloc_ip: inode that has the blocks reserved for allocation
 * @ip:  inode that is being modified
 * @ea:  the location of the new ea in a block
 * @req: the write request
 *
 * Note: does not update ea_rec_len or the GFS_EAFLAG_LAST bin of ea_flags
 *
 * returns : 0 on success, -EXXX on error
 */

int
write_ea(struct gfs_sbd *sdp, struct gfs_inode *alloc_ip, struct gfs_inode *ip,
	 struct gfs_ea_header *ea, struct gfs_easet_io *req)
{
	int err = 0;
	uint64_t *blkptr;
	uint64_t temp;
	const char *dataptr;
	uint32_t data_left, copy;
	uint32_t avail_size =
	    sdp->sd_sb.sb_bsize - sizeof (struct gfs_meta_header);
	int i;
	struct buffer_head *bh = NULL;

	ea->ea_data_len = cpu_to_gfs32(req->es_data_len);
	ea->ea_name_len = req->es_name_len;
	ea->ea_type = req->es_type;
	ea->ea_pad = 0;

	memcpy(GFS_EA_NAME(ea), req->es_name, req->es_name_len);

	if (GFS_EAREQ_IS_STUFFED(req, avail_size)) {
		ea->ea_num_ptrs = 0;
		memcpy(GFS_EA_DATA(ea), req->es_data, req->es_data_len);
	} else {
		blkptr = GFS_EA_DATA_PTRS(ea);
		dataptr = req->es_data;
		data_left = req->es_data_len;
		ea->ea_num_ptrs =
		    GFS_EADATA_NUM_PTRS(req->es_data_len, avail_size);

		for (i = 0; i < ea->ea_num_ptrs; i++) {
			if ((bh =
			     alloc_eattr_blk(sdp, alloc_ip, ip,
					     &temp)) == NULL) {
				err = -EIO;
				goto out;
			}
			copy =
			    (data_left > avail_size) ? avail_size : data_left;
			memcpy((bh)->b_data + sizeof (struct gfs_meta_header),
			       dataptr, copy);
			*blkptr = cpu_to_gfs64(temp);
			dataptr += copy;
			data_left -= copy;
			blkptr++;
			brelse(bh);
		}

		GFS_ASSERT_INODE(!data_left, ip,);
	}

      out:

	return err;
}

/**
 * erase_ea_data_ptrs - deallocate all the unstuffed data blocks pointed to
 *                          ea records in this block
 * @sdp: the superblock
 * @ip: the inode
 * @blk: the block to check for data pointers
 *
 *
 * Returns: 0 on success, -EXXX on failure
 */

static int
erase_ea_data_ptrs(struct gfs_sbd *sdp, struct gfs_inode *ip,
		   struct buffer_head *dibh, uint64_t blk)
{
	struct gfs_holder rgd_gh;
	int i, err = 0;
	uint64_t *datablk;
	struct buffer_head *eabh;
	char *buf;
	struct gfs_ea_header *ea;
	struct gfs_rgrpd *rgd = NULL;

	err = gfs_dread(sdp, blk, ip->i_gl, DIO_WAIT | DIO_START, &eabh);
	if (err)
		goto fail;

	gfs_metatype_check(sdp, eabh, GFS_METATYPE_EA);
	buf = (eabh)->b_data + sizeof (struct gfs_meta_header);
	ea = (struct gfs_ea_header *) buf;

	while (1) {
		GFS_ASSERT_INODE(GFS_EA_REC_LEN(ea), ip,);
		if (GFS_EA_IS_UNSTUFFED(ea)) {
			datablk = GFS_EA_DATA_PTRS(ea);
			rgd = gfs_blk2rgrpd(sdp, gfs64_to_cpu(*datablk));
			GFS_ASSERT_INODE(rgd, ip,
					 printk("block = %" PRIu64 "\n",
						gfs64_to_cpu(*datablk)););
			err =
			    gfs_glock_nq_init(rgd->rd_gl, LM_ST_EXCLUSIVE, 0,
					      &rgd_gh);
			if (err)
				goto fail_eabh;
			/* Trans may require:
			   One block for the RG header. One block for each ea data block. One
			   One block for the dinode. One block for the current ea block.
			   One block for a quote change.
			   FIXME */
			err =
			    gfs_trans_begin(sdp,
					    3 + ea->ea_num_ptrs, 1);
			if (err)
				goto fail_glock_rg;
			gfs_trans_add_bh(ip->i_gl, dibh);
			for (i = 0; i < ea->ea_num_ptrs; i++, datablk++) {
				gfs_metafree(ip, gfs64_to_cpu(*datablk), 1);
				ip->i_di.di_blocks--;
			}
			ea->ea_num_ptrs = 0;
			gfs_trans_add_bh(ip->i_gl, eabh);
			gfs_dinode_out(&ip->i_di, (dibh)->b_data);
			gfs_trans_end(sdp);
			gfs_glock_dq_uninit(&rgd_gh);
		}
		if (GFS_EA_IS_LAST(ea))
			break;
		ea = GFS_EA_NEXT(ea);
	}

	brelse(eabh);

	return err;

      fail_glock_rg:
	gfs_glock_dq_uninit(&rgd_gh);

      fail_eabh:
	brelse(eabh);

      fail:
	return err;
}

/**
 * gfs_ea_dealloc - deallocate the extended attribute fork
 * @ip: the inode
 *
 * Returns: 0 on success, -EXXX on failure
 */

int
gfs_ea_dealloc(struct gfs_inode *ip)
{
	struct gfs_holder ri_gh, rgd_gh;
	int err = 0;
	struct gfs_sbd *sdp = ip->i_sbd;
	struct buffer_head *dibh, *indbh = NULL;
	uint64_t *startblk, *eablk, *end, *next;
	uint64_t temp;
	int num_blks;
	struct gfs_rgrpd *rgd = NULL;

	if (!ip->i_di.di_eattr)
		goto out;

	gfs_alloc_get(ip);

	err = gfs_quota_hold_m(ip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
	if (err)
		goto out_alloc;

	err = gfs_rindex_hold(sdp, &ri_gh);
	if (err)
		goto out_unhold_q;

	err = gfs_get_inode_buffer(ip, &dibh);
	if (err)
		goto out_rindex_release;

	if (ip->i_di.di_flags & GFS_DIF_EA_INDIRECT) {
		err =
		    gfs_dread(sdp, ip->i_di.di_eattr, ip->i_gl,
			      DIO_WAIT | DIO_START, &indbh);
		if (err)
			goto out_dibh;

		gfs_metatype_check(sdp, indbh, GFS_METATYPE_IN);

		eablk =
		    (uint64_t *) ((indbh)->b_data +
				  sizeof (struct gfs_indirect));
		end =
		    eablk +
		    ((sdp->sd_sb.sb_bsize - sizeof (struct gfs_indirect)) / 8);

		while (*eablk && eablk < end) {
			err =
			    erase_ea_data_ptrs(sdp, ip, dibh,
					       gfs64_to_cpu(*eablk));
			if (err)
				goto out_indbh;
			eablk++;
		}

		startblk = eablk - 1;
		end =
		    (uint64_t *) ((indbh)->b_data +
				  sizeof (struct gfs_indirect));

		while (startblk >= end) {
			rgd = gfs_blk2rgrpd(sdp, gfs64_to_cpu(*startblk));
			GFS_ASSERT_INODE(rgd, ip,);

			num_blks = 1;
			next = eablk = startblk - 1;

			while (eablk >= end) {
				if (rgd ==
				    gfs_blk2rgrpd(sdp, gfs64_to_cpu(*eablk))) {
					if (eablk != next) {
						temp = *eablk;
						*eablk = *next;
						*next = temp;
					}
					num_blks++;
					next--;
				}
				eablk--;
			}

			err =
			    gfs_glock_nq_init(rgd->rd_gl, LM_ST_EXCLUSIVE, 0,
					      &rgd_gh);
			if (err)
				goto out_rindex_release;

			/* Trans may require:
			   One block for the RG header. One block for each block from this
			   resource group. One block for the indirect ea block, 
			   One block for the quote change */

			err =
			    gfs_trans_begin(sdp, 3 + num_blks,
					    1);
			if (err)
				goto out_gunlock_rg;

			gfs_trans_add_bh(ip->i_gl, dibh);

			while (startblk > next) {
				gfs_metafree(ip, gfs64_to_cpu(*startblk), 1);
				ip->i_di.di_blocks--;
				*startblk = 0;
				startblk--;
			}

			gfs_trans_add_bh(ip->i_gl, indbh);
			gfs_dinode_out(&ip->i_di, (dibh)->b_data);

			gfs_trans_end(sdp);

			gfs_glock_dq_uninit(&rgd_gh);
		}

		brelse(indbh);
		indbh = NULL;
	} else {
		err = erase_ea_data_ptrs(sdp, ip, dibh, ip->i_di.di_eattr);
		if (err)
			goto out_rindex_release;
	}

	rgd = gfs_blk2rgrpd(sdp, ip->i_di.di_eattr);
	GFS_ASSERT_INODE(rgd, ip,
			 printk("block = %" PRIu64 "\n", ip->i_di.di_eattr);
	    );

	err = gfs_glock_nq_init(rgd->rd_gl, LM_ST_EXCLUSIVE, 0, &rgd_gh);
	if (err)
		goto out_rindex_release;

	err = gfs_trans_begin(sdp, 3, 1);
	if (err)
		goto out_gunlock_rg;

	gfs_metafree(ip, ip->i_di.di_eattr, 1);

	ip->i_di.di_blocks--;
	ip->i_di.di_eattr = 0;

	gfs_trans_add_bh(ip->i_gl, dibh);
	gfs_dinode_out(&ip->i_di, (dibh)->b_data);

	gfs_trans_end(sdp);

      out_gunlock_rg:
	gfs_glock_dq_uninit(&rgd_gh);

      out_indbh:
	if (indbh)
		brelse(indbh);

      out_dibh:
	brelse(dibh);

      out_rindex_release:
	gfs_glock_dq_uninit(&ri_gh);

      out_unhold_q:
	gfs_quota_unhold_m(ip);

      out_alloc:
	gfs_alloc_put(ip);

      out:

	return err;
}

/**
 * functionname - summary
 * @param1: description
 * @param2: description
 * @param3: description
 *
 * Function description
 *
 * Returns: what is returned
 */

static void
remove_ea(struct gfs_inode *ip, struct gfs_ea_header *ea,
	  struct gfs_ea_header *prev)
{
	uint64_t *datablk;
	int i;

	if (GFS_EA_IS_UNSTUFFED(ea)) {
		datablk = GFS_EA_DATA_PTRS(ea);
		for (i = 0; i < ea->ea_num_ptrs; i++, datablk++) {
			gfs_metafree(ip, gfs64_to_cpu(*datablk), 1);
			ip->i_di.di_blocks--;
		}
	}

	ea->ea_type = GFS_EATYPE_UNUSED;
	ea->ea_num_ptrs = 0;

	if (prev && prev != ea) {
		prev->ea_rec_len =
		    cpu_to_gfs32(GFS_EA_REC_LEN(prev) + GFS_EA_REC_LEN(ea));
		if (GFS_EA_IS_LAST(ea))
			prev->ea_flags |= GFS_EAFLAG_LAST;
	}
}

int
init_new_inode_eattr(struct gfs_inode *dip, struct gfs_inode *ip,
		     struct gfs_easet_io *req)
{
	int err;
	struct buffer_head *bh;
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_ea_header *ea;

	err = gfs_metaalloc(dip, &ip->i_di.di_eattr);
	if (err)
		goto out;

	err = gfs_dread(sdp, ip->i_di.di_eattr, ip->i_gl,
			DIO_NEW | DIO_START | DIO_WAIT, &bh);
	if (err)
		goto out;

	gfs_metatype_set(sdp, bh, GFS_METATYPE_EA, GFS_FORMAT_EA);

	ip->i_di.di_blocks++;

	ea = GFS_FIRST_EA(bh);
	ea->ea_flags = GFS_EAFLAG_LAST;
	ea->ea_rec_len =
	    cpu_to_gfs32(sdp->sd_sb.sb_bsize - sizeof (struct gfs_meta_header));
	ea->ea_num_ptrs = 0;
	ea->ea_type = GFS_EATYPE_UNUSED;
	err = write_ea(sdp, dip, ip, ea, req);
	if (err)
		goto out_drelse;

	gfs_trans_add_bh(ip->i_gl, bh);

      out_drelse:
	brelse(bh);

      out:
	return err;
}

int
do_init_eattr(struct gfs_sbd *sdp, struct gfs_inode *ip,
	      struct gfs_easet_io *req)
{
	int err;
	struct buffer_head *bh;
	struct gfs_ea_header *ea;

	bh = alloc_eattr_blk(sdp, ip, ip, &ip->i_di.di_eattr);
	if (bh) {
		ea = GFS_FIRST_EA(bh);
		err = write_ea(sdp, ip, ip, ea, req);
		brelse(bh);
	} else
		err = -EIO;

	return err;
}

/**
 * init_eattr - initializes a new eattr block
 */

static int
init_eattr(struct gfs_sbd *sdp, struct gfs_inode *ip, struct gfs_easet_io *req)
{
	int err = 0;
	struct gfs_alloc *al;
	uint32_t ea_metablks;
	struct buffer_head *dibh;
	struct posix_acl *acl = NULL;
	uint32_t avail_size =
	    sdp->sd_sb.sb_bsize - sizeof (struct gfs_meta_header);

	ea_metablks =
	    GFS_EAREQ_IS_STUFFED(req,
				 avail_size) ? 1 : (1 +
						    GFS_EADATA_NUM_PTRS(req->
									es_data_len,
									avail_size));

	if (IS_ACCESS_ACL(req->es_name, req->es_name_len)){
                acl = posix_acl_from_xattr(req->es_data, req->es_data_len);
                if (IS_ERR(acl)) {
                        err = PTR_ERR(acl);
                        goto out;
                }
        }

	al = gfs_alloc_get(ip);

	err = gfs_quota_lock_m(ip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
	if (err)
		goto out_alloc;

	al->al_requested_meta = ea_metablks;

	err = gfs_inplace_reserve(ip);
	if (err)
		goto out_gunlock_q;

	err = gfs_quota_check(ip, ip->i_di.di_uid, ip->i_di.di_gid);
	if (err)
		goto out_ipres;

	err = gfs_get_inode_buffer(ip, &dibh);
	if (err)
		goto out_ipres;

	/* Trans may require:
	   A modified dinode, multiple EA metadata blocks, and all blocks for a RG
	   bitmap */

	err =
	    gfs_trans_begin(sdp,
			    1 + ea_metablks + al->al_rgd->rd_ri.ri_length, 1);
	if (err)
		goto out_dibh;

	err = do_init_eattr(sdp, ip, req);
	if (err)
		goto out_end_trans;

	if (acl)
                gfs_acl_set_mode(ip, acl);

	gfs_trans_add_bh(ip->i_gl, dibh);
	gfs_dinode_out(&ip->i_di, (dibh)->b_data);

      out_end_trans:
	gfs_trans_end(sdp);

      out_dibh:
	brelse(dibh);

      out_ipres:
	gfs_inplace_release(ip);

      out_gunlock_q:
	gfs_quota_unlock_m(ip);

      out_alloc:
	gfs_alloc_put(ip);
	posix_acl_release(acl);

      out:
	return err;
}

/**
 * alloc_eattr_blk - allocates a new block for extended attributes.
 * @sdp: A pointer to the superblock
 * @alloc_ip: A pointer to the inode that has reserved the blocks for
 *            allocation
 * @ip: A pointer to the inode that's getting extended attributes
 * @block: the block allocated
 *
 * Returns: the buffer head on success, NULL on failure
 */

static struct buffer_head *
alloc_eattr_blk(struct gfs_sbd *sdp, struct gfs_inode *alloc_ip,
		struct gfs_inode *ip, uint64_t * block)
{
	int err = 0;
	struct buffer_head *bh = NULL;
	struct gfs_ea_header *ea;

	err = gfs_metaalloc(alloc_ip, block);
	if (err)
		goto out;

	err =
	    gfs_dread(sdp, *block, ip->i_gl, DIO_NEW | DIO_START | DIO_WAIT, &bh);
	if (err)
		goto out;

	gfs_metatype_set(sdp, bh, GFS_METATYPE_EA, GFS_FORMAT_EA);

	ip->i_di.di_blocks++;

	ea = GFS_FIRST_EA(bh);
	ea->ea_flags = GFS_EAFLAG_LAST;
	ea->ea_rec_len =
	    cpu_to_gfs32(sdp->sd_sb.sb_bsize - sizeof (struct gfs_meta_header));
	ea->ea_num_ptrs = 0;
	ea->ea_type = GFS_EATYPE_UNUSED;

	gfs_trans_add_bh(ip->i_gl, bh);

      out:

	return bh;
}

/**
 * functionname - summary
 * @param1: description
 * @param2: description
 * @param3: description
 *
 * Function description
 *
 * Returns: what is returned
 */

static int
list_direct_ea(struct gfs_sbd *sdp, struct gfs_inode *ip,
	       struct buffer_head *bh, struct gfs_eaget_io *req,
	       gfs_ea_copy_fn_t copy_fn, uint32_t * size)
{
	int err = 0;
	struct gfs_ea_header *ea;
	char buf[256];
	char *ptr;

	gfs_metatype_check(sdp, bh, GFS_METATYPE_EA);

	ea = (struct gfs_ea_header *) ((bh)->b_data +
				       sizeof (struct gfs_meta_header));
	if (ea->ea_type == GFS_EATYPE_UNUSED) {
		if (GFS_EA_IS_LAST(ea))
			goto out;
		else
			ea = GFS_EA_NEXT(ea);
	}

	while (1) {
		GFS_ASSERT_INODE(GFS_EA_REC_LEN(ea), ip,);

		if (req->eg_data_len) {
			if (*size > req->eg_data_len) {
				err = -ERANGE;
				break;
			}
			ptr = buf;

			GFS_ASSERT_INODE(GFS_EATYPE_VALID(ea->ea_type), ip,);
			if (ea->ea_type == GFS_EATYPE_USR) {
				memcpy(ptr, "user.", 5);
				ptr += 5;
			} else {
				memcpy(ptr, "system.", 7);
				ptr += 7;
			}
			memcpy(ptr, GFS_EA_NAME(ea), ea->ea_name_len);
			ptr += ea->ea_name_len;
			*ptr = 0;
			err =
			    copy_fn(req->eg_data + *size, buf,
				    GFS_EA_STRLEN(ea));
			if (err)
				break;
		}

		*size = *size + GFS_EA_STRLEN(ea);

		if (GFS_EA_IS_LAST(ea))
			break;
		ea = GFS_EA_NEXT(ea);
	}

      out:

	return err;
}

/**
 * functionname - summary
 * @param1: description
 * @param2: description
 * @param3: description
 *
 * Function description
 *
 * Returns: what is returned
 */

static int
list_ea(struct gfs_sbd *sdp, struct gfs_inode *ip, struct gfs_eaget_io *req,
	gfs_ea_copy_fn_t copy_fn)
{
	int err;
	struct buffer_head *bh, *eabh;
	uint64_t *eablk, *end;
	uint32_t size = 0;

	err =
	    gfs_dread(sdp, ip->i_di.di_eattr, ip->i_gl, DIO_START | DIO_WAIT,
		      &bh);
	if (err)
		goto out;

	if (ip->i_di.di_flags & GFS_DIF_EA_INDIRECT) {
		gfs_metatype_check(sdp, bh, GFS_METATYPE_IN);
		eablk =
		    (uint64_t *) ((bh)->b_data + sizeof (struct gfs_indirect));
		end =
		    eablk +
		    ((sdp->sd_sb.sb_bsize - sizeof (struct gfs_indirect)) / 8);

		while (*eablk && eablk < end) {
			err =
			    gfs_dread(sdp, gfs64_to_cpu(*eablk), ip->i_gl,
				      DIO_START | DIO_WAIT, &eabh);
			if (err)
				goto out_drelse;
			err = list_direct_ea(sdp, ip, eabh, req, copy_fn, &size);
			brelse(eabh);
			if (err)
				goto out_drelse;
			eablk++;
		}
	} else {
		err = list_direct_ea(sdp, ip, bh, req, copy_fn, &size);
		if (err)
			goto out_drelse;
	}

	if (!err)
		err = size;

      out_drelse:
	brelse(bh);

      out:

	return err;
}

/**
 * gfs_get_eattr - read an extended attribute, or a list of ea names
 * @sdp: pointer to the superblock
 * @ip: pointer to the inode for the target file  
 * @req: the request information
 * @copy_fn: the function to use to do the actual copying
 *
 * Returns: actual size of data on success, -EXXX on error
 */
int
gfs_get_eattr(struct gfs_sbd *sdp, struct gfs_inode *ip,
	      struct gfs_eaget_io *req, gfs_ea_copy_fn_t copy_fn)
{
	struct gfs_holder i_gh;
	int err;

	if (req->eg_name) {
		err = gfs_ea_read_permission(req, ip);
		if (err)
			goto out;
	}

	/*  This seems to be a read.  Are we sure we don't want to acquire the lock in LM_ST_SHARED?  */

	err = gfs_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY, &i_gh);
	if (err)
		goto out;

	if (ip->i_di.di_eattr == 0) {
		if (!req->eg_name) {
			if (!req->eg_data_len && req->eg_len) {
				uint32_t no_data = 0;

				err =
				    copy_fn(req->eg_len, &no_data,
					    sizeof (uint32_t));
			}
		} else
			err = -ENODATA;

		goto out_gunlock;
	}

	if (req->eg_name)
		err = get_ea(sdp, ip, req, copy_fn);
	else
		err = list_ea(sdp, ip, req, copy_fn);

      out_gunlock:
	gfs_glock_dq_uninit(&i_gh);

      out:

	return err;
}

static int
do_set_ea(struct gfs_sbd *sdp, struct gfs_inode *ip, struct gfs_easet_io *req,
	  struct gfs_ea_location location)
{
	int err = 0;
	int req_size;
	uint32_t avail_size =
	    sdp->sd_sb.sb_bsize - sizeof (struct gfs_meta_header);
	struct gfs_ea_location space;

	req_size = get_req_size(req, avail_size);

	if (location.ea) {
		struct gfs_ea_header *new_space;
		if (req->es_cmd == GFS_EACMD_REMOVE) {
			remove_ea(ip, location.ea, location.prev);
			gfs_trans_add_bh(ip->i_gl, location.bh);
			goto out;
		}
		if (can_replace(location.ea, req, avail_size)) {
			err = replace_ea(sdp, ip, location.ea, req);
			if (!err)
				gfs_trans_add_bh(ip->i_gl, location.bh);
			goto out;
		}
		/*
		 * This part is kind of confusing.  If the inode has direct EAs
		 * Then adding another EA can't run it out of space, so it is safe to
		 * delete the EA before looking for space.  If the inode has indirect
		 * EAs, there may not be enough space left, so first you check for space
		 * and they you delete the EA.
		 */
		if ((ip->i_di.di_flags & GFS_DIF_EA_INDIRECT) == 0) {
			remove_ea(ip, location.ea, location.prev);
			err = find_space(ip, req_size, req->es_type, &space);
			if (err)
				goto out;
			new_space = prep_ea(space.ea);
			err = write_ea(sdp, ip, ip, new_space, req);
			if (!err) {
				gfs_trans_add_bh(ip->i_gl, location.bh);
				gfs_trans_add_bh(ip->i_gl, space.bh);
			}
			brelse(space.bh);
			goto out;
		}
		if (can_replace_in_block(ip, req_size, location, &new_space)) {
			remove_ea(ip, location.ea, location.prev);
			new_space = prep_ea(new_space);
			err = write_ea(sdp, ip, ip, new_space, req);
			if (!err)
				gfs_trans_add_bh(ip->i_gl, location.bh);
			goto out;
		}
		err = find_space(ip, req_size, req->es_type, &space);
		if (err)
			/* You can return a non IO error here.  If there is no space left,
			 * you can return -ENOSPC. So you must not have added a buffer to
			 * the transaction yet.
			 */
			goto out;
		remove_ea(ip, location.ea, location.prev);
		new_space = prep_ea(space.ea);
		err = write_ea(sdp, ip, ip, new_space, req);
		if (!err) {
			gfs_trans_add_bh(ip->i_gl, location.bh);
			gfs_trans_add_bh(ip->i_gl, space.bh);
		}
		brelse(space.bh);
		goto out;
	}
	err = find_space(ip, req_size, req->es_type, &space);
	if (err)
		/* you can also get -ENOSPC here */
		goto out;
	space.ea = prep_ea(space.ea);
	err = write_ea(sdp, ip, ip, space.ea, req);
	if (!err)
		gfs_trans_add_bh(ip->i_gl, space.bh);
	brelse(space.bh);

      out:
	return err;
}

static int
set_ea(struct gfs_sbd *sdp, struct gfs_inode *ip, struct gfs_easet_io *req,
       struct gfs_ea_location location)
{
	int err;
	struct gfs_alloc *al;
	struct gfs_rgrpd *rgd = NULL;
	struct buffer_head *dibh;
	uint32_t avail_size =
	    sdp->sd_sb.sb_bsize - sizeof (struct gfs_meta_header);
	int unstuffed_ea_blks = 0;
	struct gfs_holder ri_gh, rgd_gh;
	struct posix_acl *acl = NULL;

	if (IS_ACCESS_ACL(req->es_name, req->es_name_len) && req->es_data){
                acl = posix_acl_from_xattr(req->es_data, req->es_data_len);
                if (IS_ERR(acl)) {
                        err = PTR_ERR(acl);
                        goto out;
                }
        }

	err = gfs_get_inode_buffer(ip, &dibh);
	if (err)
		goto out_acl;
	al = gfs_alloc_get(ip);

	err = gfs_quota_lock_m(ip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
	if (err)
		goto out_alloc;

	/* 
	 * worst case, you need to switch from direct to indirect, which can
	 * take up to 3 new blocks, and you need to create enough unstuffed data
	 * blocks to hold all the data
	 */
	al->al_requested_meta = 3 + GFS_EADATA_NUM_PTRS(req->es_data_len, avail_size);

	err = gfs_inplace_reserve(ip);
	if (err)
		goto out_lock_quota;

	err = gfs_quota_check(ip, ip->i_di.di_uid, ip->i_di.di_gid);
	if (err)
		goto out_reserve;

	if (location.ea && GFS_EA_IS_UNSTUFFED(location.ea)) {
		/*
		 * If there is an EA, we might need to delete it. 
		 * Since all unstuffed data blocks are added at the same time,
		 * they are all from the same resource group.
		 */
		err = gfs_rindex_hold(sdp, &ri_gh);
		if (err)
			goto out_reserve;
		rgd =
		    gfs_blk2rgrpd(sdp,
				  gfs64_to_cpu(*GFS_EA_DATA_PTRS(location.ea)));
		GFS_ASSERT_INODE(rgd, ip,
				 printk("block = %" PRIu64 "\n",
					gfs64_to_cpu(*GFS_EA_DATA_PTRS
						     (location.ea)));
		    );
		err =
		    gfs_glock_nq_init(rgd->rd_gl, LM_ST_EXCLUSIVE, 0, &rgd_gh);
		if (err)
			goto out_rindex;
		unstuffed_ea_blks = location.ea->ea_num_ptrs;
	}

	/* 
	 * The transaction may require:
	 * Modifying the dinode block, Modifying the indirect ea block,
	 * modifying an ea block, all the allocation blocks, all the blocks for
	 * a RG bitmap,  the RG header block, a RG block for each unstuffed data
	 * block you might be deleting.
	 */
	err = gfs_trans_begin(sdp, 4 + al->al_requested_meta +
			      al->al_rgd->rd_ri.ri_length + unstuffed_ea_blks,
			      1);
	if (err)
		goto out_lock_rg;

	err = do_set_ea(sdp, ip, req, location);

	if (!err) {
		if (acl)
			gfs_acl_set_mode(ip, acl);
		gfs_trans_add_bh(ip->i_gl, dibh);
		gfs_dinode_out(&ip->i_di, (dibh)->b_data);
	}

	gfs_trans_end(sdp);

      out_lock_rg:
	if (rgd)
		gfs_glock_dq_uninit(&rgd_gh);

      out_rindex:
	if (rgd)
		gfs_glock_dq_uninit(&ri_gh);

      out_reserve:
	gfs_inplace_release(ip);

      out_lock_quota:
	gfs_quota_unlock_m(ip);

      out_alloc:
	gfs_alloc_put(ip);
	brelse(dibh);

      out_acl:
	posix_acl_release(acl);

      out:
	return err;
}

/**
 * gfs_set_eattr - sets (or creates or replaces) an extended attribute
 * @sdp: pointer to the superblock
 * @ip: pointer to the inode of the target file
 * @req: request information
 *
 * Returns: 0 on success -EXXX on error
 */
int
gfs_set_eattr(struct gfs_sbd *sdp, struct gfs_inode *ip,
	      struct gfs_easet_io *req)
{
	struct gfs_holder i_gh;
	int err;
	uint32_t req_size;
	uint32_t avail_size =
	    sdp->sd_sb.sb_bsize - sizeof (struct gfs_meta_header);
	struct gfs_ea_location location;

	if (!GFS_EACMD_VALID(req->es_cmd)) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (strlen(req->es_name) == 0) {
		err = -EINVAL;
		goto out;
	}

	err = gfs_ea_write_permission(req, ip);
	if (err)
		goto out;

	if ((req_size = get_req_size(req, avail_size)) > avail_size) {
		/* This can only happen with 512 byte blocks */
		err = -ERANGE;
		goto out;
	}
	err = gfs_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &i_gh);
	if (err)
		goto out;

	if (ip->i_di.di_eattr == 0) {
		if (req->es_cmd == GFS_EACMD_REPLACE
		    || req->es_cmd == GFS_EACMD_REMOVE) {
			err = -ENODATA;
			goto out_gunlock;
		}
		err = init_eattr(sdp, ip, req);
		goto out_gunlock;
	}

	err = find_eattr(ip, req->es_name, req->es_name_len, req->es_type,
			 &location);
	if (err < 0)
		goto out_gunlock;
	if (err == 0 && (req->es_cmd == GFS_EACMD_REPLACE ||
			 req->es_cmd == GFS_EACMD_REMOVE)) {
		err = -ENODATA;
		goto out_relse;
	}
	err = set_ea(sdp, ip, req, location);

      out_relse:
	if (location.bh)
		brelse(location.bh);

      out_gunlock:
	gfs_glock_dq_uninit(&i_gh);

      out:
	return err;
}

/**
 * gfs_set_eattr_ioctl - creates, modifies, or removes an extended attribute.
 * @sdp: pointer to the superblock
 * @ip: a pointer to the gfs inode for the file
 * @arg: a pointer to gfs_set_eattr_io_t struct with the request
 *
 * Notes: ioctl wrapper for gfs_set_eattr
 * Returns: 0 on success, -EXXX or error
 */

int
gfs_set_eattr_ioctl(struct gfs_sbd *sdp, struct gfs_inode *ip, void *arg)
{
	struct gfs_easet_io req;
	int err = 0;
	char *name = NULL;
	char *data = NULL;

	if (copy_from_user(&req, arg, sizeof (struct gfs_easet_io))) {
		err = -EFAULT;
		goto out;
	}

	name = gmalloc(req.es_name_len);

	if (req.es_data) {
		data = gmalloc(req.es_data_len);

		if (copy_from_user(data, req.es_data, req.es_data_len)) {
			err = -EFAULT;
			goto out_free;
		}
	}
	if (copy_from_user(name, req.es_name, req.es_name_len)) {
		err = -EFAULT;
		goto out_free;
	}
	req.es_data = data;
	req.es_name = name;
	err = gfs_set_eattr(sdp, ip, &req);

      out_free:
	kfree(name);
	if (data)
		kfree(data);

      out:
	return err;
}

/**
 * gfs_get_eattr_ioctl - gets the value for the requested attribute name,
 *                       or a list of all the extended attribute names.
 * @sdp: pointer to the superblock
 * @ip: a pointer to the inode for the file
 * @arg: a pointer to the struct gfs_eaget_io struct holding the request 
 *
 * Notes: ioctl wrapper for the gfs_get_eattr function 
 * Returns: 0 on success, -EXXX on error.
 */

int
gfs_get_eattr_ioctl(struct gfs_sbd *sdp, struct gfs_inode *ip, void *arg)
{
	struct gfs_eaget_io req;
	int result = 0;
	char *name = NULL;
	uint32_t size;

	if (copy_from_user(&req, arg, sizeof (struct gfs_eaget_io))) {
		result = -EFAULT;
		goto out;
	}

	if (req.eg_name) {
		name = gmalloc(req.eg_name_len);

		if (copy_from_user(name, req.eg_name, req.eg_name_len)) {
			result = -EFAULT;
			goto out_free;
		}
		req.eg_name = name;
	}
	result = gfs_get_eattr(sdp, ip, &req, gfs_ea_copy_to_user);

      out_free:
	if (name)
		kfree(name);

	if (result >= 0) {
		size = result;
		result =
		    gfs_ea_copy_to_user(req.eg_len, &size, sizeof(uint32_t));
	}

      out:

	return result;
}

/**
 * functionname - summary
 * @param1: description
 * @param2: description
 * @param3: description
 *
 * Function description
 *
 * Returns: what is returned
 */

static int
gfs_get_direct_eattr_meta(struct gfs_inode *ip, struct gfs_user_buffer *ub,
			  uint64_t blk)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	struct buffer_head *databh, *bh;
	struct gfs_ea_header *ea;
	uint64_t *datablk;
	unsigned int i;
	int error;

	error = gfs_dread(sdp, blk, ip->i_gl, DIO_START | DIO_WAIT, &bh);
	if (error)
		goto out;

	error = gfs_add_bh_to_ub(ub, bh);

	ea = (struct gfs_ea_header *) ((bh)->b_data +
				       sizeof (struct gfs_meta_header));
	for (;;) {
		GFS_ASSERT_INODE(GFS_EA_REC_LEN(ea), ip,);

		datablk = GFS_EA_DATA_PTRS(ea);

		for (i = 0; i < ea->ea_num_ptrs; i++) {
			error =
			    gfs_dread(sdp, gfs64_to_cpu(*datablk), ip->i_gl,
				      DIO_START | DIO_WAIT, &databh);
			if (error)
				goto out_relse;

			error = gfs_add_bh_to_ub(ub, databh);

			brelse(databh);

			if (error)
				goto out_relse;

			datablk++;
		}

		if (GFS_EA_IS_LAST(ea))
			break;
		ea = GFS_EA_NEXT(ea);
	}

      out_relse:
	brelse(bh);

      out:

	return error;
}

/**
 * gfs_get_eattr_meta - return all the eattr blocks of a file
 * @dip: the directory
 * @ub: the structure representing the user buffer to copy to
 *
 * Returns: 0 on success, -EXXX on failure
 */

int
gfs_get_eattr_meta(struct gfs_inode *ip, struct gfs_user_buffer *ub)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	struct buffer_head *bh;
	int error;
	uint64_t *eablk, *end;

	if (ip->i_di.di_flags & GFS_DIF_EA_INDIRECT) {
		error =
		    gfs_dread(sdp, ip->i_di.di_eattr, ip->i_gl,
			      DIO_WAIT | DIO_START, &bh);
		if (error)
			goto out;

		error = gfs_add_bh_to_ub(ub, bh);

		eablk =
		    (uint64_t *) ((bh)->b_data + sizeof (struct gfs_indirect));
		end =
		    eablk +
		    ((sdp->sd_sb.sb_bsize - sizeof (struct gfs_indirect)) / 8);

		while (*eablk && eablk < end) {
			error =
			    gfs_get_direct_eattr_meta(ip, ub,
						      gfs64_to_cpu(*eablk));
			if (error) {
				brelse(bh);
				goto out;
			}
			eablk++;
		}
		brelse(bh);
	} else
		error = gfs_get_direct_eattr_meta(ip, ub, ip->i_di.di_eattr);

      out:

	return error;
}
