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
#ifndef _XVM_SIMPLE_AUTH_H
#define _XVM_SIMPLE_AUTH_H

#include <sys/types.h>

/* 2-way challenge/response simple auth */
#define DEFAULT_KEY_FILE "/etc/cluster/fence_xvm.key"

int read_key_file(char *, char *, size_t);
int tcp_challenge(int, fence_auth_type_t, void *, size_t, int);
int tcp_response(int, fence_auth_type_t, void *, size_t, int);
int sign_request(fence_req_t *, void *, size_t);
int verify_request(fence_req_t *, fence_hash_t, void *, size_t);

/* SSL certificate-based authentication TBD */

#endif
