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
#include <linux/crc32.h>
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>

#include "handler.h"
#include "gulm_lock_queue.h"
#include "utils_tostr.h"

#define gulm_gfs_lmBits (13)
#define gulm_gfs_lmSize (1 << gulm_gfs_lmBits)
#define gulm_gfs_lmMask (gulm_gfs_lmSize - 1)

extern gulm_cm_t gulm_cm;

/****************************************************************************/
/* A bunch of prints that hopefully contain more information that is also
 * useful
 *
 * these are a mess.
 */

/**
 * lck_key_to_hex - 
 * @key: 
 * @len: 
 * @workspace: <> place to put string. !! better be 2x len !!
 * 
 * 
 * Returns: char
 */
static char *
lck_key_to_hex (uint8_t * key, uint16_t len, char *workspace)
{
	int i;
	for (i = 0; i < len; i++)
		sprintf (&workspace[i * 2], "%02x", (key[i] & 0xff));
	return workspace;
}

#if 0
static void __inline__
db_lck_entered (gulm_lock_t * lck)
{
	char bb[GIO_KEY_SIZE * 2 + 3];
	lck_key_to_hex (lck->key, lck->keylen, bb);
	printk ("Started  lock 0x%s cur:%#x req:%#x flags:%#x\n", bb,
		lck->cur_state, lck->req_state, lck->flags);
}
static void __inline__
db_lck_exited (gulm_lock_t * lck)
{
	char bb[GIO_KEY_SIZE * 2 + 3];
	lck_key_to_hex (lck->key, lck->keylen, bb);
	printk ("Finished lock 0x%s result:%#x\n", bb, lck->result);
}
#endif

static void __inline__
dump_gulm_lock_t (gulm_lock_t * lck)
{
	char bb[GIO_KEY_SIZE * 2 + 3];

	lck_key_to_hex (lck->key, lck->keylen, bb);
	log_msg (lgm_Always, " key = 0x%s\n", bb);
	log_msg (lgm_Always, " cur_state = %d\n", lck->cur_state);
}

/* DEBUG_BY_LOCK is gone.  I may later add something back if needed.
 *
 * I love the idea of being able to log only certain locks, I just cannot
 * think of an easy way to do it.  The best I can come up with is some
 * pattern (or set of) that are used to decide which locks get logged.  But
 * that could be expensive if the pattern is checked everytime, and won't
 * behave as expected if only applied in get_lock.
 * */

/* The old log functions.
 * These need their own sort of clean up someday as well.
 * */
#define log_msg_lk(key, keylen, fmt, args...) {\
      uint8_t bb[GIO_KEY_SIZE*2 +3]; \
      lck_key_to_hex( key, keylen, bb); \
      printk(PROTO_NAME ": On lock 0x%s " fmt , bb , ## args ); \
   }

#define log_err_lk(key, keylen, fmt, args...) {\
      uint8_t bb[GIO_KEY_SIZE*2 +3]; \
      lck_key_to_hex( key, keylen, bb); \
      printk(KERN_ERR PROTO_NAME ": ERROR On lock 0x%s " fmt , bb , ## args ); \
   }

#define log_msg_lck(lck, fmt, args...) {\
      uint8_t bb[GIO_KEY_SIZE*2 +3]; \
      lck_key_to_hex( (lck)->key, (lck)->keylen, bb); \
      printk(PROTO_NAME ": On lock 0x%s " fmt , bb , ## args ); \
   }

#define log_err_lck(lck, fmt, args...) {\
      uint8_t bb[GIO_KEY_SIZE*2 +3]; \
      lck_key_to_hex( (lck)->key, (lck)->keylen, bb); \
      printk(KERN_ERR PROTO_NAME ": ERROR On lock 0x%s " fmt , bb , ## args ); \
   }

#ifdef DEBUG_LVB
static void __inline__
print_lk_lvb (uint8_t * key, uint8_t * lvb, uint8_t st, uint8_t * dir)
{
	uint8_t bk[GIO_KEY_SIZE * 2 + 3];
	uint8_t bl[GIO_LVB_SIZE * 2 + 3];
	int i;
	for (i = 0; i < GIO_KEY_SIZE; i++)
		sprintf (&bk[(i * 2)], "%02x", (key[i]) & 0xff);
	for (i = 0; i < GIO_LVB_SIZE; i++)
		sprintf (&bl[(i * 2)], "%02x", (lvb[i]) & 0xff);
	printk (PROTO_NAME ": On lock 0x%s with state %d\n\t%s LVB 0x%s\n",
		bk, st, dir, bl);
}

#define lvb_log_msg_lk(k, fmt, args...) log_msg_lk( k , fmt , ## args )
#define lvb_log_msg(fmt, args...) log_msg(lgm_Always , fmt , ## args )
#else				/*DEBUG_LVB */
#define print_lk_lvb(k,l,s,d)
#define lvb_log_msg_lk(k, fmt, args...)
#define lvb_log_msg(fmt, args...)
#endif				/*DEBUG_LVB */

/****************************************************************************/
/**
 * pack_lock_key - 
 * @key: 
 * @keylen: 
 * 
 * key is: <type><fsname len><fsname>\0<pk len><pk>\0
 * <type> is: G J F N P
 * <fsname len> is 0-256
 * 
 * Returns: int
 */
int pack_lock_key(uint8_t *key, uint16_t keylen, uint8_t type,
		uint8_t *fsname, uint8_t *pk, uint8_t pklen)
{
	int fsnlen;
	fsnlen = strlen(fsname);

	if( keylen <= (fsnlen + pklen + 5) ) return -1;

	memset (key, 0, keylen);

	key[0] = type;

	key[1] = fsnlen;
	memcpy(&key[2], fsname, fsnlen);
	key[2 + fsnlen] = 0;

	key[3 + fsnlen] = pklen;

	memcpy(&key[4 + fsnlen], pk, pklen);

	key[4 + fsnlen + pklen] = 0;

	return fsnlen + pklen + 5;
}

/**
 * unpack_lock_key - 
 * @key: <
 * @keylen: <
 * @type: >
 * @fsname: >
 * @fsnlen: >
 * @pk: >
 * @pklen: >
 * 
 * if you're gonna fiddle with bytes returned here, copy first!
 *
 * this is broken. do I even really need this?
 * 
 * Returns: int
 */
int unpack_lock_key(uint8_t *key, uint16_t keylen, uint8_t *type,
		uint8_t **fsname, uint8_t *fsnlen,
		uint8_t **pk, uint8_t *pklen)
{
	int fsnl, pkl;
	if( type != NULL )
		*type = key[0];

	fsnl = key[1];
	if( fsnlen != NULL && *fsname != NULL ) {
		*fsnlen = key[1];
		*fsname = &key[2];
	}

	/* 0 = key[2 + fsnl] */

	pkl = key[3 + fsnl];
	if( pklen != NULL && *pk != NULL ) {
		*pklen = key[3 + fsnl];
		*pk = &key[4 + fsnl];
	}

	/* 0 = key[4 + fsnl + *pklen] */

	return fsnl + pkl + 5;
}

/**
 * pack_drop_mask - 
 * @mask: 
 * @fsname: 
 * 
 * 
 * Returns: int
 */
int pack_drop_mask(uint8_t *mask, uint16_t mlen, uint8_t *fsname)
{
	int fsnlen;
	fsnlen = strlen(fsname);

	memset (mask, 0, mlen);

	mask[0] = 0xff;
	mask[1] = fsnlen;
	memcpy(&mask[2], fsname, fsnlen);
	mask[2 + fsnlen] = 0;
	/* rest should be 0xff */

	return 3 + fsnlen;
}

/**
 * gulm_lt_init - 
 * 
 * Returns: int
 */
int gulm_lt_init (void)
{
	int i;
	gulm_cm.gfs_lockmap = vmalloc(sizeof(gulm_hb_t) * gulm_gfs_lmSize);
	if (gulm_cm.gfs_lockmap == NULL)
		return -ENOMEM;
	for(i=0; i < gulm_gfs_lmSize; i++) {
		spin_lock_init (&gulm_cm.gfs_lockmap[i].lock);
		INIT_LIST_HEAD (&gulm_cm.gfs_lockmap[i].bucket);
	}
	return 0;
}

/**
 * gulm_lt_release - 
 */
void gulm_lt_release(void)
{
	struct list_head *tmp, *lltmp;
	gulm_lock_t *lck;
	int i;

	for(i=0; i < gulm_gfs_lmSize; i++) {
		list_for_each_safe (tmp, lltmp, &gulm_cm.gfs_lockmap[i].bucket) {
			lck = list_entry (tmp, gulm_lock_t, gl_list);
			list_del (tmp);

			if (lck->lvb != NULL) kfree (lck->lvb);

			kfree(lck);
		}
	}

	vfree (gulm_cm.gfs_lockmap);
}

/**
 * find_and_mark_lock - 
 * @key: 
 * @keylen: 
 * @lockp: 
 * 
 * looks for a lock struct of key.  If found, marks it.
 * 
 * Returns: TRUE or FALSE
 */
int
find_and_mark_lock (uint8_t * key, uint8_t keylen, gulm_lock_t ** lockp)
{
	int found = FALSE;
	uint32_t bkt;
	gulm_lock_t *lck = NULL;
	struct list_head *tmp;

	/* now find the lock */
	bkt = crc32 (GULM_CRC_INIT, key, keylen);
	bkt &= gulm_gfs_lmMask;

	spin_lock (&gulm_cm.gfs_lockmap[bkt].lock);
	list_for_each (tmp, &gulm_cm.gfs_lockmap[bkt].bucket) {
		lck = list_entry (tmp, gulm_lock_t, gl_list);
		if (memcmp (lck->key, key, keylen) == 0) {
			found = TRUE;
			atomic_inc (&lck->count);
			break;
		}
	}
	spin_unlock (&gulm_cm.gfs_lockmap[bkt].lock);

	if (found)
		*lockp = lck;

	return found;
}

/**
 * mark_lock - 
 * @lck: 
 * 
 * like above, but since we have the lock, don't search for it.
 * 
 * Returns: int
 */
void __inline__
mark_lock (gulm_lock_t * lck)
{
	atomic_inc (&lck->count);
}

/**
 * unmark_and_release_lock - 
 * @lck: 
 * 
 * decrement the counter on a lock, freeing it if it reaches 0.
 * (also removes it from the hash table)
 * 
 * TRUE if lock was freed.
 *
 * Returns: TRUE or FALSE
 */
int
unmark_and_release_lock (gulm_lock_t * lck)
{
	uint32_t bkt;
	int deld = FALSE;

	bkt = crc32 (GULM_CRC_INIT, lck->key, lck->keylen);
	bkt &= gulm_gfs_lmMask;

	spin_lock (&gulm_cm.gfs_lockmap[bkt].lock);
	if (atomic_dec_and_test (&lck->count)) {
		list_del (&lck->gl_list);
		deld = TRUE;
	}
	spin_unlock (&gulm_cm.gfs_lockmap[bkt].lock);
	if (deld) {
		if (lck->lvb != NULL) {
			kfree (lck->lvb);
		}
		kfree (lck->key);
		kfree (lck);
	}

	return deld;
}

/****************************************************************************/

/**
 * gulm_key_to_lm_lockname - 
 * @key: 
 * @lockname: 
 * 
 */
void
gulm_key_to_lm_lockname (uint8_t * key, struct lm_lockname *lockname)
{
	int pos;

	pos = key[1] + 4;
	/* pos now points to the first byte of the GFS lockname that was
	 * embedded in the gulm lock key, skipping over the fs name.
	 */

	(*lockname).ln_type = key[pos];
	(*lockname).ln_number  = (u64) (key[pos+1]) << 56;
	(*lockname).ln_number |= (u64) (key[pos+2]) << 48;
	(*lockname).ln_number |= (u64) (key[pos+3]) << 40;
	(*lockname).ln_number |= (u64) (key[pos+4]) << 32;
	(*lockname).ln_number |= (u64) (key[pos+5]) << 24;
	(*lockname).ln_number |= (u64) (key[pos+6]) << 16;
	(*lockname).ln_number |= (u64) (key[pos+7]) << 8;
	(*lockname).ln_number |= (u64) (key[pos+8]) << 0;
}

/**
 * do_drop_lock_req - 
 * @key: 
 * @keylen: 
 * @state: 
 * 
 * 
 * Returns: void
 */
void
do_drop_lock_req (uint8_t *key, uint16_t keylen, uint8_t state)
{
	gulm_lock_t *lck;
	unsigned int type;
	struct lm_lockname lockname;

	if (!find_and_mark_lock (key, keylen, &lck)) {
		return;
	}

	switch (state) {
	case lg_lock_state_Unlock:
		type = LM_CB_DROPLOCKS;
		break;
	case lg_lock_state_Exclusive:
		type = LM_CB_NEED_E;
		break;
	case lg_lock_state_Shared:
		type = LM_CB_NEED_S;
		break;
	case lg_lock_state_Deferred:
		type = LM_CB_NEED_D;
		break;
	default:
		type = LM_CB_DROPLOCKS;
		break;
	}
	gulm_key_to_lm_lockname (key, &lockname);

	qu_drop_req (&lck->fs->cq, lck->fs->cb, lck->fs->fsdata, type,
		     lockname.ln_type, lockname.ln_number);

	unmark_and_release_lock (lck);
}

/****************************************************************************/

/**
 * calc_lock_result - 
 * @lck: 
 * @state: 
 * @error: 
 * @flags: 
 * 
 * This calculates the correct result to return for gfs lock requests.
 * 
 * Returns: int
 */
int
calc_lock_result (gulm_lock_t * lck,
		  uint8_t state, uint32_t error, uint32_t flags)
{
	int result = -69;

	/* adjust result based on success status. */
	switch (error) {
	case lg_err_Ok:
		/* set result to current lock state. */
		switch (state) {
		case lg_lock_state_Shared:
			result = LM_ST_SHARED;
			break;
		case lg_lock_state_Deferred:
			result = LM_ST_DEFERRED;
			break;
		case lg_lock_state_Exclusive:
			result = LM_ST_EXCLUSIVE;
			break;
		case lg_lock_state_Unlock:
			result = LM_ST_UNLOCKED;
			break;
		default: /* erm */
			break;
		}

		/* if no internal unlocks, it is cachable. */
		if (result != LM_ST_UNLOCKED && (flags & lg_lock_flag_Cachable))
			result |= LM_OUT_CACHEABLE;

		break;
	case lg_err_Canceled:
		result = LM_OUT_CANCELED | lck->cur_state;
		break;
	case lg_err_TryFailed:
		result = lck->cur_state;	/* if we didn't get it. */
		break;
	default:
		result = -error;
		break;
	}

	return result;
}

/****************************************************************************/

/**
 * gulm_get_lock - 
 * @lockspace: 
 * @name: 
 * @lockp:
 * 
 * Returns: 0 on success, -EXXX on failure
 */
int
gulm_get_lock (lm_lockspace_t * lockspace, struct lm_lockname *name,
	       lm_lock_t ** lockp)
{
	int err=0, len, bkt;
	gulm_fs_t *fs = (gulm_fs_t *) lockspace;
	uint8_t key[GIO_KEY_SIZE];
	uint8_t temp[9];
	gulm_lock_t *lck=NULL;

	temp[0] = name->ln_type & 0xff;
	temp[1] = (name->ln_number >> 56) & 0xff;
	temp[2] = (name->ln_number >> 48) & 0xff;
	temp[3] = (name->ln_number >> 40) & 0xff;
	temp[4] = (name->ln_number >> 32) & 0xff;
	temp[5] = (name->ln_number >> 24) & 0xff;
	temp[6] = (name->ln_number >> 16) & 0xff;
	temp[7] = (name->ln_number >> 8) & 0xff;
	temp[8] = (name->ln_number >> 0) & 0xff;

	len = pack_lock_key(key, GIO_KEY_SIZE, 'G', fs->fs_name, temp, 9);
	if( len <=0 ) {err = len; goto exit;}

	if (!find_and_mark_lock (key, len, &lck)) {
		/* not found, must create. */
		lck = kmalloc(sizeof(gulm_lock_t), GFP_KERNEL);
		if (lck == NULL) {
			err = -ENOMEM;
			goto exit;
		}
		INIT_LIST_HEAD (&lck->gl_list);
		atomic_set (&lck->count, 1);
		lck->key = kmalloc (len, GFP_KERNEL);
		if (lck->key == NULL) {
			kfree(lck);
			err = -ENOMEM;
			goto exit;
		}
		memcpy (lck->key, key, len);
		lck->keylen = len;
		lck->fs = fs;
		lck->lvb = NULL;
		lck->cur_state = LM_ST_UNLOCKED;

		bkt = crc32 (GULM_CRC_INIT, key, len);
		bkt &= gulm_gfs_lmMask;

		spin_lock (&gulm_cm.gfs_lockmap[bkt].lock);
		list_add (&lck->gl_list, &gulm_cm.gfs_lockmap[bkt].bucket);
		spin_unlock (&gulm_cm.gfs_lockmap[bkt].lock);

	}
	*lockp = lck;

exit:
	return err;
}

/**
 * gulm_put_lock - 
 * @lock: 
 * 
 * 
 * Returns: void
 */
void
gulm_put_lock (lm_lock_t * lock)
{
	unmark_and_release_lock ((gulm_lock_t *) lock);
}

/**
 * gulm_lock_finish - 
 * @glck: 
 * 
 * 
 * Returns: void
 */
void gulm_lock_finish (struct glck_req *item)
{
	int result;
	gulm_lock_t *lck = (gulm_lock_t *)item->misc;
	gulm_fs_t *fs = lck->fs;
	struct lm_lockname lockname;

	result = calc_lock_result (lck, item->state, item->error, item->flags);

	gulm_key_to_lm_lockname (lck->key, &lockname);

	qu_async_rpl (&fs->cq, fs->cb, fs->fsdata, &lockname, result);

	/* marked in gulm_lock() */
	unmark_and_release_lock (lck);
}

/**
 * gulm_lock - 
 * @lock: 
 * @cur_state: 
 * @req_state: 
 * @flags: 
 * 
 * 
 * Returns: int
 */
unsigned int
gulm_lock (lm_lock_t * lock, unsigned int cur_state,
	   unsigned int req_state, unsigned int flags)
{
	glckr_t *item;
	gulm_lock_t *lck = (gulm_lock_t *) lock;
	gulm_fs_t *fs = lck->fs;

	item = glq_get_new_req();
	if (item == NULL) {
		return -ENOMEM;
	}

	mark_lock (lck); /* matching unmark is in gulm_lock_finish */

	item->key = lck->key;
	item->keylen = lck->keylen;
	item->subid = 0;
	item->start = 0;
	item->stop = ~((uint64_t)0);
	item->type = glq_req_type_state;

	switch (req_state) {
	case LM_ST_EXCLUSIVE:
		item->state = lg_lock_state_Exclusive;
		break;
	case LM_ST_DEFERRED:
		item->state = lg_lock_state_Deferred;
		break;
	case LM_ST_SHARED:
		item->state = lg_lock_state_Shared;
		break;
	case LM_ST_UNLOCKED:
		item->state = lg_lock_state_Unlock;
		break;
	default:
		GULM_ASSERT (0, log_err ("fsid=%s: Anit no lock state %d.\n",
					 fs->fs_name, req_state););
		break;
	}
	item->flags = 0;
	if (flags & LM_FLAG_TRY) {
		item->flags |= lg_lock_flag_Try;
	}
	if (flags & LM_FLAG_TRY_1CB) {
		item->flags |= lg_lock_flag_Try | lg_lock_flag_DoCB;
	}
	if (flags & LM_FLAG_NOEXP) {
		item->flags |= lg_lock_flag_IgnoreExp;
	}
	if (flags & LM_FLAG_ANY) {
		item->flags |= lg_lock_flag_Any;
	}
	if (flags & LM_FLAG_PRIORITY) {
		item->flags |= lg_lock_flag_Piority;
	}
	if (lck->lvb != NULL) {
		item->lvb = lck->lvb;
		item->lvblen = fs->lvb_size;
	}else{
		item->lvb = NULL;
		item->lvblen = 0;
	}
	item->error = 0;

	item->misc = lck;
	item->finish = gulm_lock_finish;

	lck->cur_state = cur_state;

	glq_queue (item);

	return LM_OUT_ASYNC;
}

/**
 * gulm_unlock - 
 * @lock: 
 * @cur_state: 
 * 
 * 
 * Returns: int
 */
unsigned int
gulm_unlock (lm_lock_t * lock, unsigned int cur_state)
{
	int e;
	e = gulm_lock (lock, cur_state, LM_ST_UNLOCKED, 0);
	return e;
}

/**
 * gulm_cancel - 
 * @lock: 
 * 
 */
void
gulm_cancel (lm_lock_t * lock)
{
	glckr_t *item;
	gulm_lock_t *lck = (gulm_lock_t *) lock;

	mark_lock (lck);

	item = glq_get_new_req();
	if( item == NULL ) goto exit;

	/* have to make a copy for cancel req. */
	item->key = kmalloc(lck->keylen, GFP_KERNEL);
	if (item->key == NULL) {
		glq_recycle_req(item);
		goto exit;
	}
	memcpy(item->key, lck->key, lck->keylen);
	item->keylen = lck->keylen;
	item->subid = 0;
	item->start = 0;
	item->stop = ~((uint64_t)0);
	item->type = glq_req_type_cancel;
	item->finish = NULL;

	glq_cancel(item);

exit:
	unmark_and_release_lock (lck);
}

/****************************************************************************/
struct gulm_lvb_temp_s {
	int error;
	struct completion sleep;
};

/**
 * gulm_lvb_finish - 
 * @glck: 
 * 
 * 
 * Returns: void
 */
void gulm_lvb_finish(struct glck_req *glck)
{
	struct gulm_lvb_temp_s *g = (struct gulm_lvb_temp_s *)glck->misc;
	g->error = glck->error;
	complete (&g->sleep);
}

/**
 * gulm_hold_lvb - 
 * @lock: 
 * @lvbp:
 * 
 * 
 * Returns: 0 on success, -EXXX on failure
 */
int
gulm_hold_lvb (lm_lock_t * lock, char **lvbp)
{
	int err = -1;
	struct gulm_lvb_temp_s ghlt;
	glckr_t *item;
	gulm_lock_t *lck = (gulm_lock_t *) lock;
	gulm_fs_t *fs = lck->fs;

	mark_lock (lck);

	item = glq_get_new_req();
	if( item == NULL ) {
		err = -ENOMEM;
		goto fail;
	}

	item->key = lck->key;
	item->keylen = lck->keylen;
	item->subid = 0;
	item->start = 0;
	item->stop = ~((uint64_t)0);
	item->type = glq_req_type_action;
	item->state = lg_lock_act_HoldLVB;
	item->flags = 0;
	item->error = ghlt.error = 0;

	init_completion (&ghlt.sleep);

	item->misc = &ghlt;
	item->finish = gulm_lvb_finish;

	lck->lvb = kmalloc (fs->lvb_size, GFP_KERNEL);
	if (lck->lvb == NULL) {
		err = -ENOMEM;
		goto fail;
	}
	memset (lck->lvb, 0, fs->lvb_size);

	item->lvb = lck->lvb;
	item->lvblen = fs->lvb_size;

	glq_queue (item);
	wait_for_completion (&ghlt.sleep);
	/* after here, item is no longer valid
	 * (memory was probably freed.)
	 * is why we use ghlt.error and not item->error.
	 */

	if (ghlt.error != lg_err_Ok) {
		log_err ("fsid=%s: Got error %d on hold lvb request.\n",
			 fs->fs_name, ghlt.error);
		kfree (lck->lvb);
		lck->lvb = NULL;
		goto fail;
	}

	*lvbp = lck->lvb;

	unmark_and_release_lock (lck);

	lvb_log_msg_lk (lck->key, "fsid=%s: Exiting gulm_hold_lvb\n",
			fs->fs_name);
	return 0;
      fail:
	unmark_and_release_lock (lck);
	if (err != 0)
		log_msg (lgm_Always,
			 "fsid=%s: Exiting gulm_hold_lvb with errors (%d)\n",
			 fs->fs_name, err);
	return err;
}

/**
 * gulm_unhold_lvb - 
 * @lock: 
 * @lvb: 
 * 
 * 
 * Returns: void
 */
void
gulm_unhold_lvb (lm_lock_t * lock, char *lvb)
{
	struct gulm_lvb_temp_s ghlt;
	glckr_t *item;
	gulm_lock_t *lck = (gulm_lock_t *) lock;
	gulm_fs_t *fs = lck->fs;

	mark_lock (lck);

	item = glq_get_new_req();
	if( item == NULL ) {
		log_err("unhold_lvb: failed to get needed memory. skipping.\n");
		goto exit;
	}

	item->key = lck->key;
	item->keylen = lck->keylen;
	item->subid = 0;
	item->start = 0;
	item->stop = ~((uint64_t)0);
	item->type = glq_req_type_action;
	item->state = lg_lock_act_UnHoldLVB;
	item->flags = 0;
	item->error = ghlt.error = 0;

	init_completion (&ghlt.sleep);

	item->misc = &ghlt;
	item->finish = gulm_lvb_finish;

	item->lvb = lck->lvb;
	item->lvblen = fs->lvb_size;

	glq_queue (item);
	wait_for_completion (&ghlt.sleep);
	/* after here, item is no longer valid
	 * (memory was probably freed.)
	 * is why we use ghlt.error and not item->error.
	 */

	if (ghlt.error != lg_err_Ok) {
		log_err ("fsid=%s: Got error %d on unhold LVB request.\n",
			 lck->fs->fs_name, ghlt.error);
	}
	/* free it always.  GFS thinks it is gone no matter what the server
	 * thinks. (and as much as i hate to say it this way, far better to
	 * leak in userspace than in kernel space.)
	 */
	if (lck->lvb != NULL)
		kfree (lck->lvb);
	lck->lvb = NULL;
      exit:
	unmark_and_release_lock (lck);
	lvb_log_msg ("Exiting gulm_unhold_lvb\n");
}

/**
 * gulm_sync_lvb - 
 * @lock: 
 * @lvb: 
 * 
 */
void
gulm_sync_lvb (lm_lock_t * lock, char *lvb)
{
	struct gulm_lvb_temp_s ghlt;
	glckr_t *item;
	gulm_lock_t *lck = (gulm_lock_t *) lock;
	gulm_fs_t *fs = lck->fs;

	mark_lock (lck);

	item = glq_get_new_req();
	if( item == NULL ) {
		log_err("sync_lvb: failed to get needed memory. skipping.\n");
		goto exit;
	}

	item->key = lck->key;
	item->keylen = lck->keylen;
	item->subid = 0;
	item->start = 0;
	item->stop = ~((uint64_t)0);
	item->type = glq_req_type_action;
	item->state = lg_lock_act_SyncLVB;
	item->flags = 0;
	item->error = ghlt.error = 0;

	init_completion (&ghlt.sleep);

	item->misc = &ghlt;
	item->finish = gulm_lvb_finish;

	item->lvb = lck->lvb;
	item->lvblen = fs->lvb_size;

	glq_queue (item);
	wait_for_completion (&ghlt.sleep);
	/* after here, item is no longer valid
	 * (memory was probably freed.)
	 * is why we use ghlt.error and not item->error.
	 */

	if (ghlt.error != lg_err_Ok) {
		log_err ("fsid=%s: Got error %d on sync LVB request.\n",
			 lck->fs->fs_name, ghlt.error);
	}

      exit:
	unmark_and_release_lock (lck);
	lvb_log_msg ("Exiting gulm_sync_lvb\n");

}

/* vim: set ai cin noet sw=8 ts=8 : */
