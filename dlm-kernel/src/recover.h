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

int gdlm_wait_function(gd_ls_t * ls, int (*testfn) (gd_ls_t * ls));
int gdlm_wait_status_all(gd_ls_t * ls, unsigned int wait_status);
int gdlm_wait_status_low(gd_ls_t * ls, unsigned int wait_status);
int gdlm_recovery_stopped(gd_ls_t * ls);
int recover_list_empty(gd_ls_t * ls);
int recover_list_count(gd_ls_t * ls);
void recover_list_add(gd_res_t * rsb);
void recover_list_del(gd_res_t * rsb);
void recover_list_dump(gd_ls_t * ls);
int restbl_lkb_purge(gd_ls_t * ls);
void restbl_grant_after_purge(gd_ls_t * ls);
int restbl_rsb_update(gd_ls_t * ls);
int restbl_rsb_update_recv(gd_ls_t * ls, int nodeid, char *buf, int len,
			   int msgid);
int bulk_master_lookup(gd_ls_t * ls, int nodeid, char *inbuf, int inlen,
		       char *outbuf);

#endif				/* __RECOVER_DOT_H__ */
