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
#include <asm/uaccess.h>

#include "gfs.h"
#include "dio.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "lops.h"
#include "quota.h"
#include "recovery.h"

/*  Must be kept in sync with the beginning of struct gfs_glock  */
struct glock_plug {
	struct list_head gl_list;
	unsigned long gl_flags;
};

typedef void (*glock_examiner) (struct gfs_glock * gl);

/**
 * relaxed_state_ok - is a requested lock compatible with the current lock mode?
 * @actual: the current state of the lock
 * @requested: the lock state that was requested by the caller
 * @flags: the modifier flags passed in by the caller
 *
 * Returns: TRUE if the locks are compatible, FALSE otherwise
 */

static __inline__ int
relaxed_state_ok(unsigned int actual, unsigned requested, int flags)
{
	if (actual == requested)
		return TRUE;

	if (flags & GL_EXACT)
		return FALSE;

	if (actual == LM_ST_EXCLUSIVE && requested == LM_ST_SHARED)
		return TRUE;

	if (actual != LM_ST_UNLOCKED && (flags & LM_FLAG_ANY))
		return TRUE;

	return FALSE;
}

/**
 * gl_hash() - Turn glock number into hash bucket number
 * @lock: The glock number
 *
 * Returns: The number of the corresponding hash bucket
 */

static unsigned int
gl_hash(struct lm_lockname *name)
{
	unsigned int h;

	h = gfs_hash(&name->ln_number, sizeof(uint64_t));
	h = gfs_hash_more(&name->ln_type, sizeof(unsigned int), h);
	h &= GFS_GL_HASH_MASK;

	return h;
}

/**
 * glock_hold() - increment reference count on glock
 * @gl: The glock to put
 *
 */

static __inline__ void
glock_hold(struct gfs_glock *gl)
{
	atomic_inc(&gl->gl_count);
}

/**
 * glock_put() - Decrement reference count on glock
 * @gl: The glock to put
 *
 */

static __inline__ void
glock_put(struct gfs_glock *gl)
{
	if (atomic_read(&gl->gl_count) == 1)
		gfs_glock_schedule_for_reclaim(gl);
	GFS_ASSERT_GLOCK(atomic_read(&gl->gl_count) > 0, gl,);
	atomic_dec(&gl->gl_count);
}

/**
 * queue_empty - check to see if a glock's queue is empty
 * @gl: the glock
 * @head: the head of the queue to check
 *
 * Returns: TRUE if the queue is empty
 */

static __inline__ int
queue_empty(struct gfs_glock *gl, struct list_head *head)
{
	int empty;
	spin_lock(&gl->gl_spin);
	empty = list_empty(head);
	spin_unlock(&gl->gl_spin);
	return empty;
}

/**
 * search_bucket() - Find struct gfs_glock by lock number
 * @bucket: the bucket to search
 * @name: The lock name
 *
 * Returns: NULL, or the struct gfs_glock with the requested number
 */

static struct gfs_glock *
search_bucket(struct gfs_gl_hash_bucket *bucket, struct lm_lockname *name)
{
	struct list_head *tmp, *head;
	struct gfs_glock *gl;

	for (head = &bucket->hb_list, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		gl = list_entry(tmp, struct gfs_glock, gl_list);

		if (test_bit(GLF_PLUG, &gl->gl_flags))
			continue;
		if (!lm_name_equal(&gl->gl_name, name))
			continue;

		glock_hold(gl);

		return gl;
	}

	return NULL;
}

/**
 * gfs_glock_find() - Find glock by lock number
 * @sdp: The GFS superblock
 * @name: The lock name
 *
 * Figure out what bucket the lock is in, acquire the read lock on
 * it and call search_bucket().
 *
 * Returns: NULL, or the struct gfs_glock with the requested number
 */

struct gfs_glock *
gfs_glock_find(struct gfs_sbd *sdp, struct lm_lockname *name)
{
	struct gfs_gl_hash_bucket *bucket = &sdp->sd_gl_hash[gl_hash(name)];
	struct gfs_glock *gl;

	read_lock(&bucket->hb_lock);
	gl = search_bucket(bucket, name);
	read_unlock(&bucket->hb_lock);

	return gl;
}

/**
 * glock_free() - Perform a few checks and then release struct gfs_glock
 * @gl: The glock to release
 *
 */

static void
glock_free(struct gfs_glock *gl)
{
	struct gfs_sbd *sdp = gl->gl_sbd;
	struct inode *aspace = gl->gl_aspace;

	GFS_ASSERT_GLOCK(list_empty(&gl->gl_list), gl,);
	GFS_ASSERT_GLOCK(atomic_read(&gl->gl_count) == 1, gl,);
	GFS_ASSERT_GLOCK(list_empty(&gl->gl_holders), gl,);
	GFS_ASSERT_GLOCK(list_empty(&gl->gl_waiters1), gl,);
	GFS_ASSERT_GLOCK(list_empty(&gl->gl_waiters2), gl,);
	GFS_ASSERT_GLOCK(gl->gl_state == LM_ST_UNLOCKED, gl,);
	GFS_ASSERT_GLOCK(!gl->gl_object, gl,);
	GFS_ASSERT_GLOCK(!gl->gl_lvb, gl,);
	GFS_ASSERT_GLOCK(list_empty(&gl->gl_reclaim), gl,);

	sdp->sd_lockstruct.ls_ops->lm_put_lock(gl->gl_lock);

	if (aspace)
		gfs_aspace_put(aspace);

	kmem_cache_free(gfs_glock_cachep, gl);

	atomic_dec(&sdp->sd_glock_count);
}

/**
 * gfs_glock_get() - Get a glock, or create one if one doesn't exist
 * @sdp: The GFS superblock
 * @number: the lock number
 * @glops: The glock_operations to use
 * @create: If FALSE, don't create the glock if it doesn't exist
 * @glp: the glock is returned here
 *
 * Returns: 0 on success, -EXXX on failure
 */

int
gfs_glock_get(struct gfs_sbd *sdp,
	      uint64_t number, struct gfs_glock_operations *glops,
	      int create, struct gfs_glock **glp)
{
	struct lm_lockname name;
	struct gfs_glock *gl, *tmp;
	struct gfs_gl_hash_bucket *bucket;
	int error;

	name.ln_number = number;
	name.ln_type = glops->go_type;
	bucket = &sdp->sd_gl_hash[gl_hash(&name)];

	read_lock(&bucket->hb_lock);
	gl = search_bucket(bucket, &name);
	read_unlock(&bucket->hb_lock);

	if (gl || !create) {
		*glp = gl;
		return 0;
	}

	gl = kmem_cache_alloc(gfs_glock_cachep, GFP_KERNEL);
	if (!gl)
		return -ENOMEM;

	memset(gl, 0, sizeof(struct gfs_glock));

	INIT_LIST_HEAD(&gl->gl_list);
	gl->gl_name = name;
	atomic_set(&gl->gl_count, 1);

	spin_lock_init(&gl->gl_spin);

	gl->gl_state = LM_ST_UNLOCKED;
	INIT_LIST_HEAD(&gl->gl_holders);
	INIT_LIST_HEAD(&gl->gl_waiters1);
	INIT_LIST_HEAD(&gl->gl_waiters2);

	gl->gl_ops = glops;

	INIT_LE(&gl->gl_new_le, &gfs_glock_lops);
	INIT_LE(&gl->gl_incore_le, &gfs_glock_lops);

	gl->gl_bucket = bucket;
	INIT_LIST_HEAD(&gl->gl_reclaim);

	gl->gl_sbd = sdp;

	INIT_LIST_HEAD(&gl->gl_dirty_buffers);
	INIT_LIST_HEAD(&gl->gl_ail_bufs);

	if (glops == &gfs_inode_glops ||
	    glops == &gfs_rgrp_glops ||
	    glops == &gfs_meta_glops) {
		gl->gl_aspace = gfs_aspace_get(sdp);
		if (!gl->gl_aspace) {
			error = -ENOMEM;
			goto fail;
		}
	}

	error = sdp->sd_lockstruct.ls_ops->lm_get_lock(sdp->sd_lockstruct.ls_lockspace,
						       &name,
						       &gl->gl_lock);
	if (error)
		goto fail_aspace;

	atomic_inc(&sdp->sd_glock_count);

	write_lock(&bucket->hb_lock);
	tmp = search_bucket(bucket, &name);
	if (tmp) {
		write_unlock(&bucket->hb_lock);
		glock_free(gl);
		gl = tmp;
	} else {
		list_add_tail(&gl->gl_list, &bucket->hb_list);
		write_unlock(&bucket->hb_lock);
	}

	*glp = gl;

	return 0;

 fail_aspace:
	if (gl->gl_aspace)
		gfs_aspace_put(gl->gl_aspace);

 fail:
	kmem_cache_free(gfs_glock_cachep, gl);	

	return error;
}

/**
 * gfs_glock_hold() - As glock_hold(), but suitable for exporting
 * @gl: The glock to hold
 *
 */

void
gfs_glock_hold(struct gfs_glock *gl)
{
	GFS_ASSERT_GLOCK(atomic_read(&gl->gl_count) > 0, gl,);
	glock_hold(gl);
}

/**
 * gfs_glock_put() - As glock_put(), but suitable for exporting
 * @gl: The glock to put
 *
 */

void
gfs_glock_put(struct gfs_glock *gl)
{
	glock_put(gl);
}

/**
 * gfs_holder_init - initialize a struct gfs_holder in the default way
 * @gl: the glock 
 * @state: the state we're requesting
 * @flags: the modifier flags
 * @gh: the holder structure
 *
 */

void
gfs_holder_init(struct gfs_glock *gl, unsigned int state, int flags,
		struct gfs_holder *gh)
{
	memset(gh, 0, sizeof(struct gfs_holder));

	INIT_LIST_HEAD(&gh->gh_list);
	gh->gh_gl = gl;
	gh->gh_owner = current;
	gh->gh_state = state;
	gh->gh_flags = flags;

	if (gh->gh_state == LM_ST_EXCLUSIVE)
		gh->gh_flags |= GL_LOCAL_EXCL;

	init_completion(&gh->gh_wait);

	glock_hold(gl);
}

/**
 * gfs_holder_reinit - reinitialize a struct gfs_holder so we can requeue it
 * @state: the state we're requesting
 * @flags: the modifier flags
 * @gh: the holder structure
 *
 * Don't mess with the glock.
 *
 */

void
gfs_holder_reinit(unsigned int state, int flags, struct gfs_holder *gh)
{
	int alloced;

	GFS_ASSERT_GLOCK(list_empty(&gh->gh_list), gh->gh_gl,);

	gh->gh_state = state;
	gh->gh_flags = flags;

	if (gh->gh_state == LM_ST_EXCLUSIVE)
		gh->gh_flags |= GL_LOCAL_EXCL;

	alloced = test_bit(HIF_ALLOCED, &gh->gh_iflags);
	memset(&gh->gh_iflags, 0, sizeof(unsigned long));
	if (alloced)
		set_bit(HIF_ALLOCED, &gh->gh_iflags);
}

/**
 * gfs_holder_uninit - uninitialize a holder structure (drop reference on glock)
 * @gh: the holder structure
 *
 */

void
gfs_holder_uninit(struct gfs_holder *gh)
{
	struct gfs_glock *gl = gh->gh_gl;

	GFS_ASSERT_GLOCK(list_empty(&gh->gh_list), gl,);
	gh->gh_gl = NULL;

	glock_put(gl);
}

/**
 * gfs_holder_get - get a struct gfs_holder structure
 * @gl: the glock 
 * @state: the state we're requesting
 * @flags: the modifier flags
 *
 * Figure out how big an impact this function has.  Either:
 * 1) Replace it with a cache of structures hanging off the struct gfs_sbd
 * 2) Get rid of it and call gmalloc() directly
 * 3) Leave it like it is
 *
 * Returns: the holder structure
 */

struct gfs_holder *
gfs_holder_get(struct gfs_glock *gl, unsigned int state, int flags)
{
	struct gfs_holder *gh;

	gh = gmalloc(sizeof(struct gfs_holder));
	gfs_holder_init(gl, state, flags, gh);
	set_bit(HIF_ALLOCED, &gh->gh_iflags);

	return gh;
}

/**
 * gfs_holder_put - get rid of a struct gfs_holder structure
 * @gh: the holder structure
 *
 */

void
gfs_holder_put(struct gfs_holder *gh)
{
	GFS_ASSERT_GLOCK(test_bit(HIF_ALLOCED, &gh->gh_iflags), gh->gh_gl,);
	gfs_holder_uninit(gh);
	kfree(gh);
}

/**
 * handle_recurse - put other holder structures (marked recursive) into the holders list
 * @gh: the holder structure
 *
 */

static void
handle_recurse(struct gfs_holder *gh)
{
	struct gfs_glock *gl = gh->gh_gl;
	struct list_head *tmp, *head, *next;
	struct gfs_holder *tmp_gh;
	int found = FALSE;

	GFS_ASSERT_GLOCK(gh->gh_owner, gl,);

	for (head = &gl->gl_waiters2, tmp = head->next, next = tmp->next;
	     tmp != head;
	     tmp = next, next = tmp->next) {
		tmp_gh = list_entry(tmp, struct gfs_holder, gh_list);
		if (tmp_gh->gh_owner != gh->gh_owner)
			continue;

		GFS_ASSERT_GLOCK(test_bit(HIF_RECURSE, &tmp_gh->gh_iflags),
				 gl,);

		list_move_tail(&tmp_gh->gh_list, &gl->gl_holders);
		tmp_gh->gh_error = 0;
		set_bit(HIF_HOLDER, &tmp_gh->gh_iflags);

		complete(&tmp_gh->gh_wait);

		found = TRUE;
	}

	GFS_ASSERT_GLOCK(found, gl,);
}

/**
 * do_unrecurse - a recursive holder was just dropped of the waiters2 list
 * @gh: the holder
 *
 * If there is only one other recursive holder, clear is HIF_RECURSE bit.
 * If there is more than one, leave them alone.
 *
 */

static void
do_unrecurse(struct gfs_holder *gh)
{
	struct gfs_glock *gl = gh->gh_gl;
	struct list_head *tmp, *head;
	struct gfs_holder *tmp_gh, *last_gh = NULL;
	int found = FALSE;

	GFS_ASSERT_GLOCK(gh->gh_owner, gl,);

	for (head = &gl->gl_waiters2, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		tmp_gh = list_entry(tmp, struct gfs_holder, gh_list);
		if (tmp_gh->gh_owner != gh->gh_owner)
			continue;

		GFS_ASSERT_GLOCK(test_bit(HIF_RECURSE, &tmp_gh->gh_iflags),
				 gl,);

		if (found)
			return;

		found = TRUE;
		last_gh = tmp_gh;
	}

	GFS_ASSERT_GLOCK(found, gl,);
	clear_bit(HIF_RECURSE, &last_gh->gh_iflags);
}

/**
 * rq_mutex - process a mutex request in the queue
 * @gh: the glock holder
 *
 * Returns: TRUE if the queue is blocked, 
 */

static int
rq_mutex(struct gfs_holder *gh)
{
	struct gfs_glock *gl = gh->gh_gl;

	list_del_init(&gh->gh_list);
	/*  gh->gh_error never examined.  */
	set_bit(GLF_LOCK, &gl->gl_flags);
	complete(&gh->gh_wait);

	return TRUE;
}

/**
 * rq_promote - process a promote request in the queue
 * @gh: the glock holder
 * @promote_ok: It's ok to ask the LM to do promotes on a sync lock module
 *
 * Returns: TRUE if the queue is blocked, 
 */

static int
rq_promote(struct gfs_holder *gh, int promote_ok)
{
	struct gfs_glock *gl = gh->gh_gl;
	struct gfs_sbd *sdp = gl->gl_sbd;
	struct gfs_glock_operations *glops = gl->gl_ops;
	int recurse;

	if (!relaxed_state_ok(gl->gl_state, gh->gh_state, gh->gh_flags)) {
		if (list_empty(&gl->gl_holders)) {
			if (promote_ok || GFS_ASYNC_LM(sdp)) {
				gl->gl_req_gh = gh;
				set_bit(GLF_LOCK, &gl->gl_flags);
				spin_unlock(&gl->gl_spin);

				if (atomic_read(&sdp->sd_reclaim_count) >
				    sdp->sd_tune.gt_reclaim_limit &&
				    !(gh->gh_flags & LM_FLAG_PRIORITY)) {
					gfs_reclaim_glock(sdp);
					gfs_reclaim_glock(sdp);
				}

				glops->go_xmote_th(gl, gh->gh_state,
						   gh->gh_flags);

				spin_lock(&gl->gl_spin);
			} else
			    if (!test_and_set_bit(HIF_WAKEUP, &gh->gh_iflags))
				complete(&gh->gh_wait);
		}
		return TRUE;
	}

	if (list_empty(&gl->gl_holders)) {
		set_bit(HIF_FIRST, &gh->gh_iflags);
		set_bit(GLF_LOCK, &gl->gl_flags);
		recurse = FALSE;
	} else {
		struct gfs_holder *next_gh;
		if (gh->gh_flags & GL_LOCAL_EXCL)
			return TRUE;
		next_gh = list_entry(gl->gl_holders.next, struct gfs_holder, gh_list);
		if (next_gh->gh_flags & GL_LOCAL_EXCL)
			 return TRUE;
		recurse = test_bit(HIF_RECURSE, &gh->gh_iflags);
	}

	list_move_tail(&gh->gh_list, &gl->gl_holders);
	gh->gh_error = 0;
	set_bit(HIF_HOLDER, &gh->gh_iflags);

	if (recurse)
		handle_recurse(gh);

	complete(&gh->gh_wait);

	return FALSE;
}

/**
 * rq_demote - process a demote request in the queue
 * @gh: the glock holder
 *
 * Returns: TRUE if the queue is blocked, 
 */

static int
rq_demote(struct gfs_holder *gh)
{
	struct gfs_glock *gl = gh->gh_gl;
	struct gfs_glock_operations *glops = gl->gl_ops;

	if (!list_empty(&gl->gl_holders))
		return TRUE;

	if (gl->gl_state == gh->gh_state || gl->gl_state == LM_ST_UNLOCKED) {
		list_del_init(&gh->gh_list);
		gh->gh_error = 0;
		spin_unlock(&gl->gl_spin);
		if (test_bit(HIF_DEALLOC, &gh->gh_iflags))
			gfs_holder_put(gh);
		else
			complete(&gh->gh_wait);
		spin_lock(&gl->gl_spin);
	} else {
		gl->gl_req_gh = gh;
		set_bit(GLF_LOCK, &gl->gl_flags);
		spin_unlock(&gl->gl_spin);

		if (gh->gh_state == LM_ST_UNLOCKED ||
		    gl->gl_state != LM_ST_EXCLUSIVE)
			glops->go_drop_th(gl);
		else
			glops->go_xmote_th(gl, gh->gh_state, gh->gh_flags);

		spin_lock(&gl->gl_spin);
	}

	return FALSE;
}

/**
 * run_queue - process holder structures on a glock
 * @gl: the glock
 * @promote_ok: It's ok to ask the LM to do promotes on a sync lock module
 *
 */

static void
run_queue(struct gfs_glock *gl, int promote_ok)
{
	struct gfs_holder *gh;
	int blocked;

	for (;;) {
		if (test_bit(GLF_LOCK, &gl->gl_flags))
			break;

		if (!list_empty(&gl->gl_waiters1)) {
			gh = list_entry(gl->gl_waiters1.next,
					struct gfs_holder, gh_list);

			if (test_bit(HIF_MUTEX, &gh->gh_iflags))
				blocked = rq_mutex(gh);
			else
				GFS_ASSERT_GLOCK(FALSE, gl,);

		} else if (!list_empty(&gl->gl_waiters2)) {
			gh = list_entry(gl->gl_waiters2.next,
					struct gfs_holder, gh_list);

			if (test_bit(HIF_PROMOTE, &gh->gh_iflags))
				blocked = rq_promote(gh, promote_ok);
			else if (test_bit(HIF_DEMOTE, &gh->gh_iflags))
				blocked = rq_demote(gh);
			else
				GFS_ASSERT_GLOCK(FALSE, gl,);

		} else
			break;

		if (blocked)
			break;
	}
}

/**
 * lock_on_glock - acquire a local lock on a glock
 * @gl: the glock
 *
 */

static void
lock_on_glock(struct gfs_glock *gl)
{
	struct gfs_holder gh;

	gfs_holder_init(gl, 0, 0, &gh);
	set_bit(HIF_MUTEX, &gh.gh_iflags);

	spin_lock(&gl->gl_spin);
	if (test_and_set_bit(GLF_LOCK, &gl->gl_flags))
		list_add_tail(&gh.gh_list, &gl->gl_waiters1);
	else
		complete(&gh.gh_wait);
	spin_unlock(&gl->gl_spin);

	wait_for_completion(&gh.gh_wait);
	gfs_holder_uninit(&gh);
}

/**
 * trylock_on_glock - try to acquire a local lock on a glock
 * @gl: the glock
 *
 * Returns: TRUE if the glock is acquired
 */

static int
trylock_on_glock(struct gfs_glock *gl)
{
	int acquired = TRUE;

	spin_lock(&gl->gl_spin);
	if (test_and_set_bit(GLF_LOCK, &gl->gl_flags))
		acquired = FALSE;
	spin_unlock(&gl->gl_spin);

	return acquired;
}

/**
 * unlock_on_glock - release a local lock on a glock
 * @gl: the glock
 *
 */

static void
unlock_on_glock(struct gfs_glock *gl)
{
	spin_lock(&gl->gl_spin);
	clear_bit(GLF_LOCK, &gl->gl_flags);
	run_queue(gl, FALSE);
	spin_unlock(&gl->gl_spin);
}

/**
 * handle_callback - add a demote request to a lock's queue
 * @gl: the glock
 * @state: the state the callback is us to change to
 *
 */

static void
handle_callback(struct gfs_glock *gl, unsigned int state)
{
	struct list_head *tmp, *head;
	struct gfs_holder *gh, *new_gh = NULL;

	GFS_ASSERT_GLOCK(state != LM_ST_EXCLUSIVE, gl,);

 restart:
	spin_lock(&gl->gl_spin);

	for (head = &gl->gl_waiters2, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		gh = list_entry(tmp, struct gfs_holder, gh_list);
		if (test_bit(HIF_DEMOTE, &gh->gh_iflags) &&
		    gl->gl_req_gh != gh) {
			if (gh->gh_state != state)
				gh->gh_state = LM_ST_UNLOCKED;
			goto out;
		}
	}

	if (new_gh) {
		list_add(&new_gh->gh_list, &gl->gl_waiters2);
		new_gh = NULL;
	} else {
		spin_unlock(&gl->gl_spin);

		new_gh = gfs_holder_get(gl, state, LM_FLAG_TRY);
		set_bit(HIF_DEMOTE, &new_gh->gh_iflags);
		set_bit(HIF_DEALLOC, &new_gh->gh_iflags);
		new_gh->gh_owner = NULL;

		goto restart;
	}

 out:
	spin_unlock(&gl->gl_spin);

	if (new_gh)
		gfs_holder_put(new_gh);
}

/**
 * state_change - record that the glock is now in a different state
 * @gl: the glock
 * @new_state the new state
 *
 */

static void
state_change(struct gfs_glock *gl, unsigned int new_state)
{
	struct gfs_sbd *sdp = gl->gl_sbd;
	int held1, held2;

	held1 = (gl->gl_state != LM_ST_UNLOCKED);
	held2 = (new_state != LM_ST_UNLOCKED);

	if (held1 != held2) {
		if (held2) {
			atomic_inc(&sdp->sd_glock_held_count);
			glock_hold(gl);
		} else {
			atomic_dec(&sdp->sd_glock_held_count);
			glock_put(gl);
		}
	}

	gl->gl_state = new_state;
}

/**
 * xmote_bh - Called after the lock module is done acquiring a lock
 * @gl: The glock in question
 * @ret: the int returned from the lock module
 *
 */

static void
xmote_bh(struct gfs_glock *gl, unsigned int ret)
{
	struct gfs_glock_operations *glops = gl->gl_ops;
	struct gfs_holder *gh = gl->gl_req_gh;
	int prev_state = gl->gl_state;
	int op_done = TRUE;

	GFS_ASSERT_GLOCK(test_bit(GLF_LOCK, &gl->gl_flags), gl,);
	GFS_ASSERT_GLOCK(queue_empty(gl, &gl->gl_holders), gl,);
	GFS_ASSERT_GLOCK(!(ret & LM_OUT_ASYNC), gl,);

	state_change(gl, ret & LM_OUT_ST_MASK);

	if (ret & LM_OUT_NEED_E)
		handle_callback(gl, LM_ST_UNLOCKED);
	else if (ret & LM_OUT_NEED_D)
		handle_callback(gl, LM_ST_DEFERRED);
	else if (ret & LM_OUT_NEED_S)
		handle_callback(gl, LM_ST_SHARED);

	if (ret & LM_OUT_LVB_INVALID)
		set_bit(GLF_LVB_INVALID, &gl->gl_flags);

	if (prev_state != LM_ST_UNLOCKED && !(ret & LM_OUT_CACHEABLE)) {
		if (glops->go_inval)
			glops->go_inval(gl, DIO_METADATA | DIO_DATA);
	} else if (gl->gl_state == LM_ST_DEFERRED) {
		/* We might not want to do this here.
		   Look at moving to the inode glops. */
		if (glops->go_inval)
			glops->go_inval(gl, DIO_DATA);
	}

	/*  Deal with each possible exit condition  */

	if (!gh)
		gl->gl_stamp = jiffies;

	else if (test_bit(HIF_DEMOTE, &gh->gh_iflags)) {
		spin_lock(&gl->gl_spin);
		list_del_init(&gh->gh_list);
		if (gl->gl_state == gh->gh_state ||
		    gl->gl_state == LM_ST_UNLOCKED)
			gh->gh_error = 0;
		else
			gh->gh_error = GLR_TRYFAILED;
		spin_unlock(&gl->gl_spin);

		if (ret & LM_OUT_CANCELED)
			handle_callback(gl, LM_ST_UNLOCKED); /* Lame */

	} else if (ret & LM_OUT_CANCELED) {
		spin_lock(&gl->gl_spin);
		list_del_init(&gh->gh_list);
		gh->gh_error = GLR_CANCELED;
		if (test_bit(HIF_RECURSE, &gh->gh_iflags))
			do_unrecurse(gh);
		spin_unlock(&gl->gl_spin);

	} else if (relaxed_state_ok(gl->gl_state, gh->gh_state, gh->gh_flags)) {
		spin_lock(&gl->gl_spin);
		list_move_tail(&gh->gh_list, &gl->gl_holders);
		gh->gh_error = 0;
		set_bit(HIF_HOLDER, &gh->gh_iflags);
		spin_unlock(&gl->gl_spin);

		set_bit(HIF_FIRST, &gh->gh_iflags);

		op_done = FALSE;

	} else if (gh->gh_flags & (LM_FLAG_TRY | LM_FLAG_TRY_1CB)) {
		spin_lock(&gl->gl_spin);
		list_del_init(&gh->gh_list);
		gh->gh_error = GLR_TRYFAILED;
		if (test_bit(HIF_RECURSE, &gh->gh_iflags))
			do_unrecurse(gh);
		spin_unlock(&gl->gl_spin);

	} else
		GFS_ASSERT_GLOCK(FALSE, gl,);

	if (glops->go_xmote_bh)
		glops->go_xmote_bh(gl);

	if (op_done) {
		spin_lock(&gl->gl_spin);
		gl->gl_req_gh = NULL;
		gl->gl_req_bh = NULL;
		clear_bit(GLF_LOCK, &gl->gl_flags);
		run_queue(gl, FALSE);
		spin_unlock(&gl->gl_spin);
	}

	glock_put(gl);

	if (gh) {
		if (test_bit(HIF_DEALLOC, &gh->gh_iflags))
			gfs_holder_put(gh);
		else
			complete(&gh->gh_wait);
	}
}

/**
 * gfs_glock_xmote_th - Call into the lock module to acquire a glock
 * @gl: The glock in question
 * @state: the requested state
 * @flags: modifier flags to the lock call
 *
 */

void
gfs_glock_xmote_th(struct gfs_glock *gl, unsigned int state, int flags)
{
	struct gfs_sbd *sdp = gl->gl_sbd;
	struct gfs_glock_operations *glops = gl->gl_ops;
	int lck_flags = flags & (LM_FLAG_TRY | LM_FLAG_TRY_1CB |
				 LM_FLAG_NOEXP | LM_FLAG_ANY |
				 LM_FLAG_PRIORITY);
	unsigned int lck_ret;

	GFS_ASSERT_GLOCK(test_bit(GLF_LOCK, &gl->gl_flags), gl,);
	GFS_ASSERT_GLOCK(queue_empty(gl, &gl->gl_holders), gl,);
	GFS_ASSERT_GLOCK(state != LM_ST_UNLOCKED, gl,);
	GFS_ASSERT_GLOCK(state != gl->gl_state, gl,);

	if (gl->gl_state == LM_ST_EXCLUSIVE) {
		if (glops->go_sync)
			glops->go_sync(gl, DIO_METADATA | DIO_DATA);
	}

	glock_hold(gl);
	gl->gl_req_bh = xmote_bh;

	atomic_inc(&sdp->sd_lm_lock_calls);

	lck_ret = sdp->sd_lockstruct.ls_ops->lm_lock(gl->gl_lock,
						     gl->gl_state,
						     state, lck_flags);

	if (lck_ret & LM_OUT_ASYNC)
		GFS_ASSERT_GLOCK(lck_ret == LM_OUT_ASYNC, gl,);
	else
		xmote_bh(gl, lck_ret);
}

/**
 * drop_bh - Called after a lock module unlock completes
 * @gl: the glock
 * @ret: the return status
 *
 * Doesn't wake up the process waiting on the struct gfs_holder (if any)
 * Doesn't drop the reference on the glock the top half took out
 *
 */

static void
drop_bh(struct gfs_glock *gl, unsigned int ret)
{
	struct gfs_glock_operations *glops = gl->gl_ops;
	struct gfs_holder *gh = gl->gl_req_gh;

	clear_bit(GLF_PREFETCH, &gl->gl_flags);

	GFS_ASSERT_GLOCK(test_bit(GLF_LOCK, &gl->gl_flags), gl,);
	GFS_ASSERT_GLOCK(queue_empty(gl, &gl->gl_holders), gl,);
	GFS_ASSERT_GLOCK(!ret, gl,);

	state_change(gl, LM_ST_UNLOCKED);

	if (glops->go_inval)
		glops->go_inval(gl, DIO_METADATA | DIO_DATA);

	if (gh) {
		spin_lock(&gl->gl_spin);
		list_del_init(&gh->gh_list);
		gh->gh_error = 0;
		spin_unlock(&gl->gl_spin);
	}

	if (glops->go_drop_bh)
		glops->go_drop_bh(gl);

	spin_lock(&gl->gl_spin);
	gl->gl_req_gh = NULL;
	gl->gl_req_bh = NULL;
	clear_bit(GLF_LOCK, &gl->gl_flags);
	run_queue(gl, FALSE);
	spin_unlock(&gl->gl_spin);

	glock_put(gl);

	if (gh) {
		if (test_bit(HIF_DEALLOC, &gh->gh_iflags))
			gfs_holder_put(gh);
		else
			complete(&gh->gh_wait);
	}
}

/**
 * gfs_glock_drop_th - call into the lock module to unlock a lock 
 * @gl: the glock
 *
 */

void
gfs_glock_drop_th(struct gfs_glock *gl)
{
	struct gfs_sbd *sdp = gl->gl_sbd;
	struct gfs_glock_operations *glops = gl->gl_ops;
	unsigned int ret;

	GFS_ASSERT_GLOCK(test_bit(GLF_LOCK, &gl->gl_flags), gl,);
	GFS_ASSERT_GLOCK(queue_empty(gl, &gl->gl_holders), gl,);
	GFS_ASSERT_GLOCK(gl->gl_state != LM_ST_UNLOCKED, gl,);

	if (gl->gl_state == LM_ST_EXCLUSIVE) {
		if (glops->go_sync)
			glops->go_sync(gl, DIO_METADATA | DIO_DATA);
	}

	glock_hold(gl);
	gl->gl_req_bh = drop_bh;

	atomic_inc(&sdp->sd_lm_unlock_calls);

	ret = sdp->sd_lockstruct.ls_ops->lm_unlock(gl->gl_lock, gl->gl_state);

	if (!ret)
		drop_bh(gl, ret);
	else
		GFS_ASSERT_GLOCK(ret == LM_OUT_ASYNC, gl,);
}

/**
 * handle_cancels - cancel requests for locks stuck waiting on an expire flag
 * @gh: the LM_FLAG_NOEXP holder waiting to acquire the lock
 *
 */

static void
handle_cancels(struct gfs_holder *gh)
{
	struct gfs_glock *gl = gh->gh_gl;

	spin_lock(&gl->gl_spin);

	while (gl->gl_req_gh != gh &&
	       !test_bit(HIF_HOLDER, &gh->gh_iflags) &&
	       !test_bit(HIF_WAKEUP, &gh->gh_iflags) &&
	       !list_empty(&gh->gh_list)) {
		if (gl->gl_req_bh) {
			spin_unlock(&gl->gl_spin);
			gl->gl_sbd->sd_lockstruct.ls_ops->lm_cancel(gl->gl_lock);
			yield();
			spin_lock(&gl->gl_spin);
		} else {
			spin_unlock(&gl->gl_spin);
			yield();
			spin_lock(&gl->gl_spin);
		}
	}

	spin_unlock(&gl->gl_spin);
}

/**
 * glock_wait_internal - wait on a glock acquisition
 * @gh: the glock holder
 *
 * Returns: 0 on success
 */

static int
glock_wait_internal(struct gfs_holder *gh)
{
	struct gfs_glock *gl = gh->gh_gl;
	struct gfs_glock_operations *glops = gl->gl_ops;
	int error = 0;

	if (gh->gh_flags & (LM_FLAG_TRY | LM_FLAG_TRY_1CB)) {
		spin_lock(&gl->gl_spin);
		if (gl->gl_req_gh != gh &&
		    !test_bit(HIF_HOLDER, &gh->gh_iflags) &&
		    !test_bit(HIF_WAKEUP, &gh->gh_iflags) &&
		    !list_empty(&gh->gh_list)) {
			list_del_init(&gh->gh_list);
			gh->gh_error = GLR_TRYFAILED;
			if (test_bit(HIF_RECURSE, &gh->gh_iflags))
				do_unrecurse(gh);
			run_queue(gl, FALSE);
			spin_unlock(&gl->gl_spin);
			return GLR_TRYFAILED;
		}
		spin_unlock(&gl->gl_spin);
	}

	if (gh->gh_flags & LM_FLAG_NOEXP)
		handle_cancels(gh);

	for (;;) {
		wait_for_completion(&gh->gh_wait);

		spin_lock(&gl->gl_spin);
		if (test_and_clear_bit(HIF_WAKEUP, &gh->gh_iflags)) {
			run_queue(gl, TRUE);
			spin_unlock(&gl->gl_spin);
		} else {
			spin_unlock(&gl->gl_spin);
			break;
		}
	}

	if (gh->gh_error)
		return gh->gh_error;

	GFS_ASSERT_GLOCK(test_bit(HIF_HOLDER, &gh->gh_iflags), gl,);
	GFS_ASSERT_GLOCK(relaxed_state_ok(gl->gl_state, gh->gh_state,
					  gh->gh_flags), gl,);

	if (test_bit(HIF_FIRST, &gh->gh_iflags)) {
		GFS_ASSERT_GLOCK(test_bit(GLF_LOCK, &gl->gl_flags), gl,);

		if (glops->go_lock) {
			error = glops->go_lock(gl, gh->gh_flags);
			if (error) {
				spin_lock(&gl->gl_spin);
				list_del_init(&gh->gh_list);
				gh->gh_error = error;
				if (test_and_clear_bit(HIF_RECURSE, &gh->gh_iflags))
					do_unrecurse(gh);
				spin_unlock(&gl->gl_spin);
			}
		}

		spin_lock(&gl->gl_spin);
		gl->gl_req_gh = NULL;
		gl->gl_req_bh = NULL;
		clear_bit(GLF_LOCK, &gl->gl_flags);
		if (test_bit(HIF_RECURSE, &gh->gh_iflags))
			handle_recurse(gh);
		run_queue(gl, FALSE);
		spin_unlock(&gl->gl_spin);
	}

	return error;
}

/**
 * add_to_queue - Add a holder to the wait queue (but look for recursion)
 * @gh: the holder structure
 *
 */

static void
add_to_queue(struct gfs_holder *gh)
{
	struct gfs_glock *gl = gh->gh_gl;
	struct list_head *tmp, *head;
	struct gfs_holder *tmp_gh;

	if (gh->gh_owner) {
		for (head = &gl->gl_holders, tmp = head->next;
		     tmp != head;
		     tmp = tmp->next) {
			tmp_gh = list_entry(tmp, struct gfs_holder, gh_list);
			if (tmp_gh->gh_owner == gh->gh_owner) {
				GFS_ASSERT_GLOCK((gh->gh_flags & LM_FLAG_ANY) ||
						 !(tmp_gh->gh_flags & LM_FLAG_ANY),
						 gl,);
				GFS_ASSERT_GLOCK((tmp_gh->gh_flags & GL_LOCAL_EXCL) ||
						 !(gh->gh_flags & GL_LOCAL_EXCL),
						 gl,);
				GFS_ASSERT_GLOCK(relaxed_state_ok(gl->gl_state,
								  gh->gh_state,
								  gh->gh_flags),
						 gl,);

				list_add_tail(&gh->gh_list, &gl->gl_holders);
				set_bit(HIF_HOLDER, &gh->gh_iflags);

				gh->gh_error = 0;
				complete(&gh->gh_wait);

				return;
			}
		}

		for (head = &gl->gl_waiters2, tmp = head->next;
		     tmp != head;
		     tmp = tmp->next) {
			tmp_gh = list_entry(tmp, struct gfs_holder, gh_list);
			if (tmp_gh->gh_owner == gh->gh_owner) {
				GFS_ASSERT_GLOCK(test_bit(HIF_PROMOTE,
							  &tmp_gh->gh_iflags),
						 gl,);
				GFS_ASSERT_GLOCK((gh->gh_flags & LM_FLAG_ANY) ||
						 !(tmp_gh->gh_flags & LM_FLAG_ANY),
						 gl,);
				GFS_ASSERT_GLOCK((tmp_gh->gh_flags & GL_LOCAL_EXCL) ||
						 !(gh->gh_flags & GL_LOCAL_EXCL),
						 gl,);
				GFS_ASSERT_GLOCK(relaxed_state_ok(tmp_gh->gh_state,
								  gh->gh_state,
								  gh->gh_flags),
						 gl,);

				set_bit(HIF_RECURSE, &gh->gh_iflags);
				set_bit(HIF_RECURSE, &tmp_gh->gh_iflags);

				list_add_tail(&gh->gh_list, &gl->gl_waiters2);

				return;
			}
		}
	}

	if (gh->gh_flags & LM_FLAG_PRIORITY)
		list_add(&gh->gh_list, &gl->gl_waiters2);
	else
		list_add_tail(&gh->gh_list, &gl->gl_waiters2);
}

/**
 * gfs_glock_nq - enqueue a struct gfs_holder onto a glock (acquire a glock)
 * @gh: the holder structure
 *
 * if (gh->gh_flags & GL_ASYNC), this never returns an error
 *
 * Returns: 0, GLR_TRYFAILED, or -EXXX on failure
 */

int
gfs_glock_nq(struct gfs_holder *gh)
{
	struct gfs_glock *gl = gh->gh_gl;
	struct gfs_sbd *sdp = gl->gl_sbd;
	int error = 0;

	GFS_ASSERT_GLOCK(list_empty(&gh->gh_list), gl,);
	GFS_ASSERT_GLOCK(gh->gh_state != LM_ST_UNLOCKED, gl,);
	GFS_ASSERT_GLOCK((gh->gh_flags & (LM_FLAG_ANY | GL_EXACT)) !=
			 (LM_FLAG_ANY | GL_EXACT), gl,);
	GFS_ASSERT_GLOCK(GFS_ASYNC_LM(sdp) ||
			 !(gh->gh_flags & GL_ASYNC), gl,);

	atomic_inc(&sdp->sd_glock_nq_calls);

 restart:
	set_bit(HIF_PROMOTE, &gh->gh_iflags);

	spin_lock(&gl->gl_spin);
	add_to_queue(gh);
	run_queue(gl, TRUE);
	spin_unlock(&gl->gl_spin);

	if (!(gh->gh_flags & GL_ASYNC)) {
		error = glock_wait_internal(gh);
		if (error == GLR_CANCELED) {
			current->state = TASK_UNINTERRUPTIBLE;
			schedule_timeout(HZ);
			goto restart;
		}
	}

	clear_bit(GLF_PREFETCH, &gl->gl_flags);

	return error;
}

/**
 * gfs_glock_poll - poll to see if an async request has been completed
 * @gh: the holder
 *
 * Returns: TRUE if the request is ready to be gfs_glock_wait()ed on
 */

int
gfs_glock_poll(struct gfs_holder *gh)
{
	struct gfs_glock *gl = gh->gh_gl;
	int ready = FALSE;

	GFS_ASSERT_GLOCK(gh->gh_flags & GL_ASYNC, gl,);
	GFS_ASSERT_GLOCK(!test_bit(HIF_WAKEUP, &gh->gh_iflags), gl,);

	spin_lock(&gl->gl_spin);

	if (test_bit(HIF_HOLDER, &gh->gh_iflags))
		ready = TRUE;
	else if (list_empty(&gh->gh_list)) {
		if (gh->gh_error == GLR_CANCELED) {
			spin_unlock(&gl->gl_spin);
			current->state = TASK_UNINTERRUPTIBLE;
			schedule_timeout(HZ);
			gfs_glock_nq(gh);
			return FALSE;
		} else
			ready = TRUE;
	}

	spin_unlock(&gl->gl_spin);

	return ready;
}

/**
 * gfs_glock_wait - wait for a lock acquisition that ended in a GLR_ASYNC
 * @gh: the holder structure
 *
 * Returns: 0, GLR_TRYFAILED, or -EXXX on failure
 */

int
gfs_glock_wait(struct gfs_holder *gh)
{
	struct gfs_glock *gl = gh->gh_gl;
	int error;

	GFS_ASSERT_GLOCK(gh->gh_flags & GL_ASYNC, gl,);
	GFS_ASSERT_GLOCK(!test_bit(HIF_WAKEUP, &gh->gh_iflags), gl,);

	error = glock_wait_internal(gh);
	if (error == GLR_CANCELED) {
		current->state = TASK_UNINTERRUPTIBLE;
		schedule_timeout(HZ);
		gh->gh_flags &= ~GL_ASYNC;
		error = gfs_glock_nq(gh);
	}

	return error;
}

/**
 * gfs_glock_dq - dequeue a struct gfs_holder from a glock (release a glock)
 * @gh: the glock holder
 *
 */

void
gfs_glock_dq(struct gfs_holder *gh)
{
	struct gfs_glock *gl = gh->gh_gl;
	struct gfs_glock_operations *glops = gl->gl_ops;

	GFS_ASSERT_GLOCK(!queue_empty(gl, &gh->gh_list), gl,);
	GFS_ASSERT_GLOCK(test_bit(HIF_HOLDER, &gh->gh_iflags), gl,);

	atomic_inc(&gl->gl_sbd->sd_glock_dq_calls);

	if (gh->gh_flags & GL_SYNC)
		set_bit(GLF_SYNC, &gl->gl_flags);
	if (gh->gh_flags & GL_NOCACHE)
		handle_callback(gl, LM_ST_UNLOCKED);

	lock_on_glock(gl);

	spin_lock(&gl->gl_spin);
	list_del_init(&gh->gh_list);
	if (list_empty(&gl->gl_holders)) {
		spin_unlock(&gl->gl_spin);

		if (glops->go_unlock)
			glops->go_unlock(gl, gh->gh_flags);

		if (test_bit(GLF_SYNC, &gl->gl_flags)) {
			if (glops->go_sync)
				glops->go_sync(gl,
					       DIO_METADATA |
					       DIO_DATA |
					       DIO_INVISIBLE);
		}

		gl->gl_stamp = jiffies;

		spin_lock(&gl->gl_spin);
	}

	clear_bit(GLF_LOCK, &gl->gl_flags);
	run_queue(gl, FALSE);
	spin_unlock(&gl->gl_spin);
}

/**
 * gfs_glock_prefetch - Try to prefetch a glock
 * @gl: the glock
 * @state: the state to prefetch in 
 * @flags: flags passed to go_xmote_th()
 *
 */

void
gfs_glock_prefetch(struct gfs_glock *gl, unsigned int state, int flags)
{
	struct gfs_glock_operations *glops = gl->gl_ops;

	GFS_ASSERT_GLOCK(atomic_read(&gl->gl_count) > 0, gl,);
	GFS_ASSERT_GLOCK(state != LM_ST_UNLOCKED, gl,);
	GFS_ASSERT_GLOCK((flags & (LM_FLAG_ANY | GL_EXACT)) !=
			 (LM_FLAG_ANY | GL_EXACT), gl,);

	spin_lock(&gl->gl_spin);

	if (test_bit(GLF_LOCK, &gl->gl_flags) ||
	    !list_empty(&gl->gl_holders) ||
	    !list_empty(&gl->gl_waiters1) ||
	    !list_empty(&gl->gl_waiters2) ||
	    relaxed_state_ok(gl->gl_state, state, flags)) {
		spin_unlock(&gl->gl_spin);
		return;
	}

	set_bit(GLF_PREFETCH, &gl->gl_flags);

	GFS_ASSERT_GLOCK(!gl->gl_req_gh, gl,);
	set_bit(GLF_LOCK, &gl->gl_flags);
	spin_unlock(&gl->gl_spin);

	glops->go_xmote_th(gl, state, flags);

	atomic_inc(&gl->gl_sbd->sd_glock_prefetch_calls);
}

/**
 * gfs_glock_force_drop - Force a glock to be uncached
 * @gl: the glock
 *
 */

void
gfs_glock_force_drop(struct gfs_glock *gl)
{
	struct gfs_holder gh;

	gfs_holder_init(gl, LM_ST_UNLOCKED, 0, &gh);
	set_bit(HIF_DEMOTE, &gh.gh_iflags);
	gh.gh_owner = NULL;

	spin_lock(&gl->gl_spin);
	list_add(&gh.gh_list, &gl->gl_waiters2);
	run_queue(gl, FALSE);
	spin_unlock(&gl->gl_spin);

	wait_for_completion(&gh.gh_wait);
	gfs_holder_uninit(&gh);
}

/**
 * gfs_glock_nq_init - intialize a holder and enqueue it on a glock
 * @gl: the glock 
 * @state: the state we're requesting
 * @flags: the modifier flags
 * @gh: the holder structure
 *
 * Returns: 0, GLR_*, or -EXXX
 */

int
gfs_glock_nq_init(struct gfs_glock *gl, unsigned int state, int flags,
		  struct gfs_holder *gh)
{
	int error;

	gfs_holder_init(gl, state, flags, gh);

	error = gfs_glock_nq(gh);
	if (error)
		gfs_holder_uninit(gh);

	return error;
}

/**
 * gfs_glock_dq_uninit - dequeue a holder from a glock and initialize it
 * @gh: the holder structure
 *
 */

void
gfs_glock_dq_uninit(struct gfs_holder *gh)
{
	gfs_glock_dq(gh);
	gfs_holder_uninit(gh);
}

/**
 * gfs_glock_nq_num - acquire a glock based on lock number
 * @sdp: the filesystem
 * @number: the lock number
 * @glops: the glock operations for the type of glock
 * @state: the state to acquire the glock in
 * @flags: modifier flags for the aquisition
 * @gh: the struct gfs_holder
 *
 * Returns: 0 on success, -EXXX on failure
 */

int
gfs_glock_nq_num(struct gfs_sbd *sdp,
		 uint64_t number, struct gfs_glock_operations *glops,
		 unsigned int state, int flags, struct gfs_holder *gh)
{
	struct gfs_glock *gl;
	int error;

	error = gfs_glock_get(sdp, number, glops, CREATE, &gl);
	if (!error) {
		error = gfs_glock_nq_init(gl, state, flags, gh);
		glock_put(gl);
	}

	return error;
}

/**
 * glock_compare - Compare two struct gfs_glock structures for sorting
 * @arg_a: the first structure
 * @arg_b: the second structure
 *
 */

static int
glock_compare(const void *arg_a, const void *arg_b)
{
	struct gfs_holder *gh_a = *(struct gfs_holder **)arg_a;
	struct gfs_holder *gh_b = *(struct gfs_holder **)arg_b;
	struct lm_lockname *a = &gh_a->gh_gl->gl_name;
	struct lm_lockname *b = &gh_b->gh_gl->gl_name;
	int ret = 0;

	if (a->ln_number > b->ln_number)
		ret = 1;
	else if (a->ln_number < b->ln_number)
		ret = -1;
	else {
		if (gh_a->gh_state == LM_ST_SHARED &&
		    gh_b->gh_state == LM_ST_EXCLUSIVE)
			ret = 1;
		else if (!(gh_a->gh_flags & GL_LOCAL_EXCL) &&
			 (gh_b->gh_flags & GL_LOCAL_EXCL))
			ret = 1;
	}

	return ret;
}

/**
 * nq_m_sync - synchonously acquire more than one glock in deadlock free order
 * @num_gh: the number of structures
 * @ghs: an array of struct gfs_holder structures
 *
 * Returns: 0 on success (all glocks acquired), -EXXX on failure (no glocks acquired)
 */

static int
nq_m_sync(unsigned int num_gh, struct gfs_holder *ghs)
{
	struct gfs_holder *p[num_gh];
	unsigned int x;
	int error = 0;

	for (x = 0; x < num_gh; x++)
		p[x] = &ghs[x];

	gfs_sort(p, num_gh, sizeof(struct gfs_holder *), glock_compare);

	for (x = 0; x < num_gh; x++) {
		p[x]->gh_flags &= ~(LM_FLAG_TRY | GL_ASYNC);

		error = gfs_glock_nq(p[x]);
		if (error) {
			while (x--)
				gfs_glock_dq(p[x]);
			break;
		}
	}

	return error;
}

/**
 * gfs_glock_nq_m - acquire multiple glocks
 * @num_gh: the number of structures
 * @ghs: an array of struct gfs_holder structures
 *
 * Figure out how big an impact this function has.  Either:
 * 1) Replace this code with code that calls gfs_glock_prefetch()
 * 2) Forget async stuff and just call nq_m_sync()
 * 3) Leave it like it is
 *
 * Returns: 0 on success (all glocks acquired), -EXXX on failure (no glocks acquired)
 */

int
gfs_glock_nq_m(unsigned int num_gh, struct gfs_holder *ghs)
{
	int e[num_gh];
	unsigned int x;
	int borked = FALSE, serious = 0;
	int error = 0;

	GFS_ASSERT(num_gh,);

	if (num_gh == 1) {
		ghs->gh_flags &= ~(LM_FLAG_TRY | GL_ASYNC);
		error = gfs_glock_nq(ghs);
		return error;
	}

	if (!GFS_ASYNC_LM(ghs->gh_gl->gl_sbd)) {
		error = nq_m_sync(num_gh, ghs);
		return error;
	}

	for (x = 0; x < num_gh; x++) {
		ghs[x].gh_flags |= LM_FLAG_TRY | GL_ASYNC;
		gfs_glock_nq(&ghs[x]);
	}

	for (x = 0; x < num_gh; x++) {
		error = e[x] = glock_wait_internal(&ghs[x]);
		if (error) {
			borked = TRUE;
			if (error != GLR_TRYFAILED && error != GLR_CANCELED)
				serious = error;
		}
	}

	if (!borked)
		return 0;

	for (x = 0; x < num_gh; x++)
		if (!e[x])
			gfs_glock_dq(&ghs[x]);

	if (serious)
		error = serious;
	else {
		for (x = 0; x < num_gh; x++)
			gfs_holder_reinit(ghs[x].gh_state, ghs[x].gh_flags,
					  &ghs[x]);
		error = nq_m_sync(num_gh, ghs);
	}

	return error;
}

/**
 * gfs_glock_dq_m - release multiple glocks
 * @num_gh: the number of structures
 * @ghs: an array of struct gfs_holder structures
 *
 */

void
gfs_glock_dq_m(unsigned int num_gh, struct gfs_holder *ghs)
{
	unsigned int x;

	for (x = 0; x < num_gh; x++)
		gfs_glock_dq(&ghs[x]);
}

/**
 * gfs_glock_prefetch_num - prefetch a glock based on lock number
 * @sdp: the filesystem
 * @number: the lock number
 * @glops: the glock operations for the type of glock
 * @state: the state to acquire the glock in
 * @flags: modifier flags for the aquisition
 *
 * Returns: 0 on success, -EXXX on failure
 */

void
gfs_glock_prefetch_num(struct gfs_sbd *sdp,
		       uint64_t number, struct gfs_glock_operations *glops,
		       unsigned int state, int flags)
{
	struct gfs_glock *gl;
	int error;

	if (atomic_read(&sdp->sd_reclaim_count) < sdp->sd_tune.gt_reclaim_limit) {
		error = gfs_glock_get(sdp, number, glops, CREATE, &gl);
		if (!error) {
			gfs_glock_prefetch(gl, state, flags);
			glock_put(gl);
		}
	}
}

/**
 * gfs_lvb_hold - attach a LVB from a glock
 * @gl: The glock in question
 *
 */

int
gfs_lvb_hold(struct gfs_glock *gl)
{
	int error = 0;

	GFS_ASSERT_GLOCK(atomic_read(&gl->gl_count) > 0, gl,);

	lock_on_glock(gl);

	atomic_inc(&gl->gl_lvb_count);
	if (atomic_read(&gl->gl_lvb_count) == 1) {
		glock_hold(gl);
		GFS_ASSERT_GLOCK(!gl->gl_lvb, gl,);
		error = gl->gl_sbd->sd_lockstruct.ls_ops->lm_hold_lvb(gl->gl_lock,
								      &gl->gl_lvb);
		if (error) {
			glock_put(gl);
			atomic_dec(&gl->gl_lvb_count);
		}
	}

	unlock_on_glock(gl);

	return error;
}

/**
 * gfs_lvb_unhold - detach a LVB from a glock
 * @gl: The glock in question
 * 
 */

void
gfs_lvb_unhold(struct gfs_glock *gl)
{
	glock_hold(gl);

	lock_on_glock(gl);

	GFS_ASSERT_GLOCK(atomic_read(&gl->gl_lvb_count), gl,);
	if (atomic_dec_and_test(&gl->gl_lvb_count)) {
		GFS_ASSERT_GLOCK(gl->gl_lvb, gl,);
		gl->gl_sbd->sd_lockstruct.ls_ops->lm_unhold_lvb(gl->gl_lock,
								gl->gl_lvb);
		gl->gl_lvb = NULL;
		glock_put(gl);
	}

	unlock_on_glock(gl);

	glock_put(gl);
}

/**
 * gfs_lvb_sync - sync a LVB
 * @gl: The glock in question
 * 
 */

void
gfs_lvb_sync(struct gfs_glock *gl)
{
	GFS_ASSERT_GLOCK(atomic_read(&gl->gl_lvb_count), gl,);

	lock_on_glock(gl);

	GFS_ASSERT_GLOCK(gfs_glock_is_held_excl(gl), gl,);
	gl->gl_sbd->sd_lockstruct.ls_ops->lm_sync_lvb(gl->gl_lock, gl->gl_lvb);

	unlock_on_glock(gl);
}

/**
 * gfs_glock_cb - Callback used by locking module
 * @fsdata: Pointer to the superblock
 * @type: Type of callback
 * @data: Type dependent data pointer
 *
 * Called by the locking module when it wants to tell us something.
 * Either we need to drop a lock or another client expired.
 */

void
gfs_glock_cb(lm_fsdata_t * fsdata, unsigned int type, void *data)
{
	struct gfs_sbd *sdp = (struct gfs_sbd *)fsdata;
	struct gfs_glock *gl;
	struct lm_lockname *name = NULL;
	unsigned int state = 0;
	struct lm_async_cb *async;
	unsigned int journal;

	atomic_inc(&sdp->sd_lm_callbacks);

	switch (type) {
	case LM_CB_NEED_E:
		name = (struct lm_lockname *)data;
		state = LM_ST_UNLOCKED;
		break;

	case LM_CB_NEED_D:
		name = (struct lm_lockname *)data;
		state = LM_ST_DEFERRED;
		break;

	case LM_CB_NEED_S:
		name = (struct lm_lockname *)data;
		state = LM_ST_SHARED;
		break;

	case LM_CB_ASYNC:
		async = (struct lm_async_cb *)data;

		gl = gfs_glock_find(sdp, &async->lc_name);
		GFS_ASSERT_SBD(gl, sdp,);
		GFS_ASSERT_GLOCK(gl->gl_req_bh, gl,);
		gl->gl_req_bh(gl, async->lc_ret);
		glock_put(gl);

		break;

	case LM_CB_NEED_RECOVERY:
		journal = *(unsigned int *)data;

		gfs_add_dirty_j(sdp, journal);

		if (test_bit(SDF_RECOVERD_RUN, &sdp->sd_flags))
			wake_up_process(sdp->sd_recoverd_process);

		break;

	case LM_CB_DROPLOCKS:
		gfs_gl_hash_clear(sdp, FALSE);
		gfs_quota_scan(sdp);
		break;

	default:
		GFS_ASSERT_SBD(FALSE, sdp,
			       printk("type = %u\n", type););
		break;
	}

	if (name) {
		gl = gfs_glock_find(sdp, name);
		if (gl) {
			if (gl->gl_ops->go_callback)
				gl->gl_ops->go_callback(gl, state);
			handle_callback(gl, state);
			spin_lock(&gl->gl_spin);
			run_queue(gl, FALSE);
			spin_unlock(&gl->gl_spin);
			glock_put(gl);
		}
	}
}

/**
 * gfs_try_toss_inode - try to remove a particular inode from GFS' cache
 * sdp: the filesystem
 * inum: the inode number
 *
 */

void
gfs_try_toss_inode(struct gfs_sbd *sdp, struct gfs_inum *inum)
{
	struct gfs_glock *gl;
	struct gfs_inode *ip;
	int error;

	error = gfs_glock_get(sdp,
			      inum->no_formal_ino, &gfs_inode_glops,
			      NO_CREATE, &gl);
	if (error || !gl)
		return;

	if (!trylock_on_glock(gl))
		goto out;

	if (!queue_empty(gl, &gl->gl_holders))
		goto out_unlock;

	ip = gl2ip(gl);
	if (!ip)
		goto out_unlock;

	if (atomic_read(&ip->i_count))
		goto out_unlock;

	gfs_inode_destroy(ip);

 out_unlock:
	unlock_on_glock(gl);

 out:
	glock_put(gl);
}

/**
 * gfs_iopen_go_callback - Try to kick the inode/vnode associated with an iopen glock from memory
 * @io_gl: the iopen glock
 * @state: the state into which the glock should be put
 *
 */

void
gfs_iopen_go_callback(struct gfs_glock *io_gl, unsigned int state)
{
	struct gfs_glock *i_gl;
	struct gfs_inode *ip;

	if (state != LM_ST_UNLOCKED)
		return;

	spin_lock(&io_gl->gl_spin);
	i_gl = gl2gl(io_gl);
	if (i_gl) {
		glock_hold(i_gl);
		spin_unlock(&io_gl->gl_spin);
	} else {
		spin_unlock(&io_gl->gl_spin);
		return;
	}

	if (trylock_on_glock(i_gl)) {
		if (queue_empty(i_gl, &i_gl->gl_holders)) {
			ip = gl2ip(i_gl);
			if (ip) {
				gfs_try_toss_vnode(ip);
				unlock_on_glock(i_gl);
				gfs_glock_schedule_for_reclaim(i_gl);
				goto out;
			}
		}
		unlock_on_glock(i_gl);
	}

 out:
	glock_put(i_gl);
}

/**
 * demote_ok - check to see if it's ok to unlock a glock
 * @gl: the glock
 *
 * Returns: TRUE if it's ok
 */

static int
demote_ok(struct gfs_glock *gl)
{
	struct gfs_sbd *sdp = gl->gl_sbd;
	struct gfs_glock_operations *glops = gl->gl_ops;
	int demote = TRUE;

	if (test_bit(GLF_STICKY, &gl->gl_flags))
		demote = FALSE;
	else if (test_bit(GLF_PREFETCH, &gl->gl_flags))
		demote = time_after_eq(jiffies,
				       gl->gl_stamp +
				       sdp->sd_tune.gt_prefetch_secs * HZ);
	else if (glops->go_demote_ok)
		demote = glops->go_demote_ok(gl);

	return demote;
}

/**
 * gfs_glock_schedule_for_reclaim - Add a glock to the reclaim list
 * @gl: the glock
 *
 */

void
gfs_glock_schedule_for_reclaim(struct gfs_glock *gl)
{
	struct gfs_sbd *sdp = gl->gl_sbd;

	spin_lock(&sdp->sd_reclaim_lock);
	if (list_empty(&gl->gl_reclaim)) {
		glock_hold(gl);
		list_add(&gl->gl_reclaim, &sdp->sd_reclaim_list);
		atomic_inc(&sdp->sd_reclaim_count);
	}
	spin_unlock(&sdp->sd_reclaim_lock);

	wake_up(&sdp->sd_reclaim_wchan);
}

/**
 * gfs_reclaim_glock - process an glock on the reclaim list
 * @sdp: the filesystem
 *
 */

void
gfs_reclaim_glock(struct gfs_sbd *sdp)
{
	struct gfs_glock *gl;
	struct gfs_gl_hash_bucket *bucket;

	spin_lock(&sdp->sd_reclaim_lock);

	if (list_empty(&sdp->sd_reclaim_list)) {
		spin_unlock(&sdp->sd_reclaim_lock);
		return;
	}

	gl = list_entry(sdp->sd_reclaim_list.next,
			struct gfs_glock, gl_reclaim);
	list_del_init(&gl->gl_reclaim);

	spin_unlock(&sdp->sd_reclaim_lock);

	atomic_dec(&sdp->sd_reclaim_count);
	atomic_inc(&sdp->sd_reclaimed);

	if (trylock_on_glock(gl)) {
		if (queue_empty(gl, &gl->gl_holders)) {
			if (gl->gl_ops == &gfs_inode_glops) {
				struct gfs_inode *ip = gl2ip(gl);
				if (ip && !atomic_read(&ip->i_count))
					gfs_inode_destroy(ip);
			}
			if (gl->gl_state != LM_ST_UNLOCKED &&
			    demote_ok(gl))
				handle_callback(gl, LM_ST_UNLOCKED);
		}
		unlock_on_glock(gl);
	}

	bucket = gl->gl_bucket;

	write_lock(&bucket->hb_lock);
	if (atomic_read(&gl->gl_count) == 1) {
		list_del_init(&gl->gl_list);
		write_unlock(&bucket->hb_lock);
		glock_free(gl);
	} else {
		write_unlock(&bucket->hb_lock);
		glock_put(gl);
	}
}

/**
 * examine_bucket - Call a function for glock in a hash bucket
 * @examiner: the function 
 * @sdp: the filesystem
 * @bucket: the bucket
 *
 * Returns: TRUE if the bucket is has entries
 */

static int
examine_bucket(glock_examiner examiner,
	       struct gfs_sbd *sdp, struct gfs_gl_hash_bucket *bucket)
{
	struct glock_plug plug;
	struct list_head *tmp;
	struct gfs_glock *gl;
	int entries;

	memset(&plug.gl_flags, 0, sizeof(unsigned long));
	set_bit(GLF_PLUG, &plug.gl_flags);

	write_lock(&bucket->hb_lock);
	list_add(&plug.gl_list, &bucket->hb_list);
	write_unlock(&bucket->hb_lock);

	for (;;) {
		write_lock(&bucket->hb_lock);

		for (;;) {
			tmp = plug.gl_list.next;
			if (tmp == &bucket->hb_list) {
				list_del(&plug.gl_list);
				entries = !list_empty(&bucket->hb_list);
				write_unlock(&bucket->hb_lock);
				return entries;
			}
			gl = list_entry(tmp, struct gfs_glock, gl_list);

			list_move(&plug.gl_list, &gl->gl_list);

			if (test_bit(GLF_PLUG, &gl->gl_flags))
				continue;

			glock_hold(gl);

			break;
		}

		write_unlock(&bucket->hb_lock);

		examiner(gl);
	}
}

/**
 * scan_glock - lock at a glock and see if we can do stuff to it
 * @gl: the glock to look at
 *
 */

static void
scan_glock(struct gfs_glock *gl)
{
	if (trylock_on_glock(gl)) {
		if (queue_empty(gl, &gl->gl_holders)) {
			if (gl->gl_ops == &gfs_inode_glops) {
				struct gfs_inode *ip = gl2ip(gl);
				if (ip && !atomic_read(&ip->i_count)) {
					unlock_on_glock(gl);
					gfs_glock_schedule_for_reclaim(gl);
					goto out;
				}
			}
			if (gl->gl_state != LM_ST_UNLOCKED &&
			    demote_ok(gl)) {
				unlock_on_glock(gl);
				gfs_glock_schedule_for_reclaim(gl);
				goto out;
			}
		}

		unlock_on_glock(gl);
	}

 out:
	glock_put(gl);
}

/**
 * gfs_scand_internal - Look for glocks and inodes to toss from memory
 * @sdp: the filesystem
 *
 */

void
gfs_scand_internal(struct gfs_sbd *sdp)
{
	unsigned int x;

	for (x = 0; x < GFS_GL_HASH_SIZE; x++) {
		examine_bucket(scan_glock, sdp, &sdp->sd_gl_hash[x]);
		cond_resched();
	}
}

/**
 * clear_glock - lock at a glock and see if we can do stuff to it
 * @gl: the glock to look at
 * @timeout: demote locks left unused for longer than this many seconds
 *
 */

static void
clear_glock(struct gfs_glock *gl)
{
	struct gfs_sbd *sdp = gl->gl_sbd;
	struct gfs_gl_hash_bucket *bucket = gl->gl_bucket;

	spin_lock(&sdp->sd_reclaim_lock);
	if (!list_empty(&gl->gl_reclaim)) {
		list_del_init(&gl->gl_reclaim);
		atomic_dec(&sdp->sd_reclaim_count);
		glock_put(gl);
	}
	spin_unlock(&sdp->sd_reclaim_lock);

	if (trylock_on_glock(gl)) {
		if (queue_empty(gl, &gl->gl_holders)) {
			if (gl->gl_ops == &gfs_inode_glops) {
				struct gfs_inode *ip = gl2ip(gl);
				if (ip && !atomic_read(&ip->i_count))
					gfs_inode_destroy(ip);
			}
			if (gl->gl_state != LM_ST_UNLOCKED)
				handle_callback(gl, LM_ST_UNLOCKED);
		}

		unlock_on_glock(gl);
	}

	write_lock(&bucket->hb_lock);
	if (atomic_read(&gl->gl_count) == 1) {
		list_del_init(&gl->gl_list);
		write_unlock(&bucket->hb_lock);
		glock_free(gl);
	} else {
		write_unlock(&bucket->hb_lock);
		glock_put(gl);
	}
}

/**
 * gfs_gl_hash_clear - Empty out the glock hash table
 * @sdp: the filesystem
 * @wait: wait until it's all gone
 *
 */

void
gfs_gl_hash_clear(struct gfs_sbd *sdp, int wait)
{
	unsigned long t;
	unsigned int x;
	int cont;

	t = jiffies;

	for (;;) {
		cont = FALSE;

		for (x = 0; x < GFS_GL_HASH_SIZE; x++)
			if (examine_bucket(clear_glock, sdp, &sdp->sd_gl_hash[x]))
				cont = TRUE;

		if (!wait || !cont)
			break;

		if (time_after_eq(jiffies, t + sdp->sd_tune.gt_stall_secs * HZ)) {
			printk("GFS: fsid=%s: Unmount seems to be stalled. Dumping lock state...\n",
			       sdp->sd_fsname);
			gfs_dump_lockstate(sdp, NULL);
			t = jiffies;
		}

		invalidate_inodes(sdp->sd_vfs);
		yield();
	}
}

/*
 *  Diagnostic routines to help debug distributed deadlock
 */

/**
 * dump_holder - print information about a glock holder
 * @str: a string naming the type of holder
 * @gh: the glock holder
 * @buf: the buffer
 * @size: the size of the buffer
 * @count: where we are in the buffer
 *
 * Returns: 0 on success, -ENOBUFS when we run out of space
 */

static int
dump_holder(char *str, struct gfs_holder *gh,
	    char *buf, unsigned int size, unsigned int *count)
{
	unsigned int x;
	int error = 0;

	gfs_sprintf("  %s\n", str);
	gfs_sprintf("    owner = %ld\n",
		    (gh->gh_owner) ? (long)gh->gh_owner->pid : -1);
	gfs_sprintf("    gh_state = %u\n", gh->gh_state);
	gfs_sprintf("    gh_flags =");
	for (x = 0; x < 32; x++)
		if (gh->gh_flags & (1 << x))
			gfs_sprintf(" %u", x);
	gfs_sprintf(" \n");
	gfs_sprintf("    error = %d\n", gh->gh_error);
	gfs_sprintf("    gh_iflags =");
	for (x = 0; x < 32; x++)
		if (test_bit(x, &gh->gh_iflags))
			gfs_sprintf(" %u", x);
	gfs_sprintf(" \n");

 out:
	return error;
}

/**
 * dump_inode - print information about an inode
 * @ip: the inode
 * @buf: the buffer
 * @size: the size of the buffer
 * @count: where we are in the buffer
 *
 * Returns: 0 on success, -ENOBUFS when we run out of space
 */

static int
dump_inode(struct gfs_inode *ip,
	   char *buf, unsigned int size, unsigned int *count)
{
	unsigned int x;
	int error = 0;

	gfs_sprintf("  Inode:\n");
	gfs_sprintf("    num = %" PRIu64 "/%" PRIu64 "\n",
		    ip->i_num.no_formal_ino, ip->i_num.no_addr);
	gfs_sprintf("    type = %u\n", ip->i_di.di_type);
	gfs_sprintf("    i_count = %d\n", atomic_read(&ip->i_count));
	gfs_sprintf("    i_flags =");
	for (x = 0; x < 32; x++)
		if (test_bit(x, &ip->i_flags))
			gfs_sprintf(" %u", x);
	gfs_sprintf(" \n");
	gfs_sprintf("    vnode = %s\n", (ip->i_vnode) ? "yes" : "no");

 out:
	return error;
}

/**
 * dump_glock - print information about a glock
 * @gl: the glock
 * @buf: the buffer
 * @size: the size of the buffer
 * @count: where we are in the buffer
 *
 * Returns: 0 on success, -ENOBUFS when we run out of space
 */

static int
dump_glock(struct gfs_glock *gl,
	   char *buf, unsigned int size, unsigned int *count)
{
	struct list_head *head, *tmp;
	struct gfs_holder *gh;
	unsigned int x;
	int error = 0;

	spin_lock(&gl->gl_spin);

	gfs_sprintf("Glock (%u, %" PRIu64 ")\n",
		    gl->gl_name.ln_type,
		    gl->gl_name.ln_number);
	gfs_sprintf("  gl_flags =");
	for (x = 0; x < 32; x++)
		if (test_bit(x, &gl->gl_flags))
			gfs_sprintf(" %u", x);
	gfs_sprintf(" \n");
	gfs_sprintf("  gl_count = %d\n", atomic_read(&gl->gl_count));
	gfs_sprintf("  gl_state = %u\n", gl->gl_state);
	gfs_sprintf("  lvb_count = %d\n", atomic_read(&gl->gl_lvb_count));
	gfs_sprintf("  object = %s\n", (gl->gl_object) ? "yes" : "no");
	if (gl->gl_aspace)
		gfs_sprintf("  aspace = %lu\n",
			    gl->gl_aspace->i_mapping->nrpages);
	else
		gfs_sprintf("  aspace = no\n");
	gfs_sprintf("  reclaim = %s\n",
		    (list_empty(&gl->gl_reclaim)) ? "no" : "yes");
	if (gl->gl_req_gh) {
		error = dump_holder("Request", gl->gl_req_gh, buf, size, count);
		if (error)
			goto out;
	}
	for (head = &gl->gl_holders, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		gh = list_entry(tmp, struct gfs_holder, gh_list);
		error = dump_holder("Holder", gh, buf, size, count);
		if (error)
			goto out;
	}
	for (head = &gl->gl_waiters1, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		gh = list_entry(tmp, struct gfs_holder, gh_list);
		error = dump_holder("Waiter1", gh, buf, size, count);
		if (error)
			goto out;
	}
	for (head = &gl->gl_waiters2, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		gh = list_entry(tmp, struct gfs_holder, gh_list);
		error = dump_holder("Waiter2", gh, buf, size, count);
		if (error)
			goto out;
	}
	if (gl->gl_ops == &gfs_inode_glops && gl2ip(gl)) {
		if (!test_bit(GLF_LOCK, &gl->gl_flags) &&
		    list_empty(&gl->gl_holders)) {
			error = dump_inode(gl2ip(gl), buf, size, count);
			if (error)
				goto out;
		} else
			gfs_sprintf("  Inode: busy\n");
	}

 out:
	spin_unlock(&gl->gl_spin);

	return error;
}

/**
 * gfs_dump_lockstate - print out the current lockstate
 * @sdp: the filesystem
 * @ub: the buffer to copy the information into
 *
 * If @ub is NULL, dump the lockstate to the console.
 *
 */

int
gfs_dump_lockstate(struct gfs_sbd *sdp, struct gfs_user_buffer *ub)
{
	struct gfs_gl_hash_bucket *bucket;
	struct list_head *tmp, *head;
	struct gfs_glock *gl;
	char *buf = NULL;
	unsigned int size = sdp->sd_tune.gt_lockdump_size;
	unsigned int x, count;
	int error = 0;

	if (ub) {
		buf = kmalloc(size, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
	}

	for (x = 0; x < GFS_GL_HASH_SIZE; x++) {
		bucket = &sdp->sd_gl_hash[x];
		count = 0;

		read_lock(&bucket->hb_lock);

		for (head = &bucket->hb_list, tmp = head->next;
		     tmp != head;
		     tmp = tmp->next) {
			gl = list_entry(tmp, struct gfs_glock, gl_list);

			if (test_bit(GLF_PLUG, &gl->gl_flags))
				continue;

			error = dump_glock(gl, buf, size, &count);
			if (error)
				break;
		}

		read_unlock(&bucket->hb_lock);

		if (error)
			break;

		if (ub) {
			if (ub->ub_count + count > ub->ub_size) {
				error = -ENOMEM;
				break;
			}
			if (copy_to_user(ub->ub_data + ub->ub_count, buf, count)) {
				error = -EFAULT;
				break;
			}
			ub->ub_count += count;
		}
	}

	if (ub)
		kfree(buf);

	return error;
}
