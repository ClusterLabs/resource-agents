/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/delay.h>
#include <asm/semaphore.h>

#include "gfs2.h"
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

int gfs2_lm_mount(struct gfs2_sbd *sdp, int silent)
{
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
		struct buffer_head *bh;
		bh = sb_getblk(sdp->sd_vfs,
			       GFS2_SB_ADDR >> sdp->sd_fsb2bb_shift);
		lock_buffer(bh);
		clear_buffer_uptodate(bh);
		clear_buffer_dirty(bh);
		unlock_buffer(bh);
		ll_rw_block(READ, 1, &bh);
		wait_on_buffer(bh);

		if (!buffer_uptodate(bh)) {
			brelse(bh);
			return -EIO;
		}

		sb = kmalloc(sizeof(struct gfs2_sb), GFP_KERNEL);
		if (!sb) {
			brelse(bh);
			return -ENOMEM;
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

	fs_info(sdp, "Trying to join cluster \"%s\", \"%s\"\n", proto, table);

	error = lm_mount(proto, table, sdp->sd_args.ar_hostdata,
			 gfs2_glock_cb, sdp,
			 GFS2_MIN_LVB_SIZE, flags,
			 &sdp->sd_lockstruct);
	if (error) {
		fs_info(sdp, "can't mount proto=%s, table=%s, hostdata=%s\n",
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

	fs_info(sdp, "Joined cluster. Now mounting FS...\n");

	if ((sdp->sd_lockstruct.ls_flags & LM_LSFLAG_LOCAL) &&
	    !sdp->sd_args.ar_ignore_local_fs) {
		/* Force local [p|f]locks */
		sdp->sd_args.ar_localflocks = TRUE;

		/* Force local read ahead and caching */
		sdp->sd_args.ar_localcaching = TRUE;
	}

 out:
	kfree(sb);

	return error;
}

void gfs2_lm_others_may_mount(struct gfs2_sbd *sdp)
{
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_others_may_mount(sdp->sd_lockstruct.ls_lockspace);
}

void gfs2_lm_unmount(struct gfs2_sbd *sdp)
{
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		lm_unmount(&sdp->sd_lockstruct);
}

int gfs2_lm_withdraw(struct gfs2_sbd *sdp, char *fmt, ...)
{
	va_list args;

	if (test_and_set_bit(SDF_SHUTDOWN, &sdp->sd_flags))
		return 0;

	va_start(args, fmt);
	vprintk(fmt, args);
	va_end(args);

	fs_err(sdp, "about to withdraw from the cluster\n");
	if (sdp->sd_args.ar_debug)
		BUG();

	fs_err(sdp, "waiting for outstanding I/O\n");

	/* FIXME: suspend dm device so oustanding bio's complete
	   and all further io requests fail */

	fs_err(sdp, "telling LM to withdraw\n");
	lm_withdraw(&sdp->sd_lockstruct);
	fs_err(sdp, "withdrawn\n");

	return -1;
}

int gfs2_lm_get_lock(struct gfs2_sbd *sdp, struct lm_lockname *name,
		     lm_lock_t **lockp)
{
	int error;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;
	else
		error = sdp->sd_lockstruct.ls_ops->lm_get_lock(sdp->sd_lockstruct.ls_lockspace, name, lockp);
	return error;
}

void gfs2_lm_put_lock(struct gfs2_sbd *sdp, lm_lock_t *lock)
{
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_put_lock(lock);
}

unsigned int gfs2_lm_lock(struct gfs2_sbd *sdp, lm_lock_t *lock,
			  unsigned int cur_state, unsigned int req_state,
			  unsigned int flags)
{
	int ret;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		ret = 0;
	else
		ret = sdp->sd_lockstruct.ls_ops->lm_lock(lock,
							 cur_state,
							 req_state, flags);
	return ret;
}

unsigned int gfs2_lm_unlock(struct gfs2_sbd *sdp, lm_lock_t *lock,
			    unsigned int cur_state)
{
	int ret;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		ret = 0;
	else
		ret =  sdp->sd_lockstruct.ls_ops->lm_unlock(lock, cur_state);
	return ret;
}

void gfs2_lm_cancel(struct gfs2_sbd *sdp, lm_lock_t *lock)
{
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_cancel(lock);
}

int gfs2_lm_hold_lvb(struct gfs2_sbd *sdp, lm_lock_t *lock, char **lvbp)
{
	int error;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;
	else
		error = sdp->sd_lockstruct.ls_ops->lm_hold_lvb(lock, lvbp);
	return error;
}

void gfs2_lm_unhold_lvb(struct gfs2_sbd *sdp, lm_lock_t *lock, char *lvb)
{
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_unhold_lvb(lock, lvb);
}

void gfs2_lm_sync_lvb(struct gfs2_sbd *sdp, lm_lock_t *lock, char *lvb)
{
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_sync_lvb(lock, lvb);
}

int gfs2_lm_plock_get(struct gfs2_sbd *sdp, struct lm_lockname *name,
		      struct file *file, struct file_lock *fl)
{
	int error;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;
	else
		error = sdp->sd_lockstruct.ls_ops->lm_plock_get(
			sdp->sd_lockstruct.ls_lockspace,
			name, file, fl);
	return error;
}

int gfs2_lm_plock(struct gfs2_sbd *sdp, struct lm_lockname *name,
		  struct file *file, int cmd, struct file_lock *fl)
{
	int error;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;
	else
		error = sdp->sd_lockstruct.ls_ops->lm_plock(
			sdp->sd_lockstruct.ls_lockspace,
			name, file, cmd, fl);
	return error;
}

int gfs2_lm_punlock(struct gfs2_sbd *sdp, struct lm_lockname *name,
		    struct file *file, struct file_lock *fl)
{
	int error;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;
	else
		error = sdp->sd_lockstruct.ls_ops->lm_punlock(
			sdp->sd_lockstruct.ls_lockspace,
			name, file, fl);
	return error;
}

void gfs2_lm_recovery_done(struct gfs2_sbd *sdp, unsigned int jid,
			   unsigned int message)
{
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_recovery_done(sdp->sd_lockstruct.ls_lockspace, jid, message);
}

