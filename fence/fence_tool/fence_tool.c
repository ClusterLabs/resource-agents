/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2007 Red Hat, Inc.  All rights reserved.
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
#include "libcman.h"
#include "libgroup.h"

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define OPTION_STRING			("Vhcj:f:t:wQ")
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
int child_wait = FALSE;
int quorum_wait = TRUE;
int fenced_start_timeout = 300; /* five minutes */
int signalled = 0;
cman_handle_t ch;

static int get_int_arg(char argopt, char *arg)
{
	char *tmp;
	int val;                                                                                 
	val = strtol(arg, &tmp, 10);
	if (tmp == arg || tmp != arg + strlen(arg))
		die("argument to %c (%s) is not an integer", argopt, arg);
	
	if (val < 0)
		die("argument to %c cannot be negative", argopt);
	
	return val;
}

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
	return 0;
}

static void sigalarm_handler(int sig)
{
	signalled = 1;
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

static int we_are_in_fence_domain(void)
{
	group_data_t gdata;
	int rv;

	memset(&gdata, 0, sizeof(gdata));
	rv = group_get_group(0, "default", &gdata);

	if (rv || strcmp(gdata.client_name, "fence"))
		return 0;

	return gdata.member;
}

/*
 * We wait for the cluster to be quorate in this program because it's easy to
 * kill this program if we want to quit waiting.  If we just started fenced
 * without waiting for quorum, fenced's join would then wait for quorum in SM
 * but we can't kill/cancel it at that point -- we have to wait for it to
 * complete.
 *
 * A second reason to wait for quorum is that the unfencing step involves
 * cluster.conf lookups through ccs, but ccsd may wait for the cluster to be
 * quorate before responding to the lookups.  There wouldn't be a problem
 * blocking there per se, but it's cleaner I think to just wait here first.
 *
 * In the case where we're leaving, we want to wait for quorum because if we go
 * ahead and shut down fenced, the fence domain leave will block in SM where it
 * will wait for quorum before the leave can be processed.  We can't
 * kill/cancel the leave at that point, but we can if we're waiting here.
 *
 * Waiting here doesn't guarantee we won't end up blocking in SM on the join or
 * leave, but it avoids it in some common cases which can be helpful.  (Quorum
 * could easily be lost between the time we wait for it here and then begin the
 * join/leave process.)
 */

static int check_quorum(void)
{
	int rv = 0, i = 0;

	while (!signalled) {
		rv = cman_is_quorate(ch);
		if (rv)
			return TRUE;
		else if (!quorum_wait)
			return FALSE;

		sleep(1);

		if (!signalled && ++i > 9 && !(i % 10))
			printf("%s: waiting for cluster quorum\n", prog_name);
	}

	errno = ETIMEDOUT;
	return FALSE;
}

static int do_wait(int joining)
{
	int i;

	for (i=0; !fenced_start_timeout || i < fenced_start_timeout; i++) {
		if (we_are_in_fence_domain() == joining)
			return 0;
		if (!(i % 5))
			printf("Waiting for fenced to %s the fence group.\n",
				   (joining?"join":"leave"));
		sleep(1);
	}
	printf("Error joining the fence group.\n");
	return -1;
}

static int do_join(int argc, char *argv[])
{
	int i, fd, rv;
	char buf[MAXLINE];

	ch = cman_init(NULL);

	if (fenced_start_timeout) {
		signal(SIGALRM, sigalarm_handler);
		alarm(fenced_start_timeout);
	}

	if (!check_quorum()) {
		if (errno == ETIMEDOUT)
			printf("%s: Timed out waiting for cluster "
			       "quorum to form.\n", prog_name);
		cman_finish(ch);
		return EXIT_FAILURE;
	}
	cman_finish(ch);

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

	if (child_wait)
		do_wait(1);
	close(fd);
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
	if (child_wait)
		do_wait(0);
	close(fd);
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

	close(fd);
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
	printf("  -w               Wait for join to complete\n");
	printf("  -V               Print program version information, then exit\n");
	printf("  -h               Print this help, then exit\n");
	printf("  -t               Maximum time in seconds to wait\n");
	printf("  -Q               Fail if cluster is not quorate, don't wait\n");
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

		case 'Q':
			quorum_wait = FALSE;
			break;

		case 'w':
			child_wait = TRUE;
			break;

		case ':':
		case '?':
			fprintf(stderr, "Please use '-h' for usage.\n");
			exit(EXIT_FAILURE);
			break;

		case EOF:
			cont = FALSE;
			break;

		case 't':
			fenced_start_timeout = get_int_arg(optchar, optarg);
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
