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

#ifndef __SM_JOINLEAVE_DOT_H__
#define __SM_JOINLEAVE_DOT_H__

void init_joinleave(void);
void new_joinleave(sm_sevent_t *sev);
void process_joinleave(void);
void backout_sevents(void);
sm_sevent_t *find_sevent(unsigned int id);

#endif
