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

#include "gfs2.h"
#include "mount.h"
#include "proc.h"

/**
 * gfs2_make_args - Parse mount arguments
 * @data:
 * @args:
 *
 * Return: errno
 */

int
gfs2_make_args(char *data_arg, struct gfs2_args *args)
{
	ENTER(G2FN_MAKE_ARGS)
	char *data = data_arg;
	char *options, *x, *y;
	int error = 0;

	/*  If someone preloaded options, use those instead  */

	spin_lock(&gfs2_proc_margs_lock);
	if (gfs2_proc_margs) {
		data = gfs2_proc_margs;
		gfs2_proc_margs = NULL;
	}
	spin_unlock(&gfs2_proc_margs_lock);

	/*  Set some defaults  */

	memset(args, 0, sizeof(struct gfs2_args));
	args->ar_num_glockd = GFS2_GLOCKD_DEFAULT;
	args->ar_quota = GFS2_QUOTA_DEFAULT;
	args->ar_data = GFS2_DATA_DEFAULT;

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
				printk("GFS2: need argument to lockproto\n");
				error = -EINVAL;
				break;
			}
			strncpy(args->ar_lockproto, y, GFS2_LOCKNAME_LEN);
			args->ar_lockproto[GFS2_LOCKNAME_LEN - 1] = 0;
		}

		else if (!strcmp(x, "locktable")) {
			if (!y) {
				printk("GFS2: need argument to locktable\n");
				error = -EINVAL;
				break;
			}
			strncpy(args->ar_locktable, y, GFS2_LOCKNAME_LEN);
			args->ar_locktable[GFS2_LOCKNAME_LEN - 1] = 0;
		}

		else if (!strcmp(x, "hostdata")) {
			if (!y) {
				printk("GFS2: need argument to hostdata\n");
				error = -EINVAL;
				break;
			}
			strncpy(args->ar_hostdata, y, GFS2_LOCKNAME_LEN);
			args->ar_hostdata[GFS2_LOCKNAME_LEN - 1] = 0;
		}

		else if (!strcmp(x, "spectator"))
			args->ar_spectator = TRUE;

		else if (!strcmp(x, "ignore_local_fs"))
			args->ar_ignore_local_fs = TRUE;

		else if (!strcmp(x, "localflocks"))
			args->ar_localflocks = TRUE;

		else if (!strcmp(x, "localcaching"))
			args->ar_localcaching = TRUE;

		else if (!strcmp(x, "oopses_ok"))
			args->ar_oopses_ok = TRUE;

		else if (!strcmp(x, "debug")) {
			args->ar_oopses_ok = TRUE;
			args->ar_debug = TRUE;

		} else if (!strcmp(x, "upgrade"))
			args->ar_upgrade = TRUE;

		else if (!strcmp(x, "num_glockd")) {
			if (!y) {
				printk("GFS2: need argument to num_glockd\n");
				error = -EINVAL;
				break;
			}
			sscanf(y, "%u", &args->ar_num_glockd);
			if (!args->ar_num_glockd || args->ar_num_glockd > GFS2_GLOCKD_MAX) {
				printk("GFS2: 0 < num_glockd <= %u  (not %u)\n",
				       GFS2_GLOCKD_MAX, args->ar_num_glockd);
				error = -EINVAL;
				break;
			}
		}

		else if (!strcmp(x, "acl"))
			args->ar_posix_acl = TRUE;

		else if (!strcmp(x, "quota")) {
			if (!y) {
				printk("GFS2: need argument to quota\n");
				error = -EINVAL;
				break;
			}
			if (!strcmp(y, "off"))
				args->ar_quota = GFS2_QUOTA_OFF;
			else if (!strcmp(y, "account"))
				args->ar_quota = GFS2_QUOTA_ACCOUNT;
			else if (!strcmp(y, "on"))
				args->ar_quota = GFS2_QUOTA_ON;
			else {
				printk("GFS2: invalid argument to quota\n");
				error = -EINVAL;
				break;
			}

		} else if (!strcmp(x, "suiddir"))
			args->ar_suiddir = TRUE;

		else if (!strcmp(x, "data")) {
			if (!y) {
				printk("GFS2: need argument to data\n");
				error = -EINVAL;
				break;
			}
			if (!strcmp(y, "writeback"))
				args->ar_data = GFS2_DATA_WRITEBACK;
			else if (!strcmp(y, "ordered"))
				args->ar_data = GFS2_DATA_ORDERED;
			else {
				printk("GFS2: invalid argument to data\n");
				error = -EINVAL;
				break;
			}

		} else {
			printk("GFS2: unknown option: %s\n", x);
			error = -EINVAL;
			break;
		}
	}

	if (error)
		printk("GFS2: invalid mount option(s)\n");

	if (data != data_arg)
		kfree(data);

	RETURN(G2FN_MAKE_ARGS, error);
}

