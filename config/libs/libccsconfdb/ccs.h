/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2004-2008 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#ifdef __CCS_COMPAT_DOT_H__
#error DO NOT INCLUDE libccscompat.h and ccs.h at the same time. it is BAD!
#endif

#ifndef __CCS_DOT_H__
#define __CCS_DOT_H__

int ccs_connect(void);
int ccs_force_connect(char const *cluster_name, int blocking);
int ccs_disconnect(int desc);
int ccs_get(int desc, const char *query, char **rtn);
int ccs_get_list(int desc, const char *query, char **rtn);
int ccs_set(int desc, const char *path, char *val);
int ccs_lookup_nodename(int desc, const char *nodename, char **rtn);

extern int fullxpath;

#endif /*  __CCS_DOT_H__ */
