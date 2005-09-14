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

#ifndef _FILE_H
#define _FILE_H

#include <stdint.h>
#include "fsck_incore.h"

int readi(struct fsck_inode *ip, void *buf, uint64_t offset, unsigned int size);
int writei(struct fsck_inode *ip, void *buf, uint64_t offset, unsigned int size);

#endif /* _FILE_H */
