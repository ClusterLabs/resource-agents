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

#include "gulm.h"

#include <linux/kernel.h>
#include <linux/sched.h>
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>

#include <linux/utsname.h>	/* for extern system_utsname */

#include "util.h"

gulm_cm_t gulm_cm;

/**
 * init_ltpx - 
 */
int
init_ltpx (void)
{
	int j;
	lock_table_t *lt = &gulm_cm.ltpx;

	INIT_LIST_HEAD (&lt->to_be_sent);
	spin_lock_init (&lt->queue_sender);
	init_waitqueue_head (&lt->send_wchan);
	lt->magic_one = 0xAAAAAAAA;
	init_MUTEX (&lt->sender);
	init_completion (&lt->startup);
	atomic_set (&lt->locks_pending, 0);
	lt->hashbuckets = 8191;
	lt->hshlk = kmalloc (sizeof (spinlock_t) * lt->hashbuckets, GFP_KERNEL);
	if (lt->hshlk == NULL)
		return -ENOMEM;
	lt->lkhsh =
	    kmalloc (sizeof (struct list_head) * lt->hashbuckets, GFP_KERNEL);
	if (lt->lkhsh == NULL) {
		kfree (lt->hshlk);
		return -ENOMEM;
	}
	for (j = 0; j < lt->hashbuckets; j++) {
		spin_lock_init (&lt->hshlk[j]);
		INIT_LIST_HEAD (&lt->lkhsh[j]);
	}
	return 0;
}

/**
 * load_info - 
 * @hostdata: < optionally override the name of this node.
 * 
 * Returns: int
 */
int
load_info (char *hostdata)
{
	int err = 0;

	if (gulm_cm.loaded)
		goto exit;

	gulm_cm.verbosity = 0;
	if (hostdata != NULL && strlen (hostdata) > 0) {
		strncpy (gulm_cm.myName, hostdata, 64);
	} else {
		strncpy (gulm_cm.myName, system_utsname.nodename, 64);
	}
	gulm_cm.myName[63] = '\0';

	/* breaking away from ccs. just hardcoding defaults here.
	 * Noone really used these anyways and if ppl want them badly, we'll
	 * find another way to set them. (modprobe options for example.)
	 * */
	gulm_cm.handler_threads = 2;
	gulm_cm.verbosity = lgm_Network | lgm_Stomith | lgm_Forking;

	init_ltpx ();

	gulm_cm.loaded = TRUE;
      exit:
	return err;
}
/* vim: set ai cin noet sw=8 ts=8 : */
