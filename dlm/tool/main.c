/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2007 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <sys/types.h>
#include <sys/un.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>

#include "libdlm.h"

#define OPTION_STRING			"hVvd:"

#define OP_JOIN				1
#define OP_LEAVE			2
#define OP_JOINLEAVE			3

static char *prog_name;
static char *lsname;
static int operation;
static int opt_ind;
static int verbose;
static int opt_dir = 0;

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s [options] [join|leave]\n", prog_name);
	printf("\n");
	printf("Options:\n");
	printf("  -v               Verbose output, extra event information\n");
	printf("  -h               Print this help, then exit\n");
	printf("  -V               Print program version information, then exit\n");
	printf("  -d <n>           Resource directory off/on (0/1), default 0\n");
	printf("\n");
}

static void decode_arguments(int argc, char **argv)
{
	int cont = 1;
	int optchar;

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {
		case 'v':
			verbose = 1;
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'd':
			opt_dir = atoi(optarg);
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

	while (optind < argc) {
		if (!strncmp(argv[optind], "join", 4) &&
		    (strlen(argv[optind]) == 4)) {
			operation = OP_JOIN;
			opt_ind = optind + 1;
			break;
		} else if (!strncmp(argv[optind], "leave", 5) &&
			   (strlen(argv[optind]) == 5)) {
			operation = OP_LEAVE;
			opt_ind = optind + 1;
			break;
		} else if (!strncmp(argv[optind], "joinleave", 9) &&
			   (strlen(argv[optind]) == 9)) {
			operation = OP_JOINLEAVE;
			opt_ind = optind + 1;
			break;
		}
		optind++;
	}

	if (!operation || !opt_ind) {
		print_usage();
		exit(EXIT_FAILURE);
	}

	if (optind < argc - 1)
		lsname = argv[opt_ind];
	else {
		fprintf(stderr, "lockspace name required\n");
		exit(EXIT_FAILURE);
	}
}

void do_join(char *name)
{
	dlm_lshandle_t *dh;

	printf("Joining lockspace \"%s\"\n", name);
	fflush(stdout);

	dh = dlm_new_lockspace(name, 0600, DLM_LSFL_NODIR);
	if (!dh) {
		fprintf(stderr, "dlm_create_lockspace %s error %lu %d\n",
			name, (unsigned long) dh, errno);
		exit(-1);
	}

	dlm_close_lockspace(dh);
	/* there's no autofree so the ls should stay around */
	printf("done\n");
}

void do_leave(char *name)
{
	dlm_lshandle_t *dh;

	printf("Leaving lockspace \"%s\"\n", name);
	fflush(stdout);

	dh = dlm_open_lockspace(name);
	if (!dh) {
		fprintf(stderr, "dlm_open_lockspace %s error %lu %d\n",
			name, (unsigned long) dh, errno);
		exit(-1);
	}

	dlm_release_lockspace(name, dh, 1);
	printf("done\n");
}

int main(int argc, char **argv)
{
	prog_name = argv[0];
	decode_arguments(argc, argv);
	/* check_name(lsname); */

	switch (operation) {
	case OP_JOIN:
		do_join(lsname);
		break;

	case OP_LEAVE:
		do_leave(lsname);
		break;

	case OP_JOINLEAVE:
		do_join(lsname);
		do_leave(lsname);
		break;
	}

	return 0;
}

