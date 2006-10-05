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
#ifndef _XVM_OPTIONS_H
#define _XVM_OPTIONS_H

typedef struct {
	char *addr;
	char *domain;
	char *key_file;
	fence_cmd_t op;
	fence_hash_t hash;
	fence_auth_type_t auth;
	int port;
	int family;
	int timeout;
	int retr_time;
#define F_FOREGROUND	0x1
#define F_DEBUG		0x2
#define F_ERR		0x4
#define F_HELP		0x8
#define F_USE_UUID	0x10
#define F_VERSION	0x20
	int flags;
} fence_xvm_args_t;


/* Get options */
void args_init(fence_xvm_args_t *args);
void args_finalize(fence_xvm_args_t *args);

void args_get_getopt(int argc, char **argv, char *optstr,
		     fence_xvm_args_t *args);
void args_get_stdin(char *optstr, fence_xvm_args_t *args);
void args_usage(char *progname, char *optstr, int print_stdin);
void args_print(fence_xvm_args_t *args);

#endif
