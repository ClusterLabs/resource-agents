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

#include <openais/service/logsys.h>
#include "logging.h"

LOGSYS_DECLARE_SUBSYS ("CMAN", LOG_INFO);

int subsys_mask = 0;

void set_debuglog(int subsystems)
{
	subsys_mask = subsystems;
}
