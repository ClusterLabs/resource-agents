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
#include <linux/file.h>
#include <linux/crc32.h>
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>

#include "handler.h"
#include "gulm_lock_queue.h"

extern gulm_cm_t gulm_cm;

/****************************************************************************/
struct gulm_flck_return_s {
	int error;
	struct completion sleep;
};

/**
 * gulm_firstlock_finish - 
 * @item: 
 * 
 * 
 * Returns: void
 */
void gulm_firstlock_finish (struct glck_req *item)
{
	struct gulm_flck_return_s *g = (struct gulm_flck_return_s *)item->misc;
	g->error = item->error;
	complete (&g->sleep);
}

/**
 * gulm_cancel_firstlock - 
 * @misc: 
 * 
 */
void gulm_cancel_firstlock (void *misc)
{
	gulm_fs_t *fs = (gulm_fs_t *)misc;
	glckr_t *item;

	item = glq_get_new_req();
	if( item == NULL ) {
		log_err ("Out of memory, Cannot cancel Firstlock request.\n");
		return;
	}

	/* after cancel is processed, glq will call kfree on item->key. */
	item->key = kmalloc(GIO_KEY_SIZE, GFP_KERNEL);
	if (item->key == NULL) {
		glq_recycle_req(item);
		log_err ("Out of memory, Cannot cancel Firstlock request.\n");
		return;
	}
	item->keylen = pack_lock_key(item->key, GIO_KEY_SIZE, 'F',
			fs->fs_name, "irstMount", 9);
	item->subid = 0;
	item->start = 0;
	item->stop = ~((uint64_t)0);
	item->type = glq_req_type_cancel;
	item->finish = NULL;

	glq_cancel(item);
}

/**
 * do_lock_time_out - 
 * @d: 
 *
 * after timeout, set cancel request on the handler queue. (since we cannot
 * call it from within the timer code. (socket io within interrupt space is
 * bad.))
 * 
 */
static void
do_lock_time_out (unsigned long d)
{
	gulm_fs_t *fs = (gulm_fs_t *)d;
	qu_function_call (&fs->cq, gulm_cancel_firstlock, fs);
}

/**
 * get_mount_lock - 
 * @fs: 
 * @first: 
 * 
 * Get the Firstmount lock.
 * We try to grab it Exl.  IF we get that, then we are the first client
 * mounting this fs.  Otherwise we grab it shared to show that there are
 * clients using this fs.
 * 
 * Returns: int
 */
int
get_mount_lock (gulm_fs_t * fs, int *first)
{
	int err, keylen;
	struct timer_list locktimeout;
	struct gulm_flck_return_s gret;
	uint8_t key[GIO_KEY_SIZE];
	glckr_t *item;

	keylen = pack_lock_key(key, GIO_KEY_SIZE, 'F', fs->fs_name, "irstMount", 9);
	if( keylen <= 0 ) return keylen;


      try_it_again:
	*first = FALSE;		/* assume we're not first */

	item = glq_get_new_req();
	if (item == NULL) {
		err = -ENOMEM;
		goto fail;
	}

	/* glq does not try to free the key for state or action requests. */
	item->key = key;
	item->keylen = keylen;
	item->subid = 0;
	item->start = 0;
	item->stop = ~((uint64_t)0);
	item->type = glq_req_type_state;
	item->state = lg_lock_state_Exclusive;
	item->flags = lg_lock_flag_Try|lg_lock_flag_IgnoreExp|lg_lock_flag_NoCallBacks;
	item->error = gret.error = 0;

	init_completion (&gret.sleep);

	item->misc = &gret;
	item->finish = gulm_firstlock_finish;

	glq_queue (item);
	wait_for_completion (&gret.sleep);

	if (gret.error == 0) {
		/* we got the lock, we're the first mounter. */
		*first = TRUE;
		log_msg (lgm_locking, "fsid=%s: Got mount lock Exclusive.\n",
			 fs->fs_name);
		return 0;
	} else {
		log_msg (lgm_locking,
			 "fsid=%s: Didn't get mount lock Exl, someone else "
			 "was first, trying for shared.\n", fs->fs_name);

		/* the try failed, pick it up shared.
		 * If it takes too long, start over.
		 * */
		init_timer (&locktimeout);
		locktimeout.function = do_lock_time_out;
		locktimeout.data = (unsigned long)fs;
		mod_timer (&locktimeout, jiffies + (120 * HZ));

		item = glq_get_new_req();
		if (item == NULL) {
			err = -ENOMEM;
			goto fail;
		}

		item->key = key;
		item->keylen = keylen;
		item->subid = 0;
		item->start = 0;
		item->stop = ~((uint64_t)0);
		item->type = glq_req_type_state;
		item->state = lg_lock_state_Shared;
		item->flags = lg_lock_flag_NoCallBacks;
		item->error = gret.error = 0;

		init_completion (&gret.sleep);

		item->misc = &gret;
		item->finish = gulm_firstlock_finish;

		glq_queue (item);
		wait_for_completion (&gret.sleep);

		del_timer (&locktimeout);

		if (gret.error == 0) {
			/* kewl we got it. */
			log_msg (lgm_locking,
				 "fsid=%s: Got mount lock shared.\n",
				 fs->fs_name);
			return 0;
		}

		log_msg (lgm_locking,
			 "fsid=%s: Shared req timed out, trying Exl again.\n",
			 fs->fs_name);
		goto try_it_again;
	}
      fail:
	log_err ("Exit get_mount_lock err=%d\n", err);
	return err;
}

/**
 * downgrade_mount_lock - 
 * @fs: 
 * 
 * drop the Firstmount lock down to shared.  This lets others mount.
 * 
 * Returns: int
 */
int
downgrade_mount_lock (gulm_fs_t * fs)
{
	int keylen;
	struct gulm_flck_return_s gret;
	uint8_t key[GIO_KEY_SIZE];
	glckr_t *item;

	keylen = pack_lock_key(key, GIO_KEY_SIZE, 'F',
			fs->fs_name, "irstMount", 9);
	if( keylen <= 0 ) return keylen;

	item = glq_get_new_req();
	if (item == NULL) {
		return -ENOMEM;
	}

	item->key = key;
	item->keylen = keylen;
	item->subid = 0;
	item->start = 0;
	item->stop = ~((uint64_t)0);
	item->type = glq_req_type_state;
	item->state = lg_lock_state_Shared;
	item->flags = lg_lock_flag_NoCallBacks;
	item->error = gret.error = 0;

	init_completion (&gret.sleep);

	item->misc = &gret;
	item->finish = gulm_firstlock_finish;

	glq_queue (item);
	wait_for_completion (&gret.sleep);

	if (gret.error != 0)
		log_err ("fsid=%s: Couldn't unlock mount lock!!!!!! %d\n",
			 fs->fs_name, gret.error);
	return 0;
}

/**
 * drop_mount_lock - drop our hold on the firstmount lock.
 * @fs: <> the filesystem pointer.
 * 
 * Returns: int
 */
int
drop_mount_lock (gulm_fs_t * fs)
{
	int keylen;
	struct gulm_flck_return_s gret;
	uint8_t key[GIO_KEY_SIZE];
	glckr_t *item;

	keylen = pack_lock_key(key, GIO_KEY_SIZE, 'F', fs->fs_name, "irstMount", 9);
	if( keylen <= 0 ) return keylen;

	item = glq_get_new_req();
	if (item == NULL) {
		return -ENOMEM;
	}

	item->key = key;
	item->keylen = keylen;
	item->subid = 0;
	item->start = 0;
	item->stop = ~((uint64_t)0);
	item->type = glq_req_type_state;
	item->state = lg_lock_state_Unlock;
	item->flags = 0;
	item->error = gret.error = 0;

	init_completion (&gret.sleep);

	item->misc = &gret;
	item->finish = gulm_firstlock_finish;

	glq_queue (item);
	wait_for_completion (&gret.sleep);

	if (gret.error != 0)
		log_err ("fsid=%s: Couldn't unlock mount lock!!!!!! %d\n",
			 fs->fs_name, gret.error);
	return 0;
}

/* vim: set ai cin noet sw=8 ts=8 : */
