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

#ifndef __SM_BARRIER_DOT_H__
#define __SM_BARRIER_DOT_H__

#define SM_BARRIER_STARTDONE		(0)
#define SM_BARRIER_STARTDONE_NEW	(1)
#define SM_BARRIER_RECOVERY		(2)
#define SM_BARRIER_RESET		(3)

void init_barriers(void);
void process_barriers(void);
int sm_barrier(char *name, int count, int type);
void process_startdone_barrier(sm_group_t *sg, int status);
void process_startdone_barrier_new(sm_group_t *sg, int status);
void process_recovery_barrier(sm_group_t *sg, int status);

#endif
