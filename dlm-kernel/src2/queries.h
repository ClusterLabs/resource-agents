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

#ifndef __QUERIES_DOT_H__
#define __QUERIES_DOT_H__

extern int remote_query(int nodeid, struct dlm_ls *ls, struct dlm_header *msg);
extern int remote_query_reply(int nodeid, struct dlm_ls *ls, struct dlm_header *msg);

#endif                          /* __QUERIES_DOT_H__ */
