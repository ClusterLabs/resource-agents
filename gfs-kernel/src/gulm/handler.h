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

#ifndef __handler_c__
#define __handler_c__
#include <linux/lm_interface.h>

struct callback_qu_s {
	struct completion startup;
	int running;
	int task_count;
	int task_max;
	struct list_head run_tasks;
	spinlock_t list_lock;
	wait_queue_head_t waiter;
	atomic_t num_threads;
};
typedef struct callback_qu_s callback_qu_t;

/* kinda an excess overloading */
typedef void (*gulm_fn) (void *);
int qu_function_call (callback_qu_t * cq, gulm_fn fn, void *data);

int qu_async_rpl (callback_qu_t * cq, lm_callback_t cb, lm_fsdata_t * fsdata,
		  struct lm_lockname *lockname, int result);
int qu_drop_req (callback_qu_t * cq, lm_callback_t cb, lm_fsdata_t * fsdata,
		 int type, uint8_t lmtype, uint64_t lmnum);
int start_callback_qu (callback_qu_t * cq, int cnt);
void stop_callback_qu (callback_qu_t * cq);
void display_handler_queue (callback_qu_t * cq);

#endif /*__handler_c__*/
