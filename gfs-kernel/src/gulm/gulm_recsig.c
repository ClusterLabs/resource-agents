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
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>

#include "gulm_lock_queue.h"

/* This is some speed hackery to abuse the locking system to allow clients
 * to notify each other of things.  It is a super simple signaling type
 * system.  All you can know is that a signal was touched.  Pretty
 * sreightforword.
 *
 * I really would rather have something else.  I've a couple of good ideas,
 * and will most likely switch to one of those at a later date.  But this
 * is good enough for now, and will get us through the next release.
 *
 * (functionally nothing wrong with this, theoretically, I can think of
 * better designs than this abuse.)
 */

extern gulm_cm_t gulm_cm;

struct sig_watcher {
	struct list_head sw_list;
	uint8_t *name;
	uint8_t len;
	void(*func)(void *misc);
	void *misc;
};
struct list_head sig_watchers_list;
spinlock_t sig_watchers_lock;

/****************************************************************************/
/* internal funcs */
/**
 * release_sw - 
 * @name: 
 * @len: 
 * 
 * 
 * Returns: void
 */
static void release_sw(uint8_t *name, uint8_t len)
{
	struct list_head *tmp;
	struct sig_watcher *sw;
	spin_lock(&sig_watchers_lock);
	list_for_each(tmp, &sig_watchers_list) {
		sw = list_entry (tmp, struct sig_watcher, sw_list);
		if( memcmp(name, sw->name, len) == 0 ) {
			list_del(tmp);
			kfree(sw->name);
			kfree(sw);
			break;
		}
	}
	spin_unlock(&sig_watchers_lock);
}
/**
 * add_sw - 
 * @name: 
 * @len: 
 * @func: 
 * @misc: 
 * 
 * 
 * Returns: int
 */
static int add_sw(uint8_t *name, uint8_t len,
		void(*func)(void *misc), void *misc)
{
	struct sig_watcher *sw;

	sw = kmalloc(sizeof(struct sig_watcher), GFP_KERNEL);
	if( sw == NULL ) return -ENOMEM;
	sw->name = kmalloc(len, GFP_KERNEL);
	if( sw->name == NULL ) {
		kfree(sw);
		return -ENOMEM;
	}
	memcpy(sw->name, name, len);
	sw->len = len;
	sw->func = func;
	sw->misc = misc;
	INIT_LIST_HEAD (&sw->sw_list);
	spin_lock(&sig_watchers_lock);
	list_add(&sw->sw_list, &sig_watchers_list);
	spin_unlock(&sig_watchers_lock);
	return 0;
}

/**
 * gulm_sw_finish - 
 * @item: 
 * 
 * 
 * Returns: void
 */
void gulm_sw_finish (struct glck_req *item)
{
	struct completion *sleep = (struct completion *)item->misc;
	complete (sleep);
}

/**
 * hold_watch_lock - 
 * @name: 
 * @len: 
 * 
 * 
 * Returns: void
 */
void hold_watch_lock(gulm_fs_t *fs, uint8_t *name, uint8_t len)
{
	uint8_t key[GIO_KEY_SIZE];
	uint16_t keylen = GIO_KEY_SIZE;
	struct completion sleep;
	glckr_t *item;

	item = glq_get_new_req();
	if (item == NULL) {
		return;
	}

	item->keylen = pack_lock_key(key, keylen, 'S', fs->fs_name, name, len);
	item->key = key;
	item->subid = 0;
	item->start = 0;
	item->stop = ~((uint64_t)0);
	item->type = glq_req_type_state;
	item->state = lg_lock_state_Shared;
	item->flags = lg_lock_flag_IgnoreExp;
	item->error =  0;
	item->lvb = NULL;
	item->lvblen = 0;

	init_completion (&sleep);

	item->misc = &sleep;
	item->finish = gulm_sw_finish;

	glq_queue (item);
	wait_for_completion (&sleep);
}
/**
 * release_watch_lock - 
 * @name: 
 * @len: 
 * 
 * 
 * Returns: void
 */
void release_watch_lock(gulm_fs_t *fs, uint8_t *name, uint8_t len)
{
	uint8_t key[GIO_KEY_SIZE];
	uint16_t keylen = GIO_KEY_SIZE;
	struct completion sleep;
	glckr_t *item;

	item = glq_get_new_req();
	if (item == NULL) {
		return;
	}

	item->keylen = pack_lock_key(key, keylen, 'S', fs->fs_name, name, len);
	item->key = key;
	item->subid = 0;
	item->start = 0;
	item->stop = ~((uint64_t)0);
	item->type = glq_req_type_state;
	item->state = lg_lock_state_Unlock;
	item->flags = 0;
	item->error =  0;
	item->lvb = NULL;
	item->lvblen = 0;

	init_completion (&sleep);

	item->misc = &sleep;
	item->finish = gulm_sw_finish;

	glq_queue (item);
	wait_for_completion (&sleep);
}
/**
 * signal_watch_lock - 
 * @name: 
 * @len: 
 * 
 * 
 * Returns: void
 */
void signal_watch_lock(gulm_fs_t *fs, uint8_t *name, uint8_t len)
{
	uint8_t key[GIO_KEY_SIZE];
	uint16_t keylen = GIO_KEY_SIZE;
	struct completion sleep;
	glckr_t *item;

	item = glq_get_new_req();
	if (item == NULL) {
		return;
	}

	item->keylen = pack_lock_key(key, keylen, 'S', fs->fs_name, name, len);
	item->key = key;
	item->subid = 0;
	item->start = 0;
	item->stop = ~((uint64_t)0);
	item->type = glq_req_type_state;
	item->state = lg_lock_state_Exclusive;
	item->flags = lg_lock_flag_Try|lg_lock_flag_DoCB|lg_lock_flag_IgnoreExp;
	item->error =  0;
	item->lvb = NULL;
	item->lvblen = 0;

	init_completion (&sleep);

	item->misc = &sleep;
	item->finish = gulm_sw_finish;

	glq_queue (item);
	wait_for_completion (&sleep);
}

/**
 * sig_watcher_lock_drop - 
 * @key: 
 * @keylen: 
 * 
 * 
 * Returns: void
 */
void sig_watcher_lock_drop(uint8_t * key, uint16_t keylen)
{
	struct list_head *tmp;
	struct sig_watcher *sw = NULL;
	int found = FALSE;
	uint8_t *fsname, len, *name, nlen;
	if( key[0] != 'S' ) return; /* not a Signal lock */
	len = key[1];
	fsname = &key[2];
	nlen = key[3 + len];
	name = &key[4 + len];
	spin_lock(&sig_watchers_lock);
	list_for_each(tmp, &sig_watchers_list) {
		sw = list_entry (tmp, struct sig_watcher, sw_list);
		if( memcmp(name, sw->name, MIN(nlen, sw->len)) == 0 ) {
			found = TRUE;
			break;
		}
	}
	spin_unlock(&sig_watchers_lock);
	if(found) {
		sw->func(sw->misc);
	}
}

/****************************************************************************/

/**
 * sig_water_init - 
 * @oid: 
 * 
 * 
 * Returns: void
 */
void sig_watcher_init(void)
{
	INIT_LIST_HEAD (&sig_watchers_list);
	spin_lock_init(&sig_watchers_lock);
}


/**
 * watch_sig - 
 * @name: 
 * @len: 
 * @misc: 
 * @misc: 
 * 
 * Returns: int
 */
int watch_sig(gulm_fs_t *fs, uint8_t *name, uint8_t len, void(*func)(void *misc), void *misc)
{
	if( func == NULL ) {
		/* unlock signal lock */
		release_watch_lock(fs, name, len);
		release_sw(name, len);
	}else{
		/* hold signal lock shared. */
		if(add_sw(name, len, func, misc) == 0 ) {
			hold_watch_lock(fs, name, len);
		}else{
			return -ENOMEM;
		}
	}
	return 0;
}

/**
 * tap_sig - 
 * @name: 
 * @len: 
 * 
 * 
 * Returns: int
 */
void tap_sig(gulm_fs_t *fs, uint8_t *name, uint8_t len)
{
	signal_watch_lock(fs, name, len);
#if 0
	/* Make sure it is still Shr. (very lazy way to do this. but it
	 * should be low traffic enough not to bother.)
	 * */
	hold_watch_lock(fs, name, len);
#endif
}

/* vim: set ai cin noet sw=8 ts=8 : */
