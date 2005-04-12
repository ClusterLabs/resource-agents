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

#ifndef __SM_DOT_H__
#define __SM_DOT_H__

/* 
 * This is the main header file to be included in each Service Manager source
 * file.
 */

#include <linux/list.h>
#include <linux/socket.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/kthread.h>
#include <net/sock.h>

#include <cluster/cnxman.h>
#include <cluster/service.h>

#define SG_LEVELS (4)

#include "sm_internal.h"
#include "sm_barrier.h"
#include "sm_control.h"
#include "sm_daemon.h"
#include "sm_joinleave.h"
#include "sm_membership.h"
#include "sm_message.h"
#include "sm_misc.h"
#include "sm_recover.h"
#include "sm_services.h"

extern struct list_head sm_sg[SG_LEVELS];
extern struct semaphore sm_sglock;

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

#define SM_ASSERT(x, do) \
{ \
  if (!(x)) \
  { \
    printk("\nSM:  Assertion failed on line %d of file %s\n" \
               "SM:  assertion:  \"%s\"\n" \
               "SM:  time = %lu\n", \
               __LINE__, __FILE__, #x, jiffies); \
    {do} \
    printk("\n"); \
    panic("SM:  Record message above and reboot.\n"); \
  } \
}

#define SM_RETRY(do_this, until_this) \
for (;;) \
{ \
  do { do_this; schedule(); } while (0); \
  if (until_this) \
    break; \
  printk("SM:  out of memory:  %s, %u\n", __FILE__, __LINE__); \
  schedule();\
}


#define log_print(fmt, args...) printk("SM: "fmt"\n", ##args)

#define log_error(sg, fmt, args...) \
	printk("SM: %08x " fmt "\n", (sg)->global_id , ##args)


#define SM_DEBUG_LOG

#ifdef SM_DEBUG_CONSOLE
#define log_debug(sg, fmt, args...) \
	printk("SM: %08x " fmt "\n", (sg)->global_id , ##args)
#endif

#ifdef SM_DEBUG_LOG
#define log_debug(sg, fmt, args...) sm_debug_log(sg, fmt, ##args);
#endif

#ifdef SM_DEBUG_ALL
#define log_debug(sg, fmt, args...) \
do \
{ \
	printk("SM: %08x "fmt"\n", (sg)->global_id, ##args); \
	sm_debug_log(sg, fmt, ##args); \
} \
while (0)
#endif

#endif				/* __SM_DOT_H__ */
