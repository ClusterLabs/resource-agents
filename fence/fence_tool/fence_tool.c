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
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>
#include <mntent.h>
#include <libgen.h>

#include "cnxman-socket.h"
#include "ccs.h"
#include "copyright.cf"

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define OPTION_STRING			("VhScj:f:D")
#define LOCKFILE_NAME                   "/var/run/fenced.pid"
#define FENCED_SOCK_PATH                "fenced_socket"

#define OP_JOIN  			1
#define OP_LEAVE 			2
#define OP_MONITOR			3


#define die(fmt, args...) \
do \
{ \
  fprintf(stderr, "%s: ", prog_name); \
  fprintf(stderr, fmt "\n", ##args); \
  exit(EXIT_FAILURE); \
} \
while (0)

char *prog_name;
int debug;
int operation;
int skip_unfence;
int cl_sock;
char our_name[MAX_CLUSTER_MEMBER_NAME_LEN+1];

int dispatch_fence_agent(int cd, char *victim, int in);


static int check_mounted(void)
{
	FILE *fp;
	struct mntent *me;

	fp = setmntent("/etc/mtab", "r");

	for (;;) {
		me = getmntent(fp);
		if (!me)
			break;

		if (!strcmp(me->mnt_type, "gfs"))
			die("cannot leave, gfs mounted on %s",
			    me->mnt_fsname);
	}
	return 0;
}

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

static int setup_sock(void)
{
	cl_sock = socket(AF_CLUSTER, SOCK_DGRAM, CLPROTO_CLIENT);
	if (cl_sock < 0)
		die("cannot create cluster socket %d", cl_sock);

	return 0;
}

static int self_unfence(int cd)
{
	if (skip_unfence)
		return 0;

	if (debug)
		printf("%s: unfence ourself\n", prog_name);

	dispatch_fence_agent(cd, our_name, 1);
	return 0;
}

static int check_ccs(void)
{
	int i = 0, cd;

	if (debug)
		printf("%s: connect to ccs\n", prog_name);

	while ((cd = ccs_connect()) < 0) {
		printf("%s: waiting for ccs connection %d\n", prog_name, cd);
		sleep(1);
		if (++i == 10)
			die("cannot connect to ccs %d\n", cd);
	}

	return cd;
}

static int get_our_name(void)
{
	struct cl_cluster_node cl_node;
	int rv;

	if (debug)
		printf("%s: get our node name\n", prog_name);

	memset(&cl_node, 0, sizeof(struct cl_cluster_node));

	for (;;) {
		rv = ioctl(cl_sock, SIOCCLUSTER_GETNODE, &cl_node);
		if (!rv)
			break;
		printf("%s: retrying cman getnode %d\n", prog_name, rv);
		sleep(1);
	}

	memcpy(our_name, cl_node.name, strlen(cl_node.name));
	return 0;
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

static int wait_quorum(void)
{
	int rv, i = 0;

	if (debug)
		printf("%s: wait for quorum\n", prog_name);

	while (1) {
		rv = ioctl(cl_sock, SIOCCLUSTER_ISACTIVE, NULL);
		if (!rv)
			die("cluster is not active");

		rv = ioctl(cl_sock, SIOCCLUSTER_ISQUORATE, NULL);
		if (rv)
			break;

		sleep(1);

		if (++i > 9 && !(i % 10))
			printf("%s: waiting for cluster quorum\n", prog_name);
	}

	return 0;
}

static void do_join(int argc, char *argv[])
{
	int cd;

	setup_sock();
	wait_quorum();
	get_our_name();
	close(cl_sock);
	cd = check_ccs();
	self_unfence(cd);
	ccs_disconnect(cd);

	/* Options for fenced can be given to this program which then passes
	   them on to fenced when it's started (now).  We just manipulate the
	   args for this program a bit before passing them on to fenced.  We
	   change the program name in argv[0] and remove the "join" which
	   getopt places as the last argv.

	   Fenced shouldn't barf if it gets any args specific to this program */

	if (debug)
		printf("%s: start fenced\n", prog_name);

	strcpy(argv[0], "fenced");
	argv[argc - 1] = NULL;

	execvp("fenced", argv);
	die("starting fenced failed");
}

static void do_leave(void)
{
	FILE *f;
	char buf[33] = "";
	int pid = 0;

	lockfile();

	/* get the pid of fenced so we can kill it */

	f = fopen(LOCKFILE_NAME, "r");
	if (!f)
		die("fenced not running - no file %s", LOCKFILE_NAME);
	fgets(buf, 33, f);
	sscanf(buf, "%d", &pid);
	fclose(f);

	check_mounted();
	setup_sock();
	wait_quorum();
	close(cl_sock);

	kill(pid, SIGTERM);
}

static void do_monitor(void)
{
	int sfd, error, rv;
	struct sockaddr_un addr;
	socklen_t addrlen;
	char buf[256];

	sfd = socket(AF_LOCAL, SOCK_DGRAM, 0);
	if (sfd < 0)
		die("cannot create local socket");

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_LOCAL;
	strcpy(&addr.sun_path[1], FENCED_SOCK_PATH);
	addrlen = sizeof(sa_family_t) + strlen(addr.sun_path+1) + 1;

	error = bind(sfd, (struct sockaddr *) &addr, addrlen);
	if (error < 0)
		die("cannot bind to local socket");

	for (;;) {
		memset(buf, 0, 256);

		rv = recvfrom(sfd, buf, 256, 0, (struct sockaddr *)&addr,
			      &addrlen);

		printf("%s", buf);
	}
}

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s <join|leave> [options]\n", prog_name);
	printf("\n");
	printf("Options:\n");
	printf("\n");
	printf("  -V               Print program version information, then exit\n");
	printf("  -h               Print this help, then exit\n");
	printf("  -S               Skip self unfencing on join\n");
	printf("  -D               Enable debugging, don't fork (also passed to fenced)\n");
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

		case 'S':
			skip_unfence = TRUE;
			break;

		case 'D':
			debug = TRUE;
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
		do_join(argc, argv);
		break;
	case OP_LEAVE:
		do_leave();
		break;
	case OP_MONITOR:
		do_monitor();
		break;
	}

	exit(EXIT_SUCCESS);
}
