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

typedef enum {
	F_FOREGROUND	= 0x1,
	F_NOCCS		= 0x2,
	F_ERR		= 0x4,
	F_HELP		= 0x8,
	F_USE_UUID	= 0x10,
	F_VERSION	= 0x20,
	F_CCSERR	= 0x40,
	F_CCSFAIL	= 0x80
} arg_flags_t;


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
	arg_flags_t flags;
	int debug;
} fence_xvm_args_t;

/* Private structure for commandline / stdin fencing args */
struct arg_info {
	char opt;
	char *opt_desc;
	char *stdin_opt;
	char *desc;
	void (*assign)(fence_xvm_args_t *, struct arg_info *, char *);
};


/* Get options */
void args_init(fence_xvm_args_t *args);
void args_finalize(fence_xvm_args_t *args);

void args_get_getopt(int argc, char **argv, char *optstr,
		     fence_xvm_args_t *args);
void args_get_stdin(char *optstr, fence_xvm_args_t *args);
void args_get_ccs(char *optstr, fence_xvm_args_t *args);
void args_usage(char *progname, char *optstr, int print_stdin);
void args_print(fence_xvm_args_t *args);

#endif
