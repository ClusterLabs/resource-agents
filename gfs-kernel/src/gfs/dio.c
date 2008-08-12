#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>

#include "gfs.h"
#include "dio.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "log.h"
#include "lops.h"
#include "rgrp.h"
#include "trans.h"

#define buffer_busy(bh) ((bh)->b_state & ((1ul << BH_Dirty) | (1ul << BH_Lock)))

/**
 * aspace_get_block - 
 * @inode:
 * @lblock:
 * @bh_result:
 * @create:
 *
 * Returns: errno
 */

static int
aspace_get_block(struct inode *inode, sector_t lblock,
		 struct buffer_head *bh_result, int create)
{
	gfs_assert_warn(get_v2sdp(inode->i_sb), FALSE);
	return -ENOSYS;
}

/**
 * gfs_aspace_writepage - write an aspace page
 * @page: the page
 * @wbc:
 *
 * Returns: errno
 */

static int 
gfs_aspace_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, aspace_get_block, wbc);
}

/**
 * stuck_releasepage - We're stuck in gfs_releasepage().  Print stuff out.
 * @bh: the buffer we're stuck on
 *
 */

static void
stuck_releasepage(struct buffer_head *bh)
{
	struct gfs_sbd *sdp = get_v2sdp(bh->b_page->mapping->host->i_sb);
	struct gfs_bufdata *bd = get_v2bd(bh);

	printk("GFS: fsid=%s: stuck in gfs_releasepage()...\n", sdp->sd_fsname);
	printk("GFS: fsid=%s: blkno = %"PRIu64", bh->b_count = %d\n",
	       sdp->sd_fsname,
	       (uint64_t)bh->b_blocknr,
	       atomic_read(&bh->b_count));
	printk("GFS: fsid=%s: get_v2bd(bh) = %s\n",
	       sdp->sd_fsname,
	       (bd) ? "!NULL" : "NULL");

	if (bd) {
		struct gfs_glock *gl = bd->bd_gl;

		printk("GFS: fsid=%s: gl = (%u, %"PRIu64")\n",
		       sdp->sd_fsname,
		       gl->gl_name.ln_type,
		       gl->gl_name.ln_number);

		printk("GFS: fsid=%s: bd_new_le.le_trans = %s\n",
		       sdp->sd_fsname,
		       (bd->bd_new_le.le_trans) ? "!NULL" : "NULL");
		printk("GFS: fsid=%s: bd_incore_le.le_trans = %s\n",
		       sdp->sd_fsname,
		       (bd->bd_incore_le.le_trans) ? "!NULL" : "NULL");
		printk("GFS: fsid=%s: bd_frozen = %s\n",
		       sdp->sd_fsname,
		       (bd->bd_frozen) ? "!NULL" : "NULL");
		printk("GFS: fsid=%s: bd_pinned = %u\n",
		       sdp->sd_fsname, bd->bd_pinned);
		printk("GFS: fsid=%s: bd_ail_tr_list = %s\n",
		       sdp->sd_fsname,
		       (list_empty(&bd->bd_ail_tr_list)) ? "Empty" : "!Empty");

		if (gl->gl_ops == &gfs_inode_glops) {
			struct gfs_inode *ip = get_gl2ip(gl);

			if (ip) {
				unsigned int x;

				printk("GFS: fsid=%s: ip = %"PRIu64"/%"PRIu64"\n",
				       sdp->sd_fsname,
				       ip->i_num.no_formal_ino,
				       ip->i_num.no_addr);
				printk("GFS: fsid=%s: ip->i_count = %d, ip->i_vnode = %s\n",
				     sdp->sd_fsname,
				     atomic_read(&ip->i_count),
				     (ip->i_vnode) ? "!NULL" : "NULL");
				for (x = 0; x < GFS_MAX_META_HEIGHT; x++)
					printk("GFS: fsid=%s: ip->i_cache[%u] = %s\n",
					       sdp->sd_fsname, x,
					       (ip->i_cache[x]) ? "!NULL" : "NULL");
			}
		}
	}
}

/**
 * gfs_aspace_releasepage - free the metadata associated with a page 
 * @page: the page that's being released
 * @gfp_mask: passed from Linux VFS, ignored by us
 *
 * Call try_to_free_buffers() if the buffers in this page can be
 * released.
 *
 * Returns: 0
 */

static int
gfs_aspace_releasepage(struct page *page, gfp_t gfp_mask)
{
	struct inode *aspace = page->mapping->host;
	struct gfs_sbd *sdp = get_v2sdp(aspace->i_sb);
	struct buffer_head *bh, *head;
	struct gfs_bufdata *bd;
	unsigned long t;

	if (!page_has_buffers(page))
		goto out;

	head = bh = page_buffers(page);
	do {
		t = jiffies;

		while (atomic_read(&bh->b_count)) {
			if (atomic_read(&aspace->i_writecount)) {
				if (time_after_eq(jiffies,
						  t +
						  gfs_tune_get(sdp, gt_stall_secs) * HZ)) {
					stuck_releasepage(bh);
					t = jiffies;
				}

				yield();
				continue;
			}

			return 0;
		}

		bd = get_v2bd(bh);
		if (bd) {
			gfs_assert_warn(sdp, bd->bd_bh == bh);
			gfs_assert_warn(sdp, !bd->bd_new_le.le_trans);
		        gfs_assert_warn(sdp, !bd->bd_incore_le.le_trans);
			gfs_assert_warn(sdp, !bd->bd_frozen);
			gfs_assert_warn(sdp, !bd->bd_pinned);
			gfs_assert_warn(sdp, list_empty(&bd->bd_ail_tr_list));
			kmem_cache_free(gfs_bufdata_cachep, bd);
			atomic_dec(&sdp->sd_bufdata_count);
			set_v2bd(bh, NULL);
		}

		bh = bh->b_this_page;
	}
	while (bh != head);

 out:
	return try_to_free_buffers(page);
}

static struct address_space_operations aspace_aops = {
	.writepage = gfs_aspace_writepage,
	.releasepage = gfs_aspace_releasepage,
};

/**
 * gfs_aspace_get - Create and initialize a struct inode structure
 * @sdp: the filesystem the aspace is in
 *
 * Right now a struct inode is just a struct inode.  Maybe Linux
 * will supply a more lightweight address space construct (that works)
 * in the future.
 *
 * Make sure pages/buffers in this aspace aren't in high memory.
 *
 * Returns: the aspace
 */

struct inode *
gfs_aspace_get(struct gfs_sbd *sdp)
{
	struct inode *aspace;

	aspace = new_inode(sdp->sd_vfs);
	if (aspace) {
		mapping_set_gfp_mask(aspace->i_mapping, GFP_KERNEL);
		aspace->i_mapping->a_ops = &aspace_aops;
		aspace->i_size = ~0ULL;
		set_v2ip(aspace, NULL);
		insert_inode_hash(aspace);
	}

	return aspace;
}

/**
 * gfs_aspace_put - get rid of an aspace
 * @aspace:
 *
 */

void
gfs_aspace_put(struct inode *aspace)
{
	remove_inode_hash(aspace);
	iput(aspace);
}

/**
 * gfs_ail_start_trans - Start I/O on a part of the AIL
 * @sdp: the filesystem
 * @tr: the part of the AIL
 *
 */

void
gfs_ail_start_trans(struct gfs_sbd *sdp, struct gfs_trans *tr)
{
	struct list_head *head, *tmp, *prev;
	struct gfs_bufdata *bd;
	struct buffer_head *bh;
	int retry;

	do {
		retry = FALSE;

		spin_lock(&sdp->sd_ail_lock);

		for (head = &tr->tr_ail_bufs, tmp = head->prev, prev = tmp->prev;
		     tmp != head;
		     tmp = prev, prev = tmp->prev) {
			bd = list_entry(tmp, struct gfs_bufdata, bd_ail_tr_list);
			bh = bd->bd_bh;

			if (gfs_trylock_buffer(bh))
				continue;

			if (bd->bd_pinned) {
				gfs_unlock_buffer(bh);
				continue;
			}

			if (!buffer_busy(bh)) {
				if (!buffer_uptodate(bh))
					gfs_io_error_bh(sdp, bh);

				list_del_init(&bd->bd_ail_tr_list);
				list_del(&bd->bd_ail_gl_list);

				gfs_unlock_buffer(bh);
				brelse(bh);
				continue;
			}

			if (buffer_dirty(bh)) {
				list_move(&bd->bd_ail_tr_list, head);

				spin_unlock(&sdp->sd_ail_lock);
				wait_on_buffer(bh);
				ll_rw_block(WRITE, 1, &bh);
				spin_lock(&sdp->sd_ail_lock);

				gfs_unlock_buffer(bh);
				retry = TRUE;
				break;
			}

			gfs_unlock_buffer(bh);
		}

		spin_unlock(&sdp->sd_ail_lock);
	} while (retry);
}

/**
 * gfs_ail_empty_trans - Check whether or not a trans in the AIL has been synced
 * @sdp: the filesystem
 * @tr: the transaction
 *
 */

int
gfs_ail_empty_trans(struct gfs_sbd *sdp, struct gfs_trans *tr)
{
	struct list_head *head, *tmp, *prev;
	struct gfs_bufdata *bd;
	struct buffer_head *bh;
	int ret;

	spin_lock(&sdp->sd_ail_lock);

	for (head = &tr->tr_ail_bufs, tmp = head->prev, prev = tmp->prev;
	     tmp != head;
	     tmp = prev, prev = tmp->prev) {
		bd = list_entry(tmp, struct gfs_bufdata, bd_ail_tr_list);
		bh = bd->bd_bh;

		if (gfs_trylock_buffer(bh))
			continue;

		if (bd->bd_pinned || buffer_busy(bh)) {
			gfs_unlock_buffer(bh);
			continue;
		}

		if (!buffer_uptodate(bh))
			gfs_io_error_bh(sdp, bh);

		list_del_init(&bd->bd_ail_tr_list);
		list_del(&bd->bd_ail_gl_list);

		gfs_unlock_buffer(bh);
		brelse(bh);
	}

	ret = list_empty(head);

	spin_unlock(&sdp->sd_ail_lock);

	return ret;
}

/**
 * ail_empty_gl - remove all buffers for a given lock from the AIL
 * @gl: the glock
 *
 * None of the buffers should be dirty, locked, or pinned.
 */

static void
ail_empty_gl(struct gfs_glock *gl)
{
	struct gfs_sbd *sdp = gl->gl_sbd;
	struct gfs_bufdata *bd;
	struct buffer_head *bh;

	spin_lock(&sdp->sd_ail_lock);

	while (!list_empty(&gl->gl_ail_bufs)) {
		bd = list_entry(gl->gl_ail_bufs.next,
				struct gfs_bufdata, bd_ail_gl_list);
		bh = bd->bd_bh;

		gfs_assert_withdraw(sdp, !bd->bd_pinned && !buffer_busy(bh));
		if (!buffer_uptodate(bh))
			gfs_io_error_bh(sdp, bh);

		list_del_init(&bd->bd_ail_tr_list);
		list_del(&bd->bd_ail_gl_list);

		brelse(bh);
	}

	spin_unlock(&sdp->sd_ail_lock);
}

/**
 * gfs_inval_buf - Invalidate all buffers associated with a glock
 * @gl: the glock
 *
 */

void
gfs_inval_buf(struct gfs_glock *gl)
{
	struct inode *aspace = gl->gl_aspace;
	struct address_space *mapping = gl->gl_aspace->i_mapping;

	ail_empty_gl(gl);

	atomic_inc(&aspace->i_writecount);
	truncate_inode_pages(mapping, 0);
	atomic_dec(&aspace->i_writecount);

	gfs_assert_withdraw(gl->gl_sbd, !mapping->nrpages);
}

/**
 * gfs_sync_buf - Sync all buffers associated with a glock
 * @gl: The glock
 * @flags: DIO_START | DIO_WAIT | DIO_CHECK
 *
 */

void
gfs_sync_buf(struct gfs_glock *gl, int flags)
{
	struct address_space *mapping = gl->gl_aspace->i_mapping;
	int error = 0;

	if (flags & DIO_START)
		error = filemap_fdatawrite(mapping);
	if (!error && (flags & DIO_WAIT))
		error = filemap_fdatawait(mapping);
	if (!error && (flags & (DIO_INVISIBLE | DIO_CHECK)) == DIO_CHECK)
		ail_empty_gl(gl);

	if (error)
		gfs_io_error(gl->gl_sbd);
}

/**
 * getbuf - Get a buffer with a given address space
 * @sdp: the filesystem
 * @aspace: the address space
 * @blkno: the block number (filesystem scope)
 * @create: TRUE if the buffer should be created
 *
 * Returns: the buffer
 */

static struct buffer_head *
getbuf(struct gfs_sbd *sdp, struct inode *aspace, uint64_t blkno, int create)
{
	struct page *page;
	struct buffer_head *bh;
	unsigned int shift;
	unsigned long index;
	unsigned int bufnum;

	shift = PAGE_CACHE_SHIFT - sdp->sd_sb.sb_bsize_shift;
	index = blkno >> shift;             /* convert block to page */
	bufnum = blkno - (index << shift);  /* block buf index within page */

	if (create) {
		RETRY_MALLOC(page = grab_cache_page(aspace->i_mapping, index), page);
	} else {
		page = find_lock_page(aspace->i_mapping, index);
		if (!page)
			return NULL;
	}

	if (!page_has_buffers(page))
		create_empty_buffers(page, sdp->sd_sb.sb_bsize, 0);

	/* Locate header for our buffer within our page */
	for (bh = page_buffers(page); bufnum--; bh = bh->b_this_page)
		/* Do nothing */;
	get_bh(bh);

	if (!buffer_mapped(bh))
		map_bh(bh, sdp->sd_vfs, blkno);
	else if (gfs_assert_warn(sdp, bh->b_bdev == sdp->sd_vfs->s_bdev &&
				 bh->b_blocknr == blkno))
		map_bh(bh, sdp->sd_vfs, blkno);

	unlock_page(page);
	page_cache_release(page);

	return bh;
}

/**
 * gfs_dgetblk - Get a block
 * @gl: The glock associated with this block
 * @blkno: The block number
 *
 * Returns: The buffer
 */

struct buffer_head *
gfs_dgetblk(struct gfs_glock *gl, uint64_t blkno)
{
	return getbuf(gl->gl_sbd, gl->gl_aspace, blkno, CREATE);
}

/**
 * gfs_dread - Read a block from disk
 * @gl: The glock covering the block
 * @blkno: The block number
 * @flags: flags to gfs_dreread()
 * @bhp: the place where the buffer is returned (NULL on failure)
 *
 * Returns: errno
 */

int
gfs_dread(struct gfs_glock *gl, uint64_t blkno,
	  int flags, struct buffer_head **bhp)
{
	int error;

	*bhp = gfs_dgetblk(gl, blkno);
	error = gfs_dreread(gl->gl_sbd, *bhp, flags);
	if (error)
		brelse(*bhp);

	return error;
}

/**
 * gfs_prep_new_buffer - Mark a new buffer we just gfs_dgetblk()ed uptodate
 * @bh: the buffer
 *
 */

void
gfs_prep_new_buffer(struct buffer_head *bh)
{
	wait_on_buffer(bh);
	clear_buffer_dirty(bh);
	set_buffer_uptodate(bh);
}

/**
 * gfs_dreread - Reread a block from disk
 * @sdp: the filesystem
 * @bh: The block to read
 * @flags: Flags that control the read
 *
 * Returns: errno
 */

int
gfs_dreread(struct gfs_sbd *sdp, struct buffer_head *bh, int flags)
{
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		return -EIO;

	/* Fill in meta-header if we have a cached copy, else read from disk */
	if (flags & DIO_NEW) {
		if (gfs_mhc_fish(sdp, bh))
			return 0;
		clear_buffer_uptodate(bh);
	}

	if (flags & DIO_FORCE)
		clear_buffer_uptodate(bh);

	if ((flags & DIO_START) && !buffer_uptodate(bh))
		ll_rw_block(READ, 1, &bh);

	if (flags & DIO_WAIT) {
		wait_on_buffer(bh);

		if (!buffer_uptodate(bh)) {
			gfs_io_error_bh(sdp, bh);
			return -EIO;
		}
		if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
			return -EIO;
	}

	return 0;
}

/**
 * gfs_dwrite - Write a buffer to disk (and/or wait for write to complete)
 * @sdp: the filesystem
 * @bh: The buffer to write
 * @flags:  DIO_XXX The type of write/wait operation to do
 *
 * Returns: errno
 */

int
gfs_dwrite(struct gfs_sbd *sdp, struct buffer_head *bh, int flags)
{
	if (gfs_assert_warn(sdp, !test_bit(SDF_ROFS, &sdp->sd_flags)))
		return -EIO;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		return -EIO;

	if (flags & DIO_CLEAN) {
		lock_buffer(bh);
		clear_buffer_dirty(bh);
		unlock_buffer(bh);
	}

	if (flags & DIO_DIRTY) {
		if (gfs_assert_warn(sdp, buffer_uptodate(bh)))
			return -EIO;
		mark_buffer_dirty(bh);
	}

	if ((flags & DIO_START) && buffer_dirty(bh)) {
		wait_on_buffer(bh);
		ll_rw_block(WRITE, 1, &bh);
	}

	if (flags & DIO_WAIT) {
		wait_on_buffer(bh);

		if (!buffer_uptodate(bh) || buffer_dirty(bh)) {
			gfs_io_error_bh(sdp, bh);
			return -EIO;
		}
		if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
			return -EIO;
	}

	return 0;
}

/**
 * gfs_attach_bufdata - attach a struct gfs_bufdata structure to a buffer
 * @bh: The buffer to be attached to
 * @gl: the glock the buffer belongs to
 *
 */

void
gfs_attach_bufdata(struct buffer_head *bh, struct gfs_glock *gl)
{
	struct gfs_bufdata *bd;

	lock_page(bh->b_page);

	/* If there's one attached already, we're done */
	if (get_v2bd(bh)) {
		unlock_page(bh->b_page);
		return;
	}

	RETRY_MALLOC(bd = kmem_cache_alloc(gfs_bufdata_cachep, GFP_KERNEL), bd);
	atomic_inc(&gl->gl_sbd->sd_bufdata_count);

	memset(bd, 0, sizeof(struct gfs_bufdata));

	bd->bd_bh = bh;
	bd->bd_gl = gl;

	INIT_LE(&bd->bd_new_le, &gfs_buf_lops);
	INIT_LE(&bd->bd_incore_le, &gfs_buf_lops);

	init_MUTEX(&bd->bd_lock);

	INIT_LIST_HEAD(&bd->bd_ail_tr_list);

	set_v2bd(bh, bd);

	unlock_page(bh->b_page);
}

/**
 * gfs_is_pinned - Figure out if a buffer is pinned or not
 * @sdp: the filesystem the buffer belongs to
 * @bh: The buffer to be pinned
 *
 * Returns: TRUE if the buffer is pinned, FALSE otherwise
 */

int
gfs_is_pinned(struct gfs_sbd *sdp, struct buffer_head *bh)
{
	struct gfs_bufdata *bd = get_v2bd(bh);
	int ret = FALSE;

	if (bd) {
		gfs_lock_buffer(bh);
		if (bd->bd_pinned)
			ret = TRUE;
		gfs_unlock_buffer(bh);
	}

	return ret;
}

/**
 * gfs_dpin - Pin a metadata buffer in memory
 * @sdp: the filesystem the buffer belongs to
 * @bh: The buffer to be pinned
 *
 * "Pinning" means keeping buffer from being written to its in-place location.
 * A buffer should be pinned from the time it is added to a new transaction,
 *   until after it has been written to the log.
 * If an earlier change to this buffer is still pinned, waiting to be written
 *   to on-disk log, we need to keep a "frozen" copy of the old data while this
 *   transaction is modifying the real data.  We keep the frozen copy until
 *   this transaction's incore_commit(), i.e. until the transaction has
 *   finished modifying the real data, at which point we can use the real
 *   buffer for logging, even if the frozen copy didn't get written to the log.
 *
 */

void
gfs_dpin(struct gfs_sbd *sdp, struct buffer_head *bh)
{
	struct gfs_bufdata *bd = get_v2bd(bh);
	char *data;

	gfs_assert_withdraw(sdp, !test_bit(SDF_ROFS, &sdp->sd_flags));

	gfs_lock_buffer(bh);

	gfs_assert_warn(sdp, !bd->bd_frozen);

	if (!bd->bd_pinned++) {
		wait_on_buffer(bh);

		/* If this buffer is in the AIL and it has already been written
		   to in-place disk block, remove it from the AIL. */

		spin_lock(&sdp->sd_ail_lock);
		if (!list_empty(&bd->bd_ail_tr_list) && !buffer_busy(bh)) {
			list_del_init(&bd->bd_ail_tr_list);
			list_del(&bd->bd_ail_gl_list);
			brelse(bh);
		}
		spin_unlock(&sdp->sd_ail_lock);

		clear_buffer_dirty(bh);
		wait_on_buffer(bh);

		if (!buffer_uptodate(bh))
			gfs_io_error_bh(sdp, bh);
	} else {
		gfs_unlock_buffer(bh);

		gfs_assert_withdraw(sdp, buffer_uptodate(bh));

		data = gmalloc(sdp->sd_sb.sb_bsize);

		gfs_lock_buffer(bh);

		/* Create frozen copy, if needed. */
		if (bd->bd_pinned > 1) {
			memcpy(data, bh->b_data, sdp->sd_sb.sb_bsize);
			bd->bd_frozen = data;
		} else
			kfree(data);
	}

	gfs_unlock_buffer(bh);

	get_bh(bh);
}

/**
 * gfs_dunpin - Unpin a buffer
 * @sdp: the filesystem the buffer belongs to
 * @bh: The buffer to unpin
 * @tr: The transaction in the AIL that contains this buffer
 *      If NULL, don't attach buffer to any AIL list
 *      (i.e. when dropping a pin reference when merging a new transaction
 *       with an already existing incore transaction)
 *
 * Called for (meta) buffers, after they've been logged to on-disk journal.
 * Make a (meta) buffer writeable to in-place location on-disk, if recursive
 *   pin count is 1 (i.e. no other, later transaction is modifying this buffer).
 * Add buffer to AIL lists of 1) the latest transaction that's modified and
 *   logged (on-disk) the buffer, and of 2) the glock that protects the buffer.
 * A single buffer might have been modified by more than one transaction
 *   since the buffer's previous write to disk (in-place location).  We keep
 *   the buffer on only one transaction's AIL list, i.e. that of the latest
 *   transaction that's completed logging this buffer (no need to write it to
 *   in-place block multiple times for multiple transactions, only once with
 *   the most up-to-date data).
 * A single buffer will be protected by one and only one glock.  If buffer is 
 *   already on a (previous) transaction's AIL, we know that we're already
 *   on buffer's glock's AIL.
 * 
 */

void
gfs_dunpin(struct gfs_sbd *sdp, struct buffer_head *bh, struct gfs_trans *tr)
{
	struct gfs_bufdata *bd = get_v2bd(bh);

	gfs_assert_withdraw(sdp, buffer_uptodate(bh));

	gfs_lock_buffer(bh);

	if (gfs_assert_warn(sdp, bd->bd_pinned)) {
		gfs_unlock_buffer(bh);
		return;
	}

	/* No other (later) transaction is modifying buffer; ready to write */
	if (bd->bd_pinned == 1)
		mark_buffer_dirty(bh);

	bd->bd_pinned--;

	gfs_unlock_buffer(bh);

	if (tr) {
		spin_lock(&sdp->sd_ail_lock);

		if (list_empty(&bd->bd_ail_tr_list)) {
			/* Buffer not attached to any earlier transaction.  Add
			   it to glock's AIL, and this trans' AIL (below). */
			list_add(&bd->bd_ail_gl_list, &bd->bd_gl->gl_ail_bufs);
		} else {
			/* Was part of earlier transaction.
			   Move from that trans' AIL to this newer one's AIL.
			   Buf is already on glock's AIL. */
			list_del_init(&bd->bd_ail_tr_list);
			brelse(bh);
		}
		list_add(&bd->bd_ail_tr_list, &tr->tr_ail_bufs);

		spin_unlock(&sdp->sd_ail_lock);
	} else
		brelse(bh);
}

/**
 * logbh_end_io - Called by OS at the end of a logbh ("fake" bh) write to log
 * @bh: the buffer
 * @uptodate: whether or not the write succeeded
 *
 * Interrupt context, no ENTER/RETURN
 *
 */

static void
logbh_end_io(struct buffer_head *bh, int uptodate)
{
	if (uptodate)
		set_buffer_uptodate(bh);
	else
		clear_buffer_uptodate(bh);
	unlock_buffer(bh);
}

/**
 * gfs_logbh_init - Initialize a fake buffer head
 * @sdp: the filesystem
 * @bh: the buffer to initialize
 * @blkno: the block address of the buffer
 * @data: the data to be written
 *
 */

void
gfs_logbh_init(struct gfs_sbd *sdp, struct buffer_head *bh,
	       uint64_t blkno, char *data)
{
	memset(bh, 0, sizeof(struct buffer_head));
	bh->b_state = (1 << BH_Mapped) | (1 << BH_Uptodate) | (1 << BH_Lock);
	atomic_set(&bh->b_count, 1);
	set_bh_page(bh, virt_to_page(data), ((unsigned long)data) & (PAGE_SIZE - 1));
	bh->b_blocknr = blkno;
	bh->b_size = sdp->sd_sb.sb_bsize;
	bh->b_bdev = sdp->sd_vfs->s_bdev;
	init_buffer(bh, logbh_end_io, NULL);
	INIT_LIST_HEAD(&bh->b_assoc_buffers);
}

/**
 * gfs_logbh_uninit - Clean up a fake buffer head
 * @sdp: the filesystem
 * @bh: the buffer to clean
 *
 */

void
gfs_logbh_uninit(struct gfs_sbd *sdp, struct buffer_head *bh)
{
	gfs_assert_warn(sdp, test_bit(SDF_SHUTDOWN, &sdp->sd_flags) ||
			!buffer_busy(bh));
	gfs_assert_warn(sdp, atomic_read(&bh->b_count) == 1);
}

/**
 * gfs_logbh_start - Start writing a fake buffer head
 * @sdp: the filesystem
 * @bh: the buffer to write
 *
 * This starts a block write to our journal.
 */

void
gfs_logbh_start(struct gfs_sbd *sdp, struct buffer_head *bh)
{
	submit_bh(WRITE, bh);
}

/**
 * gfs_logbh_wait - Wait for the write of a fake buffer head to complete
 * @sdp: the filesystem
 * @bh: the buffer to write
 *
 * This waits for a block write to our journal to complete.
 *
 * Returns: errno
 */

int
gfs_logbh_wait(struct gfs_sbd *sdp, struct buffer_head *bh)
{
	wait_on_buffer(bh);

	if (!buffer_uptodate(bh) || buffer_dirty(bh)) {
		gfs_io_error_bh(sdp, bh);
		return -EIO;
	}
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		return -EIO;

	return 0;
}

/**
 * gfs_replay_buf - write a log buffer to its inplace location
 * @gl: the journal's glock 
 * @bh: the buffer
 *
 * Returns: errno
 */

int
gfs_replay_buf(struct gfs_glock *gl, struct buffer_head *bh)
{
	struct gfs_sbd *sdp = gl->gl_sbd;
	struct gfs_bufdata *bd;

	bd = get_v2bd(bh);
	if (!bd) {
		gfs_attach_bufdata(bh, gl);
		bd = get_v2bd(bh);
	}

	mark_buffer_dirty(bh);

	if (list_empty(&bd->bd_ail_tr_list)) {
		get_bh(bh);
		list_add(&bd->bd_ail_tr_list, &sdp->sd_recovery_bufs);
	}

	return 0;
}

/**
 * gfs_replay_check - Check up on journal replay
 * @sdp: the filesystem
 *
 */

void
gfs_replay_check(struct gfs_sbd *sdp)
{
	struct buffer_head *bh;
	struct gfs_bufdata *bd;

	while (!list_empty(&sdp->sd_recovery_bufs)) {
		bd = list_entry(sdp->sd_recovery_bufs.prev,
				struct gfs_bufdata, bd_ail_tr_list);
		bh = bd->bd_bh;

		if (buffer_busy(bh)) {
			list_move(&bd->bd_ail_tr_list,
				  &sdp->sd_recovery_bufs);
			break;
		} else {
			list_del_init(&bd->bd_ail_tr_list);
			if (!buffer_uptodate(bh))
				gfs_io_error_bh(sdp, bh);
			brelse(bh);
		}
	}
}

/**
 * gfs_replay_wait - Wait for all replayed buffers to hit the disk
 * @sdp: the filesystem
 *
 */

void
gfs_replay_wait(struct gfs_sbd *sdp)
{
	struct list_head *head, *tmp, *prev;
	struct buffer_head *bh;
	struct gfs_bufdata *bd;

	for (head = &sdp->sd_recovery_bufs, tmp = head->prev, prev = tmp->prev;
	     tmp != head;
	     tmp = prev, prev = tmp->prev) {
		bd = list_entry(tmp, struct gfs_bufdata, bd_ail_tr_list);
		bh = bd->bd_bh;

		if (!buffer_busy(bh)) {
			list_del_init(&bd->bd_ail_tr_list);
			if (!buffer_uptodate(bh))
				gfs_io_error_bh(sdp, bh);
			brelse(bh);
			continue;
		}

		if (buffer_dirty(bh)) {
			wait_on_buffer(bh);
			ll_rw_block(WRITE, 1, &bh);
		}
	}

	while (!list_empty(head)) {
		bd = list_entry(head->prev, struct gfs_bufdata, bd_ail_tr_list);
		bh = bd->bd_bh;

		wait_on_buffer(bh);

		gfs_assert_withdraw(sdp, !buffer_busy(bh));

		list_del_init(&bd->bd_ail_tr_list);
		if (!buffer_uptodate(bh))
			gfs_io_error_bh(sdp, bh);
		brelse(bh);
	}
}

/**
 * gfs_wipe_buffers - make inode's buffers so they aren't dirty/AILed anymore
 * @ip: the inode who owns the buffers
 * @rgd: the resource group
 * @bstart: the first buffer in the run
 * @blen: the number of buffers in the run
 *
 * Called when de-allocating a contiguous run of meta blocks within an rgrp.
 * Make sure all buffers for de-alloc'd blocks are removed from the AIL, if
 * they can be.  Dirty or pinned blocks are left alone.  Add relevant
 * meta-headers to meta-header cache, so we don't need to read disk
 * if we re-allocate blocks.
 */

void
gfs_wipe_buffers(struct gfs_inode *ip, struct gfs_rgrpd *rgd,
		 uint64_t bstart, uint32_t blen)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	struct inode *aspace = ip->i_gl->gl_aspace;
	struct buffer_head *bh;
	struct gfs_bufdata *bd;
	int busy;
	int add = FALSE;

	while (blen) {
		bh = getbuf(sdp, aspace, bstart, NO_CREATE);
		if (bh) {

			bd = get_v2bd(bh);

			if (buffer_uptodate(bh)) {
				if (bd) {
					gfs_lock_buffer(bh);
					gfs_mhc_add(rgd, &bh, 1);
					busy = bd->bd_pinned || buffer_busy(bh);
					gfs_unlock_buffer(bh);

					if (busy)
						add = TRUE;
					else {
						spin_lock(&sdp->sd_ail_lock);
						if (!list_empty(&bd->bd_ail_tr_list)) {
							list_del_init(&bd->bd_ail_tr_list);
							list_del(&bd->bd_ail_gl_list);
							brelse(bh);
						}
						spin_unlock(&sdp->sd_ail_lock);
					}
				} else {
					gfs_assert_withdraw(sdp, !buffer_dirty(bh));
					wait_on_buffer(bh);
					gfs_assert_withdraw(sdp, !buffer_busy(bh));
					gfs_mhc_add(rgd, &bh, 1);
				}
			} else {
				gfs_assert_withdraw(sdp, !bd || !bd->bd_pinned);
				gfs_assert_withdraw(sdp, !buffer_dirty(bh));
				wait_on_buffer(bh);
				gfs_assert_withdraw(sdp, !buffer_busy(bh));
			}

			brelse(bh);
		}

		bstart++;
		blen--;
	}

	if (add)
		gfs_depend_add(rgd, ip->i_num.no_formal_ino);
}

/**
 * gfs_sync_meta - sync all the buffers in a filesystem
 * @sdp: the filesystem
 *
 * Flush metadata blocks to on-disk journal, then
 * Flush metadata blocks (now in AIL) to on-disk in-place locations
 * Periodically keep checking until done (AIL empty)
 */

void
gfs_sync_meta(struct gfs_sbd *sdp)
{
	gfs_log_flush(sdp);
	for (;;) {
		gfs_ail_start(sdp, DIO_ALL);
		if (gfs_ail_empty(sdp))
			break;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ / 10);
	}
}

/**
 * gfs_flush_meta_cache - get rid of any references on buffers for this inode
 * @ip: The GFS inode
 *
 * This releases buffers that are in the most-recently-used array of
 *   blocks used for indirect block addressing for this inode.
 * Don't confuse this with the meta-HEADER cache (mhc)!
 */

void
gfs_flush_meta_cache(struct gfs_inode *ip)
{
	struct buffer_head **bh_slot;
	unsigned int x;

	spin_lock(&ip->i_spin);

	for (x = 0; x < GFS_MAX_META_HEIGHT; x++) {
		bh_slot = &ip->i_cache[x];
		if (*bh_slot) {
			brelse(*bh_slot);
			*bh_slot = NULL;
		}
	}

	spin_unlock(&ip->i_spin);
}

/**
 * gfs_get_meta_buffer - Get a metadata buffer
 * @ip: The GFS inode
 * @height: The level of this buf in the metadata (indir addr) tree (if any)
 * @num: The block number (device relative) of the buffer
 * @new: Non-zero if we may create a new buffer
 * @bhp: the buffer is returned here
 *
 * Returns: errno
 */

int
gfs_get_meta_buffer(struct gfs_inode *ip, int height, uint64_t num, int new,
		    struct buffer_head **bhp)
{
	struct buffer_head *bh, **bh_slot = &ip->i_cache[height];
	int flags = ((new) ? DIO_NEW : 0) | DIO_START | DIO_WAIT;
	int error;

	/* Try to use the gfs_inode's MRU metadata tree cache */
	spin_lock(&ip->i_spin);
	bh = *bh_slot;
	if (bh) {
		if (bh->b_blocknr == num)
			get_bh(bh);
		else
			bh = NULL;
	}
	spin_unlock(&ip->i_spin);

	if (bh) {
		error = gfs_dreread(ip->i_sbd, bh, flags);
		if (error) {
			brelse(bh);
			return error;
		}
	} else {
		error = gfs_dread(ip->i_gl, num, flags, &bh);
		if (error)
			return error;

		spin_lock(&ip->i_spin);
		if (*bh_slot != bh) {
			if (*bh_slot)
				brelse(*bh_slot);
			*bh_slot = bh;
			get_bh(bh);
		}
		spin_unlock(&ip->i_spin);
	}

	if (new) {
		if (gfs_assert_warn(ip->i_sbd, height)) {
			brelse(bh);
			return -EIO;
		}
		gfs_trans_add_bh(ip->i_gl, bh);
		gfs_metatype_set(bh, GFS_METATYPE_IN, GFS_FORMAT_IN);
		gfs_buffer_clear_tail(bh, sizeof(struct gfs_meta_header));
	} else if (gfs_metatype_check(ip->i_sbd, bh,
				      (height) ? GFS_METATYPE_IN : GFS_METATYPE_DI)) {
		brelse(bh);
		return -EIO;
	}

	*bhp = bh;

	return 0;
}

/**
 * gfs_get_data_buffer - Get a data buffer
 * @ip: The GFS inode
 * @num: The block number (device relative) of the data block
 * @new: Non-zero if this is a new allocation
 * @bhp: the buffer is returned here
 *
 * Returns: errno
 */

int
gfs_get_data_buffer(struct gfs_inode *ip, uint64_t block, int new,
		    struct buffer_head **bhp)
{
	struct buffer_head *bh;
	int error = 0;

	if (block == ip->i_num.no_addr) {
		if (gfs_assert_warn(ip->i_sbd, !new))
			return -EIO;
		error = gfs_dread(ip->i_gl, block, DIO_START | DIO_WAIT, &bh);
		if (error)
			return error;
		if (gfs_metatype_check(ip->i_sbd, bh, GFS_METATYPE_DI)) {
			brelse(bh);
			return -EIO;
		}
	} else if (gfs_is_jdata(ip)) {
		if (new) {
			error = gfs_dread(ip->i_gl, block,
					  DIO_NEW | DIO_START | DIO_WAIT, &bh);
			if (error)
				return error;
			gfs_trans_add_bh(ip->i_gl, bh);
			gfs_metatype_set(bh, GFS_METATYPE_JD, GFS_FORMAT_JD);
			gfs_buffer_clear_tail(bh, sizeof(struct gfs_meta_header));
		} else {
			error = gfs_dread(ip->i_gl, block,
					  DIO_START | DIO_WAIT, &bh);
			if (error)
				return error;
			if (gfs_metatype_check(ip->i_sbd, bh, GFS_METATYPE_JD)) {
				brelse(bh);
				return -EIO;
			}
		}
	} else {
		if (new) {
			bh = gfs_dgetblk(ip->i_gl, block);
			gfs_prep_new_buffer(bh);
		} else {
			error = gfs_dread(ip->i_gl, block,
					  DIO_START | DIO_WAIT, &bh);
			if (error)
				return error;
		}
	}

	*bhp = bh;

	return 0;
}

/**
 * gfs_start_ra - start readahead on an extent of a file
 * @gl: the glock the blocks belong to
 * @dblock: the starting disk block
 * @extlen: the number of blocks in the extent
 *
 */

void
gfs_start_ra(struct gfs_glock *gl, uint64_t dblock, uint32_t extlen)
{
	struct gfs_sbd *sdp = gl->gl_sbd;
	struct inode *aspace = gl->gl_aspace;
	struct buffer_head *first_bh, *bh;
	uint32_t max_ra = gfs_tune_get(sdp, gt_max_readahead) >> sdp->sd_sb.sb_bsize_shift;
	int error;

	if (!extlen)
		return;
	if (!max_ra)
		return;
	if (extlen > max_ra)
		extlen = max_ra;

	first_bh = getbuf(sdp, aspace, dblock, CREATE);

	if (buffer_uptodate(first_bh))
		goto out;
	if (!buffer_locked(first_bh)) {
		error = gfs_dreread(sdp, first_bh, DIO_START);
		if (error)
			goto out;
	}

	dblock++;
	extlen--;

	while (extlen) {
		bh = getbuf(sdp, aspace, dblock, CREATE);

		if (!buffer_uptodate(bh) && !buffer_locked(bh)) {
			error = gfs_dreread(sdp, bh, DIO_START);
			brelse(bh);
			if (error)
				goto out;
		} else
			brelse(bh);

		dblock++;
		extlen--;

		if (buffer_uptodate(first_bh))
			break;
	}

 out:
	brelse(first_bh);
}
