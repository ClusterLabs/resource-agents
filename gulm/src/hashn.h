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
/******************************************************************************
 * Hash table abstraction
 */
#ifndef __hash_h__
#define __hash_h__

#include "LLi.h"

typedef struct hashn_root_s hashn_t;

hashn_t *hashn_create(unsigned int table_size,
      int (*cmp)(void *a, void *b),
      int (*hash)(void *a));
void hashn_destroy(hashn_t *hsh);
LLi_t *hashn_find(hashn_t *hsh, LLi_t *item);
int hashn_add(hashn_t *hsh, LLi_t *item);
LLi_t *hashn_del(hashn_t *hsh, LLi_t *item);
int hashn_walk(hashn_t *hsh, int (*on_each)(LLi_t *item, void *misc), void *misc);

#endif /*__hash_h__*/
/* vim: set ai cin et sw=3 ts=3 : */
