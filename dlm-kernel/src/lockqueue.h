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

void remote_grant(gd_lkb_t * lkb);
void reply_and_grant(gd_lkb_t * lkb);
int remote_stage(gd_lkb_t * lkb, int state);
int process_cluster_request(int csid, struct gd_req_header *req, int recovery);
int send_cluster_request(gd_lkb_t * lkb, int state);
void purge_requestqueue(gd_ls_t * ls);
int process_requestqueue(gd_ls_t * ls);
int reply_in_requestqueue(gd_ls_t * ls, int lkid);
void remote_remove_resdata(gd_ls_t * ls, int nodeid, char *name, int namelen,
			   uint8_t sequence);
void allocate_and_copy_lvb(gd_ls_t * ls, char **lvbptr, char *src);

#endif				/* __LOCKQUEUE_DOT_H__ */
