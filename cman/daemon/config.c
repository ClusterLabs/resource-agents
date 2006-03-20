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

/* AIS totem defaults */
#define DEF_TOKEN_RETRANSMITS_BEFORE_LOSS_CONST     4
#define DEF_TOKEN_TIMEOUT                           1000
#define DEF_TOKEN_RETRANSMIT_TIMEOUT                238
#define DEF_TOKEN_HOLD_TIMEOUT                      190
#define DEF_JOIN_TIMEOUT                            100
#define DEF_CONSENSUS_TIMEOUT                       200
#define DEF_MERGE_TIMEOUT                           200
#define DEF_DOWNCHECK_TIMEOUT                       1000
#define DEF_FAIL_TO_RECV_CONST                      10
#define DEF_SEQNO_UNCHANGED_CONST                   30
#define DEF_THREAD_COUNT	                    2

/* Our timeouts in Ms */
#define DEF_QUORUMDEV_POLL                          10000
#define DEF_SHUTDOWN_TIMEOUT                        5000

#define DEF_DEBUG_LEVEL                             0
#define DEF_DEBUG_MASK                              0xff

struct config_entry cman_config[] = {
	[TOKEN_RETRANSMITS_BEFORE_LOSS_CONST] = { .name = "token_retransmits", .value = DEF_TOKEN_RETRANSMITS_BEFORE_LOSS_CONST},
	[TOKEN_TIMEOUT] = { .name = "token_timeout", .value = DEF_TOKEN_TIMEOUT},
        [TOKEN_RETRANSMIT_TIMEOUT] = { .name = "token_retransmit_timeout", .value = DEF_TOKEN_RETRANSMIT_TIMEOUT},
        [TOKEN_HOLD_TIMEOUT] = { .name = "token_hold_timeout", .value = DEF_TOKEN_HOLD_TIMEOUT},
	[JOIN_TIMEOUT] = { .name = "join_timeout", .value = DEF_JOIN_TIMEOUT},
	[CONSENSUS_TIMEOUT] = { .name = "consensus_timeout", .value = DEF_CONSENSUS_TIMEOUT},
	[MERGE_TIMEOUT] = { .name = "merge_timeout", .value = DEF_MERGE_TIMEOUT},
	[DOWNCHECK_TIMEOUT] = { .name = "downcheck_timeout", .value = DEF_DOWNCHECK_TIMEOUT},
	[FAIL_TO_RECV_CONST] = { .name = "fail_to_recv_const", .value = DEF_FAIL_TO_RECV_CONST},
	[SEQNO_UNCHANGED_CONST] = { .name = "seqno_unchanged_const", .value = DEF_SEQNO_UNCHANGED_CONST},

	[QUORUMDEV_POLL] = { .name = "quorumdev_poll", .value = DEF_QUORUMDEV_POLL},
	[SHUTDOWN_TIMEOUT] = { .name = "shutdown_timeout", .value = DEF_SHUTDOWN_TIMEOUT},

	[THREAD_COUNT] = { .name = "thread_count", .value = DEF_THREAD_COUNT},
	[DEBUG_MASK] = { .name = "debug_mask", .value = DEF_DEBUG_MASK},
	[DEBUG_LEVEL] = { .name = "debug_level", .value = DEF_DEBUG_LEVEL},

};

#define CCS_CMAN_PREFIX "/cluster/cman/@"
void init_config()
{
	int cd;
	int i;
	int error;
	char *str;

	cd = ccs_force_connect(NULL, 0);
	if (cd < 0)
		return;


	for (i=0; i<sizeof(cman_config)/sizeof(struct config_entry); i++)
	{
		char keyname[1024];

		if (cman_config[i].name)
		{
			sprintf(keyname, CCS_CMAN_PREFIX "%s", cman_config[i].name);

			error = ccs_get(cd, keyname, &str);
			if (!error)
			{
				cman_config[i].value = atoi(str);
				free(str);
			}
		}
	}
	ccs_disconnect(cd);
}
