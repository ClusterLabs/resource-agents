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

#ifndef __NODES_DOT_H__
#define __NODES_DOT_H__

void dlm_nodes_init(void);
int init_new_csb(uint32_t nodeid, gd_csb_t ** ret_csb);
void release_csb(gd_csb_t * csb);
uint32_t our_nodeid(void);
int ls_nodes_reconfig(gd_ls_t * ls, gd_recover_t * gr, int *neg);
int ls_nodes_init(gd_ls_t * ls, gd_recover_t * gr);
int in_nodes_gone(gd_ls_t * ls, uint32_t nodeid);

#endif				/* __NODES_DOT_H__ */
