/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "dlm_internal.h"
#include "lockspace.h"
#include "member_sysfs.h"
#include "lock.h"
#include "device.h"
#include "memory.h"
#include "lowcomms.h"

#ifdef CONFIG_DLM_DEBUG
int dlm_register_debugfs(void);
void dlm_unregister_debugfs(void);
#else
int dlm_register_debugfs(void)
{
	return 0;
}

void dlm_unregister_debugfs(void)
{
}
#endif

int dlm_node_ioctl_init(void);
void dlm_node_ioctl_exit(void);

int __init init_dlm(void)
{
	int error;

	error = dlm_memory_init();
	if (error)
		goto out;

	error = dlm_lockspace_init();
	if (error)
		goto out_mem;

	error = dlm_node_ioctl_init();
	if (error)
		goto out_mem;

	error = dlm_member_sysfs_init();
	if (error)
		goto out_node;

	error = dlm_register_debugfs();
	if (error)
		goto out_member;

	error = dlm_lowcomms_init();
	if (error)
		goto out_debug;

	printk("DLM (built %s %s) installed\n", __DATE__, __TIME__);

	return 0;

 out_debug:
	dlm_unregister_debugfs();
 out_member:
	dlm_member_sysfs_exit();
 out_node:
	dlm_node_ioctl_exit();
 out_mem:
	dlm_memory_exit();
 out:
	return error;
}

void __exit exit_dlm(void)
{
	dlm_lowcomms_exit();
	dlm_member_sysfs_exit();
	dlm_node_ioctl_exit();
	dlm_memory_exit();
	dlm_unregister_debugfs();
}

module_init(init_dlm);
module_exit(exit_dlm);

MODULE_DESCRIPTION("Distributed Lock Manager");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL_GPL(dlm_new_lockspace);
EXPORT_SYMBOL_GPL(dlm_release_lockspace);
EXPORT_SYMBOL_GPL(dlm_lock);
EXPORT_SYMBOL_GPL(dlm_unlock);

