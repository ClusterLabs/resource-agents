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

#include "gulm.h"

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/smp_lock.h>
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>

#include "handler.h"

/* things about myself
 * mostly just for verbosity here.
 * */
extern gulm_cm_t gulm_cm;

/* the task struct */
typedef struct runtask_s {
	struct list_head rt_list;

	gulm_fn fn;
	lm_callback_t cb;
	lm_fsdata_t *fsdata;
	int type;
	uint64_t lmnum;
	unsigned int lmtype;
	int result;

} runtask_t;
/* ooo crufty. */
#define LM_CB_GULM_FN 169
#if LM_CB_GULM_FN == LM_CB_NEED_E || \
    LM_CB_GULM_FN == LM_CB_NEED_D || \
    LM_CB_GULM_FN == LM_CB_NEED_S || \
    LM_CB_GULM_FN == LM_CB_NEED_RECOVERY || \
    LM_CB_GULM_FN == LM_CB_DROPLOCKS || \
    LM_CB_GULM_FN == LM_CB_ASYNC
#error "LM_CB_GULM_FN collision with other LM_CB_*"
#endif

static __inline__ int
queue_empty (callback_qu_t * cq)
{
	int ret;
	spin_lock (&cq->list_lock);
	ret = list_empty (&cq->run_tasks);
	spin_unlock (&cq->list_lock);
	return ret;
}

/**
 * handler - 
 * @d: 
 * 
 * 
 * Returns: int
 */
int
handler (void *d)
{
	callback_qu_t *cq = (callback_qu_t *) d;
	runtask_t *rt;
	struct list_head *tmp;
	struct lm_lockname lockname;
	struct lm_async_cb acb;
   int err;

	daemonize ("gulm_Cb_Handler");
	atomic_inc (&cq->num_threads);
	complete (&cq->startup);

	while (cq->running) {
      err = wait_event_interruptible(cq->waiter,
            (!cq->running || !queue_empty(cq)) );
      if( err != 0 )
         flush_signals(current);

		if (!cq->running)
			break;
		/* remove item from list */
		spin_lock (&cq->list_lock);
		if (list_empty (&cq->run_tasks)) {
			spin_unlock (&cq->list_lock);
			continue;	/* nothing here. move on */
		}
		/* take items off the end of the list, since we add them to the
		 * beginning.
		 */
		tmp = (&cq->run_tasks)->prev;
		list_del (tmp);
      cq->task_count--;
		spin_unlock (&cq->list_lock);

		rt = list_entry (tmp, runtask_t, rt_list);

		if (rt->type == LM_CB_ASYNC) {
			acb.lc_name.ln_number = rt->lmnum;
			acb.lc_name.ln_type = rt->lmtype;
			acb.lc_ret = rt->result;
			rt->cb (rt->fsdata, rt->type, &acb);
		} else if (rt->type == LM_CB_GULM_FN) {
			rt->fn (rt->fsdata);
		} else {
			lockname.ln_number = rt->lmnum;
			lockname.ln_type = rt->lmtype;
			rt->cb (rt->fsdata, rt->type, &lockname);
		}

		kfree (rt);

	}			/*while(running) */

	atomic_dec (&cq->num_threads);
	complete (&cq->startup);
	return 0;
}

/**
 * display_handler_queue - 
 * @cq: 
 * 
 * remember, items are added to the head, and removed from the tail.
 * So the last item listed, is the next item to be handled.
 * 
 */
void
display_handler_queue (callback_qu_t * cq)
{
	struct list_head *lltmp;
	runtask_t *rt;
	int i = 0;
	log_msg (lgm_Always, "Dumping Handler queue with %d items, max %d\n",
		 cq->task_count, cq->task_max);
	spin_lock (&cq->list_lock);
	list_for_each (lltmp, &cq->run_tasks) {
		rt = list_entry (lltmp, runtask_t, rt_list);
		if (rt->type == LM_CB_ASYNC) {
			log_msg (lgm_Always,
				 "%4d ASYNC    (%" PRIu64 ", %u) result:%#x\n",
				 i, rt->lmnum, rt->lmtype, rt->result);
		} else if (rt->type == LM_CB_GULM_FN) {
			log_msg (lgm_Always, "%4d GULM FN  func:%p data:%p\n",
				 i, rt->fn, rt->fsdata);
		} else {	/* callback. */
			log_msg (lgm_Always,
				 "%4d CALLBACK req:%u (%" PRIu64 ", %u)\n", i,
				 rt->type, rt->lmnum, rt->lmtype);
		}
		i++;
	}
	spin_unlock (&cq->list_lock);
}

/**
 * alloc_runtask - 
 * Returns: runtask_t
 */
runtask_t *
alloc_runtask (void)
{
	runtask_t *rt;
	rt = kmalloc (sizeof (runtask_t), GFP_KERNEL);
	return rt;
}

/**
 * qu_function_call - 
 * @cq: 
 * @fn: 
 * @data: 
 * 
 * Generic function execing on the handler thread.  Mostly so I can add
 * single things quick without having to build all the details into the
 * handler queues.
 * 
 * Returns: int
 */
int
qu_function_call (callback_qu_t * cq, gulm_fn fn, void *data)
{
	runtask_t *rt;
	rt = alloc_runtask ();
	if (rt == NULL)
		return -ENOMEM;
	rt->cb = NULL;
	rt->fn = fn;
	rt->fsdata = data;
	rt->type = LM_CB_GULM_FN;
	rt->lmtype = 0;
	rt->lmnum = 0;
	rt->result = 0;
	INIT_LIST_HEAD (&rt->rt_list);
	spin_lock (&cq->list_lock);
	list_add (&rt->rt_list, &cq->run_tasks);
	cq->task_count++;
	if (cq->task_count > cq->task_max)
		cq->task_max = cq->task_count;
	spin_unlock (&cq->list_lock);
	wake_up (&cq->waiter);
	return 0;
}

/**
 * qu_async_rpl - 
 * @cq: 
 * @cb: 
 * @fsdata: 
 * @lockname: 
 * @result: 
 * 
 * 
 * Returns: int
 */
int
qu_async_rpl (callback_qu_t * cq, lm_callback_t cb, lm_fsdata_t * fsdata,
	      struct lm_lockname *lockname, int result)
{
	runtask_t *rt;
	rt = alloc_runtask ();
	if (rt == NULL)
		return -ENOMEM;
	rt->cb = cb;
	rt->fsdata = fsdata;
	rt->type = LM_CB_ASYNC;
	rt->lmtype = lockname->ln_type;
	rt->lmnum = lockname->ln_number;
	rt->result = result;
	INIT_LIST_HEAD (&rt->rt_list);
	spin_lock (&cq->list_lock);
	list_add (&rt->rt_list, &cq->run_tasks);
	cq->task_count++;
	if (cq->task_count > cq->task_max)
		cq->task_max = cq->task_count;
	spin_unlock (&cq->list_lock);
	wake_up (&cq->waiter);
	return 0;
}

/**
 * qu_drop_req - 
 * 
 * Returns: <0:Error; =0:Ok
 */
int
qu_drop_req (callback_qu_t * cq, lm_callback_t cb, lm_fsdata_t * fsdata,
	     int type, uint8_t lmtype, uint64_t lmnum)
{
	runtask_t *rt;
	rt = alloc_runtask ();
	if (rt == NULL)
		return -ENOMEM;
	rt->cb = cb;
	rt->fsdata = fsdata;
	rt->type = type;
	rt->lmtype = lmtype;
	rt->lmnum = lmnum;
	rt->result = 0;
	INIT_LIST_HEAD (&rt->rt_list);
	spin_lock (&cq->list_lock);
	list_add (&rt->rt_list, &cq->run_tasks);
	cq->task_count++;
	if (cq->task_count > cq->task_max)
		cq->task_max = cq->task_count;
	spin_unlock (&cq->list_lock);
	wake_up (&cq->waiter);
	return 0;
}

/**
 * stop_callback_qu - stop the handler thread
 */
void
stop_callback_qu (callback_qu_t * cq)
{
	struct list_head *lltmp, *tmp;
	runtask_t *rt;

	if (cq->running) {
		cq->running = FALSE;
		/* make sure all thread stop.
		 * */
		while (atomic_read (&cq->num_threads) > 0) {
			wake_up (&cq->waiter);
			wait_for_completion (&cq->startup);
		}
		/* clear out any left overs. */
		list_for_each_safe (tmp, lltmp, &cq->run_tasks) {
			rt = list_entry (tmp, runtask_t, rt_list);
			list_del (tmp);
			kfree (rt);
		}
	}
}

/**
 * start_callback_qu - 
 *
 * Returns: <0:Error, >=0:Ok
 */
int
start_callback_qu (callback_qu_t * cq, int cnt)
{
	int err;
	INIT_LIST_HEAD (&cq->run_tasks);
	spin_lock_init (&cq->list_lock);
	init_completion (&cq->startup);
	init_waitqueue_head (&cq->waiter);
	atomic_set( &cq->num_threads, 0);
	cq->running = TRUE;
	cq->task_count = 0;
	cq->task_max = 0;
	if (cnt <= 0)
		cnt = 2;
	for (; cnt > 0; cnt--) {
		err = kernel_thread (handler, cq, 0);
		if (err < 0) {
			stop_callback_qu (cq);
			/* calling stop here might not behave correctly in all error
			 * cases.
			 */
			return err;
		}
		wait_for_completion (&cq->startup);
	}
	return 0;
}
