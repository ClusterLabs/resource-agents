/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**  
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

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
};
