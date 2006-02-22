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

#include "lock_dlm.h"

#define OPTION_STRING			"DhV"
#define LOCKFILE_NAME			"/var/run/lock_dlmd.pid"

struct client {
	int fd;
	char type[32];
};

static int client_size = MAX_CLIENTS;
static struct client client[MAX_CLIENTS];
static struct pollfd pollfd[MAX_CLIENTS];

static int listen_fd;
static int groupd_fd;
static int uevent_fd;
static int libdlm_fd;
static int plocks_fd;

extern struct list_head mounts;

static void make_args(char *buf, int *argc, char **argv, char sep)
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

static int client_add(int fd, int *maxi)
{
	int i;

	for (i = 0; i < client_size; i++) {
		if (client[i].fd == -1) {
			client[i].fd = fd;
			pollfd[i].fd = fd;
			pollfd[i].events = POLLIN;
			if (i > *maxi)
				*maxi = i;
			/* log_debug("client %d fd %d added", i, fd); */
			return i;
		}
	}
	log_debug("client add failed");
	return -1;
}

static void client_dead(int ci)
{
	/* log_debug("client %d fd %d dead", ci, client[ci].fd); */
	close(client[ci].fd);
	client[ci].fd = -1;
	pollfd[ci].fd = -1;
}

static void client_init(void)
{
	int i;

	for (i = 0; i < client_size; i++)
		client[i].fd = -1;
}

int client_send(int ci, char *buf, int len)
{
	return write(client[ci].fd, buf, len);
}

static int process_client(int ci)
{
	char *cmd, *dir, *type, *proto, *table, *extra;
	char buf[MAXLINE], *argv[MAXARGS], out[MAXLINE];
	int argc = 0, rv;

	cmd = dir = type = proto = table = extra = NULL;
	memset(buf, 0, MAXLINE);
	memset(out, 0, MAXLINE);

	rv = read(client[ci].fd, buf, MAXLINE);
	if (!rv) {
		client_dead(ci);
		return 0;
	}
	if (rv < 0) {
		log_debug("client %d fd %d read error %d %d", ci,
			   client[ci].fd, rv, errno);
		return rv;
	}

	log_debug("client %d: %s", ci, buf);

	make_args(buf, &argc, argv, ' ');
	cmd = argv[0];
	dir = argv[1];
	type = argv[2];
	proto = argv[3];
	table = argv[4];
	extra = argv[5];

	if (!strcmp(cmd, "join"))
		rv = do_mount(ci, dir, type, proto, table, extra);
	else if (!strcmp(cmd, "leave"))
		rv = do_unmount(ci, dir);
	else
		rv = -EINVAL;

	sprintf(out, "%d", rv);
	rv = client_send(ci, out, MAXLINE);

	return rv;
}

static int setup_listen(void)
{
	struct sockaddr_un addr;
	socklen_t addrlen;
	int rv, s;

	/* we listen for new client connections on socket s */

	s = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (s < 0) {
		log_error("socket error %d %d", s, errno);
		return s;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_LOCAL;
	strcpy(&addr.sun_path[1], LOCK_DLM_SOCK_PATH);
	addrlen = sizeof(sa_family_t) + strlen(addr.sun_path+1) + 1;

	rv = bind(s, (struct sockaddr *) &addr, addrlen);
	if (rv < 0) {
		log_error("bind error %d %d", rv, errno);
		close(s);
		return rv;
	}

	rv = listen(s, 5);
	if (rv < 0) {
		log_error("listen error %d %d", rv, errno);
		close(s);
		return rv;
	}

	log_debug("listen %d", s);

	return s;
}

int process_uevent(void)
{
	char buf[MAXLINE];
	char *argv[MAXARGS], *act;
	int rv, argc = 0;

	memset(buf, 0, sizeof(buf));

	rv = recv(uevent_fd, &buf, sizeof(buf), 0);
	if (rv < 0) {
		log_error("uevent recv error %d errno %d", rv, errno);
		return -1;
	}

	if (!strstr(buf, "gfs") || !strstr(buf, "lock_module"))
		return 0;

	make_args(buf, &argc, argv, '/');
	act = argv[0];

	log_debug("kernel: %s %s", act, argv[3]);

	if (!strcmp(act, "change@"))
		do_recovery_done(argv[3]);

	else if (!strcmp(act, "offline@"))
		do_withdraw(argv[3]);

	return 0;
}

int setup_uevent(void)
{
	struct sockaddr_nl snl;
	int s, rv;

	s = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
	if (s < 0) {
		log_error("netlink socket error %d errno %d", s, errno);
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

	log_debug("uevent %d", s);

	return s;
}

int loop(void)
{
	int rv, i, f, maxi = 0;

	rv = setup_cman();
	if (rv < 0)
		goto out;

	rv = listen_fd = setup_listen();
	if (rv < 0)
		goto out;
	client_add(listen_fd, &maxi);

	rv = groupd_fd = setup_groupd();
	if (rv < 0)
		goto out;
	client_add(groupd_fd, &maxi);

	rv = uevent_fd = setup_uevent();
	if (rv < 0)
		goto out;
	client_add(uevent_fd, &maxi);

	rv = libdlm_fd = setup_libdlm();
	if (rv < 0)
		goto out;
	client_add(libdlm_fd, &maxi);

	rv = plocks_fd = setup_plocks();
	if (rv < 0)
		goto out;
	client_add(plocks_fd, &maxi);

	for (;;) {
		rv = poll(pollfd, maxi + 1, -1);
		if (rv < 0)
			log_error("poll error %d errno %d", rv, errno);

		/* client[0] is listening for new connections */

		if (pollfd[0].revents & POLLIN) {
			f = accept(client[0].fd, NULL, NULL);
			if (f < 0)
				log_debug("accept error %d %d", f, errno);
			else
				client_add(f, &maxi);
                }

		for (i = 1; i <= maxi; i++) {
			if (client[i].fd < 0)
				continue;

			if (pollfd[i].revents & POLLIN) {
				if (pollfd[i].fd == groupd_fd)
					process_groupd();
				else if (pollfd[i].fd == uevent_fd)
					process_uevent();
				else if (pollfd[i].fd == libdlm_fd)
					process_libdlm();
				else if (pollfd[i].fd == plocks_fd)
					process_plocks();
				else
					process_client(i);
			}

			if (pollfd[i].revents & POLLHUP) {
				log_debug("closing fd %d", pollfd[i].fd);
				close(pollfd[i].fd);
			}
		}
	}
	rv = 0;
 out:
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
		fprintf(stderr, "lock_dlmd is already running\n");
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
	openlog("lock_dlmd", LOG_PID, LOG_DAEMON);

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
			printf("lock_dlmd (built %s %s)\n", __DATE__, __TIME__);
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
	INIT_LIST_HEAD(&mounts);
	client_init();

	decode_arguments(argc, argv);

	if (!daemon_debug_opt)
		daemonize();

	return loop();
}

char *prog_name;
int daemon_debug_opt;
char daemon_debug_buf[256];

