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

#ifndef __CONFIG_DOT_H__
#define __CONFIG_DOT_H__

struct config_entry {
	char *name;
	int value;
};


extern struct config_entry cman_config[];

/* Indexes into the array */
#define TOKEN_RETRANSMITS_BEFORE_LOSS_CONST     0
#define TOKEN_TIMEOUT                           1
#define TOKEN_RETRANSMIT_TIMEOUT                2
#define TOKEN_HOLD_TIMEOUT                      3
#define JOIN_TIMEOUT                            4
#define CONSENSUS_TIMEOUT                       5
#define MERGE_TIMEOUT                           6
#define DOWNCHECK_TIMEOUT                       7
#define FAIL_TO_RECV_CONST                      8
#define SEQNO_UNCHANGED_CONST                   9
#define QUORUMDEV_POLL                         10
#define SHUTDOWN_TIMEOUT                       11
#define THREAD_COUNT                           12

#define DEBUG_MASK                             30
#define DEBUG_LEVEL                            31

extern void init_config(void);
#endif				/* __CONFIG_DOT_H__ */
