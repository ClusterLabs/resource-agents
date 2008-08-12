#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <asm/uaccess.h>

#include "gfs.h"
#include "bmap.h"
#include "dio.h"
#include "file.h"
#include "inode.h"
#include "trans.h"

/**
 * gfs_copy2mem - Trivial copy function for gfs_readi()
 * @bh: The buffer to copy from, or NULL meaning zero the buffer
 * @buf: The buffer to copy/zero
 * @offset: The offset in the buffer to copy from
 * @size: The amount of data to copy/zero
 *
 * Returns: errno
 */

int
gfs_copy2mem(struct buffer_head *bh, void **buf, unsigned int offset,
	     unsigned int size)
{
	char **p = (char **)buf;

	if (bh)
		memcpy(*p, bh->b_data + offset, size);
	else
		memset(*p, 0, size);

	*p += size;

	return 0;
}

/**
 * gfs_copy2user - Copy data to user space
 * @bh: The buffer
 * @buf: The destination of the data
 * @offset: The offset into the buffer
 * @size: The amount of data to copy
 *
 * Returns: errno
 */

int
gfs_copy2user(struct buffer_head *bh, void **buf,
	      unsigned int offset, unsigned int size)
{
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

	return error;
}

/**
 * gfs_readi - Read a file
 * @ip: The GFS Inode
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
gfs_readi(struct gfs_inode *ip, void *buf,
	  uint64_t offset, unsigned int size,
	  read_copy_fn_t copy_fn)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	struct buffer_head *bh;
	uint64_t lblock, dblock;
	unsigned int o;
	uint32_t extlen = 0;
	unsigned int amount;
	int not_new = 0;
	int journaled = gfs_is_jdata(ip);
	int copied = 0;
	int error = 0;

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
		lblock = offset >> sdp->sd_sb.sb_bsize_shift;
		o = offset & (sdp->sd_sb.sb_bsize - 1);
	}

	if (gfs_is_stuffed(ip))
		o += sizeof(struct gfs_dinode);
	else if (journaled)
		o += sizeof(struct gfs_meta_header);

	while (copied < size) {
		amount = size - copied;
		if (amount > sdp->sd_sb.sb_bsize - o)
			amount = sdp->sd_sb.sb_bsize - o;

		if (!extlen) {
			if (!gfs_is_stuffed(ip)) {
				error = gfs_block_map(ip, lblock, &not_new,
						      &dblock, &extlen);
				if (error)
					goto fail;
			} else if (!lblock) {
				dblock = ip->i_num.no_addr;
				extlen = 1;
			} else
				dblock = 0;
		}

		if (extlen > 1)
			gfs_start_ra(ip->i_gl, dblock, extlen);

		if (dblock) {
			error = gfs_get_data_buffer(ip, dblock, not_new, &bh);
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

		o = (journaled) ? sizeof(struct gfs_meta_header) : 0;
	}

	return copied;

 fail:
	return (copied) ? copied : error;
}

/**
 * gfs_copy_from_mem - Trivial copy function for gfs_writei()
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
gfs_copy_from_mem(struct gfs_inode *ip, struct buffer_head *bh, void **buf,
		  unsigned int offset, unsigned int size, int new)
{
	char **p = (char **)buf;
	int error = 0;

	/* The dinode block always gets journaled */
	if (bh->b_blocknr == ip->i_num.no_addr) {
		if (gfs_assert_warn(ip->i_sbd, !new))
			return -EIO;
		gfs_trans_add_bh(ip->i_gl, bh);
		memcpy(bh->b_data + offset, *p, size);

	/* Data blocks for journaled files get written added to the journal */
	} else if (gfs_is_jdata(ip)) {
		gfs_trans_add_bh(ip->i_gl, bh);
		memcpy(bh->b_data + offset, *p, size);
		if (new)
			gfs_buffer_clear_ends(bh, offset, size, TRUE);

	/* Non-journaled data blocks get written to in-place disk blocks */
	} else {
		memcpy(bh->b_data + offset, *p, size);
		if (new)
			gfs_buffer_clear_ends(bh, offset, size, FALSE);
		error = gfs_dwrite(ip->i_sbd, bh, DIO_DIRTY);
	}

	if (!error)
		*p += size;

	return error;
}

/**
 * gfs_copy_from_user - Copy bytes from user space for gfs_writei()
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
gfs_copy_from_user(struct gfs_inode *ip, struct buffer_head *bh, void **buf,
		   unsigned int offset, unsigned int size, int new)
{
	char **p = (char **)buf;
	int error = 0;

	/* the dinode block always gets journaled */
	if (bh->b_blocknr == ip->i_num.no_addr) {
		if (gfs_assert_warn(ip->i_sbd, !new))
			return -EIO;
		gfs_trans_add_bh(ip->i_gl, bh);
		if (copy_from_user(bh->b_data + offset, *p, size))
			error = -EFAULT;

	/* Data blocks for journaled files get written added to the journal */
	} else if (gfs_is_jdata(ip)) {
		gfs_trans_add_bh(ip->i_gl, bh);
		if (copy_from_user(bh->b_data + offset, *p, size))
			error = -EFAULT;
		if (new) {
			gfs_buffer_clear_ends(bh, offset, size, TRUE);
			if (error)
				memset(bh->b_data + offset, 0, size);
		}

	/* non-journaled data blocks get written to in-place disk blocks */
	} else {
		if (copy_from_user(bh->b_data + offset, *p, size))
			error = -EFAULT;
		if (error) {
			if (new)
				gfs_buffer_clear(bh);
			gfs_dwrite(ip->i_sbd, bh, DIO_DIRTY);
		} else {
			if (new)
				gfs_buffer_clear_ends(bh, offset, size, FALSE);
			error = gfs_dwrite(ip->i_sbd, bh, DIO_DIRTY);
		}
	}

	if (!error)
		*p += size;

	return error;
}

/**
 * gfs_writei - Write bytes to a file
 * @ip: The GFS inode
 * @buf: The buffer containing information to be written
 * @offset: The file offset to start writing at
 * @size: The amount of data to write
 * @copy_fn: Function to do the actual copying
 *
 * Returns: The number of bytes correctly written or error code
 */

int
gfs_writei(struct gfs_inode *ip, void *buf,
	   uint64_t offset, unsigned int size,
	   write_copy_fn_t copy_fn,
           struct kiocb *iocb)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	struct buffer_head *dibh, *bh;
	uint64_t lblock, dblock;
	unsigned int o;
	uint32_t extlen = 0;
	unsigned int amount;
	int new;
	int journaled = gfs_is_jdata(ip);
	const uint64_t start = offset;
	int copied = 0;
	int error = 0;

	if (!size)
		return 0;

	if (gfs_is_stuffed(ip) &&
	    ((start + size) > (sdp->sd_sb.sb_bsize - sizeof(struct gfs_dinode)))) {
		error = gfs_unstuff_dinode(ip, gfs_unstuffer_async, NULL);
		if (error)
			return error;
	}

	if (journaled) {
		lblock = offset;
		o = do_div(lblock, sdp->sd_jbsize);
	} else {
		lblock = offset >> sdp->sd_sb.sb_bsize_shift;
		o = offset & (sdp->sd_sb.sb_bsize - 1);
	}

	if (gfs_is_stuffed(ip))
		o += sizeof(struct gfs_dinode);
	else if (journaled)
		o += sizeof(struct gfs_meta_header);

	while (copied < size) {
		amount = size - copied;
		if (amount > sdp->sd_sb.sb_bsize - o)
			amount = sdp->sd_sb.sb_bsize - o;

		if (!extlen) {
			if (!gfs_is_stuffed(ip)) {
				new = TRUE;
				error = gfs_block_map(ip, lblock, &new, &dblock, &extlen);
				if (error)
					goto fail;
			} else {
				new = FALSE;
				dblock = ip->i_num.no_addr;
				extlen = 1;
			}
		}

		if (journaled && extlen > 1)
			gfs_start_ra(ip->i_gl, dblock, extlen);

		error = gfs_get_data_buffer(ip, dblock,
					    (amount == sdp->sd_sb.sb_bsize) ? TRUE : new,
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

		o = (journaled) ? sizeof(struct gfs_meta_header) : 0;
	}

 out:
	error = gfs_get_inode_buffer(ip, &dibh);
	if (error)
		return error;

	if (ip->i_di.di_size < start + copied)
		ip->i_di.di_size = start + copied;
	ip->i_di.di_mtime = ip->i_di.di_ctime = get_seconds();

	gfs_trans_add_bh(ip->i_gl, dibh);
	gfs_dinode_out(&ip->i_di, dibh->b_data);
	brelse(dibh);

	return copied;

 fail:
	if (copied)
		goto out;
	return error;
}

/*
 * gfs_zero_blocks - zero out disk blocks via gfs_writei()
 * @ip: The file to write to
 * @bh: The buffer to clear
 * @buf: The pseudo buffer (not used but added to keep interface unchanged)
 * @offset: The offset in the buffer to write to
 * @size: The size to zero out
 * @new: Flag indicating that remaining space in the buffer should be zeroed
 *
 * Returns: 0 on success, -EXXX on failure
 */

int
gfs_zero_blocks(struct gfs_inode *ip, struct buffer_head *bh, void **buf,
                unsigned int offset, unsigned int size, int new)
{
	int error = 0;

	/* The dinode block always gets journaled */
	if (bh->b_blocknr == ip->i_num.no_addr) {
		if (gfs_assert_warn(ip->i_sbd, !new))
			return -EIO;
		gfs_trans_add_bh(ip->i_gl, bh);
		memset((bh)->b_data + offset, 0, size);

	/* Data blocks for journaled files get written added to the journal */
	} else if (gfs_is_jdata(ip)) {
		gfs_trans_add_bh(ip->i_gl, bh);
		memset((bh)->b_data + offset, 0, size);
		if (new)
			gfs_buffer_clear_ends(bh, offset, size, TRUE);

	/* Non-journaled data blocks get written to in-place disk blocks */
	} else {
		memset((bh)->b_data + offset, 0, size);
		if (new)
			gfs_buffer_clear_ends(bh, offset, size, FALSE);
		error = gfs_dwrite(ip->i_sbd, bh, DIO_DIRTY);
	}

	return error;
}

