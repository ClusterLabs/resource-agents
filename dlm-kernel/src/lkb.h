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

int free_lockidtbl(gd_ls_t * lspace);
int init_lockidtbl(gd_ls_t * lspace, int entries);

gd_lkb_t *find_lock_by_id(gd_ls_t *ls, uint32_t lkid);
gd_lkb_t *create_lkb(gd_ls_t *ls);
void release_lkb(gd_ls_t *ls, gd_lkb_t *lkb);
gd_lkb_t *dlm_get_lkb(void *ls, uint32_t lkid);
int verify_lkb_nodeids(gd_ls_t *ls);
int lkb_set_range(gd_ls_t *lspace, gd_lkb_t *lkb, uint64_t start, uint64_t end);

#endif				/* __LKB_DOT_H__ */
