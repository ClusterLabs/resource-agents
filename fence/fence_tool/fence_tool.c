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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>

#include "copyright.cf"

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define OPTION_STRING			("Vht:")
#define LOCKFILE_NAME                   "/var/run/fenced.pid"

#define OP_JOIN  			1
#define OP_LEAVE 			2

#define die(fmt, args...) \
do \
{ \
  fprintf(stderr, "%s: ", prog_name); \
  fprintf(stderr, fmt "\n", ##args); \
  exit(EXIT_FAILURE); \
} \
while (0)

char *prog_name;
int operation = 0;
int wait_time = 0;


static void lockfile(void)
{
	int fd, error;
	struct flock lock;

	fd = open(LOCKFILE_NAME, O_RDWR, 0);
	if (fd < 0)
		die("fenced not running - no %s", LOCKFILE_NAME);

	lock.l_type = F_WRLCK;
	lock.l_start = 0;
	lock.l_whence = SEEK_SET;
	lock.l_len = 0;

	error = fcntl(fd, F_SETLK, &lock);
	if (!error)
		die("fenced is not running");

	close(fd);
}

static void do_join(void)
{
	sleep(wait_time);
	execlp("fenced", "fenced", NULL);
	die("starting fenced failed");
}

static void do_leave(void)
{
	FILE *f;
	char buf[33] = "";
	int pid = 0;

	lockfile();

	f = fopen(LOCKFILE_NAME, "r");
	if (!f)
		die("fenced not running - no file %s", LOCKFILE_NAME);

	fgets(buf, 33, f);
	sscanf(buf, "%d", &pid);
	fclose(f);

	kill(pid, SIGTERM);
}

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s <join|leave> [options]\n", prog_name);
	printf("\n");
	printf("Options:\n");
	printf("\n");
	printf("  -t <seconds>     Delay before joining (starting fenced)\n");
	printf("  -V               Print program version information, then exit\n");
	printf("  -h               Print this help, then exit\n");
}

static void decode_arguments(int argc, char *argv[])
{
	int cont = TRUE;
	int optchar;

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {

		case 't':
			wait_time = atoi(optarg);
			break;

		case 'V':
			printf("fence_tool %s (built %s %s)\n",
			       FENCE_RELEASE_NAME, __DATE__, __TIME__);
			printf("%s\n", REDHAT_COPYRIGHT);
			exit(EXIT_SUCCESS);
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case ':':
		case '?':
			fprintf(stderr, "Please use '-h' for usage.\n");
			exit(EXIT_FAILURE);
			break;

		case EOF:
			cont = FALSE;
			break;

		default:
			die("unknown option: %c\n", optchar);
			break;
		}
	}

	while (optind < argc) {
		if (strcmp(argv[optind], "join") == 0) {
			operation = OP_JOIN;
		} else if (strcmp(argv[optind], "leave") == 0) {
			operation = OP_LEAVE;
		} else
			die("unknown option %s\n", argv[optind]);
		optind++;
	}

	if (!operation)
		die("no operation specified\n");
}

int main(int argc, char *argv[])
{
	prog_name = argv[0];

	decode_arguments(argc, argv);

	switch (operation) {
	case OP_JOIN:
		do_join();
		break;
	case OP_LEAVE:
		do_leave();
		break;
	}

	exit(EXIT_SUCCESS);
}
