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

#ifndef __BLOCK_DEVICE_DOT_H__
#define __BLOCK_DEVICE_DOT_H__


void find_device_size(void);
char *get_block(uint64_t bn, int fatal);

void print_size(void);
void print_hexblock(void);
void print_rawblock(void);


#endif /* __BLOCK_DEVICE_DOT_H__ */

