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
	int tcp_port;
	int lock_timeout;
	int buffer_size;
	int reshashtbl;
	int lockidtbl;
	int max_connections;
	int deadlocktime;
};

extern struct config_info dlm_config;
extern int  dlm_config_init(void);
extern void dlm_config_exit(void);

#endif				/* __CONFIG_DOT_H__ */
