/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __DLM_TOOL_DOT_H__
#define __DLM_TOOL_DOT_H__

#include <sys/types.h>
#include <asm/types.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <fcntl.h>
#include <netdb.h>
#include <limits.h>
#include <unistd.h>

#include "dlm_member.h"

extern char *prog_name;

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define die(fmt, args...) \
do \
{ \
	fprintf(stderr, "%s: ", prog_name); \
	fprintf(stderr, fmt "\n", ##args); \
	exit(EXIT_FAILURE); \
} \
while (0)

#define log_error(fmt, args...) \
do \
{ \
	fprintf(stderr, "%s: ", prog_name); \
	fprintf(stderr, fmt "\n", ##args); \
} \
while (0)

#endif  /*  __DLM_TOOL_DOT_H__  */
