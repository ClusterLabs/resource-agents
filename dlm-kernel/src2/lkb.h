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

#ifndef __LKB_DOT_H__
#define __LKB_DOT_H__

struct dlm_lkb *find_lock_by_id(struct dlm_ls *ls, uint32_t lkid);
struct dlm_lkb *create_lkb(struct dlm_ls *ls);
void release_lkb(struct dlm_ls *ls, struct dlm_lkb *lkb);
struct dlm_lkb *dlm_get_lkb(void *ls, uint32_t lkid);
int lkb_set_range(struct dlm_ls *lspace, struct dlm_lkb *lkb, uint64_t start, uint64_t end);

#endif				/* __LKB_DOT_H__ */
