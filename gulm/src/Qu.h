/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2001  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
/******************************************************************************
 * Queues!
 * uses LLi for its dirty work
 */
#ifndef __Qu_h__
#define __Qu_h__

#include "LLi.h"

typedef LLi_t Qu_t;

#define Qu_init(i,d) LLi_init((i),(d))
#define Qu_init_head(i) LLi_init((i),NULL)
#define Qu_empty(Q) LLi_empty(Q)
#define Qu_data(i) LLi_data(i)
#define Qu_EnQu(Q,i) LLi_add_before((Q),(i))
#define Qu_EnQu_Front(Q,i) LLi_add_after((Q),(i))
#define Qu_DeQu(Q) LLi_pop(Q)
#define Qu_peek(Q) LLi_next(Q)

#endif /*__Qu_h__*/
/* vim: set ai cin et sw=3 ts=3 : */
