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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <libgen.h>

#include <linux/types.h>
#include "libgfs2.h"
#include "gfs2_mkfs.h"

char *prog_name;

/**
 * main - do everything
 * @argc:
 * @argv:
 *
 * Returns: 0 on success, non-0 on failure
 */

int
main(int argc, char *argv[])
{
	char *p, *whoami;

	prog_name = argv[0];
	SRANDOM;

	p = strdup(prog_name);
	whoami = basename(p);
	
	if (!strcmp(whoami, "gfs2_jadd"))
		main_jadd(argc, argv);
	else if (!strcmp(whoami, "gfs2_mkfs") || !strcmp(whoami, "mkfs.gfs2"))
		main_mkfs(argc, argv);
#if 0
	if (!strcmp(whoami, "gfs2_grow"))
		main_grow(argc, argv);
	else if (!strcmp(whoami, "gfs2_shrink"))
		main_shrink(argc, argv);
#endif
	else
		die("I don't know who I am!\n");

	free(p);

	return EXIT_SUCCESS;
}
