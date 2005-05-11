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
#include <linux/pagemap.h>

#include "gfs.h"
#include "bmap.h"
#include "dio.h"
#include "file.h"
#include "glock.h"
#include "inode.h"
#include "ops_address.h"
#include "page.h"
#include "quota.h"
#include "trans.h"

/**
 * get_block - Fills in a buffer head with details about a block
 * @inode: The inode
 * @lblock: The block number to look up
 * @bh_result: The buffer head to return the result in
 * @create: Non-zero if we may add block to the file
 *
 * Returns: errno
 */

static int
get_block(struct inode *inode, sector_t lblock, 
	  struct buffer_head *bh_result, int create)
{
	ENTER(GFN_GET_BLOCK)
	struct gfs_inode *ip = vn2ip(inode);
	int new = create;
	uint64_t dblock;
	int error;

	error = gfs_block_map(ip, lblock, &new, &dblock, NULL);
	if (error)
		RETURN(GFN_GET_BLOCK, error);

	if (!dblock)
		RETURN(GFN_GET_BLOCK, 0);

	map_bh(bh_result, inode->i_sb, dblock);
	if (new)
		set_buffer_new(bh_result);

	RETURN(GFN_GET_BLOCK, 0);
}

/**
 * get_block_noalloc - Fills in a buffer head with details about a block
 * @inode: The inode
 * @lblock: The block number to look up
 * @bh_result: The buffer head to return the result in
 * @create: Non-zero if we may add block to the file
 *
 * Returns: errno
 */

static int
get_block_noalloc(struct inode *inode, sector_t lblock,
		  struct buffer_head *bh_result, int create)
{
	ENTER(GFN_GET_BLOCK_NOALLOC)
	int error;

	error = get_block(inode, lblock, bh_result, FALSE);
	if (error)
		RETURN(GFN_GET_BLOCK_NOALLOC, error);

	if (gfs_assert_withdraw(vfs2sdp(inode->i_sb),
				!create || buffer_mapped(bh_result)))
		RETURN(GFN_GET_BLOCK_NOALLOC, -EIO);

	RETURN(GFN_GET_BLOCK_NOALLOC, 0);
}

/**
 * get_blocks - 
 * @inode:
 * @lblock:
 * @max_blocks:
 * @bh_result:
 * @create:
 *
 * Returns: errno
 */

static int
get_blocks(struct inode *inode, sector_t lblock,
	   unsigned long max_blocks,
	   struct buffer_head *bh_result, int create)
{
	ENTER(GFN_GET_BLOCKS)
	struct gfs_inode *ip = vn2ip(inode);
	int new = create;
	uint64_t dblock;
	uint32_t extlen;
	int error;

	error = gfs_block_map(ip, lblock, &new, &dblock, &extlen);
	if (error)
		RETURN(GFN_GET_BLOCKS, error);

	if (!dblock)
		RETURN(GFN_GET_BLOCKS, 0);

	map_bh(bh_result, inode->i_sb, dblock);
	if (new)
		set_buffer_new(bh_result);

	if (extlen > max_blocks)
		extlen = max_blocks;
	bh_result->b_size = extlen << inode->i_blkbits;

	RETURN(GFN_GET_BLOCKS, 0);
}

/**
 * get_blocks_noalloc - 
 * @inode:
 * @lblock:
 * @max_blocks:
 * @bh_result:
 * @create:
 *
 * Returns: errno
 */

static int
get_blocks_noalloc(struct inode *inode, sector_t lblock,
		   unsigned long max_blocks,
		   struct buffer_head *bh_result, int create)
{
	ENTER(GFN_GET_BLOCKS_NOALLOC)
	int error;

	error = get_blocks(inode, lblock, max_blocks, bh_result, FALSE);
	if (error)
		RETURN(GFN_GET_BLOCKS_NOALLOC, error);

	if (gfs_assert_withdraw(vfs2sdp(inode->i_sb),
				!create || buffer_mapped(bh_result)))
		RETURN(GFN_GET_BLOCKS_NOALLOC, -EIO);

	RETURN(GFN_GET_BLOCKS_NOALLOC, 0);
}

/**
 * gfs_writepage - Write complete page
 * @page: Page to write
 *
 * Returns: errno
 *
 * Use Linux VFS block_write_full_page() to write one page,
 *   using GFS's get_block_noalloc to find which blocks to write.
 */

static int
gfs_writepage(struct page *page, struct writeback_control *wbc)
{
	ENTER(GFN_WRITEPAGE)
	struct gfs_inode *ip = vn2ip(page->mapping->host);
	struct gfs_sbd *sdp = ip->i_sbd;
	int error;

	atomic_inc(&sdp->sd_ops_address);

	if (gfs_assert_withdraw(sdp, gfs_glock_is_held_excl(ip->i_gl))) {
		unlock_page(page);
		RETURN(GFN_WRITEPAGE, -EIO);
	}
	if (current_transaction) {
		redirty_page_for_writepage(wbc, page);
		unlock_page(page);
		RETURN(GFN_WRITEPAGE, 0);
	}

	error = block_write_full_page(page, get_block_noalloc, wbc);

	gfs_flush_meta_cache(ip);

	RETURN(GFN_WRITEPAGE, error);
}

/**
 * stuffed_readpage - Fill in a Linux page with stuffed file data
 * @ip: the inode
 * @page: the page
 *
 * Returns: errno
 */

static int
stuffed_readpage(struct gfs_inode *ip, struct page *page)
{
	ENTER(GFN_STUFFED_READPAGE)
	struct buffer_head *dibh;
	void *kaddr;
	int error;

	error = gfs_get_inode_buffer(ip, &dibh);
	if (!error) {
		kaddr = kmap(page);
		memcpy((char *)kaddr,
		       dibh->b_data + sizeof(struct gfs_dinode),
		       ip->i_di.di_size);
		memset((char *)kaddr + ip->i_di.di_size,
		       0,
		       PAGE_CACHE_SIZE - ip->i_di.di_size);
		kunmap(page);

		brelse(dibh);

		SetPageUptodate(page);
	}

	RETURN(GFN_STUFFED_READPAGE, error);
}

/**
 * readi_readpage - readpage that goes through gfs_internal_read()
 * @page: The page to read
 *
 * Returns: errno
 */

static int
readi_readpage(struct page *page)
{
	ENTER(GFN_READI_READPAGE)
	struct gfs_inode *ip = vn2ip(page->mapping->host);
	void *kaddr;
	int ret;

	kaddr = kmap(page);

	ret = gfs_internal_read(ip, kaddr,
				(uint64_t)page->index << PAGE_CACHE_SHIFT,
				PAGE_CACHE_SIZE);
	if (ret >= 0) {
		if (ret < PAGE_CACHE_SIZE)
			memset(kaddr + ret, 0, PAGE_CACHE_SIZE - ret);
		SetPageUptodate(page);
		ret = 0;
	}

	kunmap(page);

	unlock_page(page);

	RETURN(GFN_READI_READPAGE, ret);
}

/**
 * gfs_readpage - readpage with locking
 * @file: The file to read a page for
 * @page: The page to read
 *
 * Returns: errno
 */

static int
gfs_readpage(struct file *file, struct page *page)
{
	ENTER(GFN_READPAGE)
	struct gfs_inode *ip = vn2ip(page->mapping->host);
	struct gfs_sbd *sdp = ip->i_sbd;
	int error;

	atomic_inc(&sdp->sd_ops_address);

	if (gfs_assert_warn(sdp, gfs_glock_is_locked_by_me(ip->i_gl))) {
		unlock_page(page);
		RETURN(GFN_READPAGE, -ENOSYS);
	}

	if (!gfs_is_jdata(ip)) {
		if (gfs_is_stuffed(ip) && !page->index) {
			error = stuffed_readpage(ip, page);
			unlock_page(page);
		} else
			error = block_read_full_page(page, get_block);
	} else
		error = readi_readpage(page);

	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;

	RETURN(GFN_READPAGE, error);
}

/**
 * gfs_prepare_write - Prepare to write a page to a file
 * @file: The file to write to
 * @page: The page which is to be prepared for writing
 * @from: From (byte range within page)
 * @to: To (byte range within page)
 *
 * Returns: errno
 *
 * Make sure file's inode is glocked; we shouldn't write without that!
 * If GFS dinode is currently stuffed (small enough that all data fits within
 *   the dinode block), and new file size is too large, unstuff it.
 * Use Linux VFS block_prepare_write() to write blocks, using GFS' get_block()
 *   to find which blocks to write.
 */

static int
gfs_prepare_write(struct file *file, struct page *page,
		  unsigned from, unsigned to)
{
	ENTER(GFN_PREPARE_WRITE)
	struct gfs_inode *ip = vn2ip(page->mapping->host);
	struct gfs_sbd *sdp = ip->i_sbd;
	int error = 0;

	atomic_inc(&sdp->sd_ops_address);

	if (gfs_assert_warn(sdp, gfs_glock_is_locked_by_me(ip->i_gl)))
		RETURN(GFN_PREPARE_WRITE, -ENOSYS);

	if (gfs_is_stuffed(ip)) {
		uint64_t file_size = ((uint64_t)page->index << PAGE_CACHE_SHIFT) + to;

		if (file_size > sdp->sd_sb.sb_bsize - sizeof(struct gfs_dinode)) {
			error = gfs_unstuff_dinode(ip, gfs_unstuffer_page, page);
			if (!error)
				error = block_prepare_write(page, from, to, get_block);
		} else if (!PageUptodate(page))
			error = stuffed_readpage(ip, page);
	} else
		error = block_prepare_write(page, from, to, get_block);

	RETURN(GFN_PREPARE_WRITE, error);
}

/**
 * gfs_commit_write - Commit write to a file
 * @file: The file to write to
 * @page: The page containing the data
 * @from: From (byte range within page)
 * @to: To (byte range within page)
 *
 * Returns: errno
 */

static int
gfs_commit_write(struct file *file, struct page *page,
		 unsigned from, unsigned to)
{
	ENTER(GFN_COMMIT_WRITE)
	struct inode *inode = page->mapping->host;
	struct gfs_inode *ip = vn2ip(inode);
	struct gfs_sbd *sdp = ip->i_sbd;
	int error;

	atomic_inc(&sdp->sd_ops_address);

	if (gfs_is_stuffed(ip)) {
		struct buffer_head *dibh;
		uint64_t file_size = ((uint64_t)page->index << PAGE_CACHE_SHIFT) + to;
		void *kaddr;

		error = gfs_get_inode_buffer(ip, &dibh);
		if (error)
			goto fail;

		gfs_trans_add_bh(ip->i_gl, dibh);

		kaddr = kmap(page);
		memcpy(dibh->b_data + sizeof(struct gfs_dinode) + from,
		       (char *)kaddr + from,
		       to - from);
		kunmap(page);

		brelse(dibh);

		SetPageUptodate(page);

		if (inode->i_size < file_size)
			i_size_write(inode, file_size);
	} else {
		error = generic_commit_write(file, page, from, to);
		if (error)
			goto fail;
	}

	RETURN(GFN_COMMIT_WRITE, 0);

 fail:
	ClearPageUptodate(page);

	RETURN(GFN_COMMIT_WRITE, error);
}

/**
 * gfs_bmap - Block map function
 * @mapping: Address space info
 * @lblock: The block to map
 *
 * Returns: The disk address for the block or 0 on hole or error
 */

static sector_t
gfs_bmap(struct address_space *mapping, sector_t lblock)
{
	ENTER(GFN_BMAP)
	struct gfs_inode *ip = vn2ip(mapping->host);
	struct gfs_holder i_gh;
	int dblock = 0;
	int error;

	atomic_inc(&ip->i_sbd->sd_ops_address);

	error = gfs_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY, &i_gh);
	if (error)
		RETURN(GFN_BMAP, 0);

	if (!gfs_is_stuffed(ip))
		dblock = generic_block_bmap(mapping, lblock, get_block);

	gfs_glock_dq_uninit(&i_gh);

	RETURN(GFN_BMAP, dblock);
}

/**
 * gfs_direct_IO - 
 * @rw:
 * @iocb:
 * @iov:
 * @offset:
 * @nr_segs:
 *
 * Returns: errno
 */

static int
gfs_direct_IO(int rw, struct kiocb *iocb, const struct iovec *iov,
	      loff_t offset, unsigned long nr_segs)
{
	ENTER(GFN_DIRECT_IO)
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	struct gfs_inode *ip = vn2ip(inode);
	struct gfs_sbd *sdp = ip->i_sbd;
	get_blocks_t *gb = get_blocks;

	atomic_inc(&sdp->sd_ops_address);

	if (gfs_assert_warn(sdp, gfs_glock_is_locked_by_me(ip->i_gl)) ||
	    gfs_assert_warn(sdp, !gfs_is_stuffed(ip)))
		RETURN(GFN_DIRECT_IO, -EINVAL);

	if (rw == WRITE && !current_transaction)
		gb = get_blocks_noalloc;

	RETURN(GFN_DIRECT_IO,
	       blockdev_direct_IO(rw, iocb, inode,
				  inode->i_sb->s_bdev, iov,
				  offset, nr_segs, gb, NULL));
}

struct address_space_operations gfs_file_aops = {
	.writepage = gfs_writepage,
	.readpage = gfs_readpage,
	.sync_page = block_sync_page,
	.prepare_write = gfs_prepare_write,
	.commit_write = gfs_commit_write,
	.bmap = gfs_bmap,
	.direct_IO = gfs_direct_IO,
};
