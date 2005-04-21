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

static int use_stderr;

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
