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
void _release_rsb(gd_res_t *r);
void release_rsb(gd_res_t *r);
void hold_rsb(gd_res_t *r);
int find_or_create_rsb(gd_ls_t *ls, gd_res_t *parent, char *name, int namelen,
		       int create, gd_res_t **rp);
gd_res_t *find_rsb_to_unlock(gd_ls_t *ls, gd_lkb_t *lkb);
void lkb_enqueue(gd_res_t *r, gd_lkb_t *lkb, int type);
void res_lkb_enqueue(gd_res_t *r, gd_lkb_t *lkb, int type);
int lkb_dequeue(gd_lkb_t *lkb);
int res_lkb_dequeue(gd_lkb_t *lkb);
int lkb_swqueue(gd_res_t *r, gd_lkb_t *lkb, int type);
int res_lkb_swqueue(gd_res_t *r, gd_lkb_t *lkb, int type);

#endif				/* __RSB_DOT_H__ */
