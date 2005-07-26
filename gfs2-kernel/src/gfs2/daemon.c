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
#include <linux/kthread.h>
#include <linux/delay.h>

#include "gfs2.h"
#include "daemon.h"
#include "glock.h"
#include "log.h"
#include "quota.h"
#include "recovery.h"
#include "super.h"
#include "unlinked.h"

/**
 * gfs2_scand - Look for cached glocks and inodes to toss from memory
 * @sdp: Pointer to GFS2 superblock
 *
 * One of these daemons runs, finding candidates to add to sd_reclaim_list.
 * See gfs2_glockd()
 */

int gfs2_scand(void *data)
{
	struct gfs2_sbd *sdp = (struct gfs2_sbd *)data;

	while (!kthread_should_stop()) {
		gfs2_scand_internal(sdp);
		msleep_interruptible(gfs2_tune_get(sdp, gt_scand_secs) * 1000);
	}

	return 0;
}

/**
 * gfs2_glockd - Reclaim unused glock structures
 * @sdp: Pointer to GFS2 superblock
 *
 * One or more of these daemons run, reclaiming glocks on sd_reclaim_list.
 * sd_glockd_num says how many daemons are running now.
 * Number of daemons can be set by user, with num_glockd mount option.
 * See gfs2_scand()
 */

int gfs2_glockd(void *data)
{
	struct gfs2_sbd *sdp = (struct gfs2_sbd *)data;
	DECLARE_WAITQUEUE(wait_chan, current);

	while (!kthread_should_stop()) {
		while (atomic_read(&sdp->sd_reclaim_count))
			gfs2_reclaim_glock(sdp);

		set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&sdp->sd_reclaim_wq, &wait_chan);
		if (!atomic_read(&sdp->sd_reclaim_count) &&
		    !kthread_should_stop())
			schedule();
		remove_wait_queue(&sdp->sd_reclaim_wq, &wait_chan);
		set_current_state(TASK_RUNNING);
	}

	return 0;
}

/**
 * gfs2_recoverd - Recover dead machine's journals
 * @sdp: Pointer to GFS2 superblock
 *
 */

int gfs2_recoverd(void *data)
{
	struct gfs2_sbd *sdp = (struct gfs2_sbd *)data;

	while (!kthread_should_stop()) {
		gfs2_check_journals(sdp);
		msleep_interruptible(gfs2_tune_get(sdp, gt_recoverd_secs)*1000);
	}

	return 0;
}

/**
 * gfs2_logd - Update log tail as Active Items get flushed to in-place blocks
 * @sdp: Pointer to GFS2 superblock
 *
 * Also, periodically check to make sure that we're using the most recent
 * journal index.
 */

int gfs2_logd(void *data)
{
	struct gfs2_sbd *sdp = (struct gfs2_sbd *)data;
	struct gfs2_holder ji_gh;

	while (!kthread_should_stop()) {
		/* Advance the log tail */
		gfs2_ail1_empty(sdp, DIO_ALL);
		if (time_after_eq(jiffies,
				  sdp->sd_log_flush_time +
				  gfs2_tune_get(sdp, gt_log_flush_secs) * HZ)) {
			gfs2_log_flush(sdp);
			sdp->sd_log_flush_time = jiffies;
		}

		/* Check for latest journal index */
		if (time_after_eq(jiffies,
				  sdp->sd_jindex_refresh_time +
				  gfs2_tune_get(sdp, gt_jindex_refresh_secs) * HZ)) {
			if (!gfs2_jindex_hold(sdp, &ji_gh))
				gfs2_glock_dq_uninit(&ji_gh);
			sdp->sd_jindex_refresh_time = jiffies;
		}

		msleep_interruptible(gfs2_tune_get(sdp, gt_logd_secs) * 1000);
	}

	return 0;
}

/**
 * gfs2_quotad - Write cached quota changes into the quota file
 * @sdp: Pointer to GFS2 superblock
 *
 */

int gfs2_quotad(void *data)
{
	struct gfs2_sbd *sdp = (struct gfs2_sbd *)data;
	int error;

	while (!kthread_should_stop()) {
		/* Update the master statfs file */
		if (time_after_eq(jiffies,
				  sdp->sd_statfs_sync_time +
				  gfs2_tune_get(sdp, gt_statfs_quantum) * HZ)) {
			error = gfs2_statfs_sync(sdp);
			if (error &&
			    error != -EROFS &&
			    !test_bit(SDF_SHUTDOWN, &sdp->sd_flags))
				printk("GFS2: fsid=%s: quotad: (1) error=%d\n",
				       sdp->sd_fsname, error);
			sdp->sd_statfs_sync_time = jiffies;
		}

		/* Update quota file */
		if (time_after_eq(jiffies,
				  sdp->sd_quota_sync_time +
				  gfs2_tune_get(sdp, gt_quota_quantum) * HZ)) {
			error = gfs2_quota_sync(sdp);
			if (error &&
			    error != -EROFS &&
			    !test_bit(SDF_SHUTDOWN, &sdp->sd_flags))
				printk("GFS2: fsid=%s: quotad: (2) error=%d\n",
				       sdp->sd_fsname, error);
			sdp->sd_quota_sync_time = jiffies;
		}

		gfs2_quota_scan(sdp);
		msleep_interruptible(gfs2_tune_get(sdp, gt_quotad_secs) * 1000);
	}

	return 0;
}

/**
 * gfs2_inoded - Deallocate unlinked inodes
 * @sdp: Pointer to GFS2 superblock
 *
 */

int gfs2_inoded(void *data)
{
	struct gfs2_sbd *sdp = (struct gfs2_sbd *)data;
	int error;

	while (!kthread_should_stop()) {
		error = gfs2_unlinked_dealloc(sdp);
		if (error &&
		    error != -EROFS &&
		    !test_bit(SDF_SHUTDOWN, &sdp->sd_flags))
			printk("GFS2: fsid=%s: inoded: error = %d\n",
			       sdp->sd_fsname, error);

		msleep_interruptible(gfs2_tune_get(sdp, gt_inoded_secs) * 1000);
	}

	return 0;
}

