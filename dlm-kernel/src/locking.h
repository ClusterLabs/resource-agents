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

void process_remastered_lkb(gd_lkb_t * lkb, int state);
void dlm_lock_stage3(gd_lkb_t * lkb);
int dlm_convert_stage2(gd_lkb_t * lkb, int do_ast);
int dlm_unlock_stage2(gd_lkb_t * lkb, uint32_t flags);
int dlm_lock_stage2(gd_ls_t * lspace, gd_lkb_t * lkb, gd_res_t * rsb,
		    int flags);
gd_res_t *create_rsb(gd_ls_t * lspace, gd_lkb_t * lkb, char *name, int namelen);
int free_rsb_if_unused(gd_res_t * rsb);
gd_lkb_t *remote_stage2(int remote_csid, gd_ls_t * lspace,
			struct gd_remlockrequest *freq);
int cancel_lockop(gd_lkb_t * lkb, int status);
int dlm_remove_lock(gd_lkb_t * lkb, uint32_t flags);
int grant_pending_locks(gd_res_t * rsb);
void cancel_conversion(gd_lkb_t * lkb, int ret);
gd_lkb_t *conversion_deadlock_check(gd_lkb_t * lkb);

#endif				/* __LOCKING_DOT_H__ */
