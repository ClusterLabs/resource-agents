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
#include "trans.h"
#include "unlinked.h"

/**
 * gfs_trans_print - Print a transaction to the console
 * @sdp: the filesystem
 * @tr: The GFS transaction
 * @where: Situation of transaction
 *
 */

void
gfs_trans_print(struct gfs_sbd *sdp, struct gfs_trans *tr, unsigned int where)
{
	struct gfs_log_element *le;
	struct list_head *tmp, *head;
	unsigned int mblks = 0, eblks = 0;

	LO_TRANS_SIZE(sdp, tr, &mblks, &eblks, NULL, NULL);

	printk("Transaction:  (%s, %u)\n", tr->tr_file, tr->tr_line);
	printk("  tr_mblks_asked = %u, tr_eblks_asked = %u, tr_seg_reserved = %u\n",
	       tr->tr_mblks_asked, tr->tr_eblks_asked, tr->tr_seg_reserved);
	printk("  mblks = %u, eblks = %u\n", mblks, eblks);
	printk("  tr_flags = 0x%.8X\n", tr->tr_flags);

	for (head = &tr->tr_elements, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		le = list_entry(tmp, struct gfs_log_element, le_list);
		LO_PRINT(sdp, le, where);
	}

	printk("End Trans\n");
}

/**
 * gfs_trans_begin_i - Prepare to start a transaction
 * @sdp: The GFS superblock
 * @meta_blocks: Reserve this many metadata blocks in the log
 * @extra_blocks: Number of non-metadata blocks to reserve
 *
 * Allocate the struct gfs_trans struct.
 * Grab a shared TRANSaction lock (protects this transaction from
 *   overlapping with unusual fs writes, e.g. journal replay, fs upgrade,
 *   while allowing simultaneous transaction writes throughout cluster).
 * Reserve space in the log.  @meta_blocks and @extra_blocks must indicate
 *   the worst case (maximum) size of the transaction.
 * Record this transaction as the *one* transaction being built by this
 *   Linux process, in current->journal_info.
 *
 * Returns: errno
 */

int
gfs_trans_begin_i(struct gfs_sbd *sdp,
		  unsigned int meta_blocks, unsigned int extra_blocks,
		  char *file, unsigned int line)
{
	struct gfs_trans *tr;
	unsigned int blocks;
	int error;

	tr = kmalloc(sizeof(struct gfs_trans), GFP_KERNEL);
	if (!tr)
		return -ENOMEM;
	memset(tr, 0, sizeof(struct gfs_trans));

	INIT_LIST_HEAD(&tr->tr_elements);
	INIT_LIST_HEAD(&tr->tr_free_bufs);
	INIT_LIST_HEAD(&tr->tr_free_bmem);
	INIT_LIST_HEAD(&tr->tr_bufs);
	INIT_LIST_HEAD(&tr->tr_ail_bufs);
	tr->tr_file = file;
	tr->tr_line = line;

	error = -ENOMEM;
	tr->tr_t_gh = gfs_holder_get(sdp->sd_trans_gl, LM_ST_SHARED, 0);
	if (!tr->tr_t_gh)
		goto fail;

	error = gfs_glock_nq(tr->tr_t_gh);
	if (error)
		goto fail_holder_put;

	if (test_bit(SDF_ROFS, &sdp->sd_flags)) {
		tr->tr_t_gh->gh_flags |= GL_NOCACHE;
		error = -EROFS;
		goto fail_gunlock;
	}

	/*  Do log reservation  */

	tr->tr_mblks_asked = meta_blocks;
	tr->tr_eblks_asked = extra_blocks;

	blocks = 1;
	if (meta_blocks)
		blocks += gfs_struct2blk(sdp, meta_blocks,
					 sizeof(struct gfs_block_tag)) +
			meta_blocks;
	blocks += extra_blocks;
	tr->tr_seg_reserved = gfs_blk2seg(sdp, blocks);

	error = gfs_log_reserve(sdp, tr->tr_seg_reserved, FALSE);
	if (error)
		goto fail_gunlock;

	gfs_assert(sdp, !get_transaction,);
	set_transaction(tr);

	return 0;

 fail_gunlock:
	gfs_glock_dq(tr->tr_t_gh);

 fail_holder_put:
	gfs_holder_put(tr->tr_t_gh);

 fail:
	kfree(tr);

	return error;
}

/**
 * gfs_trans_end - End a transaction
 * @sdp: The GFS superblock
 *
 * If buffers were actually added to the transaction,
 * commit it.
 *
 */

void
gfs_trans_end(struct gfs_sbd *sdp)
{
	struct gfs_trans *tr;
	struct gfs_holder *t_gh;
	struct list_head *tmp, *head;
	struct gfs_log_element *le;

	/* Linux task struct indicates current new trans for this process.
	 * We're done building it, so set it to NULL */
	tr = get_transaction;
	gfs_assert(sdp, tr,);
	set_transaction(NULL);

	t_gh = tr->tr_t_gh;
	tr->tr_t_gh = NULL;

	/* If no buffers were ever added to trans, forget it */
	if (list_empty(&tr->tr_elements)) {
		gfs_log_release(sdp, tr->tr_seg_reserved);
		kfree(tr);

		gfs_glock_dq(t_gh);
		gfs_holder_put(t_gh);

		return;
	}

	/* Do trans_end log-operation for each log element */
	for (head = &tr->tr_elements, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		le = list_entry(tmp, struct gfs_log_element, le_list);
		LO_TRANS_END(sdp, le);
	}

	gfs_log_commit(sdp, tr);

	gfs_glock_dq(t_gh);
	gfs_holder_put(t_gh);

	if (sdp->sd_vfs->s_flags & MS_SYNCHRONOUS)
		gfs_log_flush(sdp);
}

/**
 * gfs_trans_add_gl - Add a glock to a transaction
 * @gl: the glock
 *
 * If not already attached, add the given glock to this process's transaction.
 *
 * Even though no glock info will be written to the on-disk log, the glocks
 *   associated with a transaction provide bridges by which to combine
 *   a just-built transaction with an earlier incore committed transaction
 *   that was protected by the same glock.  See incore_commit().
 *   Combining transactions makes for more efficient logging.
 *
 * Note that more than one glock may be associated with a single transaction.
 *   However, a given glock protects no more than *one* transaction at a
 *   given stage in the transaction pipeline (i.e. new or incore-committed).
 *   After all, the process holds the glock EX (so no other process can be
 *   building a separate trans protected by this glock), and the process can
 *   build only one transaction at a time.
 *
 * Rules:
 *   This process must hold the glock in EXclusive mode, since we're going
 *   to be writing to something protected by this glock.
 */

void
gfs_trans_add_gl(struct gfs_glock *gl)
{
	if (!gl->gl_new_le.le_trans) {
		gfs_assert_withdraw(gl->gl_sbd,
				    gfs_glock_is_locked_by_me(gl) &&
				    gfs_glock_is_held_excl(gl));
		gfs_glock_hold(gl); /* Released in glock_trans_end() */

		/* Ask for eventual flush of (meta)data protected by this glock,
		   once trans is complete and logged.  */
		set_bit(GLF_DIRTY, &gl->gl_flags);

		/* Invoke generic_le_add() */
		LO_ADD(gl->gl_sbd, &gl->gl_new_le);
		gl->gl_new_le.le_trans->tr_num_gl++;
	}
}

/**
 * gfs_trans_add_bh - Add a to-be-modified buffer to the current transaction
 * @gl: the glock the buffer belongs to
 * @bh: The buffer to add
 *
 * Add a to-be-modified buffer to the current being-built (i.e. new) trans,
 *   and pin the buffer in memory.
 *
 * Caller must hold the glock protecting this buffer.
 *
 * Call this as many times as you want during transaction formation.  It does
 * its attachment work only once.  After buffer is attached to trans, the
 * process building the trans can modify the buffer again and again (calling
 * this function before each change).  Only the final result (within this trans)
 * will be written to log.  A good example is when allocating blocks in an RG,
 * a given bitmap buffer may be updated many times within a transaction.
 *
 * Note:  This final result will also be written to its in-place location,
 *  unless this transaction gets combined with a later transaction,
 *  in which case only the later result will go to in-place.
 *
 */

void
gfs_trans_add_bh(struct gfs_glock *gl, struct buffer_head *bh)
{
	struct gfs_sbd *sdp = gl->gl_sbd;
	struct gfs_bufdata *bd;

	/* Make sure GFS private info struct is attached to buffer head */
	bd = get_v2bd(bh);
	if (!bd) {
		gfs_attach_bufdata(bh, gl);
		bd = get_v2bd(bh);
	}

	/* If buffer has already been attached to trans, we're done */
	if (bd->bd_new_le.le_trans)
		return;

	gfs_meta_check(sdp, bh);

	gfs_assert(sdp, bd->bd_gl == gl,);

	/* Make sure glock is attached to trans */
	if (!gl->gl_new_le.le_trans)
		gfs_trans_add_gl(gl);

	gfs_dpin(sdp, bh);

	/* Attach buffer to trans */
	LO_ADD(sdp, &bd->bd_new_le);
	bd->bd_new_le.le_trans->tr_num_buf++;
}

/**
 * gfs_trans_add_unlinked - Add an unlinked or dealloced tag to
 *      the current transaction
 * @sdp: the filesystem
 * @type: the type of entry
 * @inum: the inode number
 *
 * Returns: the unlinked structure
 */

struct gfs_unlinked *
gfs_trans_add_unlinked(struct gfs_sbd *sdp, unsigned int type,
		       struct gfs_inum *inum)
{
	struct gfs_unlinked *ul;

	/* Find in fileystem's unlinked list, or create */
	ul = gfs_unlinked_get(sdp, inum, CREATE);

	LO_ADD(sdp, &ul->ul_new_le);

	switch (type) {
	case GFS_LOG_DESC_IUL:
		set_bit(ULF_NEW_UL, &ul->ul_flags);
		ul->ul_new_le.le_trans->tr_num_iul++;
		break;
	case GFS_LOG_DESC_IDA:
		clear_bit(ULF_NEW_UL, &ul->ul_flags);
		ul->ul_new_le.le_trans->tr_num_ida++;
		break;
	default:
		gfs_assert(sdp, FALSE,);
		break;
	}

	return ul;
}

/**
 * gfs_trans_add_quota - Add quota changes to a transaction
 * @sdp: the filesystem
 * @change: The number of blocks allocated (positive) or freed (negative)
 * @uid: the user ID doing the change
 * @gid: the group ID doing the change
 *
 */

void
gfs_trans_add_quota(struct gfs_sbd *sdp, int64_t change,
		    uint32_t uid, uint32_t gid)
{
	struct gfs_trans *tr;
	struct list_head *tmp, *head, *next;
	struct gfs_log_element *le;
	struct gfs_quota_le *ql;
	int found_uid, found_gid;
	int error;

	if (!gfs_tune_get(sdp, gt_quota_account))
		return;
	if (gfs_assert_warn(sdp, change))
		return;

	found_uid = (uid == NO_QUOTA_CHANGE);
	found_gid = (gid == NO_QUOTA_CHANGE);

	if (gfs_assert_warn(sdp, !found_uid || !found_gid))
		return;

	tr = get_transaction;
	gfs_assert(sdp, tr,);

	for (head = &tr->tr_elements, tmp = head->next, next = tmp->next;
	     tmp != head;
	     tmp = next, next = next->next) {
		le = list_entry(tmp, struct gfs_log_element, le_list);
		if (le->le_ops != &gfs_quota_lops)
			continue;

		ql = container_of(le, struct gfs_quota_le, ql_le);

		if (test_bit(QDF_USER, &ql->ql_data->qd_flags)) {
			if (ql->ql_data->qd_id == uid) {
				ql->ql_change += change;

				spin_lock(&sdp->sd_quota_lock);
				ql->ql_data->qd_change_new += change;
				spin_unlock(&sdp->sd_quota_lock);

				list_del(&le->le_list);

				if (ql->ql_change)
					list_add(&le->le_list,
						 &tr->tr_elements);
				else {
					gfs_quota_put(sdp, ql->ql_data);
					kfree(ql);
					tr->tr_num_q--;
				}

				gfs_assert(sdp, !found_uid,);
				found_uid = TRUE;
				if (found_gid)
					break;
			}
		} else {
			if (ql->ql_data->qd_id == gid) {
				ql->ql_change += change;

				spin_lock(&sdp->sd_quota_lock);
				ql->ql_data->qd_change_new += change;
				spin_unlock(&sdp->sd_quota_lock);

				list_del(&le->le_list);

				if (ql->ql_change)
					list_add(&le->le_list,
						 &tr->tr_elements);
				else {
					gfs_quota_put(sdp, ql->ql_data);
					kfree(ql);
					tr->tr_num_q--;
				}

				gfs_assert(sdp, !found_gid,);
				found_gid = TRUE;
				if (found_uid)
					break;
			}
		}
	}

	while (!found_uid || !found_gid) {
		ql = gmalloc(sizeof(struct gfs_quota_le));
		memset(ql, 0, sizeof(struct gfs_quota_le));

		INIT_LE(&ql->ql_le, &gfs_quota_lops);

		if (found_uid) {
			error = gfs_quota_get(sdp, FALSE, gid,
					      NO_CREATE,
					      &ql->ql_data);
			found_gid = TRUE;
		} else {
			error = gfs_quota_get(sdp, TRUE, uid,
					      NO_CREATE,
					      &ql->ql_data);
			found_uid = TRUE;
		}

		gfs_assert(sdp, !error && ql->ql_data,);

		ql->ql_change = change;

		spin_lock(&sdp->sd_quota_lock);
		ql->ql_data->qd_change_new += change;
		spin_unlock(&sdp->sd_quota_lock);

		LO_ADD(sdp, &ql->ql_le);
		tr->tr_num_q++;
	}
}
