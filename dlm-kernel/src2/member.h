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

#ifndef __MEMBER_DOT_H__
#define __MEMBER_DOT_H__

int dlm_member_init(void);
void dlm_member_exit(void);

int dlm_set_node(struct dlm_member_ioctl *param);
int dlm_set_local(struct dlm_member_ioctl *param);

int dlm_ls_terminate(struct dlm_ls *ls);
int dlm_ls_stop(struct dlm_ls *ls);
int dlm_ls_start(struct dlm_ls *ls, int event_nr);
int dlm_ls_finish(struct dlm_ls *ls, int event_nr);

void dlm_clear_members(struct dlm_ls *ls);
void dlm_clear_members_gone(struct dlm_ls *ls);
void dlm_clear_members_finish(struct dlm_ls *ls, int finish_event);
int dlm_recover_members_first(struct dlm_ls *ls, struct dlm_recover *rv);
int dlm_recover_members(struct dlm_ls *ls, struct dlm_recover *rv,int *neg_out);
int dlm_is_removed(struct dlm_ls *ls, int nodeid);

int dlm_nodeid_addr(int nodeid, char *addr);
int dlm_addr_nodeid(char *addr, int *nodeid);
int dlm_our_nodeid(void);
int dlm_our_addr(int i, char *addr);

/* FIXME: just to keep src/src2 files similar for a while */
static __inline__ int our_nodeid(void)
{
	return dlm_our_nodeid();
}

#endif                          /* __MEMBER_DOT_H__ */

