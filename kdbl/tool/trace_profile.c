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
#include <sys/ioctl.h>
#include <errno.h>

#define HELPER_PROGRAM
#include <linux/gfs_debug_const.h>

#include "kdbl.h"

/**
 * trace_print - print out the available tracing flags
 * @argc:
 * @argv:
 *
 */

void
trace_print(int argc, char *argv[])
{
	const debug_flag_t *df = NULL;
	unsigned int x;

	if (argc != 3)
		die("Usage: kdbl_tool trprint <program>\n");

	if (FALSE) {
		/* Do nothing */
	} else if (strcmp(argv[2], "gfs") == 0)
		df = gfs_debug_flags;
	else
		die("unknown program %s\n", argv[2]);

	for (x = 0; df[x].name[0]; x++)
		printf("%-30s %u\n",
		       df[x].name, df[x].len);
}

/**
 * trace_change - change a trace flag
 * @argc:
 * @argv:
 *
 */

void
trace_change(int argc, char *argv[])
{
	int fd;
	const debug_flag_t *df = NULL;
	char *program, *version, *state;
	int arg;
	unsigned int x, y;
	int error;

	if (argc < 4)
		die("Usage: kdbl_tool <tron|troff> <program> <flags>\n");

	fd = open(KDBL_DEVICE, O_RDWR);
	if (fd < 0)
		die("can't open %s: %s\n", KDBL_DEVICE, strerror(errno));


	state = (strcmp(argv[1], "tron") == 0) ? "on" : "off";
	program = argv[2];

	if (strcmp(argv[2], "gfs") == 0) {
		df = gfs_debug_flags;
		version = GFS_DEBUG_VERSION;
	} else
		die("unknown program %s\n", argv[2]);


	for (arg = 3; arg < argc; arg++) {
		int found = FALSE;

		for (x = 0; df[x].name[0]; x++)
			if (strcmp(df[x].name, argv[arg]) == 0) {
				found = TRUE;
				break;
			}

		if (!found)
			die("can't find trace flag %s\n", argv[arg]);

		for (y = 0; y < df[x].len; y++) {
			char buf[256];
			int len;

			len = sprintf(buf, "trace_change %s %s %u %s",
				      program, version, df[x].flag[y], state);

			error = write(fd, buf, len);
			if (error != len)
				die("can't write trace_change command (%d): %s\n",
				    error, strerror(errno));

			error = read(fd, buf, 256);
			if (error) {
				if (error < 0 && errno == EILSEQ)
					die("symbol mismatch between kernel and kdbl_tool (recompile)\n");
				die("can't read trace_change command (%d): %s\n",
				    error, strerror(errno));
			}
		}
	}


	close(fd);
}

struct profile_element {
	uint64_t pe_total_calls;
	uint64_t pe_total_micros;
	uint64_t pe_min_micros;
	uint64_t pe_max_micros;
};

/**
 * sort_by_total -
 * @a: 
 * @b:
 *
 * Returns:  -1, 0, or 1
 */

static int
sort_by_total(const void *a, const void *b)
{
	struct profile_element **pa = (struct profile_element **)a;
	struct profile_element **pb = (struct profile_element **)b;

	if ((*pa)->pe_total_micros < (*pb)->pe_total_micros)
		return 1;
	else if ((*pa)->pe_total_micros > (*pb)->pe_total_micros)
		return -1;
	else
		return 0;
}


/**
 * sort_by_called -
 * @a: 
 * @b:
 *
 * Returns:  -1, 0, or 1
 */

static int
sort_by_called(const void *a, const void *b)
{
	struct profile_element **pa = (struct profile_element **)a;
	struct profile_element **pb = (struct profile_element **)b;

	if ((*pa)->pe_total_calls < (*pb)->pe_total_calls)
		return 1;
	else if ((*pa)->pe_total_calls > (*pb)->pe_total_calls)
		return -1;
	else
		return 0;
}


/**
 * sort_by_max -
 * @a: 
 * @b:
 *
 * Returns:  -1, 0, or 1
 */

static int
sort_by_max(const void *a, const void *b)
{
	struct profile_element **pa = (struct profile_element **)a;
	struct profile_element **pb = (struct profile_element **)b;

	if ((*pa)->pe_max_micros < (*pb)->pe_max_micros)
		return 1;
	else if ((*pa)->pe_max_micros > (*pb)->pe_max_micros)
		return -1;
	else
		return 0;
}

/**
 * profile_dump - print out profiling stats for a program
 * @argc:
 * @argv:
 *
 */

void
profile_dump(int argc, char *argv[])
{
	int fd;
	const debug_flag_t *df = NULL;
	char *program, *version, *data;
	unsigned int flags;
	char buf[256];
	int len;
	struct profile_element **p;
	unsigned int x, pass;
	const char *name;
	int error;

	if (argc != 3)
		die("Usage: kdbl_tool prof <program>\n");

	fd = open(KDBL_DEVICE, O_RDWR);
	if (fd < 0)
		die("can't open %s: %s\n",
		    KDBL_DEVICE, strerror(errno));


	program = argv[2];

	if (strcmp(argv[2], "gfs") == 0) {
		df = gfs_debug_flags;
		version = GFS_DEBUG_VERSION;
		flags = GFS_DEBUG_FLAGS;
	} else
		die("unknown program %s\n", argv[2]);


	data = malloc(flags * sizeof(struct profile_element));
	if (!data)
		die("out of memory\n");


	len = sprintf(buf, "profile_dump %s %s",
		      program, version);

	error = write(fd, buf, len);
	if (error != len)
		die("can't write profile_dump (%d): %s\n",
		    error, strerror(errno));

	error = read(fd, data, flags * sizeof(struct profile_element));
	if (error < 0 && errno == EILSEQ)
		die("symbol mismatch between kernel and kdbl_tool (recompile)\n");
	if (error != flags * sizeof(struct profile_element))
		die("can't read profile_dump (%d): %s\n",
		    error, strerror(errno));


	p = malloc(flags * sizeof(struct profile_element *));
	if (!p)
		die("out of memory");

	for (x = 0; x < flags; x++)
		p[x] = &((struct profile_element *)data)[x];


	for (pass = 0; pass < 3; pass++) {
		switch (pass)
		{
		case 0:
			printf("Sorted by \"tot_micro\":\n\n");
			qsort(p, flags, sizeof(struct profile_element *), sort_by_total);
			break;

		case 1:
			printf("\n\n\nSorted by \"called\":\n\n");
			qsort(p, flags, sizeof(struct profile_element *), sort_by_called);
			break;

		case 2:
			printf("\n\n\nSorted by \"max_micro\":\n\n");
			qsort(p, flags, sizeof(struct profile_element *), sort_by_max);
			break;
		}

		printf("%-5s%-20s%-10s%-10s%-10s%20s %s\n\n",
		       "#",
		       "called",
		       "min_micro",
		       "ave_micro",
		       "max_micro",
		       "tot_micro",
		       "function");

		for (x = 0; x < flags; x++) {
			name = df[p[x] - (struct profile_element *)data].name;

			printf("%-5u%-20"PRIu64"%-10"PRIu64"%-10u%-10"PRIu64"%20"PRIu64" %s\n",
			       x + 1,
			       p[x]->pe_total_calls,
			       (p[x]->pe_min_micros > p[x]->pe_max_micros) ? 0 : p[x]->pe_min_micros,
			       (unsigned int)((double)p[x]->pe_total_micros / p[x]->pe_total_calls + 0.5),
			       p[x]->pe_max_micros,
			       p[x]->pe_total_micros,
			       name);
		}
	}


	free(p);
	free(data);

	close(fd);
}

