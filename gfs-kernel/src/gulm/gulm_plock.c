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

/*****************************************************************************/
/**
 * gulm_plock_packname - 
 * @fsname: 
 * @num: 
 * @key: 
 * @keylen: 
 * 
 * 
 * Returns: int
 */
int gulm_plock_packname(uint8_t * fsname, uint64_t num, uint8_t *key, uint16_t keylen)
{
	uint8_t temp[8];
	temp[0] = (num >> 56) & 0xff;
	temp[1] = (num >> 48) & 0xff;
	temp[2] = (num >> 40) & 0xff;
	temp[3] = (num >> 32) & 0xff;
	temp[4] = (num >> 24) & 0xff;
	temp[5] = (num >> 16) & 0xff;
	temp[6] = (num >> 8) & 0xff;
	temp[7] = (num >> 0) & 0xff;
	return pack_lock_key(key, keylen, 'P', fsname, temp, 8);
}

struct gulm_pqur_s {
	uint64_t start;
	uint64_t stop;
	uint64_t subid;
	int error;
	uint8_t state;
	struct completion sleep;
};
/**
 * gulm_plock_query_finish - 
 * @glck: 
 * 
 * 
 * Returns: void
 */
void gulm_plock_query_finish(struct glck_req *glck)
{
	struct gulm_pqur_s *g = (struct gulm_pqur_s *)glck->misc;
	g->error = glck->error;
	g->start = glck->start;
	g->stop = glck->stop;
	g->subid = glck->subid;
	g->state = glck->state;
	complete (&g->sleep);
}
/**
 * gulm_plock_get - 
 */
int
gulm_plock_get (lm_lockspace_t * lockspace, struct lm_lockname *name,
		 struct file *file, struct file_lock *fl)
{
	int err = 0;
	struct gulm_pqur_s pqur;
	uint8_t key[GIO_KEY_SIZE];
	gulm_fs_t *fs = (gulm_fs_t *) lockspace;
	glckr_t *item;

	item = glq_get_new_req();
	if( item == NULL ) {
		err = -ENOMEM;
		goto fail;
	}

	item->keylen = gulm_plock_packname(fs->fs_name, name->ln_number,
			key, GIO_KEY_SIZE);
	item->key = key;
	item->subid = (unsigned long) fl->fl_owner;
	item->start = fl->fl_start;
	item->stop = fl->fl_end;
	item->type = glq_req_type_query;
	if (fl->fl_type == F_WRLCK) {
		item->state = lg_lock_state_Exclusive;
	} else {
		item->state = lg_lock_state_Shared;
	}
	item->flags = lg_lock_flag_NoCallBacks;
	item->error = pqur.error = 0;

	init_completion (&pqur.sleep);

	item->misc = &pqur;
	item->finish = gulm_plock_query_finish;

	glq_queue (item);
	wait_for_completion (&pqur.sleep);

	if (pqur.error == lg_err_TryFailed) {
		err = -EAGAIN;
		fl->fl_start = pqur.start;
		fl->fl_end = pqur.stop;
		fl->fl_pid = pqur.subid;
		if( pqur.state == lg_lock_state_Exclusive )
			fl->fl_type = F_WRLCK;
		else
			fl->fl_type = F_RDLCK;
	} else if (pqur.error == 0) {
		fl->fl_type = F_UNLCK;
	} else {
		err = -pqur.error;
	}

fail:
	return err;
}

struct gulm_plock_req_wait_s {
	int error;
	int done;
	wait_queue_head_t waiter;
};

/**
 * gulm_plock_req_finish - 
 * @glck: 
 * 
 * 
 * Returns: void
 */
void gulm_plock_req_finish(struct glck_req *glck)
{
	struct gulm_plock_req_wait_s *g = (struct gulm_plock_req_wait_s *)glck->misc;
	g->error = glck->error;
	g->done = TRUE;
	wake_up (&g->waiter);
}
/**
 * do_plock_cancel - 
 * @item: 
 * 
 * 
 * Returns: void
 */
void do_plock_cancel(glckr_t *item)
{
	glckr_t *cancel;
	cancel = glq_get_new_req();
	if( cancel == NULL ) {
		log_err ("Out of memory, Cannot cancel plock request.\n");
		return;
	}

	/* after cancel is processed, glq will call kfree on item->key. */
	cancel->key = kmalloc(item->keylen, GFP_KERNEL);
	if (cancel->key == NULL) {
		glq_recycle_req(cancel);
		log_err ("Out of memory, Cannot cancel plock request.\n");
		return;
	}
	memcpy(cancel->key, item->key, item->keylen);
	cancel->keylen = item->keylen;
	cancel->subid = item->subid;
	cancel->start = item->start;
	cancel->stop = item->stop;
	cancel->type = glq_req_type_cancel;
	cancel->finish = NULL;

	glq_cancel(cancel);
}
/**
 * gulm_plock - 
 *
 */
int
gulm_plock (lm_lockspace_t *lockspace, struct lm_lockname *name,
		struct file *file, int cmd, struct file_lock *fl)
{
	int err = 0;
	struct gulm_plock_req_wait_s pwait;
	uint8_t key[GIO_KEY_SIZE];
	gulm_fs_t *fs = (gulm_fs_t *) lockspace;
	glckr_t *item;

	item = glq_get_new_req();
	if( item == NULL ) {
		err = -ENOMEM;
		goto fail;
	}

	item->keylen = gulm_plock_packname(fs->fs_name, name->ln_number,
			key, GIO_KEY_SIZE);
	item->key = key;
	item->subid = (unsigned long) fl->fl_owner;
	item->start = fl->fl_start;
	item->stop = fl->fl_end;
	item->type = glq_req_type_state;
	if (fl->fl_type == F_WRLCK) {
		item->state = lg_lock_state_Exclusive;
	} else {
		item->state = lg_lock_state_Shared;
	}
	item->flags = lg_lock_flag_NoCallBacks;
	if (!IS_SETLKW(cmd))
		item->flags |= lg_lock_flag_Try;
	item->error = pwait.error = 0;

	pwait.done = FALSE;
	init_waitqueue_head(&pwait.waiter);

	item->misc = &pwait;
	item->finish = gulm_plock_req_finish;

	glq_queue (item);
	err = wait_event_interruptible(pwait.waiter, pwait.done);
	if( err != 0 ) {
		/* signals. */
		/* send cancel req. */
		do_plock_cancel(item);
		/* wait for canceled (or success if we were too slow in
		 * canceling) */
		wait_event(pwait.waiter, pwait.done);
	}

	if (pwait.error == lg_err_TryFailed) {
		err = -EAGAIN;
	} else if (pwait.error == lg_err_Canceled) {
		err = -EINTR;
	} else {
		err = -pwait.error;
	}

	if ( err == 0) err = posix_lock_file_wait(file, fl);

fail:
	return err;
}

struct gulm_pret_s {
	int error;
	struct completion sleep;
};

/**
 * gulm_plock_finish - 
 * @glck: 
 * 
 * 
 * Returns: void
 */
void gulm_plock_finish(struct glck_req *glck)
{
	struct gulm_pret_s *g = (struct gulm_pret_s *)glck->misc;
	g->error = glck->error;
	complete (&g->sleep);
}

/**
 * gulm_unplock - 
 */
int
gulm_punlock (lm_lockspace_t * lockspace, struct lm_lockname *name,
	      struct file *file, struct file_lock *fl)
{
	int err = 0;
	struct gulm_pret_s pret;
	uint8_t key[GIO_KEY_SIZE];
	gulm_fs_t *fs = (gulm_fs_t *) lockspace;
	glckr_t *item;

	item = glq_get_new_req();
	if( item == NULL ) {
		err = -ENOMEM;
		goto fail;
	}

	item->keylen = gulm_plock_packname(fs->fs_name, name->ln_number,
			key, GIO_KEY_SIZE);
	item->key = key;
	item->subid = (unsigned long) fl->fl_owner;
	item->start = fl->fl_start;
	item->stop = fl->fl_end;
	item->type = glq_req_type_state;
	item->state = lg_lock_state_Unlock;
	item->flags = 0;
	item->error = pret.error = 0;

	init_completion (&pret.sleep);

	item->misc = &pret;
	item->finish = gulm_plock_finish;

	glq_queue (item);
	wait_for_completion (&pret.sleep);

	err = -pret.error;
	if ( err == 0) err = posix_lock_file_wait(file, fl);

fail:
	return err;
}

/* vim: set ai cin noet sw=8 ts=8 : */
