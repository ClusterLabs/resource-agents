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

struct semaphore jid_listlock;

/**
 * jid_init - 
 */
void
jid_init (void)
{
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
 * gulm_jid_finish - 
 * @item: 
 * 
 * 
 * Returns: void
 */
void gulm_jid_finish (struct glck_req *item)
{
	struct completion *sleep = (struct completion *)item->misc;
	complete (sleep);
}

/**
 * jid_lvb_action - 
 * @key: 
 * @keylen: 
 * @lvb: 
 * @lvblen: 
 * @action: 
 * 
 * 
 * Returns: void
 */
void jid_lvb_action (uint8_t * key, uint16_t keylen, uint8_t * lvb,
		uint16_t lvblen, uint8_t action)
{
	struct completion sleep;
	glckr_t *item;

	item = glq_get_new_req();
	if (item == NULL) {
		return;
	}

	item->key = key;
	item->keylen = keylen;
	item->subid = 0;
	item->start = 0;
	item->stop = ~((uint64_t)0);
	item->type = glq_req_type_action;
	item->state = action;
	item->flags = 0;
	item->error =  0;
	item->lvb = lvb;
	item->lvblen = lvblen;

	init_completion (&sleep);

	item->misc = &sleep;
	item->finish = gulm_jid_finish;

	glq_queue (item);
	wait_for_completion (&sleep);
}
void
jid_sync_lvb (uint8_t * key, uint16_t keylen, uint8_t * lvb, uint16_t lvblen)
{
	jid_lvb_action (key, keylen, lvb, lvblen, lg_lock_act_SyncLVB);
}
void
jid_unhold_lvb (uint8_t * key, uint16_t keylen)
{
	jid_lvb_action (key, keylen, NULL, 0, lg_lock_act_UnHoldLVB);
}
void
jid_hold_lvb (uint8_t * key, uint16_t keylen)
{
	jid_lvb_action (key, keylen, NULL, 0, lg_lock_act_HoldLVB);
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
	struct completion sleep;
	glckr_t *item;
	GULM_ASSERT (keylen > 6,
			printk("keylen: %d\n", keylen););

	init_completion (&sleep);

	item = glq_get_new_req();
	if (item == NULL) {
		return;
	}

	item->key = key;
	item->keylen = keylen;
	item->subid = 0;
	item->start = 0;
	item->stop = ~((uint64_t)0);
	item->type = glq_req_type_state;
	item->state = state;
	item->flags = flags;
	item->error =  0;
	item->lvb = lvb;
	item->lvblen = lvblen;

	item->misc = &sleep;
	item->finish = gulm_jid_finish;

	glq_queue (item);

	wait_for_completion (&sleep);
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
	uint32_t oldjcnt=0;
	uint8_t key[GIO_KEY_SIZE];
	uint16_t keylen = GIO_KEY_SIZE;

	for (i = oldjcnt; i < fs->JIDcount; i++) {
		keylen = sizeof (key);
		jid_get_lock_name (fs->fs_name, i, key, &keylen);
		jid_hold_lvb (key, keylen);
	}

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

	GULM_ASSERT( first_clear >= 0,);
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
 * This is very icky. but it works for the time being. must fix later.
 */
void
jid_fs_init (gulm_fs_t * fs)
{
	fs->JIDcount = 300;
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
}

/****************************************************************************/
/* 6 bytes for stuff in key (lengths and type bytes)
 * 32 for fs name
 * 64 for node name.
 */
#define NodeLockNameLen (6 + 32 + 64)

/**
 * gulm_nodelock_finish - 
 * @item: 
 * 
 * 
 * Returns: void
 */
void gulm_nodelock_finish (struct glck_req *item)
{
	struct completion *sleep = (struct completion *)item->misc;
	complete (sleep);
}

/**
 * jid_lockstate_reserve - 
 * @fs: 
 * 
 * if we are expired, this will block until someone else has
 * cleaned our last mess up.
 *
 * Will very well may need to put in some kind of timeout
 * otherwise this may do a forever lockup much like the
 * FirstMounter lock had.
 * 
 * Returns: void
 */
void
jid_lockstate_reserve (gulm_fs_t * fs, int first)
{
	int len;
	struct completion sleep;
	glckr_t *item;
	uint8_t *key;

	item = glq_get_new_req();
	if (item == NULL) {
		return;
	}

	key = kmalloc(NodeLockNameLen, GFP_KERNEL);
	item->key = key;
	if (item->key == NULL) {
		glq_recycle_req(item);
		return;
	}
	len = strlen(gulm_cm.myName);
	item->keylen = pack_lock_key(item->key, NodeLockNameLen, 'N',
			fs->fs_name, gulm_cm.myName, MIN(64,len));
	item->subid = 0;
	item->start = 0;
	item->stop = ~((uint64_t)0);
	item->type = glq_req_type_state;
	item->state = lg_lock_state_Exclusive;
	item->flags = (first?lg_lock_flag_IgnoreExp:0)|lg_lock_flag_NoCallBacks;
	item->error = 0;

	init_completion (&sleep);

	item->misc = &sleep;
	item->finish = gulm_nodelock_finish;

	glq_queue (item);
	wait_for_completion (&sleep);
	kfree(key);
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
	int len;
	struct completion sleep;
	glckr_t *item;
	uint8_t *key;

	item = glq_get_new_req();
	if (item == NULL) {
		return;
	}

	key = kmalloc(NodeLockNameLen, GFP_KERNEL);
	item->key = key;
	if (item->key == NULL) {
		glq_recycle_req(item);
		return;
	}
	len = strlen(gulm_cm.myName);
	item->keylen = pack_lock_key(item->key, NodeLockNameLen, 'N',
			fs->fs_name, gulm_cm.myName, MIN(64,len));
	item->subid = 0;
	item->start = 0;
	item->stop = ~((uint64_t)0);
	item->type = glq_req_type_state;
	item->state = lg_lock_state_Unlock;
	item->flags = 0;
	item->error = 0;

	init_completion (&sleep);

	item->misc = &sleep;
	item->finish = gulm_nodelock_finish;

	glq_queue (item);
	wait_for_completion (&sleep);
	kfree(key);
}


/* vim: set ai cin noet sw=8 ts=8 : */
