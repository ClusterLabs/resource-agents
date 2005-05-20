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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <string.h>

#include "libgroup.h"

#define MAX_GROUPS			64
#define OPTION_STRING			"hV"

static char *prog_name;

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s [options]\n", prog_name);
	printf("\n");
	printf("Options:\n");
	printf("\n");
	printf("  -h               Print this help, then exit\n");
	printf("  -V               Print program version information, then exit\n");
}

static void decode_arguments(int argc, char **argv)
{
	int cont = 1;
	int optchar;

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'V':
			printf("%s (built %s %s)\n",
				prog_name, __DATE__, __TIME__);
			/* printf("%s\n", REDHAT_COPYRIGHT); */
			exit(EXIT_SUCCESS);
			break;

		case ':':
		case '?':
			fprintf(stderr, "Please use '-h' for usage.\n");
			exit(EXIT_FAILURE);
			break;

		case EOF:
			cont = 0;
			break;

		default:
			fprintf(stderr, "unknown option: %c\n", optchar);
			exit(EXIT_FAILURE);
			break;
		};
	}
}

int main(int argc, char **argv)
{
	group_data_t data[MAX_GROUPS];
	int i, rv, count = 0;

	prog_name = argv[0];
	decode_arguments(argc, argv);

	memset(&data, 0, sizeof(data));

	rv = group_get_groups(MAX_GROUPS, &count, data);

	printf("group count %d\n", count);

	for (i = 0; i < count; i++) {
		printf("client name %s\n", data[i].client_name);
		printf("group name  %s\n", data[i].name);
		printf("level       %d\n", data[i].level);
		printf("members     %d\n", data[i].member_count);
	}

	return 0;
}

