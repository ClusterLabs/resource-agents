/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**  
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "dlm_internal.h"
#include "rcom.h"

/**
 * dlm_hash - hash an array of data
 * @data: the data to be hashed
 * @len: the length of data to be hashed
 *
 * Copied from GFS which copied from...
 *
 * Take some data and convert it to a 32-bit hash.
 * This is the 32-bit FNV-1a hash from:
 * http://www.isthe.com/chongo/tech/comp/fnv/
 */

static __inline__ uint32_t
hash_more_internal(const void *data, unsigned int len, uint32_t hash)
{
	unsigned char *p = (unsigned char *)data;
	unsigned char *e = p + len;
	uint32_t h = hash;

	while (p < e) {
		h ^= (uint32_t)(*p++);
		h *= 0x01000193;
	}

	return h;
}

uint32_t dlm_hash(const void *data, int len)
{
	uint32_t h = 0x811C9DC5;
	h = hash_more_internal(data, len, h);
	return h;
}

void dlm_message_out(struct dlm_message *ms)
{
	struct dlm_header *hd = (struct dlm_header *) ms;

	hd->h_length = cpu_to_le16(hd->h_length);
}

void dlm_message_in(struct dlm_message *ms)
{
	struct dlm_header *hd = (struct dlm_header *) ms;

	hd->h_length = le16_to_cpu(hd->h_length);
}

void rcom_lock_out(struct rcom_lock *rl)
{
}

void rcom_lock_in(struct rcom_lock *rl)
{
}

void dlm_rcom_out(struct dlm_rcom *rc)
{
	rc->rc_header.h_length = cpu_to_le16(rc->rc_header.h_length);
}

void dlm_rcom_in(struct dlm_rcom *rc)
{
	struct dlm_header *hd = (struct dlm_header *) rc;

	hd->h_length = le16_to_cpu(hd->h_length);
}


