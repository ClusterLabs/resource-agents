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
#include "flock.h"
#include "glock.h"
#include "glops.h"

/**
 * gfs_flock - Acquire a flock on a file
 * @fp: the file
 * @ex: exclusive lock
 * @wait: wait for lock
 *
 * Returns: 0 on success, -EXXX on failure
 */

int
gfs_flock(struct gfs_file *fp, int ex, int wait)
{
	struct gfs_holder *fl_gh = &fp->f_fl_gh;
	struct gfs_inode *ip = fp->f_inode;
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_glock *gl;
	int error = 0;

	down(&fp->f_fl_lock);

	if (fl_gh->gh_gl) {
		gfs_glock_dq_uninit(fl_gh);
		error = -EDEADLK;
		goto out;
	}

	error = gfs_glock_get(sdp,
			      ip->i_num.no_formal_ino, &gfs_flock_glops,
			      CREATE, &gl);
	if (error)
		goto out;

	gfs_holder_init(gl, (ex) ? LM_ST_EXCLUSIVE : LM_ST_SHARED,
			((wait) ? 0 : LM_FLAG_TRY) | GL_EXACT | GL_NOCACHE,
			fl_gh);
	fl_gh->gh_owner = NULL;

	gfs_glock_put(gl);

	error = gfs_glock_nq(fl_gh);
	if (error) {
		gfs_holder_uninit(fl_gh);
		if (error == GLR_TRYFAILED) {
			GFS_ASSERT_INODE(!wait, ip,);
			error = -EAGAIN;
		}
	}

 out:
	up(&fp->f_fl_lock);

	return error;
}

/**
 * gfs_funlock - Release a flock on a file
 * @fp: the file
 *
 */

int
gfs_funlock(struct gfs_file *fp)
{
	struct gfs_holder *fl_gh = &fp->f_fl_gh;

	down(&fp->f_fl_lock);
	if (fl_gh->gh_gl)
		gfs_glock_dq_uninit(fl_gh);
	up(&fp->f_fl_lock);

	return 0;
}
