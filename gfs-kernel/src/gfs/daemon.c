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
#include "daemon.h"
#include "glock.h"
#include "log.h"
#include "quota.h"
#include "recovery.h"
#include "super.h"
#include "unlinked.h"

/**
 * gfs_scand - Writing of cached scan chanes into the scan file
 * @sdp: Pointer to GFS superblock
 *
 */

int
gfs_scand(void *data)
{
	struct gfs_sbd *sdp = (struct gfs_sbd *)data;

	daemonize("gfs_scand");
	sdp->sd_scand_process = current;
	set_bit(SDF_SCAND_RUN, &sdp->sd_flags);
	complete(&sdp->sd_thread_completion);

	for (;;) {
		gfs_scand_internal(sdp);

		if (!test_bit(SDF_SCAND_RUN, &sdp->sd_flags))
			break;

		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(sdp->sd_tune.gt_scand_secs * HZ);
	}

	down(&sdp->sd_thread_lock);
	up(&sdp->sd_thread_lock);

	complete(&sdp->sd_thread_completion);

	return 0;
}

/**
 * gfs_glockd - Writing of cached scan chanes into the scan file
 * @sdp: Pointer to GFS superblock
 *
 */

int
gfs_glockd(void *data)
{
	struct gfs_sbd *sdp = (struct gfs_sbd *)data;

	daemonize("gfs_glockd");
	set_bit(SDF_GLOCKD_RUN, &sdp->sd_flags);
	complete(&sdp->sd_thread_completion);

	for (;;) {
		while (atomic_read(&sdp->sd_reclaim_count))
			gfs_reclaim_glock(sdp);

		if (!test_bit(SDF_GLOCKD_RUN, &sdp->sd_flags))
			break;

		{
			DECLARE_WAITQUEUE(__wait_chan, current);
			current->state = TASK_INTERRUPTIBLE;
			add_wait_queue(&sdp->sd_reclaim_wchan, &__wait_chan);
			if (!atomic_read(&sdp->sd_reclaim_count)
			    && test_bit(SDF_GLOCKD_RUN, &sdp->sd_flags))
				schedule();
			remove_wait_queue(&sdp->sd_reclaim_wchan, &__wait_chan);
			current->state = TASK_RUNNING;
		}
	}

	complete(&sdp->sd_thread_completion);

	return 0;
}

/**
 * gfs_recoverd - Recovery of dead machine's journals
 * @sdp: Pointer to GFS superblock
 *
 */

int
gfs_recoverd(void *data)
{
	struct gfs_sbd *sdp = (struct gfs_sbd *)data;

	daemonize("gfs_recoverd");
	sdp->sd_recoverd_process = current;
	set_bit(SDF_RECOVERD_RUN, &sdp->sd_flags);
	complete(&sdp->sd_thread_completion);

	for (;;) {
		gfs_check_journals(sdp);

		if (!test_bit(SDF_RECOVERD_RUN, &sdp->sd_flags))
			break;

		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(sdp->sd_tune.gt_recoverd_secs * HZ);
	}

	down(&sdp->sd_thread_lock);
	up(&sdp->sd_thread_lock);

	complete(&sdp->sd_thread_completion);

	return 0;
}

/**
 * gfs_logd - Writing of cached log chanes into the log file
 * @sdp: Pointer to GFS superblock
 *
 */

int
gfs_logd(void *data)
{
	struct gfs_sbd *sdp = (struct gfs_sbd *)data;
	struct gfs_holder ji_gh;

	daemonize("gfs_logd");
	sdp->sd_logd_process = current;
	set_bit(SDF_LOGD_RUN, &sdp->sd_flags);
	complete(&sdp->sd_thread_completion);

	for (;;) {
		gfs_ail_empty(sdp);

		if (time_after_eq(jiffies,
				  sdp->sd_jindex_refresh_time +
				  sdp->sd_tune.gt_jindex_refresh_secs * HZ)) {
			if (test_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags) &&
			    !gfs_jindex_hold(sdp, &ji_gh))
				gfs_glock_dq_uninit(&ji_gh);
			sdp->sd_jindex_refresh_time = jiffies;
		}

		if (!test_bit(SDF_LOGD_RUN, &sdp->sd_flags))
			break;

		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(sdp->sd_tune.gt_logd_secs * HZ);
	}

	down(&sdp->sd_thread_lock);
	up(&sdp->sd_thread_lock);

	complete(&sdp->sd_thread_completion);

	return 0;
}

/**
 * gfs_quotad - Writing of cached quota chanes into the quota file
 * @sdp: Pointer to GFS superblock
 *
 */

int
gfs_quotad(void *data)
{
	struct gfs_sbd *sdp = (struct gfs_sbd *)data;
	int error;

	daemonize("gfs_quotad");
	sdp->sd_quotad_process = current;
	set_bit(SDF_QUOTAD_RUN, &sdp->sd_flags);
	complete(&sdp->sd_thread_completion);

	for (;;) {
		if (time_after_eq(jiffies,
				  sdp->sd_quota_sync_time +
				  sdp->sd_tune.gt_quota_quantum * HZ)) {
			error = gfs_quota_sync(sdp);
			if (error && error != -EROFS)
				printk("GFS: fsid=%s: quotad: error = %d\n",
				       sdp->sd_fsname, error);
			sdp->sd_quota_sync_time = jiffies;
		}

		gfs_quota_scan(sdp);

		if (!test_bit(SDF_QUOTAD_RUN, &sdp->sd_flags))
			break;

		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(sdp->sd_tune.gt_quotad_secs * HZ);
	}

	down(&sdp->sd_thread_lock);
	up(&sdp->sd_thread_lock);

	complete(&sdp->sd_thread_completion);

	return 0;
}

/**
 * gfs_inoded - Deallocation of unlinked inodes
 * @sdp: Pointer to GFS superblock
 *
 */

int
gfs_inoded(void *data)
{
	struct gfs_sbd *sdp = (struct gfs_sbd *)data;

	daemonize("gfs_inoded");
	sdp->sd_inoded_process = current;
	set_bit(SDF_INODED_RUN, &sdp->sd_flags);
	complete(&sdp->sd_thread_completion);

	for (;;) {
		gfs_unlinked_dealloc(sdp);

		if (!test_bit(SDF_INODED_RUN, &sdp->sd_flags))
			break;

		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(sdp->sd_tune.gt_inoded_secs * HZ);
	}

	down(&sdp->sd_thread_lock);
	up(&sdp->sd_thread_lock);

	complete(&sdp->sd_thread_completion);

	return 0;
}
