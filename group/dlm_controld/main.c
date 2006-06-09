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

static int uevent_fd;
static int groupd_fd;
static int member_fd;

struct list_head lockspaces;

extern group_handle_t gh;


struct lockspace *create_ls(char *name)
{
	struct lockspace *ls;

	ls = malloc(sizeof(*ls));
	memset(ls, 0, sizeof(*ls));

	strncpy(ls->name, name, MAXNAME);

	return ls;
}

struct lockspace *find_ls(char *name)
{
	struct lockspace *ls;

	list_for_each_entry(ls, &lockspaces, list) {
		if ((strlen(ls->name) == strlen(name)) &&
		    !strncmp(ls->name, name, strlen(name)))
			return ls;
	}
	return NULL;
}

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

/* recv "online" (join) and "offline" (leave) 
   messages from dlm via uevents and pass them on to groupd */

int process_uevent(void)
{
	struct lockspace *ls;
	char buf[MAXLINE];
	char *argv[MAXARGS], *act, *sys;
	int rv, argc = 0;

	memset(buf, 0, sizeof(buf));

	rv = recv(uevent_fd, &buf, sizeof(buf), 0);
	if (rv < 0) {
		log_error("uevent recv error %d errno %d", rv, errno);
		goto out;
	}

	if (!strstr(buf, "dlm"))
		return 0;

	make_args(buf, &argc, argv, '/');
	act = argv[0];
	sys = argv[2];

	if ((strlen(sys) != strlen("dlm")) || strcmp(sys, "dlm"))
		return 0;

	log_debug("kernel: %s %s", act, argv[3]);

	if (!strcmp(act, "online@")) {
		ls = find_ls(argv[3]);
		if (ls) {
			rv = -EEXIST;
			goto out;
		}

		ls = create_ls(argv[3]);
		if (!ls) {
			rv = -ENOMEM;
			goto out;
		}

		ls->joining = 1;
		list_add(&ls->list, &lockspaces);

		rv = group_join(gh, argv[3]);

	} else if (!strcmp(act, "offline@")) {
		ls = find_ls(argv[3]);
		if (!ls) {
			rv = -ENOENT;
			goto out;
		}

		rv = group_leave(gh, argv[3]);
	} else
		rv = 0;
 out:
	if (rv < 0)
		log_error("process_uevent %s error %d errno %d",
			  act, rv, errno);
	return rv;
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
				if (pollfd[i].fd == member_fd) {
					log_error("cluster is down, exiting");
					exit(1);
				}
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
	openlog("dlm_controld", LOG_PID, LOG_DAEMON);

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
			daemon_debug_opt = 1;
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

	INIT_LIST_HEAD(&lockspaces);

	decode_arguments(argc, argv);

	if (!daemon_debug_opt)
		daemonize();

	return loop();
}

char *prog_name;
int daemon_debug_opt;
char daemon_debug_buf[256];

