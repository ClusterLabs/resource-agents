/*  
  Copyright (C) Mission Critical Linux, Inc. 2000
  Copyright (C) Red Hat, Inc. 2002-2004

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.
                                                                                
  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.
                                                                                
  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to the
  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge,
  MA 02139, USA.
 */
#ifndef _MAGMACOMM_H
#define _MAGMACOMM_H
#include <magma.h>

int msg_update(cluster_member_list_t *membership);
ssize_t msg_receive_timeout(int fd, void *buf, ssize_t count,
			    unsigned int timeout);
int msg_receive(int fd, void *buf, ssize_t buflen);
ssize_t msg_peek(int fd, void *buf, ssize_t buflen);
ssize_t msg_receive_timeout(int fd, void *buf, ssize_t count,
			    unsigned int timeout);
int msg_open(uint64_t nodeid, uint16_t baseport, int purpose, int timeout);
int msg_listen(uint16_t baseport, int purpose, int *ret, int retlen);
int msg_accept(int fd, int members_only, uint64_t *nodeid);
int msg_close(int fd);
int msg_fill_fdset(fd_set *set, int flags, int purpose);
int msg_next_fd(fd_set *set);
ssize_t msg_send(int fd, void *buf, ssize_t count);
int msg_set_purpose(int fd, int purpose);
int msg_get_purpose(int fd);
int msg_get_flags(int fd); /* Read-only for users */

#define MSG_OPEN	0x1
#define MSG_LISTEN	0x2
#define MSG_CONNECTED	0x4
#define MSG_WRITE	0x8
#define MSG_READ	0x10
#define MSG_ALL		0x0	/** No set -> Don't care what flags exist */

#define MSGP_GENERIC	0
#define MSGP_ALL	-1
#define MSGP_CLUSTER	-2

#endif
