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

/* Make this global so that all of cman can use the same subsys name */
unsigned int logsys_subsys_id;
int subsys_mask = 0;

void init_debug(int subsystems)
{
	logsys_subsys_id = logsys_config_subsys_set ("CMAN", LOGSYS_TAG_LOG, (subsystems?LOG_LEVEL_DEBUG:LOG_LEVEL_WARNING) );
	logsys_config_mode_set(LOG_MODE_BUFFER_BEFORE_CONFIG | ((subsystems)?LOG_MODE_OUTPUT_STDERR:0));
	subsys_mask = subsystems;
}

void set_debuglog(int subsystems)
{
	subsys_mask = subsystems;
}


void cman_flush_debuglog()
{
	logsys_config_mode_set(LOG_MODE_FLUSH_AFTER_CONFIG | ((subsys_mask)?LOG_MODE_OUTPUT_STDERR:0));
}
