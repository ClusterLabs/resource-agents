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

#include "gfs2.h"
#include "bmap.h"
#include "dio.h"
#include "file.h"
#include "inode.h"
#include "trans.h"

/**
 * gfs2_copy2mem - Trivial copy function for gfs2_readi()
 * @bh: The buffer to copy from, or NULL meaning zero the buffer
 * @buf: The buffer to copy/zero
 * @offset: The offset in the buffer to copy from
 * @size: The amount of data to copy/zero
 *
 * Returns: errno
 */

int
gfs2_copy2mem(struct buffer_head *bh, void **buf, unsigned int offset,
	     unsigned int size)
{
	ENTER(G2FN_COPY2MEM)
	char **p = (char **)buf;

	if (bh)
		memcpy(*p, bh->b_data + offset, size);
	else
		memset(*p, 0, size);

	*p += size;

	RETURN(G2FN_COPY2MEM, 0);
}

/**
 * gfs2_copy2user - Copy data to user space
 * @bh: The buffer
 * @buf: The destination of the data
 * @offset: The offset into the buffer
 * @size: The amount of data to copy
 *
 * Returns: errno
 */

int
gfs2_copy2user(struct buffer_head *bh, void **buf,
	      unsigned int offset, unsigned int size)
{
	ENTER(G2FN_COPY2USER)
	char **p = (char **)buf;
	int error;

	if (bh)
		error = copy_to_user(*p, bh->b_data + offset, size);
	else
		error = clear_user(*p, size);

	if (error)
		error = -EFAULT;
	else
		*p += size;

	RETURN(G2FN_COPY2USER, error);
}

/**
 * gfs2_readi - Read a file
 * @ip: The GFS2 Inode
 * @buf: The buffer to place result into
 * @offset: File offset to begin reading from
 * @size: Amount of data to transfer
 * @copy_fn: Function to actually perform the copy
 *
 * The @copy_fn only copies a maximum of a single block at once so
 * we are safe calling it with int arguments. It is done so that
 * we don't needlessly put 64bit arguments on the stack and it
 * also makes the code in the @copy_fn nicer too.
 *
 * Returns: The amount of data actually copied or the error
 */

int
gfs2_readi(struct gfs2_inode *ip, void *buf,
	  uint64_t offset, unsigned int size,
	  read_copy_fn_t copy_fn)
{
	ENTER(G2FN_READI)
	struct gfs2_sbd *sdp = ip->i_sbd;
	uint64_t lblock, dblock;
	uint32_t extlen = 0;
	unsigned int o;
	int journaled = gfs2_is_jdata(ip);
	int copied = 0;
	int error = 0;

	if (offset >= ip->i_di.di_size)
		RETURN(G2FN_READI, 0);

	if ((offset + size) > ip->i_di.di_size)
		size = ip->i_di.di_size - offset;

	if (!size)
		RETURN(G2FN_READI, 0);

	if (journaled) {
		lblock = offset;
		o = do_div(lblock, sdp->sd_jbsize);
	} else {
		lblock = offset >> sdp->sd_sb.sb_bsize_shift;
		o = offset & (sdp->sd_sb.sb_bsize - 1);
	}

	if (gfs2_is_stuffed(ip))
		o += sizeof(struct gfs2_dinode);
	else if (journaled)
		o += sizeof(struct gfs2_meta_header);

	while (copied < size) {
		unsigned int amount;
		struct buffer_head *bh;
		int new;

		amount = size - copied;
		if (amount > sdp->sd_sb.sb_bsize - o)
			amount = sdp->sd_sb.sb_bsize - o;

		if (!extlen) {
			new = FALSE;
			error = gfs2_block_map(ip, lblock, &new,
					      &dblock, &extlen);
			if (error)
				goto fail;
		}

		if (extlen > 1)
			gfs2_start_ra(ip->i_gl, dblock, extlen);

		if (dblock) {
			error = gfs2_get_data_buffer(ip, dblock, new, &bh);
			if (error)
				goto fail;
			dblock++;
			extlen--;
		} else
			bh = NULL;

		error = copy_fn(bh, &buf, o, amount);
		if (bh)
			brelse(bh);
		if (error)
			goto fail;

		copied += amount;
		lblock++;

		o = (journaled) ? sizeof(struct gfs2_meta_header) : 0;
	}

	RETURN(G2FN_READI, copied);

 fail:
	RETURN(G2FN_READI, (copied) ? copied : error);
}

/**
 * gfs2_copy_from_mem - Trivial copy function for gfs2_writei()
 * @ip: The file to write to
 * @bh: The buffer to copy to or clear
 * @buf: The buffer to copy from
 * @offset: The offset in the buffer to write to
 * @size: The amount of data to write
 * @new: Flag indicating that remaining space in the buffer should be zeroed
 *
 * Returns: errno
 */

int
gfs2_copy_from_mem(struct gfs2_inode *ip, struct buffer_head *bh, void **buf,
		  unsigned int offset, unsigned int size, int new)
{
	ENTER(G2FN_COPY_FROM_MEM)
	char **p = (char **)buf;
	int error = 0;

	/* The dinode block always gets journaled */
	if (bh->b_blocknr == ip->i_num.no_addr) {
		if (gfs2_assert_warn(ip->i_sbd, !new))
			RETURN(G2FN_COPY_FROM_MEM, -EIO);
		gfs2_trans_add_bh(ip->i_gl, bh);
		memcpy(bh->b_data + offset, *p, size);

	/* Data blocks for journaled files get written added to the journal */
	} else if (gfs2_is_jdata(ip)) {
		gfs2_trans_add_bh(ip->i_gl, bh);
		memcpy(bh->b_data + offset, *p, size);
		if (new)
			gfs2_buffer_clear_ends(bh, offset, size, TRUE);

	/* Non-journaled data blocks get written to in-place disk blocks */
	} else {
		memcpy(bh->b_data + offset, *p, size);
		if (new)
			gfs2_buffer_clear_ends(bh, offset, size, FALSE);
		error = gfs2_dwrite(ip->i_sbd, bh, DIO_DIRTY);
	}

	if (!error)
		*p += size;

	RETURN(G2FN_COPY_FROM_MEM, error);
}

/**
 * gfs2_copy_from_user - Copy bytes from user space for gfs2_writei()
 * @ip: The file to write to
 * @bh: The buffer to copy to or clear
 * @buf: The buffer to copy from
 * @offset: The offset in the buffer to write to
 * @size: The amount of data to write
 * @new: Flag indicating that remaining space in the buffer should be zeroed
 *
 * Returns: errno
 */

int
gfs2_copy_from_user(struct gfs2_inode *ip, struct buffer_head *bh, void **buf,
		   unsigned int offset, unsigned int size, int new)
{
	ENTER(G2FN_COPY_FROM_USER)
	char **p = (char **)buf;
	int error = 0;

	/* the dinode block always gets journaled */
	if (bh->b_blocknr == ip->i_num.no_addr) {
		if (gfs2_assert_warn(ip->i_sbd, !new))
			RETURN(G2FN_COPY_FROM_USER, -EIO);
		gfs2_trans_add_bh(ip->i_gl, bh);
		if (copy_from_user(bh->b_data + offset, *p, size))
			error = -EFAULT;

	/* Data blocks for journaled files get written added to the journal */
	} else if (gfs2_is_jdata(ip)) {
		gfs2_trans_add_bh(ip->i_gl, bh);
		if (copy_from_user(bh->b_data + offset, *p, size))
			error = -EFAULT;
		if (new) {
			gfs2_buffer_clear_ends(bh, offset, size, TRUE);
			if (error)
				memset(bh->b_data + offset, 0, size);
		}

	/* non-journaled data blocks get written to in-place disk blocks */
	} else {
		if (copy_from_user(bh->b_data + offset, *p, size))
			error = -EFAULT;
		if (error) {
			if (new)
				gfs2_buffer_clear(bh);
			gfs2_dwrite(ip->i_sbd, bh, DIO_DIRTY);
		} else {
			if (new)
				gfs2_buffer_clear_ends(bh, offset, size, FALSE);
			error = gfs2_dwrite(ip->i_sbd, bh, DIO_DIRTY);
		}
	}

	if (!error)
		*p += size;

	RETURN(G2FN_COPY_FROM_USER, error);
}

/**
 * gfs2_writei - Write bytes to a file
 * @ip: The GFS2 inode
 * @buf: The buffer containing information to be written
 * @offset: The file offset to start writing at
 * @size: The amount of data to write
 * @copy_fn: Function to do the actual copying
 *
 * Returns: The number of bytes correctly written or error code
 */

int
gfs2_writei(struct gfs2_inode *ip, void *buf,
	   uint64_t offset, unsigned int size,
	   write_copy_fn_t copy_fn)
{
	ENTER(G2FN_WRITEI)
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct buffer_head *dibh;
	uint64_t lblock, dblock;
	uint32_t extlen = 0;
	unsigned int o;
	int journaled = gfs2_is_jdata(ip);
	unsigned int bsize;
	const uint64_t start = offset;
	int copied = 0;
	int error = 0;

	if (!size)
		RETURN(G2FN_WRITEI, 0);

	if (gfs2_is_stuffed(ip) &&
	    ((start + size) > (sdp->sd_sb.sb_bsize - sizeof(struct gfs2_dinode)))) {
		error = gfs2_unstuff_dinode(ip, gfs2_unstuffer_async, NULL);
		if (error)
			RETURN(G2FN_WRITEI, error);
	}

	if (journaled) {
		lblock = offset;
		o = do_div(lblock, sdp->sd_jbsize);
		bsize = sdp->sd_jbsize;
	} else {
		lblock = offset >> sdp->sd_sb.sb_bsize_shift;
		o = offset & (sdp->sd_sb.sb_bsize - 1);
		bsize = sdp->sd_sb.sb_bsize;
	}

	if (gfs2_is_stuffed(ip))
		o += sizeof(struct gfs2_dinode);
	else if (journaled)
		o += sizeof(struct gfs2_meta_header);

	while (copied < size) {
		unsigned int amount;
		struct buffer_head *bh;
		int new;

		amount = size - copied;
		if (amount > sdp->sd_sb.sb_bsize - o)
			amount = sdp->sd_sb.sb_bsize - o;

		if (!extlen) {
			new = TRUE;
			error = gfs2_block_map(ip, lblock, &new, &dblock, &extlen);
			if (error)
				goto fail;
			error = -EIO;
			if (gfs2_assert_withdraw(sdp, dblock))
				goto fail;
		}

		error = gfs2_get_data_buffer(ip, dblock,
					    (amount == bsize) ? TRUE : new,
					    &bh);
		if (error)
			goto fail;

		error = copy_fn(ip, bh, &buf, o, amount, new);
		brelse(bh);
		if (error)
			goto fail;

		copied += amount;
		lblock++;
		dblock++;
		extlen--;

		o = (journaled) ? sizeof(struct gfs2_meta_header) : 0;
	}

 out:
	error = gfs2_get_inode_buffer(ip, &dibh);
	if (error)
		RETURN(G2FN_WRITEI, error);

	if (ip->i_di.di_size < start + copied)
		ip->i_di.di_size = start + copied;
	ip->i_di.di_mtime = ip->i_di.di_ctime = get_seconds();

	gfs2_trans_add_bh(ip->i_gl, dibh);
	gfs2_dinode_out(&ip->i_di, dibh->b_data);
	brelse(dibh);

	RETURN(G2FN_WRITEI, copied);

 fail:
	if (copied)
		goto out;
	RETURN(G2FN_WRITEI, error);
}
