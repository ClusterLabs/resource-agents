#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>

#include "gfs.h"
#include "mount.h"
#include "proc.h"
#include "sys.h"

/**
 * gfs_make_args - Parse mount arguments
 * @data:
 * @args:
 *
 * Return: errno
 */

int
gfs_make_args(char *data_arg, struct gfs_args *args, int remount)
{
	char *data = data_arg;
	char *options, *x, *y;
	int error = 0;

	/*  If someone preloaded options, use those instead  */

	spin_lock(&gfs_proc_margs_lock);
	if (!remount && gfs_proc_margs) {
		data = gfs_proc_margs;
		gfs_proc_margs = NULL;
	}
	spin_unlock(&gfs_proc_margs_lock);

	/*  Set some defaults  */

	memset(args, 0, sizeof(struct gfs_args));
	args->ar_num_glockd = GFS_GLOCKD_DEFAULT;

	if (!remount) {
		/*  If someone preloaded options, use those instead  */
		spin_lock(&gfs_sys_margs_lock);
		if (gfs_sys_margs) {
			data = gfs_sys_margs;
			gfs_sys_margs = NULL;
		}
		spin_unlock(&gfs_sys_margs_lock);
		
		/*  Set some defaults  */
		args->ar_num_glockd = GFS_GLOCKD_DEFAULT;
	}

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
			strncpy(args->ar_lockproto, y, GFS_LOCKNAME_LEN);
			args->ar_lockproto[GFS_LOCKNAME_LEN - 1] = 0;
		}

		else if (!strcmp(x, "locktable")) {
			if (!y) {
				printk("GFS: need argument to locktable\n");
				error = -EINVAL;
				break;
			}
			strncpy(args->ar_locktable, y, GFS_LOCKNAME_LEN);
			args->ar_locktable[GFS_LOCKNAME_LEN - 1] = 0;
		}

		else if (!strcmp(x, "hostdata")) {
			if (!y) {
				printk("GFS: need argument to hostdata\n");
				error = -EINVAL;
				break;
			}
			strncpy(args->ar_hostdata, y, GFS_LOCKNAME_LEN);
			args->ar_hostdata[GFS_LOCKNAME_LEN - 1] = 0;
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

		else if (!strcmp(x, "noacl"))
			args->ar_posix_acls = FALSE;

		else if (!strcmp(x, "suiddir"))
			args->ar_suiddir = TRUE;

		else if (!strcmp(x, "noquota"))
			args->ar_noquota = TRUE;

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

