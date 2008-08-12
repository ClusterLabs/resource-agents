#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>

#include "gfs.h"
#include "inode.h"
#include "log.h"
#include "lops.h"
#include "unlinked.h"

/**
 * gfs_unlinked_get - Get a structure to represent an unlinked inode
 * @sdp: the filesystem
 * @inum: identifies the inode that's unlinked
 * @create: if TRUE, we're allowed to create the structure if we can't find it,
 *      otherwise return NULL
 *
 * Returns: the structure, or NULL
 *
 * Search the filesystem's list of gfs_unlinked to find a match.
 * If none found, create a new one and place on list.
 */

struct gfs_unlinked *
gfs_unlinked_get(struct gfs_sbd *sdp, struct gfs_inum *inum, int create)
{
	struct gfs_unlinked *ul = NULL, *new_ul = NULL;
	struct list_head *tmp, *head;

	for (;;) {
		spin_lock(&sdp->sd_unlinked_lock);

		for (head = &sdp->sd_unlinked_list, tmp = head->next;
		     tmp != head;
		     tmp = tmp->next) {
			ul = list_entry(tmp, struct gfs_unlinked, ul_list);
			if (gfs_inum_equal(&ul->ul_inum, inum)) {
				ul->ul_count++;
				break;
			}
		}

		if (tmp == head)
			ul = NULL;

		/* 2nd pass, still not there; add the new_ul we prepared */
		if (!ul && new_ul) {
			ul = new_ul;
			list_add(&ul->ul_list, &sdp->sd_unlinked_list);
			new_ul = NULL;
		}

		spin_unlock(&sdp->sd_unlinked_lock);

		/* 1st pass; we found pre-existing, OR not allowed to create.
		   2nd pass; another process added it, or we did */
		if (ul || !create) {
			if (new_ul)
				/* someone beat us to it; forget our new_ul */
				kfree(new_ul);
			return ul;
		}

		/* No match on list, 1st time through loop.
		   Prepare new_ul, then repeat loop to find out if another
		   process has created or unlinked an inode and put its
		   gfs_unlinked on list while we've been preparing this one. */
		new_ul = gmalloc(sizeof(struct gfs_unlinked));
		memset(new_ul, 0, sizeof(struct gfs_unlinked));

		new_ul->ul_count = 1;
		new_ul->ul_inum = *inum;

		INIT_LE(&new_ul->ul_new_le, &gfs_unlinked_lops);
		INIT_LE(&new_ul->ul_incore_le, &gfs_unlinked_lops);
		INIT_LE(&new_ul->ul_ondisk_le, &gfs_unlinked_lops);
	}
}

/**
 * gfs_unlinked_hold - increment the usage count on a struct gfs_unlinked
 * @sdp: the filesystem
 * @ul: the structure
 *
 */

void
gfs_unlinked_hold(struct gfs_sbd *sdp, struct gfs_unlinked *ul)
{
	spin_lock(&sdp->sd_unlinked_lock);
	gfs_assert(sdp, ul->ul_count,);
	ul->ul_count++;
	spin_unlock(&sdp->sd_unlinked_lock);
}

/**
 * gfs_unlinked_put - decrement the usage count on a struct gfs_unlinked
 * @sdp: the filesystem
 * @ul: the structure
 *
 * Free the structure if its reference count hits zero.
 *
 */

void
gfs_unlinked_put(struct gfs_sbd *sdp, struct gfs_unlinked *ul)
{
	spin_lock(&sdp->sd_unlinked_lock);

	gfs_assert(sdp, ul->ul_count,);
	ul->ul_count--;

	if (!ul->ul_count) {
		gfs_assert_warn(sdp,
				!test_bit(ULF_IC_LIST, &ul->ul_flags) &&
				!test_bit(ULF_OD_LIST, &ul->ul_flags) &&
				!test_bit(ULF_LOCK, &ul->ul_flags));
		list_del(&ul->ul_list);
		spin_unlock(&sdp->sd_unlinked_lock);
		kfree(ul);
	} else
		spin_unlock(&sdp->sd_unlinked_lock);
}

/**
 * unlinked_find - Find a inode to try to deallocate
 * @sdp: the filesystem
 *
 * The returned structure is locked and needs to be unlocked
 * with gfs_unlinked_unlock().
 *
 * Returns: A unlinked structure, or NULL
 */

struct gfs_unlinked *
unlinked_find(struct gfs_sbd *sdp)
{
	struct list_head *tmp, *head;
	struct gfs_unlinked *ul = NULL;

	if (test_bit(SDF_ROFS, &sdp->sd_flags))
		return NULL;

	gfs_log_lock(sdp);
	spin_lock(&sdp->sd_unlinked_lock);

	if (!atomic_read(&sdp->sd_unlinked_ic_count))
		goto out;

	for (head = &sdp->sd_unlinked_list, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		ul = list_entry(tmp, struct gfs_unlinked, ul_list);

		if (test_bit(ULF_LOCK, &ul->ul_flags))
			continue;
		if (!test_bit(ULF_IC_LIST, &ul->ul_flags))
			continue;

		list_move_tail(&ul->ul_list, &sdp->sd_unlinked_list);

		set_bit(ULF_LOCK, &ul->ul_flags);
		ul->ul_count++;

		goto out;
	}

	ul = NULL;

 out:
	spin_unlock(&sdp->sd_unlinked_lock);
	gfs_log_unlock(sdp);

	return ul;
}

/**
 * gfs_unlinked_lock - lock a unlinked structure
 * @sdp: the filesystem
 * @ul: the unlinked inode structure
 *
 */

void
gfs_unlinked_lock(struct gfs_sbd *sdp, struct gfs_unlinked *ul)
{
	spin_lock(&sdp->sd_unlinked_lock);

	gfs_assert_warn(sdp, !test_bit(ULF_LOCK, &ul->ul_flags));
	set_bit(ULF_LOCK, &ul->ul_flags);

	ul->ul_count++;

	spin_unlock(&sdp->sd_unlinked_lock);	
}

/**
 * gfs_unlinked_unlock - drop a reference on a unlinked structure
 * @sdp: the filesystem
 * @ul: the unlinked inode structure
 *
 */

void
gfs_unlinked_unlock(struct gfs_sbd *sdp, struct gfs_unlinked *ul)
{
	spin_lock(&sdp->sd_unlinked_lock);

	gfs_assert_warn(sdp, test_bit(ULF_LOCK, &ul->ul_flags));
	clear_bit(ULF_LOCK, &ul->ul_flags);

	gfs_assert(sdp, ul->ul_count,);
	ul->ul_count--;

	if (!ul->ul_count) {
		gfs_assert_warn(sdp, !test_bit(ULF_IC_LIST, &ul->ul_flags) &&
				!test_bit(ULF_OD_LIST, &ul->ul_flags));
		list_del(&ul->ul_list);
		spin_unlock(&sdp->sd_unlinked_lock);
		kfree(ul);
	} else
		spin_unlock(&sdp->sd_unlinked_lock);
}

/**
 * gfs_unlinked_merge - add/remove a unlinked inode from the in-memory list
 * @sdp: the filesystem
 * @type: is this a unlink tag or a dealloc tag
 * @inum: the inode number
 *
 * Called during journal recovery.
 */

void
gfs_unlinked_merge(struct gfs_sbd *sdp, unsigned int type,
		   struct gfs_inum *inum)
{
	struct gfs_unlinked *ul;

	gfs_assert(sdp, atomic_read(&sdp->sd_unlinked_ic_count) ==
		   atomic_read(&sdp->sd_unlinked_od_count),);

	ul = gfs_unlinked_get(sdp, inum, CREATE);

	gfs_log_lock(sdp);

	switch (type) {
	case GFS_LOG_DESC_IUL:
		gfs_unlinked_hold(sdp, ul);
		gfs_unlinked_hold(sdp, ul);
		gfs_assert(sdp, !test_bit(ULF_IC_LIST, &ul->ul_flags) &&
			   !test_bit(ULF_OD_LIST, &ul->ul_flags),);
		set_bit(ULF_IC_LIST, &ul->ul_flags);
		set_bit(ULF_OD_LIST, &ul->ul_flags);
		atomic_inc(&sdp->sd_unlinked_ic_count);
		atomic_inc(&sdp->sd_unlinked_od_count);

		break;

	case GFS_LOG_DESC_IDA:
		gfs_assert(sdp, test_bit(ULF_IC_LIST, &ul->ul_flags) &&
			   test_bit(ULF_OD_LIST, &ul->ul_flags),);
		clear_bit(ULF_IC_LIST, &ul->ul_flags);
		clear_bit(ULF_OD_LIST, &ul->ul_flags);
		gfs_unlinked_put(sdp, ul);
		gfs_unlinked_put(sdp, ul);
		gfs_assert(sdp, atomic_read(&sdp->sd_unlinked_ic_count) > 0,);
		atomic_dec(&sdp->sd_unlinked_ic_count);
		gfs_assert(sdp, atomic_read(&sdp->sd_unlinked_od_count) > 0,);
		atomic_dec(&sdp->sd_unlinked_od_count);

		break;
	}

	gfs_log_unlock(sdp);

	gfs_unlinked_put(sdp, ul);
}

/**
 * gfs_unlinked_cleanup - get rid of any extra struct gfs_unlinked structures
 * @sdp: the filesystem
 *
 */

void
gfs_unlinked_cleanup(struct gfs_sbd *sdp)
{
	struct gfs_unlinked *ul;

 restart:
	gfs_log_lock(sdp);

	gfs_assert(sdp, atomic_read(&sdp->sd_unlinked_ic_count) ==
		   atomic_read(&sdp->sd_unlinked_od_count),);

	spin_lock(&sdp->sd_unlinked_lock);

	while (!list_empty(&sdp->sd_unlinked_list)) {
		ul = list_entry(sdp->sd_unlinked_list.next,
				struct gfs_unlinked, ul_list);

		if (ul->ul_count > 2) {
			spin_unlock(&sdp->sd_unlinked_lock);
			gfs_log_unlock(sdp);
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(HZ);
			goto restart;
		}
		gfs_assert(sdp, ul->ul_count == 2,);

		gfs_assert_warn(sdp,
				test_bit(ULF_IC_LIST, &ul->ul_flags) &&
				test_bit(ULF_OD_LIST, &ul->ul_flags) &&
				!test_bit(ULF_LOCK, &ul->ul_flags));

		list_del(&ul->ul_list);

		atomic_dec(&sdp->sd_unlinked_ic_count);
		atomic_dec(&sdp->sd_unlinked_od_count);

		spin_unlock(&sdp->sd_unlinked_lock);
		kfree(ul);
		spin_lock(&sdp->sd_unlinked_lock);
	}

	spin_unlock(&sdp->sd_unlinked_lock);

	gfs_assert(sdp, !atomic_read(&sdp->sd_unlinked_ic_count) &&
		   !atomic_read(&sdp->sd_unlinked_od_count),);

	gfs_log_unlock(sdp);
}

/**
 * gfs_unlinked_limit - limit the number of inodes waiting to be deallocated
 * @sdp: the filesystem
 *
 * Returns: errno
 */

void
gfs_unlinked_limit(struct gfs_sbd *sdp)
{
	unsigned int tries = 0, min = 0;
	int error;

	if (atomic_read(&sdp->sd_unlinked_ic_count) >=
	    gfs_tune_get(sdp, gt_ilimit2)) {
		tries = gfs_tune_get(sdp, gt_ilimit2_tries);
		min = gfs_tune_get(sdp, gt_ilimit2_min);
	} else if (atomic_read(&sdp->sd_unlinked_ic_count) >=
		   gfs_tune_get(sdp, gt_ilimit1)) {
		tries = gfs_tune_get(sdp, gt_ilimit1_tries);
		min = gfs_tune_get(sdp, gt_ilimit1_min);
	}

	while (tries--) {
		struct gfs_unlinked *ul = unlinked_find(sdp);
		if (!ul)
			break;

		error = gfs_inode_dealloc(sdp, &ul->ul_inum);

		gfs_unlinked_unlock(sdp, ul);

		if (!error) {
			if (!--min)
				break;
		} else if (error != 1)
			break;
	}
}

/**
 * gfs_unlinked_dealloc - Go through the list of inodes to be deallocated
 * @sdp: the filesystem
 *
 * Returns: errno
 */

void
gfs_unlinked_dealloc(struct gfs_sbd *sdp)
{
	unsigned int hits, strikes;
	int error;

	for (;;) {
		hits = 0;
		strikes = 0;

		for (;;) {
			struct gfs_unlinked *ul = unlinked_find(sdp);
			if (!ul)
				return;

			error = gfs_inode_dealloc(sdp, &ul->ul_inum);

			gfs_unlinked_unlock(sdp, ul);

			if (!error) {
				hits++;
				if (strikes)
					strikes--;
			} else if (error == 1) {
				strikes++;
				if (strikes >= atomic_read(&sdp->sd_unlinked_ic_count)) {
					error = 0;
					break;
				}
			} else
				goto out;
		}

		if (!hits || kthread_should_stop())
			break;

		cond_resched();
	}

 out:
	if (error &&
	    error != -EROFS &&
	    !test_bit(SDF_SHUTDOWN, &sdp->sd_flags))
		printk("GFS: fsid=%s: error deallocating inodes: %d\n",
		       sdp->sd_fsname, error);
}
