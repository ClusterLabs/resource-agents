/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#ifndef __utils_srt_h__
#define __utils_srt_h__

#include <unistd.h>

#ifdef _POSIX_PRIORITY_SCHEDULING
void set_fifo_sched(int level);
void unset_sched(void);
#else /*_POSIX_PRIORITY_SCHEDULING*/
#define set_fifo_sched(x)
#define unset_sched()
#endif /*_POSIX_PRIORITY_SCHEDULING*/

#ifdef _POSIX_MEMLOCK
void lock_pages_down(void);
#else /*_POSIX_MEMLOCK*/
#define lock_pages_down()
#endif /*_POSIX_MEMLOCK*/

#endif /*__utils_srt_h__*/
/* vim: set ai cin et sw=3 ts=3 : */
