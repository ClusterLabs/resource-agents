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

#ifndef __RECOVERD_DOT_H__
#define __RECOVERD_DOT_H__

void dlm_recoverd_init(void);
void recoverd_kick(gd_ls_t * ls);
int recoverd_start(void);
int recoverd_stop(void);

#endif				/* __RECOVERD_DOT_H__ */
