/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/init.h>

#include "lock_dlm.h"

int init_lock_dlm()
{
	int error;

	error = gfs_register_lockproto(&gdlm_ops);
	if (error) {
		printk(KERN_WARNING "lock_dlm:  can't register protocol: %d\n",
		       error);
		return error;
	}

	error = gdlm_sysfs_init();
	if (error) {
		gfs_unregister_lockproto(&gdlm_ops);
		return error;
	}

	printk(KERN_INFO
	       "Lock_DLM (built %s %s) installed\n", __DATE__, __TIME__);
	return 0;
}

void exit_lock_dlm()
{
	gdlm_sysfs_exit();
	gfs_unregister_lockproto(&gdlm_ops);
}
