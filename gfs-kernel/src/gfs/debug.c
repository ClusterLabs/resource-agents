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

#define WANT_DEBUG_NAMES
#include "gfs.h"

#ifdef GFS_TRACE

static unsigned char *debug_flags = NULL;

/**
 * gfs_trace_enter -
 * @flag:
 *
 * Returns: 0
 */

int
gfs_trace_enter(unsigned int flag)
{
	if (KDBL_TRACE_TEST(debug_flags, flag))
		kdbl_printf("En %s %ld\n",
			    gfs_debug_flag_names[flag], (long)current->pid);
	return 0;
}

/**
 * gfs_trace_exit -
 * @flag:
 * @dummy:
 *
 */

void
gfs_trace_exit(unsigned int flag, int dummy)
{
	if (KDBL_TRACE_TEST(debug_flags, flag))
		kdbl_printf("Ex %s %ld\n",
			    gfs_debug_flag_names[flag], (long)current->pid);
}

/**
 * gfs_trace_init -
 *
 * Returns: errno
 */

int
gfs_trace_init(void)
{
	return kdbl_trace_create_array("gfs",
				       GFS_DEBUG_VERSION, GFS_DEBUG_FLAGS,
				       &debug_flags);
}

/**
 * gfs_trace_uninit -
 *
 */

void
gfs_trace_uninit(void)
{
	kdbl_trace_destroy_array(debug_flags);
}

#endif

#ifdef GFS_PROFILE

void *gfs_profile_cookie = NULL;

/**
 * gfs_profile_init - setup profiling
 *
 */

int
gfs_profile_init(void)
{
	return kdbl_profile_create_array("gfs",
					 GFS_DEBUG_VERSION, GFS_DEBUG_FLAGS,
					 &gfs_profile_cookie);
}

/**
 * gfs_profile_uninit - cleanpu profiling
 *
 */

void
gfs_profile_uninit(void)
{
	kdbl_profile_destroy_array(gfs_profile_cookie);
}

#endif
