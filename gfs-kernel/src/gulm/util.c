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

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include "utils_crc.h"

/**
 * atoi
 *
 * @c:
 *
 */

int
atoi (char *c)
{
	int x = 0;

	while ('0' <= *c && *c <= '9') {
		x = x * 10 + (*c - '0');
		c++;
	}

	return (x);
}

/**
 * inet_aton
 *
 * @ascii:
 * @ip:
 *
 */

int
inet_aton (char *ascii, uint32_t * ip)
{
	uint32_t value;
	int x;

	*ip = 0;

	for (x = 0; x < 4; x++) {
		value = atoi (ascii);
		if (value > 255)
			return (-1);

		*ip = (*ip << 8) | value;

		if (x != 3) {
			for (; *ascii != '.' && *ascii != '\0'; ascii++) {
				if (*ascii < '0' || *ascii > '9') {
					/* not a number. stop */
					return -1;
				}
			}
			if (*ascii == '\0')
				return (-1);

			ascii++;
		}
	}

	return (0);
}

/**
 * inet_ntoa
 *
 * @ascii:
 * @ip:
 *
 */
void
inet_ntoa (uint32_t ip, char *buf)
{
	int i;
	char *p;

	p = buf;

	for (i = 3; i >= 0; i--) {
		p += sprintf (p, "%d", (ip >> (8 * i)) & 0xFF);
		if (i > 0)
			*(p++) = '.';
	}

}

/* public functions */
#define hash_init_val 0x6d696b65

uint32_t __inline__
hash_lock_key (uint8_t * in, uint8_t len)
{				/* other hash function was to variable */
	return crc32 (in, len, hash_init_val);
}
