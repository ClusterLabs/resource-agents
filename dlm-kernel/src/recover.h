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

#ifndef __RECOVER_DOT_H__
#define __RECOVER_DOT_H__

int dlm_wait_function(struct dlm_ls *ls, int (*testfn) (struct dlm_ls * ls));
int dlm_wait_status_all(struct dlm_ls *ls, unsigned int wait_status);
int dlm_wait_status_low(struct dlm_ls *ls, unsigned int wait_status);
int dlm_recovery_stopped(struct dlm_ls *ls);
int recover_list_empty(struct dlm_ls *ls);
int recover_list_count(struct dlm_ls *ls);
void recover_list_add(struct dlm_rsb *rsb);
void recover_list_del(struct dlm_rsb *rsb);
int restbl_lkb_purge(struct dlm_ls *ls);
void restbl_grant_after_purge(struct dlm_ls *ls);
int restbl_rsb_update(struct dlm_ls *ls);
int restbl_rsb_update_recv(struct dlm_ls *ls, int nodeid, char *buf, int len,
			   int msgid);
int bulk_master_lookup(struct dlm_ls *ls, int nodeid, char *inbuf, int inlen,
		       char *outbuf);

#endif				/* __RECOVER_DOT_H__ */
