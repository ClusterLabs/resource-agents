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

#ifndef __SM_RECOVER_DOT_H__
#define __SM_RECOVER_DOT_H__

void init_recovery(void);
void process_recoveries(void);
void process_nodechange(void);
int check_recovery(sm_group_t *sg, int event_id);
void process_recover_msg(sm_msg_t *smsg, uint32_t nodeid);

#endif
