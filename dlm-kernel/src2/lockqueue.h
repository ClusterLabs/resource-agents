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

#ifndef __LOCKQUEUE_DOT_H__
#define __LOCKQUEUE_DOT_H__

void remote_grant(struct dlm_lkb * lkb);
void reply_and_grant(struct dlm_lkb * lkb);
int remote_stage(struct dlm_lkb * lkb, int state);
int process_cluster_request(int csid, struct dlm_header *req, int recovery);
int send_cluster_request(struct dlm_lkb * lkb, int state);
void purge_requestqueue(struct dlm_ls * ls);
int process_requestqueue(struct dlm_ls * ls);
int reply_in_requestqueue(struct dlm_ls * ls, int lkid);
void remote_remove_direntry(struct dlm_ls * ls, int nodeid, char *name,
			    int namelen);
void allocate_and_copy_lvb(struct dlm_ls * ls, char **lvbptr, char *src);

#endif				/* __LOCKQUEUE_DOT_H__ */
