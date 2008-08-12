/*
          What rolls down stairs
             Alone or in pairs
      Rolls over your neighbor's dog.
         What's great for a snack
           And fits on your back
             It's log, log, log!
             It's lo-og, lo-og,
       It's big, it's heavy, it's wood.
             It's lo-og, lo-og,
       It's better than bad, it's good.
           Everyone wants a log,
         You're gonna love it, log
         Come on and get your log,
           Everyone needs a log...
            LOG... FROM BLAMMO!

                     -- The Ren and Stimpy Show
*/

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>

#include "gfs.h"
#include "dio.h"
#include "log.h"
#include "lops.h"

/**
 * gfs_struct2blk - compute stuff
 * @sdp: the filesystem
 * @nstruct: the number of structures
 * @ssize: the size of the structures
 *
 * Compute the number of log descriptor blocks needed to hold a certain number
 * of structures of a certain size.
 *
 * Returns: the number of blocks needed (minimum is always 1)
 */

unsigned int
gfs_struct2blk(struct gfs_sbd *sdp, unsigned int nstruct, unsigned int ssize)
{
	unsigned int blks;
	unsigned int first, second;

	blks = 1;
	first = (sdp->sd_sb.sb_bsize - sizeof(struct gfs_log_descriptor)) / ssize;

	if (nstruct > first) {
		second = sdp->sd_sb.sb_bsize / ssize;
		blks += DIV_RU(nstruct - first, second);
	}

	return blks;
}

/**
 * gfs_blk2seg - Convert number of blocks into number of segments
 * @sdp: The GFS superblock
 * @blocks: The number of blocks
 *
 * Returns: The number of journal segments
 */

unsigned int
gfs_blk2seg(struct gfs_sbd *sdp, unsigned int blocks)
{
	return DIV_RU(blocks, sdp->sd_sb.sb_seg_size - 1);
}

/**
 * log_distance - Compute distance between two journal blocks
 * @sdp: The GFS superblock
 * @newer: The most recent journal block of the pair
 * @older: The older journal block of the pair
 *
 *   Compute the distance (in the journal direction) between two
 *   blocks in the journal
 *
 * Returns: the distance in blocks
 */

static __inline__ unsigned int
log_distance(struct gfs_sbd *sdp, uint64_t newer, uint64_t older)
{
	int64_t dist;

	dist = newer - older;
	if (dist < 0)
		dist += sdp->sd_jdesc.ji_nsegment * sdp->sd_sb.sb_seg_size;

	return dist;
}

/**
 * log_incr_head - Increment journal head (next block to fill in journal)
 * @sdp: The GFS superblock
 * @head: the variable holding the head of the journal
 *
 * Increment journal head by one. 
 * At the end of the journal, wrap head back to the start.
 * Don't confuse journal/log head with a gfs_log_header!
 */

static __inline__ void
log_incr_head(struct gfs_sbd *sdp, uint64_t * head)
{
	struct gfs_jindex *jdesc = &sdp->sd_jdesc;

	if (++*head ==
	    jdesc->ji_addr + jdesc->ji_nsegment * sdp->sd_sb.sb_seg_size)
		*head = jdesc->ji_addr;
}

/**
 * gfs_ail_start - Start I/O on the AIL
 * @sdp: the filesystem
 * @flags:  DIO_ALL -- flush *all* AIL transactions to disk
 *          default -- flush first-on-list AIL transaction to disk
 *
 */

void
gfs_ail_start(struct gfs_sbd *sdp, int flags)
{
	struct list_head *head = &sdp->sd_log_ail;
	struct list_head *first, *tmp;
	struct gfs_trans *first_tr, *tr;

	gfs_log_lock(sdp);

	if (list_empty(head)) {
		gfs_log_unlock(sdp);
		return;
	}

	first = head->prev;
	first_tr = list_entry(first, struct gfs_trans, tr_list);
	gfs_ail_start_trans(sdp, first_tr);

	if (flags & DIO_ALL)
		first_tr = NULL;

	for (tmp = first->prev; tmp != head; tmp = tmp->prev) {
		if (first_tr && gfs_ail_empty_trans(sdp, first_tr))
			break;

		tr = list_entry(tmp, struct gfs_trans, tr_list);
		gfs_ail_start_trans(sdp, tr);
	}

	gfs_log_unlock(sdp);
}

/**
 * current_tail - Find block number of current log tail
 * @sdp: The GFS superblock
 *
 * Find the block number of the current tail of the log.
 * Assumes that the log lock is held.
 *
 * Returns: The tail's block number (must be on a log segment boundary)
 */

static uint64_t
current_tail(struct gfs_sbd *sdp)
{
	struct gfs_trans *tr;
	uint64_t tail;

	if (list_empty(&sdp->sd_log_ail)) {
		tail = sdp->sd_log_head;

		if (!gfs_log_is_header(sdp, tail)) {
			tail--;
			gfs_assert(sdp, gfs_log_is_header(sdp, tail), );
		}
	} else {
		tr = list_entry(sdp->sd_log_ail.prev,
				struct gfs_trans, tr_list);
		tail = tr->tr_first_head;
	}

	return tail;
}

/**
 * gfs_ail_empty - move the tail of the log forward (if possible)
 * @sdp: the filesystem
 *
 * Returns: TRUE if the AIL is empty
 *
 * Checks each transaction on sd_log_ail, to see if it has been successfully
 *   flushed to in-place blocks on disk.  If so, removes trans from sd_log_ail,
 *   effectively advancing the tail of the log (freeing log segments so they
 *   can be overwritten).
 * Adds # freed log segments to sd_log_seg_free.
 */

int
gfs_ail_empty(struct gfs_sbd *sdp)
{
	struct list_head *head, *tmp, *prev;
	struct gfs_trans *tr;
	uint64_t oldtail, newtail;
	unsigned int dist;
	unsigned int segments;
	int ret;

	gfs_log_lock(sdp);

	oldtail = current_tail(sdp);

	for (head = &sdp->sd_log_ail, tmp = head->prev, prev = tmp->prev;
	     tmp != head;
	     tmp = prev, prev = tmp->prev) {
		tr = list_entry(tmp, struct gfs_trans, tr_list);
		if (gfs_ail_empty_trans(sdp, tr)) {
			list_del(&tr->tr_list);
			kfree(tr);
		}
	}

	newtail = current_tail(sdp);

	if (oldtail != newtail) {
		dist = log_distance(sdp, newtail, oldtail);

		segments = dist / sdp->sd_sb.sb_seg_size;
		gfs_assert(sdp, segments * sdp->sd_sb.sb_seg_size == dist,);

		sdp->sd_log_seg_ail2 += segments;
		gfs_assert(sdp, sdp->sd_log_seg_free + sdp->sd_log_seg_ail2 <=
			   sdp->sd_jdesc.ji_nsegment,); 
	}

	ret = list_empty(head);

	gfs_log_unlock(sdp);

	return ret;
}

/**
 * gfs_log_reserve - Make a log reservation
 * @sdp: The GFS superblock
 * @segments: The number of segments to reserve
 * @jump_queue: if TRUE, don't care about fairness ordering
 *
 * Returns: errno
 */

int
gfs_log_reserve(struct gfs_sbd *sdp, unsigned int segments, int jump_queue)
{
	struct list_head list;
	unsigned int try = 0;

	if (gfs_assert_warn(sdp, segments))
		return -EINVAL;
	if (gfs_assert_warn(sdp, segments < sdp->sd_jdesc.ji_nsegment))
		return -EINVAL;

	INIT_LIST_HEAD(&list);

	for (;;) {
		spin_lock(&sdp->sd_log_seg_lock);

		if (list_empty(&list)) {
			if (jump_queue)
				list_add(&list, &sdp->sd_log_seg_list);
			else {
				list_add_tail(&list, &sdp->sd_log_seg_list);
				while (sdp->sd_log_seg_list.next != &list) {
					DECLARE_WAITQUEUE(__wait_chan, current);
					set_current_state(TASK_UNINTERRUPTIBLE);
					add_wait_queue(&sdp->sd_log_seg_wait,
						       &__wait_chan);
					spin_unlock(&sdp->sd_log_seg_lock);
					schedule();
					spin_lock(&sdp->sd_log_seg_lock);
					remove_wait_queue(&sdp->sd_log_seg_wait,
							  &__wait_chan);
					set_current_state(TASK_RUNNING);
				}
			}
		}

		if (sdp->sd_log_seg_free > segments) {
			sdp->sd_log_seg_free -= segments;
			list_del(&list);
			spin_unlock(&sdp->sd_log_seg_lock);
			wake_up(&sdp->sd_log_seg_wait);
			break;
		}

		spin_unlock(&sdp->sd_log_seg_lock);

		if (try) {
			gfs_log_flush(sdp);
			gfs_ail_start(sdp, 0);
		}

		gfs_ail_empty(sdp);

		try++;
		yield();
	}

	return 0;
}

/**
 * gfs_log_release - Release a given number of log segments
 * @sdp: The GFS superblock
 * @segments: The number of segments
 *
 */

void
gfs_log_release(struct gfs_sbd *sdp, unsigned int segments)
{
	spin_lock(&sdp->sd_log_seg_lock);
	sdp->sd_log_seg_free += segments;
	gfs_assert(sdp, sdp->sd_log_seg_free + sdp->sd_log_seg_ail2 <=
		   sdp->sd_jdesc.ji_nsegment,);
	spin_unlock(&sdp->sd_log_seg_lock);
}

/**
 * log_get_header - Get and initialize a journal header buffer
 * @sdp: The GFS superblock
 * @tr: The transaction that needs a log header
 * @next: FALSE if this log header appears in midst of current transaction
 *        TRUE if this starts next transaction (and commits current trans)
 *
 * Returns: the initialized log buffer descriptor
 *
 * Initialize one of the transaction's pre-allocated buffers (and associated
 *   log buffer descriptor) to be a log header for this transaction.
 * A log header gets written to *each* log segment boundary block, so journal
 *   recovery will quickly be able to get its bearings.  A single transaction
 *   may span several log segments, which means that log headers will appear
 *   in the midst of that transaction (@next == FALSE).  These headers get
 *   added to trans' list of buffers to write to log.
 * Log commit is accomplished by writing the log header for the next
 *   transaction (@next == TRUE), with pre-incremented sequence number,
 *   and updated first-in-transaction block number.  These headers do *not* get
 *   added to trans' buffer list, since they are written separately to disk
 *   *after* the trans gets completely flushed to on-disk log.
 * NOTE:  This buffer will *not* get written to an in-place location in the
 *        filesystem; it is for use only within the log.
 */

static struct gfs_log_buf *
log_get_header(struct gfs_sbd *sdp, struct gfs_trans *tr, int next)
{
	struct gfs_log_buf *lb;
	struct list_head *bmem;
	struct gfs_log_header header;

	/* Make sure we're on a log segment boundary block */
	gfs_assert(sdp, gfs_log_is_header(sdp, tr->tr_log_head),);

	/* Grab a free log buffer descriptor (attached to trans) */
	gfs_assert(sdp, tr->tr_num_free_bufs &&
		   !list_empty(&tr->tr_free_bufs),);
	lb = list_entry(tr->tr_free_bufs.next, struct gfs_log_buf, lb_list);
	list_del(&lb->lb_list);
	tr->tr_num_free_bufs--;

	/* Grab a free log buffer (attached to trans) */
	gfs_assert(sdp, tr->tr_num_free_bmem &&
		   !list_empty(&tr->tr_free_bmem),);
	bmem = tr->tr_free_bmem.next;
	list_del(bmem);
	tr->tr_num_free_bmem--;

	/* Create "fake" bh to write bmem to log header block */
	gfs_logbh_init(sdp, &lb->lb_bh, tr->tr_log_head, (char *)bmem);
	memset(bmem, 0, sdp->sd_sb.sb_bsize);

	memset(&header, 0, sizeof (header));

	if (next) {
		/* Fill in header for next transaction, committing previous */
		header.lh_header.mh_magic = GFS_MAGIC;
		header.lh_header.mh_type = GFS_METATYPE_LH;
		header.lh_header.mh_format = GFS_FORMAT_LH;
		header.lh_first = tr->tr_log_head;
		header.lh_sequence = sdp->sd_sequence + 1;
		header.lh_tail = current_tail(sdp);
		header.lh_last_dump = sdp->sd_log_dump_last;
	} else {
		/* Fill in another header for this transaction */
		header.lh_header.mh_magic = GFS_MAGIC;
		header.lh_header.mh_type = GFS_METATYPE_LH;
		header.lh_header.mh_format = GFS_FORMAT_LH;
		header.lh_first = tr->tr_first_head;
		header.lh_sequence = sdp->sd_sequence;
		header.lh_tail = current_tail(sdp);
		header.lh_last_dump = sdp->sd_log_dump_last;

		/* Attach log header buf to trans' list of bufs going to log */
		list_add(&lb->lb_list, &tr->tr_bufs);
	}

	/* Copy log header struct to beginning and end of buffer's 1st 512B */
	gfs_log_header_out(&header, lb->lb_bh.b_data);
	gfs_log_header_out(&header,
			   lb->lb_bh.b_data + GFS_BASIC_BLOCK -
			   sizeof(struct gfs_log_header));

	/* Find next log buffer to fill */
	log_incr_head(sdp, &tr->tr_log_head);

	return lb;
}

/**
 * gfs_log_get_buf - Get and initialize a buffer to use for log control data
 * @sdp: The GFS superblock
 * @tr: The GFS transaction
 *
 * Initialize one of the transaction's pre-allocated buffers (and associated
 *   log buffer descriptor) to be used for log control data (e.g. log tags).
 * Make sure this buffer is attached to the transaction, to be logged to disk.
 * NOTE:  This buffer will *not* get written to an in-place location in the
 *        filesystem; it is for use only within the log.
 *
 * Returns: the log buffer descriptor
 */

struct gfs_log_buf *
gfs_log_get_buf(struct gfs_sbd *sdp, struct gfs_trans *tr)
{
	struct gfs_log_buf *lb;
	struct list_head *bmem;

	/* If next block in log is on a segment boundary, we need to
	    write a log header */
	if (gfs_log_is_header(sdp, tr->tr_log_head))
		log_get_header(sdp, tr, FALSE);

	/* Grab a free buffer descriptor (attached to trans) */
	gfs_assert(sdp, tr->tr_num_free_bufs &&
		   !list_empty(&tr->tr_free_bufs),);
	lb = list_entry(tr->tr_free_bufs.next, struct gfs_log_buf, lb_list);
	list_del(&lb->lb_list);
	tr->tr_num_free_bufs--;

	/* Grab a free buffer (attached to trans) */
	gfs_assert(sdp, tr->tr_num_free_bmem
		   && !list_empty(&tr->tr_free_bmem),);
	bmem = tr->tr_free_bmem.next;
	list_del(bmem);
	tr->tr_num_free_bmem--;

	/* Create "fake" bh to write bmem to log block */
	gfs_logbh_init(sdp, &lb->lb_bh, tr->tr_log_head, (char *)bmem);
	memset(bmem, 0, sdp->sd_sb.sb_bsize);

	list_add(&lb->lb_list, &tr->tr_bufs);

	/* Find next log buffer to fill */
	log_incr_head(sdp, &tr->tr_log_head);

	return lb;
}

/**
 * gfs_log_fake_buf - Build a fake buffer head to write metadata buffer to log
 * @sdp: the filesystem
 * @tr: the transaction this is part of
 * @data: the data the buffer_head should point to
 * @unlock: a buffer_head to be unlocked when struct gfs_log_buf is torn down
 *    (i.e. the "real" buffer_head that will write to in-place location)
 *
 * Initialize one of the transaction's pre-allocated log buffer descriptors
 *   to be used for writing a metadata buffer into the log.
 * Make sure this buffer is attached to the transaction, to be logged to disk.
 * NOTE:  This buffer *will* be written to in-place location within filesytem,
 *        in addition to being written into the log.
 * 
 */

void
gfs_log_fake_buf(struct gfs_sbd *sdp, struct gfs_trans *tr, char *data,
		 struct buffer_head *unlock)
{
	struct gfs_log_buf *lb;

	if (gfs_log_is_header(sdp, tr->tr_log_head))
		log_get_header(sdp, tr, FALSE);

	/* Grab a free buffer descriptor (attached to trans) */
	gfs_assert(sdp, tr->tr_num_free_bufs &&
		   !list_empty(&tr->tr_free_bufs),);
	lb = list_entry(tr->tr_free_bufs.next, struct gfs_log_buf, lb_list);
	list_del(&lb->lb_list);
	tr->tr_num_free_bufs--;

	/* Create "fake" bh to write data to log block */
	gfs_logbh_init(sdp, &lb->lb_bh, tr->tr_log_head, data);
	lb->lb_unlock = unlock;

	list_add(&lb->lb_list, &tr->tr_bufs);

	/* Find next log buffer to fill */
	log_incr_head(sdp, &tr->tr_log_head);
}

/**
 * check_seg_usage - Check that we didn't use too many segments
 * @sdp: The GFS superblock
 * @tr: The transaction
 *
 * Also, make sure we don't write ever get to a point where there are
 * no dumps in the log (corrupting the log).  Panic before we let
 * that happen.
 * 
 */

static void
check_seg_usage(struct gfs_sbd *sdp, struct gfs_trans *tr)
{
	struct gfs_jindex *jdesc = &sdp->sd_jdesc;
	unsigned int dist;
	unsigned int segments;
	uint64_t head_off, head_wrap;
	uint64_t dump_off, dump_wrap;

	dist = log_distance(sdp, tr->tr_log_head, tr->tr_first_head);

	segments = dist / sdp->sd_sb.sb_seg_size;
	gfs_assert(sdp, segments * sdp->sd_sb.sb_seg_size == dist,);
	gfs_assert(sdp, segments == tr->tr_seg_reserved,);

	if (sdp->sd_log_dump_last) {
		int diff;

		head_off = tr->tr_first_head +
			tr->tr_seg_reserved * sdp->sd_sb.sb_seg_size;
		head_wrap = sdp->sd_log_wrap;
		if (head_off >= jdesc->ji_addr +
		    jdesc->ji_nsegment * sdp->sd_sb.sb_seg_size) {
			head_off -= jdesc->ji_nsegment * sdp->sd_sb.sb_seg_size;
			head_wrap++;
		}

		dump_off = sdp->sd_log_dump_last;
		dump_wrap = sdp->sd_log_dump_last_wrap;

		diff = (int)(head_wrap - dump_wrap);
		switch (diff) {
		case 0:
			break;

		case 1:
			if (head_off < dump_off - sdp->sd_sb.sb_seg_size)
				break;
			else if (head_off <= dump_off &&
				 (tr->tr_flags & TRF_LOG_DUMP))
				break;

		default:
			gfs_assert(sdp, FALSE,
				   printk("GFS: fsid=%s: head_off = %"PRIu64", head_wrap = %"PRIu64"\n"
					  "GFS: fsid=%s: dump_off = %"PRIu64", dump_wrap = %"PRIu64"\n",
					  sdp->sd_fsname, head_off, head_wrap,
					  sdp->sd_fsname, dump_off, dump_wrap););
			break;
		}
	}
}

/**
 * log_free_buf - Free a struct gfs_log_buf (and possibly the data it points to)
 * @sdp: the filesystem
 * @lb: the log buffer descriptor
 *
 * If buffer contains (meta)data to be written into filesystem in-place block,
 *   descriptor will point to the "real" (lb_unlock) buffer head.  Unlock it.
 * If buffer was used only for log header or control data (e.g. tags), we're
 *   done with it as soon as it gets written to on-disk log.  Free it.
 * Either way, we can free the log descriptor structure.
 */

static void
log_free_buf(struct gfs_sbd *sdp, struct gfs_log_buf *lb)
{
	char *bmem;

	bmem = lb->lb_bh.b_data;
	gfs_logbh_uninit(sdp, &lb->lb_bh);

	if (lb->lb_unlock)
		gfs_unlock_buffer(lb->lb_unlock);
	else
		kfree(bmem);

	kfree(lb);
}

/**
 * sync_trans - Add "last" descriptor, sync transaction to on-disk log
 * @sdp: The GFS superblock
 * @tr: The transaction
 *
 * Add the "last" descriptor onto the end of the current transaction
 *   and sync the whole transaction out to on-disk log.
 * Don't log-commit (i.e. write next transaction's log header) yet, though.
 *
 * Returns: errno
 */

static int
sync_trans(struct gfs_sbd *sdp, struct gfs_trans *tr)
{
	struct list_head *tmp, *head, *prev;
	struct gfs_log_descriptor desc;
	struct gfs_log_buf *lb;
	uint64_t blk;
	int error = 0, e;

	/*  Build LAST descriptor  */

	lb = gfs_log_get_buf(sdp, tr);

	memset(&desc, 0, sizeof(struct gfs_log_descriptor));
	desc.ld_header.mh_magic = GFS_MAGIC;
	desc.ld_header.mh_type = GFS_METATYPE_LD;
	desc.ld_header.mh_format = GFS_FORMAT_LD;
	desc.ld_type = GFS_LOG_DESC_LAST;
	desc.ld_length = 1;
	for (blk = tr->tr_log_head; !gfs_log_is_header(sdp, blk); blk++)
		desc.ld_length++;
	gfs_desc_out(&desc, lb->lb_bh.b_data);

	while (!gfs_log_is_header(sdp, tr->tr_log_head))
		log_incr_head(sdp, &tr->tr_log_head);

	check_seg_usage(sdp, tr);

	/* Start I/O
	   Go in "prev" direction to start the I/O in order. */

	for (head = &tr->tr_bufs, tmp = head->prev, prev = tmp->prev;
	     tmp != head;
	     tmp = prev, prev = tmp->prev) {
		lb = list_entry(tmp, struct gfs_log_buf, lb_list);
		gfs_logbh_start(sdp, &lb->lb_bh);
	}

	/* Wait on I/O
	   Go in "next" direction to minimize sleeps/wakeups. */

	while (!list_empty(&tr->tr_bufs)) {
		lb = list_entry(tr->tr_bufs.next, struct gfs_log_buf, lb_list);

		e = gfs_logbh_wait(sdp, &lb->lb_bh);
		if (e)
			error = e;

		list_del(&lb->lb_list);
		log_free_buf(sdp, lb);
	}

	return error;
}

/**
 * commit_trans - Commit the current transaction
 * @sdp: The GFS superblock
 * @tr: The transaction
 *
 * Write next header to commit
 *
 * Returns: errno
 */

static int
commit_trans(struct gfs_sbd *sdp, struct gfs_trans *tr)
{
	struct gfs_log_buf *lb;
	int error;

	lb = log_get_header(sdp, tr, TRUE);

	gfs_logbh_start(sdp, &lb->lb_bh);
	error = gfs_logbh_wait(sdp, &lb->lb_bh);
	if (!error) {
		spin_lock(&sdp->sd_log_seg_lock);
		if (!(tr->tr_flags & TRF_DUMMY))
			sdp->sd_log_seg_free += sdp->sd_log_seg_ail2;
		else
			sdp->sd_log_seg_free += (sdp->sd_log_seg_ail2 - 1);
		sdp->sd_log_seg_ail2 = 0;
		spin_unlock(&sdp->sd_log_seg_lock);
	}
	log_free_buf(sdp, lb);

	return error;
}

/**
 * disk_commit - Write a transaction to the on-disk journal
 * @sdp: The GFS superblock
 * @tr: The transaction
 *
 * Returns: errno
 */

static int
disk_commit(struct gfs_sbd *sdp, struct gfs_trans *tr)
{
	uint64_t last_dump, last_dump_wrap;
	int error = 0;

	gfs_assert(sdp, !test_bit(SDF_ROFS, &sdp->sd_flags),);
	tr->tr_log_head = sdp->sd_log_head;
	tr->tr_first_head = tr->tr_log_head - 1;
	gfs_assert(sdp, gfs_log_is_header(sdp, tr->tr_first_head),);

	LO_BUILD_BHLIST(sdp, tr);

	if (!(tr->tr_flags & TRF_DUMMY))
		gfs_assert(sdp, !list_empty(&tr->tr_bufs),);

	error = sync_trans(sdp, tr);
	if (error) {
		/* Eat unusable commit buffer */
		log_free_buf(sdp, log_get_header(sdp, tr, TRUE));
		goto out;
	}

	if (tr->tr_flags & TRF_LOG_DUMP) {
		/* This commit header should point to the log dump we're
		   commiting as the current one.  But save the copy of the
		   old one in case we have problems commiting the dump. */

		last_dump = sdp->sd_log_dump_last;
		last_dump_wrap = sdp->sd_log_dump_last_wrap;

		sdp->sd_log_dump_last = tr->tr_first_head;
		sdp->sd_log_dump_last_wrap = sdp->sd_log_wrap;

		error = commit_trans(sdp, tr);
		if (error) {
			sdp->sd_log_dump_last = last_dump;
			sdp->sd_log_dump_last_wrap = last_dump_wrap;
			goto out;
		}
	} else {
		error = commit_trans(sdp, tr);
		if (error)
			goto out;
	}

	if (sdp->sd_log_head > tr->tr_log_head)
		sdp->sd_log_wrap++;
   sdp->sd_log_head = tr->tr_log_head;
   sdp->sd_sequence++;

 out:
	gfs_assert_warn(sdp, !tr->tr_num_free_bufs &&
			list_empty(&tr->tr_free_bufs));
	gfs_assert_warn(sdp, !tr->tr_num_free_bmem &&
			list_empty(&tr->tr_free_bmem));

	return error;
}

/**
 * add_trans_to_ail - Add a ondisk commited transaction to the AIL
 * @sdp: the filesystem
 * @tr: the transaction 
 *
 */

static void
add_trans_to_ail(struct gfs_sbd *sdp, struct gfs_trans *tr)
{
	struct gfs_log_element *le;

	while (!list_empty(&tr->tr_elements)) {
		le = list_entry(tr->tr_elements.next,
				struct gfs_log_element, le_list);
		LO_ADD_TO_AIL(sdp, le);
	}

	list_add(&tr->tr_list, &sdp->sd_log_ail);
}

/**
 * log_refund - Refund log segments to the free pool
 * @sdp: The GFS superblock
 * @tr: The transaction to examine
 *
 * Look at the number of segments reserved for this transaction and the
 * number of segments actually needed for it.  If they aren't the
 * same, refund the difference to the free segment pool.
 *
 * De-alloc any unneeded log buffers and log buffer descriptors.
 *
 * Called with the log lock held.
 */

static void
log_refund(struct gfs_sbd *sdp, struct gfs_trans *tr)
{
	struct gfs_log_buf *lb;
	struct list_head *bmem;
	unsigned int num_bufs = 0, num_bmem = 0;
	unsigned int segments;

	LO_TRANS_SIZE(sdp, tr, NULL, NULL, &num_bufs, &num_bmem);

	segments = gfs_blk2seg(sdp, num_bufs + 1);
	num_bufs += segments + 1;
	num_bmem += segments + 1;

	/* Unreserve unneeded log segments */
	if (tr->tr_seg_reserved > segments) {
		spin_lock(&sdp->sd_log_seg_lock);
		sdp->sd_log_seg_free += tr->tr_seg_reserved - segments;
		gfs_assert(sdp, sdp->sd_log_seg_free + sdp->sd_log_seg_ail2 <=
			   sdp->sd_jdesc.ji_nsegment,);
		spin_unlock(&sdp->sd_log_seg_lock);

		tr->tr_seg_reserved = segments;
	} else
		gfs_assert(sdp, tr->tr_seg_reserved == segments,);

	/* De-alloc unneeded log buffer descriptors */
	gfs_assert(sdp, tr->tr_num_free_bufs >= num_bufs,);
	while (tr->tr_num_free_bufs > num_bufs) {
		lb = list_entry(tr->tr_free_bufs.next,
				struct gfs_log_buf, lb_list);
		list_del(&lb->lb_list);
		kfree(lb);
		tr->tr_num_free_bufs--;
	}

	/* De-alloc unneeded log buffers */
	gfs_assert(sdp, tr->tr_num_free_bmem >= num_bmem,);
	while (tr->tr_num_free_bmem > num_bmem) {
		bmem = tr->tr_free_bmem.next;
		list_del(bmem);
		kfree(bmem);
		tr->tr_num_free_bmem--;
	}
}

/**
 * trans_combine - combine two transactions
 * @sdp: the filesystem
 * @tr: the surviving transaction
 * @new_tr: the transaction that gets freed
 *
 * Assumes that the two transactions are independent.
 */

static void
trans_combine(struct gfs_sbd *sdp, struct gfs_trans *tr,
	      struct gfs_trans *new_tr)
{
	struct gfs_log_element *le;
	struct gfs_log_buf *lb;
	struct list_head *bmem;

	tr->tr_file = __FILE__;
	tr->tr_line = __LINE__;
	tr->tr_seg_reserved += new_tr->tr_seg_reserved;
	tr->tr_flags |= new_tr->tr_flags;
	tr->tr_num_free_bufs += new_tr->tr_num_free_bufs;
	tr->tr_num_free_bmem += new_tr->tr_num_free_bmem;

	/*  Combine the log elements of the two transactions  */

	while (!list_empty(&new_tr->tr_elements)) {
		le = list_entry(new_tr->tr_elements.next,
				struct gfs_log_element, le_list);
		gfs_assert(sdp, le->le_trans == new_tr,);
		le->le_trans = tr;
		list_move(&le->le_list, &tr->tr_elements);
	}

	LO_TRANS_COMBINE(sdp, tr, new_tr);

	/* Move free log buffer descriptors to surviving trans */
	while (!list_empty(&new_tr->tr_free_bufs)) {
		lb = list_entry(new_tr->tr_free_bufs.next,
				struct gfs_log_buf, lb_list);
		list_move(&lb->lb_list, &tr->tr_free_bufs);
		new_tr->tr_num_free_bufs--;
	}
	/* Move free log buffers to surviving trans */
	while (!list_empty(&new_tr->tr_free_bmem)) {
		bmem = new_tr->tr_free_bmem.next;
		list_move(bmem, &tr->tr_free_bmem);
		new_tr->tr_num_free_bmem--;
	}

	gfs_assert_warn(sdp, !new_tr->tr_num_free_bufs);
	gfs_assert_warn(sdp, !new_tr->tr_num_free_bmem);

	kfree(new_tr);
}

static void
make_dummy_transaction(struct gfs_sbd *sdp, struct gfs_trans *tr)
{
	struct gfs_log_buf *lb;
	struct list_head *bmem;

	memset(tr, 0, sizeof(struct gfs_trans));
	INIT_LIST_HEAD(&tr->tr_list);
	INIT_LIST_HEAD(&tr->tr_elements);
	INIT_LIST_HEAD(&tr->tr_free_bufs);
	INIT_LIST_HEAD(&tr->tr_free_bmem);
	INIT_LIST_HEAD(&tr->tr_bufs);
	INIT_LIST_HEAD(&tr->tr_ail_bufs);
	tr->tr_flags = TRF_DUMMY;
	tr->tr_file = __FILE__;
	tr->tr_line = __LINE__;
	tr->tr_seg_reserved = 1;
	while (tr->tr_num_free_bufs < 2) {
		lb = gmalloc(sizeof(struct gfs_log_buf));
		memset(lb, 0, sizeof(struct gfs_log_buf));
		list_add(&lb->lb_list, &tr->tr_free_bufs);
		tr->tr_num_free_bufs++;
	}
	while (tr->tr_num_free_bmem < 2) {
		bmem = gmalloc(sdp->sd_sb.sb_bsize);
		list_add(bmem, &tr->tr_free_bmem);
		tr->tr_num_free_bmem++;
	}
}


/**
 * log_flush_internal - flush incore transaction(s)
 * @sdp: the filesystem
 * @gl: The glock structure to flush.  If NULL, flush the whole incore log
 *
 * If a glock is provided, we flush, to on-disk log, all of the metadata for
 *   the one incore-committed (complete, but not-yet-flushed-to-log)
 *   transaction that the glock protects.
 * If NULL, we combine *all* of the filesystem's incore-committed
 *   transactions into one big transaction, and flush it to the log.
 */

static void
log_flush_internal(struct gfs_sbd *sdp, struct gfs_glock *gl)
{
	
	struct gfs_trans *trans = NULL, *tr;
	int error;

	gfs_log_lock(sdp);

	if (!gl && list_empty(&sdp->sd_log_incore)) {
		if (sdp->sd_log_seg_ail2) {
			trans = gmalloc(sizeof(struct gfs_trans));
			make_dummy_transaction(sdp, trans);
		}
		else
			goto out;
	}

	if (gl) {
		if (!gl->gl_incore_le.le_trans)
			goto out;

		trans = gl->gl_incore_le.le_trans;

		list_del(&trans->tr_list);
	} else {
		/* combine *all* transactions in incore list */
		while (!list_empty(&sdp->sd_log_incore)) {
			tr = list_entry(sdp->sd_log_incore.next,
					struct gfs_trans, tr_list);

			list_del(&tr->tr_list);

			if (trans)
				trans_combine(sdp, trans, tr);
			else
				trans = tr;
		}
	}

	log_refund(sdp, trans);

	/*  Actually do the stuff to commit the transaction  */

	error = disk_commit(sdp, trans);
	if (error)
		gfs_io_error(sdp);

	add_trans_to_ail(sdp, trans);

	if (log_distance(sdp, sdp->sd_log_head, sdp->sd_log_dump_last) * GFS_DUMPS_PER_LOG >=
	    sdp->sd_jdesc.ji_nsegment * sdp->sd_sb.sb_seg_size)
		set_bit(SDF_NEED_LOG_DUMP, &sdp->sd_flags);

 out:
	if (list_empty(&sdp->sd_log_incore))
		sdp->sd_vfs->s_dirt = FALSE;

	gfs_log_unlock(sdp);

	/*  Dump if we need to.  */

	if (test_bit(SDF_NEED_LOG_DUMP, &sdp->sd_flags))
		gfs_log_dump(sdp, FALSE);
}

/**
 * gfs_log_flush - flush the whole incore log
 * @sdp: the filesystem
 *
 */

void
gfs_log_flush(struct gfs_sbd *sdp)
{
	log_flush_internal(sdp, NULL);
}

/**
 * gfs_log_flush_glock - flush the incore log for a glock
 * @gl: the glock
 *
 */

void
gfs_log_flush_glock(struct gfs_glock *gl)
{
	log_flush_internal(gl->gl_sbd, gl);
}

/**
 * incore_commit - commit a transaction in-core
 * @sdp: the filesystem
 * @new_tr: the transaction to commit
 *
 * Add the transaction @new_tr to the end of the incore commit list.
 * Pull up and merge any previously committed transactions that share
 * locks.  Also pull up any rename transactions that need it.
 */

static void
incore_commit(struct gfs_sbd *sdp, struct gfs_trans *new_tr)
{
	struct gfs_log_element *le;
	struct gfs_trans *trans = NULL, *exist_tr;
	struct gfs_log_buf *lb;
	struct list_head *bmem;
	struct list_head *tmp, *head, *next;

	for (head = &new_tr->tr_elements, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		le = list_entry(tmp, struct gfs_log_element, le_list);

		/* Do overlap_trans log-op, if any, to find another
		   incore transaction with which we can combine new_tr */
		exist_tr = LO_OVERLAP_TRANS(sdp, le);
		if (!exist_tr)
			continue;

		if (exist_tr != trans) {
			/* remove trans from superblock's sd_log_incore list */
			list_del(&exist_tr->tr_list);

			/* Maybe there's more than one that can be combined.
			   If so, combine them together before merging new_tr */
			if (trans)
				trans_combine(sdp, trans, exist_tr);
			else
				trans = exist_tr;
		}
	}

	/* Yes, we can combine new_tr with pre-existing transaction(s) */
	if (trans) {
		trans->tr_file = __FILE__;
		trans->tr_line = __LINE__;
		trans->tr_seg_reserved += new_tr->tr_seg_reserved;
		trans->tr_flags |= new_tr->tr_flags;
		trans->tr_num_free_bufs += new_tr->tr_num_free_bufs;
		trans->tr_num_free_bmem += new_tr->tr_num_free_bmem;

		/* Move free log buffer descriptors to surviving trans */
		while (!list_empty(&new_tr->tr_free_bufs)) {
			lb = list_entry(new_tr->tr_free_bufs.next,
					struct gfs_log_buf, lb_list);
			list_move(&lb->lb_list, &trans->tr_free_bufs);
			new_tr->tr_num_free_bufs--;
		}

		/* Move free log buffers to surviving trans */
		while (!list_empty(&new_tr->tr_free_bmem)) {
			bmem = new_tr->tr_free_bmem.next;
			list_move(bmem, &trans->tr_free_bmem);
			new_tr->tr_num_free_bmem--;
		}
	} else
		trans = new_tr;

	/* Do incore_commit log-op for each *new* log element (in new_tr).
	   Each commit log-op removes its log element from "new_tr" LE list,
	   and attaches an LE to "trans" LE list; if there was no trans
	   combining, "new_tr" is the same transaction as "trans". */
	for (head = &new_tr->tr_elements, tmp = head->next, next = tmp->next;
	     tmp != head;
	     tmp = next, next = next->next) {
		le = list_entry(tmp, struct gfs_log_element, le_list);
		LO_INCORE_COMMIT(sdp, trans, le);
	}

	/* If we successfully combined transactions, new_tr should be empty */
	if (trans != new_tr) {
		gfs_assert_warn(sdp, !new_tr->tr_num_free_bufs);
		gfs_assert_warn(sdp, !new_tr->tr_num_free_bmem);
		gfs_assert_warn(sdp, list_empty(&new_tr->tr_elements));
		kfree(new_tr);
	}

	/* If we successfully combined transactions, we might have some log
	   segments that we reserved, and log buffers and buffer descriptors
	   that we allocated, but now don't need. */
	log_refund(sdp, trans);

	list_add(&trans->tr_list, &sdp->sd_log_incore);
}

/**
 * gfs_log_commit - Commit a transaction to the log
 * @sdp: the filesystem
 * @tr: the transaction
 *
 * Returns: errno
 */

void
gfs_log_commit(struct gfs_sbd *sdp, struct gfs_trans *tr)
{
	struct gfs_log_buf *lb;
	struct list_head *bmem;
	unsigned int num_mblks = 0, num_eblks = 0, num_bufs = 0, num_bmem = 0;
	unsigned int segments;

	/* Calculate actual log area needed for this trans */
	LO_TRANS_SIZE(sdp, tr, &num_mblks, &num_eblks, &num_bufs, &num_bmem);

	gfs_assert(sdp, num_mblks <= tr->tr_mblks_asked &&
		   num_eblks <= tr->tr_eblks_asked,
		   printk("GFS: fsid=%s: type = (%s, %u)\n"
			  "GFS: fsid=%s: num_mblks = %u, tr->tr_mblks_asked = %u\n"
			  "GFS: fsid=%s: num_eblks = %u, tr->tr_eblks_asked = %u\n",
			  sdp->sd_fsname, tr->tr_file, tr->tr_line,
			  sdp->sd_fsname, num_mblks, tr->tr_mblks_asked,
			  sdp->sd_fsname, num_eblks, tr->tr_eblks_asked););

	segments = gfs_blk2seg(sdp, num_bufs + 1);
	num_bufs += segments + 1;
	num_bmem += segments + 1;

	/* Alloc log buffer descriptors */
	while (num_bufs--) {
		lb = gmalloc(sizeof(struct gfs_log_buf));
		memset(lb, 0, sizeof(struct gfs_log_buf));
		list_add(&lb->lb_list, &tr->tr_free_bufs);
		tr->tr_num_free_bufs++;
	}
	/* Alloc log buffers */
	while (num_bmem--) {
		bmem = gmalloc(sdp->sd_sb.sb_bsize);
		list_add(bmem, &tr->tr_free_bmem);
		tr->tr_num_free_bmem++;
	}

	gfs_log_lock(sdp);

	incore_commit(sdp, tr);

	/* Flush log buffers to disk if we're over the threshold */
	if (sdp->sd_log_buffers > gfs_tune_get(sdp, gt_incore_log_blocks)) {
		gfs_log_unlock(sdp);
		gfs_log_flush(sdp);
	} else {
		sdp->sd_vfs->s_dirt = TRUE;
		gfs_log_unlock(sdp);
	}

}

/**
 * gfs_log_dump - make a Log Dump entry in the log
 * @sdp: the filesystem
 * @force: if TRUE, always make the dump even if one has been made recently
 *
 */

void
gfs_log_dump(struct gfs_sbd *sdp, int force)
{
	struct gfs_log_element *le;
	struct gfs_trans tr;
	struct gfs_log_buf *lb;
	struct list_head *bmem;
	unsigned int num_bufs, num_bmem;
	unsigned int segments;
	int error;

	if (test_and_set_bit(SDF_IN_LOG_DUMP, &sdp->sd_flags)) {
		gfs_assert(sdp, !force,);
		return;
	}

	memset(&tr, 0, sizeof(struct gfs_trans));
	INIT_LIST_HEAD(&tr.tr_elements);
	INIT_LIST_HEAD(&tr.tr_free_bufs);
	INIT_LIST_HEAD(&tr.tr_free_bmem);
	INIT_LIST_HEAD(&tr.tr_bufs);
	tr.tr_flags = TRF_LOG_DUMP;
	tr.tr_file = __FILE__;
	tr.tr_line = __LINE__;

	for (;;) {
		gfs_log_lock(sdp);

		if (!force && !test_bit(SDF_NEED_LOG_DUMP, &sdp->sd_flags))
			goto out;

		num_bufs = num_bmem = 0;
		LO_DUMP_SIZE(sdp, NULL, &num_bufs, &num_bmem);
		gfs_assert(sdp, num_bufs,);
		segments = gfs_blk2seg(sdp, num_bufs + 1);
		num_bufs += segments + 1;
		num_bmem += segments + 1;

		if (tr.tr_seg_reserved >= segments &&
		    tr.tr_num_free_bufs >= num_bufs &&
		    tr.tr_num_free_bmem >= num_bmem)
			break;

		gfs_log_unlock(sdp);

		if (tr.tr_seg_reserved < segments) {
			error = gfs_log_reserve(sdp,
						segments - tr.tr_seg_reserved,
						TRUE);
			gfs_assert(sdp, !error,);
			tr.tr_seg_reserved = segments;
		}
		while (tr.tr_num_free_bufs < num_bufs) {
			lb = gmalloc(sizeof(struct gfs_log_buf));
			memset(lb, 0, sizeof(struct gfs_log_buf));
			list_add(&lb->lb_list, &tr.tr_free_bufs);
			tr.tr_num_free_bufs++;
		}
		while (tr.tr_num_free_bmem < num_bmem) {
			bmem = gmalloc(sdp->sd_sb.sb_bsize);
			list_add(bmem, &tr.tr_free_bmem);
			tr.tr_num_free_bmem++;
		}
	}

	if (tr.tr_seg_reserved > segments) {
		spin_lock(&sdp->sd_log_seg_lock);
		sdp->sd_log_seg_free += tr.tr_seg_reserved - segments;
		gfs_assert(sdp, sdp->sd_log_seg_free + sdp->sd_log_seg_ail2 <=
			   sdp->sd_jdesc.ji_nsegment,);
		spin_unlock(&sdp->sd_log_seg_lock);
		tr.tr_seg_reserved = segments;
	}
	while (tr.tr_num_free_bufs > num_bufs) {
		lb = list_entry(tr.tr_free_bufs.next,
				struct gfs_log_buf, lb_list);
		list_del(&lb->lb_list);
		kfree(lb);
		tr.tr_num_free_bufs--;
	}
	while (tr.tr_num_free_bmem > num_bmem) {
		bmem = tr.tr_free_bmem.next;
		list_del(bmem);
		kfree(bmem);
		tr.tr_num_free_bmem--;
	}

	LO_BUILD_DUMP(sdp, &tr);

	error = disk_commit(sdp, &tr);
	if (error)
		gfs_io_error(sdp);

	while (!list_empty(&tr.tr_elements)) {
		le = list_entry(tr.tr_elements.next,
				struct gfs_log_element, le_list);
		LO_CLEAN_DUMP(sdp, le);
	}

	/* If there isn't anything in the AIL, we won't get back the log
	   space we reserved unless we do it ourselves. */

	if (list_empty(&sdp->sd_log_ail)) {
		spin_lock(&sdp->sd_log_seg_lock);
		sdp->sd_log_seg_free += tr.tr_seg_reserved;
		gfs_assert(sdp, sdp->sd_log_seg_free + sdp->sd_log_seg_ail2 <=
			   sdp->sd_jdesc.ji_nsegment,);
		spin_unlock(&sdp->sd_log_seg_lock);
	}

	clear_bit(SDF_NEED_LOG_DUMP, &sdp->sd_flags);

 out:
	gfs_log_unlock(sdp);
	clear_bit(SDF_IN_LOG_DUMP, &sdp->sd_flags);
}

/**
 * gfs_log_shutdown - write a shutdown header into a journal
 * @sdp: the filesystem
 *
 */

void
gfs_log_shutdown(struct gfs_sbd *sdp)
{
	struct gfs_log_buf *lb;
	char *bmem;
	struct gfs_log_header head;
	struct gfs_log_descriptor desc;
	unsigned int elements = 0;
	int error;

	lb = gmalloc(sizeof(struct gfs_log_buf));
	memset(lb, 0, sizeof(struct gfs_log_buf));
	bmem = gmalloc(sdp->sd_sb.sb_bsize);

	gfs_log_lock(sdp);

	gfs_assert_withdraw(sdp, list_empty(&sdp->sd_log_ail));
	gfs_assert_withdraw(sdp, sdp->sd_log_seg_free + sdp->sd_log_seg_ail2 ==
			    sdp->sd_jdesc.ji_nsegment);
	gfs_assert_withdraw(sdp, !sdp->sd_log_buffers);
	gfs_assert_withdraw(sdp, gfs_log_is_header(sdp, sdp->sd_log_head - 1));
	if (test_bit(SDF_SHUTDOWN, &sdp->sd_flags))
		goto out;

	/*  Build a "last" log descriptor  */

	memset(&desc, 0, sizeof(struct gfs_log_descriptor));
	desc.ld_header.mh_magic = GFS_MAGIC;
	desc.ld_header.mh_type = GFS_METATYPE_LD;
	desc.ld_header.mh_format = GFS_FORMAT_LD;
	desc.ld_type = GFS_LOG_DESC_LAST;
	desc.ld_length = sdp->sd_sb.sb_seg_size - 1;

	/*  Write the descriptor  */

	gfs_logbh_init(sdp, &lb->lb_bh, sdp->sd_log_head, bmem);
	memset(bmem, 0, sdp->sd_sb.sb_bsize);
	gfs_desc_out(&desc, lb->lb_bh.b_data);
	gfs_logbh_start(sdp, &lb->lb_bh);
	error = gfs_logbh_wait(sdp, &lb->lb_bh);
	gfs_logbh_uninit(sdp, &lb->lb_bh);

	if (error)
		goto out;

	/*  Move to the next header  */

	while (!gfs_log_is_header(sdp, sdp->sd_log_head))
		log_incr_head(sdp, &sdp->sd_log_head);

	LO_DUMP_SIZE(sdp, &elements, NULL, NULL);

	/*  Build the shutdown header  */

	memset(&head, 0, sizeof (struct gfs_log_header));
	head.lh_header.mh_magic = GFS_MAGIC;
	head.lh_header.mh_type = GFS_METATYPE_LH;
	head.lh_header.mh_format = GFS_FORMAT_LH;
	head.lh_flags = GFS_LOG_HEAD_UNMOUNT;
	head.lh_first = sdp->sd_log_head;
	head.lh_sequence = sdp->sd_sequence + 1;
	/*  Don't care about tail  */
	head.lh_last_dump = (elements) ? sdp->sd_log_dump_last : 0;

	/*  Write out the shutdown header  */

	gfs_logbh_init(sdp, &lb->lb_bh, sdp->sd_log_head, bmem);
	memset(bmem, 0, sdp->sd_sb.sb_bsize);
	gfs_log_header_out(&head, lb->lb_bh.b_data);
	gfs_log_header_out(&head,
			   lb->lb_bh.b_data + GFS_BASIC_BLOCK -
			   sizeof(struct gfs_log_header));
	gfs_logbh_start(sdp, &lb->lb_bh);
	gfs_logbh_wait(sdp, &lb->lb_bh);
	gfs_logbh_uninit(sdp, &lb->lb_bh);

   /* If a withdraw is called before we've a chance to relock the trans
    * lock, the sd_log_head points to the wrong place, and a umount will
    * fail on asserts because of this.
    * Adding one puts sd_log_head at a value that passes the assert.  The
    * value may not be correct for on disk, but we've withdrawn so there is
    * no more disk io.
    * If we're not withdrawn, the next io will grab the trans lock, which
    * will fill sd_log_head with the correct value.
    */
   sdp->sd_log_head += 1;

 out:
	gfs_log_unlock(sdp);

	kfree(lb);
	kfree(bmem);
}
