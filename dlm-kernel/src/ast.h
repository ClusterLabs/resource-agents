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

#ifndef __AST_DOT_H__
#define __AST_DOT_H__

void lockqueue_lkb_mark(gd_ls_t *ls);
int resend_cluster_requests(gd_ls_t *ls);
void add_to_lockqueue(gd_lkb_t *lkb);
void remove_from_lockqueue(gd_lkb_t *lkb);
void add_to_deadlockqueue(gd_lkb_t *lkb);
void remove_from_deadlockqueue(gd_lkb_t *lkb);
void queue_ast(gd_lkb_t *lkb, uint16_t astflags, uint8_t rqmode);
void wake_astd(void);
int astd_start(void);
void astd_stop(void);

#endif				/* __AST_DOT_H__ */
