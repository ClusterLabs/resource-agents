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
struct gulm_pret_s {
	int error;
	struct completion sleep;
};

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
gulm_plock_get (lm_lockspace_t * lockspace,
		struct lm_lockname *name, unsigned long owner,
		uint64_t * start, uint64_t * end, int *exclusive,
		unsigned long *rowner)
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
	item->subid = owner;
	item->start = *start;
	item->stop = *end;
	item->type = glq_req_type_query;
	if (*exclusive) {
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
		*start = pqur.start;
		*end = pqur.stop;
		*rowner = pqur.subid;
		if( pqur.state == lg_lock_state_Exclusive )
			*exclusive = TRUE;
		else
			*exclusive = FALSE;
	} else {
		err = -pqur.error;
	}

fail:
	return err;
}

/**
 * gulm_plock - 
 *
 */
int
gulm_plock (lm_lockspace_t * lockspace,
	    struct lm_lockname *name, unsigned long owner,
	    int wait, int exclusive, uint64_t start, uint64_t end)
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
	item->subid = owner;
	item->start = start;
	item->stop = end;
	item->type = glq_req_type_state;
	if (exclusive) {
		item->state = lg_lock_state_Exclusive;
	} else {
		item->state = lg_lock_state_Shared;
	}
	item->flags = lg_lock_flag_NoCallBacks;
	if (wait)
		item->flags |= lg_lock_flag_Try;
	item->error = pret.error = 0;

	init_completion (&pret.sleep);

	item->misc = &pret;
	item->finish = gulm_plock_finish;

	glq_queue (item);
	wait_for_completion (&pret.sleep);

	if (pret.error == lg_err_TryFailed) {
		err = -EAGAIN;
	} else {
		err = -pret.error;
	}

fail:
	return err;
}

/**
 * gulm_unplock - 
 */
int
gulm_punlock (lm_lockspace_t * lockspace,
	      struct lm_lockname *name, unsigned long owner,
	      uint64_t start, uint64_t end)
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
	item->subid = owner;
	item->start = start;
	item->stop = end;
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

fail:
	return err;
}

/* vim: set ai cin noet sw=8 ts=8 : */
