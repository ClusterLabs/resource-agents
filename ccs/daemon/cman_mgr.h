/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#ifndef __CMAN_MGR_DOT_H__
#define __CMAN_MGR_DOT_H__

extern int quorate;
extern int update_config;

int start_cman_monitor_thread(void);
int update_remote_nodes(char *mem_doc, int doc_size);

#endif /* __CMAN_MGR_DOT_H__ */
