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
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sched.h>
#include <sys/mman.h>
#include "gulm_defines.h"

/* soft-real-time */
#ifdef _POSIX_PRIORITY_SCHEDULING
/**
 * set_fifo_sched - 
 * @level: 1 2 3
 *
 * switch to fifo schedulling at a mid-range priority.
 */
void set_fifo_sched(int level)
{
   struct sched_param sp;
   int err, min, max, mid;

   min = sched_get_priority_min(SCHED_FIFO);
   max = sched_get_priority_max(SCHED_FIFO);

   mid = (( max - min ) / 2) + min;
   mid += level;
   sp.sched_priority = MAX(max, mid);

   err = sched_setscheduler(0, SCHED_FIFO, &sp);
   if( err != 0 )
      log_err("Failed to switch to FIFO schedulling. %d:%s\n",
            errno, strerror(errno));
}

void unset_sched(void)
{
   struct sched_param sp;
   int err;
   sp.sched_priority = 0;
   err = sched_setscheduler(0, SCHED_OTHER, &sp);
   if( err != 0 )
      log_err("Failed to switch to FIFO schedulling. %d:%s\n",
            errno, strerror(errno));
}
#endif /*_POSIX_PRIORITY_SCHEDULING*/

#ifdef _POSIX_MEMLOCK
/**
 * big_stack_space - 
 * try to grow the stack space so it is big enough that we won't need more
 * in the future.
 */
#define BIGSTACK (2*1024*1024)
static void big_stack_space(void)
{
   uint8_t temp[BIGSTACK];
   memset(temp, 42, BIGSTACK - 1);
   /* NOTE: If running within valgrind, this function seems to cause write
    * errors.
    */
}

/**
 * lock_pages_down - 
 * swapping is bad, mmkay?
 */
void lock_pages_down(void)
{
   big_stack_space();

   /* lock down current pages. */
   if( mlockall(MCL_CURRENT) != 0 )
      log_err("Failed to mlockall(MCL_CURRENT). %d:%s\n",
            errno, strerror(errno));

   /* lock down future pages.
    * evil occures if there is not enough physical memory for this.
    * really need this?
    */
   if( mlockall(MCL_FUTURE) != 0 )
      log_err("Failed to mlockall(MCL_FUTURE). %d:%s\n",
            errno, strerror(errno));
}
#endif /*_POSIX_MEMLOCK*/

/* vim: set ai cin et sw=3 ts=3 : */
