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
	int rsbtbl_size;
	int lkbtbl_size;
	int dirtbl_size;
	int conn_increment;
	int deadlocktime;
	int recover_timer;
};

extern struct config_info dlm_config;
extern int  dlm_config_init(void);
extern void dlm_config_exit(void);

#endif				/* __CONFIG_DOT_H__ */
