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

#ifndef __SM_CONTROL_DOT_H__
#define __SM_CONTROL_DOT_H__

void sm_init(void);
void sm_start(void);
int sm_stop(int force);
void sm_member_update(int quorate);

#endif
