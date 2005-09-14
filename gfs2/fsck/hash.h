/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef _HASH_H
#define _HASH_H

uint32_t fsck_hash(const void *data, unsigned int len);
uint32_t fsck_hash_more(const void *data, unsigned int len, uint32_t hash);

#endif				/* _HASH_H  */
