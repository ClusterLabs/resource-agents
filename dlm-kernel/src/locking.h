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

#ifndef __LOCKING_DOT_H__
#define __LOCKING_DOT_H__

void process_remastered_lkb(struct dlm_ls *ls, struct dlm_lkb *lkb, int state);
void dlm_lock_stage3(struct dlm_lkb *lkb);
int dlm_convert_stage2(struct dlm_lkb *lkb, int do_ast);
int dlm_unlock_stage2(struct dlm_lkb *lkb, struct dlm_rsb *rsb, uint32_t flags);
int dlm_lock_stage2(struct dlm_ls *lspace, struct dlm_lkb *lkb, struct dlm_rsb *rsb, int flags);
struct dlm_rsb *create_rsb(struct dlm_ls *lspace, struct dlm_lkb *lkb, char *name, int namelen);
int free_rsb_if_unused(struct dlm_rsb *rsb);
struct dlm_lkb *remote_stage2(int remote_csid, struct dlm_ls *lspace,
			struct dlm_request *freq);
int cancel_lockop(struct dlm_lkb *lkb, int status);
int dlm_remove_lock(struct dlm_lkb *lkb, uint32_t flags);
int grant_pending_locks(struct dlm_rsb *rsb);
void cancel_conversion(struct dlm_lkb *lkb, int ret);
struct dlm_lkb *conversion_deadlock_check(struct dlm_lkb *lkb);

#endif				/* __LOCKING_DOT_H__ */
