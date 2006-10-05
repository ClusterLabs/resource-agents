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
#ifndef _XVM_TCP_H
#define _XVM_TCP_H

int ipv4_connect(struct in_addr *in_addr, uint16_t port, int timeout);
int ipv6_connect(struct in6_addr *in6_addr, uint16_t port, int timeout);
int ipv4_listen(uint16_t port, int backlog);
int ipv6_listen(uint16_t port, int backlog);

#endif
