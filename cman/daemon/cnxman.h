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

#ifndef __CNXMAN_H
#define __CNXMAN_H

/* In-kernel API */


/* Reasons for a kernel membership callback */
typedef enum { CLUSTER_RECONFIG, DIED, LEAVING, NEWNODE } kcl_callback_reason;

int cl_sendmsg(struct connection *con,
	       int port, int nodeid, int flags,
	       char *msg, size_t size);
int process_command(struct connection *con, int cmd, char *cmdbuf,
		    char **retbuf, int *retlen, int retsize, int offset);
int comms_receive_message(struct cl_comms_socket *csock);
struct cl_comms_socket *add_clsock(int broadcast, int number, int fd);
void unbind_con(struct connection *con);
int cluster_init(void);
void check_mainloop_flags();

#define min(a,b) (((a) < (b)) ? (a) : (b))
#define max(a,b) (((a) > (b)) ? (a) : (b))


#endif
