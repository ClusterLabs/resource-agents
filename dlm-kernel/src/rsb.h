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

#ifndef __RSB_DOT_H__
#define __RSB_DOT_H__

void lkb_add_ordered(struct list_head *new, struct list_head *head, int mode);
void release_rsb(struct dlm_rsb *r);
void release_rsb_locked(struct dlm_rsb *r);
void hold_rsb(struct dlm_rsb *r);
int find_or_create_rsb(struct dlm_ls *ls, struct dlm_rsb *parent, char *name,
		       int namelen, int create, struct dlm_rsb **rp);
struct dlm_rsb *find_rsb_to_unlock(struct dlm_ls *ls, struct dlm_lkb *lkb);
void lkb_enqueue(struct dlm_rsb *r, struct dlm_lkb *lkb, int type);
void res_lkb_enqueue(struct dlm_rsb *r, struct dlm_lkb *lkb, int type);
int lkb_dequeue(struct dlm_lkb *lkb);
int res_lkb_dequeue(struct dlm_lkb *lkb);
int lkb_swqueue(struct dlm_rsb *r, struct dlm_lkb *lkb, int type);
int res_lkb_swqueue(struct dlm_rsb *r, struct dlm_lkb *lkb, int type);

#endif				/* __RSB_DOT_H__ */
