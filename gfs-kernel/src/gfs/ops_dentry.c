#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>

#include "gfs.h"
#include "dir.h"
#include "glock.h"
#include "ops_dentry.h"

/**
 * gfs_drevalidate - Check directory lookup consistency
 * @dentry: the mapping to check
 * @nd:
 *
 * Check to make sure the lookup necessary to arrive at this inode from its
 * parent is still good.
 *
 * Returns: 1 if the dentry is ok, 0 if it isn't
 */

static int
gfs_drevalidate(struct dentry *dentry, struct nameidata *nd)
{
	struct dentry *parent = dget_parent(dentry);
	struct gfs_inode *dip = get_v2ip(parent->d_inode);
	struct gfs_sbd *sdp = dip->i_sbd;
	struct inode *inode;
	struct gfs_holder d_gh;
	struct gfs_inode *ip;
	struct gfs_inum inum;
	unsigned int type;
	int error;

	lock_kernel();

	atomic_inc(&sdp->sd_ops_dentry);

	if (sdp->sd_args.ar_localcaching)
		goto valid;

	inode = dentry->d_inode;
	if (inode && is_bad_inode(inode))
		goto invalid;

	error = gfs_glock_nq_init(dip->i_gl, LM_ST_SHARED, 0, &d_gh);
	if (error)
		goto fail;

	error = gfs_dir_search(dip, &dentry->d_name, &inum, &type);
	switch (error) {
	case 0:
		if (!inode)
			goto invalid_gunlock;
		break;
	case -ENOENT:
		if (!inode)
			goto valid_gunlock;
		goto invalid_gunlock;
	default:
		goto fail_gunlock;
	}

	ip = get_v2ip(inode);

	if (ip->i_num.no_formal_ino != inum.no_formal_ino)
		goto invalid_gunlock;

	if (ip->i_di.di_type != type) {
		gfs_consist_inode(dip);
		goto fail_gunlock;
	}

 valid_gunlock:
	gfs_glock_dq_uninit(&d_gh);

 valid:
	unlock_kernel();
	dput(parent);
	return 1;

 invalid_gunlock:
	gfs_glock_dq_uninit(&d_gh);

 invalid:
	if (inode && S_ISDIR(inode->i_mode)) {
		if (have_submounts(dentry))
			goto valid;
		shrink_dcache_parent(dentry);
	}
	d_drop(dentry);

	unlock_kernel();
	dput(parent);
	return 0;

 fail_gunlock:
	gfs_glock_dq_uninit(&d_gh);

 fail:
	unlock_kernel();
	dput(parent);
	return 0;
}

struct dentry_operations gfs_dops = {
	.d_revalidate = gfs_drevalidate,
};
