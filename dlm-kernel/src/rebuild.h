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

#ifndef __REBUILD_DOT_H__
#define __REBUILD_DOT_H__

int rebuild_rsbs_send(gd_ls_t * ls);
int rebuild_rsbs_recv(gd_ls_t * ls, int nodeid, char *buf, int len);
int rebuild_rsbs_lkids_recv(gd_ls_t * ls, int nodeid, char *buf, int len);
int rebuild_freemem(gd_ls_t * ls);

#endif				/* __REBUILD_DOT_H__ */
