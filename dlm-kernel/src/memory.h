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

#ifndef __MEMORY_DOT_H__
#define __MEMORY_DOT_H__

int dlm_memory_init(void);
void dlm_memory_exit(void);
gd_res_t *allocate_rsb(gd_ls_t * ls, int namelen);
void free_rsb(gd_res_t * r);
gd_lkb_t *allocate_lkb(gd_ls_t * ls);
void free_lkb(gd_lkb_t * l);
gd_resdata_t *allocate_resdata(gd_ls_t * ls, int namelen);
void free_resdata(gd_resdata_t * rd);
char *allocate_lvb(gd_ls_t * ls);
void free_lvb(char *l);
gd_rcom_t *allocate_rcom_buffer(gd_ls_t * ls);
void free_rcom_buffer(gd_rcom_t * rc);
uint64_t *allocate_range(gd_ls_t * ls);
void free_range(uint64_t * l);

#endif		/* __MEMORY_DOT_H__ */
