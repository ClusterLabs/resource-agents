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

#ifndef __trans_h__
#define __trans_h__

#include <signal.h>

extern int got_sighup;

void sig_hup(int sig);
sigset_t block_sigchld();
void unblock_sigchld();
sigset_t block_sighup();
void unblock_sighup();
int retry_read(int fd, void *buf, size_t count);
int retry_write(int fd, void *buf, size_t count);
char *gstrerror(int errcode);
int connect_to_comm_device(char *name);
int send_cmd(int fd, uint32_t cmd, char *type);
int recv_reply(int fd, char *type);
int send_u32(int fd, uint32_t msg);
int recv_u32(int fd, uint32_t *msg);

/* FIXME -- there are errno values.. should I do this differently */
#define GNBD_GOT_SIGHUP 254
#define GNBD_GOT_EOF    253

#endif /* __trans_h__ */
