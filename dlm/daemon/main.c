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

#include "dlm_daemon.h"

#define OPTION_STRING			"DhV"
#define LOCKFILE_NAME			"/var/run/dlm_controld.pid"
#define MAXCON				4

static char *prog_name;
static int debug;

static int uevent_fd;
static int groupd_fd;
static int member_fd;

extern group_handle_t gh;

void make_args(char *buf, int *argc, char **argv, char sep)
{
	char *p = buf;
	int i;

	argv[0] = p;

	for (i = 1; i < MAXARGS; i++) {
		p = strchr(buf, sep);
		if (!p)
			break;
		*p = '\0';
		argv[i] = p + 1;
		buf = p + 1;
	}
	*argc = i;
}

void get_done_event(char *name, int *event_nr)
{
	char *argv[] = { name };
	int rv;

	rv = ls_get_done(1, argv, event_nr);
	if (rv)
		log_error("ls get_done error %d", rv);
}

/* recv "online" (join), "offline" (leave) and "change" (startdone)
   messages from dlm via uevents and pass them on to groupd */

int process_uevent(void)
{
	char buf[MAXLINE];
	char *argv[MAXARGS], *act, *sys;
	int rv, argc = 0;

	memset(buf, 0, sizeof(buf));

	rv = recv(uevent_fd, &buf, sizeof(buf), 0);
	if (rv < 0) {
		log_error("recv error %d errno %d", rv, errno);
		return -1;
	}

	if (!strstr(buf, "dlm"))
		return 0;

	make_args(buf, &argc, argv, '/');

	act = argv[0];
	sys = argv[2];

	if ((strlen(sys) != strlen("dlm")) || strcmp(sys, "dlm"))
		return 0;

	log_debug("I: uevent recv:  %s", buf);

	if (!strcmp(act, "online@"))
		rv = group_join(gh, argv[3], NULL);

	else if (!strcmp(act, "offline@"))
		rv = group_leave(gh, argv[3], NULL);

	else if (!strcmp(act, "change@")) {
		int event_nr = 0;
		get_done_event(argv[3], &event_nr);
		rv = group_done(gh, argv[3], event_nr);

	} else
		goto out;

	if (rv < 0)
		log_error("libgroup %s error %d errno %d", act, rv, errno);
 out:
	return 0;
}

int setup_uevent(void)
{
	struct sockaddr_nl snl;
	int s, rv;

	s = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
	if (s < 0) {
		log_error("netlink socket");
		return s;
	}

	memset(&snl, 0, sizeof(snl));
	snl.nl_family = AF_NETLINK;
	snl.nl_pid = getpid();
	snl.nl_groups = 1;

	rv = bind(s, (struct sockaddr *) &snl, sizeof(snl));
	if (rv < 0) {
		log_error("uevent bind error %d errno %d", rv, errno);
		close(s);
		return rv;
	}

	return s;
}

int loop(void)
{
	struct pollfd *pollfd;
	int rv, i, maxi;

	pollfd = malloc(MAXCON * sizeof(struct pollfd));
	if (!pollfd)
		return -1;

	rv = groupd_fd = setup_groupd();
	if (rv < 0)
		goto out;
	pollfd[0].fd = groupd_fd;
	pollfd[0].events = POLLIN;

	rv = uevent_fd = setup_uevent();
	if (rv < 0)
		goto out;
	pollfd[1].fd = uevent_fd;
	pollfd[1].events = POLLIN;

	rv = member_fd = setup_member();
	if (rv < 0)
		goto out;
	pollfd[2].fd = member_fd;
	pollfd[2].events = POLLIN;

	maxi = 2;

	log_debug("groupd_fd %d uevent_fd %d member_fd %d",
		   groupd_fd, uevent_fd, member_fd);

	for (;;) {
		rv = poll(pollfd, maxi + 1, -1);
		if (rv < 0) {
			log_error("poll");
			goto out;
		}

		for (i = 0; i <= maxi; i++) {
			if (pollfd[i].revents & POLLIN) {
				if (pollfd[i].fd == groupd_fd)
					process_groupd();
				else if (pollfd[i].fd == uevent_fd)
					process_uevent();
				else if (pollfd[i].fd == member_fd)
					process_member();
			}

			if (pollfd[i].revents & POLLHUP) {
				log_error("closing fd %d", pollfd[i].fd);
				close(pollfd[i].fd);
			}
		}
	}
	rv = 0;
 out:
	free(pollfd);
	return rv;
}

static void lockfile(void)
{
	int fd, error;
	struct flock lock;
	char buf[33];

	memset(buf, 0, 33);

	fd = open(LOCKFILE_NAME, O_CREAT|O_WRONLY,
		  S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (fd < 0) {
		fprintf(stderr, "cannot open/create lock file %s\n",
			LOCKFILE_NAME);
		exit(EXIT_FAILURE);
	}

	lock.l_type = F_WRLCK;
	lock.l_start = 0;
	lock.l_whence = SEEK_SET;
	lock.l_len = 0;

	error = fcntl(fd, F_SETLK, &lock);
	if (error) {
		fprintf(stderr, "dlm_controld is already running\n");
		exit(EXIT_FAILURE);
	}

	error = ftruncate(fd, 0);
	if (error) {
		fprintf(stderr, "cannot clear lock file %s\n", LOCKFILE_NAME);
		exit(EXIT_FAILURE);
	}

	sprintf(buf, "%d\n", getpid());

	error = write(fd, buf, strlen(buf));
	if (error <= 0) {
		fprintf(stderr, "cannot write lock file %s\n", LOCKFILE_NAME);
		exit(EXIT_FAILURE);
	}
}

void daemonize(void)
{
	pid_t pid = fork();
	if (pid < 0) {
		perror("main: cannot fork");
		exit(EXIT_FAILURE);
	}
	if (pid)
		exit(EXIT_SUCCESS);
	setsid();
	chdir("/");
	umask(0);
	close(0);
	close(1);
	close(2);

	lockfile();
}

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s [options]\n", prog_name);
	printf("\n");
	printf("Options:\n");
	printf("\n");
	printf("  -D               Enable debugging code and don't fork\n");
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

		case 'D':
			debug = 1;
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'V':
			printf("dlm_controld (built %s %s)\n", __DATE__, __TIME__);
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
	prog_name = argv[0];

	decode_arguments(argc, argv);

	if (!debug)
		daemonize();

	return loop();
}

