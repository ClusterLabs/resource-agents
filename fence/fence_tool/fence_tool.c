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
#include <inttypes.h>
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
#include <mntent.h>
#include <libgen.h>

#include "cnxman-socket.h"
#include "ccs.h"
#include "copyright.cf"

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define OPTION_STRING			("VhScj:f:t:DwQ")
#define LOCKFILE_NAME                   "/var/run/fenced.pid"
#define FENCED_SOCK_PATH                "fenced_socket"

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
int debug;
int operation;
int child_wait;
int quorum_wait = TRUE;
int fenced_start_timeout = 0;
int signalled = 0;
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

static void sigalarm_handler(int sig)
{
	signalled = 1;
}

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

static int check_quorum(void)
{
	int rv, i = 0;

	if (debug)
		printf("%s: wait for quorum %d\n", prog_name, quorum_wait);

	while (!signalled) {
		rv = ioctl(cl_sock, SIOCCLUSTER_ISACTIVE, NULL);
		if (!rv)
			die("cluster is not active");

		rv = ioctl(cl_sock, SIOCCLUSTER_ISQUORATE, NULL);
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

/*
 * This is a really lousy way of waiting, which is why I took so long to add
 * it.  I guess it's better than nothing for a lot of people.  The state may
 * not be "run" if we've joined but other nodes are joining/leaving.
 */

static int do_wait(void)
{
	FILE *file;
	char line[256];
	int error, i = 0, starting = 0;

	file = fopen("/proc/cluster/services", "r");
	if (!file)
		return EXIT_FAILURE;

	while (!signalled) {
		memset(line, 0, 256);
		while (fgets(line, 256, file)) {
			if (strstr(line, "Fence Domain")) {
				if (strstr(line, "run")) {
					error = EXIT_SUCCESS;
					goto out;
				}
				starting = 1;
			}
		}

		if (++i > 9 && !(i % 10))
			printf("%s: waiting for fence domain run state\n",
			       prog_name);
		if (i == 10 && !starting) {
			printf("%s: fenced not starting\n", prog_name);
			return EXIT_FAILURE;
		}
		sleep(1);
		rewind(file);
	}
	errno = ETIMEDOUT;
	error = EXIT_FAILURE;
                        
 out:
	fclose(file);
	return error;
}

static int do_join(int argc, char *argv[])
{
	int cd;

	setup_sock();

       	if (fenced_start_timeout) {
               	signal(SIGALRM, sigalarm_handler);
               	alarm(fenced_start_timeout);
       	}

	if (!check_quorum()) {
		if (errno == ETIMEDOUT)
			printf("%s: Timed out waiting for cluster "
			       "quorum to form.\n", prog_name);
		return EXIT_FAILURE;
	}

	get_our_name();
	close(cl_sock);
	cd = check_ccs();
	ccs_disconnect(cd);

	/* Options for fenced can be given to this program which then passes
	   them on to fenced when it's started (now).  We just manipulate the
	   args for this program a bit before passing them on to fenced.  We
	   change the program name in argv[0] and remove the "join" which
	   getopt places as the last argv.

	   Fenced shouldn't barf if it gets any args specific to this program */

	if (debug)
		printf("%s: start fenced\n", prog_name);

	if (!debug && child_wait) {
		int status;
		pid_t pid = fork();

		/* parent waits for fenced to join */
		if (pid > 0) {
			waitpid(pid, &status, 0);
			if (WIFEXITED(status) &&
			    !WEXITSTATUS(status)) {
				if (do_wait() == EXIT_SUCCESS)
					exit(EXIT_SUCCESS);

				if (errno == ETIMEDOUT)
					printf("%s: Timed out waiting "
					       "for fenced to start.\n",
					       prog_name);
				return EXIT_FAILURE;
			}

			/* If we get here, the child died with a signal */
			if (WIFSIGNALED(status)) {
				printf("%s: child pid %d died with "
				       "signal %d\n", prog_name, (int)pid,
				       WTERMSIG(status));
			}

			return EXIT_FAILURE;
		} else if (pid < 0) {
			fprintf(stderr, "%s: fork(): %s\n",
				prog_name, strerror(errno));
		}
		/* child execs fenced */
	}

	strcpy(argv[0], "fenced");
	argv[argc - 1] = NULL;

	execvp("fenced", argv);
	die("starting fenced failed");

	return EXIT_FAILURE;
}

static int do_leave(void)
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

	if (!check_quorum())
		return EXIT_FAILURE;

	close(cl_sock);

	kill(pid, SIGTERM);

	return EXIT_SUCCESS;
}

static int do_monitor(void)
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

		case 'Q':
			quorum_wait = FALSE;
			break;

		case 'D':
			debug = TRUE;
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
		} else if (strcmp(argv[optind], "wait") == 0) {
			operation = OP_WAIT;
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
		return do_wait();
	}

	return EXIT_FAILURE;
}
