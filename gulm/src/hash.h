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

typedef struct hash_root_s hash_t;

hash_t *hash_create(unsigned int table_size,
		unsigned char *(*getkey)(void *item),
		int (*getkeylen)(void *item));
void hash_destroy(hash_t *hsh);
LLi_t *hash_find(hash_t *hsh, unsigned char *key, unsigned int klen);
LLi_t *hash_del(hash_t *hsh, unsigned char *key, unsigned int klen);
int hash_add(hash_t *hsh, LLi_t *item);
int hash_walk(hash_t *hsh, int (*on_each)(LLi_t *item, void *misc),
      void *misc);

#endif /*__hash_h__*/
/* vim: set ai cin et sw=3 ts=3 : */
