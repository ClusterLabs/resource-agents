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

	len = pack_lock_key(key, *keylen, 'J', fsname, "Header", 6);
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

	if( fs->JIDcount > oldjcnt ) {
		for (i = oldjcnt; i < fs->JIDcount; i++) {
			keylen = sizeof (key);
			jid_get_lock_name (fs->fs_name, i, key, &keylen);
			jid_hold_lvb (key, keylen);
		}
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
	down (&fs->headerlock);
	jid_get_lock_state_lvb (key, keylen, lg_lock_state_Exclusive, lvb,
				jid_header_lvb_size);
	jidc = (uint32_t) (lvb[0]) << 0;
	jidc |= (uint32_t) (lvb[1]) << 8;
	jidc |= (uint32_t) (lvb[2]) << 16;
	jidc |= (uint32_t) (lvb[3]) << 24;
	jidc += 1;
	lvb[3] = (jidc >> 24) & 0xff;
	lvb[2] = (jidc >> 16) & 0xff;
	lvb[1] = (jidc >> 8) & 0xff;
	lvb[0] = (jidc >> 0) & 0xff;
	jid_sync_lvb (key, keylen, lvb, jid_header_lvb_size);
	jid_get_lock_state (key, keylen, lg_lock_state_Unlock);
	/* do an unlock here, so that when rehold grabs it shared, there is no
	 * lvb writing. yeah, bit icky.  fix some other day.
	 */

	jid_rehold_lvbs (fs);
	up (&fs->headerlock);
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

	jid_get_lock_name (fs->fs_name, jid, key, &keylen);
	jid_get_lock_state_inr (key, keylen, lg_lock_state_Exclusive,
				lg_lock_flag_IgnoreExp, lvb, 64);

	if (lvb[0] != 0) {
		memcpy (name, &lvb[1], strlen (&lvb[1]) + 1);
	} else {
		err = -1;
	}

	jid_get_lock_state (key, keylen, lg_lock_state_Unlock);

      exit:
	return err;
}

/**
 * Release_JID - 
 * @fs: 
 * @jid: 
 * 
 * This is called when a node replays someone else's journal.
 * 
 */
void
release_JID (gulm_fs_t * fs, uint32_t jid)
{
	uint8_t key[GIO_KEY_SIZE], lvb[64];
	uint16_t keylen = GIO_KEY_SIZE;

	/* there is no such, so this becomes a nop. */
	if (jid >= fs->JIDcount)
		return;

	jid_get_lock_name (fs->fs_name, jid, key, &keylen);
	jid_get_lock_state_inr (key, keylen, lg_lock_state_Exclusive,
				lg_lock_flag_IgnoreExp, lvb, 64);
	if (lvb[0] == 1 ) {
		/* if byte0 is 2, then that node is alive.  They're waiting
		 * for us to finish, but once we're done, it would be mean
		 * to mark their jid as free.  So we leave the byte alone.
		 *
		 * Actually, If the byte isn't 1 (which means we are
		 * replaying the journal) don't change it.
		 *
		 * Remind: 0 = free, 1 = replaying, 2 = owned.
		 */
		lvb[0] = 0;
		jid_sync_lvb (key, keylen, lvb, strlen (&lvb[1]) + 2);
	}
	jid_get_lock_state (key, keylen, lg_lock_state_Unlock);

}

/**
 * put_journalID - 
 * @fs: 
 * 
 * This is called when this node unmounts or withdraws.
 * 
 */
void
put_journalID (gulm_fs_t * fs, int leavebehind)
{
	uint8_t key[GIO_KEY_SIZE], lvb[64];
	uint16_t keylen = GIO_KEY_SIZE;

	/* there is no such, so this becomes a nop. */
	if (fs->fsJID >= fs->JIDcount)
		return;

	jid_get_lock_name (fs->fs_name, fs->fsJID, key, &keylen);
	jid_get_lock_state_inr (key, keylen, lg_lock_state_Exclusive,
				lg_lock_flag_IgnoreExp, lvb, 64);
	if(leavebehind)
		lvb[0] = 1;
	else
		lvb[0] = 0;
	jid_sync_lvb (key, keylen, lvb, strlen (&lvb[1]) + 2);
	jid_get_lock_state (key, keylen, lg_lock_state_Unlock);
}

/**
 * get_journalID - 
 * @fs: 
 * @jid: 
 * 
 * grab EXL on names until we find one we want. (or have all.)
 * grab that one.
 * Unlock everything we got.
 * 
 * Returns: int
 */
void
get_journalID (gulm_fs_t * fs)
{
	uint8_t key[GIO_KEY_SIZE], lvb[64];
	uint16_t keylen = GIO_KEY_SIZE;
	int i, first_clear = -1, lockedto;

retry:
	/* find an empty space, or ourselves again */
	for (i = 0, lockedto = 0; i < fs->JIDcount; i++, lockedto++) {
		keylen = sizeof (key);
		jid_get_lock_name (fs->fs_name, i, key, &keylen);
		jid_get_lock_state_inr (key, keylen, lg_lock_state_Exclusive,
					lg_lock_flag_IgnoreExp, lvb, 64);
		if (first_clear == -1 && lvb[0] == 0 ) {
			first_clear = i;
		} else if (strcmp (gulm_cm.myName, &lvb[1]) == 0) {
			first_clear = i;
			break;
		}
	}
	if (first_clear >= 0) {
		/* we should be hold all jid mapping locks up to this one
		 * (and maybe beyond) EXL, so just lvb sync to the one we
		 * want.
		 */
		lvb[0] = 2;
		memcpy (&lvb[1], gulm_cm.myName, strlen (gulm_cm.myName) + 1);

		keylen = sizeof (key);
		jid_get_lock_name (fs->fs_name, first_clear, key, &keylen);
		jid_sync_lvb (key, keylen, lvb, strlen (gulm_cm.myName) + 2);

		fs->fsJID = first_clear;
	}

	/* unlock them so others can find */
	for (; lockedto >= 0; lockedto--) {
		keylen = sizeof (key);
		jid_get_lock_name (fs->fs_name, lockedto, key, &keylen);
		jid_get_lock_state (key, keylen, lg_lock_state_Unlock);
	}

	if (first_clear < 0) {
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

	down (&fs->headerlock); /*???*/
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
	up (&fs->headerlock);

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

	down (&fs->headerlock); /*???*/
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
	up (&fs->headerlock);
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

	init_MUTEX (&fs->headerlock);

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
	keylen = GIO_KEY_SIZE;
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

	down (&fs->headerlock);
	jid_get_lock_state (key, keylen, lg_lock_state_Unlock);

	jid_rehold_lvbs (fs);
	up (&fs->headerlock);
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
	uint8_t *fsname;
	uint8_t len;
	uint8_t ktype, jtype;
	ktype = key[0];
	len = key[1];
	fsname = &key[2];
	jtype = key[4 + len];

	/* make sure this is the header lock.... */
	if (ktype == 'J' && jtype == 'H' &&
			(fs = get_fs_by_name (fsname)) != NULL) {
		qu_function_call (&fs->cq, jid_unlock_callback, fs);
	}
}


/* vim: set ai cin noet sw=8 ts=8 : */
