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

#include "kdbl.h"

char *prog_name;

const char *usage[] = {
  "Print incore debug buffer [continuously]:\n",
  "  kdbl_tool dump [-c]\n",
  "Print out available tracing flags for a program:\n",
  "  kdbl_tool trprint <program>\n",
  "Turn a tracing flag on or off:\n",
  "  kdbl_tool [tron|troff] <program> <flags>\n",
  "Print profiling info:\n",
  "  kdbl_tool prof <program>\n",
  "Print version info:\n",
  "  kdbl_tool version\n",
  "\n",
  ""
};

/**
 * print_usage - print the program usage
 *
 */

void
print_usage()
{
	int x;
	for (x = 0; usage[x][0]; x++)
		printf(usage[x]);
}

/**
 * print_version - print the program version
 *
 */

static void
print_version(int argc, char **argv)
{
	printf("%s (built %s %s)\n",
	       prog_name,
	       __DATE__, __TIME__);
}

/**
 * main - the program main
 * @argc:
 * @argv:
 *
 * Returns:
 */

int
main(int argc,char *argv[])
{
	prog_name = argv[0];

	if (argc < 2) {
		print_usage();
		exit(EXIT_SUCCESS);
	}

	if (FALSE) {
		/*  Do Nothing  */
	} else if (strcmp(argv[1], "dump") == 0)
		printf_dump(argc, argv);
	else if (strcmp(argv[1], "trprint") == 0)
		trace_print(argc, argv);
	else if (strcmp(argv[1], "tron") == 0 ||
		 strcmp(argv[1], "troff") == 0)
		trace_change(argc, argv);
	else if (strcmp(argv[1], "prof") == 0)
		profile_dump(argc, argv);
	else if (strcmp(argv[1], "version") == 0 ||
		 strcmp(argv[1], "-V") == 0)
		print_version(argc, argv);
	else if (strcmp(argv[1], "-h") == 0 ||
		 strcmp(argv[1], "--help") == 0)
		print_usage();
	else
		die("%s: invalid option -- %s\nPlease use '-h' for usage.\n", 
		    argv[0], argv[1]);

	exit(EXIT_SUCCESS);
}

