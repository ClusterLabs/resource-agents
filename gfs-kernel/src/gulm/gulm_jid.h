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

#ifndef __GULM_JID_H__
#define __GULM_JID_H__
#include "gulm.h"
void jid_init (void);
void jid_fs_init (gulm_fs_t * fs);
void jid_fs_release (gulm_fs_t * fs);
int get_journalID (gulm_fs_t * fs);
int lookup_jid_by_name (gulm_fs_t * fs, uint8_t * name, uint32_t * injid);
int lookup_name_by_jid (gulm_fs_t * fs, uint32_t jid, uint8_t * name);
void release_JID (gulm_fs_t * fs, uint32_t jid, int owner);
void put_journalID (gulm_fs_t * fs);
void check_for_stale_expires (gulm_fs_t * fs);

int
 find_jid_by_name_and_mark_replay (gulm_fs_t * fs, uint8_t * name, uint32_t * jid);

void jid_start_journal_reply (gulm_fs_t * fs, uint32_t jid);
void jid_finish_journal_reply (gulm_fs_t * fs, uint32_t jid);

void jid_lockstate_reserve (gulm_fs_t * fs, int first);
void jid_lockstate_release (gulm_fs_t * fs);

/* to be called from the lg_lock callbacks. */
void jid_state_reply (uint8_t * key, uint16_t keylen, uint8_t * lvb,
		      uint16_t lvblen);
void jid_action_reply (uint8_t * key, uint16_t keylen);
void jid_header_lock_drop (uint8_t * key, uint16_t keylen);
#endif /*__GULM_JID_H__*/
