/*
  Copyright Red Hat, Inc. 2002-2003

  The Magma Cluster API Library is free software; you can redistribute
  it and/or modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either version
  2.1 of the License, or (at your option) any later version.

  The Magma Cluster API Library is distributed in the hope that it will
  be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
  of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
*/
/** @file
 * Header for clist.c
 */
#ifndef __CLIST_H
#define __CLIST_H

#include <sys/select.h>

int clist_insert(int fd, int flags);
int clist_delete(int fd);
int clist_fill_fdset(fd_set *set, int flags, int purpose);
int clist_next_set(fd_set *set);
int clist_set_purpose(int fd, int purpose);
int clist_get_purpose(int fd);
int clist_get_flags(int fd);
int clist_multicast(void *msg, int len, int purpose);

#endif
