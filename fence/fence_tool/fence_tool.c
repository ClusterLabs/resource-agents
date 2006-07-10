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
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>
#include <libgen.h>

#include "ccs.h"
#include "copyright.cf"

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define OPTION_STRING			("Vhcj:f:")
#define FENCED_SOCK_PATH                "fenced_socket"
#define MAXLINE				256

#define OP_JOIN  			1
#define OP_LEAVE 			2
#define OP_MONITOR			3
#define OP_WAIT				4

#define die(fmt, args...) \
do \
{ \
  fprintf(stderr, "%s: ", prog_name); \
  fprintf(stderr, fmt "\n", ##args); \
  exit(EXIT_FAILURE); \
} \
while (0)

char *prog_name;
int operation;

int dispatch_fence_agent(int cd, char *victim, char *self);

static int check_mounted(void)
{
	FILE *file;
	char line[PATH_MAX];
	char device[PATH_MAX];
	char path[PATH_MAX];
	char type[PATH_MAX];

	file = fopen("/proc/mounts", "r");
	if (!file)
		return 0;

	while (fgets(line, PATH_MAX, file)) {
		if (sscanf(line, "%s %s %s", device, path, type) != 3)
			continue;
		if (!strcmp(type, "gfs") || !strcmp(type, "gfs2"))
			die("cannot leave, %s file system mounted from %s on %s",
			    type, device, path);
	}

	fclose(file);
}

int fenced_connect(void)
{
	struct sockaddr_un sun;
	socklen_t addrlen;
	int rv, fd;

	fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		goto out;

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strcpy(&sun.sun_path[1], FENCED_SOCK_PATH);
	addrlen = sizeof(sa_family_t) + strlen(sun.sun_path+1) + 1;

	rv = connect(fd, (struct sockaddr *) &sun, addrlen);
	if (rv < 0) {
		close(fd);
		fd = rv;
	}
 out:
	return fd;
}

static int do_join(int argc, char *argv[])
{
	int i, fd, rv;
	char buf[MAXLINE];

	i = 0;
	do {
		sleep(1);
		fd = fenced_connect();
		if (!fd)
			fprintf(stderr, "waiting for fenced to start\n");
	} while (!fd && ++i < 10);

	if (!fd)
		die("fenced not running");

	memset(buf, 0, sizeof(buf));
	sprintf(buf, "join default");

	rv = write(fd, buf, sizeof(buf));
	if (rv < 0)
		die("can't communicate with fenced %d", rv);

	memset(buf, 0, sizeof(buf));
	rv = read(fd, buf, sizeof(buf));

	/* printf("join result %d %s\n", rv, buf); */
	return EXIT_SUCCESS;
}

static int do_leave(void)
{
	int fd, rv;
	char buf[MAXLINE];

	fd = fenced_connect();
	if (!fd)
		die("fenced not running");

	check_mounted();


	memset(buf, 0, sizeof(buf));
	sprintf(buf, "leave default");

	rv = write(fd, buf, sizeof(buf));
	if (rv < 0)
		die("can't communicate with fenced");

	memset(buf, 0, sizeof(buf));
	rv = read(fd, buf, sizeof(buf));

	/* printf("leave result %d %s\n", rv, buf); */
	return EXIT_SUCCESS;
}

static int do_monitor(void)
{
	int fd, rv;
	char *out, buf[256];

	fd = fenced_connect();
	if (!fd)
		die("fenced not running");

	out = "monitor";

	rv = write(fd, out, sizeof(out));
	if (rv < 0)
		die("can't communicate with fenced");

	while (1) {
		memset(buf, 0, sizeof(buf));
		rv = read(fd, buf, sizeof(buf));
		printf("%s", buf);
	}

	return EXIT_SUCCESS;
}

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s <join|leave|wait> [options]\n", prog_name);
	printf("\n");
	printf("Actions:\n");
	printf("  join             Join the default fence domain\n");
	printf("  leave            Leave default fence domain\n");
	printf("  wait             Wait for node to be member of default fence domain\n");
	printf("\n");
	printf("Options:\n");
	printf("  -V               Print program version information, then exit\n");
	printf("  -h               Print this help, then exit\n");
	printf("\n");
	printf("Fenced options:\n");
	printf("  these are passed on to fenced when it's started\n");
	printf("  -c               All nodes are in a clean state to start\n");
	printf("  -j <secs>        Post-join fencing delay\n");
	printf("  -f <secs>        Post-fail fencing delay\n");
	printf("\n");
}

static void decode_arguments(int argc, char *argv[])
{
	int cont = TRUE;
	int optchar;

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {

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

		case 'c':
		case 'j':
		case 'f':
			/* Do nothing, just pass these options on to fenced */
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
		} else if (strcmp(argv[optind], "monitor") == 0) {
			operation = OP_MONITOR;
		} else
			die("unknown option %s\n", argv[optind]);
		optind++;
	}

	if (!operation)
		die("no operation specified\n");
}

int main(int argc, char *argv[])
{
	prog_name = basename(argv[0]);

	decode_arguments(argc, argv);

	switch (operation) {
	case OP_JOIN:
		return do_join(argc, argv);
	case OP_LEAVE:
		return do_leave();
	case OP_MONITOR:
		return do_monitor();
	case OP_WAIT:
		return -1;
	}

	return EXIT_FAILURE;
}
