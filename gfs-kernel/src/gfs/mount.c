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
#include "mount.h"
#include "proc.h"
#include "super.h"

/**
 * gfs_make_args - Parse mount arguments
 * @data:
 * @args:
 *
 * Return: 0 on success, -EXXX on failure
 */

int
gfs_make_args(char *data_arg, struct gfs_args *args)
{
	char *data = data_arg;
	char *options, *x, *y;
	int error = 0;

	/*  If someone preloaded options, use those instead  */

	spin_lock(&gfs_proc_margs_lock);
	if (gfs_proc_margs) {
		data = gfs_proc_margs;
		gfs_proc_margs = NULL;
	}
	spin_unlock(&gfs_proc_margs_lock);

	/*  Set some defaults  */

	memset(args, 0, sizeof(struct gfs_args));
	args->ar_num_glockd = GFS_GLOCKD_DEFAULT;

	/*  Split the options into tokens with the "," character and
	    process them  */

	for (options = data; (x = strsep(&options, ",")); ) {
		if (!*x)
			continue;

		y = strchr(x, '=');
		if (y)
			*y++ = 0;

		if (!strcmp(x, "lockproto")) {
			if (!y) {
				printk("GFS: need argument to lockproto\n");
				error = -EINVAL;
				break;
			}
			strncpy(args->ar_lockproto, y, 256);
			args->ar_lockproto[255] = 0;
		}

		else if (!strcmp(x, "locktable")) {
			if (!y) {
				printk("GFS: need argument to locktable\n");
				error = -EINVAL;
				break;
			}
			strncpy(args->ar_locktable, y, 256);
			args->ar_locktable[255] = 0;
		}

		else if (!strcmp(x, "hostdata")) {
			if (!y) {
				printk("GFS: need argument to hostdata\n");
				error = -EINVAL;
				break;
			}
			strncpy(args->ar_hostdata, y, 256);
			args->ar_hostdata[255] = 0;
		}

		else if (!strcmp(x, "ignore_local_fs"))
			args->ar_ignore_local_fs = TRUE;

		else if (!strcmp(x, "localflocks"))
			args->ar_localflocks = TRUE;

		else if (!strcmp(x, "localcaching"))
			args->ar_localcaching = TRUE;

		else if (!strcmp(x, "upgrade"))
			args->ar_upgrade = TRUE;

		else if (!strcmp(x, "num_glockd")) {
			if (!y) {
				printk("GFS: need argument to num_glockd\n");
				error = -EINVAL;
				break;
			}
			sscanf(y, "%u", &args->ar_num_glockd);
			if (!args->ar_num_glockd || args->ar_num_glockd > GFS_GLOCKD_MAX) {
				printk("GFS: 0 < num_glockd <= %u  (not %u)\n",
				       GFS_GLOCKD_MAX, args->ar_num_glockd);
				error = -EINVAL;
				break;
			}
		}

		else if (!strcmp(x, "acl"))
			args->ar_posix_acls = TRUE;

		else if (!strcmp(x, "suiddir"))
			args->ar_suiddir = TRUE;

		/*  Unknown  */

		else {
			printk("GFS: unknown option: %s\n", x);
			error = -EINVAL;
			break;
		}
	}

	if (error)
		printk("GFS: invalid mount option(s)\n");

	if (data != data_arg)
		kfree(data);

	return error;
}

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
	char *proto, *table;
	int error;

	proto = sdp->sd_args.ar_lockproto;
	table = sdp->sd_args.ar_locktable;

	/*  Try to autodetect  */

	if (!proto[0] || !table[0]) {
		struct buffer_head *bh;

		error = gfs_dread(sdp, GFS_SB_ADDR >> sdp->sd_fsb2bb_shift, NULL,
				  DIO_FORCE | DIO_START | DIO_WAIT, &bh);
		if (error)
			return error;

		sb = kmalloc(sizeof(struct gfs_sb), GFP_KERNEL);
		if (!sb) {
			brelse(bh);
			return -ENOMEM;
		}
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

	printk("GFS: Trying to join cluster \"%s\", \"%s\"\n",
	       proto, table);

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

	snprintf(sdp->sd_fsname, 256, "%s.%u",
		 (*table) ? table : sdp->sd_vfs->s_id,
		 sdp->sd_lockstruct.ls_jid);

	printk("GFS: fsid=%s: Joined cluster. Now mounting FS...\n",
	       sdp->sd_fsname);

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
