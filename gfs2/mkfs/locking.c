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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <ctype.h>

#include "gfs2_mkfs.h"

/**
 * test_locking - Make sure the GFS2 is set up to use the right lock protocol
 * @lockproto: the lock protocol to mount
 * @locktable: the locktable name
 *
 */

void
test_locking(char *lockproto, char *locktable)
{
	char *c;

	if (strcmp(lockproto, "lock_nolock") == 0) {
		/*  Nolock is always ok.  */
	} else if (strcmp(lockproto, "lock_gulm") == 0 ||
		   strcmp(lockproto, "lock_dlm") == 0) {
		for (c = locktable; *c; c++) {
			if (isspace(*c))
				die("locktable error: contains space characters\n");
			if (!isprint(*c))
				die("locktable error: contains unprintable characters\n");
		}

		c = strstr(locktable, ":");
		if (!c)
			die("locktable error: missing colon in the locktable\n");

		if (c == locktable)
			die("locktable error: missing cluster name\n");
		if (c - locktable > 16)
			die("locktable error: cluster name too long\n");

		c++;
		if (!c)
			die("locktable error: missing filesystem name\n");

		if (strstr(c, ":"))
			die("locktable error: more than one colon present\n");

		if (!strlen(c))
			die("locktable error: missing filesystem name\n");
		if (strlen(c) > 16)
			die("locktable error: filesystem name too long\n");
	} else {
		die("lockproto error: %s unknown\n", lockproto);
	}
}
