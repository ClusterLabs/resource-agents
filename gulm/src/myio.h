/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2004  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#ifndef __myio_h__
#define __myio_h__
#include <sys/uio.h>
int open_max(void);
ssize_t my_recv(int fd, void *buf, size_t len);
ssize_t my_send(int fd, void *buf, size_t len);
ssize_t my_recv_iov(int fd, struct iovec *iov, size_t iov_size);
ssize_t my_send_iov(int fd, struct iovec *iov, size_t iov_size);

int socket_free_send_space(int sk);
int set_opts(int sk);
int serv_listen(unsigned int port);

#endif /*__myio_h__*/

/* vim: set ai cin et sw=3 ts=3 : */
