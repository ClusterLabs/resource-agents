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
#include <linux/mm.h>

#include "gfs2.h"
#include "bmap.h"
#include "inode.h"
#include "page.h"
#include "trans.h"

/**
 * gfs2_inval_pte - Sync and invalidate all PTEs associated with a glock
 * @gl: the glock
 *
 */

void
gfs2_inval_pte(struct gfs2_glock *gl)
{
	ENTER(G2FN_INVAL_PTE)
	struct gfs2_inode *ip;
	struct inode *inode;

	ip = get_gl2ip(gl);
	if (!ip || !S_ISREG(ip->i_di.di_mode))
		RET(G2FN_INVAL_PTE);

	if (!test_bit(GIF_PAGED, &ip->i_flags))
		RET(G2FN_INVAL_PTE);

	inode = gfs2_ip2v(ip, NO_CREATE);
	if (inode) {
		unmap_shared_mapping_range(inode->i_mapping, 0, 0);
		iput(inode);

		if (test_bit(GIF_SW_PAGED, &ip->i_flags))
			set_bit(GLF_DIRTY, &gl->gl_flags);
	}

	clear_bit(GIF_SW_PAGED, &ip->i_flags);

	RET(G2FN_INVAL_PTE);
}

/**
 * gfs2_inval_page - Invalidate all pages associated with a glock
 * @gl: the glock
 *
 */

void
gfs2_inval_page(struct gfs2_glock *gl)
{
	ENTER(G2FN_INVAL_PAGE)
	struct gfs2_inode *ip;
	struct inode *inode;

	ip = get_gl2ip(gl);
	if (!ip || !S_ISREG(ip->i_di.di_mode))
		RET(G2FN_INVAL_PAGE);

	inode = gfs2_ip2v(ip, NO_CREATE);
	if (inode) {
		struct address_space *mapping = inode->i_mapping;

		truncate_inode_pages(mapping, 0);
		gfs2_assert_withdraw(ip->i_sbd, !mapping->nrpages);

		iput(inode);
	}

	clear_bit(GIF_PAGED, &ip->i_flags);

	RET(G2FN_INVAL_PAGE);
}

/**
 * gfs2_sync_page_i - Sync the data pages (not metadata) for a struct inode
 * @inode: the inode
 * @flags: DIO_START | DIO_WAIT
 *
 */

void
gfs2_sync_page_i(struct inode *inode, int flags)
{
	ENTER(G2FN_SYNC_PAGE_I)
	struct address_space *mapping = inode->i_mapping;
	int error = 0;

	if (flags & DIO_START)
		error = filemap_fdatawrite(mapping);
	if (!error && (flags & DIO_WAIT))
		error = filemap_fdatawait(mapping);

	/* Find a better way to report this to the user. */
	if (error)
		gfs2_io_error_inode(get_v2ip(inode));

	RET(G2FN_SYNC_PAGE_I);
}

/**
 * gfs2_sync_page - Sync the data pages (not metadata) associated with a glock
 * @gl: the glock
 * @flags: DIO_START | DIO_WAIT
 *
 * Syncs data (not metadata) for a regular file.
 * No-op for all other types.
 */

void
gfs2_sync_page(struct gfs2_glock *gl, int flags)
{
	ENTER(G2FN_SYNC_PAGE)
	struct gfs2_inode *ip;
	struct inode *inode;

	ip = get_gl2ip(gl);
	if (!ip || !S_ISREG(ip->i_di.di_mode))
		RET(G2FN_SYNC_PAGE);

	inode = gfs2_ip2v(ip, NO_CREATE);
	if (inode) {
		gfs2_sync_page_i(inode, flags);
		iput(inode);
	}

	RET(G2FN_SYNC_PAGE);
}

/**
 * gfs2_unstuffer_page - unstuff a stuffed inode into a block cached by a page
 * @ip: the inode
 * @dibh: the dinode buffer
 * @block: the block number that was allocated
 * @private: any locked page held by the caller process
 *
 * Returns: errno
 */

int
gfs2_unstuffer_page(struct gfs2_inode *ip, struct buffer_head *dibh,
		    uint64_t block, void *private)
{
	ENTER(G2FN_UNSTUFFER_PAGE)
       	struct gfs2_sbd *sdp = ip->i_sbd;
	struct inode *inode = ip->i_vnode;
	struct page *page = (struct page *)private;
	struct buffer_head *bh;
	int release = FALSE;

	if (!page || page->index) {
		page = grab_cache_page(inode->i_mapping, 0);
		if (!page)
			RETURN(G2FN_UNSTUFFER_PAGE, -ENOMEM);
		release = TRUE;
	}

	if (!PageUptodate(page)) {
		void *kaddr = kmap(page);

		memcpy(kaddr,
		       dibh->b_data + sizeof(struct gfs2_dinode),
		       ip->i_di.di_size);
		memset(kaddr + ip->i_di.di_size,
		       0,
		       PAGE_CACHE_SIZE - ip->i_di.di_size);
		kunmap(page);

		SetPageUptodate(page);
	}

	if (!page_has_buffers(page))
		create_empty_buffers(page, 1 << inode->i_blkbits,
				     (1 << BH_Uptodate));

	bh = page_buffers(page);

	if (!buffer_mapped(bh))
		map_bh(bh, inode->i_sb, block);
	else if (gfs2_assert_warn(sdp,
				  bh->b_bdev == inode->i_sb->s_bdev &&
				  bh->b_blocknr == block))
                map_bh(bh, inode->i_sb, block);

	set_buffer_uptodate(bh);
	if (sdp->sd_args.ar_data == GFS2_DATA_ORDERED)
		gfs2_trans_add_databuf(sdp, bh);
	mark_buffer_dirty(bh);

	if (release) {
		unlock_page(page);
		page_cache_release(page);
	}

	RETURN(G2FN_UNSTUFFER_PAGE, 0);
}

/**
 * gfs2_truncator_page - truncate a partial data block in the page cache
 * @ip: the inode
 * @size: the size the file should be
 *
 * Returns: errno
 */

int
gfs2_truncator_page(struct gfs2_inode *ip, uint64_t size)
{
	ENTER(G2FN_TRUNCATOR_PAGE)
       	struct gfs2_sbd *sdp = ip->i_sbd;
	struct inode *inode = ip->i_vnode;
	struct page *page;
	struct buffer_head *bh;
	void *kaddr;
	uint64_t lbn, dbn;
	unsigned long index;
	unsigned int offset;
	unsigned int bufnum;
	int not_new = 0;
	int error;

	lbn = size >> inode->i_blkbits;
	error = gfs2_block_map(ip,
			       lbn, &not_new,
			       &dbn, NULL);
	if (error || !dbn)
		RETURN(G2FN_TRUNCATOR_PAGE, error);

	index = size >> PAGE_CACHE_SHIFT;
	offset = size & (PAGE_CACHE_SIZE - 1);
	bufnum = lbn - (index << (PAGE_CACHE_SHIFT - inode->i_blkbits));

	/* Not in a transaction here -- a non-disk-I/O error is ok. */

	page = read_cache_page(inode->i_mapping, index,
			       (filler_t *)inode->i_mapping->a_ops->readpage,
			       NULL);
	if (IS_ERR(page))
		RETURN(G2FN_TRUNCATOR_PAGE, PTR_ERR(page));

	lock_page(page);

	if (!PageUptodate(page) || PageError(page)) {
		error = -EIO;
		goto out;
	}

	kaddr = kmap(page);
	memset(kaddr + offset, 0, PAGE_CACHE_SIZE - offset);
	kunmap(page);

	if (!page_has_buffers(page))
		create_empty_buffers(page, 1 << inode->i_blkbits,
				     (1 << BH_Uptodate));

	for (bh = page_buffers(page); bufnum--; bh = bh->b_this_page)
		/* Do nothing */;

	if (!buffer_mapped(bh))
		map_bh(bh, inode->i_sb, dbn);
	else if (gfs2_assert_warn(sdp,
				  bh->b_bdev == inode->i_sb->s_bdev &&
				  bh->b_blocknr == dbn))
		map_bh(bh, inode->i_sb, dbn);

	set_buffer_uptodate(bh);
	if (sdp->sd_args.ar_data == GFS2_DATA_ORDERED)
		gfs2_trans_add_databuf(sdp, bh);
	mark_buffer_dirty(bh);

 out:
	unlock_page(page);
	page_cache_release(page);

	RETURN(G2FN_TRUNCATOR_PAGE, error);
}

void
gfs2_page_add_databufs(struct gfs2_sbd *sdp, struct page *page,
		       unsigned int from, unsigned int to)
{
	ENTER(G2FN_PAGE_ADD_DATABUFS)
       	struct buffer_head *head = page_buffers(page);
	unsigned int bsize = head->b_size;
	struct buffer_head *bh;
	unsigned int start, end;

	for (bh = head, start = 0;
	     bh != head || !start;
	     bh = bh->b_this_page, start = end) {
		end = start + bsize;
		if (end <= from || start >= to)
			continue;
		gfs2_trans_add_databuf(sdp, bh);
	}

	RET(G2FN_PAGE_ADD_DATABUFS);
}

