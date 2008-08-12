#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/tty.h>

#include "gfs.h"
#include "bmap.h"
#include "file.h"
#include "glock.h"
#include "glops.h"
#include "log.h"
#include "quota.h"
#include "rgrp.h"
#include "super.h"
#include "trans.h"

/**
 * gfs_quota_get - Get a structure to represent a quota change
 * @sdp: the filesystem
 * @user: TRUE if this is a user quota
 * @id: the uid or gid
 * @create: if TRUE, create the structure, otherwise return NULL
 * @qdp: the returned quota structure
 *
 * Returns: errno
 */

int
gfs_quota_get(struct gfs_sbd *sdp, int user, uint32_t id, int create,
	      struct gfs_quota_data **qdp)
{
	struct gfs_quota_data *qd = NULL, *new_qd = NULL;
	struct list_head *tmp, *head;
	int error;

	*qdp = NULL;

	for (;;) {
		spin_lock(&sdp->sd_quota_lock);

		for (head = &sdp->sd_quota_list, tmp = head->next;
		     tmp != head;
		     tmp = tmp->next) {
			qd = list_entry(tmp, struct gfs_quota_data, qd_list);
			if (qd->qd_id == id &&
			    !test_bit(QDF_USER, &qd->qd_flags) == !user) {
				qd->qd_count++;
				break;
			}
		}

		if (tmp == head)
			qd = NULL;

		if (!qd && new_qd) {
			qd = new_qd;
			list_add(&qd->qd_list, &sdp->sd_quota_list);
			new_qd = NULL;
		}

		spin_unlock(&sdp->sd_quota_lock);

		if (qd || !create) {
			if (new_qd) {
				gfs_lvb_unhold(new_qd->qd_gl);
				kfree(new_qd);
				atomic_dec(&sdp->sd_quota_count);
			}
			*qdp = qd;
			return 0;
		}

		new_qd = kmalloc(sizeof(struct gfs_quota_data), GFP_KERNEL);
		if (!new_qd)
			return -ENOMEM;
		memset(new_qd, 0, sizeof(struct gfs_quota_data));

		new_qd->qd_count = 1;

		new_qd->qd_id = id;
		if (user)
			set_bit(QDF_USER, &new_qd->qd_flags);

		INIT_LIST_HEAD(&new_qd->qd_le_list);

		error = gfs_glock_get(sdp, 2 * (uint64_t)id + ((user) ? 0 : 1),
				      &gfs_quota_glops, CREATE,
				      &new_qd->qd_gl);
		if (error) {
			kfree(new_qd);
			return error;
		}

		error = gfs_lvb_hold(new_qd->qd_gl);

		gfs_glock_put(new_qd->qd_gl);

		if (error) {
			kfree(new_qd);
			return error;
		}

		atomic_inc(&sdp->sd_quota_count);
	}
}

/**
 * gfs_quota_hold - increment the usage count on a struct gfs_quota_data
 * @sdp: the filesystem
 * @qd: the structure
 *
 */

void
gfs_quota_hold(struct gfs_sbd *sdp, struct gfs_quota_data *qd)
{
	spin_lock(&sdp->sd_quota_lock);
	gfs_assert(sdp, qd->qd_count,);
	qd->qd_count++;
	spin_unlock(&sdp->sd_quota_lock);
}

/**
 * gfs_quota_put - decrement the usage count on a struct gfs_quota_data
 * @sdp: the filesystem
 * @qd: the structure
 *
 * Free the structure if its reference count hits zero.
 *
 */

void
gfs_quota_put(struct gfs_sbd *sdp, struct gfs_quota_data *qd)
{
	spin_lock(&sdp->sd_quota_lock);
	gfs_assert(sdp, qd->qd_count,);
	qd->qd_count--;
	spin_unlock(&sdp->sd_quota_lock);
}

/**
 * quota_find - Find a quota change to sync to the quota file
 * @sdp: the filesystem
 *
 * The returned structure is locked and needs to be unlocked
 * with quota_unlock().
 *
 * Returns: A quota structure, or NULL
 */

static struct gfs_quota_data *
quota_find(struct gfs_sbd *sdp)
{
	struct list_head *tmp, *head;
	struct gfs_quota_data *qd = NULL;

	if (test_bit(SDF_ROFS, &sdp->sd_flags))
		return NULL;

	gfs_log_lock(sdp);
	spin_lock(&sdp->sd_quota_lock);

	if (!atomic_read(&sdp->sd_quota_od_count))
		goto out;

	for (head = &sdp->sd_quota_list, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		qd = list_entry(tmp, struct gfs_quota_data, qd_list);

		if (test_bit(QDF_LOCK, &qd->qd_flags))
			continue;
		if (!test_bit(QDF_OD_LIST, &qd->qd_flags))
			continue;
		if (qd->qd_sync_gen >= sdp->sd_quota_sync_gen)
			continue;

		list_move_tail(&qd->qd_list, &sdp->sd_quota_list);

		set_bit(QDF_LOCK, &qd->qd_flags);
		qd->qd_count++;
		qd->qd_change_sync = qd->qd_change_od;

		goto out;
	}

	qd = NULL;

 out:
	spin_unlock(&sdp->sd_quota_lock);
	gfs_log_unlock(sdp);

	return qd;
}

/**
 * quota_trylock - Try to lock a given quota entry
 * @sdp: the filesystem
 * @qd: the quota data structure
 *
 * Returns: TRUE if the lock was successful, FALSE, otherwise
 */

static int
quota_trylock(struct gfs_sbd *sdp, struct gfs_quota_data *qd)
{
	int ret = FALSE;

	if (test_bit(SDF_ROFS, &sdp->sd_flags))
		return FALSE;

	gfs_log_lock(sdp);
	spin_lock(&sdp->sd_quota_lock);

	if (test_bit(QDF_LOCK, &qd->qd_flags))
		goto out;
	if (!test_bit(QDF_OD_LIST, &qd->qd_flags))
		goto out;

	list_move_tail(&qd->qd_list, &sdp->sd_quota_list);

	set_bit(QDF_LOCK, &qd->qd_flags);
	qd->qd_count++;
	qd->qd_change_sync = qd->qd_change_od;

	ret = TRUE;

 out:
	spin_unlock(&sdp->sd_quota_lock);
	gfs_log_unlock(sdp);

	return ret;
}

/**
 * quota_unlock - drop and a reference on a quota structure
 * @sdp: the filesystem
 * @qd: the quota inode structure
 *
 */

static void
quota_unlock(struct gfs_sbd *sdp, struct gfs_quota_data *qd)
{
	spin_lock(&sdp->sd_quota_lock);

	gfs_assert_warn(sdp, test_bit(QDF_LOCK, &qd->qd_flags));
	clear_bit(QDF_LOCK, &qd->qd_flags);

	gfs_assert(sdp, qd->qd_count,);
	qd->qd_count--;

	spin_unlock(&sdp->sd_quota_lock);
}

/**
 * gfs_quota_merge - add/remove a quota change from the in-memory list
 * @sdp: the filesystem
 * @tag: the quota change tag
 *
 * Returns: errno
 */

int
gfs_quota_merge(struct gfs_sbd *sdp, struct gfs_quota_tag *tag)
{
	struct gfs_quota_data *qd;
	int error;

	error = gfs_quota_get(sdp,
			      tag->qt_flags & GFS_QTF_USER, tag->qt_id,
			      CREATE, &qd);
	if (error)
		return error;

	gfs_assert(sdp, qd->qd_change_ic == qd->qd_change_od,);

	gfs_log_lock(sdp);

	qd->qd_change_ic += tag->qt_change;
	qd->qd_change_od += tag->qt_change;

	if (qd->qd_change_od) {
		if (!test_bit(QDF_OD_LIST, &qd->qd_flags)) {
			gfs_quota_hold(sdp, qd);
			set_bit(QDF_OD_LIST, &qd->qd_flags);
			atomic_inc(&sdp->sd_quota_od_count);
		}
	} else {
		gfs_assert_warn(sdp, test_bit(QDF_OD_LIST, &qd->qd_flags));
		clear_bit(QDF_OD_LIST, &qd->qd_flags);
		gfs_quota_put(sdp, qd);
		gfs_assert(sdp, atomic_read(&sdp->sd_quota_od_count) > 0,);
		atomic_dec(&sdp->sd_quota_od_count);
	}

	gfs_log_unlock(sdp);

	gfs_quota_put(sdp, qd);

	return 0;
}

/**
 * gfs_quota_scan - Look for unused struct gfs_quota_data structures to throw away
 * @sdp: the filesystem
 *
 */

void
gfs_quota_scan(struct gfs_sbd *sdp)
{
	struct list_head *head, *tmp, *next;
	struct gfs_quota_data *qd;
	LIST_HEAD(dead);

	spin_lock(&sdp->sd_quota_lock);

	for (head = &sdp->sd_quota_list, tmp = head->next, next = tmp->next;
	     tmp != head;
	     tmp = next, next = next->next) {
		qd = list_entry(tmp, struct gfs_quota_data, qd_list);
		if (!qd->qd_count)
			list_move(&qd->qd_list, &dead);
	}

	spin_unlock(&sdp->sd_quota_lock);

	while (!list_empty(&dead)) {
		qd = list_entry(dead.next, struct gfs_quota_data, qd_list);

		gfs_assert_warn(sdp, !qd->qd_count);
		gfs_assert_warn(sdp, !test_bit(QDF_OD_LIST, &qd->qd_flags) &&
				!test_bit(QDF_LOCK, &qd->qd_flags));
		gfs_assert_warn(sdp, !qd->qd_change_new && !qd->qd_change_ic &&
				!qd->qd_change_od);

		list_del(&qd->qd_list);
		gfs_lvb_unhold(qd->qd_gl);
		kfree(qd);
		atomic_dec(&sdp->sd_quota_count);
	}
}

/**
 * gfs_quota_cleanup - get rid of any extra struct gfs_quota_data structures
 * @sdp: the filesystem
 *
 */

void
gfs_quota_cleanup(struct gfs_sbd *sdp)
{
	struct gfs_quota_data *qd;

 restart:
	gfs_log_lock(sdp);

	spin_lock(&sdp->sd_quota_lock);

	while (!list_empty(&sdp->sd_quota_list)) {
		qd = list_entry(sdp->sd_quota_list.next,
				struct gfs_quota_data,
				qd_list);

		if (qd->qd_count > 1) {
			spin_unlock(&sdp->sd_quota_lock);
			gfs_log_unlock(sdp);
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(HZ);
			goto restart;

		} else if (qd->qd_count) {
			gfs_assert_warn(sdp,
					test_bit(QDF_OD_LIST, &qd->qd_flags) &&
					!test_bit(QDF_LOCK, &qd->qd_flags));
			gfs_assert_warn(sdp, qd->qd_change_od &&
					qd->qd_change_od == qd->qd_change_ic);
			gfs_assert_warn(sdp, !qd->qd_change_new);

			list_del(&qd->qd_list);
			atomic_dec(&sdp->sd_quota_od_count);

			spin_unlock(&sdp->sd_quota_lock);
			gfs_lvb_unhold(qd->qd_gl);
			kfree(qd);
			atomic_dec(&sdp->sd_quota_count);
			spin_lock(&sdp->sd_quota_lock);

		} else {
			gfs_assert_warn(sdp,
					!test_bit(QDF_OD_LIST, &qd->qd_flags) &&
					!test_bit(QDF_LOCK, &qd->qd_flags));
			gfs_assert_warn(sdp, !qd->qd_change_new &&
					!qd->qd_change_ic &&
					!qd->qd_change_od);

			list_del(&qd->qd_list);

			spin_unlock(&sdp->sd_quota_lock);
			gfs_lvb_unhold(qd->qd_gl);
			kfree(qd);
			atomic_dec(&sdp->sd_quota_count);
			spin_lock(&sdp->sd_quota_lock);
		}
	}

	spin_unlock(&sdp->sd_quota_lock);

	gfs_assert(sdp, !atomic_read(&sdp->sd_quota_od_count),);

	gfs_log_unlock(sdp);
}

/**
 * sort_qd - figure out the order between two quota data structures
 * @a: first quota data structure
 * @b: second quota data structure
 *
 * Returns: -1 if @a comes before @b, 0 if @a equals @b, 1 if @b comes before @a
 */

static int
sort_qd(const void *a, const void *b)
{
	struct gfs_quota_data *qd_a = *(struct gfs_quota_data **)a;
	struct gfs_quota_data *qd_b = *(struct gfs_quota_data **)b;
	int ret = 0;

	if (!test_bit(QDF_USER, &qd_a->qd_flags) !=
	    !test_bit(QDF_USER, &qd_b->qd_flags)) {
		if (test_bit(QDF_USER, &qd_a->qd_flags))
			ret = -1;
		else
			ret = 1;
	} else {
		if (qd_a->qd_id < qd_b->qd_id)
			ret = -1;
		else if (qd_a->qd_id > qd_b->qd_id)
			ret = 1;
	}

	return ret;
}

/**
 * do_quota_sync - Sync a bunch quota changes to the quota file
 * @sdp: the filesystem
 * @qda: an array of struct gfs_quota_data structures to be synced
 * @num_qd: the number of elements in @qda
 *
 * Returns: errno
 */

static int
do_quota_sync(struct gfs_sbd *sdp, struct gfs_quota_data **qda,
	      unsigned int num_qd)
{
	struct gfs_inode *ip = sdp->sd_qinode;
	struct gfs_alloc *al = NULL;
	struct gfs_holder i_gh, *ghs;
	struct gfs_quota q;
	char buf[sizeof(struct gfs_quota)];
	uint64_t offset;
	unsigned int qx, x;
	int ar;
	unsigned int nalloc = 0;
	unsigned int data_blocks, ind_blocks;
	int error;

	gfs_write_calc_reserv(ip, sizeof(struct gfs_quota), &data_blocks,
			      &ind_blocks);

	ghs = kmalloc(num_qd * sizeof(struct gfs_holder), GFP_KERNEL);
	if (!ghs)
		return -ENOMEM;

	gfs_sort(qda, num_qd, sizeof (struct gfs_quota_data *), sort_qd);
	for (qx = 0; qx < num_qd; qx++) {
		error = gfs_glock_nq_init(qda[qx]->qd_gl,
					  LM_ST_EXCLUSIVE,
					  GL_NOCACHE, &ghs[qx]);
		if (error)
			goto fail;
	}

	error = gfs_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &i_gh);
	if (error)
		goto fail;

	for (x = 0; x < num_qd; x++) {
		offset = (2 * (uint64_t)qda[x]->qd_id +
			  ((test_bit(QDF_USER, &qda[x]->qd_flags)) ? 0 : 1)) *
			sizeof(struct gfs_quota);

		error = gfs_write_alloc_required(ip, offset,
						 sizeof(struct gfs_quota),
						 &ar);
		if (error)
			goto fail_gunlock;

		if (ar)
			nalloc++;
	}

	if (nalloc) {
		al = gfs_alloc_get(ip);

		error =
		    gfs_quota_hold_m(ip, NO_QUOTA_CHANGE,
					   NO_QUOTA_CHANGE);
		if (error)
			goto fail_alloc;

		al->al_requested_meta = nalloc * (data_blocks + ind_blocks);

		error = gfs_inplace_reserve(ip);
		if (error)
			goto fail_qs;

		/* Trans may require:
		   two (journaled) data blocks, a dinode block, RG bitmaps to allocate from,
		   indirect blocks, and a quota block */

		error = gfs_trans_begin(sdp,
					1 + al->al_rgd->rd_ri.ri_length +
					num_qd * data_blocks +
					nalloc * ind_blocks,
					gfs_struct2blk(sdp, num_qd + 2,
						       sizeof(struct gfs_quota_tag)));
		if (error)
			goto fail_ipres;
	} else {
		/* Trans may require:
		   Data blocks, a dinode block, and quota blocks */

		error = gfs_trans_begin(sdp,
					1 + data_blocks * num_qd,
					gfs_struct2blk(sdp, num_qd,
						       sizeof(struct gfs_quota_tag)));
		if (error)
			goto fail_gunlock;
	}

	for (x = 0; x < num_qd; x++) {
		offset = (2 * (uint64_t)qda[x]->qd_id +
			  ((test_bit(QDF_USER, &qda[x]->qd_flags)) ? 0 : 1)) *
			sizeof(struct gfs_quota);

		/*  The quota file may not be a multiple of sizeof(struct gfs_quota) bytes.  */
		memset(buf, 0, sizeof(struct gfs_quota));

		error = gfs_internal_read(ip, buf, offset,
					  sizeof(struct gfs_quota));
		if (error < 0)
			goto fail_end_trans;

		gfs_quota_in(&q, buf);
		q.qu_value += qda[x]->qd_change_sync;
		gfs_quota_out(&q, buf);

		error = gfs_internal_write(ip, buf, offset,
					   sizeof(struct gfs_quota));
		if (error < 0)
			goto fail_end_trans;
		else if (error != sizeof(struct gfs_quota)) {
			error = -EIO;
			goto fail_end_trans;
		}

		if (test_bit(QDF_USER, &qda[x]->qd_flags))
			gfs_trans_add_quota(sdp, -qda[x]->qd_change_sync,
					    qda[x]->qd_id, NO_QUOTA_CHANGE);
		else
			gfs_trans_add_quota(sdp, -qda[x]->qd_change_sync,
					    NO_QUOTA_CHANGE, qda[x]->qd_id);

		memset(&qda[x]->qd_qb, 0, sizeof(struct gfs_quota_lvb));
		qda[x]->qd_qb.qb_magic = GFS_MAGIC;
		qda[x]->qd_qb.qb_limit = q.qu_limit;
		qda[x]->qd_qb.qb_warn = q.qu_warn;
		qda[x]->qd_qb.qb_value = q.qu_value;

		gfs_quota_lvb_out(&qda[x]->qd_qb, qda[x]->qd_gl->gl_lvb);
	}

	gfs_trans_end(sdp);

	if (nalloc) {
		gfs_assert_warn(sdp, al->al_alloced_meta);
		gfs_inplace_release(ip);
		gfs_quota_unhold_m(ip);
		gfs_alloc_put(ip);
	}

	gfs_glock_dq_uninit(&i_gh);

	for (x = 0; x < num_qd; x++)
		gfs_glock_dq_uninit(&ghs[x]);

	kfree(ghs);

	gfs_log_flush_glock(ip->i_gl);

	return 0;

 fail_end_trans:
	gfs_trans_end(sdp);

 fail_ipres:
	if (nalloc)
		gfs_inplace_release(ip);

 fail_qs:
	if (nalloc)
		gfs_quota_unhold_m(ip);

 fail_alloc:
	if (nalloc)
		gfs_alloc_put(ip);

 fail_gunlock:
	gfs_glock_dq_uninit(&i_gh);

 fail:
	while (qx--)
		gfs_glock_dq_uninit(&ghs[qx]);

	kfree(ghs);

	return error;
}

/**
 * glock_q - Acquire a lock for a quota entry
 * @sdp: the filesystem
 * @qd: the quota data structure to glock
 * @force_refresh: If TRUE, always read from the quota file
 * @q_gh: the glock holder for the quota lock
 *
 * Returns: errno
 */

static int
glock_q(struct gfs_sbd *sdp, struct gfs_quota_data *qd, int force_refresh,
	struct gfs_holder *q_gh)
{
	struct gfs_holder i_gh;
	struct gfs_quota q;
	char buf[sizeof(struct gfs_quota)];
	int error;

 restart:
	error = gfs_glock_nq_init(qd->qd_gl, LM_ST_SHARED, 0, q_gh);
	if (error)
		return error;

	gfs_quota_lvb_in(&qd->qd_qb, qd->qd_gl->gl_lvb);

	if (force_refresh ||
	    qd->qd_qb.qb_magic != GFS_MAGIC) {
		gfs_glock_dq_uninit(q_gh);
		error = gfs_glock_nq_init(qd->qd_gl,
					  LM_ST_EXCLUSIVE, GL_NOCACHE,
					  q_gh);
		if (error)
			return error;

		error = gfs_glock_nq_init(sdp->sd_qinode->i_gl,
					  LM_ST_SHARED, 0,
					  &i_gh);
		if (error)
			goto fail;

		memset(buf, 0, sizeof(struct gfs_quota));

		error = gfs_internal_read(sdp->sd_qinode, buf,
					  (2 * (uint64_t)qd->qd_id +
					   ((test_bit(QDF_USER, &qd->qd_flags)) ? 0 : 1)) *
					  sizeof(struct gfs_quota),
					  sizeof(struct gfs_quota));
		if (error < 0)
			goto fail_gunlock;

		gfs_glock_dq_uninit(&i_gh);

		gfs_quota_in(&q, buf);

		memset(&qd->qd_qb, 0, sizeof(struct gfs_quota_lvb));
		qd->qd_qb.qb_magic = GFS_MAGIC;
		qd->qd_qb.qb_limit = q.qu_limit;
		qd->qd_qb.qb_warn = q.qu_warn;
		qd->qd_qb.qb_value = q.qu_value;

		gfs_quota_lvb_out(&qd->qd_qb, qd->qd_gl->gl_lvb);

		gfs_glock_dq_uninit(q_gh);
		force_refresh = FALSE;
		goto restart;
	}

	return 0;

 fail_gunlock:
	gfs_glock_dq_uninit(&i_gh);

 fail:
	gfs_glock_dq_uninit(q_gh);

	return error;
}

/**
 * gfs_quota_hold_m - Hold the quota structures for up to 4 IDs
 * @ip: Two of the IDs are the UID and GID from this file
 * @uid: a UID or the constant NO_QUOTA_CHANGE
 * @gid: a GID or the constant NO_QUOTA_CHANGE
 *
 * The struct gfs_quota_data structures representing the locks are
 * stored in the ip->i_alloc->al_qd array.
 * 
 * Returns:  errno
 */

int
gfs_quota_hold_m(struct gfs_inode *ip, uint32_t uid, uint32_t gid)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_alloc *al = ip->i_alloc;
	unsigned int x = 0;
	int error;

	if (gfs_assert_warn(sdp, !al->al_qd_num &&
			    !test_bit(GIF_QD_LOCKED, &ip->i_flags)))
		return -EIO;

	if (!gfs_tune_get(sdp, gt_quota_account))
		return 0;

	error = gfs_quota_get(sdp, TRUE, ip->i_di.di_uid,
			      CREATE, &al->al_qd[x]);
	if (error)
		goto fail;
	x++;

	error = gfs_quota_get(sdp, FALSE, ip->i_di.di_gid,
			      CREATE, &al->al_qd[x]);
	if (error)
		goto fail;
	x++;

	if (uid != NO_QUOTA_CHANGE) {
		error = gfs_quota_get(sdp, TRUE, uid,
				      CREATE, &al->al_qd[x]);
		if (error)
			goto fail;
		x++;
	}

	if (gid != NO_QUOTA_CHANGE) {
		error = gfs_quota_get(sdp, FALSE, gid,
				      CREATE, &al->al_qd[x]);
		if (error)
			goto fail;
		x++;
	}

	al->al_qd_num = x;

	return 0;

 fail:
	if (x) {
		al->al_qd_num = x;
		gfs_quota_unhold_m(ip);
	}

	return error;
}

/**
 * gfs_quota_unhold_m - throw away some quota locks
 * @ip: the inode who's ip->i_alloc->al_qd array holds the structures
 *
 */

void
gfs_quota_unhold_m(struct gfs_inode *ip)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_alloc *al = ip->i_alloc;
	unsigned int x;

	gfs_assert_warn(sdp, !test_bit(GIF_QD_LOCKED, &ip->i_flags));

	for (x = 0; x < al->al_qd_num; x++) {
		gfs_quota_put(sdp, al->al_qd[x]);
		al->al_qd[x] = NULL;
	}
	al->al_qd_num = 0;
}

/**
 * gfs_quota_lock_m - Acquire the quota locks for up to 4 IDs
 * @ip: Two of the IDs are the UID and GID from this file
 * @uid: a UID or the constant NO_QUOTA_CHANGE
 * @gid: a GID or the constant NO_QUOTA_CHANGE
 *
 * The struct gfs_quota_data structures representing the locks are
 * stored in the ip->i_alloc->al_qd array.
 * 
 * Returns:  errno
 */

int
gfs_quota_lock_m(struct gfs_inode *ip, uint32_t uid, uint32_t gid)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_alloc *al = ip->i_alloc;
	unsigned int x;
	int error;

	gfs_quota_hold_m(ip, uid, gid);

	if (!gfs_tune_get(sdp, gt_quota_enforce))
		return 0;
	if (capable(CAP_SYS_RESOURCE))
		return 0;

	gfs_sort(al->al_qd, al->al_qd_num,
		 sizeof(struct gfs_quota_data *), sort_qd);

	for (x = 0; x < al->al_qd_num; x++) {
		error = glock_q(sdp, al->al_qd[x], FALSE, &al->al_qd_ghs[x]);
		if (error)
			goto fail;
	}

	set_bit(GIF_QD_LOCKED, &ip->i_flags);

	return 0;

      fail:
	while (x--)
		gfs_glock_dq_uninit(&al->al_qd_ghs[x]);

	return error;
}

/**
 * gfs_quota_unlock_m - drop some quota locks
 * @ip: the inode who's ip->i_alloc->al_qd array holds the locks
 *
 */

void
gfs_quota_unlock_m(struct gfs_inode *ip)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_alloc *al = ip->i_alloc;
	struct gfs_quota_data *qd, *qda[4];
	int64_t value;
	unsigned int count = 0;
	unsigned int x;
	int do_sync;

	if (!test_and_clear_bit(GIF_QD_LOCKED, &ip->i_flags))
		goto out;

	for (x = 0; x < al->al_qd_num; x++) {
		qd = al->al_qd[x];

		spin_lock(&sdp->sd_quota_lock);
		value = qd->qd_change_new + qd->qd_change_ic;
		spin_unlock(&sdp->sd_quota_lock);

		do_sync = TRUE;
		if (!qd->qd_qb.qb_limit)
			do_sync = FALSE;
		else if (qd->qd_qb.qb_value >= (int64_t)qd->qd_qb.qb_limit)
			do_sync = FALSE;
		else {
			struct gfs_tune *gt = &sdp->sd_tune;
			unsigned int num, den;
			int64_t v;

			spin_lock(&gt->gt_spin);
			num = gt->gt_quota_scale_num;
			den = gt->gt_quota_scale_den;
			spin_unlock(&gt->gt_spin);

			v = value * gfs_num_journals(sdp) * num;
			do_div(v, den);
			v += qd->qd_qb.qb_value;
			if (v < (int64_t)qd->qd_qb.qb_limit)
				do_sync = FALSE;
		}

		gfs_glock_dq_uninit(&al->al_qd_ghs[x]);

		if (do_sync) {
			gfs_log_flush(sdp);
			if (quota_trylock(sdp, qd))
				qda[count++] = qd;
		}
	}

	if (count) {
		do_quota_sync(sdp, qda, count);

		for (x = 0; x < count; x++)
			quota_unlock(sdp, qda[x]);
	}

 out:
	gfs_quota_unhold_m(ip);
}

/**
 * print_quota_message - print a message to the user's tty about quotas
 * @sdp: the filesystem
 * @qd: the quota ID that the message is about
 * @type: the type of message ("exceeded" or "warning")
 *
 * Returns: errno
 */

static int
print_quota_message(struct gfs_sbd *sdp, struct gfs_quota_data *qd, char *type)
{
	struct tty_struct *tty;
	char *line;
	int len;

	line = kmalloc(256, GFP_KERNEL);
	if (!line)
		return -ENOMEM;

	len = snprintf(line, 256, "GFS: fsid=%s: quota %s for %s %u\r\n",
		       sdp->sd_fsname, type,
		       (test_bit(QDF_USER, &qd->qd_flags)) ? "user" : "group",
		       qd->qd_id);

	if (current->signal) {
		tty = current->signal->tty;
		if (tty && tty->ops->write)
			tty->ops->write(tty, line, len);
	}

	kfree(line);

	return 0;
}

/**
 * gfs_quota_check - Check to see if a block allocation is possible
 * @ip: the inode who's ip->i_res.ir_qd array holds the quota locks
 * @uid: the UID the block is allocated for
 * @gid: the GID the block is allocated for
 *
 */

int
gfs_quota_check(struct gfs_inode *ip, uint32_t uid, uint32_t gid)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_alloc *al = ip->i_alloc;
	struct gfs_quota_data *qd;
	int64_t value;
	unsigned int x;
	int error = 0;

	if (!al)
		return 0;

	if (!gfs_tune_get(sdp, gt_quota_enforce))
		return 0;

	for (x = 0; x < al->al_qd_num; x++) {
		qd = al->al_qd[x];

		if (!((qd->qd_id == uid && test_bit(QDF_USER, &qd->qd_flags)) ||
		      (qd->qd_id == gid && !test_bit(QDF_USER, &qd->qd_flags))))
			continue;

		spin_lock(&sdp->sd_quota_lock);
		value = qd->qd_change_new + qd->qd_change_ic;
		spin_unlock(&sdp->sd_quota_lock);
		value += qd->qd_qb.qb_value;

		if (qd->qd_qb.qb_limit && (int64_t)qd->qd_qb.qb_limit < value) {
			print_quota_message(sdp, qd, "exceeded");
			error = -EDQUOT;
			break;
		} else if (qd->qd_qb.qb_warn &&
			   (int64_t)qd->qd_qb.qb_warn < value &&
			   time_after_eq(jiffies,
					 qd->qd_last_warn +
					 gfs_tune_get(sdp, gt_quota_warn_period) * HZ)) {
			error = print_quota_message(sdp, qd, "warning");
			qd->qd_last_warn = jiffies;
		}
	}

	return error;
}

/**
 * gfs_quota_sync - Sync quota changes to the quota file
 * @sdp: the filesystem
 *
 * Returns: errno
 */

int
gfs_quota_sync(struct gfs_sbd *sdp)
{
	struct gfs_quota_data **qda;
	unsigned int max_qd = gfs_tune_get(sdp, gt_quota_simul_sync);
	unsigned int num_qd;
	unsigned int x;
	int error = 0;

	sdp->sd_quota_sync_gen++;

	qda = kmalloc(max_qd * sizeof(struct gfs_quota_data *), GFP_KERNEL);
	if (!qda)
		return -ENOMEM;
	memset(qda, 0, max_qd * sizeof(struct gfs_quota_data *));

	do {
		num_qd = 0;

		for (;;) {
			qda[num_qd] = quota_find(sdp);
			if (!qda[num_qd])
				break;

			if (++num_qd == max_qd)
				break;
		}

		if (num_qd) {
			error = do_quota_sync(sdp, qda, num_qd);
			if (!error)
				for (x = 0; x < num_qd; x++)
					qda[x]->qd_sync_gen =
						sdp->sd_quota_sync_gen;

			for (x = 0; x < num_qd; x++)
				quota_unlock(sdp, qda[x]);
		}
	}
	while (!error && num_qd == max_qd);

	kfree(qda);

	return error;
}

/**
 * gfs_quota_refresh - Refresh the LVB for a given quota ID
 * @sdp: the filesystem
 * @user:
 * @id:
 *
 * Returns: errno
 */

int
gfs_quota_refresh(struct gfs_sbd *sdp, int user, uint32_t id)
{
	struct gfs_quota_data *qd;
	struct gfs_holder q_gh;
	int error;

	error = gfs_quota_get(sdp, user, id, CREATE, &qd);
	if (error)
		return error;

	error = glock_q(sdp, qd, TRUE, &q_gh);
	if (!error)
		gfs_glock_dq_uninit(&q_gh);

	gfs_quota_put(sdp, qd);

	return error;
}

/**
 * gfs_quota_read - Read the info a given quota ID
 * @sdp: the filesystem
 * @user:
 * @id:
 * @q:
 *
 * Returns: errno
 */

int
gfs_quota_read(struct gfs_sbd *sdp, int user, uint32_t id,
	       struct gfs_quota *q)
{
	struct gfs_quota_data *qd;
	struct gfs_holder q_gh;
	int error;

	if (((user) ? (id != current->fsuid) : (!in_group_p(id))) &&
	    !capable(CAP_SYS_ADMIN))
		return -EACCES;

	error = gfs_quota_get(sdp, user, id, CREATE, &qd);
	if (error)
		return error;

	error = glock_q(sdp, qd, FALSE, &q_gh);
	if (error)
		goto out;

	memset(q, 0, sizeof(struct gfs_quota));
	q->qu_limit = qd->qd_qb.qb_limit;
	q->qu_warn = qd->qd_qb.qb_warn;
	q->qu_value = qd->qd_qb.qb_value;

	spin_lock(&sdp->sd_quota_lock);
	q->qu_value += qd->qd_change_new + qd->qd_change_ic;
	spin_unlock(&sdp->sd_quota_lock);

	gfs_glock_dq_uninit(&q_gh);

 out:
	gfs_quota_put(sdp, qd);

	return error;
}
