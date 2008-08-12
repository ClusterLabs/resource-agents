#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>

#include "gfs.h"
#include "dio.h"
#include "glock.h"
#include "log.h"
#include "lops.h"
#include "quota.h"
#include "recovery.h"
#include "trans.h"
#include "unlinked.h"

/**
 * generic_le_add - generic routine to add a log element to a transaction
 * @sdp: the filesystem
 * @le: the log entry
 *
 */

static void
generic_le_add(struct gfs_sbd *sdp, struct gfs_log_element *le)
{
	struct gfs_trans *tr;

	/* Make sure it's not attached to a transaction already */
	gfs_assert(sdp, le->le_ops &&
		   !le->le_trans &&
		   list_empty(&le->le_list),);

	/* Attach it to the (one) transaction being built by this process */
	tr = get_transaction;
	gfs_assert(sdp, tr,);

	le->le_trans = tr;
	list_add(&le->le_list, &tr->tr_elements);
}

/**
 * glock_trans_end - drop a glock reference
 * @sdp: the filesystem
 * @le: the log element
 *
 * Called before incore-committing a transaction
 * Release reference that was taken in gfs_trans_add_gl()
 */

static void
glock_trans_end(struct gfs_sbd *sdp, struct gfs_log_element *le)
{
	struct gfs_glock *gl = container_of(le, struct gfs_glock, gl_new_le);

	gfs_assert(sdp, gfs_glock_is_locked_by_me(gl) &&
		   gfs_glock_is_held_excl(gl),);
	gfs_glock_put(gl);
}

/**
 * glock_print - print debug info about a log element
 * @sdp: the filesystem
 * @le: the log element
 * @where: is this a new transaction or a incore transaction
 *
 */

static void
glock_print(struct gfs_sbd *sdp, struct gfs_log_element *le, unsigned int where)
{
	struct gfs_glock *gl;

	switch (where) {
	case TRANS_IS_NEW:
		gl = container_of(le, struct gfs_glock, gl_new_le);
		break;
	case TRANS_IS_INCORE:
		gl = container_of(le, struct gfs_glock, gl_incore_le);
		break;
	default:
		gfs_assert_warn(sdp, FALSE);
		return;
	}

	printk("  Glock:  (%u, %"PRIu64")\n",
	       gl->gl_name.ln_type,
	       gl->gl_name.ln_number);
}

/**
 * glock_overlap_trans - Find any incore transactions that might overlap with
 *   (i.e. be combinable with the transaction containing) this LE
 * @sdp: the filesystem
 * @le: the log element
 *
 * Transactions that share a given glock are combinable.
 *
 * For a glock, the scope of the "search" is just the (max) one unique incore
 *   committed transaction to which the glock may be attached via its
 *   gl->gl_incore_le embedded log element.  This trans may have previously
 *   been combined with other transactions, though (i.e. previous
 *   incore committed transactions that shared the same glock).
 *  
 * Called as a beginning part of the incore commit of a transaction.
 */

static struct gfs_trans *
glock_overlap_trans(struct gfs_sbd *sdp, struct gfs_log_element *le)
{
	struct gfs_glock *gl = container_of(le, struct gfs_glock, gl_new_le);

	return gl->gl_incore_le.le_trans;
}

/**
 * glock_incore_commit - commit this LE to the incore log
 * @sdp: the filesystem
 * @tr: the being-incore-committed transaction this LE is to be a part of
 * @le: the log element (should be a gl->gl_new_le), which is attached
 *      to a "new" (just-ended) transaction.
 *      
 * Attach glock's gl_incore_le to the being-incore-committed trans' LE list.
 * Remove glock's gl_new_le from the just-ended new trans' LE list.
 * If the just-ended new trans (le->le_trans) was combined (in incore_commit())
 *   with a pre-existing incore trans (tr), this function effectively moves
 *   the LE from the new to the combined incore trans.
 * If there was no combining, then the new trans itself is being committed
 *   (le->le_trans == tr); this function simply replaces the gl_new_le with a
 *   gl_incore_le on the trans' LE list.
 * 
 * Make sure that this glock's gl_incore_le is attached to one and only one
 *   incore-committed transaction's (this one's) tr_elements list.
 *   One transaction (instead of a list of transactions) is sufficient,
 *   because incore_commit() combines multiple transactions that share a glock
 *   into one trans.
 * Since transactions can contain multiple glocks, there are multiple
 *   possibilities for shared glocks, therefore multiple potential "bridges"
 *   for combining transactions.
 */

static void
glock_incore_commit(struct gfs_sbd *sdp, struct gfs_trans *tr,
		    struct gfs_log_element *le)
{
	struct gfs_glock *gl = container_of(le, struct gfs_glock, gl_new_le);

	/* Transactions were combined, based on this glock */
	if (gl->gl_incore_le.le_trans)
		gfs_assert(sdp, gl->gl_incore_le.le_trans == tr,);
	else {
		/* Attach gl->gl_incore_le to being-committed trans */
		gl->gl_incore_le.le_trans = tr;
		list_add(&gl->gl_incore_le.le_list, &tr->tr_elements);

		/* If transactions were combined (via another shared glock),
		   the combined trans is getting a new glock log element */
		if (tr != le->le_trans)
			tr->tr_num_gl++;
	}

	/* Remove gl->gl_new_le from "new" trans */
	le->le_trans = NULL;
	list_del_init(&le->le_list);
}

/**
 * glock_add_to_ail - Add this LE to the AIL
 * @sdp: the filesystem
 * @le: the log element
 *
 * Glocks don't really get added to AIL (there's nothing to write to disk),
 * they just get removed from the transaction at this time.
 */

static void
glock_add_to_ail(struct gfs_sbd *sdp, struct gfs_log_element *le)
{
	le->le_trans = NULL;
	list_del_init(&le->le_list);
}

/**
 * glock_trans_combine - combine two incore transactions
 * @sdp: the filesystem
 * @tr: the surviving transaction
 * @new_tr: the transaction that's going to disappear
 *
 */

static void
glock_trans_combine(struct gfs_sbd *sdp, struct gfs_trans *tr,
		    struct gfs_trans *new_tr)
{
	tr->tr_num_gl += new_tr->tr_num_gl;
}

/**
 * buf_print - print debug info about a log element
 * @sdp: the filesystem
 * @le: the log element
 * @where: is this a new transaction or a incore transaction
 *
 */

static void
buf_print(struct gfs_sbd *sdp, struct gfs_log_element *le, unsigned int where)
{
	struct gfs_bufdata *bd;

	switch (where) {
	case TRANS_IS_NEW:
		bd = container_of(le, struct gfs_bufdata, bd_new_le);
		break;
	case TRANS_IS_INCORE:
		bd = container_of(le, struct gfs_bufdata, bd_incore_le);
		break;
	default:
		gfs_assert_warn(sdp, FALSE);
		return;
	}

	printk("  Buffer:  %"PRIu64"\n", (uint64_t)bd->bd_bh->b_blocknr);
}

/**
 * buf_incore_commit - commit this buffer LE to the incore log
 * @sdp: the filesystem
 * @tr: the incore transaction this LE is a part of
 * @le: the log element for the "new" (just now complete) trans
 *
 * Invoked from incore_commit().
 * Move this buffer from "new" stage to "incore committed" stage of the
 *   transaction pipeline.
 * If this buffer was already attached to a pre-existing incore trans, GFS is
 *   combining the new and incore transactions; decrement buffer's recursive
 *   pin count that was incremented when it was added to the new transaction,
 *   and remove the reference to the "new" (being swallowed) trans.
 * Else, move this buffer's attach point from "new" to "incore" embedded LE
 *   (same transaction, just new status) and add this buf to (incore) trans'
 *   LE list.
 */

static void
buf_incore_commit(struct gfs_sbd *sdp, struct gfs_trans *tr,
		  struct gfs_log_element *le)
{
	struct gfs_bufdata *bd = container_of(le, struct gfs_bufdata, bd_new_le);

	/* We've completed our (atomic) changes to this buffer for this trans.
	   We no longer need the frozen copy.  If frozen copy was not written
	   to on-disk log already, there's no longer a need to; we can now
	   write the "real" buffer (with more up-to-date content) instead. */
	if (bd->bd_frozen) {
		kfree(bd->bd_frozen);
		bd->bd_frozen = NULL;
	}

	/* New trans being combined with pre-existing incore trans? */
	if (bd->bd_incore_le.le_trans) {
		gfs_assert(sdp, bd->bd_incore_le.le_trans == tr,);
		gfs_dunpin(sdp, bd->bd_bh, NULL);
	} else {
		bd->bd_incore_le.le_trans = tr;
		list_add(&bd->bd_incore_le.le_list, &tr->tr_elements);
		if (tr != le->le_trans)
			tr->tr_num_buf++;

		sdp->sd_log_buffers++;
	}

	/* Reset buffer's bd_new_le */
	le->le_trans = NULL;
	list_del_init(&le->le_list);
}

/**
 * buf_add_to_ail - Add this LE to the AIL
 * @sdp: the filesystem
 * @le: the log element
 *
 */

static void
buf_add_to_ail(struct gfs_sbd *sdp, struct gfs_log_element *le)
{
	struct gfs_bufdata *bd = container_of(le,
					       struct gfs_bufdata,
					       bd_incore_le);

	gfs_dunpin(sdp, bd->bd_bh, le->le_trans);

	le->le_trans = NULL;
	list_del_init(&le->le_list);

	gfs_assert(sdp, sdp->sd_log_buffers,);
	sdp->sd_log_buffers--;
}

/**
 * buf_trans_size - compute how much space the LE class takes up in a transaction
 * @sdp: the filesystem
 * @tr: the transaction
 * @mblks: the number of regular metadata blocks
 * @eblks: the number of extra blocks
 * @blocks: the number of log blocks
 * @bmem: the number of buffer-sized chunks of memory we need
 *
 */

static void
buf_trans_size(struct gfs_sbd *sdp, struct gfs_trans *tr,
	       unsigned int *mblks, unsigned int *eblks,
	       unsigned int *blocks, unsigned int *bmem)
{
	unsigned int cblks;

	if (tr->tr_num_buf) {
		cblks = gfs_struct2blk(sdp, tr->tr_num_buf,
				       sizeof(struct gfs_block_tag));

		if (mblks)
			*mblks += tr->tr_num_buf;
		if (blocks)
			*blocks += tr->tr_num_buf + cblks;
		if (bmem)
			*bmem += cblks;
	}
}

/**
 * buf_trans_combine - combine two incore transactions
 * @sdp: the filesystem
 * @tr: the surviving transaction
 * @new_tr: the transaction that's going to disappear
 *
 */

static void
buf_trans_combine(struct gfs_sbd *sdp, struct gfs_trans *tr,
		  struct gfs_trans *new_tr)
{
	tr->tr_num_buf += new_tr->tr_num_buf;
}

/**
 * increment_generation - increment the generation number in metadata buffer
 * @sdp: the filesystem
 * @bd: the struct gfs_bufdata structure associated with the buffer
 *
 * Increment the generation # of the most recent buffer contents, as well as
 *   that of frozen buffer, if any.  If there is a frozen buffer, only *it*
 *   will be going to the log now ... in this case, the current buffer will
 *   have its gen # incremented again later, when it gets written to log.
 * Gen # is used by journal recovery (replay_block()) to determine whether
 *   to overwrite an inplace block with the logged block contents.
 */

static void
increment_generation(struct gfs_sbd *sdp, struct gfs_bufdata *bd)
{
	struct gfs_meta_header *mh, *mh2;
	uint64_t tmp64;

	mh = (struct gfs_meta_header *)bd->bd_bh->b_data;

	tmp64 = gfs64_to_cpu(mh->mh_generation) + 1;
	tmp64 = cpu_to_gfs64(tmp64);

	if (bd->bd_frozen) {
		mh2 = (struct gfs_meta_header *)bd->bd_frozen;
		gfs_assert(sdp, mh->mh_generation == mh2->mh_generation,);
		mh2->mh_generation = tmp64;
	}
	mh->mh_generation = tmp64;
}

/**
 * buf_build_bhlist - create the buffers that will make up the ondisk part of a transaction
 * @sdp: the filesystem
 * @tr: the transaction
 *
 * Create the log (transaction) descriptor block
 */

static void
buf_build_bhlist(struct gfs_sbd *sdp, struct gfs_trans *tr)
{
	struct list_head *tmp, *head;
	struct gfs_log_element *le;
	struct gfs_bufdata *bd;
	struct gfs_log_descriptor desc;
	struct gfs_block_tag tag;
	struct gfs_log_buf *clb = NULL;
	unsigned int num_ctl;
	unsigned int offset = sizeof(struct gfs_log_descriptor);
	unsigned int x, bufs;

	if (!tr->tr_num_buf)
		return;

	/* set up control buffers for descriptor and tags */

	num_ctl = gfs_struct2blk(sdp, tr->tr_num_buf,
				 sizeof(struct gfs_block_tag));

	for (x = 0; x < num_ctl; x++) {
		if (clb)
			gfs_log_get_buf(sdp, tr);
		else
			clb = gfs_log_get_buf(sdp, tr);
	}

	/* Init and copy log descriptor into 1st control block */
	memset(&desc, 0, sizeof(struct gfs_log_descriptor));
	desc.ld_header.mh_magic = GFS_MAGIC;
	desc.ld_header.mh_type = GFS_METATYPE_LD;
	desc.ld_header.mh_format = GFS_FORMAT_LD;
	desc.ld_type = GFS_LOG_DESC_METADATA;
	desc.ld_length = num_ctl + tr->tr_num_buf;
	desc.ld_data1 = tr->tr_num_buf;
	gfs_desc_out(&desc, clb->lb_bh.b_data);

	x = 1;
	bufs = 0;

	for (head = &tr->tr_elements, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		le = list_entry(tmp, struct gfs_log_element, le_list);

		/* Skip over non-buffer (e.g. glock, unlinked, etc.) LEs */
		if (le->le_ops != &gfs_buf_lops)
			continue;

		bd = container_of(le, struct gfs_bufdata, bd_incore_le);

		gfs_meta_check(sdp, bd->bd_bh);

		gfs_lock_buffer(bd->bd_bh);

		increment_generation(sdp, bd);

		/* Create "fake" buffer head to write block to on-disk log.  Use
		   frozen copy if another transaction is modifying the "real"
		   buffer contents.  Unlock real bh after log write completes,
		   so Linux can write real contents to inplace block. */
		gfs_log_fake_buf(sdp, tr,
				 (bd->bd_frozen) ? bd->bd_frozen : bd->bd_bh->b_data,
				 bd->bd_bh);

		/* find another buffer for tags if we're overflowing this one */
		if (offset + sizeof(struct gfs_block_tag) > sdp->sd_sb.sb_bsize) {
			clb = list_entry(clb->lb_list.prev,
					 struct gfs_log_buf, lb_list);
			if (gfs_log_is_header(sdp, clb->lb_bh.b_blocknr))
				clb = list_entry(clb->lb_list.prev,
						 struct gfs_log_buf, lb_list);
			x++;
			offset = 0;
		}

		/* Write this LE's tag into a control buffer */
		memset(&tag, 0, sizeof(struct gfs_block_tag));
		tag.bt_blkno = bd->bd_bh->b_blocknr;

		gfs_block_tag_out(&tag, clb->lb_bh.b_data + offset);

		offset += sizeof(struct gfs_block_tag);
		bufs++;
	}

	gfs_assert(sdp, x == num_ctl,);
	gfs_assert(sdp, bufs == tr->tr_num_buf,);
}

/**
 * buf_before_scan - called before journal replay
 * @sdp: the filesystem
 * @jid: the journal ID about to be replayed
 * @head: the current head of the log
 * @pass: the pass through the journal
 *
 */

static void
buf_before_scan(struct gfs_sbd *sdp, unsigned int jid,
		struct gfs_log_header *head, unsigned int pass)
{
	if (pass == GFS_RECPASS_A1)
		sdp->sd_recovery_replays =
			sdp->sd_recovery_skips =
			sdp->sd_recovery_sames = 0;
}

/**
 * replay_block - Replay a single metadata block
 * @sdp: the filesystem
 * @jdesc: the struct gfs_jindex structure for the journal being replayed
 * @gl: the journal's glock
 * @tag: the block tag describing the inplace location of the block
 * @blkno: the location of the log's copy of the block
 *
 * Returns: errno
 *
 * Read in-place block from disk
 * Read log (journal) block from disk
 * Compare generation numbers
 * Copy log block to in-place block on-disk if:
 *   log generation # > in-place generation #
 *   OR generation #s are ==, but data contained in block is different (corrupt)
 */

static int
replay_block(struct gfs_sbd *sdp, struct gfs_jindex *jdesc,
	     struct gfs_glock *gl, struct gfs_block_tag *tag, uint64_t blkno)
{
	struct buffer_head *inplace_bh, *log_bh;
	struct gfs_meta_header inplace_mh, log_mh;
	int replay_block = TRUE;
	int error = 0;

	gfs_replay_check(sdp);

	/* Warning:  Using a real buffer here instead of a tempbh can be bad
	   on a OS that won't support multiple simultaneous buffers for the
	   same block on different glocks. */

	error = gfs_dread(gl, tag->bt_blkno,
			  DIO_START | DIO_WAIT, &inplace_bh);
	if (error)
		return error;
	if (gfs_meta_check(sdp, inplace_bh)) {
		brelse(inplace_bh);
		return -EIO;
	}
	gfs_meta_header_in(&inplace_mh, inplace_bh->b_data);

	error = gfs_dread(gl, blkno, DIO_START | DIO_WAIT, &log_bh);
	if (error) {
		brelse(inplace_bh);
		return error;
	}
	if (gfs_meta_check(sdp, log_bh)) {
		brelse(inplace_bh);
		brelse(log_bh);
		return -EIO;
	}
	gfs_meta_header_in(&log_mh, log_bh->b_data);

	if (log_mh.mh_generation < inplace_mh.mh_generation) {
		replay_block = FALSE;
		sdp->sd_recovery_skips++;
	} else if (log_mh.mh_generation == inplace_mh.mh_generation) {
		if (memcmp(log_bh->b_data,
			   inplace_bh->b_data,
			   sdp->sd_sb.sb_bsize) == 0) {
			replay_block = FALSE;
			sdp->sd_recovery_sames++;
		}
	}

	if (replay_block) {
		memcpy(inplace_bh->b_data,
		       log_bh->b_data,
		       sdp->sd_sb.sb_bsize);

		error = gfs_replay_buf(gl, inplace_bh);
		if (!error)
			sdp->sd_recovery_replays++;
	}

	brelse(log_bh);
	brelse(inplace_bh);

	return error;
}

/**
 * buf_scan_elements - Replay a metadata log descriptor
 * @sdp: the filesystem
 * @jdesc: the struct gfs_jindex structure for the journal being replayed
 * @gl: the journal's glock
 * @start: the starting block of the descriptor
 * @desc: the descriptor structure
 * @pass: the pass through the journal
 *
 * Returns: errno
 */

static int
buf_scan_elements(struct gfs_sbd *sdp, struct gfs_jindex *jdesc,
		  struct gfs_glock *gl, uint64_t start,
		  struct gfs_log_descriptor *desc, unsigned int pass)
{
	struct gfs_block_tag tag;
	struct buffer_head *bh;
	uint64_t cblk = start;
	unsigned int num_tags = desc->ld_data1;
	unsigned int offset = sizeof(struct gfs_log_descriptor);
	unsigned int x;
	int error;

	if (pass != GFS_RECPASS_A1)
		return 0;
	if (desc->ld_type != GFS_LOG_DESC_METADATA)
		return 0;

	x = gfs_struct2blk(sdp, num_tags, sizeof(struct gfs_block_tag));
	while (x--) {
		error = gfs_increment_blkno(sdp, jdesc, gl, &start, TRUE);
		if (error)
			return error;
	}

	for (;;) {
		gfs_assert(sdp, num_tags,);

		error = gfs_dread(gl, cblk, DIO_START | DIO_WAIT, &bh);
		if (error)
			return error;

		/* Do readahead for the inplace blocks in this control block */
		{
			unsigned int o2 = offset;
			unsigned int nt2 = num_tags;

			while (o2 + sizeof(struct gfs_block_tag) <=
			       sdp->sd_sb.sb_bsize) {
				gfs_block_tag_in(&tag, bh->b_data + o2);
				gfs_start_ra(gl, tag.bt_blkno, 1);
				if (!--nt2)
					break;
				o2 += sizeof(struct gfs_block_tag);
			}
		}

		while (offset + sizeof(struct gfs_block_tag) <=
		       sdp->sd_sb.sb_bsize) {
			gfs_block_tag_in(&tag, bh->b_data + offset);

			error = replay_block(sdp, jdesc, gl, &tag, start);
			if (error)
				goto out_drelse;

			if (!--num_tags)
				goto out_drelse;

			error = gfs_increment_blkno(sdp, jdesc, gl, &start, TRUE);
			if (error)
				goto out_drelse;

			offset += sizeof(struct gfs_block_tag);
		}

		brelse(bh);

		error = gfs_increment_blkno(sdp, jdesc, gl, &cblk, TRUE);
		if (error)
			return error;

		offset = 0;
	}

	return 0;

 out_drelse:
	brelse(bh);

	return error;
}

/**
 * buf_after_scan - called after journal replay
 * @sdp: the filesystem
 * @jid: the journal ID about to be replayed
 * @pass: the pass through the journal
 *
 */

static void
buf_after_scan(struct gfs_sbd *sdp, unsigned int jid, unsigned int pass)
{
	if (pass == GFS_RECPASS_A1) {
		printk("GFS: fsid=%s: jid=%u: Replayed %u of %u blocks\n",
		       sdp->sd_fsname, jid,
		       sdp->sd_recovery_replays,
		       sdp->sd_recovery_replays + sdp->sd_recovery_skips +
		       sdp->sd_recovery_sames);
		printk("GFS: fsid=%s: jid=%u: replays = %u, skips = %u, sames = %u\n",
		       sdp->sd_fsname, jid, sdp->sd_recovery_replays,
		       sdp->sd_recovery_skips, sdp->sd_recovery_sames);
	}
}

/**
 * unlinked_print - print debug info about a log element
 * @sdp: the filesystem
 * @le: the log element
 * @where: is this a new transaction or a incore transaction
 *
 */

static void
unlinked_print(struct gfs_sbd *sdp, struct gfs_log_element *le,
	       unsigned int where)
{
	struct gfs_unlinked *ul;
	char *type;

	switch (where) {
	case TRANS_IS_NEW:
		ul = container_of(le, struct gfs_unlinked, ul_new_le);
		type = (test_bit(ULF_NEW_UL, &ul->ul_flags)) ?
			"unlink" : "dealloc";
		break;
	case TRANS_IS_INCORE:
		ul = container_of(le, struct gfs_unlinked, ul_incore_le);
		type = (test_bit(ULF_INCORE_UL, &ul->ul_flags)) ?
			"unlink" : "dealloc";
		break;
	default:
		gfs_assert_warn(sdp, FALSE);
		return;
	}

	printk("  unlinked:  %"PRIu64"/%"PRIu64", %s\n",
	       ul->ul_inum.no_formal_ino, ul->ul_inum.no_addr,
	       type);
}

/**
 * unlinked_incore_commit - commit this LE to the incore log
 * @sdp: the filesystem
 * @tr: the incore transaction this LE is a part of
 * @le: the log element
 *
 */

static void
unlinked_incore_commit(struct gfs_sbd *sdp, struct gfs_trans *tr,
		       struct gfs_log_element *le)
{
	struct gfs_unlinked *ul = container_of(le,
					       struct gfs_unlinked,
					       ul_new_le);
	int n = !!test_bit(ULF_NEW_UL, &ul->ul_flags);
	int i = !!test_bit(ULF_INCORE_UL, &ul->ul_flags);

	if (ul->ul_incore_le.le_trans) {
		gfs_assert(sdp, ul->ul_incore_le.le_trans == tr,);
		gfs_assert(sdp, n != i,);

		ul->ul_incore_le.le_trans = NULL;
		list_del_init(&ul->ul_incore_le.le_list);
		gfs_unlinked_put(sdp, ul);

		if (i) {
			gfs_assert(sdp, tr->tr_num_iul,);
			tr->tr_num_iul--;
		} else {
			gfs_assert(sdp, tr->tr_num_ida,);
			tr->tr_num_ida--;
		}
	} else {
		gfs_unlinked_hold(sdp, ul);
		ul->ul_incore_le.le_trans = tr;
		list_add(&ul->ul_incore_le.le_list, &tr->tr_elements);

		if (n) {
			set_bit(ULF_INCORE_UL, &ul->ul_flags);
			if (tr != le->le_trans)
				tr->tr_num_iul++;
		} else {
			clear_bit(ULF_INCORE_UL, &ul->ul_flags);
			if (tr != le->le_trans)
				tr->tr_num_ida++;
		}
	}

	if (n) {
		gfs_unlinked_hold(sdp, ul);
		gfs_assert(sdp, !test_bit(ULF_IC_LIST, &ul->ul_flags),);
		set_bit(ULF_IC_LIST, &ul->ul_flags);
		atomic_inc(&sdp->sd_unlinked_ic_count);
	} else {
		gfs_assert(sdp, test_bit(ULF_IC_LIST, &ul->ul_flags),);
		clear_bit(ULF_IC_LIST, &ul->ul_flags);
		gfs_unlinked_put(sdp, ul);
		gfs_assert(sdp, atomic_read(&sdp->sd_unlinked_ic_count),);
		atomic_dec(&sdp->sd_unlinked_ic_count);
	}

	le->le_trans = NULL;
	list_del_init(&le->le_list);
	gfs_unlinked_put(sdp, ul);
}

/**
 * unlinked_add_to_ail - Add this LE to the AIL
 * @sdp: the filesystem
 * @le: the log element
 *
 */

static void
unlinked_add_to_ail(struct gfs_sbd *sdp, struct gfs_log_element *le)
{
	struct gfs_unlinked *ul = container_of(le,
						struct gfs_unlinked,
						ul_incore_le);
	int i = !!test_bit(ULF_INCORE_UL, &ul->ul_flags);

	if (i) {
		gfs_unlinked_hold(sdp, ul);
		gfs_assert(sdp, !test_bit(ULF_OD_LIST, &ul->ul_flags),);
		set_bit(ULF_OD_LIST, &ul->ul_flags);
		atomic_inc(&sdp->sd_unlinked_od_count);
	} else {
		gfs_assert(sdp, test_bit(ULF_OD_LIST, &ul->ul_flags),);
		clear_bit(ULF_OD_LIST, &ul->ul_flags);
		gfs_unlinked_put(sdp, ul);
		gfs_assert(sdp, atomic_read(&sdp->sd_unlinked_od_count),);
		atomic_dec(&sdp->sd_unlinked_od_count);
	}

	le->le_trans = NULL;
	list_del_init(&le->le_list);
	gfs_unlinked_put(sdp, ul);
}

/**
 * unlinked_clean_dump - clean up a LE after a log dump
 * @sdp: the filesystem
 * @le: the log element
 *
 */

static void
unlinked_clean_dump(struct gfs_sbd *sdp, struct gfs_log_element *le)
{
	le->le_trans = NULL;
	list_del_init(&le->le_list);
}

/**
 * unlinked_trans_size - compute how much space the LE class takes up in a transaction
 * @sdp: the filesystem
 * @tr: the transaction
 * @mblks: the number of regular metadata blocks
 * @eblks: the number of extra blocks
 * @blocks: the number of log blocks
 * @bmem: the number of buffer-sized chunks of memory we need
 *
 */

static void
unlinked_trans_size(struct gfs_sbd *sdp, struct gfs_trans *tr,
		    unsigned int *mblks, unsigned int *eblks,
		    unsigned int *blocks, unsigned int *bmem)
{
	unsigned int ublks = 0;

	if (tr->tr_num_iul)
		ublks = gfs_struct2blk(sdp, tr->tr_num_iul,
				       sizeof(struct gfs_inum));
	if (tr->tr_num_ida)
		ublks += gfs_struct2blk(sdp, tr->tr_num_ida,
					sizeof(struct gfs_inum));

	if (eblks)
		*eblks += ublks;
	if (blocks)
		*blocks += ublks;
	if (bmem)
		*bmem += ublks;
}

/**
 * unlinked_trans_combine - combine two incore transactions
 * @sdp: the filesystem
 * @tr: the surviving transaction
 * @new_tr: the transaction that's going to disappear
 *
 */

static void
unlinked_trans_combine(struct gfs_sbd *sdp, struct gfs_trans *tr,
		       struct gfs_trans *new_tr)
{
	tr->tr_num_iul += new_tr->tr_num_iul;
	tr->tr_num_ida += new_tr->tr_num_ida;
}

/**
 * unlinked_build_bhlist - create the buffers that will make up the ondisk part of a transaction
 * @sdp: the filesystem
 * @tr: the transaction
 *
 * For unlinked and/or deallocated inode log elements (separately):
 *   Get a log block
 *   Create a log descriptor in beginning of that block
 *   Fill rest of block with gfs_inum structs to identify each inode
 *     that became unlinked/deallocated during this transaction.
 *   Get another log block if needed, continue filling with gfs_inums.
 */

static void
unlinked_build_bhlist(struct gfs_sbd *sdp, struct gfs_trans *tr)
{
	struct list_head *tmp, *head;
	struct gfs_log_element *le;
	struct gfs_unlinked *ul;
	struct gfs_log_descriptor desc;
	struct gfs_log_buf *lb;
	unsigned int pass = 2;
	unsigned int type, number;
	unsigned int offset, entries;

	/* 2 passes:  1st for Unlinked, 2nd for De-Alloced inodes,
	     unless this is a log dump:  just 1 pass, for Unlinked */
	while (pass--) {
		if (tr->tr_flags & TRF_LOG_DUMP) {
			if (pass) {
				type = GFS_LOG_DESC_IUL;
				number = tr->tr_num_iul;
			} else
				break;
		} else {
			if (pass) {
				type = GFS_LOG_DESC_IUL;
				number = tr->tr_num_iul;
			} else {
				type = GFS_LOG_DESC_IDA;
				number = tr->tr_num_ida;
			}

			if (!number)
				continue;
		}

		lb = gfs_log_get_buf(sdp, tr);

		/* Header:  log descriptor */
		memset(&desc, 0, sizeof(struct gfs_log_descriptor));
		desc.ld_header.mh_magic = GFS_MAGIC;
		desc.ld_header.mh_type = GFS_METATYPE_LD;
		desc.ld_header.mh_format = GFS_FORMAT_LD;
		desc.ld_type = type;
		desc.ld_length = gfs_struct2blk(sdp, number, sizeof(struct gfs_inum));
		desc.ld_data1 = (tr->tr_flags & TRF_LOG_DUMP) ? TRUE : FALSE;
		gfs_desc_out(&desc, lb->lb_bh.b_data);

		offset = sizeof(struct gfs_log_descriptor);
		entries = 0;

		/* Look through transaction's log elements for Unlinked LEs */
		for (head = &tr->tr_elements, tmp = head->next;
		     tmp != head;
		     tmp = tmp->next) {
			le = list_entry(tmp, struct gfs_log_element, le_list);
			if (le->le_ops != &gfs_unlinked_lops)
				continue;
			if (tr->tr_flags & TRF_LOG_DUMP)
				ul = container_of(le,
						  struct gfs_unlinked,
						  ul_ondisk_le);
			else {
				ul = container_of(le,
						  struct gfs_unlinked,
						  ul_incore_le);
				if (!!test_bit(ULF_INCORE_UL, &ul->ul_flags) != pass)
					continue;
			}

			if (offset + sizeof(struct gfs_inum) > sdp->sd_sb.sb_bsize) {
				offset = 0;
				lb = gfs_log_get_buf(sdp, tr);
			}

			/* Payload:  write the inode identifier */
			gfs_inum_out(&ul->ul_inum,
				     lb->lb_bh.b_data + offset);

			offset += sizeof(struct gfs_inum);
			entries++;
		}

		gfs_assert(sdp, entries == number,);
	}
}

/**
 * unlinked_dump_size - compute how much space the LE class takes up in a log dump
 * @sdp: the filesystem
 * @elements: the number of log elements in the dump
 * @blocks: the number of blocks in the dump
 * @bmem: the number of buffer-sized chunks of memory we need
 *
 */

static void
unlinked_dump_size(struct gfs_sbd *sdp, unsigned int *elements,
		   unsigned int *blocks, unsigned int *bmem)
{
	unsigned int c = atomic_read(&sdp->sd_unlinked_od_count);
	unsigned int b = gfs_struct2blk(sdp, c, sizeof(struct gfs_inum));

	if (elements)
		*elements += c;
	if (blocks)
		*blocks += b;
	if (bmem)
		*bmem += b;
}

/**
 * unlinked_build_dump - create a transaction that represents a log dump for this LE class
 * @sdp: the filesystem
 * @tr: the transaction to fill
 *
 */

static void
unlinked_build_dump(struct gfs_sbd *sdp, struct gfs_trans *tr)
{
	struct list_head *tmp, *head;
	struct gfs_unlinked *ul;
	unsigned int x = 0;

	tr->tr_num_iul = atomic_read(&sdp->sd_unlinked_od_count);

	spin_lock(&sdp->sd_unlinked_lock);

	for (head = &sdp->sd_unlinked_list, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		ul = list_entry(tmp, struct gfs_unlinked, ul_list);
		if (!test_bit(ULF_OD_LIST, &ul->ul_flags))
			continue;

		gfs_assert(sdp, !ul->ul_ondisk_le.le_trans,);
		ul->ul_ondisk_le.le_trans = tr;
		list_add(&ul->ul_ondisk_le.le_list, &tr->tr_elements);

		x++;
	}

	spin_unlock(&sdp->sd_unlinked_lock);

	gfs_assert(sdp, x == atomic_read(&sdp->sd_unlinked_od_count),);
}

/**
 * unlinked_before_scan - called before a log dump is recovered
 * @sdp: the filesystem
 * @jid: the journal ID about to be scanned
 * @head: the current head of the log
 * @pass: the pass through the journal
 *
 */

static void
unlinked_before_scan(struct gfs_sbd *sdp, unsigned int jid,
		     struct gfs_log_header *head, unsigned int pass)
{
	if (pass == GFS_RECPASS_B1)
		clear_bit(SDF_FOUND_UL_DUMP, &sdp->sd_flags);
}

/**
 * unlinked_scan_elements - scan unlinked inodes from the journal
 * @sdp: the filesystem
 * @jdesc: the struct gfs_jindex structure for the journal being scaned
 * @gl: the journal's glock
 * @start: the starting block of the descriptor
 * @desc: the descriptor structure
 * @pass: the pass through the journal
 *
 * Returns: errno
 */

static int
unlinked_scan_elements(struct gfs_sbd *sdp, struct gfs_jindex *jdesc,
		       struct gfs_glock *gl, uint64_t start,
		       struct gfs_log_descriptor *desc, unsigned int pass)
{
	struct gfs_inum inum;
	struct buffer_head *bh;
	unsigned int offset = sizeof(struct gfs_log_descriptor);
	unsigned int x;
	int error;

	if (pass != GFS_RECPASS_B1)
		return 0;

	switch (desc->ld_type) {
	case GFS_LOG_DESC_IUL:
		if (test_bit(SDF_FOUND_UL_DUMP, &sdp->sd_flags))
			gfs_assert(sdp, !desc->ld_data1,);
		else {
			gfs_assert(sdp, desc->ld_data1,);
			set_bit(SDF_FOUND_UL_DUMP, &sdp->sd_flags);
		}
		break;

	case GFS_LOG_DESC_IDA:
		gfs_assert(sdp, test_bit(SDF_FOUND_UL_DUMP, &sdp->sd_flags),);
		break;

	default:
		return 0;
	}

	for (x = 0; x < desc->ld_length; x++) {
		error = gfs_dread(gl, start, DIO_START | DIO_WAIT, &bh);
		if (error)
			return error;

		for (;
		     offset + sizeof(struct gfs_inum) <= sdp->sd_sb.sb_bsize;
		     offset += sizeof(struct gfs_inum)) {
			gfs_inum_in(&inum, bh->b_data + offset);

			if (inum.no_addr)
				gfs_unlinked_merge(sdp, desc->ld_type, &inum);
		}

		brelse(bh);

		error = gfs_increment_blkno(sdp, jdesc, gl, &start, TRUE);
		if (error)
			return error;

		offset = 0;
	}

	return 0;
}

/**
 * unlinked_after_scan - called after a log dump is recovered
 * @sdp: the filesystem
 * @jid: the journal ID about to be scanned
 * @pass: the pass through the journal
 *
 */

static void
unlinked_after_scan(struct gfs_sbd *sdp, unsigned int jid, unsigned int pass)
{
	if (pass == GFS_RECPASS_B1) {
		gfs_assert(sdp, test_bit(SDF_FOUND_UL_DUMP, &sdp->sd_flags),);
		printk("GFS: fsid=%s: Found %d unlinked inodes\n",
		       sdp->sd_fsname, atomic_read(&sdp->sd_unlinked_ic_count));
	}
}

/**
 * quota_print - print debug info about a log element
 * @sdp: the filesystem
 * @le: the log element
 * @where: is this a new transaction or a incore transaction
 *
 */

static void
quota_print(struct gfs_sbd *sdp, struct gfs_log_element *le, unsigned int where)
{
	struct gfs_quota_le *ql;

	ql = container_of(le, struct gfs_quota_le, ql_le);
	printk("  quota:  %s %u:  %"PRId64" blocks\n",
	       (test_bit(QDF_USER, &ql->ql_data->qd_flags)) ? "user" : "group",
	       ql->ql_data->qd_id, ql->ql_change);
}

/**
 * quota_incore_commit - commit this LE to the incore log
 * @sdp: the filesystem
 * @tr: the incore transaction this LE is a part of
 * @le: the log element
 *
 */

static void
quota_incore_commit(struct gfs_sbd *sdp, struct gfs_trans *tr,
		    struct gfs_log_element *le)
{
	struct gfs_quota_le *ql = container_of(le, struct gfs_quota_le, ql_le);
	struct gfs_quota_data *qd = ql->ql_data;

	gfs_assert(sdp, ql->ql_change,);

	/*  Make this change under the sd_quota_lock, so other processes
	   checking qd_change_ic don't have to acquire the log lock.  */

	spin_lock(&sdp->sd_quota_lock);
	qd->qd_change_new -= ql->ql_change;
	qd->qd_change_ic += ql->ql_change;
	spin_unlock(&sdp->sd_quota_lock);

	if (le->le_trans == tr)
		list_add(&ql->ql_data_list, &qd->qd_le_list);
	else {
		struct list_head *tmp, *head;
		struct gfs_quota_le *tmp_ql;
		int found = FALSE;

		for (head = &qd->qd_le_list, tmp = head->next;
		     tmp != head;
		     tmp = tmp->next) {
			tmp_ql = list_entry(tmp, struct gfs_quota_le, ql_data_list);
			if (tmp_ql->ql_le.le_trans != tr)
				continue;

			tmp_ql->ql_change += ql->ql_change;

			list_del(&le->le_list);
			gfs_quota_put(sdp, qd);
			kfree(ql);

			if (!tmp_ql->ql_change) {
				list_del(&tmp_ql->ql_data_list);
				list_del(&tmp_ql->ql_le.le_list);
				gfs_quota_put(sdp, tmp_ql->ql_data);
				kfree(tmp_ql);
				tr->tr_num_q--;
			}

			found = TRUE;
			break;
		}

		if (!found) {
			le->le_trans = tr;
			list_move(&le->le_list, &tr->tr_elements);
			tr->tr_num_q++;
			list_add(&ql->ql_data_list, &qd->qd_le_list);
		}
	}
}

/**
 * quota_add_to_ail - Add this LE to the AIL
 * @sdp: the filesystem
 * @le: the log element
 *
 */

static void
quota_add_to_ail(struct gfs_sbd *sdp, struct gfs_log_element *le)
{
	struct gfs_quota_le *ql = container_of(le, struct gfs_quota_le, ql_le);
	struct gfs_quota_data *qd = ql->ql_data;

	qd->qd_change_od += ql->ql_change;
	if (qd->qd_change_od) {
		if (!test_bit(QDF_OD_LIST, &qd->qd_flags)) {
			gfs_quota_hold(sdp, qd);
			set_bit(QDF_OD_LIST, &qd->qd_flags);
			atomic_inc(&sdp->sd_quota_od_count);
		}
	} else {
		gfs_assert(sdp, test_bit(QDF_OD_LIST, &qd->qd_flags),);
		clear_bit(QDF_OD_LIST, &qd->qd_flags);
		gfs_quota_put(sdp, qd);
		gfs_assert(sdp, atomic_read(&sdp->sd_quota_od_count),);
		atomic_dec(&sdp->sd_quota_od_count);
	}

	list_del(&ql->ql_data_list);
	list_del(&le->le_list);
	gfs_quota_put(sdp, qd);
	kfree(ql);
}

/**
 * quota_clean_dump - clean up a LE after a log dump
 * @sdp: the filesystem
 * @le: the log element
 *
 */

static void
quota_clean_dump(struct gfs_sbd *sdp, struct gfs_log_element *le)
{
	le->le_trans = NULL;
	list_del_init(&le->le_list);
}

/**
 * quota_trans_size - compute how much space the LE class takes up in a transaction
 * @sdp: the filesystem
 * @tr: the transaction
 * @mblks: the number of regular metadata blocks
 * @eblks: the number of extra blocks
 * @blocks: the number of log blocks
 * @bmem: the number of buffer-sized chunks of memory we need
 *
 */

static void
quota_trans_size(struct gfs_sbd *sdp, struct gfs_trans *tr,
		 unsigned int *mblks, unsigned int *eblks,
		 unsigned int *blocks, unsigned int *bmem)
{
	unsigned int qblks;

	if (tr->tr_num_q) {
		qblks = gfs_struct2blk(sdp, tr->tr_num_q,
				       sizeof(struct gfs_quota_tag));

		if (eblks)
			*eblks += qblks;
		if (blocks)
			*blocks += qblks;
		if (bmem)
			*bmem += qblks;
	}
}

/**
 * quota_trans_combine - combine two incore transactions
 * @sdp: the filesystem
 * @tr: the surviving transaction
 * @new_tr: the transaction that's going to disappear
 *
 */

static void
quota_trans_combine(struct gfs_sbd *sdp, struct gfs_trans *tr,
		    struct gfs_trans *new_tr)
{
	tr->tr_num_q += new_tr->tr_num_q;
}

/**
 * quota_build_bhlist - create the buffers that will make up the ondisk part of a transaction
 * @sdp: the filesystem
 * @tr: the transaction
 *
 */

static void
quota_build_bhlist(struct gfs_sbd *sdp, struct gfs_trans *tr)
{
	struct list_head *tmp, *head;
	struct gfs_log_element *le;
	struct gfs_quota_le *ql;
	struct gfs_log_descriptor desc;
	struct gfs_quota_tag tag;
	struct gfs_log_buf *lb;
	unsigned int offset = sizeof(struct gfs_log_descriptor), entries = 0;

	if (!tr->tr_num_q && !(tr->tr_flags & TRF_LOG_DUMP))
		return;

	lb = gfs_log_get_buf(sdp, tr);

	memset(&desc, 0, sizeof(struct gfs_log_descriptor));
	desc.ld_header.mh_magic = GFS_MAGIC;
	desc.ld_header.mh_type = GFS_METATYPE_LD;
	desc.ld_header.mh_format = GFS_FORMAT_LD;
	desc.ld_type = GFS_LOG_DESC_Q;
	desc.ld_length = gfs_struct2blk(sdp, tr->tr_num_q,
					sizeof(struct gfs_quota_tag));
	desc.ld_data1 = tr->tr_num_q;
	desc.ld_data2 = (tr->tr_flags & TRF_LOG_DUMP) ? TRUE : FALSE;
	gfs_desc_out(&desc, lb->lb_bh.b_data);

	for (head = &tr->tr_elements, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		le = list_entry(tmp, struct gfs_log_element, le_list);
		if (le->le_ops != &gfs_quota_lops)
			continue;

		ql = container_of(le, struct gfs_quota_le, ql_le);

		if (offset + sizeof(struct gfs_quota_tag) >
		    sdp->sd_sb.sb_bsize) {
			offset = 0;
			lb = gfs_log_get_buf(sdp, tr);
		}

		memset(&tag, 0, sizeof(struct gfs_quota_tag));
		tag.qt_change = ql->ql_change;
		tag.qt_flags = (test_bit(QDF_USER, &ql->ql_data->qd_flags)) ?
			GFS_QTF_USER : 0;
		tag.qt_id = ql->ql_data->qd_id;

		gfs_quota_tag_out(&tag, lb->lb_bh.b_data + offset);

		offset += sizeof(struct gfs_quota_tag);
		entries++;
	}

	gfs_assert(sdp, entries == tr->tr_num_q,);
}

/**
 * quota_dump_size - compute how much space the LE class takes up in a log dump
 * @sdp: the filesystem
 * @elements: the number of log elements in the dump
 * @blocks: the number of blocks in the dump
 * @bmem: the number of buffer-sized chunks of memory we need
 *
 */

static void
quota_dump_size(struct gfs_sbd *sdp, unsigned int *elements,
		unsigned int *blocks, unsigned int *bmem)
{
	unsigned int c = atomic_read(&sdp->sd_quota_od_count);
	unsigned int b = gfs_struct2blk(sdp, c, sizeof(struct gfs_quota_tag));

	if (elements)
		*elements += c;
	if (blocks)
		*blocks += b;
	if (bmem)
		*bmem += b;
}

/**
 * quota_build_dump - create a transaction that represents a log dump for this LE class
 * @sdp: the filesystem
 * @tr: the transaction to fill
 *
 */

static void
quota_build_dump(struct gfs_sbd *sdp, struct gfs_trans *tr)
{
	struct list_head *tmp, *head;
	struct gfs_quota_data *qd;
	struct gfs_quota_le *ql;
	unsigned int x = 0;

	tr->tr_num_q = atomic_read(&sdp->sd_quota_od_count);

	spin_lock(&sdp->sd_quota_lock);

	for (head = &sdp->sd_quota_list, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		qd = list_entry(tmp, struct gfs_quota_data, qd_list);
		if (!test_bit(QDF_OD_LIST, &qd->qd_flags))
			continue;

		ql = &qd->qd_ondisk_ql;

		ql->ql_le.le_ops = &gfs_quota_lops;
		gfs_assert(sdp, !ql->ql_le.le_trans,);
		ql->ql_le.le_trans = tr;
		list_add(&ql->ql_le.le_list, &tr->tr_elements);

		ql->ql_data = qd;
		ql->ql_change = qd->qd_change_od;

		x++;
	}

	spin_unlock(&sdp->sd_quota_lock);

	gfs_assert(sdp, x == atomic_read(&sdp->sd_quota_od_count),);
}

/**
 * quota_before_scan - called before a log dump is recovered
 * @sdp: the filesystem
 * @jid: the journal ID about to be scanned
 * @head: the current head of the log
 * @pass: the pass through the journal
 *
 */

static void
quota_before_scan(struct gfs_sbd *sdp, unsigned int jid,
		  struct gfs_log_header *head, unsigned int pass)
{
	if (pass == GFS_RECPASS_B1)
		clear_bit(SDF_FOUND_Q_DUMP, &sdp->sd_flags);
}

/**
 * quota_scan_elements - scan quota inodes from the journal
 * @sdp: the filesystem
 * @jdesc: the struct gfs_jindex structure for the journal being scaned
 * @gl: the journal's glock
 * @start: the starting block of the descriptor
 * @desc: the descriptor structure
 * @pass: the pass through the journal
 *
 * Returns: errno
 */

static int
quota_scan_elements(struct gfs_sbd *sdp, struct gfs_jindex *jdesc,
		    struct gfs_glock *gl, uint64_t start,
		    struct gfs_log_descriptor *desc, unsigned int pass)
{
	struct gfs_quota_tag tag;
	struct buffer_head *bh;
	unsigned int num_tags = desc->ld_data1;
	unsigned int offset = sizeof(struct gfs_log_descriptor);
	unsigned int x;
	int error;

	if (pass != GFS_RECPASS_B1)
		return 0;
	if (desc->ld_type != GFS_LOG_DESC_Q)
		return 0;

	if (test_bit(SDF_FOUND_Q_DUMP, &sdp->sd_flags))
		gfs_assert(sdp, !desc->ld_data2,);
	else {
		gfs_assert(sdp, desc->ld_data2,);
		set_bit(SDF_FOUND_Q_DUMP, &sdp->sd_flags);
	}

	if (!num_tags)
		return 0;

	for (x = 0; x < desc->ld_length; x++) {
		error = gfs_dread(gl, start, DIO_START | DIO_WAIT, &bh);
		if (error)
			return error;

		while (offset + sizeof(struct gfs_quota_tag) <=
		       sdp->sd_sb.sb_bsize) {
			gfs_quota_tag_in(&tag, bh->b_data + offset);

			error = gfs_quota_merge(sdp, &tag);
			if (error)
				goto out_drelse;

			if (!--num_tags)
				goto out_drelse;

			offset += sizeof(struct gfs_quota_tag);
		}

		brelse(bh);

		error = gfs_increment_blkno(sdp, jdesc, gl, &start, TRUE);
		if (error)
			return error;

		offset = 0;
	}

	return 0;

 out_drelse:
	brelse(bh);

	return error;
}

/**
 * quota_after_scan - called after a log dump is recovered
 * @sdp: the filesystem
 * @jid: the journal ID about to be scanned
 * @pass: the pass through the journal
 *
 */

static void
quota_after_scan(struct gfs_sbd *sdp, unsigned int jid, unsigned int pass)
{
	if (pass == GFS_RECPASS_B1) {
		gfs_assert(sdp, !sdp->sd_sb.sb_quota_di.no_formal_ino ||
			   test_bit(SDF_FOUND_Q_DUMP, &sdp->sd_flags),);
		printk("GFS: fsid=%s: Found quota changes for %d IDs\n",
		       sdp->sd_fsname, atomic_read(&sdp->sd_quota_od_count));
	}
}

struct gfs_log_operations gfs_glock_lops = {
	.lo_add = generic_le_add,
	.lo_trans_end = glock_trans_end,
	.lo_print = glock_print,
	.lo_overlap_trans = glock_overlap_trans,
	.lo_incore_commit = glock_incore_commit,
	.lo_add_to_ail = glock_add_to_ail,
	.lo_trans_combine = glock_trans_combine,
	.lo_name = "glock"
};

struct gfs_log_operations gfs_buf_lops = {
	.lo_add = generic_le_add,
	.lo_print = buf_print,
	.lo_incore_commit = buf_incore_commit,
	.lo_add_to_ail = buf_add_to_ail,
	.lo_trans_size = buf_trans_size,
	.lo_trans_combine = buf_trans_combine,
	.lo_build_bhlist = buf_build_bhlist,
	.lo_before_scan = buf_before_scan,
	.lo_scan_elements = buf_scan_elements,
	.lo_after_scan = buf_after_scan,
	.lo_name = "buf"
};

struct gfs_log_operations gfs_unlinked_lops = {
	.lo_add = generic_le_add,
	.lo_print = unlinked_print,
	.lo_incore_commit = unlinked_incore_commit,
	.lo_add_to_ail = unlinked_add_to_ail,
	.lo_clean_dump = unlinked_clean_dump,
	.lo_trans_size = unlinked_trans_size,
	.lo_trans_combine = unlinked_trans_combine,
	.lo_build_bhlist = unlinked_build_bhlist,
	.lo_dump_size = unlinked_dump_size,
	.lo_build_dump = unlinked_build_dump,
	.lo_before_scan = unlinked_before_scan,
	.lo_scan_elements = unlinked_scan_elements,
	.lo_after_scan = unlinked_after_scan,
	.lo_name = "unlinked"
};

struct gfs_log_operations gfs_quota_lops = {
	.lo_add = generic_le_add,
	.lo_print = quota_print,
	.lo_incore_commit = quota_incore_commit,
	.lo_add_to_ail = quota_add_to_ail,
	.lo_clean_dump = quota_clean_dump,
	.lo_trans_size = quota_trans_size,
	.lo_trans_combine = quota_trans_combine,
	.lo_build_bhlist = quota_build_bhlist,
	.lo_dump_size = quota_dump_size,
	.lo_build_dump = quota_build_dump,
	.lo_before_scan = quota_before_scan,
	.lo_scan_elements = quota_scan_elements,
	.lo_after_scan = quota_after_scan,
	.lo_name = "quota"
};

struct gfs_log_operations *gfs_log_ops[] = {
	&gfs_glock_lops,
	&gfs_buf_lops,
	&gfs_unlinked_lops,
	&gfs_quota_lops,
	NULL
};
