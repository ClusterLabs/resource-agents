/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**  
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __LOCK_DOT_H__
#define __LOCK_DOT_H__

void dlm_lock_init(void);
int dlm_receive_message(struct dlm_header *hd, int nodeid, int recovery);
int dlm_modes_compat(int mode1, int mode2);
int dlm_find_rsb(struct dlm_ls *ls, char *name, int namelen,
	unsigned int flags, struct dlm_rsb **r_ret);
void dlm_put_rsb(struct dlm_rsb *r);
void dlm_hold_rsb(struct dlm_rsb *r);
void dlm_lock_rsb(struct dlm_rsb *r);
void dlm_unlock_rsb(struct dlm_rsb *r);
int dlm_put_lkb(struct dlm_lkb *lkb);
int dlm_remove_from_waiters(struct dlm_lkb *lkb);

int dlm_create_root_list(struct dlm_ls *ls);
void dlm_release_root_list(struct dlm_ls *ls);
int dlm_purge_locks(struct dlm_ls *ls);
int dlm_grant_after_purge(struct dlm_ls *ls);
int dlm_recover_waiters_post(struct dlm_ls *ls);
void dlm_recover_waiters_pre(struct dlm_ls *ls);

/* FIXME: just forward declarations of routines called within lock.c */

int request_lock(struct dlm_ls *ls, struct dlm_lkb *lkb, char *name, int len);
int convert_lock(struct dlm_ls *ls, struct dlm_lkb *lkb);
int unlock_lock(struct dlm_ls *ls, struct dlm_lkb *lkb);
int cancel_lock(struct dlm_ls *ls, struct dlm_lkb *lkb);

int _request_lock(struct dlm_rsb *r, struct dlm_lkb *lkb);
int _convert_lock(struct dlm_rsb *r, struct dlm_lkb *lkb);
int _unlock_lock(struct dlm_rsb *r, struct dlm_lkb *lkb);
int _cancel_lock(struct dlm_rsb *r, struct dlm_lkb *lkb);

int do_request(struct dlm_rsb *r, struct dlm_lkb *lkb);
int do_convert(struct dlm_rsb *r, struct dlm_lkb *lkb);
int do_unlock(struct dlm_rsb *r, struct dlm_lkb *lkb);
int do_cancel(struct dlm_rsb *r, struct dlm_lkb *lkb);

int send_request(struct dlm_rsb *r, struct dlm_lkb *lkb);
int send_convert(struct dlm_rsb *r, struct dlm_lkb *lkb);
int send_unlock(struct dlm_rsb *r, struct dlm_lkb *lkb);
int send_cancel(struct dlm_rsb *r, struct dlm_lkb *lkb);
int send_grant(struct dlm_rsb *r, struct dlm_lkb *lkb);
int send_bast(struct dlm_rsb *r, struct dlm_lkb *lkb, int mode);
int send_lookup(struct dlm_rsb *r, struct dlm_lkb *lkb);
int send_remove(struct dlm_rsb *r);

#endif
