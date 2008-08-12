#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/pagemap.h>
#include <linux/mm.h>

#include "gfs.h"
#include "bmap.h"
#include "inode.h"
#include "page.h"

/**
 * gfs_inval_pte - Sync and invalidate all PTEs associated with a glock
 * @gl: the glock
 *
 */

void
gfs_inval_pte(struct gfs_glock *gl)
{
	struct gfs_inode *ip;
	struct inode *inode;

	ip = get_gl2ip(gl);
	if (!ip ||
	    ip->i_di.di_type != GFS_FILE_REG)
		return;

	if (!test_bit(GIF_PAGED, &ip->i_flags))
		return;

	inode = gfs_iget(ip, NO_CREATE);
	if (inode) {
		unmap_shared_mapping_range(inode->i_mapping, 0, 0);
		iput(inode);

		if (test_bit(GIF_SW_PAGED, &ip->i_flags))
			set_bit(GLF_DIRTY, &gl->gl_flags);
	}

	clear_bit(GIF_SW_PAGED, &ip->i_flags);
}

/**
 * gfs_inval_page - Invalidate all pages associated with a glock
 * @gl: the glock
 *
 */

void
gfs_inval_page(struct gfs_glock *gl)
{
	struct gfs_inode *ip;
	struct inode *inode;

	ip = get_gl2ip(gl);
	if (!ip ||
	    ip->i_di.di_type != GFS_FILE_REG)
		return;

	inode = gfs_iget(ip, NO_CREATE);
	if (inode) {
		struct address_space *mapping = inode->i_mapping;

		truncate_inode_pages(mapping, 0);
		gfs_assert_withdraw(ip->i_sbd, !mapping->nrpages);

		iput(inode);
	}

	clear_bit(GIF_PAGED, &ip->i_flags);
}

/**
 * gfs_sync_page_i - Sync the data pages (not metadata) for a struct inode
 * @inode: the inode
 * @flags: DIO_START | DIO_WAIT
 *
 */

void
gfs_sync_page_i(struct inode *inode, int flags)
{
	struct address_space *mapping = inode->i_mapping;
	int error = 0;

	if (flags & DIO_START)
		error = filemap_fdatawrite(mapping);
	if (!error && (flags & DIO_WAIT))
		error = filemap_fdatawait(mapping);

	/* Find a better way to report this to the user. */
	if (error)
		gfs_io_error_inode(get_v2ip(inode));
}

/**
 * gfs_sync_page - Sync the data pages (not metadata) associated with a glock
 * @gl: the glock
 * @flags: DIO_START | DIO_WAIT
 *
 * Syncs data (not metadata) for a regular file.
 * No-op for all other types.
 */

void
gfs_sync_page(struct gfs_glock *gl, int flags)
{
	struct gfs_inode *ip;
	struct inode *inode;

	ip = get_gl2ip(gl);
	if (!ip ||
	    ip->i_di.di_type != GFS_FILE_REG)
		return;

	inode = gfs_iget(ip, NO_CREATE);
	if (inode) {
		gfs_sync_page_i(inode, flags);
		iput(inode);
	}
}

/**
 * gfs_unstuffer_page - unstuff a stuffed inode into a block cached by a page
 * @ip: the inode
 * @dibh: the dinode buffer
 * @block: the block number that was allocated
 * @private: any locked page held by the caller process
 *
 * Returns: errno
 */

int
gfs_unstuffer_page(struct gfs_inode *ip, struct buffer_head *dibh,
		   uint64_t block, void *private)
{
	struct inode *inode = ip->i_vnode;
	struct page *page = (struct page *)private;
	struct buffer_head *bh;
	int release = FALSE;

	if (!page || page->index) {
		page = grab_cache_page(inode->i_mapping, 0);
		if (!page)
			return -ENOMEM;
		release = TRUE;
	}

	if (!PageUptodate(page)) {
		void *kaddr = kmap(page);

		memcpy(kaddr,
		       dibh->b_data + sizeof(struct gfs_dinode),
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
	else if (gfs_assert_warn(ip->i_sbd,
				 bh->b_bdev == inode->i_sb->s_bdev &&
				 bh->b_blocknr == block))
                map_bh(bh, inode->i_sb, block);

	set_buffer_uptodate(bh);
	mark_buffer_dirty(bh);

	if (release) {
		unlock_page(page);
		page_cache_release(page);
	}

	return 0;
}

/**
 * gfs_truncator_page - truncate a partial data block in the page cache
 * @ip: the inode
 * @size: the size the file should be
 *
 * Returns: errno
 */

int
gfs_truncator_page(struct gfs_inode *ip, uint64_t size)
{
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
	error = gfs_block_map(ip,
			      lbn, &not_new,
			      &dbn, NULL);
	if (error || !dbn)
		return error;

	index = size >> PAGE_CACHE_SHIFT;
	offset = size & (PAGE_CACHE_SIZE - 1);
	bufnum = lbn - (index << (PAGE_CACHE_SHIFT - inode->i_blkbits));

	/* Not in a transaction here -- a non-disk-I/O error is ok. */

	page = read_cache_page(inode->i_mapping, index,
			       (filler_t *)inode->i_mapping->a_ops->readpage,
			       NULL);
	if (IS_ERR(page))
		return PTR_ERR(page);

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
	else if (gfs_assert_warn(ip->i_sbd,
				 bh->b_bdev == inode->i_sb->s_bdev &&
				 bh->b_blocknr == dbn))
		map_bh(bh, inode->i_sb, dbn);

	set_buffer_uptodate(bh);
	mark_buffer_dirty(bh);

 out:
	unlock_page(page);
	page_cache_release(page);

	return error;
}
