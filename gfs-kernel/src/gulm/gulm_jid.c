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

extern gulm_cm_t gulm_cm;

/****************************************************************************/

/* jid locks:
 *
 * Header lock: "JHeader" + \0\0\0 + fsname
 *         lvb: <uint32> :number of JIDs
 * Mappinglock: "JM" + <uint32> + \0\0\0\0 + fsname
 *         lvb: [012] + <node name>
 *              0: unused
 *              1: replaying journal
 *              2: Mounted
 * list lock  : "JL" + "listlock" + fsname
 * Node Locks : "JN" + <nodename[8]> + fsname
 *
 */
#define jid_header_lvb_size (8)

struct jid_lookup_item_s {
	struct list_head jp_list;
	uint8_t *key;
	uint16_t keylen;
	uint8_t *lvb;
	uint16_t lvblen;
	struct completion waitforit;
};
typedef struct jid_lookup_item_s jid_lookup_item_t;

LIST_HEAD (jid_pending_locks);
spinlock_t jid_pending;
struct semaphore jid_listlock;

/**
 * jid_init - 
 */
void
jid_init (void)
{
	spin_lock_init (&jid_pending);
	init_MUTEX (&jid_listlock);
}

/**
 * jid_get_header_name - 
 * @fs: <
 * @key: <>
 * @keylen: <> 
 * 
 * key is buffer to write to, keylen is size of buffer on input, and real
 * length on output.
 * 
 * Returns: int
 */
int
jid_get_header_name (uint8_t * fsname, uint8_t * key, uint16_t * keylen)
{
	int len;

	len = pack_lock_key(key, *keylen, 'J', fsname, "Header\0\0\0", 9);
	if( len <=0 ) return len;

	*keylen = len;

	return 0;
}

int
jid_get_listlock_name (uint8_t * fsname, uint8_t * key, uint16_t * keylen)
{
	int len;

	len = pack_lock_key(key, *keylen, 'J', fsname, "Llistlock", 9);
	if( len <=0 ) return len;

	*keylen = len;

	return 0;
}

/**
 * jid_get_lock_name - 
 * @fs: <
 * @jid: <
 * @key: <>
 * @keylen: <>
 * 
 * key is buffer to write to, keylen is size of buffer on input, and real
 * length on output.
 * 
 * Returns: int
 */
int
jid_get_lock_name (uint8_t * fsname, uint32_t jid, uint8_t * key,
		   uint16_t * keylen)
{
	int len;
	uint8_t temp[9];

	temp[0] = 'M';
	temp[1] = (jid >> 0) & 0xff;
	temp[2] = (jid >> 8) & 0xff;
	temp[3] = (jid >> 16) & 0xff;
	temp[4] = (jid >> 24) & 0xff;
	temp[5] = 0;
	temp[6] = 0;
	temp[7] = 0;
	temp[8] = 0;

	len = pack_lock_key(key, *keylen, 'J', fsname, temp, 9);
	if( len <=0 ) return len;

	*keylen = len;

	return 0;
}

/**
 * jid_hold_lvb - 
 * @key: 
 * @keylen: 
 * 
 * 
 */
void
jid_hold_lvb (uint8_t * key, uint16_t keylen)
{
	jid_lookup_item_t jp;
	GULM_ASSERT (keylen > 6,);
	jp.key = key;
	jp.keylen = keylen;
	jp.lvb = NULL;
	jp.lvblen = 0;
	INIT_LIST_HEAD (&jp.jp_list);
	init_completion (&jp.waitforit);

	spin_lock (&jid_pending);
	list_add (&jp.jp_list, &jid_pending_locks);
	spin_unlock (&jid_pending);

	lg_lock_action_req (gulm_cm.hookup, key, keylen, 0,
			    lg_lock_act_HoldLVB, NULL, 0);

	wait_for_completion (&jp.waitforit);
}

void
jid_unhold_lvb (uint8_t * key, uint16_t keylen)
{
	jid_lookup_item_t jp;
	GULM_ASSERT (keylen > 6,);
	jp.key = key;
	jp.keylen = keylen;
	jp.lvb = NULL;
	jp.lvblen = 0;
	INIT_LIST_HEAD (&jp.jp_list);
	init_completion (&jp.waitforit);

	spin_lock (&jid_pending);
	list_add (&jp.jp_list, &jid_pending_locks);
	spin_unlock (&jid_pending);

	lg_lock_action_req (gulm_cm.hookup, key, keylen, 0,
			    lg_lock_act_UnHoldLVB, NULL, 0);

	wait_for_completion (&jp.waitforit);
}

void
jid_sync_lvb (uint8_t * key, uint16_t keylen, uint8_t * lvb, uint16_t lvblen)
{
	jid_lookup_item_t jp;
	GULM_ASSERT (keylen > 6,);
	jp.key = key;
	jp.keylen = keylen;
	jp.lvb = NULL;
	jp.lvblen = 0;
	INIT_LIST_HEAD (&jp.jp_list);
	init_completion (&jp.waitforit);

	spin_lock (&jid_pending);
	list_add (&jp.jp_list, &jid_pending_locks);
	spin_unlock (&jid_pending);

	lg_lock_action_req (gulm_cm.hookup, key, keylen, 0,
			    lg_lock_act_SyncLVB, lvb, lvblen);

	wait_for_completion (&jp.waitforit);
}

/**
 * jid_action_reply - 
 * @key: 
 * @keylen: 
 * 
 * called from the lock handler callback.
 * 
 * Returns: void
 */
void
jid_action_reply (uint8_t * key, uint16_t keylen)
{
	struct list_head *tmp, *nxt;
	jid_lookup_item_t *jp, *fnd = NULL;
	spin_lock (&jid_pending);
	list_for_each_safe (tmp, nxt, &jid_pending_locks) {
		jp = list_entry (tmp, jid_lookup_item_t, jp_list);
		if (memcmp (key, jp->key, MIN (keylen, jp->keylen)) == 0) {
			fnd = jp;
			list_del (tmp);
			break;
		}
	}
	spin_unlock (&jid_pending);

	if (fnd != NULL)
		complete (&fnd->waitforit);
}

/**
 * jid_get_lock_state_inr - 
 * @key: 
 * @keylen: 
 * @state: 
 * @flags:
 * @lvb: 
 * @lvblen: 
 * 
 * 
 */
void
jid_get_lock_state_inr (uint8_t * key, uint16_t keylen, uint8_t state,
			uint32_t flags, uint8_t * lvb, uint16_t lvblen)
{
	jid_lookup_item_t jp;
	GULM_ASSERT (keylen > 6,
			printk("keylen: %d\n", keylen););
	jp.key = key;
	jp.keylen = keylen;
	jp.lvb = lvb;
	jp.lvblen = lvblen;
	INIT_LIST_HEAD (&jp.jp_list);
	init_completion (&jp.waitforit);

	spin_lock (&jid_pending);
	list_add (&jp.jp_list, &jid_pending_locks);
	spin_unlock (&jid_pending);

	lg_lock_state_req (gulm_cm.hookup, key, keylen, 0, 0, ~((uint64_t)0),
			state, flags, lvb, lvblen);

	wait_for_completion (&jp.waitforit);
}

/**
 * jid_get_lock_state_lvb - 
 * @key: 
 * @keylen: 
 * @state: 
 * @lvb: 
 * @lvblen: 
 * 
 * 
 */
void
jid_get_lock_state_lvb (uint8_t * key, uint16_t keylen, uint8_t state,
			uint8_t * lvb, uint16_t lvblen)
{
	jid_get_lock_state_inr (key, keylen, state, 0, lvb, lvblen);
}
/**
 * jid_get_lock_state - 
 * @key: 
 * @keylen: 
 * @state: 
 * 
 * 
 */
void
jid_get_lock_state (uint8_t * key, uint16_t keylen, uint8_t state)
{
	jid_get_lock_state_inr (key, keylen, state, 0, NULL, 0);
}

/**
 * jid_state_reply - 
 * @key: 
 * @keylen: 
 * @lvb: 
 * @lvblen: 
 * 
 * 
 */
void
jid_state_reply (uint8_t * key, uint16_t keylen, uint8_t * lvb, uint16_t lvblen)
{
	struct list_head *tmp, *nxt;
	jid_lookup_item_t *jp, *fnd = NULL;
	spin_lock (&jid_pending);
	list_for_each_safe (tmp, nxt, &jid_pending_locks) {
		jp = list_entry (tmp, jid_lookup_item_t, jp_list);
		if (memcmp (key, jp->key, MIN (keylen, jp->keylen)) == 0) {
			fnd = jp;
			list_del (tmp);
			break;
		}
	}
	spin_unlock (&jid_pending);

	if (fnd != NULL) {
		if (lvb != NULL && fnd->lvb != NULL)
			memcpy (fnd->lvb, lvb, MIN (fnd->lvblen, lvblen));
		complete (&fnd->waitforit);
	}
}

/****************************************************************************/

/**
 * jid_hold_list_lock - 
 * @fs: 
 * 
 * only make one call to this per node.
 * 
 * Returns: void
 */
void
jid_hold_list_lock (gulm_fs_t * fs)
{
	uint8_t key[GIO_KEY_SIZE];
	uint16_t keylen = GIO_KEY_SIZE;

	down (&jid_listlock);

	keylen = sizeof (key);
	jid_get_listlock_name (fs->fs_name, key, &keylen);
	jid_get_lock_state_inr (key, keylen, lg_lock_state_Exclusive,
			lg_lock_flag_IgnoreExp, NULL, 0);

}

/**
 * jid_release_list_lock - 
 * @fs: 
 * 
 * 
 * Returns: void
 */
void
jid_release_list_lock (gulm_fs_t * fs)
{
	uint8_t key[GIO_KEY_SIZE];
	uint16_t keylen = GIO_KEY_SIZE;

	keylen = sizeof (key);
	jid_get_listlock_name (fs->fs_name, key, &keylen);
	jid_get_lock_state (key, keylen, lg_lock_state_Unlock);

	up (&jid_listlock);
}

/**
 * jid_rehold_lvbs - 
 * @fs: 
 * 
 * 
 */
void
jid_rehold_lvbs (gulm_fs_t * fs)
{
	int i;
	uint32_t oldjcnt;
	uint8_t key[GIO_KEY_SIZE], lvb[jid_header_lvb_size];
	uint16_t keylen = GIO_KEY_SIZE;

	oldjcnt = fs->JIDcount;

	jid_get_header_name (fs->fs_name, key, &keylen);
	jid_get_lock_state_lvb (key, keylen, lg_lock_state_Shared, lvb,
				jid_header_lvb_size);
	fs->JIDcount = (uint32_t) (lvb[0]) << 0;
	fs->JIDcount |= (uint32_t) (lvb[1]) << 8;
	fs->JIDcount |= (uint32_t) (lvb[2]) << 16;
	fs->JIDcount |= (uint32_t) (lvb[3]) << 24;

	for (i = oldjcnt; i < fs->JIDcount; i++) {
		keylen = sizeof (key);
		jid_get_lock_name (fs->fs_name, i, key, &keylen);
		jid_hold_lvb (key, keylen);
	}

}

void
jid_grow_space (gulm_fs_t * fs)
{
	uint8_t key[GIO_KEY_SIZE], lvb[jid_header_lvb_size];
	uint16_t keylen = GIO_KEY_SIZE;
	uint32_t jidc;

	keylen = sizeof (key);
	jid_get_header_name (fs->fs_name, key, &keylen);
	jid_get_lock_state_inr (key, keylen, lg_lock_state_Exclusive,
				lg_lock_flag_IgnoreExp, lvb,
				jid_header_lvb_size);
	jidc = (uint32_t) (lvb[0]) << 0;
	jidc |= (uint32_t) (lvb[1]) << 8;
	jidc |= (uint32_t) (lvb[2]) << 16;
	jidc |= (uint32_t) (lvb[3]) << 24;
	jidc += 300;
	lvb[3] = (jidc >> 24) & 0xff;
	lvb[2] = (jidc >> 16) & 0xff;
	lvb[1] = (jidc >> 8) & 0xff;
	lvb[0] = (jidc >> 0) & 0xff;
	jid_sync_lvb (key, keylen, lvb, jid_header_lvb_size);
	jid_get_lock_state (key, keylen, lg_lock_state_Unlock);
	/* do an unlock here, so that when rehold grabs it shared, there is no
	 * lvb writing.
	 */

	jid_rehold_lvbs (fs);
}

/**
 * lookup_name_by_jid - 
 * @fs: 
 * @jid: 
 * @name: 
 * 
 * 
 * Returns: int
 */
int
lookup_name_by_jid (gulm_fs_t * fs, uint32_t jid, uint8_t * name)
{
	uint8_t key[GIO_KEY_SIZE], lvb[64];
	uint16_t keylen = GIO_KEY_SIZE;
	int err = 0;

	if (jid >= fs->JIDcount) {
		err = -1;
		goto exit;
	}

	jid_hold_list_lock (fs);

	jid_get_lock_name (fs->fs_name, jid, key, &keylen);
	jid_get_lock_state_inr (key, keylen, lg_lock_state_Exclusive,
				lg_lock_flag_IgnoreExp, lvb, 64);

	if (lvb[0] != 0) {
		memcpy (name, &lvb[1], strlen (&lvb[1]) + 1);
	} else {
		err = -1;
	}

	jid_get_lock_state (key, keylen, lg_lock_state_Unlock);

	jid_release_list_lock (fs);

      exit:
	return err;
}

/**
 * Release_JID - 
 * @fs: 
 * @jid: 
 * 
 * actually may only need to et first byte to zero
 * 
 * Returns: int
 */
int
release_JID (gulm_fs_t * fs, uint32_t jid, int nop)
{
	uint8_t key[GIO_KEY_SIZE], lvb[64];
	uint16_t keylen = GIO_KEY_SIZE;

	/* there is no such, so this becomes a nop. */
	if (jid >= fs->JIDcount)
		goto exit;

	jid_hold_list_lock (fs);

	jid_get_lock_name (fs->fs_name, jid, key, &keylen);
	jid_get_lock_state_inr (key, keylen, lg_lock_state_Exclusive,
				lg_lock_flag_IgnoreExp, lvb, 64);
	lvb[0] = 0;
	jid_sync_lvb (key, keylen, lvb, strlen (&lvb[1]) + 2);
	jid_get_lock_state (key, keylen, lg_lock_state_Unlock);

	jid_release_list_lock (fs);

      exit:
	return 0;
}

void
put_journalID (gulm_fs_t * fs)
{
	release_JID (fs, fs->fsJID, TRUE);
}

/**
 * get_journalID - 
 * @fs: 
 * @jid: 
 * 
 * This is broken.
 * 
 * Returns: int
 */
void
get_journalID (gulm_fs_t * fs)
{
	uint32_t i = 0;
	uint8_t key[GIO_KEY_SIZE], lvb[64];
	uint16_t keylen = GIO_KEY_SIZE;
	int first_clear = -1;

      retry:
	jid_hold_list_lock (fs);

	/* find an empty space, or ourselves again */
	for (i = 0; i < fs->JIDcount; i++) {
		keylen = sizeof (key);
		jid_get_lock_name (fs->fs_name, i, key, &keylen);
		jid_get_lock_state_inr (key, keylen, lg_lock_state_Exclusive,
					lg_lock_flag_IgnoreExp, lvb, 64);
		jid_get_lock_state (key, keylen, lg_lock_state_Unlock);
		if (first_clear == -1 && lvb[0] == 0 ) {
			first_clear = i;
		} else if (strcmp (gulm_cm.myName, &lvb[1]) == 0) {
			first_clear = i;
			break;
		}
	}
	if (first_clear >= 0) {
		/* take the jid we have found */
		keylen = sizeof (key);
		jid_get_lock_name (fs->fs_name, first_clear, key, &keylen);
		jid_get_lock_state_inr (key, keylen, lg_lock_state_Exclusive,
					lg_lock_flag_IgnoreExp, lvb, 64);
		lvb[0] = 2;
		memcpy (&lvb[1], gulm_cm.myName, strlen (gulm_cm.myName) + 1);
		jid_sync_lvb (key, keylen, lvb, strlen (gulm_cm.myName) + 2);
		jid_get_lock_state (key, keylen, lg_lock_state_Unlock);

		fs->fsJID = first_clear;
	}

	/* unlock the header lock */
	jid_release_list_lock (fs);

	if (first_clear < 0) {
		/* nothing found, grow and try again. */
		jid_grow_space (fs);
		goto retry;
	}

}

/**
 * find_jid_by_name_and_mark_replay - 
 * @fs: 
 * @name: 
 * @jid: 
 * 
 * 
 * Returns: int
 */
int
find_jid_by_name_and_mark_replay (gulm_fs_t * fs, uint8_t * name,
				  uint32_t * jid)
{
	uint32_t i, found = -1;
	uint8_t key[GIO_KEY_SIZE], lvb[64];
	uint16_t keylen = GIO_KEY_SIZE;

	/* grab list lock */
	jid_hold_list_lock (fs);

	for (i = 0; i < fs->JIDcount; i++) {
		keylen = sizeof (key);
		jid_get_lock_name (fs->fs_name, i, key, &keylen);
		jid_get_lock_state_inr (key, keylen, lg_lock_state_Exclusive,
					lg_lock_flag_IgnoreExp, lvb, 64);
		if (strcmp (name, &lvb[1]) == 0) {
			*jid = i;
			found = 0;
			lvb[0] = 1;
			jid_sync_lvb (key, keylen, lvb, strlen (&lvb[1]) + 2);
			jid_get_lock_state (key, keylen, lg_lock_state_Unlock);
			break;
		}
		jid_get_lock_state (key, keylen, lg_lock_state_Unlock);

	}
	/* unlock the list lock */
	jid_release_list_lock (fs);

	return found;
}

/**
 * Check_for_replays - 
 * @fs: 
 * 
 * 
 * Returns: int
 */
void
check_for_stale_expires (gulm_fs_t * fs)
{
	uint32_t i;
	uint8_t key[GIO_KEY_SIZE], lvb[64];
	uint16_t keylen = GIO_KEY_SIZE;
	unsigned int ujid;

	/* grab list lock */
	jid_hold_list_lock (fs);

	for (i = 0; i < fs->JIDcount; i++) {
		keylen = sizeof (key);
		jid_get_lock_name (fs->fs_name, i, key, &keylen);
		jid_get_lock_state_inr (key, keylen, lg_lock_state_Exclusive,
					lg_lock_flag_IgnoreExp, lvb, 64);
		jid_get_lock_state (key, keylen, lg_lock_state_Unlock);

		if (lvb[0] == 1) {
			log_msg (lgm_JIDMap,
				 "fsid=%s: stale JID %d found\n",
				 fs->fs_name, i);
			ujid = i;
			fs->cb (fs->fsdata, LM_CB_NEED_RECOVERY, &ujid);
		}
	}

	/* unlock the list lock */
	jid_release_list_lock (fs);
}

/**
 * jid_fs_init - 
 * @fs: 
 * 
 */
void
jid_fs_init (gulm_fs_t * fs)
{
	uint8_t key[GIO_KEY_SIZE];
	uint16_t keylen = GIO_KEY_SIZE;

	fs->JIDcount = 0;

	jid_get_header_name (fs->fs_name, key, &keylen);
	jid_hold_lvb (key, keylen);
	jid_rehold_lvbs (fs);
}

/**
 * jid_fs_release - 
 * @fs: 
 * 
 */
void
jid_fs_release (gulm_fs_t * fs)
{
	uint32_t i;
	uint8_t key[GIO_KEY_SIZE];
	uint16_t keylen = GIO_KEY_SIZE;
	for (i = 0; i < fs->JIDcount; i++) {
		keylen = sizeof (key);
		jid_get_lock_name (fs->fs_name, i, key, &keylen);
		jid_unhold_lvb (key, keylen);
	}
	keylen = sizeof (key);
	jid_get_header_name (fs->fs_name, key, &keylen);
	jid_unhold_lvb (key, keylen);
	jid_get_lock_state (key, keylen, lg_lock_state_Unlock);
}

/**
 * jid_unlock_callback - 
 * @d: 
 * 
 * *MUST* be called from a Handler thread.
 * 
 * Returns: int
 */
void
jid_unlock_callback (void *d)
{
	uint8_t key[GIO_KEY_SIZE];
	uint16_t keylen = GIO_KEY_SIZE;

	gulm_fs_t *fs = (gulm_fs_t *) d;
	jid_get_header_name (fs->fs_name, key, &keylen);
	jid_get_lock_state (key, keylen, lg_lock_state_Unlock);

	jid_rehold_lvbs (fs);
}

/**
 * jid_header_lock_drop - 
 * @key: 
 * @keylen: 
 * 
 * Returns: void
 */
void
jid_header_lock_drop (uint8_t * key, uint16_t keylen)
{
	gulm_fs_t *fs;
	/* make sure this is the header lock.... */
	if (key[1] == 'H' && (fs = get_fs_by_name (&key[10])) != NULL) {
		qu_function_call (&fs->cq, jid_unlock_callback, fs);
	}
}

/****************************************************************************/
/**
 * jid_get_lsresv_name - 
 * @fsname: 
 * @key: 
 * @keylen: 
 * 
 * 
 * Returns: int
 */
int
jid_get_lsresv_name (char *fsname, uint8_t * key, uint16_t * keylen)
{
	int len;

	len = strlen(gulm_cm.myName);
	len = pack_lock_key(key, *keylen, 'N', fsname, gulm_cm.myName,
			MIN(64,len));
	if( len <=0 ) return len;

	*keylen = len;

	return 0;
}

/**
 * jid_lockstate_reserve - 
 * @fs: 
 * 
 * 
 * Returns: void
 */
void
jid_lockstate_reserve (gulm_fs_t * fs, int first)
{
	uint8_t key[5 + 32 + 64];
	uint16_t keylen = 5 + 32 + 64;
	/* 5 bytes for stuff in key (lengths and type bytes)
	 * 32 for fs name
	 * 64 for node name.
	 */

	jid_get_lsresv_name (fs->fs_name, key, &keylen);

	/* if we are expired, this will block until someone else has
	 * cleaned our last mess up.
	 *
	 * Will very well may need to put in some kind of timeout
	 * otherwise this may do a forever lockup much like the
	 * FirstMounter lock had.
	 */
	jid_get_lock_state_inr (key, keylen, lg_lock_state_Exclusive,
			first?lg_lock_flag_IgnoreExp:0, NULL, 0);

}

/**
 * jid_lockstate_release - 
 * @fs: 
 * 
 * 
 * Returns: void
 */
void
jid_lockstate_release (gulm_fs_t * fs)
{
	uint8_t key[5 + 32 + 64];
	uint16_t keylen = 5 + 32 + 64;

	jid_get_lsresv_name (fs->fs_name, key, &keylen);

	jid_get_lock_state (key, keylen, lg_lock_state_Unlock);

}


/* vim: set ai cin noet sw=8 ts=8 : */
