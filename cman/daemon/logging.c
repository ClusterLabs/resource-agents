/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005-2007 Red Hat, Inc.  All rights reserved.
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
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* openais header */
#include <openais/service/print.h>

#include "logging.h"

/* All logging comes through here so it can be stamped [CMAN] */

static int use_stderr = 0;
int subsys_mask = 0;

void log_msg(int priority, char *fmt, ...)
{
	va_list va;
	char log_buf[1024];

	va_start(va, fmt);
	vsprintf(log_buf, fmt, va);
	va_end(va);
	log_printf(priority, log_buf);
}

void init_debug(int subsystems)
{
	log_init("CMAN");

	use_stderr = (subsystems != 0);
	subsys_mask = subsystems;
}

void set_debuglog(int subsystems)
{
	subsys_mask = subsystems;
}

void log_debug(int subsys, int stamp, const char *fmt, ...)
{
	va_list va;
	char newfmt[strlen(fmt)+10];
	char log_buf[1024];

	if (!(subsys_mask & subsys))
		return;

	if (stamp)
	{
		switch(subsys)
		{
		case CMAN_DEBUG_MEMB:
			strcpy(newfmt, "memb: ");
			break;
		case CMAN_DEBUG_DAEMON:
			strcpy(newfmt, "daemon: ");
			break;
		case CMAN_DEBUG_BARRIER:
			strcpy(newfmt, "barrier: ");
			break;
		case CMAN_DEBUG_AIS:
			strcpy(newfmt, "ais: ");
			break;
		default:
			break;
		}
	}
	else
	{
		newfmt[0] = '\0';
	}
	strcat(newfmt, fmt);

	va_start(va, fmt);
	vsprintf(log_buf, newfmt, va);
	log_printf(LOG_LEVEL_DEBUG, log_buf);
	va_end(va);
}
