/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#ifndef __nodel_h__
#define __nodel_h__
#include "hash.h"
#include <netinet/in.h>
hash_t *initialize_nodel(void);
int update_nodel(hash_t *hsh, char *name, struct in6_addr *ip, uint8_t state);
int validate_nodel(hash_t *hsh, char *name, struct in6_addr *ip);
#endif /*__nodel_h__*/

/* vim: set ai cin et sw=3 ts=3 : */
