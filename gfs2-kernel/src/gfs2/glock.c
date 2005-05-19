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

#include "gfs2.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "lm.h"
#include "lops.h"
#include "meta_io.h"
#include "quota.h"
#include "super.h"

/*  Must be kept in sync with the beginning of struct gfs2_glock  */
struct glock_plug {
	struct list_head gl_list;
	unsigned long gl_flags;
};

struct greedy {
	struct gfs2_holder gr_gh;
	struct work_struct gr_work;
};

typedef void (*glock_examiner) (struct gfs2_glock * gl);

/**
 * relaxed_state_ok - is a requested lock compatible with the current lock mode?
 * @actual: the current state of the lock
 * @requested: the lock state that was requested by the caller
 * @flags: the modifier flags passed in by the caller
 *
 * Returns: TRUE if the locks are compatible, FALSE otherwise
 *
 * It's often possible that a holder B may request the lock in SHARED mode,
 * while another holder A (on this same node) has the lock in EXCLUSIVE mode
 * (node must hold the glock in EXCLUSIVE mode for this situation, of course).
 * This is okay to grant, in some cases, since both holders would have access
 * to the in-core up-to-date cached data that the EX holder would write to disk.
 * This is the default behavior.
 *
 * The EXACT flag disallows this behavior, though.  A SHARED request would
 * compatible only with a SHARED lock with this flag.
 *
 * The ANY flag provides broader permission to grant the lock to a holder,
 * whatever the requested state is, as long as the lock is locked in any mode.
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
	ENTER(G2FN_GL_HASH)
	unsigned int h;

	h = gfs2_hash(&name->ln_number, sizeof(uint64_t));
	h = gfs2_hash_more(&name->ln_type, sizeof(unsigned int), h);
	h &= GFS2_GL_HASH_MASK;

	RETURN(G2FN_GL_HASH, h);
}

/**
 * glock_hold() - increment reference count on glock
 * @gl: The glock to hold
 *
 */

static __inline__ void
glock_hold(struct gfs2_glock *gl)
{
	gfs2_assert(gl->gl_sbd, atomic_read(&gl->gl_count) > 0,);
	atomic_inc(&gl->gl_count);
}

/**
 * glock_put() - Decrement reference count on glock
 * @gl: The glock to put
 *
 */

static __inline__ void
glock_put(struct gfs2_glock *gl)
{
	if (atomic_read(&gl->gl_count) == 1)
		gfs2_glock_schedule_for_reclaim(gl);
	gfs2_assert(gl->gl_sbd, atomic_read(&gl->gl_count) > 0,);
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
queue_empty(struct gfs2_glock *gl, struct list_head *head)
{
	int empty;
	spin_lock(&gl->gl_spin);
	empty = list_empty(head);
	spin_unlock(&gl->gl_spin);
	return empty;
}

/**
 * search_bucket() - Find struct gfs2_glock by lock number
 * @bucket: the bucket to search
 * @name: The lock name
 *
 * Returns: NULL, or the struct gfs2_glock with the requested number
 */

static struct gfs2_glock *
search_bucket(struct gfs2_gl_hash_bucket *bucket, struct lm_lockname *name)
{
	ENTER(G2FN_SEARCH_BUCKET)
	struct list_head *tmp, *head;
	struct gfs2_glock *gl;

	for (head = &bucket->hb_list, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		gl = list_entry(tmp, struct gfs2_glock, gl_list);

		if (test_bit(GLF_PLUG, &gl->gl_flags))
			continue;
		if (!lm_name_equal(&gl->gl_name, name))
			continue;

		atomic_inc(&gl->gl_count);

		RETURN(G2FN_SEARCH_BUCKET, gl);
	}

	RETURN(G2FN_SEARCH_BUCKET, NULL);
}

/**
 * gfs2_glock_find() - Find glock by lock number
 * @sdp: The GFS2 superblock
 * @name: The lock name
 *
 * Figure out what bucket the lock is in, acquire the read lock on
 * it and call search_bucket().
 *
 * Returns: NULL, or the struct gfs2_glock with the requested number
 */

struct gfs2_glock *
gfs2_glock_find(struct gfs2_sbd *sdp, struct lm_lockname *name)
{
	ENTER(G2FN_GLOCK_FIND)
	struct gfs2_gl_hash_bucket *bucket = &sdp->sd_gl_hash[gl_hash(name)];
	struct gfs2_glock *gl;

	read_lock(&bucket->hb_lock);
	gl = search_bucket(bucket, name);
	read_unlock(&bucket->hb_lock);

	RETURN(G2FN_GLOCK_FIND, gl);
}

/**
 * glock_free() - Perform a few checks and then release struct gfs2_glock
 * @gl: The glock to release
 *
 * Also calls lock module to release its internal structure for this glock.
 *
 */

static void
glock_free(struct gfs2_glock *gl)
{
	ENTER(G2FN_GLOCK_FREE)
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct inode *aspace = gl->gl_aspace;

	gfs2_lm_put_lock(sdp, gl->gl_lock);

	if (aspace)
		gfs2_aspace_put(aspace);

	gfs2_memory_rm(gl);
	kmem_cache_free(gfs2_glock_cachep, gl);

	atomic_dec(&sdp->sd_glock_count);

	RET(G2FN_GLOCK_FREE);
}

/**
 * gfs2_glock_get() - Get a glock, or create one if one doesn't exist
 * @sdp: The GFS2 superblock
 * @number: the lock number
 * @glops: The glock_operations to use
 * @create: If FALSE, don't create the glock if it doesn't exist
 * @glp: the glock is returned here
 *
 * This does not lock a glock, just finds/creates structures for one.
 * 
 * Returns: errno
 */

int
gfs2_glock_get(struct gfs2_sbd *sdp,
	      uint64_t number, struct gfs2_glock_operations *glops,
	      int create, struct gfs2_glock **glp)
{
	ENTER(G2FN_GLOCK_GET)
	struct lm_lockname name;
	struct gfs2_glock *gl, *tmp;
	struct gfs2_gl_hash_bucket *bucket;
	int error;

	/* Look for pre-existing glock in hash table */
	name.ln_number = number;
	name.ln_type = glops->go_type;
	bucket = &sdp->sd_gl_hash[gl_hash(&name)];

	read_lock(&bucket->hb_lock);
	gl = search_bucket(bucket, &name);
	read_unlock(&bucket->hb_lock);

	if (gl || !create) {
		*glp = gl;
		RETURN(G2FN_GLOCK_GET, 0);
	}

	/* None found; create a new one */
	gl = kmem_cache_alloc(gfs2_glock_cachep, GFP_KERNEL);
	if (!gl)
		RETURN(G2FN_GLOCK_GET, -ENOMEM);
	gfs2_memory_add(gl);

	memset(gl, 0, sizeof(struct gfs2_glock));

	INIT_LIST_HEAD(&gl->gl_list);
	gl->gl_name = name;
	atomic_set(&gl->gl_count, 1);

	spin_lock_init(&gl->gl_spin);

	gl->gl_state = LM_ST_UNLOCKED;
	INIT_LIST_HEAD(&gl->gl_holders);
	INIT_LIST_HEAD(&gl->gl_waiters1);
	INIT_LIST_HEAD(&gl->gl_waiters2);
	INIT_LIST_HEAD(&gl->gl_waiters3);

	gl->gl_ops = glops;

	gl->gl_bucket = bucket;
	INIT_LIST_HEAD(&gl->gl_reclaim);

	gl->gl_sbd = sdp;

	INIT_LE(&gl->gl_le, &gfs2_glock_lops);
	INIT_LIST_HEAD(&gl->gl_ail_list);

	/* If this glock protects actual on-disk data or metadata blocks,
	   create a VFS inode to manage the pages/buffers holding them. */
	if (glops == &gfs2_inode_glops ||
	    glops == &gfs2_rgrp_glops ||
	    glops == &gfs2_meta_glops) {
		gl->gl_aspace = gfs2_aspace_get(sdp);
		if (!gl->gl_aspace) {
			error = -ENOMEM;
			goto fail;
		}
	}

	/* Ask lock module to find/create its structure for this lock
	   (but this doesn't lock the inter-node lock yet) */
	error = gfs2_lm_get_lock(sdp, &name, &gl->gl_lock);
	if (error)
		goto fail_aspace;

	atomic_inc(&sdp->sd_glock_count);

	/* Double-check, in case another process created the glock, and has
	   put it in the hash table while we were preparing this one */
	write_lock(&bucket->hb_lock);
	tmp = search_bucket(bucket, &name);
	if (tmp) {
		/* Somebody beat us to it; forget the one we prepared */
		write_unlock(&bucket->hb_lock);
		glock_free(gl);
		gl = tmp;
	} else {
		/* Add our glock to hash table */
		list_add_tail(&gl->gl_list, &bucket->hb_list);
		write_unlock(&bucket->hb_lock);
	}

	*glp = gl;

	RETURN(G2FN_GLOCK_GET, 0);

 fail_aspace:
	if (gl->gl_aspace)
		gfs2_aspace_put(gl->gl_aspace);

 fail:
	gfs2_memory_rm(gl);
	kmem_cache_free(gfs2_glock_cachep, gl);	

	RETURN(G2FN_GLOCK_GET, error);
}

/**
 * gfs2_glock_hold() - As glock_hold(), but suitable for exporting
 * @gl: The glock to hold
 *
 */

void
gfs2_glock_hold(struct gfs2_glock *gl)
{
	ENTER(G2FN_GLOCK_HOLD)
	glock_hold(gl);
	RET(G2FN_GLOCK_HOLD);
}

/**
 * gfs2_glock_put() - As glock_put(), but suitable for exporting
 * @gl: The glock to put
 *
 */

void
gfs2_glock_put(struct gfs2_glock *gl)
{
	ENTER(G2FN_GLOCK_PUT)
	glock_put(gl);
	RET(G2FN_GLOCK_PUT);
}

/**
 * gfs2_holder_init - initialize a struct gfs2_holder in the default way
 * @gl: the glock 
 * @state: the state we're requesting
 * @flags: the modifier flags
 * @gh: the holder structure
 *
 */

void
gfs2_holder_init(struct gfs2_glock *gl, unsigned int state, int flags,
		struct gfs2_holder *gh)
{
	ENTER(G2FN_HOLDER_INIT)

	INIT_LIST_HEAD(&gh->gh_list);
	gh->gh_gl = gl;
	gh->gh_owner = (flags & GL_NEVER_RECURSE) ? NULL : current;
	gh->gh_state = state;
	gh->gh_flags = flags;
	gh->gh_error = 0;
	gh->gh_iflags = 0;
	init_completion(&gh->gh_wait);

	if (gh->gh_state == LM_ST_EXCLUSIVE)
		gh->gh_flags |= GL_LOCAL_EXCL;

	glock_hold(gl);

	RET(G2FN_HOLDER_INIT);
}

/**
 * gfs2_holder_reinit - reinitialize a struct gfs2_holder so we can requeue it
 * @state: the state we're requesting
 * @flags: the modifier flags
 * @gh: the holder structure
 *
 * Preserve holder's associated glock and owning process.
 * Reset all holder state flags (we're starting a new request from scratch),
 *   except for HIF_ALLOCED.
 * Don't do glock_hold() again (it was done in gfs2_holder_init()).
 * Don't mess with the glock.
 *
 * Rules:
 *   Holder must have been gfs2_holder_init()d already
 *   Holder must *not* be in glock's holder list or wait queues now
 */

void
gfs2_holder_reinit(unsigned int state, int flags, struct gfs2_holder *gh)
{
	ENTER(G2FN_HOLDER_REINIT)

	gh->gh_state = state;
	gh->gh_flags = flags;
	if (gh->gh_state == LM_ST_EXCLUSIVE)
		gh->gh_flags |= GL_LOCAL_EXCL;

	gh->gh_iflags &= 1 << HIF_ALLOCED;

	RET(G2FN_HOLDER_REINIT);
}

/**
 * gfs2_holder_uninit - uninitialize a holder structure (drop reference on glock)
 * @gh: the holder structure
 *
 */

void
gfs2_holder_uninit(struct gfs2_holder *gh)
{
	ENTER(G2FN_HOLDER_UNINIT)
	glock_put(gh->gh_gl);
	gh->gh_gl = NULL;
	RET(G2FN_HOLDER_UNINIT);
}

/**
 * gfs2_holder_get - get a struct gfs2_holder structure
 * @gl: the glock 
 * @state: the state we're requesting
 * @flags: the modifier flags
 *
 * Figure out how big an impact this function has.  Either:
 * 1) Replace it with a cache of structures hanging off the struct gfs2_sbd
 * 2) Leave it like it is
 *
 * Returns: the holder structure, NULL on ENOMEM
 */

struct gfs2_holder *
gfs2_holder_get(struct gfs2_glock *gl, unsigned int state, int flags)
{
	ENTER(G2FN_HOLDER_GET)
	struct gfs2_holder *gh;

	gh = kmalloc(sizeof(struct gfs2_holder), GFP_KERNEL);
	if (!gh)
		RETURN(G2FN_HOLDER_GET, NULL);

	gfs2_holder_init(gl, state, flags, gh);
	set_bit(HIF_ALLOCED, &gh->gh_iflags);

	RETURN(G2FN_HOLDER_GET, gh);
}

/**
 * gfs2_holder_put - get rid of a struct gfs2_holder structure
 * @gh: the holder structure
 *
 */

void
gfs2_holder_put(struct gfs2_holder *gh)
{
	ENTER(G2FN_HOLDER_PUT)
	gfs2_holder_uninit(gh);
	kfree(gh);
	RET(G2FN_HOLDER_PUT);
}

/**
 * handle_recurse - put other holder structures (marked recursive) into the holders list
 * @gh: the holder structure
 *
 */

static void
handle_recurse(struct gfs2_holder *gh)
{
	ENTER(G2FN_HANDLE_RECURSE)
	struct gfs2_glock *gl = gh->gh_gl;
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct list_head *tmp, *head, *next;
	struct gfs2_holder *tmp_gh;
	int found = FALSE;

	if (gfs2_assert_warn(sdp, gh->gh_owner))
		RET(G2FN_HANDLE_RECURSE);

	for (head = &gl->gl_waiters3, tmp = head->next, next = tmp->next;
	     tmp != head;
	     tmp = next, next = tmp->next) {
		tmp_gh = list_entry(tmp, struct gfs2_holder, gh_list);
		if (tmp_gh->gh_owner != gh->gh_owner)
			continue;

		gfs2_assert_warn(sdp, test_bit(HIF_RECURSE, &tmp_gh->gh_iflags));

		list_move_tail(&tmp_gh->gh_list, &gl->gl_holders);
		tmp_gh->gh_error = 0;
		set_bit(HIF_HOLDER, &tmp_gh->gh_iflags);

		complete(&tmp_gh->gh_wait);

		found = TRUE;
	}

	gfs2_assert_warn(sdp, found);

	RET(G2FN_HANDLE_RECURSE);
}

/**
 * do_unrecurse - a recursive holder was just dropped of the waiters3 list
 * @gh: the holder
 *
 * If there is only one other recursive holder, clear its HIF_RECURSE bit
 *   (it's no longer a recursive request).
 * If there is more than one, leave them alone (they're recursive!).
 *
 */

static void
do_unrecurse(struct gfs2_holder *gh)
{
	ENTER(G2FN_DO_UNRECURSE)
	struct gfs2_glock *gl = gh->gh_gl;
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct list_head *tmp, *head;
	struct gfs2_holder *tmp_gh, *last_gh = NULL;
	int found = FALSE;

	if (gfs2_assert_warn(sdp, gh->gh_owner))
		RET(G2FN_DO_UNRECURSE);

	for (head = &gl->gl_waiters3, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		tmp_gh = list_entry(tmp, struct gfs2_holder, gh_list);
		if (tmp_gh->gh_owner != gh->gh_owner)
			continue;

		gfs2_assert_warn(sdp, test_bit(HIF_RECURSE, &tmp_gh->gh_iflags));

		/* found more than one */
		if (found)
			RET(G2FN_DO_UNRECURSE);

		found = TRUE;
		last_gh = tmp_gh;
	}

	/* found just one */
	if (!gfs2_assert_warn(sdp, found))
		clear_bit(HIF_RECURSE, &last_gh->gh_iflags);

	RET(G2FN_DO_UNRECURSE);
}

/**
 * rq_mutex - process a mutex request in the queue
 * @gh: the glock holder
 *
 * Returns: TRUE if the queue is blocked (always, since there can be only one
 *      holder of the mutex).
 *
 * See lock_on_glock()
 */

static int
rq_mutex(struct gfs2_holder *gh)
{
	ENTER(G2FN_RQ_MUTEX)
	struct gfs2_glock *gl = gh->gh_gl;

	list_del_init(&gh->gh_list);
	/*  gh->gh_error never examined.  */
	set_bit(GLF_LOCK, &gl->gl_flags);
	complete(&gh->gh_wait);

	RETURN(G2FN_RQ_MUTEX, TRUE);
}

/**
 * rq_promote - process a promote request in the queue
 * @gh: the glock holder
 *
 * Acquire a new inter-node lock, or change a lock state to more restrictive.
 *
 * Returns: TRUE if the queue is blocked
 */

static int
rq_promote(struct gfs2_holder *gh)
{
	ENTER(G2FN_RQ_PROMOTE)
	struct gfs2_glock *gl = gh->gh_gl;
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct gfs2_glock_operations *glops = gl->gl_ops;
	int recurse;

	if (!relaxed_state_ok(gl->gl_state, gh->gh_state, gh->gh_flags)) {
		if (list_empty(&gl->gl_holders)) {
			gl->gl_req_gh = gh;
			set_bit(GLF_LOCK, &gl->gl_flags);
			spin_unlock(&gl->gl_spin);

			/* If we notice a lot of glocks in reclaim list, free
			   up memory for 2 of them before locking a new one */
			if (atomic_read(&sdp->sd_reclaim_count) >
			    gfs2_tune_get(sdp, gt_reclaim_limit) &&
			    !(gh->gh_flags & LM_FLAG_PRIORITY)) {
				gfs2_reclaim_glock(sdp);
				gfs2_reclaim_glock(sdp);
			}

			glops->go_xmote_th(gl, gh->gh_state,
					   gh->gh_flags);

			spin_lock(&gl->gl_spin);
		}
		RETURN(G2FN_RQ_PROMOTE, TRUE);
	}

	if (list_empty(&gl->gl_holders)) {
		set_bit(HIF_FIRST, &gh->gh_iflags);
		set_bit(GLF_LOCK, &gl->gl_flags);
		recurse = FALSE;
	} else {
		struct gfs2_holder *next_gh;
		if (gh->gh_flags & GL_LOCAL_EXCL)
			RETURN(G2FN_RQ_PROMOTE, TRUE);
		next_gh = list_entry(gl->gl_holders.next, struct gfs2_holder, gh_list);
		if (next_gh->gh_flags & GL_LOCAL_EXCL)
			 RETURN(G2FN_RQ_PROMOTE, TRUE);
		recurse = test_bit(HIF_RECURSE, &gh->gh_iflags);
	}

	list_move_tail(&gh->gh_list, &gl->gl_holders);
	gh->gh_error = 0;
	set_bit(HIF_HOLDER, &gh->gh_iflags);

	if (recurse)
		handle_recurse(gh);

	complete(&gh->gh_wait);

	RETURN(G2FN_RQ_PROMOTE, FALSE);
}

/**
 * rq_demote - process a demote request in the queue
 * @gh: the glock holder
 *
 * Returns: TRUE if the queue is blocked
 *
 * Unlock an inter-node lock, or change a lock state to less restrictive.
 * If the glock is already the same as the holder's requested state, or is
 *   UNLOCKED, no lock module request is required.
 * Otherwise, we need to ask lock module to unlock or change locked state
 *   of the glock.
 * If requested state is UNLOCKED, or current glock state is SHARED or
 *   DEFERRED (neither of which have a less restrictive state other than
 *   UNLOCK), we call go_drop_th() to unlock the lock.
 * Otherwise (i.e. requested is SHARED or DEFERRED, and current is EXCLUSIVE),
 *   we can continue to hold the lock, and just ask for a new state;
 *   we call go_xmote_th() to change state.
 *
 * Must be called with glock's gl->gl_spin locked.
 */

static int
rq_demote(struct gfs2_holder *gh)
{
	ENTER(G2FN_RQ_DEMOTE)
	struct gfs2_glock *gl = gh->gh_gl;
	struct gfs2_glock_operations *glops = gl->gl_ops;

	if (!list_empty(&gl->gl_holders))
		RETURN(G2FN_RQ_DEMOTE, TRUE);

	if (gl->gl_state == gh->gh_state || gl->gl_state == LM_ST_UNLOCKED) {
		list_del_init(&gh->gh_list);
		gh->gh_error = 0;
		spin_unlock(&gl->gl_spin);
		if (test_bit(HIF_DEALLOC, &gh->gh_iflags))
			gfs2_holder_put(gh);
		else
			complete(&gh->gh_wait);
		spin_lock(&gl->gl_spin);
	} else {
		gl->gl_req_gh = gh;
		set_bit(GLF_LOCK, &gl->gl_flags);
		spin_unlock(&gl->gl_spin);

		if (gh->gh_state == LM_ST_UNLOCKED ||
		    gl->gl_state != LM_ST_EXCLUSIVE)
			/* Unlock */
			glops->go_drop_th(gl);
		else
			/* Change state while holding lock */
			glops->go_xmote_th(gl, gh->gh_state, gh->gh_flags);

		spin_lock(&gl->gl_spin);
	}

	RETURN(G2FN_RQ_DEMOTE, FALSE);
}

/**
 * rq_greedy - process a queued request to drop greedy status
 * @gh: the glock holder
 *
 * Returns: TRUE if the queue is blocked
 */

static int
rq_greedy(struct gfs2_holder *gh)
{
	ENTER(G2FN_RQ_GREEDY)
	struct gfs2_glock *gl = gh->gh_gl;

	list_del_init(&gh->gh_list);
	/*  gh->gh_error never examined.  */
	clear_bit(GLF_GREEDY, &gl->gl_flags);
	spin_unlock(&gl->gl_spin);

	gfs2_holder_uninit(gh);
	kfree(container_of(gh, struct greedy, gr_gh));

	spin_lock(&gl->gl_spin);		

	RETURN(G2FN_RQ_GREEDY, FALSE);
}

/**
 * run_queue - process holder structures on the glock's wait queues
 * @gl: the glock
 *
 * Rules:
 *   Caller must hold gl->gl_spin.
 */

static void
run_queue(struct gfs2_glock *gl)
{
	ENTER(G2FN_RUN_QUEUE)
	struct gfs2_holder *gh;
	int blocked = TRUE;

	for (;;) {
		/* Another process is manipulating the glock structure;
		   we can't do anything now */
		if (test_bit(GLF_LOCK, &gl->gl_flags))
			break;

		/* Waiting to manipulate the glock structure */
		if (!list_empty(&gl->gl_waiters1)) {
			gh = list_entry(gl->gl_waiters1.next,
					struct gfs2_holder, gh_list);

			if (test_bit(HIF_MUTEX, &gh->gh_iflags))
				blocked = rq_mutex(gh);
			else
				gfs2_assert_warn(gl->gl_sbd, FALSE);

		/* Waiting to demote the lock, or drop greedy status */
		} else if (!list_empty(&gl->gl_waiters2) &&
			   !test_bit(GLF_SKIP_WAITERS2, &gl->gl_flags)) {
			gh = list_entry(gl->gl_waiters2.next,
					struct gfs2_holder, gh_list);

			if (test_bit(HIF_DEMOTE, &gh->gh_iflags))
				blocked = rq_demote(gh);
			else if (test_bit(HIF_GREEDY, &gh->gh_iflags))
				blocked = rq_greedy(gh);
			else
				gfs2_assert_warn(gl->gl_sbd, FALSE);

		/* Waiting to promote the lock */
		} else if (!list_empty(&gl->gl_waiters3)) {
			gh = list_entry(gl->gl_waiters3.next,
					struct gfs2_holder, gh_list);

			if (test_bit(HIF_PROMOTE, &gh->gh_iflags))
				blocked = rq_promote(gh);
			else
				gfs2_assert_warn(gl->gl_sbd, FALSE);

		} else
			break;

		if (blocked)
			break;
	}

	RET(G2FN_RUN_QUEUE);
}

/**
 * gfs2_glmutex_lock - acquire a local lock on a glock (structure)
 * @gl: the glock
 *
 * Gives caller exclusive access to manipulate a glock structure.
 * Has nothing to do with inter-node lock state or GL_LOCAL_EXCL!
 *
 * If structure already locked, places temporary holder structure on glock's
 * wait-for-exclusive-access queue, and blocks until exclusive access granted.
 */

void
gfs2_glmutex_lock(struct gfs2_glock *gl)
{
	ENTER(G2FN_GLMUTEX_LOCK)
	struct gfs2_holder gh;

	gfs2_holder_init(gl, 0, 0, &gh);
	set_bit(HIF_MUTEX, &gh.gh_iflags);

	spin_lock(&gl->gl_spin);
	if (test_and_set_bit(GLF_LOCK, &gl->gl_flags))
		list_add_tail(&gh.gh_list, &gl->gl_waiters1);
	else
		complete(&gh.gh_wait);
	spin_unlock(&gl->gl_spin);

	wait_for_completion(&gh.gh_wait);
	gfs2_holder_uninit(&gh);

	RET(G2FN_GLMUTEX_LOCK);
}

/**
 * gfs2_glmutex_trylock - try to acquire a local lock on a glock (structure)
 * @gl: the glock
 *
 * Returns: TRUE if the glock is acquired
 *
 * Tries to give caller exclusive access to manipulate a glock structure.
 * Has nothing to do with inter-node lock state or LOCAL_EXCL!
 *
 * If structure already locked, does not block to wait; returns FALSE.
 */

int
gfs2_glmutex_trylock(struct gfs2_glock *gl)
{
	ENTER(G2FN_GLMUTEX_TRYLOCK)
	int acquired = TRUE;

	spin_lock(&gl->gl_spin);
	if (test_and_set_bit(GLF_LOCK, &gl->gl_flags))
		acquired = FALSE;
	spin_unlock(&gl->gl_spin);

	RETURN(G2FN_GLMUTEX_TRYLOCK, acquired);
}

/**
 * gfs2_glmutex_unlock - release a local lock on a glock (structure)
 * @gl: the glock
 *
 * Caller is done manipulating glock structure.
 * Service any others waiting for exclusive access.
 */

void
gfs2_glmutex_unlock(struct gfs2_glock *gl)
{
	ENTER(G2FN_GLMUTEX_UNLOCK)

	spin_lock(&gl->gl_spin);
	clear_bit(GLF_LOCK, &gl->gl_flags);
	run_queue(gl);
	spin_unlock(&gl->gl_spin);

	RET(G2FN_GLMUTEX_UNLOCK);
}

/**
 * handle_callback - add a demote request to a lock's queue
 * @gl: the glock
 * @state: the state the caller wants us to change to
 *
 * Called when we learn that another node needs a lock held by this node,
 *   or when this node simply wants to drop a lock as soon as it's done with
 *   it (NOCACHE flag), or dump a glock out of glock cache (reclaim it).
 *
 * We are told the @state that will satisfy the needs of the caller, so
 *   we can ask for a demote to that state.
 *
 * If another demote request is already on the queue for a different state, just
 *   set its request to UNLOCK (and don't bother queueing a request for us).
 *   This consolidates LM requests and moves the lock to the least restrictive
 *   state, so it will be compatible with whatever reason we were called.
 *   No need to be too smart here.  Demotes between the shared and deferred
 *   states will often fail, so don't even try.
 *
 * Otherwise, queue a demote request to the requested state.
 */

static void
handle_callback(struct gfs2_glock *gl, unsigned int state)
{
	ENTER(G2FN_HANDLE_CALLBACK)
	struct list_head *tmp, *head;
	struct gfs2_holder *gh, *new_gh = NULL;

 restart:
	spin_lock(&gl->gl_spin);

	/* If another queued demote request is for a different state,
	   set its request to UNLOCKED */
	for (head = &gl->gl_waiters2, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		gh = list_entry(tmp, struct gfs2_holder, gh_list);
		if (test_bit(HIF_DEMOTE, &gh->gh_iflags) &&
		    gl->gl_req_gh != gh) {
			if (gh->gh_state != state)
				gh->gh_state = LM_ST_UNLOCKED;
			goto out;
		}
	}

	/* pass 2; add new holder to glock's demote request queue */
	if (new_gh) {
		list_add_tail(&new_gh->gh_list, &gl->gl_waiters2);
		new_gh = NULL;

	/* pass 1; set up a new holder struct for a demote request, then
	   check again to see if another process added a demote request
	   while we were preparing this one. */
	} else {
		spin_unlock(&gl->gl_spin);

		RETRY_MALLOC(new_gh = gfs2_holder_get(gl, state,
						      LM_FLAG_TRY |
						      GL_NEVER_RECURSE),
			     new_gh);
		set_bit(HIF_DEMOTE, &new_gh->gh_iflags);
		set_bit(HIF_DEALLOC, &new_gh->gh_iflags);

		goto restart;
	}

 out:
	spin_unlock(&gl->gl_spin);

	if (new_gh)
		gfs2_holder_put(new_gh);

	RET(G2FN_HANDLE_CALLBACK);
}

/**
 * state_change - record that the glock is now in a different state
 * @gl: the glock
 * @new_state the new state
 *
 */

static void
state_change(struct gfs2_glock *gl, unsigned int new_state)
{
	ENTER(G2FN_STATE_CHANGE)
	struct gfs2_sbd *sdp = gl->gl_sbd;
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

	RET(G2FN_STATE_CHANGE);
}

/**
 * xmote_bh - Called after the lock module is done acquiring a lock
 * @gl: The glock in question
 * @ret: the int returned from the lock module
 *
 */

static void
xmote_bh(struct gfs2_glock *gl, unsigned int ret)
{
	ENTER(G2FN_XMOTE_BH)
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct gfs2_glock_operations *glops = gl->gl_ops;
	struct gfs2_holder *gh = gl->gl_req_gh;
	int prev_state = gl->gl_state;
	int op_done = TRUE;

	gfs2_assert_warn(sdp, test_bit(GLF_LOCK, &gl->gl_flags));
	gfs2_assert_warn(sdp, queue_empty(gl, &gl->gl_holders));
	gfs2_assert_warn(sdp, !(ret & LM_OUT_ASYNC));

	state_change(gl, ret & LM_OUT_ST_MASK);

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

	else if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags))) {
		spin_lock(&gl->gl_spin);
		list_del_init(&gh->gh_list);
		gh->gh_error = -EIO;
		if (test_bit(HIF_RECURSE, &gh->gh_iflags))
			do_unrecurse(gh);
		spin_unlock(&gl->gl_spin);

	} else if (test_bit(HIF_DEMOTE, &gh->gh_iflags)) {
		spin_lock(&gl->gl_spin);
		list_del_init(&gh->gh_list);
		if (gl->gl_state == gh->gh_state ||
		    gl->gl_state == LM_ST_UNLOCKED)
			gh->gh_error = 0;
		else {
			if (gfs2_assert_warn(sdp, gh->gh_flags &
					     (LM_FLAG_TRY | LM_FLAG_TRY_1CB)) == -1)
				printk("GFS2: fsid=%s: ret = 0x%.8X\n",
				       sdp->sd_fsname, ret);
			gh->gh_error = GLR_TRYFAILED;
		}
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

	} else {
		if (gfs2_assert_withdraw(sdp, FALSE) == -1)
			printk("GFS2: fsid=%s: ret = 0x%.8X\n",
			       sdp->sd_fsname, ret);
	}

	if (glops->go_xmote_bh)
		glops->go_xmote_bh(gl);

	if (op_done) {
		spin_lock(&gl->gl_spin);
		gl->gl_req_gh = NULL;
		gl->gl_req_bh = NULL;
		clear_bit(GLF_LOCK, &gl->gl_flags);
		run_queue(gl);
		spin_unlock(&gl->gl_spin);
	}

	glock_put(gl);

	if (gh) {
		if (test_bit(HIF_DEALLOC, &gh->gh_iflags))
			gfs2_holder_put(gh);
		else
			complete(&gh->gh_wait);
	}

	RET(G2FN_XMOTE_BH);
}

/**
 * gfs2_glock_xmote_th - Call into the lock module to acquire or change a glock
 * @gl: The glock in question
 * @state: the requested state
 * @flags: modifier flags to the lock call
 *
 * Used to acquire a new glock, or to change an already-acquired glock to
 *   more/less restrictive state (other than LM_ST_UNLOCKED).
 *
 * *Not* used to unlock a glock; use gfs2_glock_drop_th() for that.
 */

void
gfs2_glock_xmote_th(struct gfs2_glock *gl, unsigned int state, int flags)
{
	ENTER(G2FN_GLOCK_XMOTE_TH)
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct gfs2_glock_operations *glops = gl->gl_ops;
	int lck_flags = flags & (LM_FLAG_TRY | LM_FLAG_TRY_1CB |
				 LM_FLAG_NOEXP | LM_FLAG_ANY |
				 LM_FLAG_PRIORITY);
	unsigned int lck_ret;

	gfs2_assert_warn(sdp, test_bit(GLF_LOCK, &gl->gl_flags));
	gfs2_assert_warn(sdp, queue_empty(gl, &gl->gl_holders));
	gfs2_assert_warn(sdp, state != LM_ST_UNLOCKED);
	gfs2_assert_warn(sdp, state != gl->gl_state);

	/* Current state EX, may need to sync log/data/metadata to disk */
	if (gl->gl_state == LM_ST_EXCLUSIVE) {
		if (glops->go_sync)
			glops->go_sync(gl, DIO_METADATA | DIO_DATA | DIO_RELEASE);
	}

	glock_hold(gl);
	gl->gl_req_bh = xmote_bh;

	atomic_inc(&sdp->sd_lm_lock_calls);

	lck_ret = gfs2_lm_lock(sdp, gl->gl_lock,
			      gl->gl_state, state,
			      lck_flags);

	if (lck_ret & LM_OUT_ASYNC)
		gfs2_assert_warn(sdp, lck_ret == LM_OUT_ASYNC);
	else
		xmote_bh(gl, lck_ret);

	RET(G2FN_GLOCK_XMOTE_TH);
}

/**
 * drop_bh - Called after a lock module unlock completes
 * @gl: the glock
 * @ret: the return status
 *
 * Doesn't wake up the process waiting on the struct gfs2_holder (if any)
 * Doesn't drop the reference on the glock the top half took out
 *
 */

static void
drop_bh(struct gfs2_glock *gl, unsigned int ret)
{
	ENTER(G2FN_DROP_BH)
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct gfs2_glock_operations *glops = gl->gl_ops;
	struct gfs2_holder *gh = gl->gl_req_gh;

	clear_bit(GLF_PREFETCH, &gl->gl_flags);

	gfs2_assert_warn(sdp, test_bit(GLF_LOCK, &gl->gl_flags));
	gfs2_assert_warn(sdp, queue_empty(gl, &gl->gl_holders));
	gfs2_assert_warn(sdp, !ret);

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
	run_queue(gl);
	spin_unlock(&gl->gl_spin);

	glock_put(gl);

	if (gh) {
		if (test_bit(HIF_DEALLOC, &gh->gh_iflags))
			gfs2_holder_put(gh);
		else
			complete(&gh->gh_wait);
	}

	RET(G2FN_DROP_BH);
}

/**
 * gfs2_glock_drop_th - call into the lock module to unlock a lock 
 * @gl: the glock
 *
 */

void
gfs2_glock_drop_th(struct gfs2_glock *gl)
{
	ENTER(G2FN_GLOCK_DROP_TH)
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct gfs2_glock_operations *glops = gl->gl_ops;
	unsigned int ret;

	gfs2_assert_warn(sdp, test_bit(GLF_LOCK, &gl->gl_flags));
	gfs2_assert_warn(sdp, queue_empty(gl, &gl->gl_holders));
	gfs2_assert_warn(sdp, gl->gl_state != LM_ST_UNLOCKED);

	/* Leaving state EX, may need to sync log/data/metadata to disk */
	if (gl->gl_state == LM_ST_EXCLUSIVE) {
		if (glops->go_sync)
			glops->go_sync(gl, DIO_METADATA | DIO_DATA | DIO_RELEASE);
	}

	glock_hold(gl);
	gl->gl_req_bh = drop_bh;

	atomic_inc(&sdp->sd_lm_unlock_calls);

	ret = gfs2_lm_unlock(sdp, gl->gl_lock, gl->gl_state);

	if (!ret)
		drop_bh(gl, ret);
	else
		gfs2_assert_warn(sdp, ret == LM_OUT_ASYNC);

	RET(G2FN_GLOCK_DROP_TH);
}

/**
 * do_cancels - cancel requests for locks stuck waiting on an expire flag
 * @gh: the LM_FLAG_PRIORITY holder waiting to acquire the lock
 *
 * Don't cancel GL_NOCANCEL requests.
 */

static void
do_cancels(struct gfs2_holder *gh)
{
	ENTER(G2FN_DO_CANCELS)
	struct gfs2_glock *gl = gh->gh_gl;

	spin_lock(&gl->gl_spin);

	while (gl->gl_req_gh != gh &&
	       !test_bit(HIF_HOLDER, &gh->gh_iflags) &&
	       !list_empty(&gh->gh_list)) {
		if (gl->gl_req_bh &&
		    !(gl->gl_req_gh &&
		      (gl->gl_req_gh->gh_flags & GL_NOCANCEL))) {
			spin_unlock(&gl->gl_spin);
			gfs2_lm_cancel(gl->gl_sbd, gl->gl_lock);
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(HZ / 10);
			spin_lock(&gl->gl_spin);
		} else {
			spin_unlock(&gl->gl_spin);
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(HZ / 10);
			spin_lock(&gl->gl_spin);
		}
	}

	spin_unlock(&gl->gl_spin);

	RET(G2FN_DO_CANCELS);
}

/**
 * glock_wait_internal - wait on a glock acquisition
 * @gh: the glock holder
 *
 * Returns: 0 on success
 */

static int
glock_wait_internal(struct gfs2_holder *gh)
{
	ENTER(G2FN_GLOCK_WAIT_INTERNAL)
	struct gfs2_glock *gl = gh->gh_gl;
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct gfs2_glock_operations *glops = gl->gl_ops;
	int error = 0;

	if (test_bit(HIF_ABORTED, &gh->gh_iflags))
		RETURN(G2FN_GLOCK_WAIT_INTERNAL, -EIO);

	if (gh->gh_flags & (LM_FLAG_TRY | LM_FLAG_TRY_1CB)) {
		spin_lock(&gl->gl_spin);
		if (gl->gl_req_gh != gh &&
		    !test_bit(HIF_HOLDER, &gh->gh_iflags) &&
		    !list_empty(&gh->gh_list)) {
			list_del_init(&gh->gh_list);
			gh->gh_error = GLR_TRYFAILED;
			if (test_bit(HIF_RECURSE, &gh->gh_iflags))
				do_unrecurse(gh);
			run_queue(gl);
			spin_unlock(&gl->gl_spin);
			RETURN(G2FN_GLOCK_WAIT_INTERNAL, GLR_TRYFAILED);
		}
		spin_unlock(&gl->gl_spin);
	}

	if (gh->gh_flags & LM_FLAG_PRIORITY)
		do_cancels(gh);

	wait_for_completion(&gh->gh_wait);

	if (gh->gh_error)
		RETURN(G2FN_GLOCK_WAIT_INTERNAL, gh->gh_error);

	gfs2_assert_withdraw(sdp, test_bit(HIF_HOLDER, &gh->gh_iflags));
	gfs2_assert_withdraw(sdp, relaxed_state_ok(gl->gl_state,
						   gh->gh_state,
						   gh->gh_flags));

	if (test_bit(HIF_FIRST, &gh->gh_iflags)) {
		gfs2_assert_warn(sdp, test_bit(GLF_LOCK, &gl->gl_flags));

		if (glops->go_lock) {
			error = glops->go_lock(gh);
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
		run_queue(gl);
		spin_unlock(&gl->gl_spin);
	}

	RETURN(G2FN_GLOCK_WAIT_INTERNAL, error);
}

static __inline__ struct gfs2_holder *
find_holder_by_owner(struct list_head *head, struct task_struct *owner)
{
	ENTER(G2FN_FIND_HOLDER_BY_OWNER)
	struct list_head *tmp;
	struct gfs2_holder *gh;

	for (tmp = head->next; tmp != head; tmp = tmp->next) {
		gh = list_entry(tmp, struct gfs2_holder, gh_list);
		if (gh->gh_owner == owner)
			RETURN(G2FN_FIND_HOLDER_BY_OWNER, gh);
	}

	RETURN(G2FN_FIND_HOLDER_BY_OWNER, NULL);
}

/**
 * recurse_check -
 *
 * Make sure the new holder is compatible
 * with the pre-existing one.
 *
 */

static int
recurse_check(struct gfs2_holder *existing, struct gfs2_holder *new,
	      unsigned int state)
{
	ENTER(RECURSE_CHECK)
	struct gfs2_sbd *sdp = existing->gh_gl->gl_sbd;
	int error = 0;

	if (gfs2_assert_warn(sdp, (new->gh_flags & LM_FLAG_ANY) ||
			     !(existing->gh_flags & LM_FLAG_ANY)) ||
	    gfs2_assert_warn(sdp, (existing->gh_flags & GL_LOCAL_EXCL) ||
			     !(new->gh_flags & GL_LOCAL_EXCL)) ||
	    gfs2_assert_warn(sdp, relaxed_state_ok(state,
						   new->gh_state,
						   new->gh_flags))) {
		set_bit(HIF_ABORTED, &new->gh_iflags);
		error = -EINVAL;
	}

	RETURN(RECURSE_CHECK, error);
}

/**
 * add_to_queue - Add a holder to the wait-for-promotion queue or holder list
 *       (according to recursion)
 * @gh: the holder structure to add
 *
 * If the hold requestor's process already has a granted lock (on holder list),
 *   and this new request is compatible, go ahead and grant it, adding this
 *   new holder to the glock's holder list. 
 *
 * If the hold requestor's process has earlier requested a lock, and is still
 *   waiting for it to be granted, and this new request is compatible with
 *   the earlier one, they can be handled at the same time when the request
 *   is finally granted.  Mark both (all) with RECURSE flags, and add new
 *   holder to wait-for-promotion queue.
 *
 * If there is no previous holder from this process (on holder list or wait-
 *   for-promotion queue), simply add new holder to wait-for-promotion queue.
 */

static void
add_to_queue(struct gfs2_holder *gh)
{
	ENTER(G2FN_ADD_TO_QUEUE)
	struct gfs2_glock *gl = gh->gh_gl;
	struct gfs2_holder *existing;

	if (!gh->gh_owner)
		goto out;

	/* Search through glock's holders list to see if this process
	   already holds a granted lock. */
	existing = find_holder_by_owner(&gl->gl_holders, gh->gh_owner);
	if (existing) {
		if (recurse_check(existing, gh, gl->gl_state))
			RET(G2FN_ADD_TO_QUEUE);

		/* Grant the hold. */
		list_add_tail(&gh->gh_list, &gl->gl_holders);
		set_bit(HIF_HOLDER, &gh->gh_iflags);

		gh->gh_error = 0;
		complete(&gh->gh_wait);

		RET(G2FN_ADD_TO_QUEUE);
	}

	/* Search through glock's wait-for-promotion list to
	   see if this process already is waiting for a grant. */
	existing = find_holder_by_owner(&gl->gl_waiters3, gh->gh_owner);
	if (existing) {
		if (recurse_check(existing, gh, existing->gh_state))
			RET(G2FN_ADD_TO_QUEUE);

		/* Make sure they're marked, so when one gets granted,
		   the other will too. */
		set_bit(HIF_RECURSE, &gh->gh_iflags);
		set_bit(HIF_RECURSE, &existing->gh_iflags);

		list_add_tail(&gh->gh_list, &gl->gl_waiters3);

		RET(G2FN_ADD_TO_QUEUE);
	}

 out:
	if (gh->gh_flags & LM_FLAG_PRIORITY)
		list_add(&gh->gh_list, &gl->gl_waiters3);
	else
		list_add_tail(&gh->gh_list, &gl->gl_waiters3);	

	RET(G2FN_ADD_TO_QUEUE);
}

/**
 * gfs2_glock_nq - enqueue a struct gfs2_holder onto a glock (acquire a glock)
 * @gh: the holder structure
 *
 * if (gh->gh_flags & GL_ASYNC), this never returns an error
 *
 * Returns: 0, GLR_TRYFAILED, or errno on failure
 *
 * Rules:
 *   @gh must not be already attached to a glock.
 *   Don't ask for UNLOCKED state (use gfs2_glock_dq() for that).
 *   LM_FLAG_ANY (liberal) and GL_EXACT (restrictive) are mutually exclusive.
 */

int
gfs2_glock_nq(struct gfs2_holder *gh)
{
	ENTER(G2FN_GLOCK_NQ)
	struct gfs2_glock *gl = gh->gh_gl;
	struct gfs2_sbd *sdp = gl->gl_sbd;
	int error = 0;

	atomic_inc(&sdp->sd_glock_nq_calls);

 restart:
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags))) {
		set_bit(HIF_ABORTED, &gh->gh_iflags);
		RETURN(G2FN_GLOCK_NQ, -EIO);
	}

	set_bit(HIF_PROMOTE, &gh->gh_iflags);

	spin_lock(&gl->gl_spin);
	add_to_queue(gh);
	run_queue(gl);
	spin_unlock(&gl->gl_spin);

	if (!(gh->gh_flags & GL_ASYNC)) {
		error = glock_wait_internal(gh);
		if (error == GLR_CANCELED) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(HZ);
			goto restart;
		}
	}

	clear_bit(GLF_PREFETCH, &gl->gl_flags);

	RETURN(G2FN_GLOCK_NQ, error);
}

/**
 * gfs2_glock_poll - poll to see if an async request has been completed
 * @gh: the holder
 *
 * Returns: TRUE if the request is ready to be gfs2_glock_wait()ed on
 */

int
gfs2_glock_poll(struct gfs2_holder *gh)
{
	ENTER(G2FN_GLOCK_POLL)
	struct gfs2_glock *gl = gh->gh_gl;
	int ready = FALSE;

	spin_lock(&gl->gl_spin);

	if (test_bit(HIF_HOLDER, &gh->gh_iflags))
		ready = TRUE;
	else if (list_empty(&gh->gh_list)) {
		if (gh->gh_error == GLR_CANCELED) {
			spin_unlock(&gl->gl_spin);
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(HZ);
			if (gfs2_glock_nq(gh))
				RETURN(G2FN_GLOCK_POLL, TRUE);
			RETURN(G2FN_GLOCK_POLL, FALSE);
		} else
			ready = TRUE;
	}

	spin_unlock(&gl->gl_spin);

	RETURN(G2FN_GLOCK_POLL, ready);
}

/**
 * gfs2_glock_wait - wait for a lock acquisition that ended in a GLR_ASYNC
 * @gh: the holder structure
 *
 * Returns: 0, GLR_TRYFAILED, or errno on failure
 */

int
gfs2_glock_wait(struct gfs2_holder *gh)
{
	ENTER(G2FN_GLOCK_WAIT)
	int error;

	error = glock_wait_internal(gh);
	if (error == GLR_CANCELED) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ);
		gh->gh_flags &= ~GL_ASYNC;
		error = gfs2_glock_nq(gh);
	}

	RETURN(G2FN_GLOCK_WAIT, error);
}

/**
 * gfs2_glock_dq - dequeue a struct gfs2_holder from a glock (release a glock)
 * @gh: the glock holder
 *
 * This releases a local process' hold on a glock, and services other waiters.
 * If this is the last holder on this node, calls glock operation go_unlock(),
 *    and go_sync() if requested by glock's GL_SYNC flag.
 * If glock's GL_NOCACHE flag is set, requests demotion to unlock the inter-
 *    node lock now, rather than caching the glock for later use.
 * Otherwise, this function does *not* release the glock at inter-node scope.
 *   The glock will stay in glock cache until:
 *   --  This node uses it again (extending residence in glock cache), or
 *   --  Another node asks (via callback) for the lock, or
 *   --  The glock sits unused in glock cache for a while, and the cleanup
 *         daemons (gfs2_scand and gfs2_glockd) reclaim it.
 */

void
gfs2_glock_dq(struct gfs2_holder *gh)
{
	ENTER(G2FN_GLOCK_DQ)
	struct gfs2_glock *gl = gh->gh_gl;
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct gfs2_glock_operations *glops = gl->gl_ops;

	atomic_inc(&sdp->sd_glock_dq_calls);

	if (gh->gh_flags & GL_SYNC)
		set_bit(GLF_SYNC, &gl->gl_flags);

	/* Don't cache glock; request demote to unlock at inter-node scope */
	if (gh->gh_flags & GL_NOCACHE)
		handle_callback(gl, LM_ST_UNLOCKED);

	gfs2_glmutex_lock(gl);

	spin_lock(&gl->gl_spin);
	list_del_init(&gh->gh_list);

	/* If last holder, do appropriate glock operations, set cache timer */
	if (list_empty(&gl->gl_holders)) {
		spin_unlock(&gl->gl_spin);

		if (glops->go_unlock)
			glops->go_unlock(gh);

		/* Do "early" sync, if requested by holder */
		if (test_bit(GLF_SYNC, &gl->gl_flags)) {
			if (glops->go_sync)
				glops->go_sync(gl,
					       DIO_METADATA |
					       DIO_DATA);
		}

		gl->gl_stamp = jiffies;

		spin_lock(&gl->gl_spin);
	}

	clear_bit(GLF_LOCK, &gl->gl_flags);
	run_queue(gl);
	spin_unlock(&gl->gl_spin);

	RET(G2FN_GLOCK_DQ);
}

/**
 * gfs2_glock_prefetch - Try to prefetch a glock
 * @gl: the glock
 * @state: the state to prefetch in 
 * @flags: flags passed to go_xmote_th()
 *
 * Bypass request queues of glock (i.e. no holder involved), and directly call
 *   go_xmote_th() to ask lock module for lock, to put in glock cache for
 *   later use.
 *
 * Will not prefetch the lock (no need to) if a process on this node is already
 *   interested in the lock, or if it's sitting in glock cache in a compatible
 *   state.
 *
 * Rules:
 *   Don't ask for UNLOCKED state (use gfs2_glock_dq() for that).
 *   LM_FLAG_ANY (liberal) and GL_EXACT (restrictive) are mutually exclusive.
 */

void
gfs2_glock_prefetch(struct gfs2_glock *gl, unsigned int state, int flags)
{
	ENTER(G2FN_GLOCK_PREFETCH)
	struct gfs2_glock_operations *glops = gl->gl_ops;

	spin_lock(&gl->gl_spin);

	/* Should we prefetch? */
	if (test_bit(GLF_LOCK, &gl->gl_flags) ||
	    !list_empty(&gl->gl_holders) ||
	    !list_empty(&gl->gl_waiters1) ||
	    !list_empty(&gl->gl_waiters2) ||
	    !list_empty(&gl->gl_waiters3) ||
	    relaxed_state_ok(gl->gl_state, state, flags)) {
		spin_unlock(&gl->gl_spin);
		RET(G2FN_GLOCK_PREFETCH);
	}

	set_bit(GLF_PREFETCH, &gl->gl_flags);
	set_bit(GLF_LOCK, &gl->gl_flags);
	spin_unlock(&gl->gl_spin);

	glops->go_xmote_th(gl, state, flags);

	atomic_inc(&gl->gl_sbd->sd_glock_prefetch_calls);

	RET(G2FN_GLOCK_PREFETCH);
}

/**
 * gfs2_glock_force_drop - Force a glock to be uncached
 * @gl: the glock
 *
 */

void
gfs2_glock_force_drop(struct gfs2_glock *gl)
{
	ENTER(G2FN_GLOCK_FORCE_DROP)
	struct gfs2_holder gh;

	gfs2_holder_init(gl, LM_ST_UNLOCKED, GL_NEVER_RECURSE, &gh);
	set_bit(HIF_DEMOTE, &gh.gh_iflags);

	spin_lock(&gl->gl_spin);
	list_add_tail(&gh.gh_list, &gl->gl_waiters2);
	run_queue(gl);
	spin_unlock(&gl->gl_spin);

	wait_for_completion(&gh.gh_wait);
	gfs2_holder_uninit(&gh);

	RET(G2FN_GLOCK_FORCE_DROP);
}

/**
 * greedy_work -
 * @data:
 *
 */

static void
greedy_work(void *data)
{
	ENTER(G2FN_GREEDY_WORK)
	struct greedy *gr = (struct greedy *)data;
	struct gfs2_holder *gh = &gr->gr_gh;
	struct gfs2_glock *gl = gh->gh_gl;
	struct gfs2_glock_operations *glops = gl->gl_ops;

	clear_bit(GLF_SKIP_WAITERS2, &gl->gl_flags);

	if (glops->go_greedy)
		glops->go_greedy(gl);

	spin_lock(&gl->gl_spin);

	if (list_empty(&gl->gl_waiters2)) {
		clear_bit(GLF_GREEDY, &gl->gl_flags);
		spin_unlock(&gl->gl_spin);
		gfs2_holder_uninit(gh);
		kfree(gr);
	} else {
		glock_hold(gl);
		list_add_tail(&gh->gh_list, &gl->gl_waiters2);
		run_queue(gl);
		spin_unlock(&gl->gl_spin);
		glock_put(gl);
	}

	RET(G2FN_GREEDY_WORK);
}

/**
 * gfs2_glock_be_greedy -
 * @gl:
 * @time:
 *
 * Returns: 0 if go_greedy will be called, 1 otherwise
 */

int
gfs2_glock_be_greedy(struct gfs2_glock *gl, unsigned int time)
{
	ENTER(G2FN_GLOCK_BE_GREEDY)
	struct greedy *gr;
	struct gfs2_holder *gh;

	if (!time ||
	    gl->gl_sbd->sd_args.ar_localcaching ||
	    test_and_set_bit(GLF_GREEDY, &gl->gl_flags))
		RETURN(G2FN_GLOCK_BE_GREEDY, 1);

	gr = kmalloc(sizeof(struct greedy), GFP_KERNEL);
	if (!gr) {
		clear_bit(GLF_GREEDY, &gl->gl_flags);
		RETURN(G2FN_GLOCK_BE_GREEDY, 1);
	}
	gh = &gr->gr_gh;

	gfs2_holder_init(gl, 0, GL_NEVER_RECURSE, gh);
	set_bit(HIF_GREEDY, &gh->gh_iflags);
	INIT_WORK(&gr->gr_work, greedy_work, gr);

	set_bit(GLF_SKIP_WAITERS2, &gl->gl_flags);
	schedule_delayed_work(&gr->gr_work, time);

	RETURN(G2FN_GLOCK_BE_GREEDY, 0);
}

/**
 * gfs2_glock_nq_init - intialize a holder and enqueue it on a glock
 * @gl: the glock 
 * @state: the state we're requesting
 * @flags: the modifier flags
 * @gh: the holder structure
 *
 * Returns: 0, GLR_*, or errno
 */

int
gfs2_glock_nq_init(struct gfs2_glock *gl, unsigned int state, int flags,
		  struct gfs2_holder *gh)
{
	ENTER(G2FN_GLOCK_NQ_INIT)
	int error;

	gfs2_holder_init(gl, state, flags, gh);

	error = gfs2_glock_nq(gh);
	if (error)
		gfs2_holder_uninit(gh);

	RETURN(G2FN_GLOCK_NQ_INIT, error);
}

/**
 * gfs2_glock_dq_uninit - dequeue a holder from a glock and initialize it
 * @gh: the holder structure
 *
 */

void
gfs2_glock_dq_uninit(struct gfs2_holder *gh)
{
	ENTER(G2FN_GLOCK_DQ_UNINIT)

	gfs2_glock_dq(gh);
	gfs2_holder_uninit(gh);

	RET(G2FN_GLOCK_DQ_UNINIT);
}

/**
 * gfs2_glock_nq_num - acquire a glock based on lock number
 * @sdp: the filesystem
 * @number: the lock number
 * @glops: the glock operations for the type of glock
 * @state: the state to acquire the glock in
 * @flags: modifier flags for the aquisition
 * @gh: the struct gfs2_holder
 *
 * Returns: errno
 */

int
gfs2_glock_nq_num(struct gfs2_sbd *sdp,
		 uint64_t number, struct gfs2_glock_operations *glops,
		 unsigned int state, int flags, struct gfs2_holder *gh)
{
	ENTER(G2FN_GLOCK_NQ_NUM)
	struct gfs2_glock *gl;
	int error;

	error = gfs2_glock_get(sdp, number, glops, CREATE, &gl);
	if (!error) {
		error = gfs2_glock_nq_init(gl, state, flags, gh);
		glock_put(gl);
	}

	RETURN(G2FN_GLOCK_NQ_NUM, error);
}

/**
 * glock_compare - Compare two struct gfs2_glock structures for sorting
 * @arg_a: the first structure
 * @arg_b: the second structure
 *
 */

static int
glock_compare(const void *arg_a, const void *arg_b)
{
	ENTER(G2FN_GLOCK_COMPARE)
	struct gfs2_holder *gh_a = *(struct gfs2_holder **)arg_a;
	struct gfs2_holder *gh_b = *(struct gfs2_holder **)arg_b;
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

	RETURN(G2FN_GLOCK_COMPARE, ret);
}

/**
 * nq_m_sync - synchonously acquire more than one glock in deadlock free order
 * @num_gh: the number of structures
 * @ghs: an array of struct gfs2_holder structures
 *
 * Returns: 0 on success (all glocks acquired), errno on failure (no glocks acquired)
 */

static int
nq_m_sync(unsigned int num_gh, struct gfs2_holder *ghs, struct gfs2_holder **p)
{
	ENTER(G2FN_NQ_M_SYNC)
	unsigned int x;
	int error = 0;

	for (x = 0; x < num_gh; x++)
		p[x] = &ghs[x];

	gfs2_sort(p, num_gh, sizeof(struct gfs2_holder *), glock_compare);

	for (x = 0; x < num_gh; x++) {
		p[x]->gh_flags &= ~(LM_FLAG_TRY | GL_ASYNC);

		error = gfs2_glock_nq(p[x]);
		if (error) {
			while (x--)
				gfs2_glock_dq(p[x]);
			break;
		}
	}

	RETURN(G2FN_NQ_M_SYNC, error);
}

/**
 * gfs2_glock_nq_m - acquire multiple glocks
 * @num_gh: the number of structures
 * @ghs: an array of struct gfs2_holder structures
 *
 * Figure out how big an impact this function has.  Either:
 * 1) Replace this code with code that calls gfs2_glock_prefetch()
 * 2) Forget async stuff and just call nq_m_sync()
 * 3) Leave it like it is
 *
 * Returns: 0 on success (all glocks acquired), errno on failure (no glocks acquired)
 */

int
gfs2_glock_nq_m(unsigned int num_gh, struct gfs2_holder *ghs)
{
	ENTER(G2FN_GLOCK_NQ_M)
	int *e;
	unsigned int x;
	int borked = FALSE, serious = 0;
	int error = 0;

	if (!num_gh)
		RETURN(G2FN_GLOCK_NQ_M, 0);

	/* For just one gh, do request synchronously */
	if (num_gh == 1) {
		ghs->gh_flags &= ~(LM_FLAG_TRY | GL_ASYNC);
		RETURN(G2FN_GLOCK_NQ_M, gfs2_glock_nq(ghs));
	}

	/* using sizeof(struct gfs2_holder *) instead of sizeof(int), because
	 * we're also using this memory for nq_m_sync and ints should never be
	 * larger than pointers.... I hope
	 */
	e = kmalloc(num_gh * sizeof(struct gfs2_holder *), GFP_KERNEL);
	if (!e)
		RETURN(G2FN_GLOCK_NQ_M, -ENOMEM);

	/* Send off asynchronous requests */
	for (x = 0; x < num_gh; x++) {
		ghs[x].gh_flags |= LM_FLAG_TRY | GL_ASYNC;
		error = gfs2_glock_nq(&ghs[x]);
		if (error) {
			borked = TRUE;
			serious = error;
			num_gh = x;
			break;
		}
	}

	/* Wait for all to complete */
	for (x = 0; x < num_gh; x++) {
		error = e[x] = glock_wait_internal(&ghs[x]);
		if (error) {
			borked = TRUE;
			if (error != GLR_TRYFAILED && error != GLR_CANCELED)
				serious = error;
		}
	}

	/* If all good, done! */
	if (!borked) {
		kfree(e);
		RETURN(G2FN_GLOCK_NQ_M, 0);
	}

	for (x = 0; x < num_gh; x++)
		if (!e[x])
			gfs2_glock_dq(&ghs[x]);

	if (serious)
		error = serious;
	else {
		for (x = 0; x < num_gh; x++)
			gfs2_holder_reinit(ghs[x].gh_state, ghs[x].gh_flags,
					  &ghs[x]);
		error = nq_m_sync(num_gh, ghs, (struct gfs2_holder **)e);
	}

	kfree(e);
	RETURN(G2FN_GLOCK_NQ_M, error);
}

/**
 * gfs2_glock_dq_m - release multiple glocks
 * @num_gh: the number of structures
 * @ghs: an array of struct gfs2_holder structures
 *
 */

void
gfs2_glock_dq_m(unsigned int num_gh, struct gfs2_holder *ghs)
{
	ENTER(G2FN_GLOCK_DQ_M)
	unsigned int x;

	for (x = 0; x < num_gh; x++)
		gfs2_glock_dq(&ghs[x]);

	RET(G2FN_GLOCK_DQ_M);
}

/**
 * gfs2_glock_dq_uninit_m - release multiple glocks
 * @num_gh: the number of structures
 * @ghs: an array of struct gfs2_holder structures
 *
 */

void
gfs2_glock_dq_uninit_m(unsigned int num_gh, struct gfs2_holder *ghs)
{
	ENTER(G2FN_GLOCK_DQ_UNINIT_M)
	unsigned int x;

	for (x = 0; x < num_gh; x++)
		gfs2_glock_dq_uninit(&ghs[x]);

	RET(G2FN_GLOCK_DQ_UNINIT_M);
}

/**
 * gfs2_glock_prefetch_num - prefetch a glock based on lock number
 * @sdp: the filesystem
 * @number: the lock number
 * @glops: the glock operations for the type of glock
 * @state: the state to acquire the glock in
 * @flags: modifier flags for the aquisition
 *
 * Returns: errno
 */

void
gfs2_glock_prefetch_num(struct gfs2_sbd *sdp,
		       uint64_t number, struct gfs2_glock_operations *glops,
		       unsigned int state, int flags)
{
	ENTER(G2FN_GLOCK_PREFETCH_NUM)
	struct gfs2_glock *gl;
	int error;

	if (atomic_read(&sdp->sd_reclaim_count) < gfs2_tune_get(sdp, gt_reclaim_limit)) {
		error = gfs2_glock_get(sdp, number, glops, CREATE, &gl);
		if (!error) {
			gfs2_glock_prefetch(gl, state, flags);
			glock_put(gl);
		}
	}

	RET(G2FN_GLOCK_PREFETCH_NUM);
}

/**
 * gfs2_lvb_hold - attach a LVB from a glock
 * @gl: The glock in question
 *
 */

int
gfs2_lvb_hold(struct gfs2_glock *gl)
{
	ENTER(G2FN_LVB_HOLD)
	int error;

	gfs2_glmutex_lock(gl);

	if (!atomic_read(&gl->gl_lvb_count)) {
		error = gfs2_lm_hold_lvb(gl->gl_sbd, gl->gl_lock, &gl->gl_lvb);
		if (error) {
			gfs2_glmutex_unlock(gl);
			RETURN(G2FN_LVB_HOLD, error);
		}
		glock_hold(gl);
	}
	atomic_inc(&gl->gl_lvb_count);

	gfs2_glmutex_unlock(gl);

	RETURN(G2FN_LVB_HOLD, 0);
}

/**
 * gfs2_lvb_unhold - detach a LVB from a glock
 * @gl: The glock in question
 * 
 */

void
gfs2_lvb_unhold(struct gfs2_glock *gl)
{
	ENTER(G2FN_LVB_UNHOLD)

	glock_hold(gl);
	gfs2_glmutex_lock(gl);

	gfs2_assert(gl->gl_sbd, atomic_read(&gl->gl_lvb_count) > 0,);
	if (atomic_dec_and_test(&gl->gl_lvb_count)) {
		gfs2_lm_unhold_lvb(gl->gl_sbd, gl->gl_lock, gl->gl_lvb);
		gl->gl_lvb = NULL;
		glock_put(gl);
	}

	gfs2_glmutex_unlock(gl);
	glock_put(gl);

	RET(G2FN_LVB_UNHOLD);
}

/**
 * gfs2_lvb_sync - sync a LVB
 * @gl: The glock in question
 * 
 */

void
gfs2_lvb_sync(struct gfs2_glock *gl)
{
	ENTER(G2FN_LVB_SYNC)

	gfs2_glmutex_lock(gl);

	gfs2_assert(gl->gl_sbd, atomic_read(&gl->gl_lvb_count),);
	if (!gfs2_assert_warn(gl->gl_sbd, gfs2_glock_is_held_excl(gl)))
		gfs2_lm_sync_lvb(gl->gl_sbd, gl->gl_lock, gl->gl_lvb);

	gfs2_glmutex_unlock(gl);

	RET(G2FN_LVB_SYNC);
}

/**
 * blocking_cb -
 * @sdp:
 * @name:
 * @state:
 *
 */

void
blocking_cb(struct gfs2_sbd *sdp, struct lm_lockname *name, unsigned int state)
{
	ENTER(G2FN_BLOCKING_CB)
	struct gfs2_glock *gl;

	gl = gfs2_glock_find(sdp, name);
	if (!gl)
		RET(G2FN_BLOCKING_CB);

	if (gl->gl_ops->go_callback)
		gl->gl_ops->go_callback(gl, state);
	handle_callback(gl, state);

	spin_lock(&gl->gl_spin);
	run_queue(gl);
	spin_unlock(&gl->gl_spin);

	glock_put(gl);

	RET(G2FN_BLOCKING_CB);
}

/**
 * gfs2_glock_cb - Callback used by locking module
 * @fsdata: Pointer to the superblock
 * @type: Type of callback
 * @data: Type dependent data pointer
 *
 * Called by the locking module when it wants to tell us something.
 * Either we need to drop a lock, one of our ASYNC requests completed, or
 *   another client expired (crashed/died) and we need to recover its journal.
 * If another node needs a lock held by this node, we queue a request to demote
 *   our lock to a state compatible with that needed by the other node.  
 *   For example, if the other node needs EXCLUSIVE, we request UNLOCKED.
 *   SHARED and DEFERRED modes can be shared with other nodes, so we request
 *   accordingly.
 * Once all incompatible holders on this node are done with the lock, the
 *   queued request will cause run_queue() to call the lock module to demote
 *   our lock to a compatible state, allowing the other node to grab the lock.
 */

void
gfs2_glock_cb(lm_fsdata_t *fsdata, unsigned int type, void *data)
{
	ENTER(G2FN_GLOCK_CB)
	struct gfs2_sbd *sdp = (struct gfs2_sbd *)fsdata;

	atomic_inc(&sdp->sd_lm_callbacks);

	switch (type) {
	case LM_CB_NEED_E:
		blocking_cb(sdp, (struct lm_lockname *)data, LM_ST_UNLOCKED);
		RET(G2FN_GLOCK_CB);

	case LM_CB_NEED_D:
		blocking_cb(sdp, (struct lm_lockname *)data, LM_ST_DEFERRED);
		RET(G2FN_GLOCK_CB);

	case LM_CB_NEED_S:
		blocking_cb(sdp, (struct lm_lockname *)data, LM_ST_SHARED);
		RET(G2FN_GLOCK_CB);

	case LM_CB_ASYNC: {
		struct lm_async_cb *async = (struct lm_async_cb *)data;
		struct gfs2_glock *gl;

		gl = gfs2_glock_find(sdp, &async->lc_name);
		if (gfs2_assert_warn(sdp, gl))
			RET(G2FN_GLOCK_CB);
		if (!gfs2_assert_warn(sdp, gl->gl_req_bh))
			gl->gl_req_bh(gl, async->lc_ret);
		glock_put(gl);

		RET(G2FN_GLOCK_CB);
	}

	case LM_CB_NEED_RECOVERY:
		gfs2_jdesc_make_dirty(sdp, *(unsigned int *)data);
		if (test_bit(SDF_RECOVERD_RUN, &sdp->sd_flags))
			wake_up_process(sdp->sd_recoverd_process);
		RET(G2FN_GLOCK_CB);

	case LM_CB_DROPLOCKS:
		gfs2_gl_hash_clear(sdp, NO_WAIT);
		gfs2_quota_scan(sdp);
		RET(G2FN_GLOCK_CB);

	default:
		gfs2_assert_warn(sdp, FALSE);
		RET(G2FN_GLOCK_CB);
	}

	RET(G2FN_GLOCK_CB);
}

/**
 * gfs2_try_toss_inode - try to remove a particular GFS2 inode struct from cache
 * sdp: the filesystem
 * inum: the inode number
 *
 * Look for the glock protecting the inode of interest.
 * If no process is manipulating or holding the glock, see if the glock
 *   has a gfs2_inode attached.
 * If gfs2_inode has no references, unhold its iopen glock, release any
 *   indirect addressing buffers, and destroy the gfs2_inode.
 */

void
gfs2_try_toss_inode(struct gfs2_sbd *sdp, struct gfs2_inum *inum)
{
	ENTER(G2FN_TRY_TOSS_INODE)
	struct gfs2_glock *gl;
	struct gfs2_inode *ip;
	int error;

	error = gfs2_glock_get(sdp,
			      inum->no_addr, &gfs2_inode_glops,
			      NO_CREATE, &gl);
	if (error || !gl)
		RET(G2FN_TRY_TOSS_INODE);

	if (!gfs2_glmutex_trylock(gl))
		goto out;

	ip = get_gl2ip(gl);
	if (!ip)
		goto out_unlock;

	if (atomic_read(&ip->i_count))
		goto out_unlock;

	gfs2_inode_destroy(ip);

 out_unlock:
	gfs2_glmutex_unlock(gl);

 out:
	glock_put(gl);

	RET(G2FN_TRY_TOSS_INODE);
}

/**
 * gfs2_iopen_go_callback - Try to kick the inode/vnode associated with an iopen glock from memory
 * @io_gl: the iopen glock
 * @state: the state into which the glock should be put
 *
 */

void
gfs2_iopen_go_callback(struct gfs2_glock *io_gl, unsigned int state)
{
	ENTER(G2FN_IOPEN_GO_CALLBACK)
	struct gfs2_glock *i_gl;

	if (state != LM_ST_UNLOCKED)
		RET(G2FN_IOPEN_GO_CALLBACK);

	spin_lock(&io_gl->gl_spin);
	i_gl = get_gl2gl(io_gl);
	if (i_gl) {
		glock_hold(i_gl);
		spin_unlock(&io_gl->gl_spin);
	} else {
		spin_unlock(&io_gl->gl_spin);
		RET(G2FN_IOPEN_GO_CALLBACK);
	}

	if (gfs2_glmutex_trylock(i_gl)) {
		struct gfs2_inode *ip = get_gl2ip(i_gl);
		if (ip) {
			gfs2_try_toss_vnode(ip);
			gfs2_glmutex_unlock(i_gl);
			gfs2_glock_schedule_for_reclaim(i_gl);
				goto out;
		}
		gfs2_glmutex_unlock(i_gl);
	}

 out:
	glock_put(i_gl);

	RET(G2FN_IOPEN_GO_CALLBACK);
}

/**
 * demote_ok - Check to see if it's ok to unlock a glock (to remove it
 *       from glock cache)
 * @gl: the glock
 *
 * Called when trying to reclaim glocks, once it's determined that the glock
 *   has no holders on this node.
 *
 * Returns: TRUE if it's ok
 *
 * It's not okay if:
 * --  glock is STICKY
 * --  PREFETCHed glock has not been given enough chance to be used
 * --  glock-type-specific test says "no"
 */

static int
demote_ok(struct gfs2_glock *gl)
{
	ENTER(G2FN_DEMOTE_OK)
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct gfs2_glock_operations *glops = gl->gl_ops;
	int demote = TRUE;

	if (test_bit(GLF_STICKY, &gl->gl_flags))
		demote = FALSE;
	else if (test_bit(GLF_PREFETCH, &gl->gl_flags))
		demote = time_after_eq(jiffies,
				       gl->gl_stamp +
				       gfs2_tune_get(sdp, gt_prefetch_secs) * HZ);
	else if (glops->go_demote_ok)
		demote = glops->go_demote_ok(gl);

	RETURN(G2FN_DEMOTE_OK, demote);
}

/**
 * gfs2_glock_schedule_for_reclaim - Add a glock to the reclaim list
 * @gl: the glock
 *
 */

void
gfs2_glock_schedule_for_reclaim(struct gfs2_glock *gl)
{
	ENTER(G2FN_GLOCK_SCHEDULE_FOR_RECLAIM)
	struct gfs2_sbd *sdp = gl->gl_sbd;

	spin_lock(&sdp->sd_reclaim_lock);
	if (list_empty(&gl->gl_reclaim)) {
		glock_hold(gl);
		list_add(&gl->gl_reclaim, &sdp->sd_reclaim_list);
		atomic_inc(&sdp->sd_reclaim_count);
	}
	spin_unlock(&sdp->sd_reclaim_lock);

	wake_up(&sdp->sd_reclaim_wq);

	RET(G2FN_GLOCK_SCHEDULE_FOR_RECLAIM);
}

/**
 * gfs2_reclaim_glock - process the next glock on the filesystem's reclaim list
 * @sdp: the filesystem
 *
 * Called from gfs2_glockd() glock reclaim daemon, or when promoting a
 *   (different) glock and we notice that there are a lot of glocks in the
 *   reclaim list.
 *
 * Remove glock from filesystem's reclaim list, update reclaim statistics.
 * If no holders (might have gotten added since glock was placed on reclaim
 *   list):
 *   --  Destroy any now-unused inode protected by glock
 *         (and release hold on iopen glock).
 *   --  Ask for demote to UNLOCKED to enable removal of glock from glock cache.
 *
 * If no further interest in glock struct, remove it from glock cache, and
 *   free it from memory.  (During normal operation, this is the only place
 *   that this is done).
 *
 * Glock-type-specific considerations for permission to demote are handled
 *   in demote_ok().  This includes how long to retain a glock in cache after it
 *   is no longer held by any process.
 *
 * Be sure and drop the reference acquired by gfs2_glock_schedule_for_reclaim().
 *
 */

void
gfs2_reclaim_glock(struct gfs2_sbd *sdp)
{
	ENTER(G2FN_RECLAIM_GLOCK)
	struct gfs2_glock *gl;
	struct gfs2_gl_hash_bucket *bucket;

	spin_lock(&sdp->sd_reclaim_lock);

	/* Nothing to reclaim?  Done! */
	if (list_empty(&sdp->sd_reclaim_list)) {
		spin_unlock(&sdp->sd_reclaim_lock);
		RET(G2FN_RECLAIM_GLOCK);
	}

	/* Remove next victim from reclaim list */
	gl = list_entry(sdp->sd_reclaim_list.next,
			struct gfs2_glock, gl_reclaim);
	list_del_init(&gl->gl_reclaim);

	spin_unlock(&sdp->sd_reclaim_lock);

	atomic_dec(&sdp->sd_reclaim_count);
	atomic_inc(&sdp->sd_reclaimed);

	if (gfs2_glmutex_trylock(gl)) {
		if (gl->gl_ops == &gfs2_inode_glops) {
			struct gfs2_inode *ip = get_gl2ip(gl);
			if (ip && !atomic_read(&ip->i_count))
				gfs2_inode_destroy(ip);
		}
		if (queue_empty(gl, &gl->gl_holders) &&
		    gl->gl_state != LM_ST_UNLOCKED &&
		    demote_ok(gl))
			handle_callback(gl, LM_ST_UNLOCKED);
		gfs2_glmutex_unlock(gl);
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

	RET(G2FN_RECLAIM_GLOCK);
}

/**
 * examine_bucket - Call a function for glock in a hash bucket
 * @examiner: the function 
 * @sdp: the filesystem
 * @bucket: the bucket
 *
 * Returns: TRUE if the bucket has entries
 */

static int
examine_bucket(glock_examiner examiner,
	       struct gfs2_sbd *sdp, struct gfs2_gl_hash_bucket *bucket)
{
	ENTER(G2FN_EXAMINE_BUCKET)
	struct glock_plug plug;
	struct list_head *tmp;
	struct gfs2_glock *gl;
	int entries;

	/* Add "plug" to end of bucket list, work back up list from there */
	memset(&plug.gl_flags, 0, sizeof(unsigned long));
	set_bit(GLF_PLUG, &plug.gl_flags);

	write_lock(&bucket->hb_lock);
	list_add(&plug.gl_list, &bucket->hb_list);
	write_unlock(&bucket->hb_lock);

	/* Look at each bucket entry */
	for (;;) {
		write_lock(&bucket->hb_lock);

		/* Work back up list from plug */
		for (;;) {
			tmp = plug.gl_list.next;

			/* Top of list; we're done */
			if (tmp == &bucket->hb_list) {
				list_del(&plug.gl_list);
				entries = !list_empty(&bucket->hb_list);
				write_unlock(&bucket->hb_lock);
				RETURN(G2FN_EXAMINE_BUCKET, entries);
			}
			gl = list_entry(tmp, struct gfs2_glock, gl_list);

			/* Move plug up list */
			list_move(&plug.gl_list, &gl->gl_list);

			if (test_bit(GLF_PLUG, &gl->gl_flags))
				continue;

			/* glock_hold; examiner must glock_put() */
			atomic_inc(&gl->gl_count);

			break;
		}

		write_unlock(&bucket->hb_lock);

		examiner(gl);
	}
}

/**
 * scan_glock - look at a glock and see if we can reclaim it
 * @gl: the glock to look at
 *
 * Called via examine_bucket() when trying to release glocks from glock cache,
 *   during normal operation (i.e. not unmount time).
 * 
 * Place glock on filesystem's reclaim list if, on this node:
 * --  No process is manipulating glock struct, and
 * --  No current holders, and either:
 *     --  GFS2 incore inode, protected by glock, is no longer in use, or
 *     --  Glock-type-specific demote_ok glops gives permission
 *
 * Be sure and drop the reference acquired by examine_bucket().
 *
 */

static void
scan_glock(struct gfs2_glock *gl)
{
	ENTER(G2FN_SCAN_GLOCK)

	if (gfs2_glmutex_trylock(gl)) {
		if (gl->gl_ops == &gfs2_inode_glops) {
			struct gfs2_inode *ip = get_gl2ip(gl);
			if (ip && !atomic_read(&ip->i_count))
				goto out_schedule;
		}
		if (queue_empty(gl, &gl->gl_holders) &&
		    gl->gl_state != LM_ST_UNLOCKED &&
		    demote_ok(gl))
			goto out_schedule;

		gfs2_glmutex_unlock(gl);
	}

	glock_put(gl);

	RET(G2FN_SCAN_GLOCK);

 out_schedule:
	gfs2_glmutex_unlock(gl);
	gfs2_glock_schedule_for_reclaim(gl);
	glock_put(gl);
	RET(G2FN_SCAN_GLOCK);
}

/**
 * gfs2_scand_internal - Look for glocks and inodes to toss from memory
 * @sdp: the filesystem
 *
 * Invokes scan_glock() for each glock in each cache bucket.
 *
 * Steps of reclaiming a glock:
 * --  scan_glock() places eligible glocks on filesystem's reclaim list.
 * --  gfs2_reclaim_glock() processes list members, attaches demotion requests
 *     to wait queues of glocks still locked at inter-node scope.
 * --  Demote to UNLOCKED state (if not already unlocked).
 * --  gfs2_reclaim_lock() cleans up glock structure.
 */

void
gfs2_scand_internal(struct gfs2_sbd *sdp)
{
	ENTER(G2FN_SCAND_INTERNAL)
	unsigned int x;

	for (x = 0; x < GFS2_GL_HASH_SIZE; x++) {
		examine_bucket(scan_glock, sdp, &sdp->sd_gl_hash[x]);
		cond_resched();
	}

	RET(G2FN_SCAND_INTERNAL);
}

/**
 * clear_glock - look at a glock and see if we can free it from glock cache
 * @gl: the glock to look at
 *
 * Called via examine_bucket() when unmounting the filesystem, or
 *   when inter-node lock manager requests DROPLOCKS because it is running
 *   out of capacity.
 *
 * Similar to gfs2_reclaim_glock(), except does *not*:
 *   --  Consult demote_ok() for permission
 *   --  Increment sdp->sd_reclaimed statistic
 *
 * Be sure and drop the reference acquired by examine_bucket().
 *
 */

static void
clear_glock(struct gfs2_glock *gl)
{
	ENTER(G2FN_CLEAR_GLOCK)
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct gfs2_gl_hash_bucket *bucket = gl->gl_bucket;

	spin_lock(&sdp->sd_reclaim_lock);
	if (!list_empty(&gl->gl_reclaim)) {
		list_del_init(&gl->gl_reclaim);
		atomic_dec(&sdp->sd_reclaim_count);
		glock_put(gl);
	}
	spin_unlock(&sdp->sd_reclaim_lock);

	if (gfs2_glmutex_trylock(gl)) {
		if (gl->gl_ops == &gfs2_inode_glops) {
			struct gfs2_inode *ip = get_gl2ip(gl);
			if (ip && !atomic_read(&ip->i_count))
				gfs2_inode_destroy(ip);
		}
		if (queue_empty(gl, &gl->gl_holders) &&
		    gl->gl_state != LM_ST_UNLOCKED)
			handle_callback(gl, LM_ST_UNLOCKED);

		gfs2_glmutex_unlock(gl);
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

	RET(G2FN_CLEAR_GLOCK);
}

/**
 * gfs2_gl_hash_clear - Empty out the glock hash table
 * @sdp: the filesystem
 * @wait: wait until it's all gone
 *
 * Called when unmounting the filesystem, or when inter-node lock manager
 *   requests DROPLOCKS because it is running out of capacity.
 */

void
gfs2_gl_hash_clear(struct gfs2_sbd *sdp, int wait)
{
	ENTER(G2FN_GL_HASH_CLEAR)
	unsigned long t;
	unsigned int x;
	int cont;

	t = jiffies;

	for (;;) {
		cont = FALSE;

		for (x = 0; x < GFS2_GL_HASH_SIZE; x++)
			if (examine_bucket(clear_glock, sdp, &sdp->sd_gl_hash[x]))
				cont = TRUE;

		if (!wait || !cont)
			break;

		if (time_after_eq(jiffies, t + gfs2_tune_get(sdp, gt_stall_secs) * HZ)) {
			printk("GFS2: fsid=%s: Unmount seems to be stalled. Dumping lock state...\n",
			       sdp->sd_fsname);
			gfs2_dump_lockstate(sdp, NULL);
			t = jiffies;
		}

		invalidate_inodes(sdp->sd_vfs);
		yield();
	}

	RET(G2FN_GL_HASH_CLEAR);
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
dump_holder(char *str, struct gfs2_holder *gh,
	    char *buf, unsigned int size, unsigned int *count)
{
	ENTER(G2FN_DUMP_HOLDER)
	unsigned int x;
	int error = -ENOBUFS;

	gfs2_printf("  %s\n", str);
	gfs2_printf("    owner = %ld\n",
		   (gh->gh_owner) ? (long)gh->gh_owner->pid : -1);
	gfs2_printf("    gh_state = %u\n", gh->gh_state);
	gfs2_printf("    gh_flags =");
	for (x = 0; x < 32; x++)
		if (gh->gh_flags & (1 << x))
			gfs2_printf(" %u", x);
	gfs2_printf(" \n");
	gfs2_printf("    error = %d\n", gh->gh_error);
	gfs2_printf("    gh_iflags =");
	for (x = 0; x < 32; x++)
		if (test_bit(x, &gh->gh_iflags))
			gfs2_printf(" %u", x);
	gfs2_printf(" \n");

	error = 0;

 out:
	RETURN(G2FN_DUMP_HOLDER, error);
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
dump_inode(struct gfs2_inode *ip,
	   char *buf, unsigned int size, unsigned int *count)
{
	ENTER(G2FN_DUMP_INODE)
	unsigned int x;
	int error = -ENOBUFS;

	gfs2_printf("  Inode:\n");
	gfs2_printf("    num = %"PRIu64"/%"PRIu64"\n",
		    ip->i_num.no_formal_ino, ip->i_num.no_addr);
	gfs2_printf("    type = %u\n", IF2DT(ip->i_di.di_mode));
	gfs2_printf("    i_count = %d\n", atomic_read(&ip->i_count));
	gfs2_printf("    i_flags =");
	for (x = 0; x < 32; x++)
		if (test_bit(x, &ip->i_flags))
			gfs2_printf(" %u", x);
	gfs2_printf(" \n");
	gfs2_printf("    vnode = %s\n", (ip->i_vnode) ? "yes" : "no");

	error = 0;

 out:
	RETURN(G2FN_DUMP_INODE, error);
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
dump_glock(struct gfs2_glock *gl,
	   char *buf, unsigned int size, unsigned int *count)
{
	ENTER(G2FN_DUMP_GLOCK)
	struct list_head *head, *tmp;
	struct gfs2_holder *gh;
	unsigned int x;
	int error = -ENOBUFS;

	spin_lock(&gl->gl_spin);

	gfs2_printf("Glock (%u, %"PRIu64")\n",
		    gl->gl_name.ln_type,
		    gl->gl_name.ln_number);
	gfs2_printf("  gl_flags =");
	for (x = 0; x < 32; x++)
		if (test_bit(x, &gl->gl_flags))
			gfs2_printf(" %u", x);
	gfs2_printf(" \n");
	gfs2_printf("  gl_count = %d\n", atomic_read(&gl->gl_count));
	gfs2_printf("  gl_state = %u\n", gl->gl_state);
	gfs2_printf("  req_gh = %s\n", (gl->gl_req_gh) ? "yes" : "no");
	gfs2_printf("  req_bh = %s\n", (gl->gl_req_bh) ? "yes" : "no");
	gfs2_printf("  lvb_count = %d\n", atomic_read(&gl->gl_lvb_count));
	gfs2_printf("  object = %s\n", (gl->gl_object) ? "yes" : "no");
	gfs2_printf("  le = %s\n",
		   (list_empty(&gl->gl_le.le_list)) ? "no" : "yes");
	gfs2_printf("  reclaim = %s\n",
		    (list_empty(&gl->gl_reclaim)) ? "no" : "yes");
	if (gl->gl_aspace)
		gfs2_printf("  aspace = %lu\n",
			    gl->gl_aspace->i_mapping->nrpages);
	else
		gfs2_printf("  aspace = no\n");
	gfs2_printf("  ail = %d\n", atomic_read(&gl->gl_ail_count));
	if (gl->gl_req_gh) {
		error = dump_holder("Request", gl->gl_req_gh, buf, size, count);
		if (error)
			goto out;
	}
	for (head = &gl->gl_holders, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		gh = list_entry(tmp, struct gfs2_holder, gh_list);
		error = dump_holder("Holder", gh, buf, size, count);
		if (error)
			goto out;
	}
	for (head = &gl->gl_waiters1, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		gh = list_entry(tmp, struct gfs2_holder, gh_list);
		error = dump_holder("Waiter1", gh, buf, size, count);
		if (error)
			goto out;
	}
	for (head = &gl->gl_waiters2, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		gh = list_entry(tmp, struct gfs2_holder, gh_list);
		error = dump_holder("Waiter2", gh, buf, size, count);
		if (error)
			goto out;
	}
	for (head = &gl->gl_waiters3, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		gh = list_entry(tmp, struct gfs2_holder, gh_list);
		error = dump_holder("Waiter3", gh, buf, size, count);
		if (error)
			goto out;
	}
	if (gl->gl_ops == &gfs2_inode_glops && get_gl2ip(gl)) {
		if (!test_bit(GLF_LOCK, &gl->gl_flags) &&
		    list_empty(&gl->gl_holders)) {
			error = dump_inode(get_gl2ip(gl), buf, size, count);
			if (error)
				goto out;
		} else {
			error = -ENOBUFS;
			gfs2_printf("  Inode: busy\n");
		}
	}

	error = 0;

 out:
	spin_unlock(&gl->gl_spin);

	RETURN(G2FN_DUMP_GLOCK, error);
}

/**
 * gfs2_dump_lockstate - print out the current lockstate
 * @sdp: the filesystem
 * @ub: the buffer to copy the information into
 *
 * If @ub is NULL, dump the lockstate to the console.
 *
 */

int
gfs2_dump_lockstate(struct gfs2_sbd *sdp, struct gfs2_user_buffer *ub)
{
	ENTER(G2FN_DUMP_LOCKSTATE)
	struct gfs2_gl_hash_bucket *bucket;
	struct list_head *tmp, *head;
	struct gfs2_glock *gl;
	char *buf = NULL;
	unsigned int size = gfs2_tune_get(sdp, gt_lockdump_size);
	unsigned int x, count;
	int error = 0;

	if (ub) {
		buf = kmalloc(size, GFP_KERNEL);
		if (!buf)
			RETURN(G2FN_DUMP_LOCKSTATE, -ENOMEM);
	}

	for (x = 0; x < GFS2_GL_HASH_SIZE; x++) {
		bucket = &sdp->sd_gl_hash[x];
		count = 0;

		read_lock(&bucket->hb_lock);

		for (head = &bucket->hb_list, tmp = head->next;
		     tmp != head;
		     tmp = tmp->next) {
			gl = list_entry(tmp, struct gfs2_glock, gl_list);

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

	RETURN(G2FN_DUMP_LOCKSTATE, error);
}
