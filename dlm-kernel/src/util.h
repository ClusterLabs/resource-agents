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

#ifndef __UTIL_DOT_H__
#define __UTIL_DOT_H__

uint32_t dlm_hash(const char *data, int len);

void print_lkb(struct dlm_lkb *lkb);
void print_rsb(struct dlm_rsb *r);
void print_request(struct dlm_request *req);
void print_reply(struct dlm_reply *rp);

#endif
