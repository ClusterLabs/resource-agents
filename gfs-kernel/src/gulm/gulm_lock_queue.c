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
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/file.h>
#include <linux/smp_lock.h>
#include <linux/crc32.h>
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>

#include "handler.h"

#include "gulm_lock_queue.h"

/* The Queues. */
struct list_head glq_Free;
spinlock_t glq_FreeLock;
unsigned int glq_FreeCount;
struct list_head glq_OutQueue;
spinlock_t glq_OutLock;
unsigned int glq_OutCount;
/* Not sure that ReplyMap really needs to be this big.  Things shouldn't be
 * on it that long.  maybe 1<<8?  must test and see. later.
 */
#define ReplyMapSize (1<<13)  /* map size is a power of 2 */
#define ReplyMapBits (0x1FFF) /* & is faster than % */
struct list_head *glq_ReplyMap;
spinlock_t *glq_ReplyLock;

/* The Threads. */
struct task_struct *glq_recver_task = NULL;
struct task_struct *glq_sender_task = NULL;
struct completion glq_startedup;
int glq_running;
wait_queue_head_t glq_send_wchan;

/* */
extern gulm_cm_t gulm_cm;

/* The code. */
/**
 * glq_init - 
 * 
 * Returns: int
 */
int glq_init(void)
{
	int i;

	glq_running = FALSE;
	glq_recver_task = NULL;
	glq_sender_task = NULL;
	init_waitqueue_head (&glq_send_wchan);
	init_completion (&glq_startedup);

	INIT_LIST_HEAD (&glq_Free);
	spin_lock_init (&glq_FreeLock);
	glq_FreeCount = 0;
	INIT_LIST_HEAD (&glq_OutQueue);
	spin_lock_init (&glq_OutLock);
	glq_OutCount = 0;

	glq_ReplyMap = vmalloc(sizeof(struct list_head) * ReplyMapSize);
	if( glq_ReplyMap == NULL ) {
		return -ENOMEM;
	}
	glq_ReplyLock = vmalloc(sizeof(spinlock_t) * ReplyMapSize);
	if( glq_ReplyLock == NULL ) {
		vfree(glq_ReplyMap);
		return -ENOMEM;
	}
	for(i=0; i < ReplyMapSize; i++) {
		INIT_LIST_HEAD (&glq_ReplyMap[i]);
		spin_lock_init (&glq_ReplyLock[i]);
	}
	/* ?Add some empty reqs to the Free list right now? */
	return 0;
}

/**
 * glq_release - 
 *
 * doesn't grab spins, because by the time this is called, there should be
 * no other threads anywhere that could possibly be working on these lists.
 * 
 * Returns: void
 */
void glq_release(void)
{
	struct list_head *tmp, *lltmp;
	glckr_t *item;
	int i;

	list_for_each_safe (tmp, lltmp, &glq_OutQueue) {
		item = list_entry (tmp, glckr_t, list);
		list_del (tmp);
		if (item->key != NULL) kfree (item->key);
		if (item->lvb != NULL) kfree (item->lvb);
		kfree (item);
	}
	glq_FreeCount = 0;
	list_for_each_safe (tmp, lltmp, &glq_Free) {
		item = list_entry (tmp, glckr_t, list);
		list_del (tmp);
		if (item->key != NULL) kfree (item->key);
		if (item->lvb != NULL) kfree (item->lvb);
		kfree (item);
	}
	glq_OutCount = 0;
	for(i=0; i < ReplyMapSize; i++) {
		list_for_each_safe (tmp, lltmp, &glq_ReplyMap[i]) {
			item = list_entry (tmp, glckr_t, list);
			list_del (tmp);
			if (item->key != NULL) kfree (item->key);
			if (item->lvb != NULL) kfree (item->lvb);
			kfree (item);
		}
	}

	vfree(glq_ReplyLock);
	vfree(glq_ReplyMap);
}

/**
 * glq_get_new_req - 
 *
 * WARNING! For state and action requests, glq will not free the key or
 * lvb pointers.  For drop and cancel glq WILL free the pointer when it is
 * finished.
 * 
 * Returns: glckr_t
 */
glckr_t *glq_get_new_req(void)
{
	struct list_head *tmp;
	glckr_t *item = NULL;

	/* try to reclaim a recycled req first. */
	spin_lock (&glq_FreeLock);
	if (!list_empty (&glq_Free)) {
		tmp = glq_Free.next;
		list_del (tmp);
		item = list_entry (tmp, glckr_t, list);
		glq_FreeCount --;
	}
	spin_unlock (&glq_FreeLock);

	/* nothing on Free list, make new. */
	if (item == NULL) {
		item = kmalloc(sizeof(glckr_t), GFP_KERNEL);
		if (item == NULL) 
			return NULL;
		memset(item, 0, sizeof(glckr_t));
	}

	/* initialize.
	 * reset list so its good.
	 */
	INIT_LIST_HEAD (&item->list);

	return item;
}

/**
 * glq_recycle_req - 
 * @lckr_t: 
 * 
 * assumes that item is not on any lists.
 * 
 * Returns: void
 */
void glq_recycle_req(glckr_t *item)
{
	/* clean it up */
	INIT_LIST_HEAD (&item->list);

	if (item->type == glq_req_type_drop ||
	    item->type == glq_req_type_cancel) {
		if (item->key != NULL) {
			kfree(item->key);
			item->key = NULL;
		}
		if (item->lvb != NULL) {
			kfree(item->lvb);
			item->lvb = NULL;
		}
	} else {
		item->key = NULL;
		item->lvb = NULL;
	}
	item->misc = NULL;
	item->finish = NULL;

	/* everything else is ignoreable. */

	/* onto the Free list. unless too many. */
	spin_lock (&glq_FreeLock);
	if (glq_FreeCount > 20) { /* XXX icky hidden constant */
		kfree (item);
	}else{
		list_add (&item->list, &glq_Free);
		glq_FreeCount ++;
	}
	spin_unlock (&glq_FreeLock);
}

/**
 * glq_calc_hash_key_long - 
 * @key: 
 * @keylen: 
 * @subid: 
 * @start: 
 * @stop: 
 * 
 * 
 * Returns: int
 */
int glq_calc_hash_key_long(uint8_t *key, uint16_t keylen,
		uint64_t subid, uint64_t start, uint64_t stop)
{
	int ret = GULM_CRC_INIT;
	ret = crc32 (ret, &keylen, sizeof(uint16_t));
	ret = crc32 (ret, key, keylen);
	ret = crc32 (ret, &subid, sizeof(uint64_t));
	ret = crc32 (ret, &start, sizeof(uint64_t));
	ret = crc32 (ret, &stop, sizeof(uint64_t));
	ret &= ReplyMapBits;
	return ret;
}

/**
 * glq_calc_hash_key - 
 * @item: 
 * 
 * 
 * Returns: int
 */
int glq_calc_hash_key(glckr_t *item)
{
	return glq_calc_hash_key_long (item->key, item->keylen, item->subid,
			item->start, item->stop);
}

/**
 * glq_queue - 
 * @item: 
 * 
 * 
 * Returns: void
 */
void glq_queue(glckr_t *item)
{
	spin_lock (&glq_OutLock);
	list_add (&item->list, &glq_OutQueue);
	glq_OutCount++;
	spin_unlock (&glq_OutLock);
	wake_up (&glq_send_wchan);
}

/**
 * glq_cancel - 
 * @item: 
 * 
 * You MUST call glq_get_new_req() and fill that with the info of the
 * request you want to cancel.
 * 
 * Returns: void
 */
void glq_cancel(glckr_t *cancel)
{
	int found = FALSE;
	struct list_head *tmp, *lltmp;
	glckr_t *item;

	spin_lock (&glq_OutLock);
	list_for_each_safe (tmp, lltmp, &glq_OutQueue) {
		item = list_entry (tmp, glckr_t, list);
		if (item->subid == cancel->subid &&
		    item->start == cancel->start &&
		    item->stop  == cancel->stop &&
		    item->keylen == cancel->keylen &&
		    memcmp(item->key, cancel->key, cancel->keylen) ) {
			/* found it. */
			list_del (tmp);
			found = TRUE;
			item->error = lg_err_Canceled;
			if (item->finish != NULL )
				item->finish (item);
			glq_recycle_req (item);
			break;
		}
	}
	spin_unlock(&glq_OutLock);

	if (!found) {
		cancel->type = glq_req_type_cancel;
		glq_queue (cancel);
	}
}

/**
 * glq_send_queue_empty - 
 * 
 * Returns: int
 */
static int glq_send_queue_empty(void)
{
	int ret;
	spin_lock (&glq_OutLock);
	ret = list_empty (&glq_OutQueue);
	spin_unlock (&glq_OutLock);
	return ret;
}

/**
 * glq_sender_thread - 
 * @data: 
 * 
 * 
 * Returns: int
 */
int glq_sender_thread(void *data)
{
	int err=0, bucket;
	struct list_head *tmp;
	glckr_t *item = NULL;
	DECLARE_WAITQUEUE (__wait_chan, current);

	daemonize ("gulm_glq_sender");
	glq_sender_task = current;
	complete (&glq_startedup);

	while (glq_running) {
		/* wait for item */
		current->state = TASK_INTERRUPTIBLE; 
		add_wait_queue (&glq_send_wchan, &__wait_chan);
		if( glq_send_queue_empty () )
			schedule ();
		remove_wait_queue (&glq_send_wchan, &__wait_chan);
		current->state = TASK_RUNNING;
		if (!glq_running) break;

		/* pull item off queue */
		spin_lock (&glq_OutLock);
		if (list_empty (&glq_OutQueue) ) {
			spin_unlock (&glq_OutLock);
			continue;
		}
		tmp = glq_OutQueue.prev;
		list_del (tmp);
		glq_OutCount--;
		spin_unlock (&glq_OutLock);
		item = list_entry (tmp, glckr_t, list);

		/* send to local ltpx or die */
		if (item->type == glq_req_type_state ) {
			INIT_LIST_HEAD (&item->list);
			bucket = glq_calc_hash_key(item);
			spin_lock (&glq_ReplyLock[bucket]);
			list_add (&item->list, &glq_ReplyMap[bucket]);
			spin_unlock (&glq_ReplyLock[bucket]);
			err = lg_lock_state_req (gulm_cm.hookup, item->key,
					item->keylen, item->subid, item->start,
					item->stop, item->state, item->flags,
					item->lvb, item->lvblen);
		} else if (item->type == glq_req_type_action) {
			INIT_LIST_HEAD (&item->list);
			bucket = glq_calc_hash_key(item);
			spin_lock (&glq_ReplyLock[bucket]);
			list_add (&item->list, &glq_ReplyMap[bucket]);
			spin_unlock (&glq_ReplyLock[bucket]);
			err = lg_lock_action_req (gulm_cm.hookup, item->key,
					item->keylen, item->subid, item->state,
					item->lvb, item->lvblen);
		} else if (item->type == glq_req_type_query ) {
			INIT_LIST_HEAD (&item->list);
			bucket = glq_calc_hash_key(item);
			spin_lock (&glq_ReplyLock[bucket]);
			list_add (&item->list, &glq_ReplyMap[bucket]);
			spin_unlock (&glq_ReplyLock[bucket]);
			err = lg_lock_query_req (gulm_cm.hookup, item->key,
					item->keylen, item->subid, item->start,
					item->stop, item->state);
		} else if (item->type == glq_req_type_drop) {
			err = lg_lock_drop_exp (gulm_cm.hookup, item->lvb,
					item->key, item->keylen);
			/* drop exp has no reply. */
			glq_recycle_req (item);
		} else if (item->type == glq_req_type_cancel) {
			err = lg_lock_cancel_req (gulm_cm.hookup, item->key,
					item->keylen, item->subid);
			/* cancels have no reply. */
			glq_recycle_req (item);
		} else {
			/* bad type. */
			log_err ("Unknown send type %d, tossing request.\n",
					item->type);
			glq_recycle_req (item);
		}
		if (err != 0 ) {
			log_err ("gulm_glq_sender error %d\n", err);
			glq_running = FALSE;
			glq_recycle_req (item);
			break;
		}
	}
	complete (&glq_startedup);
	return 0;
}

/**
 * glq_login_reply - 
 * @misc: 
 * @err: 
 * @which: 
 * 
 * 
 * Returns: int
 */
int glq_login_reply (void *misc, uint32_t error, uint8_t which)
{
	if (error != 0) {
		glq_running = FALSE;
		log_err ("glq: Got error %d from login request.\n", error);
	}
	return error;
}

/**
 * glq_logout_reply - 
 * @misc: 
 * 
 * 
 * Returns: int
 */
int glq_logout_reply (void *misc)
{
	glq_running = FALSE; /* if it isn't already. */
	return 0;
}

/**
 * glq_lock_state - 
 * @misc: 
 * @key: 
 * @keylen: 
 * @state: 
 * @flags: 
 * @error: 
 * @LVB: 
 * @LVBlen: 
 * 
 * 
 * Returns: int
 */
int
glq_lock_state (void *misc, uint8_t * key, uint16_t keylen,
		    uint64_t subid, uint64_t start, uint64_t stop,
		    uint8_t state, uint32_t flags, uint32_t error,
		    uint8_t * LVB, uint16_t LVBlen)
{
	int bucket, found = FALSE;
	struct list_head *tmp;
	glckr_t *item=NULL;

	/* lookup and remove from ReplyMap */
	bucket = glq_calc_hash_key_long(key, keylen, subid, start, stop);
	spin_lock (&glq_ReplyLock[bucket]);
	list_for_each(tmp, &glq_ReplyMap[bucket]) {
		item = list_entry (tmp, glckr_t, list);
		if (item->subid == subid &&
		    item->start == start &&
		    item->stop  == stop &&
		    item->keylen == keylen &&
		    memcmp(item->key, key, keylen) == 0 ) {
			/* found it. */
			list_del (tmp);
			found = TRUE;
			break;
		}
	}
	spin_unlock(&glq_ReplyLock[bucket]);

	if( !found ) {
		/* not found complaint */
		return 0;
	}

	/* restuff results */
	item->state = state;
	item->flags = flags;
	item->error = error;
	if (item->lvb != NULL && LVB != NULL) {
		item->lvblen = MIN(item->lvblen, LVBlen);
		memcpy(item->lvb, LVB, item->lvblen);
	}

	/* call finish */
	if (item->finish != NULL) item->finish (item);

	/* put on Free */
	glq_recycle_req(item);
	return 0;
}

/**
 * glq_lock_action - 
 * @misc: 
 * @key: 
 * @keylen: 
 * @action: 
 * @error: 
 * 
 * 
 * Returns: int
 */
int
glq_lock_action (void *misc, uint8_t * key, uint16_t keylen,
		     uint64_t subid, uint8_t action, uint32_t error)
{
	int bucket, found = FALSE;
	struct list_head *tmp;
	glckr_t *item = NULL;

	/* lookup and remove from ReplyMap */
	bucket = glq_calc_hash_key_long(key, keylen, subid, 0, ~((uint64_t)0));
	spin_lock (&glq_ReplyLock[bucket]);
	list_for_each(tmp, &glq_ReplyMap[bucket]) {
		item = list_entry (tmp, glckr_t, list);
		if (item->subid == subid &&
		    item->start == 0 &&
		    item->stop  == ~((uint64_t)0) &&
		    item->keylen == keylen &&
		    memcmp(item->key, key, keylen) == 0 ) {
			/* found it. */
			list_del (tmp);
			found = TRUE;
			break;
		}
	}
	spin_unlock(&glq_ReplyLock[bucket]);

	if( !found ) {
		/* not found complaint */
		return 0;
	}

	/* restuff results */
	item->error = error;

	/* call finish */
	if (item->finish != NULL) item->finish (item);

	/* put on Free */
	glq_recycle_req(item);
	return 0;
}

/**
 * glq_lock_query -
 * this is an ugly interface.....
 * there is somehtign that needs to be done here to clean things up.  I'm
 * not sure what that is right now, and I need to have somehting working.
 * So we're going with this for now.
 *
 */
int 
glq_lock_query (void *misc, uint8_t * key, uint16_t keylen,
		   uint64_t subid, uint64_t start, uint64_t stop,
		   uint8_t state, uint32_t error, uint8_t * cnode,
		   uint64_t csubid, uint64_t cstart, uint64_t cstop,
		   uint8_t cstate)
{
	int bucket, found = FALSE;
	struct list_head *tmp;
	glckr_t *item = NULL;

	/* lookup and remove from ReplyMap */
	bucket = glq_calc_hash_key_long(key, keylen, subid, start, stop);
	spin_lock (&glq_ReplyLock[bucket]);
	list_for_each(tmp, &glq_ReplyMap[bucket]) {
		item = list_entry (tmp, glckr_t, list);
		if (item->subid == subid &&
		    item->start == start &&
		    item->stop  == stop &&
		    item->keylen == keylen &&
		    memcmp(item->key, key, keylen) == 0 ) {
			/* found it. */
			list_del (tmp);
			found = TRUE;
			break;
		}
	}
	spin_unlock(&glq_ReplyLock[bucket]);

	if( !found ) {
		/* not found complaint */
		return 0;
	}

	/* restuff results */
	item->error = error;
	item->subid = csubid;
	item->start = cstart;
	item->stop = cstop;
	item->state = cstate;

	/* call finish */
	if (item->finish != NULL) item->finish (item);

	/* put on Free */
	glq_recycle_req(item);
	return 0;
}

/**
 * glq_drop_lock_req - 
 * @misc: 
 * @key: 
 * @keylen: 
 * @state: 
 * 
 * 
 * Returns: int
 */
int
glq_drop_lock_req (void *misc, uint8_t * key, uint16_t keylen,
		       uint64_t subid, uint8_t state)
{
	do_drop_lock_req (key, keylen, state);
	jid_header_lock_drop (key, keylen);
	return 0;
}

/**
 * glq_drop_all - 
 * @misc: 
 * 
 * 
 * Returns: int
 */
int glq_drop_all (void *misc)
{
	passup_droplocks ();
	return 0;
}

/**
 * glq_error - 
 * @misc: 
 * @error: 
 * 
 * 
 * Returns: int
 */
int glq_error (void *misc, uint32_t error)
{
	log_err ("glq: weird last gasp error %d\n", error);
	return error;
}

static lg_lockspace_callbacks_t glq_lock_ops = {
      login_reply:glq_login_reply,
      logout_reply:glq_logout_reply,
      lock_state:glq_lock_state,
      lock_action:glq_lock_action,
      lock_query:glq_lock_query,
      drop_lock_req:glq_drop_lock_req,
      drop_all:glq_drop_all,
      error:glq_error
};
/**
 * glq_recving_thread - 
 * @data: 
 * 
 * 
 * Returns: int
 */
int glq_recving_thread(void *data)
{
	int err;
	daemonize ("gulm_glq_recver");
	glq_recver_task = current;
	complete (&glq_startedup);

	while (glq_running) {
		err = lg_lock_handle_messages (gulm_cm.hookup, &glq_lock_ops, NULL);
		if (err != 0) {
			log_err ("gulm_glq_recver error %d\n", err);
			glq_running = FALSE;
			wake_up (&glq_send_wchan);
			break;
		}
	}
	complete (&glq_startedup);
	return 0;
}

/**
 * glq_shutdown - 
 * 
 * Returns: void
 */
void glq_shutdown(void)
{
	if (glq_running) glq_running = FALSE;
	if (glq_sender_task != NULL) {
		wake_up (&glq_send_wchan);
		wait_for_completion (&glq_startedup);
		glq_sender_task = NULL;
	}
	if (glq_recver_task != NULL) {
		lg_lock_logout (gulm_cm.hookup);
		wait_for_completion (&glq_startedup);
		glq_recver_task = NULL;
	}
}

/**
 * glq_startup - 
 * 
 * Returns: int
 */
int glq_startup(void)
{
	int err;

	if (glq_running) return 0;

	err = lg_lock_login (gulm_cm.hookup, "GFS ");
	if (err != 0) {
		log_err ("Failed to send lock login. %d\n", err);
		return -err;
	}

	glq_running = TRUE;
	if( glq_recver_task == NULL ) {
		err = kernel_thread (glq_recving_thread, NULL, 0);
		if( err < 0 ) {
			log_err ("Failed to start glq_recving_thread %d\n",
					err);
			glq_shutdown();
			return err;
		}
		wait_for_completion (&glq_startedup);
	}

	if (glq_sender_task == NULL) {
		err = kernel_thread (glq_sender_thread, NULL, 0);
		if( err < 0 ) {
			log_err ("Failed to start glq_sender_thread %d\n",
					err);
			glq_shutdown();
			return err;
		}
		wait_for_completion (&glq_startedup);
	}
	return 0;
}

/* vim: set ai cin noet sw=8 ts=8 : */
