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

#ifndef __LOCKSPACE_DOT_H__
#define __LOCKSPACE_DOT_H__

void dlm_lockspace_init(void);
int dlm_init(void);
int dlm_release(void);
int dlm_new_lockspace(char *name, int namelen, void **ls, int flags);
int dlm_release_lockspace(void *ls, int force);
void dlm_emergency_shutdown(void);
struct dlm_ls *find_lockspace_by_global_id(uint32_t id);
struct dlm_ls *find_lockspace_by_local_id(void *id);
struct dlm_ls *find_lockspace_by_name(char *name, int namelen);
void hold_lockspace(struct dlm_ls *ls);
void put_lockspace(struct dlm_ls *ls);

#endif				/* __LOCKSPACE_DOT_H__ */
