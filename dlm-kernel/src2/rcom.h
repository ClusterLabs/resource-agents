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

#ifndef __RCOM_DOT_H__
#define __RCOM_DOT_H__

int dlm_send_rcom(struct dlm_ls *ls, int nodeid, int type, struct dlm_rcom *rc,
		  int need_reply);
void dlm_receive_rcom(struct dlm_header *hd, int nodeid);

#endif
