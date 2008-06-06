#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/delay.h>

#include "gfs_ondisk.h"
#include "gfs.h"
#include "incore.h"
#include "glock.h"
#include "lm.h"
#include "super.h"
#include "util.h"
#include "lvb.h"

/**
 * gfs_lm_mount - mount a locking protocol
 * @sdp: the filesystem
 * @args: mount arguements
 * @silent: if 1, don't complain if the FS isn't a GFS fs
 *
 * Returns: errno
 */

int gfs_lm_mount(struct gfs_sbd *sdp, int silent)
{
	char *proto = sdp->sd_proto_name;
	char *table = sdp->sd_table_name;
	int flags = 0;
	int error;

	if (sdp->sd_args.ar_spectator)
		flags |= LM_MFLAG_SPECTATOR;

	printk("Trying to join cluster \"%s\", \"%s\"\n", proto, table);

	error = gfs2_mount_lockproto(proto, table, sdp->sd_args.ar_hostdata,
				     gfs_glock_cb, sdp,
				     GFS_MIN_LVB_SIZE, flags,
				     &sdp->sd_lockstruct, &sdp->sd_kobj);
	if (error) {
		printk("can't mount proto=%s, table=%s, hostdata=%s\n",
			   proto, table, sdp->sd_args.ar_hostdata);
		goto out;
	}

	if (gfs_assert_warn(sdp, sdp->sd_lockstruct.ls_lockspace) ||
	    gfs_assert_warn(sdp, sdp->sd_lockstruct.ls_ops) ||
	    gfs_assert_warn(sdp, sdp->sd_lockstruct.ls_lvb_size >=
				  GFS_MIN_LVB_SIZE)) {
		gfs2_unmount_lockproto(&sdp->sd_lockstruct);
		goto out;
	}

	if (sdp->sd_args.ar_spectator)
		snprintf(sdp->sd_fsname, 256, "%s.s", table);
	else
		snprintf(sdp->sd_fsname, 256, "%s.%u", table,
			 sdp->sd_lockstruct.ls_jid);

	printk("Joined cluster. Now mounting FS...\n");
	if ((sdp->sd_lockstruct.ls_flags & LM_LSFLAG_LOCAL) &&
	    !sdp->sd_args.ar_ignore_local_fs) {
		sdp->sd_args.ar_localflocks = 1;
		sdp->sd_args.ar_localcaching = 1;
	}

 out:
	return error;
}

void gfs_lm_others_may_mount(struct gfs_sbd *sdp)
{
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_others_may_mount(
					sdp->sd_lockstruct.ls_lockspace);
}

void gfs_lm_unmount(struct gfs_sbd *sdp)
{
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		gfs2_unmount_lockproto(&sdp->sd_lockstruct);
}

int gfs_lm_withdraw(struct gfs_sbd *sdp, char *fmt, ...)
{
	va_list args;

	if (test_and_set_bit(SDF_SHUTDOWN, &sdp->sd_flags))
		return 0;

	va_start(args, fmt);
	vprintk(fmt, args);
	va_end(args);

	printk("GFS: fsid=%s: about to withdraw from the cluster\n",
	       sdp->sd_fsname);

	BUG_ON(sdp->sd_args.ar_debug);

	printk("GFS: fsid=%s: telling LM to withdraw\n",
	       sdp->sd_fsname);

	gfs2_withdraw_lockproto(&sdp->sd_lockstruct);

	printk("GFS: fsid=%s: withdrawn\n",
	       sdp->sd_fsname);
	dump_stack();

	return -1;
}

int gfs_lm_get_lock(struct gfs_sbd *sdp, struct lm_lockname *name,
		     void **lockp)
{
	int error;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;
	else
		error = sdp->sd_lockstruct.ls_ops->lm_get_lock(
				sdp->sd_lockstruct.ls_lockspace, name, lockp);
	return error;
}

void gfs_lm_put_lock(struct gfs_sbd *sdp, void *lock)
{
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_put_lock(lock);
}

unsigned int gfs_lm_lock(struct gfs_sbd *sdp, void *lock,
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

unsigned int gfs_lm_unlock(struct gfs_sbd *sdp, void *lock,
			    unsigned int cur_state)
{
	int ret;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		ret = 0;
	else
		ret = sdp->sd_lockstruct.ls_ops->lm_unlock(lock, cur_state);
	return ret;
}

void gfs_lm_cancel(struct gfs_sbd *sdp, void *lock)
{
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_cancel(lock);
}

int gfs_lm_hold_lvb(struct gfs_sbd *sdp, void *lock, char **lvbp)
{
	int error;
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;
	else
		error = sdp->sd_lockstruct.ls_ops->lm_hold_lvb(lock, lvbp);
	return error;
}

void gfs_lm_unhold_lvb(struct gfs_sbd *sdp, void *lock, char *lvb)
{
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_unhold_lvb(lock, lvb);
}

#if 0
void gfs_lm_sync_lvb(struct gfs_sbd *sdp, void *lock, char *lvb)
{
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_sync_lvb(lock, lvb);
}
#endif

int gfs_lm_plock_get(struct gfs_sbd *sdp, struct lm_lockname *name,
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

int gfs_lm_plock(struct gfs_sbd *sdp, struct lm_lockname *name,
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

int gfs_lm_punlock(struct gfs_sbd *sdp, struct lm_lockname *name,
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

void gfs_lm_recovery_done(struct gfs_sbd *sdp, unsigned int jid,
			   unsigned int message)
{
	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		sdp->sd_lockstruct.ls_ops->lm_recovery_done(
			sdp->sd_lockstruct.ls_lockspace, jid, message);
}

