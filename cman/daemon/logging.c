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

#include <syslog.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "list.h"
#include "cnxman-socket.h"
#include "cnxman-private.h"

static int use_stderr = 0;
static int subsys_mask = 0;

int init_log(int debug)
{
	openlog("cman", LOG_CONS|LOG_PID, LOG_DAEMON);
	use_stderr = debug;

	return 0;
}


void log_msg(int priority, const char *fmt, ...)
{
	va_list va;

	if (use_stderr)
	{
		fprintf(stderr, "cman: ");
		va_start(va, fmt);
		vfprintf(stderr, fmt, va);
		va_end(va);
	}
	else
	{
		va_start(va, fmt);
		vsyslog(priority, fmt, va);
		va_end(va);
	}
}

void init_debug(int subsystems)
{
	subsys_mask = subsystems;
}

#ifdef DEBUG
void log_debug(int subsys, const char *fmt, ...)
{
	va_list va;
	char newfmt[strlen(fmt)+10];

	if (!(subsys_mask & subsys))
		return;

	switch(subsys)
	{
	case CMAN_DEBUG_MEMB:
		strcpy(newfmt, "memb: ");
		break;
	case CMAN_DEBUG_COMMS:
		strcpy(newfmt, "comms: ");
		break;
	case CMAN_DEBUG_DAEMON:
		strcpy(newfmt, "daemon: ");
		break;
	case CMAN_DEBUG_BARRIER:
		strcpy(newfmt, "barrier: ");
		break;
	default:
		break;
	}
	strcat(newfmt, fmt);

	va_start(va, fmt);
	if (use_stderr)
		vfprintf(stderr, newfmt, va);
	else
		vsyslog(LOG_DEBUG, newfmt, va);
	va_end(va);
}
#endif
