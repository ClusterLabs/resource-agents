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

#ifndef __UTIL_DOT_H__
#define __UTIL_DOT_H__

int atoi (char *c);
int inet_aton (char *ascii, uint32_t * ip);
void inet_ntoa (uint32_t ip, char *buf);
void dump_buffer (void *buf, int len);

uint32_t __inline__ hash_lock_key (uint8_t * in, uint8_t len);
uint8_t __inline__ fourtoone (uint32_t);

__inline__ int testbit (uint16_t bit, uint8_t * set);
__inline__ void setbit (uint16_t bit, uint8_t * set);
__inline__ void clearbit (uint16_t bit, uint8_t * set);

#endif				/*  __UTIL_DOT_H__  */
