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

#ifndef __SM_MISC_DOT_H__
#define __SM_MISC_DOT_H__

void init_sm_misc(void);
sm_node_t *sm_new_node(uint32_t nodeid);
sm_node_t *sm_find_joiner(sm_group_t *sg, uint32_t nodeid);
sm_node_t *sm_find_member(uint32_t nodeid);
uint32_t sm_new_local_id(int level);
int sm_id_to_level(uint32_t id);
void sm_set_event_id(int *id);
sm_group_t *sm_local_id_to_sg(int id);
sm_group_t *sm_global_id_to_sg(int id);
void sm_debug_log(sm_group_t *sg, const char *fmt, ...);
void sm_debug_setup(int size);

#endif
