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

#include "gfs2.h"
#include "dio.h"
#include "glock.h"
#include "log.h"
#include "lops.h"
#include "trans.h"

/**
 * gfs2_trans_begin_i - Prepare to start a transaction
 * @sdp: The GFS2 superblock
 * @meta_blocks: Reserve this many metadata blocks in the log
 * @extra_blocks: Number of non-metadata blocks to reserve
 *
 * Allocate the struct gfs2_trans struct.
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
gfs2_trans_begin_i(struct gfs2_sbd *sdp,
		  unsigned int blocks, unsigned int revokes,
		  char *file, unsigned int line)
{
	ENTER(G2FN_TRANS_BEGIN_I)
	struct gfs2_trans *tr;
	int error;

	if (gfs2_assert_warn(sdp, !get_transaction) ||
	    gfs2_assert_warn(sdp, blocks || revokes)) {
		printk("GFS2: fsid=%s: (%s, %u)\n",
		       sdp->sd_fsname, file, line);
		RETURN(G2FN_TRANS_BEGIN_I, -EINVAL);
	}

	tr = kmalloc(sizeof(struct gfs2_trans), GFP_KERNEL);
	if (!tr)
		RETURN(G2FN_TRANS_BEGIN_I, -ENOMEM);

	memset(tr, 0, sizeof(struct gfs2_trans));
	tr->tr_file = file;
	tr->tr_line = line;
	tr->tr_blocks = blocks;
	tr->tr_revokes = revokes;
	tr->tr_reserved = 1;
	if (blocks)
		tr->tr_reserved += 1 + blocks;
	if (revokes)
		tr->tr_reserved += gfs2_struct2blk(sdp, revokes, sizeof(uint64_t));
	INIT_LIST_HEAD(&tr->tr_list_buf);

	error = -ENOMEM;
	tr->tr_t_gh = gfs2_holder_get(sdp->sd_trans_gl, LM_ST_SHARED, 0);
	if (!tr->tr_t_gh)
		goto fail;

	error = gfs2_glock_nq(tr->tr_t_gh);
	if (error)
		goto fail_holder_put;

	if (test_bit(SDF_ROFS, &sdp->sd_flags)) {
		tr->tr_t_gh->gh_flags |= GL_NOCACHE;
		error = -EROFS;
		goto fail_gunlock;
	}

	error = gfs2_log_reserve(sdp, tr->tr_reserved);
	if (error)
		goto fail_gunlock;

	set_transaction(tr);

	RETURN(G2FN_TRANS_BEGIN_I, 0);

 fail_gunlock:
	gfs2_glock_dq(tr->tr_t_gh);

 fail_holder_put:
	gfs2_holder_put(tr->tr_t_gh);

 fail:
	kfree(tr);

	RETURN(G2FN_TRANS_BEGIN_I, error);
}

/**
 * gfs2_trans_end - End a transaction
 * @sdp: The GFS2 superblock
 *
 * If buffers were actually added to the transaction,
 * commit it.
 *
 */

void
gfs2_trans_end(struct gfs2_sbd *sdp)
{
	ENTER(G2FN_TRANS_END)
	struct gfs2_trans *tr;
	struct gfs2_holder *t_gh;

	tr = get_transaction;
	set_transaction(NULL);

	if (gfs2_assert_warn(sdp, tr))
		RET(G2FN_TRANS_END);

	t_gh = tr->tr_t_gh;
	tr->tr_t_gh = NULL;

	if (!tr->tr_touched) {
		gfs2_log_release(sdp, tr->tr_reserved);
		kfree(tr);

		gfs2_glock_dq(t_gh);
		gfs2_holder_put(t_gh);

		RET(G2FN_TRANS_END);
	}

	if (gfs2_assert_withdraw(sdp, tr->tr_num_buf <= tr->tr_blocks))
		printk("GFS2: fsid=%s: tr_num_buf = %u, tr_blocks = %u\n"
		       "GFS2: fsid=%s: tr_file = %s, tr_line = %u\n",
		       sdp->sd_fsname, tr->tr_num_buf, tr->tr_blocks,
		       sdp->sd_fsname, tr->tr_file, tr->tr_line);
	if (gfs2_assert_withdraw(sdp, tr->tr_num_revoke <= tr->tr_revokes))
		printk("GFS2: fsid=%s: tr_num_revoke = %u, tr_revokes = %u\n"
		       "GFS2: fsid=%s: tr_file = %s, tr_line = %u\n",
		       sdp->sd_fsname, tr->tr_num_revoke, tr->tr_revokes,
		       sdp->sd_fsname, tr->tr_file, tr->tr_line);

	gfs2_log_commit(sdp, tr);

	gfs2_glock_dq(t_gh);
	gfs2_holder_put(t_gh);

	if (sdp->sd_vfs->s_flags & MS_SYNCHRONOUS)
		gfs2_log_flush(sdp);

	RET(G2FN_TRANS_END);
}

void
gfs2_trans_add_gl(struct gfs2_glock *gl)
{
	ENTER(G2FN_TRANS_ADD_GL)
	LO_ADD(gl->gl_sbd, &gl->gl_le);
	RET(G2FN_TRANS_ADD_GL);
}

/**
 * gfs2_trans_add_bh - Add a to-be-modified buffer to the current transaction
 * @gl: the glock the buffer belongs to
 * @bh: The buffer to add
 *
 */

void
gfs2_trans_add_bh(struct gfs2_glock *gl, struct buffer_head *bh)
{
	ENTER(G2FN_TRANS_ADD_BH)
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct gfs2_bufdata *bd;

	/* Make sure GFS2 private info struct is attached to buffer head */
	bd = get_v2bd(bh);
	if (bd)
		gfs2_assert(sdp, bd->bd_gl == gl,);
	else {
		gfs2_attach_bufdata(gl, bh);
		bd = get_v2bd(bh);
	}

	LO_ADD(sdp, &bd->bd_le);

	RET(G2FN_TRANS_ADD_BH);
}

/**
 * gfs2_trans_add_revoke -
 * @sdp:
 * @blkno:
 *
 */

void
gfs2_trans_add_revoke(struct gfs2_sbd *sdp, uint64_t blkno)
{
	ENTER(G2FN_TRANS_ADD_REVOKE)
	struct gfs2_revoke *rv = kmalloc_nofail(sizeof(struct gfs2_revoke),
						GFP_KERNEL);
	INIT_LE(&rv->rv_le, &gfs2_revoke_lops);
	rv->rv_blkno = blkno;
	LO_ADD(sdp, &rv->rv_le);
	RET(G2FN_TRANS_ADD_REVOKE);
}

void
gfs2_trans_add_unrevoke(struct gfs2_sbd *sdp, uint64_t blkno)
{
	ENTER(G2FN_TRANS_ADD_UNREVOKE)
	struct list_head *head, *tmp;
	struct gfs2_revoke *rv = NULL;

	gfs2_log_lock(sdp);

	for (head = &sdp->sd_log_le_revoke, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		rv = list_entry(tmp, struct gfs2_revoke, rv_le.le_list);
		if (rv->rv_blkno == blkno) {
			list_del(tmp);
			gfs2_assert_withdraw(sdp, sdp->sd_log_num_revoke);
			sdp->sd_log_num_revoke--;
			break;
		}
	}

	gfs2_log_unlock(sdp);

	if (tmp != head) {
		kfree(rv);
		get_transaction->tr_num_revoke_rm++;
	}

	RET(G2FN_TRANS_ADD_UNREVOKE);
}

void
gfs2_trans_add_rg(struct gfs2_rgrpd *rgd)
{
	ENTER(G2FN_TRANS_ADD_RG)
	LO_ADD(rgd->rd_sbd, &rgd->rd_le);
	RET(G2FN_TRANS_ADD_RG);
}

void
gfs2_trans_add_databuf(struct gfs2_sbd *sdp, struct buffer_head *bh)
{
	ENTER(G2FN_TRANS_ADD_DATABUF)
	struct gfs2_databuf *db = kmalloc_nofail(sizeof(struct gfs2_databuf),
						 GFP_KERNEL);
	INIT_LE(&db->db_le, &gfs2_databuf_lops);
	get_bh(bh);
	db->db_bh = bh;
	LO_ADD(sdp, &db->db_le);
	RET(G2FN_TRANS_ADD_DATABUF);
}

