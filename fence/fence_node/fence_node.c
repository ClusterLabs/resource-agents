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
#include <unistd.h>
#include <string.h>
#include <syslog.h>

#include "copyright.cf"
#include "ccs.h"

#define OPTION_STRING           ("hOuV")

#define die(fmt, args...) \
do \
{ \
  fprintf(stderr, "%s: ", prog_name); \
  fprintf(stderr, fmt "\n", ##args); \
  exit(EXIT_FAILURE); \
} \
while (0)

static char *prog_name;
static int force;

int dispatch_fence_agent(int cd, char *victim, int in);

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s [options] node_name\n", prog_name);
	printf("\n");
	printf("Options:\n");
	printf("\n");
	printf("  -h               Print this help, then exit\n");
	printf("  -O               Force connection to CCS\n");
	printf("  -V               Print program version information, then exit\n");
	printf("\n");
}

int main(int argc, char *argv[])
{
	int cont = 1, optchar, error, cd;
	char *victim = NULL;

	prog_name = argv[0];

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'O':
			force = 1;
			break;

		case 'V':
			printf("%s %s (built %s %s)\n", prog_name,
				FENCE_RELEASE_NAME, __DATE__, __TIME__);
			printf("%s\n", REDHAT_COPYRIGHT);
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
			die("unknown option: %c", optchar);
			break;
		};
	}

	while (optind < argc) {
		if (victim)
			die("unknown option %s", argv[optind]);
		victim = argv[optind];
		optind++;
	}

	if (!victim)
		die("no node name specified");

	if (force)
		cd = ccs_force_connect(NULL, 0);
	else
		cd = ccs_connect();

	openlog("fence_node", LOG_PID, LOG_USER);

	if (cd < 0) {
		syslog(LOG_ERR, "cannot connect to ccs %d\n", cd);
		goto fail;
	}

	error = dispatch_fence_agent(cd, victim, 0);
	if (error)
		goto fail_ccs;

	syslog(LOG_NOTICE, "Fence of \"%s\" was successful\n", victim);
	ccs_disconnect(cd);
	exit(EXIT_SUCCESS);

 fail_ccs:
	ccs_disconnect(cd);
 fail:
	syslog(LOG_ERR, "Fence of \"%s\" was unsuccessful\n", victim);
	exit(EXIT_FAILURE);
}

