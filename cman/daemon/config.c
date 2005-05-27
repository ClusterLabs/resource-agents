/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#include <stdio.h>
#include <stdlib.h>

#include "ccs.h"
#include "config.h"

/* Config file defaults */

#define DEFAULT_JOIN_WAIT_TIME   16	/* Time to wait while sending JOINREQ
					 * messages. Should be at least twice
					 * the HELLO timer, probably 3x */
#define DEFAULT_JOIN_TIMEOUT     30	/* How long we wait after getting a
					 * JOINACK to regarding that node as
					 * dead */
#define DEFAULT_HELLO_TIMER       5	/* Period between HELLO messages */
#define DEFAULT_DEADNODE_TIMER   21	/* If we don't get a message from a
					 * node in this period kill it */
#define DEFAULT_TRANSITION_TIMER 15	/* Maximum time a state transition
					 * should take */
#define DEFAULT_JOINCONF_TIMER    5	/* Time allowed to a node to respond to
					 * a JOINCONF message */
#define DEFAULT_MAX_NODES       128	/* Max allowed nodes */
#define DEFAULT_TRANSITION_RESTARTS  10	/* Maximum number of transition
					 * restarts before we die */
#define DEFAULT_SM_DEBUG_SIZE	256	/* Size in bytes of SM debug buffer */

#define DEFAULT_NEWCLUSTER_TIMEOUT 16   /* Time to send NEWCLUSTER messages */
#define DEFAULT_MAX_RETRIES 5		/* Number of times we resend a message */

#define DEFAULT_DEBUG_MASK 0            /* No debugging */

struct config_info cman_config = {
	.joinwait_timeout = DEFAULT_JOIN_WAIT_TIME,
	.joinconf_timeout = DEFAULT_JOINCONF_TIMER,
	.join_timeout = DEFAULT_JOIN_TIMEOUT,
	.hello_timer = DEFAULT_HELLO_TIMER,
	.deadnode_timeout = DEFAULT_DEADNODE_TIMER,
	.transition_timeout = DEFAULT_TRANSITION_TIMER,
	.transition_restarts = DEFAULT_TRANSITION_RESTARTS,
	.max_nodes = DEFAULT_MAX_NODES,
	.sm_debug_size = DEFAULT_SM_DEBUG_SIZE,
	.newcluster_timeout = DEFAULT_NEWCLUSTER_TIMEOUT,
	.max_retries = DEFAULT_MAX_RETRIES,
	.debug_mask = DEFAULT_DEBUG_MASK,
};

#define CCS_CMAN_PREFIX "/cluster/cman/config/@"
void init_config()
{
	int cd;
	int error;
	char *str;

	cd = ccs_force_connect(NULL, 0);
	if (cd < 0)
		return;

	error = ccs_get(cd, CCS_CMAN_PREFIX "joinwait_timeout", &str);
	if (!error)
	{
		cman_config.joinwait_timeout = atoi(str);
		free(str);
	}

	error = ccs_get(cd, CCS_CMAN_PREFIX "joinconf_timeout", &str);
	if (!error)
	{
		cman_config.joinconf_timeout = atoi(str);
		free(str);
	}
	error = ccs_get(cd, CCS_CMAN_PREFIX "join_timeout", &str);
	if (!error)
	{
		cman_config.join_timeout = atoi(str);
		free(str);
	}
	error = ccs_get(cd, CCS_CMAN_PREFIX "hello_timer", &str);
	if (!error)
	{
		cman_config.hello_timer = atoi(str);
		free(str);
	}
	error = ccs_get(cd, CCS_CMAN_PREFIX "deadnode_timeout", &str);
	if (!error)
	{
		cman_config.deadnode_timeout = atoi(str);
		free(str);
	}
	error = ccs_get(cd, CCS_CMAN_PREFIX "transition_timeout", &str);
	if (!error)
	{
		cman_config.transition_timeout = atoi(str);
		free(str);
	}
	error = ccs_get(cd, CCS_CMAN_PREFIX "transition_restarts", &str);
	if (!error)
	{
		cman_config.transition_restarts = atoi(str);
		free(str);
	}
	error = ccs_get(cd, CCS_CMAN_PREFIX "max_nodes", &str);
	if (!error)
	{
		cman_config.max_nodes = atoi(str);
		free(str);
	}
	error = ccs_get(cd, CCS_CMAN_PREFIX "newcluster_timeout", &str);
	if (!error)
	{
		cman_config.newcluster_timeout = atoi(str);
		free(str);
	}
	error = ccs_get(cd, CCS_CMAN_PREFIX "max_retries", &str);
	if (!error)
	{
		cman_config.max_retries = atoi(str);
		free(str);
	}
	error = ccs_get(cd, CCS_CMAN_PREFIX "debug_mask", &str);
	if (!error)
	{
		cman_config.debug_mask = atoi(str);
		free(str);
	}
	ccs_disconnect(cd);
}
