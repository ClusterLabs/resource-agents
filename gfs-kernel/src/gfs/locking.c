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
#include "locking.h"
#include "super.h"

/**
 * gfs_mount_lockproto - mount a locking protocol
 * @sdp: the filesystem
 * @args: mount arguements
 * @silent: if TRUE, don't complain if the FS isn't a GFS fs
 *
 * Returns: 0 on success, -EXXX on failure
 */

int
gfs_mount_lockproto(struct gfs_sbd *sdp, int silent)
{
	struct gfs_sb *sb = NULL;
	struct buffer_head *bh;
	char *proto, *table, *p = NULL;
	int error = 0;

	proto = sdp->sd_args.ar_lockproto;
	table = sdp->sd_args.ar_locktable;

	/*  Try to autodetect  */

	if (!proto[0] || !table[0]) {
		error = gfs_dread(sdp, GFS_SB_ADDR >> sdp->sd_fsb2bb_shift, NULL,
				  DIO_FORCE | DIO_START | DIO_WAIT, &bh);
		if (error)
			goto out;

		sb = gmalloc(sizeof(struct gfs_sb));
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

	error = lm_mount(proto, table, sdp->sd_args.ar_hostdata,
			 gfs_glock_cb, sdp,
			 GFS_MIN_LVB_SIZE, &sdp->sd_lockstruct);
	if (error) {
		printk("GFS: can't mount proto = %s, table = %s, hostdata = %s\n",
		     proto, table, sdp->sd_args.ar_hostdata);
		goto out;
	}

	GFS_ASSERT_SBD(sdp->sd_lockstruct.ls_lockspace, sdp,);
	GFS_ASSERT_SBD(sdp->sd_lockstruct.ls_ops, sdp,);
	GFS_ASSERT_SBD(sdp->sd_lockstruct.ls_lvb_size >= GFS_MIN_LVB_SIZE,
		       sdp,);

	if (!*table) {
		table = p = gmalloc(sizeof(sdp->sd_vfs->s_id) + 1);
		strncpy(table, sdp->sd_vfs->s_id, sizeof(sdp->sd_vfs->s_id));
		table[sizeof(sdp->sd_vfs->s_id)] = 0;
	}

	snprintf(sdp->sd_fsname, 256, "%s.%u", table,
		 sdp->sd_lockstruct.ls_jid);

	if (p)
		kfree(p);

      out:
	if (sb)
		kfree(sb);

	return error;
}

/**
 * gfs_unmount_lockproto - Unmount lock protocol
 * @sdp: The GFS superblock
 *
 */

void
gfs_unmount_lockproto(struct gfs_sbd *sdp)
{
	lm_unmount(&sdp->sd_lockstruct);
}
