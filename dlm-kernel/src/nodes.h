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
int init_new_csb(uint32_t nodeid, struct dlm_csb ** ret_csb);
void release_csb(struct dlm_csb * csb);
uint32_t our_nodeid(void);
int ls_nodes_reconfig(struct dlm_ls * ls, struct dlm_recover * gr, int *neg);
int ls_nodes_init(struct dlm_ls * ls, struct dlm_recover * gr);
int in_nodes_gone(struct dlm_ls * ls, uint32_t nodeid);
void ls_nodes_clear(struct dlm_ls *ls);
void ls_nodes_gone_clear(struct dlm_ls *ls);

#endif				/* __NODES_DOT_H__ */
