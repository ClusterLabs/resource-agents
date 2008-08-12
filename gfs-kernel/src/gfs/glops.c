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
#include "glops.h"
#include "inode.h"
#include "log.h"
#include "page.h"
#include "recovery.h"
#include "rgrp.h"

/**
 * meta_go_sync - sync out the metadata for this glock
 * @gl: the glock
 * @flags: DIO_*
 *
 * Used for meta and rgrp glocks.
 *
 * Called when demoting (gfs_glock_xmote_th()) or unlocking
 * (gfs_glock_drop_th() an EX glock at inter-node scope.  We must flush
 * to disk all dirty buffers/pages relating to this glock, and must not
 * not return to caller to demote/unlock the glock until I/O is complete.
 *
 * This is *not* called from gfs_glock_dq(), because GL_SYNC flag is not
 * currently used for anything but inode glocks.
 */

static void
meta_go_sync(struct gfs_glock *gl, int flags)
{
	if (!(flags & DIO_METADATA))
		return;

	if (test_bit(GLF_DIRTY, &gl->gl_flags)) {
		gfs_log_flush_glock(gl);
		gfs_sync_buf(gl, flags | DIO_START | DIO_WAIT | DIO_CHECK);
	}

	/* We've synced everything, clear SYNC request and DIRTY flags */
	clear_bit(GLF_DIRTY, &gl->gl_flags);
	clear_bit(GLF_SYNC, &gl->gl_flags);
}

/**
 * meta_go_inval - invalidate the metadata for this glock
 * @gl: the glock
 * @flags: 
 *
 */

static void
meta_go_inval(struct gfs_glock *gl, int flags)
{
	if (!(flags & DIO_METADATA))
		return;

	gfs_inval_buf(gl);
	gl->gl_vn++;
}

/**
 * meta_go_demote_ok - Check to see if it's ok to unlock a meta glock
 * @gl: the glock
 *
 * Returns: TRUE if we have no cached data; ok to demote meta glock
 *
 * Called when trying to dump (reclaim) a glock from the glock cache, after
 *   determining that there is currently no holder on this node for this glock,
 *   and before placing LM_ST_UNLOCKED request on glock's wait-for-demote queue.
 * Note that callbacks from other nodes that need a lock do *not*
 *   seek permission from this function before requesting a demote.
 *   Nor do glocks obtained with the following flags (see demote_ok()):
 *   --  GL_NOCACHE:  gets unlocked (and not cached) immediately after use
 *   --  GLF_STICKY:  equivalent to always getting "FALSE" from this function
 *   --  GLF_PREFETCH:  uses its own timeout
 *
 * For glocks that protect on-disk data (meta, inode, and rgrp glocks), disk
 *   accesses are slow, while lock manipulation is usually fast.  Releasing
 *   a lock means that we:
 *   --  Must sync memory-cached write data to disk immediately, before another
 *       node can be granted the lock (at which point that node must read the
 *       data from disk).
 *   --  Must invalidate memory-cached data that we had read from or written
 *       to disk.  Another node can change it if we don't have a lock, so it's
 *       now useless to us.
 *
 * Then, if we re-acquire the lock again in the future, we:
 *   --  Must (re-)read (perhaps unchanged) data from disk into memory.
 *
 * All of these are painful, so it pays to retain a glock in our glock cache
 *   as long as we have cached data (even though we have no active holders
 *   for this lock on this node currently), unless/until another node needs
 *   to change it.  This allows Linux block I/O to sync write data to disk in
 *   a "lazy" way, rather than forcing an immediate sync (and resultant WAIT),
 *   and retains current data in memory as long as possible.
 *
 * This also helps GFS respond to memory pressure.  There is no mechanism for
 *   the Linux virtual memory manager to directly call into GFS to ask it to
 *   drop locks.  So, we take a hint from what the VM does to the page cache.
 *   When that cache is trimmed (and we see no more pages relating to this
 *   glock), we trim the glock cache as well, by releasing this lock.
 */

static int
meta_go_demote_ok(struct gfs_glock *gl)
{
	return (gl->gl_aspace->i_mapping->nrpages) ? FALSE : TRUE;
}

/**
 * inode_go_xmote_th - promote/demote (but don't unlock) an inode glock
 * @gl: the glock
 * @state: the requested state
 * @flags: the flags passed into gfs_glock()
 *
 * Acquire a new glock, or change an already-acquired glock to
 *   more/less restrictive state (other than LM_ST_UNLOCKED).
 */

static void
inode_go_xmote_th(struct gfs_glock *gl, unsigned int state, int flags)
{
	if (gl->gl_state != LM_ST_UNLOCKED)
		gfs_inval_pte(gl);
	gfs_glock_xmote_th(gl, state, flags);
}

/**
 * inode_go_xmote_bh - After promoting/demoting (but not unlocking)
 *      an inode glock
 * @gl: the glock
 *
 * FIXME: This will be really broken when (no_formal_ino != no_addr)
 *        and gl_name.ln_number no longer refers to the dinode block #.
 *
 * If we've just acquired the inter-node lock for an inode,
 *   read the dinode block from disk (but don't wait for I/O completion).
 * Exceptions (don't read if):
 *    Glock state is UNLOCKED.
 *    Glock's requesting holder's GL_SKIP flag is set.
 */

static void
inode_go_xmote_bh(struct gfs_glock *gl)
{
	struct gfs_holder *gh = gl->gl_req_gh;
	struct buffer_head *bh;
	int error;

	if (gl->gl_state != LM_ST_UNLOCKED &&
	    (!gh || !(gh->gh_flags & GL_SKIP))) {
		error = gfs_dread(gl, gl->gl_name.ln_number, DIO_START, &bh);
		if (!error)
			brelse(bh);
	}
}

/**
 * inode_go_drop_th - unlock an inode glock
 * @gl: the glock
 *
 * Invoked from rq_demote().
 * Another node needs the lock in EXCLUSIVE mode, or lock (unused for too long)
 *   is being purged from our node's glock cache; we're dropping lock.
 */

static void
inode_go_drop_th(struct gfs_glock *gl)
{
	gfs_inval_pte(gl);
	gfs_glock_drop_th(gl);
}

/**
 * inode_go_sync - Sync the dirty data and/or metadata for an inode glock
 * @gl: the glock protecting the inode
 * @flags: DIO_METADATA -- sync inode's metadata
 *         DIO_DATA     -- sync inode's data
 *         DIO_INVISIBLE --  don't clear glock's DIRTY flag when done
 *
 * If DIO_INVISIBLE:
 *   1) Called from gfs_glock_dq(), when releasing the last holder for an EX
 *   glock (but glock is still in our glock cache in EX state, and might
 *   stay there for a few minutes).  Holder had GL_SYNC flag set, asking
 *   for early sync (i.e. now, instead of later when we release the EX at
 *   inter-node scope).  GL_SYNC is currently used only for inodes in
 *   special cases, so inode is the only type of glock for which
 *   DIO_INVISIBLE would apply.
 *   2) Called from depend_sync_one() to sync deallocated inode metadata
 *   before it can be reallocated by another process or machine.  Since
 *   this call can happen at any time during the lifetime of the
 *   glock, don't clear the sync bit (more data might be dirtied
 *   momentarily).
 * Else (later):
 *   Called when demoting (gfs_glock_xmote_th()) or unlocking
 *   (gfs_glock_drop_th() an EX glock at inter-node scope.  We must flush
 *   to disk all dirty buffers/pages relating to this glock, and must not
 *   return to caller to demote/unlock the glock until I/O is complete.
 *
 * Syncs go in following order:
 *   Start data page writes
 *   Sync metadata to log (wait to complete I/O)
 *   Sync metadata to in-place location (wait to complete I/O)
 *   Wait for data page I/O to complete
 * 
 */

static void
inode_go_sync(struct gfs_glock *gl, int flags)
{
	int meta = (flags & DIO_METADATA);
	int data = (flags & DIO_DATA);

	if (test_bit(GLF_DIRTY, &gl->gl_flags)) {
		if (meta && data) {
			gfs_sync_page(gl, flags | DIO_START);
			gfs_log_flush_glock(gl);
			gfs_sync_buf(gl, flags | DIO_START | DIO_WAIT | DIO_CHECK);
			gfs_sync_page(gl, flags | DIO_WAIT | DIO_CHECK);
		} else if (meta) {
			gfs_log_flush_glock(gl);
			gfs_sync_buf(gl, flags | DIO_START | DIO_WAIT | DIO_CHECK);
		} else if (data)
			gfs_sync_page(gl, flags | DIO_START | DIO_WAIT | DIO_CHECK);
	}

	/* If we've synced everything, clear the SYNC request.
	   If we're doing the final (not early) sync, clear DIRTY */
	if (meta && data) {
		if (!(flags & DIO_INVISIBLE))
			clear_bit(GLF_DIRTY, &gl->gl_flags);
		clear_bit(GLF_SYNC, &gl->gl_flags);
	}
}

/**
 * inode_go_inval - prepare a inode glock to be released
 * @gl: the glock
 * @flags: 
 *
 */

static void
inode_go_inval(struct gfs_glock *gl, int flags)
{
	int meta = (flags & DIO_METADATA);
	int data = (flags & DIO_DATA);

	if (meta) {
		gfs_inval_buf(gl);
		gl->gl_vn++;
	}
	if (data)
		gfs_inval_page(gl);
}

/**
 * inode_go_demote_ok - Check to see if it's ok to unlock an inode glock
 * @gl: the glock
 *
 * See comments for meta_go_demote_ok().
 *
 * While other glock types (meta, rgrp) that protect disk data can be retained
 *   indefinitely, GFS imposes a timeout (overridden when using no_lock lock
 *   module) for inode glocks, even if there is still data in page cache for
 *   this inode.
 *
 * Returns: TRUE if it's ok
 */

static int
inode_go_demote_ok(struct gfs_glock *gl)
{
	struct gfs_sbd *sdp = gl->gl_sbd;
	int demote = FALSE;

	if (!get_gl2ip(gl) && !gl->gl_aspace->i_mapping->nrpages)
		demote = TRUE;
	else if (!sdp->sd_args.ar_localcaching &&
		 time_after_eq(jiffies, gl->gl_stamp + gfs_tune_get(sdp, gt_demote_secs) * HZ))
		demote = TRUE;

	return demote;
}

/**
 * inode_go_lock - operation done after an inode lock is locked by
 *      a first holder on this node
 * @gl: the glock
 * @flags: the flags passed into gfs_glock()
 *
 * Returns: errno
 */

static int
inode_go_lock(struct gfs_glock *gl, int flags)
{
	struct gfs_inode *ip = get_gl2ip(gl);
	int error = 0;

	if (ip && ip->i_vn != gl->gl_vn) {
		error = gfs_copyin_dinode(ip);
		if (!error)
			gfs_inode_attr_in(ip);
	}

	return error;
}

/**
 * inode_go_unlock - operation done when an inode lock is unlocked by
 *     a last holder on this node
 * @gl: the glock
 * @flags: the flags passed into gfs_gunlock()
 *
 */

static void
inode_go_unlock(struct gfs_glock *gl, int flags)
{
	struct gfs_inode *ip = get_gl2ip(gl);

	if (ip && test_bit(GLF_DIRTY, &gl->gl_flags))
		gfs_inode_attr_in(ip);

	if (ip)
		gfs_flush_meta_cache(ip);
}

/**
 * inode_greedy -
 * @gl: the glock
 *
 */

static void
inode_greedy(struct gfs_glock *gl)
{
	struct gfs_sbd *sdp = gl->gl_sbd;
	struct gfs_inode *ip = get_gl2ip(gl);
	unsigned int quantum = gfs_tune_get(sdp, gt_greedy_quantum);
	unsigned int max = gfs_tune_get(sdp, gt_greedy_max);
	unsigned int new_time;

	spin_lock(&ip->i_spin);

	if (time_after(ip->i_last_pfault + quantum, jiffies)) {
		new_time = ip->i_greedy + quantum;
		if (new_time > max)
			new_time = max;
	} else {
		new_time = ip->i_greedy - quantum;
		if (!new_time || new_time > max)
			new_time = 1;
	}

	ip->i_greedy = new_time;

	spin_unlock(&ip->i_spin);

	gfs_inode_put(ip);
}

/**
 * rgrp_go_xmote_th - promote/demote (but don't unlock) a resource group glock
 * @gl: the glock
 * @state: the requested state
 * @flags: the flags passed into gfs_glock()
 *
 * Acquire a new glock, or change an already-acquired glock to
 *   more/less restrictive state (other than LM_ST_UNLOCKED).
 *
 * We're going to lock the lock in SHARED or EXCLUSIVE state, or
 *   demote it from EXCLUSIVE to SHARED (because another node needs it SHARED).
 * When locking, gfs_mhc_zap() and gfs_depend_sync() are basically no-ops;
 *   meta-header cache and dependency lists should be empty.
 *
 */

static void
rgrp_go_xmote_th(struct gfs_glock *gl, unsigned int state, int flags)
{
	struct gfs_rgrpd *rgd = get_gl2rgd(gl);

	gfs_mhc_zap(rgd);
	gfs_depend_sync(rgd);
	gfs_glock_xmote_th(gl, state, flags);
}

/**
 * rgrp_go_drop_th - unlock a resource group glock
 * @gl: the glock
 *
 * Invoked from rq_demote().
 * Another node needs the lock in EXCLUSIVE mode, or lock (unused for too long)
 *   is being purged from our node's glock cache; we're dropping lock.
 */

static void
rgrp_go_drop_th(struct gfs_glock *gl)
{
	struct gfs_rgrpd *rgd = get_gl2rgd(gl);

	gfs_mhc_zap(rgd);
	gfs_depend_sync(rgd);
	gfs_glock_drop_th(gl);
}

/**
 * rgrp_go_demote_ok - Check to see if it's ok to unlock a RG's glock
 * @gl: the glock
 *
 * See comments for meta_go_demote_ok().
 *
 * In addition to Linux page cache, we also check GFS meta-header-cache.
 *
 * Returns: TRUE if it's ok
 */

static int
rgrp_go_demote_ok(struct gfs_glock *gl)
{
	struct gfs_rgrpd *rgd = get_gl2rgd(gl);
	int demote = TRUE;

	if (gl->gl_aspace->i_mapping->nrpages)
		demote = FALSE;
	else if (rgd && !list_empty(&rgd->rd_mhc)) /* Don't bother with lock here */
		demote = FALSE;

	return demote;
}

/**
 * rgrp_go_lock - operation done after an rgrp lock is locked by
 *    a first holder on this node.
 * @gl: the glock
 * @flags: the flags passed into gfs_glock()
 *
 * Returns: errno
 *
 * Read rgrp's header and block allocation bitmaps from disk.
 */

static int
rgrp_go_lock(struct gfs_glock *gl, int flags)
{
	if (flags & GL_SKIP)
		return 0;
	return gfs_rgrp_read(get_gl2rgd(gl));
}

/**
 * rgrp_go_unlock - operation done when an rgrp lock is unlocked by
 *    a last holder on this node.
 * @gl: the glock
 * @flags: the flags passed into gfs_gunlock()
 *
 * Release rgrp's bitmap buffers (read in when lock was first obtained).
 * Make sure rgrp's glock's Lock Value Block has up-to-date block usage stats,
 *   so other nodes can see them.
 */

static void
rgrp_go_unlock(struct gfs_glock *gl, int flags)
{
	struct gfs_rgrpd *rgd = get_gl2rgd(gl);
	if (flags & GL_SKIP)
		return;
	gfs_rgrp_relse(rgd);
	if (test_bit(GLF_DIRTY, &gl->gl_flags))
		gfs_rgrp_lvb_fill(rgd);
}

/**
 * trans_go_xmote_th - promote/demote (but don't unlock) the transaction glock
 * @gl: the glock
 * @state: the requested state
 * @flags: the flags passed into gfs_glock()
 *
 * Acquire a new glock, or change an already-acquired glock to
 *   more/less restrictive state (other than LM_ST_UNLOCKED).
 */

static void
trans_go_xmote_th(struct gfs_glock *gl, unsigned int state, int flags)
{
	struct gfs_sbd *sdp = gl->gl_sbd;

	if (gl->gl_state != LM_ST_UNLOCKED &&
	    test_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags)) {
		gfs_sync_meta(sdp);
		gfs_log_shutdown(sdp);
	}

	gfs_glock_xmote_th(gl, state, flags);
}

/**
 * trans_go_xmote_bh - After promoting/demoting (but not unlocking)
 *       the transaction glock
 * @gl: the glock
 *
 */

static void
trans_go_xmote_bh(struct gfs_glock *gl)
{
	struct gfs_sbd *sdp = gl->gl_sbd;
	struct gfs_glock *j_gl = sdp->sd_journal_gh.gh_gl;
	struct gfs_log_header head;
	int error;

	if (gl->gl_state != LM_ST_UNLOCKED &&
	    test_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags)) {
		j_gl->gl_ops->go_inval(j_gl, DIO_METADATA | DIO_DATA);

		error = gfs_find_jhead(sdp, &sdp->sd_jdesc, j_gl, &head);
		if (error)
			gfs_consist(sdp);
		if (!(head.lh_flags & GFS_LOG_HEAD_UNMOUNT))
			gfs_consist(sdp);

		/*  Initialize some head of the log stuff  */
		if (!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)) {
			sdp->sd_sequence = head.lh_sequence;
			sdp->sd_log_head = head.lh_first + 1;
		}
	}
}

/**
 * trans_go_drop_th - unlock the transaction glock
 * @gl: the glock
 *
 * Invoked from rq_demote().
 * Another node needs the lock in EXCLUSIVE mode to quiesce the filesystem
 *   (for journal replay, etc.).
 *
 * We want to sync the device even with localcaching.  Remember
 * that localcaching journal replay only marks buffers dirty.
 */

static void
trans_go_drop_th(struct gfs_glock *gl)
{
	struct gfs_sbd *sdp = gl->gl_sbd;

	if (test_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags)) {
		gfs_sync_meta(sdp);
		gfs_log_shutdown(sdp);
	}

	gfs_glock_drop_th(gl);
}

/**
 * nondisk_go_demote_ok - Check to see if it's ok to unlock a non-disk glock
 * @gl: the glock
 *
 * See comments for meta_go_demote_ok().
 *
 * We never give up a non-disk glock (unless another node needs it).
 * Non-disk type used for GFS_MOUNT_LOCK, GFS_LIVE_LOCK, GFS_RENAME_LOCK.
 * GFS_MOUNT_LOCK is always requested GL_NOCACHE, however, so it never uses
 *   this function.
 *
 * Returns: TRUE if it's ok
 */

static int
nondisk_go_demote_ok(struct gfs_glock *gl)
{
	return FALSE;
}

/**
 * quota_go_demote_ok - Check to see if it's ok to unlock a quota glock
 * @gl: the glock
 *
 * See comments for meta_go_demote_ok().
 *
 * Returns: TRUE if it's ok
 */

static int
quota_go_demote_ok(struct gfs_glock *gl)
{
	return !atomic_read(&gl->gl_lvb_count);
}

struct gfs_glock_operations gfs_meta_glops = {
      .go_xmote_th = gfs_glock_xmote_th,
      .go_drop_th = gfs_glock_drop_th,
      .go_sync = meta_go_sync,
      .go_inval = meta_go_inval,
      .go_demote_ok = meta_go_demote_ok,
      .go_type = LM_TYPE_META
};

struct gfs_glock_operations gfs_inode_glops = {
      .go_xmote_th = inode_go_xmote_th,
      .go_xmote_bh = inode_go_xmote_bh,
      .go_drop_th = inode_go_drop_th,
      .go_sync = inode_go_sync,
      .go_inval = inode_go_inval,
      .go_demote_ok = inode_go_demote_ok,
      .go_lock = inode_go_lock,
      .go_unlock = inode_go_unlock,
      .go_greedy = inode_greedy,
      .go_type = LM_TYPE_INODE
};

struct gfs_glock_operations gfs_rgrp_glops = {
      .go_xmote_th = rgrp_go_xmote_th,
      .go_drop_th = rgrp_go_drop_th,
      .go_sync = meta_go_sync,
      .go_inval = meta_go_inval,
      .go_demote_ok = rgrp_go_demote_ok,
      .go_lock = rgrp_go_lock,
      .go_unlock = rgrp_go_unlock,
      .go_type = LM_TYPE_RGRP
};

struct gfs_glock_operations gfs_trans_glops = {
      .go_xmote_th = trans_go_xmote_th,
      .go_xmote_bh = trans_go_xmote_bh,
      .go_drop_th = trans_go_drop_th,
      .go_type = LM_TYPE_NONDISK
};

struct gfs_glock_operations gfs_iopen_glops = {
      .go_xmote_th = gfs_glock_xmote_th,
      .go_drop_th = gfs_glock_drop_th,
      .go_callback = gfs_iopen_go_callback,
      .go_type = LM_TYPE_IOPEN
};

struct gfs_glock_operations gfs_flock_glops = {
      .go_xmote_th = gfs_glock_xmote_th,
      .go_drop_th = gfs_glock_drop_th,
      .go_type = LM_TYPE_FLOCK
};

struct gfs_glock_operations gfs_nondisk_glops = {
      .go_xmote_th = gfs_glock_xmote_th,
      .go_drop_th = gfs_glock_drop_th,
      .go_demote_ok = nondisk_go_demote_ok,
      .go_type = LM_TYPE_NONDISK
};

struct gfs_glock_operations gfs_quota_glops = {
      .go_xmote_th = gfs_glock_xmote_th,
      .go_drop_th = gfs_glock_drop_th,
      .go_demote_ok = quota_go_demote_ok,
      .go_type = LM_TYPE_QUOTA
};
