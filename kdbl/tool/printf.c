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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "kdbl.h"

#define SIZE (1048576)

/**
 * printf_dump - print out the incore print buffer
 * @argc:
 * @argv:
 *
 */

void
printf_dump(int argc, char *argv[])
{
	int fd;
	char *buf = NULL;
	unsigned int size = SIZE;
	unsigned int x;
	int continuous = FALSE;
	int error;


	if (argc == 3) {
		if (strcmp(argv[2], "-c") == 0)
			continuous = TRUE;
		else
			die("unknown option %s\n", argv[2]);
	}
	else if (argc != 2)
		die("Usage: debug_tool dump [-c]\n");

	buf = malloc(size);
	if (!buf)
		die("out of memory\n");


	fd = open(KDBL_DEVICE, O_RDWR);
	if (fd < 0)
		die("can't open %s: %s\n",
		    KDBL_DEVICE, strerror(errno));


	for (;;) {
		error = write(fd, "printf_dump", 11);
		if (error != 11)
			die("error doing printf_dump (%d)\n",
			    error);

		error = read(fd, buf, size);
		if (error < 0) {
			if (errno == ENOBUFS) {
				size += SIZE;
				buf = realloc(buf, size);
				if (!buf)
					die("out of memory\n");
				continue;
			} else
				die("can't read kdbl device (%d): %s\n",
				    error,
				    strerror(errno));

		}
		
		for (x = 0; x < size; x++)
			if (buf[x])
				putchar(buf[x]);

		if (!continuous)
			break;

		fflush(stdout);

		sleep(1);
	}


	free(buf);

	close(fd);
}
