#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
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
 * gfs_scand - Look for cached glocks and inodes to toss from memory
 * @sdp: Pointer to GFS superblock
 *
 * One of these daemons runs, finding candidates to add to sd_reclaim_list.
 * See gfs_glockd()
 */

int
gfs_scand(void *data)
{
	struct gfs_sbd *sdp = (struct gfs_sbd *)data;

	while (!kthread_should_stop()) {
		gfs_scand_internal(sdp);
		schedule_timeout_interruptible(gfs_tune_get(sdp, gt_scand_secs) * HZ);
	}

	return 0;
}

/**
 * gfs_glockd - Reclaim unused glock structures
 * @sdp: Pointer to GFS superblock
 *
 * One or more of these daemons run, reclaiming glocks on sd_reclaim_list.
 * sd_glockd_num says how many daemons are running now.
 * Number of daemons can be set by user, with num_glockd mount option.
 * See gfs_scand()
 */

int
gfs_glockd(void *data)
{
	struct gfs_sbd *sdp = (struct gfs_sbd *)data;

	while (!kthread_should_stop()) {
		while (atomic_read(&sdp->sd_reclaim_count))
			gfs_reclaim_glock(sdp);

		wait_event_interruptible(sdp->sd_reclaim_wchan,
								 (atomic_read(&sdp->sd_reclaim_count) ||
								  kthread_should_stop()));
	}

	return 0;
}

/**
 * gfs_recoverd - Recover dead machine's journals
 * @sdp: Pointer to GFS superblock
 *
 */

int
gfs_recoverd(void *data)
{
	struct gfs_sbd *sdp = (struct gfs_sbd *)data;

	while (!kthread_should_stop()) {
		gfs_check_journals(sdp);
		schedule_timeout_interruptible(gfs_tune_get(sdp, gt_recoverd_secs) * HZ);
	}

	return 0;
}

/**
 * gfs_logd - Update log tail as Active Items get flushed to in-place blocks
 * @sdp: Pointer to GFS superblock
 *
 * Also, periodically check to make sure that we're using the most recent
 * journal index.
 */

int
gfs_logd(void *data)
{
	struct gfs_sbd *sdp = (struct gfs_sbd *)data;
	struct gfs_holder ji_gh;

	while (!kthread_should_stop()) {
		/* Advance the log tail */
		gfs_ail_empty(sdp);

		/* Check for latest journal index */
		if (time_after_eq(jiffies,
				  sdp->sd_jindex_refresh_time +
				  gfs_tune_get(sdp, gt_jindex_refresh_secs) * HZ)) {
			if (test_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags) &&
			    !gfs_jindex_hold(sdp, &ji_gh))
				gfs_glock_dq_uninit(&ji_gh);
			sdp->sd_jindex_refresh_time = jiffies;
		}

		schedule_timeout_interruptible(gfs_tune_get(sdp, gt_logd_secs) * HZ);
	}

	return 0;
}

/**
 * gfs_quotad - Write cached quota changes into the quota file
 * @sdp: Pointer to GFS superblock
 *
 */

int
gfs_quotad(void *data)
{
	struct gfs_sbd *sdp = (struct gfs_sbd *)data;
	int error;

	while (!kthread_should_stop()) {
		/* Update statfs file */
		if (gfs_tune_get(sdp, gt_statfs_fast) &&
			time_after_eq(jiffies,
			sdp->sd_statfs_sync_time +
			gfs_tune_get(sdp, gt_statfs_fast) * HZ)) {
			error = gfs_statfs_sync(sdp);
			if (error && error != -EROFS &&
				!test_bit(SDF_SHUTDOWN, &sdp->sd_flags))
				printk("GFS: fsid=%s: statfs: error = %d\n",
				sdp->sd_fsname, error);
				sdp->sd_statfs_sync_time = jiffies;
		}
		/* Update quota file */
		if (time_after_eq(jiffies,
				  sdp->sd_quota_sync_time +
				  gfs_tune_get(sdp, gt_quota_quantum) * HZ)) {
			error = gfs_quota_sync(sdp);
			if (error &&
			    error != -EROFS &&
			    !test_bit(SDF_SHUTDOWN, &sdp->sd_flags))
				printk("GFS: fsid=%s: quotad: error = %d\n",
				       sdp->sd_fsname, error);
			sdp->sd_quota_sync_time = jiffies;
		}

		/* Clean up */
		gfs_quota_scan(sdp);
		schedule_timeout_interruptible(gfs_tune_get(sdp, gt_quotad_secs) * HZ);
	}

	return 0;
}

/**
 * gfs_inoded - Deallocate unlinked inodes
 * @sdp: Pointer to GFS superblock
 *
 */

int
gfs_inoded(void *data)
{
	struct gfs_sbd *sdp = (struct gfs_sbd *)data;

	while (!kthread_should_stop()) {
		gfs_unlinked_dealloc(sdp);
		schedule_timeout_interruptible(gfs_tune_get(sdp, gt_inoded_secs) * HZ);
	}

	return 0;
}
