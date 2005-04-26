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
#include "lm.h"
#include "super.h"

/**
 * gfs2_lm_mount - mount a locking protocol
 * @sdp: the filesystem
 * @args: mount arguements
 * @silent: if TRUE, don't complain if the FS isn't a GFS2 fs
 *
 * Returns: errno
 */

int
gfs2_lm_mount(struct gfs2_sbd *sdp, int silent)
{
	ENTER(G2FN_LM_MOUNT)
	struct gfs2_sb *sb = NULL;
	char *proto, *table;
	int flags = 0;
	int error;

	if (sdp->sd_args.ar_spectator)
		flags |= LM_MFLAG_SPECTATOR;

	proto = sdp->sd_args.ar_lockproto;
	table = sdp->sd_args.ar_locktable;

	/*  Try to autodetect  */

	if (!proto[0] || !table[0]) {
		struct buffer_head *bh = sb_getblk(sdp->sd_vfs,
						   GFS2_SB_ADDR >> sdp->sd_fsb2bb_shift);
		lock_buffer(bh);
		clear_buffer_dirty(bh);
		clear_buffer_uptodate(bh);
		unlock_buffer(bh);
		ll_rw_block(READ, 1, &bh);
		wait_on_buffer(bh);

		if (!buffer_uptodate(bh)) {
			brelse(bh);
			RETURN(G2FN_LM_MOUNT, -EIO);
		}

		sb = kmalloc(sizeof(struct gfs2_sb), GFP_KERNEL);
		if (!sb) {
			brelse(bh);
			RETURN(G2FN_LM_MOUNT, -ENOMEM);
		}
		gfs2_sb_in(sb, bh->b_data);
		brelse(bh);

		error = gfs2_check_sb(sdp, sb, silent);
		if (error)
			goto out;

		if (!proto[0])
			proto = sb->sb_lockproto;

		if (!table[0])
			table = sb->sb_locktable;
	}

	printk("GFS2: Trying to join cluster \"%s\", \"%s\"\n",
	       proto, table);

	error = lm_mount(proto, table, sdp->sd_args.ar_hostdata,
			 gfs2_glock_cb, sdp,
			 GFS2_MIN_LVB_SIZE, flags,
			 &sdp->sd_lockstruct);
	if (error) {
		printk("GFS2: can't mount proto = %s, table = %s, hostdata = %s\n",
		     proto, table, sdp->sd_args.ar_hostdata);
		goto out;
	}

	if (gfs2_assert_warn(sdp, sdp->sd_lockstruct.ls_lockspace) ||
	    gfs2_assert_warn(sdp, sdp->sd_lockstruct.ls_ops) ||
	    gfs2_assert_warn(sdp, sdp->sd_lockstruct.ls_lvb_size >= GFS2_MIN_LVB_SIZE)) {
		lm_unmount(&sdp->sd_lockstruct);
		goto out;
	}

	if (sdp->sd_args.ar_spectator)
		snprintf(sdp->sd_fsname, 256, "%s.s",
			 (*table) ? table : sdp->sd_vfs->s_id);
	else
		snprintf(sdp->sd_fsname, 256, "%s.%u",
			 (*table) ? table : sdp->sd_vfs->s_id,
			 sdp->sd_lockstruct.ls_jid);

	printk("GFS2: fsid=%s: Joined cluster. Now mounting FS...\n",
	       sdp->sd_fsname);

	if ((sdp->sd_lockstruct.ls_flags & LM_LSFLAG_LOCAL) &&
	    !sdp->sd_args.ar_ignore_local_fs) {
		/* Force local [p|f]locks */
		sdp->sd_args.ar_localflocks = TRUE;

		/* Force local read ahead and caching */
		sdp->sd_args.ar_localcaching = TRUE;

		/* Allow the machine to oops */
		sdp->sd_args.ar_oopses_ok = TRUE;
	}

 out:
	if (sb)
		kfree(sb);

	RETURN(G2FN_LM_MOUNT, error);
}

/**
 * gfs2_lm_others_may_mount -
 * @sdp:
 *
 */

void
gfs2_lm_others_may_mount(struct gfs2_sbd *sdp)
{
	ENTER(G2FN_LM_OTHERS_MAY_MOUNT)
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_others_may_mount(sdp->sd_lockstruct.ls_lockspace);
	RET(G2FN_LM_OTHERS_MAY_MOUNT);
}

/**
 * gfs2_lm_unmount - Unmount lock protocol
 * @sdp: The GFS2 superblock
 *
 */

void
gfs2_lm_unmount(struct gfs2_sbd *sdp)
{
	ENTER(G2FN_LM_UNMOUNT)
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		lm_unmount(&sdp->sd_lockstruct);
	RET(G2FN_LM_UNMOUNT);
}

/**
 * gfs2_lm_withdraw -
 * @sdp:
 *
 * Returns: -1 if this call withdrew the machine,
 *          0 if it was already withdrawn
 */

int
gfs2_lm_withdraw(struct gfs2_sbd *sdp, char *fmt, ...)
{
	ENTER(G2FN_LM_WITHDRAW)
	va_list args;

	if (test_and_set_bit(SDF_SHUTDOWN, &sdp->sd_flags))
		RETURN(G2FN_LM_WITHDRAW, 0);

	va_start(args, fmt);
	vprintk(fmt, args);
	va_end(args);

	printk("GFS2: fsid=%s: about to withdraw from the cluster\n",
	       sdp->sd_fsname);
	if (sdp->sd_args.ar_debug)
		BUG();

	printk("GFS2: fsid=%s: waiting for outstanding I/O\n",
	       sdp->sd_fsname);
	while (atomic_read(&sdp->sd_bio_outstanding)) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ / 10);
	}

	printk("GFS2: fsid=%s: telling LM to withdraw\n",
	       sdp->sd_fsname);
	lm_withdraw(&sdp->sd_lockstruct);
	printk("GFS2: fsid=%s: withdrawn\n",
	       sdp->sd_fsname);

	RETURN(G2FN_LM_WITHDRAW, -1);
}

/**
 * gfs2_lm_get_lock -
 * @sdp:
 * @name:
 * @lockp:
 *
 * Returns: errno
 */

int
gfs2_lm_get_lock(struct gfs2_sbd *sdp,
		struct lm_lockname *name, lm_lock_t **lockp)
{
	ENTER(G2FN_LM_GET_LOCK)
	int error;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;
	else
		error = sdp->sd_lockstruct.ls_ops->lm_get_lock(sdp->sd_lockstruct.ls_lockspace,
							       name, lockp);
	RETURN(G2FN_LM_GET_LOCK, error);
}

/**
 * gfs2_lm_put_lock -
 * @sdp:
 * @lock:
 *
 */

void
gfs2_lm_put_lock(struct gfs2_sbd *sdp, lm_lock_t *lock)
{
	ENTER(G2FN_LM_PUT_LOCK)
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_put_lock(lock);
	RET(G2FN_LM_PUT_LOCK);
}

/**
 * gfs2_lm_lock -
 * @sdp:
 * @lock:
 * @cur_state:
 * @req_state:
 * @flags:
 *
 * Returns:
 */

unsigned int
gfs2_lm_lock(struct gfs2_sbd *sdp, lm_lock_t *lock,
	    unsigned int cur_state, unsigned int req_state,
	    unsigned int flags)
{
	ENTER(G2FN_LM_LOCK)
	int ret;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		ret = 0;
	else
		ret = sdp->sd_lockstruct.ls_ops->lm_lock(lock,
							 cur_state,
							 req_state, flags);
	RETURN(G2FN_LM_LOCK, ret);
}

/**
 * gfs2_lm_lock -
 * @sdp:
 * @lock:
 * @cur_state:
 *
 * Returns:
 */

unsigned int
gfs2_lm_unlock(struct gfs2_sbd *sdp, lm_lock_t *lock,
	      unsigned int cur_state)
{
	ENTER(G2FN_LM_UNLOCK)
	int ret;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		ret = 0;
	else
		ret =  sdp->sd_lockstruct.ls_ops->lm_unlock(lock, cur_state);
	RETURN(G2FN_LM_UNLOCK, ret);
}

/**
 * gfs2_lm_cancel -
 * @sdp:
 * @lock:
 *
 */

void
gfs2_lm_cancel(struct gfs2_sbd *sdp, lm_lock_t *lock)
{
	ENTER(G2FN_LM_CANCEL)
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_cancel(lock);
	RET(G2FN_LM_CANCEL);
}

/**
 * gfs2_lm_hold_lvb -
 * @sdp:
 * @lock:
 * @lvbp:
 *
 * Returns: errno
 */

int
gfs2_lm_hold_lvb(struct gfs2_sbd *sdp, lm_lock_t *lock, char **lvbp)
{
	ENTER(G2FN_LM_HOLD_LVB)
	int error;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;
	else
		error = sdp->sd_lockstruct.ls_ops->lm_hold_lvb(lock, lvbp);
	RETURN(G2FN_LM_HOLD_LVB, error);
}

/**
 * gfs2_lm_unhold_lvb -
 * @sdp:
 * @lock:
 * @lvb:
 *
 */

void
gfs2_lm_unhold_lvb(struct gfs2_sbd *sdp, lm_lock_t *lock, char *lvb)
{
	ENTER(G2FN_LM_UNHOLD_LVB)
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_unhold_lvb(lock, lvb);
	RET(G2FN_LM_UNHOLD_LVB);
}

/**
 * gfs2_lm_sync_lvb -
 * @sdp:
 * @lock:
 * @lvb:
 *
 */

void
gfs2_lm_sync_lvb(struct gfs2_sbd *sdp, lm_lock_t *lock, char *lvb)
{
	ENTER(G2FN_LM_SYNC_LVB)
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_sync_lvb(lock, lvb);
	RET(G2FN_LM_SYNC_LVB);
}

/**
 * gfs2_lm_plock_get -
 * @sdp:
 * @name:
 * @file:
 * @fl:
 *
 * Returns: errno
 */

int
gfs2_lm_plock_get(struct gfs2_sbd *sdp,
		 struct lm_lockname *name,
		 struct file *file, struct file_lock *fl)
{
	ENTER(G2FN_LM_PLOCK_GET)
	int error;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;
	else
		error = sdp->sd_lockstruct.ls_ops->lm_plock_get(
			sdp->sd_lockstruct.ls_lockspace,
			name, file, fl);
	RETURN(G2FN_LM_PLOCK_GET, error);
}

/**
 * gfs2_lm_plock -
 * @sdp:
 * @name:
 * @file:
 * @cmd:
 * @fl:
 *
 * Returns: errno
 */

int
gfs2_lm_plock(struct gfs2_sbd *sdp,
	     struct lm_lockname *name,
	     struct file *file, int cmd, struct file_lock *fl)
{
	ENTER(G2FN_LM_PLOCK)
	int error;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;
	else
		error = sdp->sd_lockstruct.ls_ops->lm_plock(
			sdp->sd_lockstruct.ls_lockspace,
			name, file, cmd, fl);
	RETURN(G2FN_LM_PLOCK, error);
}

/**
 * gfs2_lm_punlock -
 * @sdp:
 * @name:
 * @file:
 * @fl:
 *
 * Returns: errno
 */

int
gfs2_lm_punlock(struct gfs2_sbd *sdp,
	       struct lm_lockname *name,
	       struct file *file, struct file_lock *fl)
{
	ENTER(G2FN_LM_PUNLOCK)
	int error;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;
	else
		error = sdp->sd_lockstruct.ls_ops->lm_punlock(
			sdp->sd_lockstruct.ls_lockspace,
			name, file, fl);
	RETURN(G2FN_LM_PUNLOCK, error);
}

/**
 * gfs2_lm_recovery_done -
 * @sdp:
 * @jid:
 * @message:
 *
 */

void
gfs2_lm_recovery_done(struct gfs2_sbd *sdp,
		     unsigned int jid, unsigned int message)
{
	ENTER(G2FN_LM_RECOVERY_DONE)
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_recovery_done(sdp->sd_lockstruct.ls_lockspace,
							    jid,
							    message);
	RET(G2FN_LM_RECOVERY_DONE);
}


