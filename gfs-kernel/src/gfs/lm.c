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
#include "lm.h"
#include "super.h"

/**
 * lm_cb -
 * @fsdata:
 * @type:
 * @data:
 *
 */

static void
lm_cb(lm_fsdata_t *fsdata, unsigned int type, void *data)
{
	if (type == LM_CB_ASYNC)
		atomic_dec(&((struct gfs_sbd *)fsdata)->sd_lm_outstanding);
	gfs_glock_cb(fsdata, type, data);
}

/**
 * gfs_lm_mount - mount a locking protocol
 * @sdp: the filesystem
 * @args: mount arguements
 * @silent: if TRUE, don't complain if the FS isn't a GFS fs
 *
 * Returns: errno
 */

int
gfs_lm_mount(struct gfs_sbd *sdp, int silent)
{
	struct gfs_sb *sb = NULL;
	char *proto, *table;
	int error;

	proto = sdp->sd_args.ar_lockproto;
	table = sdp->sd_args.ar_locktable;

	/*  Try to autodetect  */

	if (!proto[0] || !table[0]) {
		struct buffer_head *bh = sb_getblk(sdp->sd_vfs,
						   GFS_SB_ADDR >> sdp->sd_fsb2bb_shift);
		lock_buffer(bh);
		clear_buffer_dirty(bh);
		clear_buffer_uptodate(bh);
		unlock_buffer(bh);
		ll_rw_block(READ, 1, &bh);
		wait_on_buffer(bh);

		if (!buffer_uptodate(bh)) {
			brelse(bh);
			return -EIO;
		}

		sb = kmalloc(sizeof(struct gfs_sb), GFP_KERNEL);
		if (!sb) {
			brelse(bh);
			return -ENOMEM;
		}
		gfs_sb_in(sb, bh->b_data);
		brelse(bh);

		error = gfs_check_sb(sdp, sb, silent);
		if (error)
			goto out;

		if (!proto[0])
			proto = sb->sb_lockproto;

		if (!table[0])
			table = sb->sb_locktable;
	}

	printk("GFS: Trying to join cluster \"%s\", \"%s\"\n",
	       proto, table);

	atomic_inc(&sdp->sd_lm_outstanding);
	error = lm_mount(proto, table, sdp->sd_args.ar_hostdata,
			 lm_cb, sdp,
			 GFS_MIN_LVB_SIZE, &sdp->sd_lockstruct);
	atomic_dec(&sdp->sd_lm_outstanding);
	if (error) {
		printk("GFS: can't mount proto = %s, table = %s, hostdata = %s\n",
		     proto, table, sdp->sd_args.ar_hostdata);
		goto out;
	}

	if (gfs_assert_warn(sdp, sdp->sd_lockstruct.ls_lockspace) ||
	    gfs_assert_warn(sdp, sdp->sd_lockstruct.ls_ops) ||
	    gfs_assert_warn(sdp, sdp->sd_lockstruct.ls_lvb_size >= GFS_MIN_LVB_SIZE)) {
		lm_unmount(&sdp->sd_lockstruct);
		goto out;
	}

	snprintf(sdp->sd_fsname, 256, "%s.%u",
		 (*table) ? table : sdp->sd_vfs->s_id,
		 sdp->sd_lockstruct.ls_jid);

	printk("GFS: fsid=%s: Joined cluster. Now mounting FS...\n",
	       sdp->sd_fsname);

 out:
	if (sb)
		kfree(sb);

	return error;
}

/**
 * gfs_lm_others_may_mount -
 * @sdp:
 *
 */

void
gfs_lm_others_may_mount(struct gfs_sbd *sdp)
{
	atomic_inc(&sdp->sd_lm_outstanding);
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_others_may_mount(sdp->sd_lockstruct.ls_lockspace);
	atomic_dec(&sdp->sd_lm_outstanding);
}

/**
 * gfs_lm_unmount - Unmount lock protocol
 * @sdp: The GFS superblock
 *
 */

void
gfs_lm_unmount(struct gfs_sbd *sdp)
{
	atomic_inc(&sdp->sd_lm_outstanding);
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		lm_unmount(&sdp->sd_lockstruct);
	atomic_dec(&sdp->sd_lm_outstanding);
}

/**
 * gfs_lm_withdraw -
 * @sdp:
 *
 * Returns: -1 if this call withdrew the machine,
 *          0 if it was already withdrawn
 */

int
gfs_lm_withdraw(struct gfs_sbd *sdp, char *fmt, ...)
{
	va_list args;

	if (test_and_set_bit(SDF_SHUTDOWN, &sdp->sd_flags))
		return 0;

	va_start(args, fmt);
	vprintk(fmt, args);
	va_end(args);

	printk("GFS: fsid=%s: about to withdraw from the cluster\n",
	       sdp->sd_fsname);
	printk("GFS: fsid=%s: waiting for outstanding I/O\n",
	       sdp->sd_fsname);

	while (atomic_read(&sdp->sd_bio_outstanding)) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ / 10);
	}
/*
 *  I'm still not sure if we want to do this.  If we do, we need to
 *  add code to cancel outstanding requests.
 *
	while (atomic_read(&sdp->sd_lm_outstanding)) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ / 10);
	}
*/

	printk("GFS: fsid=%s: telling LM to withdraw\n",
	       sdp->sd_fsname);

	atomic_inc(&sdp->sd_lm_outstanding);
	lm_withdraw(&sdp->sd_lockstruct);
	atomic_dec(&sdp->sd_lm_outstanding);

	printk("GFS: fsid=%s: withdrawn\n",
	       sdp->sd_fsname);

	return -1;
}

/**
 * gfs_lm_get_lock -
 * @sdp:
 * @name:
 * @lockp:
 *
 * Returns: errno
 */

int
gfs_lm_get_lock(struct gfs_sbd *sdp,
		struct lm_lockname *name, lm_lock_t **lockp)
{
	int error;

	atomic_inc(&sdp->sd_lm_outstanding);
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;
	else
		error = sdp->sd_lockstruct.ls_ops->lm_get_lock(sdp->sd_lockstruct.ls_lockspace,
							       name, lockp);
	atomic_dec(&sdp->sd_lm_outstanding);

	return error;
}

/**
 * gfs_lm_put_lock -
 * @sdp:
 * @lock:
 *
 */

void
gfs_lm_put_lock(struct gfs_sbd *sdp, lm_lock_t *lock)
{
	atomic_inc(&sdp->sd_lm_outstanding);
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_put_lock(lock);
	atomic_dec(&sdp->sd_lm_outstanding);
}

/**
 * gfs_lm_lock -
 * @sdp:
 * @lock:
 * @cur_state:
 * @req_state:
 * @flags:
 *
 * Returns:
 */

unsigned int
gfs_lm_lock(struct gfs_sbd *sdp, lm_lock_t *lock,
	    unsigned int cur_state, unsigned int req_state,
	    unsigned int flags)
{
	int ret;

	atomic_inc(&sdp->sd_lm_outstanding);
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		ret = 0;
	else
		ret = sdp->sd_lockstruct.ls_ops->lm_lock(lock,
							 cur_state,
							 req_state, flags);
	if (ret != LM_OUT_ASYNC)
		atomic_dec(&sdp->sd_lm_outstanding);

	return ret;
}

/**
 * gfs_lm_lock -
 * @sdp:
 * @lock:
 * @cur_state:
 *
 * Returns:
 */

unsigned int
gfs_lm_unlock(struct gfs_sbd *sdp, lm_lock_t *lock,
	      unsigned int cur_state)
{
	int ret;

	atomic_inc(&sdp->sd_lm_outstanding);
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		ret = 0;
	else
		ret =  sdp->sd_lockstruct.ls_ops->lm_unlock(lock, cur_state);
	if (ret != LM_OUT_ASYNC)
		atomic_dec(&sdp->sd_lm_outstanding);

	return ret;
}

/**
 * gfs_lm_cancel -
 * @sdp:
 * @lock:
 *
 */

void
gfs_lm_cancel(struct gfs_sbd *sdp, lm_lock_t *lock)
{
	atomic_inc(&sdp->sd_lm_outstanding);
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_cancel(lock);
	atomic_dec(&sdp->sd_lm_outstanding);
}

/**
 * gfs_lm_hold_lvb -
 * @sdp:
 * @lock:
 * @lvbp:
 *
 * Returns: errno
 */

int
gfs_lm_hold_lvb(struct gfs_sbd *sdp, lm_lock_t *lock, char **lvbp)
{
	int error;

	atomic_inc(&sdp->sd_lm_outstanding);
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;
	else
		error = sdp->sd_lockstruct.ls_ops->lm_hold_lvb(lock, lvbp);
	atomic_dec(&sdp->sd_lm_outstanding);

	return error;
}

/**
 * gfs_lm_unhold_lvb -
 * @sdp:
 * @lock:
 * @lvb:
 *
 */

void
gfs_lm_unhold_lvb(struct gfs_sbd *sdp, lm_lock_t *lock, char *lvb)
{
	atomic_inc(&sdp->sd_lm_outstanding);
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_unhold_lvb(lock, lvb);
	atomic_dec(&sdp->sd_lm_outstanding);
}

/**
 * gfs_lm_sync_lvb -
 * @sdp:
 * @lock:
 * @lvb:
 *
 */

void
gfs_lm_sync_lvb(struct gfs_sbd *sdp, lm_lock_t *lock, char *lvb)
{
	atomic_inc(&sdp->sd_lm_outstanding);
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_sync_lvb(lock, lvb);
	atomic_dec(&sdp->sd_lm_outstanding);
}

/**
 * gfs_lm_plock_get -
 * @sdp:
 * @name:
 * @file:
 * @fl:
 *
 * Returns: errno
 */

int
gfs_lm_plock_get(struct gfs_sbd *sdp,
		 struct lm_lockname *name,
		 struct file *file, struct file_lock *fl)
{
	int error;

	atomic_inc(&sdp->sd_lm_outstanding);
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;
	else
		error = sdp->sd_lockstruct.ls_ops->lm_plock_get(
			sdp->sd_lockstruct.ls_lockspace,
			name, file, fl);
	atomic_dec(&sdp->sd_lm_outstanding);

	return error;
}

/**
 * gfs_lm_plock -
 * @sdp:
 * @name:
 * @file:
 * @cmd:
 * @fl:
 *
 * Returns: errno
 */

int
gfs_lm_plock(struct gfs_sbd *sdp,
	     struct lm_lockname *name,
	     struct file *file, int cmd, struct file_lock *fl)
{
	int error;

	atomic_inc(&sdp->sd_lm_outstanding);
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;
	else
		error = sdp->sd_lockstruct.ls_ops->lm_plock(
			sdp->sd_lockstruct.ls_lockspace,
			name, file, cmd, fl);
	atomic_dec(&sdp->sd_lm_outstanding);

	return error;
}

/**
 * gfs_lm_punlock -
 * @sdp:
 * @name:
 * @file:
 * @fl:
 *
 * Returns: errno
 */

int
gfs_lm_punlock(struct gfs_sbd *sdp,
	       struct lm_lockname *name,
	       struct file *file, struct file_lock *fl)
{
	int error;

	atomic_inc(&sdp->sd_lm_outstanding);
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;
	else
		error = sdp->sd_lockstruct.ls_ops->lm_punlock(
			sdp->sd_lockstruct.ls_lockspace,
			name, file, fl);
	atomic_dec(&sdp->sd_lm_outstanding);

	return error;
}

/**
 * gfs_lm_recovery_done -
 * @sdp:
 * @jid:
 * @message:
 *
 */

void
gfs_lm_recovery_done(struct gfs_sbd *sdp,
		     unsigned int jid, unsigned int message)
{
	atomic_inc(&sdp->sd_lm_outstanding);
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_recovery_done(sdp->sd_lockstruct.ls_lockspace,
							    jid,
							    message);
	atomic_dec(&sdp->sd_lm_outstanding);
}


