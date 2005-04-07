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

int dlm_recovery_stopped(struct dlm_ls *ls);
int dlm_wait_function(struct dlm_ls *ls, int (*testfn) (struct dlm_ls *ls));
int dlm_wait_status_all(struct dlm_ls *ls, unsigned int wait_status);
int dlm_wait_status_low(struct dlm_ls *ls, unsigned int wait_status);
int dlm_recover_masters(struct dlm_ls *ls);
int dlm_recover_master_reply(struct dlm_ls *ls, struct dlm_rcom *rc);

#endif				/* __RECOVER_DOT_H__ */
