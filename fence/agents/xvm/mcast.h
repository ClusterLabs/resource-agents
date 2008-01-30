/*
  Copyright Red Hat, Inc. 2006

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
#ifndef _XVM_MCAST_H
#define _XVM_MCAST_H

#define IPV4_MCAST_DEFAULT "225.0.0.12"
#define IPV6_MCAST_DEFAULT "ff05::3:1"

int ipv4_recv_sk(char *addr, int port);
int ipv4_send_sk(char *src_addr, char *addr, int port,
		 struct sockaddr *src, socklen_t slen,
		 int ttl);
int ipv6_recv_sk(char *addr, int port);
int ipv6_send_sk(char *src_addr, char *addr, int port,
		 struct sockaddr *src, socklen_t slen,
		 int ttl);

#endif
