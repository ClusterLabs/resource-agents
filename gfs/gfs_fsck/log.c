/*****************************************************************************
******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
******************************************************************************
*****************************************************************************/

#include <stdio.h>
#include <stdarg.h>
#include <libintl.h>
#define _(String) gettext(String)
#include "log.h"

struct log_state {
	int print_level;
};
static struct log_state _state = {MSG_NOTICE};

void increase_verbosity(void)
{
	_state.print_level++;
}

void decrease_verbosity(void)
{
	_state.print_level--;
}

void print_msg(int priority, char *file, int line, const char *format, va_list args) {

	switch (priority) {

	case MSG_DEBUG:
		printf("(%s:%d)\t", file, line);
		vprintf(format, args);
		break;
	case MSG_INFO:
	case MSG_NOTICE:
	case MSG_WARN:
		vprintf(format, args);
		break;
	case MSG_ERROR:
	case MSG_CRITICAL:
	default:
		vfprintf(stderr, format, args);
		break;
	}
	return;
}

void print_fsck_log(int priority, char *file, int line, const char *format, ...)
{

	va_list args;
	const char *transform;

        va_start(args, format);

	transform = _(format);

	if(_state.print_level >= priority) {
		print_msg(priority, file, line, transform, args);
	}


	va_end(args);
}
