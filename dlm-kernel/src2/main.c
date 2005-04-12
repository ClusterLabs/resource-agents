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

#define EXPORT_SYMTAB

#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include <linux/module.h>
#include <net/sock.h>

#include "dlm_internal.h"
#include "lockspace.h"
#include "member.h"
#include "lock.h"
#include "device.h"
#include "memory.h"

void dlm_register_debugfs(void);
void dlm_unregister_debugfs(void);

int __init init_dlm(void)
{
	dlm_memory_init();
	dlm_lockspace_init();
	dlm_member_init();
	dlm_register_debugfs();
	dlm_device_init();

	printk("DLM %s (built %s %s) installed\n",
	       DLM_RELEASE_NAME, __DATE__, __TIME__);

	return 0;
}

void __exit exit_dlm(void)
{
	dlm_member_exit();
	dlm_lockspace_exit();
	dlm_device_exit();
	dlm_memory_exit();
	dlm_unregister_debugfs();
}

MODULE_DESCRIPTION("Distributed Lock Manager " DLM_RELEASE_NAME);
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

module_init(init_dlm);
module_exit(exit_dlm);

EXPORT_SYMBOL(dlm_new_lockspace);
EXPORT_SYMBOL(dlm_release_lockspace);
EXPORT_SYMBOL(dlm_lock);
EXPORT_SYMBOL(dlm_unlock);
