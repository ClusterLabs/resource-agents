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

#ifndef __CONFIG_DOT_H__
#define __CONFIG_DOT_H__

struct config_info {
	int joinwait_timeout;
	int joinconf_timeout;
	int join_timeout;
	int hello_timer;
	int deadnode_timeout;
	int transition_timeout;
	int transition_restarts;
	int max_nodes;
	int sm_debug_size;
        int newcluster_timeout;
};

extern struct config_info cman_config;

#endif				/* __CONFIG_DOT_H__ */
