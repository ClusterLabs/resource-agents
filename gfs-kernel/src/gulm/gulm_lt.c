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

#include "util.h"
#include "handler.h"
#include "utils_tostr.h"
#include "gulm_jid.h"

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

static void __inline__
dump_gulm_lock_t (gulm_lock_t * lck)
{
	char bb[GIO_KEY_SIZE * 2 + 3];

	lck_key_to_hex (lck->key, lck->keylen, bb);
	log_msg (lgm_Always, " key = 0x%s\n", bb);
	log_msg (lgm_Always, " req_type = %#x\n", lck->req_type);
	log_msg (lgm_Always, " last_suc_state = %#x\n", lck->last_suc_state);
	log_msg (lgm_Always, " actuallypending = %d\n", lck->actuallypending);
	log_msg (lgm_Always, " in_to_be_sent = %d\n", lck->in_to_be_sent);
	log_msg (lgm_Always, " cur_state = %d\n", lck->cur_state);
	log_msg (lgm_Always, " req_state = %d\n", lck->req_state);
	log_msg (lgm_Always, " flags = %#x\n", lck->flags);
	log_msg (lgm_Always, " action = %d\n", lck->action);
	log_msg (lgm_Always, " result = %d\n", lck->result);
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
 * <type> is: G J F N
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

	memset (mask, 0, GIO_KEY_SIZE);

	mask[0] = 0xff;
	mask[1] = fsnlen;
	memcpy(&mask[2], fsname, fsnlen);
	mask[2 + fsnlen] = 0;
	/* rest should be 0xff */

	return 3 + fsnlen;
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
	bkt = hash_lock_key (key, keylen);
	bkt %= gulm_cm.ltpx.hashbuckets;

	spin_lock (&gulm_cm.ltpx.hshlk[bkt]);
	list_for_each (tmp, &gulm_cm.ltpx.lkhsh[bkt]) {
		lck = list_entry (tmp, gulm_lock_t, gl_list);
		if (memcmp (lck->key, key, keylen) == 0) {
			found = TRUE;
			atomic_inc (&lck->count);
			break;
		}
	}
	spin_unlock (&gulm_cm.ltpx.hshlk[bkt]);

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

	bkt = hash_lock_key (lck->key, lck->keylen);
	bkt %= gulm_cm.ltpx.hashbuckets;
	spin_lock (&gulm_cm.ltpx.hshlk[bkt]);
	if (atomic_dec_and_test (&lck->count)) {
		list_del (&lck->gl_list);
		deld = TRUE;
	}
	spin_unlock (&gulm_cm.ltpx.hshlk[bkt]);
	if (deld) {
		gulm_cm.ltpx.locks_total--;
		gulm_cm.ltpx.locks_unl--;
		if (lck->lvb != NULL) {
			kfree (lck->lvb);
		}
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
	 * embedded in the gulm lock key
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

void
do_drop_lock_req (gulm_fs_t * fs, uint8_t state, uint8_t key[GIO_KEY_SIZE])
{
	unsigned int type;
	struct lm_lockname lockname;
	/* i might want to shove most of this function into the new
	 * lockcallback handing queue.
	 * later.
	 */

	/* don't do callbacks on the gulm mount lock.
	 * */
	if (key[0] != 'G') {
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

	qu_drop_req (&fs->cq, fs->cb, fs->fsdata, type,
		     lockname.ln_type, lockname.ln_number);
}

/**
 * send_async_reply - 
 * @lck: 
 * 
 * 
 * Returns: void
 */
void
send_async_reply (gulm_lock_t * lck)
{
	gulm_fs_t *fs = lck->fs;
	struct lm_lockname lockname;

	if (lck->key[0] == 'F') {
		/* whee! it is the first mounter lock.  two things:
		 * A: gfs could care less about this.
		 * B: we need to up the sleeper in the fs.  (hack)
		 */
		complete (&fs->sleep);
		return;
	}

	if( lck->key[0] != 'G' ) return;

	gulm_key_to_lm_lockname (lck->key, &lockname);

	qu_async_rpl (&fs->cq, fs->cb, fs->fsdata, &lockname, lck->result);
}

/**
 * send_drop_exp_inter - 
 * @lt: 
 * @name: 
 * 
 * 
 * Returns: int
 */
int
send_drop_exp_inter (gulm_fs_t * fs, lock_table_t * lt, char *name)
{
	int err, len;
	uint8_t mask[GIO_KEY_SIZE];

	len = pack_drop_mask(mask, GIO_KEY_SIZE, fs->fs_name); 

	err = lg_lock_drop_exp (gulm_cm.hookup, name, mask, len);

	return err;
}

/**
 * send_lock_action - 
 * @lck: 
 * 
 * 
 * Returns: int
 */
int
send_lock_action (gulm_lock_t * lck, uint8_t action)
{
	int err;

	GULM_ASSERT (lck->req_type == glck_action, dump_gulm_lock_t (lck););

	err = lg_lock_action_req (gulm_cm.hookup, lck->key, lck->keylen,
				  0, action, lck->lvb, lck->fs->lvb_size);
	if (err != 0)
		log_err ("Issues sending action request. %d\n", err);

	return err;
}

/**
 * send_lock_req - 
 * @lck: 
 * 
 * 
 * Returns: int
 */
int
send_lock_req (gulm_lock_t * lck)
{
	gulm_fs_t *fs = lck->fs;
	int err;
	uint32_t flags = 0;
	uint8_t state;

	GULM_ASSERT (lck->req_type == glck_state, dump_gulm_lock_t (lck););

	switch (lck->req_state) {
	case LM_ST_EXCLUSIVE:
		state = lg_lock_state_Exclusive;
		break;
	case LM_ST_DEFERRED:
		state = lg_lock_state_Deferred;
		break;
	case LM_ST_SHARED:
		state = lg_lock_state_Shared;
		break;
	case LM_ST_UNLOCKED:
		state = lg_lock_state_Unlock;
		break;
	default:
		GULM_ASSERT (0, log_err ("fsid=%s: Anit no lock state %d.\n",
					 fs->fs_name, lck->req_state););
		break;
	}
	if (lck->flags & LM_FLAG_TRY) {
		flags |= lg_lock_flag_Try;
	}
	if (lck->flags & LM_FLAG_TRY_1CB) {
		flags |= lg_lock_flag_Try | lg_lock_flag_DoCB;
	}
	if (lck->flags & LM_FLAG_NOEXP) {
		flags |= lg_lock_flag_IgnoreExp;
	}
	if (lck->flags & LM_FLAG_ANY) {
		flags |= lg_lock_flag_Any;
	}
	if (lck->flags & LM_FLAG_PRIORITY) {
		flags |= lg_lock_flag_Piority;
	}
	if (lck->lvb != NULL) {
		print_lk_lvb (lck->key, lck->lvb, lck->req_state, "Sending");
	}

	err = lg_lock_state_req (gulm_cm.hookup, lck->key, lck->keylen,
				 0, 0, ~((uint64_t)0),
				 state, flags, lck->lvb, lck->fs->lvb_size);
	if (err != 0)
		log_err ("Issues sending state request. %d\n", err);

	return err;
}

/**
 * toggle_lock_counters - 
 * 
 * called after a succesful request to change lock state.  Decrements
 * counts for what the lock was, and increments for what it is now.
 */
void
toggle_lock_counters (lock_table_t * lt, int old, int new)
{
	/* what we had it in */
	switch (old) {
	case LM_ST_EXCLUSIVE:
		lt->locks_exl--;
		break;
	case LM_ST_DEFERRED:
		lt->locks_dfr--;
		break;
	case LM_ST_SHARED:
		lt->locks_shd--;
		break;
	case LM_ST_UNLOCKED:
		lt->locks_unl--;
		break;
	}
	/* what we have it in */
	switch (new) {
	case LM_ST_EXCLUSIVE:
		lt->locks_exl++;
		break;
	case LM_ST_DEFERRED:
		lt->locks_dfr++;
		break;
	case LM_ST_SHARED:
		lt->locks_shd++;
		break;
	case LM_ST_UNLOCKED:
		lt->locks_unl++;
		break;
	}
}

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
	gulm_fs_t *fs = lck->fs;
	lock_table_t *lt = &gulm_cm.ltpx;
	int result = -69;

	/* adjust result based on success status. */
	switch (error) {
	case lg_err_Ok:
		/* set result to current lock state. */
		if (!(lck->flags & LM_FLAG_ANY)) {
			/* simple case, we got what we asked for. */
			result = lck->req_state;
		} else {
			/* complex case, we got something else, but we said that was ok */
			switch (state) {
			case lg_lock_state_Shared:
				result = LM_ST_SHARED;
				break;
			case lg_lock_state_Deferred:
				result = LM_ST_DEFERRED;
				break;

			case lg_lock_state_Exclusive:
			case lg_lock_state_Unlock:
				GULM_ASSERT (0,
					     dump_gulm_lock_t (lck);
					     log_err
					     ("fsid=%s: lock state %d is invalid on "
					      "ANY flag return\n", fs->fs_name,
					      state);
				    );
				break;

			default:
				GULM_ASSERT (0,
					     dump_gulm_lock_t (lck);
					     log_err_lck (lck,
							  "fsid=%s: Anit no lock state %d.\n",
							  fs->fs_name, state);
				    );
				break;
			}
		}

		/* toggle counters.
		 * due to ANY flag, new state may not be req_state.
		 * */
		toggle_lock_counters (lt, lck->cur_state, result);

		/* if no internal unlocks, it is cachable. */
		if (result != LM_ST_UNLOCKED && (flags & lg_lock_flag_Cachable))
			result |= LM_OUT_CACHEABLE;

		/* record and move on
		 * */
		lck->last_suc_state = result & LM_OUT_ST_MASK;
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

/**
 * my_strdup - 
 * @s: 
 * 
 * 
 * Returns: char
 */
char *
my_strdup (char *s)
{
	char *tmp;
	int len;
	len = strlen (s) + 1;
	tmp = kmalloc (len, GFP_KERNEL);
	if (tmp == NULL)
		return NULL;
	memcpy (tmp, s, len);
	return tmp;
}

/* Instead of directly calling the send function below, the functions will
 * create of of these.
 * Which exist only because I cannot stick the lock_t onto two lists
 * at once.
 *
 * this could use some clean up.
 */
typedef struct send_req_s {
	struct list_head sr_list;
	enum { sr_lock, sr_act, sr_cancel, sr_drop } type;
	gulm_lock_t *who;
	gulm_fs_t *fs;
	lock_table_t *lt;
	char *name;
} send_req_t;

/**
 * alloc_send_req - 
 * @oid: 
 * 
 * 
 * Returns: send_req_t
 */
send_req_t *
alloc_send_req (void)
{
	send_req_t *tmp;
	tmp = kmalloc (sizeof (send_req_t), GFP_KERNEL);
	GULM_ASSERT (tmp != NULL,);	/* so evil.... */
	return tmp;
}

/**
 * send_drop_exp - 
 * @fs: 
 * @lt: 
 * @name: 
 * 
 * 
 * Returns: int
 */
int
send_drop_exp (gulm_fs_t * fs, lock_table_t * lt, char *name)
{
	send_req_t *sr;

	sr = alloc_send_req ();
	INIT_LIST_HEAD (&sr->sr_list);
	sr->type = sr_drop;
	sr->who = NULL;
	sr->fs = fs;
	sr->lt = lt;
	if (name != NULL) {
		sr->name = my_strdup (name);
	} else {
		sr->name = NULL;
	}

	spin_lock (&lt->queue_sender);
	list_add (&sr->sr_list, &lt->to_be_sent);
	spin_unlock (&lt->queue_sender);

	wake_up (&lt->send_wchan);
	return 0;
}

/**
 * add_lock_to_send_req_queue - 
 * @lt: 
 * @lck: 
 * 
 * 
 * Returns: void
 */
void
add_lock_to_send_req_queue (lock_table_t * lt, gulm_lock_t * lck, int type)
{
	send_req_t *sr;

	sr = alloc_send_req ();
	INIT_LIST_HEAD (&sr->sr_list);
	sr->type = type;
	sr->who = lck;
	sr->fs = NULL;
	sr->lt = NULL;
	sr->name = NULL;
	if (type != sr_cancel)
		lck->in_to_be_sent = TRUE;

	mark_lock (lck);

	spin_lock (&lt->queue_sender);
	list_add (&sr->sr_list, &lt->to_be_sent);
	spin_unlock (&lt->queue_sender);

	wake_up (&lt->send_wchan);
}

/**
 * queue_empty - 
 * @lt: 
 * 
 * 
 * Returns: int
 */
static __inline__ int
queue_empty (lock_table_t * lt)
{
	int ret;
	spin_lock (&lt->queue_sender);
	ret = list_empty (&lt->to_be_sent);
	spin_unlock (&lt->queue_sender);
	return ret;
}

/**
 * lt_io_sender_thread - 
 * @data: 
 *
 * Right now, only gfs lock requests should go through this thread.
 * Must look, May not even need this.
 * well, it is nice to get the socket io off of what ever process the user
 * is running that is going through gfs into here. ?is it?
 *
 * 
 * Returns: int
 */
int
lt_io_sender_thread (void *data)
{
	lock_table_t *lt = (lock_table_t *) data;
	struct list_head *tmp;
	send_req_t *sr = NULL;
	int err = 0;

	daemonize ("gulm_LT_sender");
	lt->sender_task = current;
	complete (&lt->startup);

	while (lt->running) {
		do {
			DECLARE_WAITQUEUE (__wait_chan, current);
			current->state = TASK_INTERRUPTIBLE;
			add_wait_queue (&lt->send_wchan, &__wait_chan);
			if (queue_empty (lt))
				schedule ();
			remove_wait_queue (&lt->send_wchan, &__wait_chan);
			current->state = TASK_RUNNING;
		} while (0);
		if (!lt->running)
			break;

		/* check to make sure socket is ok. */
		down (&lt->sender);

		/* pop next item to be sent
		 *  (it will get pushed back if there was problems.)
		 */
		spin_lock (&lt->queue_sender);
		if (list_empty (&lt->to_be_sent)) {
			spin_unlock (&lt->queue_sender);
			up (&lt->sender);
			continue;
		}
		tmp = (&lt->to_be_sent)->prev;
		list_del (tmp);
		spin_unlock (&lt->queue_sender);
		sr = list_entry (tmp, send_req_t, sr_list);

		/* send. */
		if (sr->type == sr_lock) {
			err = send_lock_req (sr->who);
			if (err == 0) {
				sr->who->in_to_be_sent = FALSE;
				unmark_and_release_lock (sr->who);
			}
		} else if (sr->type == sr_act) {
			err = send_lock_action (sr->who, sr->who->action);
			if (err == 0) {
				sr->who->in_to_be_sent = FALSE;
				unmark_and_release_lock (sr->who);
			}
		} else if (sr->type == sr_cancel) {
			err =
			    lg_lock_cancel_req (gulm_cm.hookup, sr->who->key,
						sr->who->keylen, 0);
			if (err == 0)
				unmark_and_release_lock (sr->who);
		} else if (sr->type == sr_drop) {
			/* XXX sr->lt isn't really needed.
			 * just lt should be fine.
			 * look into it someday.
			 */
			err = send_drop_exp_inter (sr->fs, sr->lt, sr->name);
		} else {
			log_err ("Unknown send_req type! %d\n", sr->type);
		}
		up (&lt->sender);

		/* if no errors, remove from queue. */
		if (err == 0) {
			if (sr->type == sr_drop && sr->name != NULL)
				kfree (sr->name);
			kfree (sr);
			sr = NULL;
		} else {
			/* if errors, re-queue.
			 * the send_* funcs already reported the error, so we won't
			 * repeat that.
			 * */
			spin_lock (&lt->queue_sender);
			/* reset the pointers. otherwise things get weird. */
			INIT_LIST_HEAD (&sr->sr_list);
			list_add_tail (&sr->sr_list, &lt->to_be_sent);
			spin_unlock (&lt->queue_sender);

			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout (3 * HZ);

			/* gotta break shit up.
			 * else this loops hard and fast.
			 */
		}
	}			/* while( lt->running ) */

	complete (&lt->startup);
	return 0;
}

/**
 * cancel_pending_sender - 
 * @lck: 
 * 
 * want to cancel a lock request that we haven't sent to the server yet.
 * 
 * this must skip over unlock requests. (never cancel unlocks)
 * 
 * Returns: int
 */
int
cancel_pending_sender (gulm_lock_t * lck)
{
	lock_table_t *lt = &gulm_cm.ltpx;
	struct list_head *tmp, *nxt;
	send_req_t *sr;
	int found = FALSE;

	spin_lock (&lt->queue_sender);

	list_for_each_safe (tmp, nxt, &lt->to_be_sent) {
		sr = list_entry (tmp, send_req_t, sr_list);
		if (sr->who == lck) {	/* good enough? */
			if (lck->req_type == sr_cancel)
				continue;
			if (lck->req_state == LM_ST_UNLOCKED)
				continue;	/*donot cancel unlocks */
			list_del (tmp);
			kfree (sr);
			found = TRUE;
			lck->in_to_be_sent = FALSE;

			/* Now we need to tell the waiting lock req that it got canceled.
			 * basically, we need to fake a lg_err_Canceled return....
			 */
			lck->result = LM_OUT_CANCELED | lck->cur_state;
			lck->actuallypending = FALSE;
			lck->req_type = glck_nothing;
			atomic_dec (&lt->locks_pending);
#ifndef USE_SYNC_LOCKING
			send_async_reply (lck);
#else
			complete (&lck->actsleep);
#endif
			unmark_and_release_lock (lck);
			break;
		}
	}

	spin_unlock (&lt->queue_sender);
	return found;
}

/**
 * gulm_lt_login_reply - 
 * @misc: 
 * @error: 
 * @which: 
 * 
 * 
 * Returns: int
 */
int
gulm_lt_login_reply (void *misc, uint32_t error, uint8_t which)
{
	if (error != 0) {
		gulm_cm.ltpx.running = FALSE;
		log_err ("LTPX: Got a %d from the login request.\n", error);
	} else {
		log_msg (lgm_Network2, "Logged into local LTPX.\n");
	}
	return error;
}

/**
 * gulm_lt_logout_reply - 
 * @misc: 
 * 
 * 
 * Returns: int
 */
int
gulm_lt_logout_reply (void *misc)
{
	gulm_cm.ltpx.running = FALSE;
	log_msg (lgm_Network2, "Logged out of local LTPX.\n");
	return 0;
}

/**
 * gulm_lt_lock_state - 
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
gulm_lt_lock_state (void *misc, uint8_t * key, uint16_t keylen,
		    uint64_t subid, uint64_t start, uint64_t stop,
		    uint8_t state, uint32_t flags, uint32_t error,
		    uint8_t * LVB, uint16_t LVBlen)
{
	gulm_lock_t *lck;

	if (key[0] == 'J' || key[0] == 'N' ) {
		jid_state_reply (key, keylen, LVB, LVBlen);
		return 0;
	}

	if (!find_and_mark_lock (key, keylen, &lck)) {
		log_err_lk (key, keylen, "Got a lock state reply for a lock "
			    "that we don't know of. state:%#x flags:%#x error:%#x\n",
			    state, flags, error);
		return 0;
	}

	lck->result = calc_lock_result (lck, state, error, flags);

	if ((lck->result & LM_OUT_ST_MASK) != LM_ST_UNLOCKED &&
	    lck->lvb != NULL) {
		memcpy (lck->lvb, LVB, MIN (lck->fs->lvb_size, LVBlen));
	}

	lck->actuallypending = FALSE;
	lck->req_type = glck_nothing;
	atomic_dec (&gulm_cm.ltpx.locks_pending);
#ifndef USE_SYNC_LOCKING
	send_async_reply (lck);
#else
	complete (&lck->actsleep);
#endif

	if (error != 0 && error != lg_err_TryFailed && error != lg_err_Canceled)
		log_msg_lck (lck, "Error: %d:%s (req:%#x rpl:%#x lss:%#x)\n",
			     error, gio_Err_to_str (error),
			     lck->req_state, state, lck->last_suc_state);

	unmark_and_release_lock (lck);
	return 0;
}

/**
 * gulm_lt_lock_action - 
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
gulm_lt_lock_action (void *misc, uint8_t * key, uint16_t keylen,
		     uint64_t subid, uint8_t action, uint32_t error)
{
	gulm_lock_t *lck;

	if (key[0] == 'J') {
		jid_action_reply (key, keylen);
		return 0;
	}

	if (!find_and_mark_lock (key, keylen, &lck)) {
		log_err_lk (key, keylen, "Got a lock action reply for a lock "
			    "that we don't know of. action:%#x error:%#x\n",
			    action, error);
		return 0;
	}

	if (action == lg_lock_act_HoldLVB ||
	    action == lg_lock_act_UnHoldLVB || action == lg_lock_act_SyncLVB) {
		/*  */
		lck->result = error;
		if (error != lg_err_Ok) {
			log_err ("on action reply act:%d err:%d\n", action,
				 error);
		}
		lck->req_type = glck_nothing;
		lck->actuallypending = FALSE;
		complete (&lck->actsleep);
	} else {
		log_err_lck (lck, "Got strange Action %#x\n", action);
	}
	unmark_and_release_lock (lck);
	return 0;
}

/**
 * gulm_lt_drop_lock_req - 
 * @misc: 
 * @key: 
 * @keylen: 
 * @state: 
 * 
 * 
 * Returns: int
 */
int
gulm_lt_drop_lock_req (void *misc, uint8_t * key, uint16_t keylen,
		       uint64_t subid, uint8_t state)
{
	gulm_lock_t *lck;

	if (key[0] == 'J') {
		jid_header_lock_drop (key, keylen);
		return 0;
	}

	if (!find_and_mark_lock (key, keylen, &lck)) {
		log_err_lk (key, keylen, "Got a drop lcok request for a lock "
			    "that we don't know of. state:%#x\n", state);
		return 0;
	}

	do_drop_lock_req (lck->fs, state, key);

	unmark_and_release_lock (lck);
	return 0;
}

/**
 * gulm_lt_drop_all - 
 * @misc: 
 * 
 * 
 * Returns: int
 */
int
gulm_lt_drop_all (void *misc)
{
	passup_droplocks ();
	return 0;
}

/**
 * gulm_lt_error - 
 * @misc: 
 * @err: 
 * 
 * 
 * Returns: int
 */
int
gulm_lt_error (void *misc, uint32_t err)
{
	log_err ("LTPX: RANDOM ERROR %d\n", err);
	return err;
}

static lg_lockspace_callbacks_t lock_cb = {
      login_reply:gulm_lt_login_reply,
      logout_reply:gulm_lt_logout_reply,
      lock_state:gulm_lt_lock_state,
      lock_action:gulm_lt_lock_action,
      drop_lock_req:gulm_lt_drop_lock_req,
      drop_all:gulm_lt_drop_all,
      error:gulm_lt_error
};

/**
 * lt_io_recving_thread - 
 * @data: 
 * 
 * 
 * Returns: int
 */
int
lt_io_recving_thread (void *data)
{
	lock_table_t *lt = &gulm_cm.ltpx;
	int err;

	daemonize ("gulm_LT_recver");
	lt->recver_task = current;
	complete (&lt->startup);

	while (lt->running) {
		err = lg_lock_handle_messages (gulm_cm.hookup, &lock_cb, NULL);
		if (err != 0) {
			log_err ("gulm_LT_recver err %d\n", err);
			lt->running = FALSE;	/* should stop the sender thread. */
			wake_up (&lt->send_wchan);
			break;
		}
	}			/* while( lt->running ) */

	complete (&lt->startup);
	return 0;
}

/**
 * lt_logout - log out of all of the lock tables
 */
void
lt_logout (void)
{
	lock_table_t *lt = &gulm_cm.ltpx;
	int err;

	if (lt->running) {
		lt->running = FALSE;

		/* stop sender thread */
		wake_up (&lt->send_wchan);
		wait_for_completion (&lt->startup);

		/* stop recver thread */
		down (&lt->sender);
		err = lg_lock_logout (gulm_cm.hookup);
		up (&lt->sender);

		/* wait for thread to finish */
		wait_for_completion (&lt->startup);
	}

}

/**
 * lt_login - login to lock tables.
 * 
 * Returns: int
 */
int
lt_login (void)
{
	int err;
	lock_table_t *lt = &gulm_cm.ltpx;

	if (lt->running)
		log_err
		    ("Trying to log into LTPX when it appears to be logged in!\n");

	err = lg_lock_login (gulm_cm.hookup, "GFS ");
	if (err != 0) {
		log_err ("Failed to send login request. %d\n", err);
		goto fail;
	}

	/* start recver thread. */
	lt->running = TRUE;
	err = kernel_thread (lt_io_recving_thread, lt, 0);
	if (err < 0) {
		log_err ("Failed to start gulm_lt_IOd. (%d)\n", err);
		goto fail;
	}
	wait_for_completion (&lt->startup);

	/* start sender thread */
	err = kernel_thread (lt_io_sender_thread, lt, 0);
	if (err < 0) {
		log_err ("Failed to start gulm_LT_sender. (%d)\n", err);
		goto fail;
	}
	wait_for_completion (&lt->startup);

	return 0;
      fail:
	lt_logout ();
	log_msg (lgm_Always, "Exiting lt_login. err:%d\n", err);
	return err;
}

/****************************************************************************/

/**
 * internal_gulm_get_lock - 
 * @fs: 
 * @key: 
 * @keylen: 
 * @lockp: 
 * 
 * 
 * Returns: 0 on success, -EXXX on failure
 */
int
internal_gulm_get_lock (gulm_fs_t * fs, uint8_t * key, uint8_t keylen,
			gulm_lock_t ** lockp)
{
	int found = FALSE;
	uint32_t bkt;
	gulm_lock_t *lck = NULL;

	found = find_and_mark_lock (key, keylen, &lck);

	/* malloc space */
	if (found) {
		GULM_ASSERT (lck->magic_one == 0xAAAAAAAA,);
	} else {
		lck = kmalloc (sizeof (gulm_lock_t), GFP_KERNEL);
		if (lck == NULL) {
			log_err
			    ("fsid=%s: Out of memory for lock struct in get_lock!\n",
			     fs->fs_name);
			return -ENOMEM;
		}
		memset (lck, 0, sizeof (gulm_lock_t));
		INIT_LIST_HEAD (&lck->gl_list);
		atomic_set (&lck->count, 1);
		lck->magic_one = 0xAAAAAAAA;
		lck->fs = fs;
		memcpy (lck->key, key, keylen);
		lck->keylen = keylen;
		lck->lvb = NULL;
		init_completion (&lck->actsleep);
		lck->actuallypending = FALSE;
		lck->in_to_be_sent = FALSE;
		lck->result = 0;
		lck->action = -1;
		lck->req_type = glck_nothing;
		lck->last_suc_state = LM_ST_UNLOCKED;

		gulm_cm.ltpx.locks_total++;
		gulm_cm.ltpx.locks_unl++;

		bkt = hash_lock_key (key, keylen);
		bkt %= gulm_cm.ltpx.hashbuckets;

		spin_lock (&gulm_cm.ltpx.hshlk[bkt]);
		list_add (&lck->gl_list, &gulm_cm.ltpx.lkhsh[bkt]);
		spin_unlock (&gulm_cm.ltpx.hshlk[bkt]);
	}

	*lockp = lck;

	return 0;
}

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
	int err, len;
	gulm_fs_t *fs = (gulm_fs_t *) lockspace;
	uint8_t key[GIO_KEY_SIZE]; uint8_t temp[9];

	down (&fs->get_lock);


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

	err = internal_gulm_get_lock (fs, key, len, (gulm_lock_t **) lockp);

	up (&fs->get_lock);
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
	gulm_lock_t *lck = (gulm_lock_t *) lock;
	lock_table_t *lt = &gulm_cm.ltpx;
	gulm_fs_t *fs = lck->fs;

	down (&fs->get_lock);

	GULM_ASSERT (lt != NULL,);

	if (lck->last_suc_state != LM_ST_UNLOCKED) {
		log_err_lck (lck,
			     "fsid=%s: gulm_put_lock called on a lock that is not unlocked!"
			     " Current state:%#x\n", lck->fs->fs_name,
			     lck->last_suc_state);
		/* I'm still not sure about this one.  We should never see it, so I
		 * don't think it is that big of a deal, but i duno.
		 *
		 * Maybe should just make it an assertion.
		 *
		 * with the mark/unmark code, is it even a concern?
		 */
	}

	unmark_and_release_lock (lck);
	/* lck = NULL; */

	up (&fs->get_lock);

}

static int
valid_trasition (unsigned int cur, unsigned int req)
{
	int lock_state_changes[16] = {	/* unl   exl    def    shr  */
		FALSE, TRUE, TRUE, TRUE,	/* unl */
		TRUE, FALSE, TRUE, TRUE,	/* exl */
		TRUE, TRUE, FALSE, TRUE,	/* def */
		TRUE, TRUE, TRUE, FALSE	/* shr */
	};
	GULM_ASSERT (cur < 4
		     && req < 4, log_err ("cur:%d req:%d\n", cur, req););

	return (lock_state_changes[4 * cur + req]);
}

/**
 * verify_gulm_lock_t - 
 * @lck: 
 * 
 * wonder if I should add some other checks.
 * 
 * Returns: int
 */
int
verify_gulm_lock_t (gulm_lock_t * lck)
{
	if (lck == NULL) {
		log_err ("Lock pointer was NULL!\n");
		return -1;
	}
	if (lck->fs == NULL) {
		log_err ("This lock has no filesystem!!!\n");
		return -1;
	}
	return 0;
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
	gulm_lock_t *lck = NULL;
	gulm_fs_t *fs;
	lock_table_t *lt;

	/* verify vars. */
	lck = (gulm_lock_t *) lock;
	if (verify_gulm_lock_t (lck) != 0) {
		return -EINVAL;
	}
	lt = &gulm_cm.ltpx;
	fs = lck->fs;

	GULM_ASSERT (valid_trasition (cur_state, req_state),
		     log_err_lck (lck, "want %d with %s thinks:%d\n", req_state,
				  (LM_FLAG_TRY & flags) ? "try" : (LM_FLAG_NOEXP
								   & flags) ?
				  "noexp" : "no flags", cur_state);
	    );

	GULM_ASSERT (lck->actuallypending == FALSE, dump_gulm_lock_t (lck););

	/* save the details of this request. */
	lck->req_type = glck_state;
	lck->result = 0;
	lck->cur_state = cur_state;
	lck->req_state = req_state;
	lck->flags = flags;

	/* moving these here fixes a race on the s390 that ben found.
	 * basically, the request was sent to the server, the server receives
	 * it, the server processes, the server sends a reply, the client
	 * receives the reply, and the client tries to processe the reply before
	 * this thread could mark it as actuallypending.
	 * */
	lck->actuallypending = TRUE;
	atomic_inc (&lt->locks_pending);
	add_lock_to_send_req_queue (lt, lck, sr_lock);

	lt->lops++;
#ifdef USE_SYNC_LOCKING
	wait_for_completion (&lck->actsleep);
#endif

#ifdef USE_SYNC_LOCKING
	return lck->result;
#else
	return LM_OUT_ASYNC;
#endif
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
	gulm_lock_t *lck;
	gulm_fs_t *fs;
	lock_table_t *lt;

	/* verify vars. */
	lck = (gulm_lock_t *) lock;
	if (verify_gulm_lock_t (lck) != 0) {
		return;
	}
	lt = &gulm_cm.ltpx;
	fs = lck->fs;

	if (lck->actuallypending) {
		if (lck->in_to_be_sent) {
			/* this should pull the req out of the send queue and have it
			 * return with a cancel code without going to the server.
			 */
			cancel_pending_sender (lck);
		} else {
			add_lock_to_send_req_queue (lt, lck, sr_cancel);
		}
	} else {
		log_msg_lck (lck, "Cancel called with no pending request.\n");
	}

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
	gulm_lock_t *lck;
	gulm_fs_t *fs;
	lock_table_t *lt;
	int err = -1;

	/* verify vars. */
	lck = (gulm_lock_t *) lock;
	if (verify_gulm_lock_t (lck) != 0) {
		return -EINVAL;
	}
	lt = &gulm_cm.ltpx;
	fs = lck->fs;

	/* what where these for? */
	GULM_ASSERT (lck->magic_one == 0xAAAAAAAA,
		     log_msg_lck (lck, "Bad gulm_lock magic.\n"););
	GULM_ASSERT (lt->magic_one == 0xAAAAAAAA,
		     log_msg_lck (lck, "Bad lock_table magic.\n"););

	lvb_log_msg_lk (lck->key, "Entering gulm_hold_lvb\n");

	GULM_ASSERT (lck->lvb == NULL,
		     log_msg_lck (lck,
				  "fsid=%s: Lvb data wasn't null! must be held "
				  "already.\n", fs->fs_name);
	    );

	GULM_ASSERT (lck->actuallypending == FALSE, dump_gulm_lock_t (lck););

	lck->lvb = kmalloc (fs->lvb_size, GFP_KERNEL);
	if (lck->lvb == NULL) {
		err = -ENOMEM;
		goto fail;
	}
	memset (lck->lvb, 0, fs->lvb_size);

	lck->req_type = glck_action;
	lck->action = lg_lock_act_HoldLVB;
	lck->result = 0;
	lck->actuallypending = TRUE;
	add_lock_to_send_req_queue (lt, lck, sr_act);

	wait_for_completion (&lck->actsleep);

	if (lck->result != lg_err_Ok) {
		log_err ("fsid=%s: Got error %d on hold lvb request.\n",
			 fs->fs_name, lck->result);
		kfree (lck->lvb);
		lck->lvb = NULL;
		goto fail;
	}

	lt->locks_lvbs++;

	*lvbp = lck->lvb;

	lvb_log_msg_lk (lck->key, "fsid=%s: Exiting gulm_hold_lvb\n",
			fs->fs_name);
	return 0;
      fail:
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
	gulm_lock_t *lck = NULL;
	gulm_fs_t *fs;
	lock_table_t *lt;

	/* verify vars. */
	lck = (gulm_lock_t *) lock;
	if (verify_gulm_lock_t (lck) != 0) {
		return;
	}
	lt = &gulm_cm.ltpx;
	fs = lck->fs;

	GULM_ASSERT (lck->actuallypending == FALSE, dump_gulm_lock_t (lck););

	if (lck->lvb != lvb) {
		log_err ("fsid=%s: AH! LVB pointer missmatch! %p != %p\n",
			 fs->fs_name, lck->lvb, lvb);
		goto exit;
	}

	lvb_log_msg_lk (lck->key, "Entering gulm_unhold_lvb\n");

	lck->req_type = glck_action;
	lck->action = lg_lock_act_UnHoldLVB;
	lck->result = 0;
	lck->actuallypending = TRUE;
	add_lock_to_send_req_queue (lt, lck, sr_act);

	wait_for_completion (&lck->actsleep);

	/* XXX ummm, is it sane to not free the memory if the command fails?
	 * gfs will still think that the lvb was dropped sucessfully....
	 * (it assumes it is always sucessful)
	 * Maybe I should retry the drop request then?
	 */
	if (lck->result != lg_err_Ok) {
		log_err ("fsid=%s: Got error %d on unhold LVB request.\n",
			 lck->fs->fs_name, lck->result);
	} else {
		if (lck->lvb != NULL)
			kfree (lck->lvb);
		lck->lvb = NULL;
		lt->locks_lvbs--;
	}
      exit:
	lvb_log_msg ("Exiting gulm_unhold_lvb\n");
}

/**
 * gulm_sync_lvb - 
 * @lock: 
 * @lvb: 
 * 
 * umm, is this even used anymore? yes.
 * 
 * Returns: void
 */
void
gulm_sync_lvb (lm_lock_t * lock, char *lvb)
{
	gulm_lock_t *lck = NULL;
	gulm_fs_t *fs;
	lock_table_t *lt;

	/* verify vars. */
	lck = (gulm_lock_t *) lock;
	if (verify_gulm_lock_t (lck) != 0) {
		return;
	}
	lt = &gulm_cm.ltpx;
	fs = lck->fs;

	GULM_ASSERT (lck->actuallypending == FALSE, dump_gulm_lock_t (lck););

	/* this check is also in the server, so it isn't really needed here. */
	if (lck->last_suc_state != LM_ST_EXCLUSIVE) {
		log_err ("sync_lvb: You must hold the lock Exclusive first.\n");
		goto exit;	/*cannot do anything */
	}
	if (lck->lvb == NULL) {
		log_err ("sync_lvb: You forgot to call hold lvb first.\n");
		goto exit;
	}
	if (lck->lvb != lvb) {
		log_err ("fsid=%s: AH! LVB pointer missmatch! %p != %p\n",
			 fs->fs_name, lck->lvb, lvb);
		goto exit;
	}

	lvb_log_msg_lk (lck->key, "Entering gulm_sync_lvb\n");

	lck->req_type = glck_action;
	lck->action = lg_lock_act_SyncLVB;
	lck->result = 0;
	lck->actuallypending = TRUE;
	add_lock_to_send_req_queue (lt, lck, sr_act);

	wait_for_completion (&lck->actsleep);

	/* XXX? retry if I get an error? */
	if (lck->result != lg_err_Ok) {
		log_err_lck (lck,
			     "fsid=%s: Got error %d:%s on Sync LVB request.\n",
			     fs->fs_name, lck->result,
			     gio_Err_to_str (lck->result));
	}
      exit:
	lvb_log_msg ("Exiting gulm_sync_lvb\n");
}

/*****************************************************************************/
static int
gulm_plock_get (lm_lockspace_t * lockspace,
		struct lm_lockname *name, unsigned long owner,
		uint64_t * start, uint64_t * end, int *exclusive,
		unsigned long *rowner)
{
	return -ENOSYS;
}

static int
gulm_plock (lm_lockspace_t * lockspace,
	    struct lm_lockname *name, unsigned long owner,
	    int wait, int exclusive, uint64_t start, uint64_t end)
{
	return -ENOSYS;
}

static int
gulm_punlock (lm_lockspace_t * lockspace,
	      struct lm_lockname *name, unsigned long owner,
	      uint64_t start, uint64_t end)
{
	return -ENOSYS;
}

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/
/* should move the firstmounter lock stuff into its own file perhaps? */
/**
 * get_special_lock - 
 * @fs: <> filesystem we're getting special lock for
 *
 * Returns: gulm_lock_t
 */
STATIC gulm_lock_t *
get_special_lock (gulm_fs_t * fs)
{
	int err, len;
	gulm_lock_t *lck = NULL;
	uint8_t key[GIO_KEY_SIZE];

	len = pack_lock_key(key, GIO_KEY_SIZE, 'F', fs->fs_name, "irstMount", 9);
	if( len <= 0 ) return NULL;

	err = internal_gulm_get_lock (fs, key, len, &lck);

	/* return pointer */
	return lck;
}

/**
 * do_lock_time_out - 
 * @d: 
 *
 * after timeout, set cancel request on the handler queue. (since we cannot
 * call it from within the timer code.
 * 
 */
static void
do_lock_time_out (unsigned long d)
{
	gulm_lock_t *lck = (gulm_lock_t *) d;
	qu_function_call (&lck->fs->cq, gulm_cancel, lck);
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
	int err;
	struct timer_list locktimeout;
	gulm_lock_t *lck = NULL;
	/*
	 * first we need to get the lock into the hash.
	 * then we can try to get it Exl with try and noexp.
	 * if the try fails, grab it shared.
	 */

	lck = get_special_lock (fs);	/* there is only a mount lock. */
	if (lck == NULL) {
		err = -ENOMEM;
		goto fail;
	}

	fs->mountlock = lck;
      try_it_again:
	*first = FALSE;		/* assume we're not first */

	err = gulm_lock (lck, LM_ST_UNLOCKED, LM_ST_EXCLUSIVE,
			 LM_FLAG_TRY | LM_FLAG_NOEXP);
#ifndef USE_SYNC_LOCKING
	wait_for_completion (&fs->sleep);
#endif

	if ((lck->result & LM_OUT_ST_MASK) == LM_ST_EXCLUSIVE) {
		/* we got the lock, we're the first mounter. */
		*first = TRUE;
		log_msg (lgm_locking, "fsid=%s: Got mount lock Exclusive.\n",
			 fs->fs_name);
		return 0;
	} else if ((lck->result & LM_OUT_ST_MASK) == LM_ST_UNLOCKED) {
		log_msg (lgm_locking,
			 "fsid=%s: Didn't get mount lock Exl, someone else "
			 "was first, trying for shared.\n", fs->fs_name);

		/* the try failed, pick it up shared. */
		/* There was a case (bug #220) where we could hang here.
		 *
		 * To handle this, we put up a timer for a couple of
		 * minutes.  That if it trips, it cancels our shared
		 * request.  Which we then see, so we go back and try the
		 * EXL again.  If the Firstmounter is fine and is just
		 * taking a damn long time to do its work, this just ends
		 * back here, no worse for the wear.
		 *
		 * Another way to do this, is to wait for a killed message
		 * for the master.  When we get that, && we're pending
		 * shared here, send the gulm_canel for the mounter lock.
		 * (too bad we are not in the fs list yet at this point.
		 * (well, maybe that *isn't* a bad thing))
		 */
		init_timer (&locktimeout);
		locktimeout.function = do_lock_time_out;
		locktimeout.data = (unsigned long) lck;
		mod_timer (&locktimeout, jiffies + (120 * HZ));
		err = gulm_lock (lck, LM_ST_UNLOCKED, LM_ST_SHARED, 0);
#ifndef USE_SYNC_LOCKING
		wait_for_completion (&fs->sleep);
#endif
		del_timer (&locktimeout);

		if ((lck->result & LM_OUT_ST_MASK) == LM_ST_SHARED) {
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
 * drop the Firstmount lock down to shared.  This lets other mount.
 * 
 * Returns: int
 */
int
downgrade_mount_lock (gulm_fs_t * fs)
{
	int err;
	gulm_lock_t *lck = (gulm_lock_t *) fs->mountlock;
	/* we were first, so we have it exl.
	 * shift it to shared so others may mount.
	 */
	err = gulm_lock (lck, LM_ST_EXCLUSIVE, LM_ST_SHARED, LM_FLAG_NOEXP);
#ifndef USE_SYNC_LOCKING
	wait_for_completion (&fs->sleep);
#endif

	if ((lck->result & LM_OUT_ST_MASK) != LM_ST_SHARED) {
		log_err
		    ("fsid=%s: Couldn't downgrade mount lock to shared!!!!!\n",
		     fs->fs_name);
	}
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
	int err;
	gulm_lock_t *lck = (gulm_lock_t *) fs->mountlock;

	if (fs->mountlock == NULL) {
		log_err ("fsid=%s: There's no Mount lock!!!!!\n", fs->fs_name);
		return -1;
	}
	err = gulm_unlock (lck, LM_ST_SHARED);
#ifndef USE_SYNC_LOCKING
	wait_for_completion (&fs->sleep);
#endif

	if (lck->result != LM_ST_UNLOCKED)
		log_err ("fsid=%s: Couldn't unlock mount lock!!!!!!\n",
			 fs->fs_name);
	gulm_put_lock (fs->mountlock);
	fs->mountlock = NULL;
	return 0;
}

/*****************************************************************************/
struct lm_lockops gulm_ops = {
      lm_proto_name:PROTO_NAME,
      lm_mount:gulm_mount,
      lm_others_may_mount:gulm_others_may_mount,
      lm_unmount:gulm_unmount,
      lm_get_lock:gulm_get_lock,
      lm_put_lock:gulm_put_lock,
      lm_lock:gulm_lock,
      lm_unlock:gulm_unlock,
      lm_cancel:gulm_cancel,
      lm_hold_lvb:gulm_hold_lvb,
      lm_unhold_lvb:gulm_unhold_lvb,
      lm_sync_lvb:gulm_sync_lvb,
      lm_plock_get:gulm_plock_get,
      lm_plock:gulm_plock,
      lm_punlock:gulm_punlock,
      lm_recovery_done:gulm_recovery_done,
      lm_owner:THIS_MODULE,
};
/* vim: set ai cin noet sw=8 ts=8 : */
