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
 *
 * Returns: 0 on success, -EXXX on failure
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
	tr->tr_t_gh = gfs_holder_get(sdp->sd_trans_gl, LM_ST_SHARED, 0);

	error = gfs_glock_nq(tr->tr_t_gh);
	if (error)
		goto fail;

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

	GFS_ASSERT_SBD(!current_transaction, sdp,);
	current_transaction = tr;

	return 0;

 fail_gunlock:
	gfs_glock_dq(tr->tr_t_gh);

 fail:
	gfs_holder_put(tr->tr_t_gh);
	kfree(tr);

	return error;
}

/**
 * gfs_trans_end - End a transaction
 * @sdp: The GFS superblock
 *
 * If buffers were actually added to the transaction,
 * commit it.
 */

void
gfs_trans_end(struct gfs_sbd *sdp)
{
	struct gfs_trans *tr;
	struct gfs_holder *t_gh;
	struct list_head *tmp, *head;
	struct gfs_log_element *le;

	tr = current_transaction;
	GFS_ASSERT_SBD(tr, sdp,);
	current_transaction = NULL;

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
 * Add the given glock to this process's transaction
 */

void
gfs_trans_add_gl(struct gfs_glock *gl)
{
	if (!gl->gl_new_le.le_trans) {
		GFS_ASSERT_GLOCK(gfs_glock_is_locked_by_me(gl) &&
				 gfs_glock_is_held_excl(gl), gl,);
		gfs_glock_hold(gl); /* Released in glock_trans_end() */

		set_bit(GLF_DIRTY, &gl->gl_flags);

		LO_ADD(gl->gl_sbd, &gl->gl_new_le);
		gl->gl_new_le.le_trans->tr_num_gl++;
	}
}

/**
 * gfs_trans_add_bh - Add a buffer to the current transaction
 * @gl: the glock the buffer belongs to
 * @bh: The buffer to add
 *
 * Add a buffer to the current transaction.  The glock for the buffer
 * should be held.  This pins the buffer as well.
 *
 * Call this as many times as you want during transaction formation.
 * It only does its work once.
 *
 */

void
gfs_trans_add_bh(struct gfs_glock *gl, struct buffer_head *bh)
{
	struct gfs_sbd *sdp = gl->gl_sbd;
	struct gfs_bufdata *bd;

	bd = bh2bd(bh);
	if (!bd) {
		gfs_attach_bufdata(bh, gl);
		bd = bh2bd(bh);
	}

	if (bd->bd_new_le.le_trans)
		return;

	gfs_meta_check(sdp, bh);

	GFS_ASSERT_GLOCK(bd->bd_gl == gl, gl,);

	if (!gl->gl_new_le.le_trans)
		gfs_trans_add_gl(gl);

	gfs_dpin(sdp, bh);

	LO_ADD(sdp, &bd->bd_new_le);
	bd->bd_new_le.le_trans->tr_num_buf++;
}

/**
 * gfs_trans_add_unlinked - Add a unlinked/dealloced tag to the current transaction
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
		GFS_ASSERT_SBD(FALSE, sdp,);
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

	if (!sdp->sd_tune.gt_quota_account)
		return;

	GFS_ASSERT_SBD(change, sdp,);

	found_uid = (uid == NO_QUOTA_CHANGE);
	found_gid = (gid == NO_QUOTA_CHANGE);

	GFS_ASSERT_SBD(!found_uid || !found_gid, sdp,);

	tr = current_transaction;
	GFS_ASSERT_SBD(tr, sdp,);

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

				GFS_ASSERT_SBD(!found_uid, sdp,);
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

				GFS_ASSERT_SBD(!found_gid, sdp,);
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

		GFS_ASSERT_SBD(!error && ql->ql_data, sdp,);

		ql->ql_change = change;

		spin_lock(&sdp->sd_quota_lock);
		ql->ql_data->qd_change_new += change;
		spin_unlock(&sdp->sd_quota_lock);

		LO_ADD(sdp, &ql->ql_le);
		tr->tr_num_q++;
	}
}
