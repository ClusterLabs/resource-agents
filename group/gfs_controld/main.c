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

#define OPTION_STRING			"DPhVw"
#define LOCKFILE_NAME			"/var/run/gfs_controld.pid"

struct client {
	int fd;
	char type[32];
};

static int client_size = 0;
static struct client *client = NULL;
static struct pollfd *pollfd = NULL;

static int cman_fd;
static int cpg_fd;
static int listen_fd;
static int groupd_fd;
static int uevent_fd;
static int plocks_fd;

extern struct list_head mounts;
extern struct list_head withdrawn_mounts;
int no_withdraw;

#if 0
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
#endif

static char *get_args(char *buf, int *argc, char **argv, char sep, int want)
{
	char *p = buf, *rp = NULL;
	int i;

	argv[0] = p;

	for (i = 1; i < MAXARGS; i++) {
		p = strchr(buf, sep);
		if (!p)
			break;
		*p = '\0';

		if (want == i) { 
			rp = p + 1;
			break;
		}

		argv[i] = p + 1;
		buf = p + 1;
	}
	*argc = i;

	/* we ended by hitting \0, return the point following that */
	if (!rp)
		rp = strchr(buf, '\0') + 1;

	return rp;
}

static int client_add(int fd, int *maxi)
{
	int i;

	while (1) {
		/* This fails the first time with client_size of zero */
		for (i = 0; i < client_size; i++) {
			if (client[i].fd == -1) {
				client[i].fd = fd;
				pollfd[i].fd = fd;
				pollfd[i].events = POLLIN;
				if (i > *maxi)
					*maxi = i;
				return i;
			}
		}

		/* We didn't find an empty slot, so allocate more. */
		client_size += MAX_CLIENTS;

		if (!client) {
			client = malloc(client_size * sizeof(struct client));
			pollfd = malloc(client_size * sizeof(struct pollfd));
		} else {
			client = realloc(client, client_size *
						 sizeof(struct client));
			pollfd = realloc(pollfd, client_size *
						 sizeof(struct pollfd));
		}
		if (!client || !pollfd)
			log_error("Can't allocate client memory.");

		for (i = client_size - MAX_CLIENTS; i < client_size; i++) {
			client[i].fd = -1;
			pollfd[i].fd = -1;
		}
	}
}

static void client_dead(int ci)
{
	/* log_debug("client %d fd %d dead", ci, client[ci].fd); */
	close(client[ci].fd);
	client[ci].fd = -1;
	pollfd[ci].fd = -1;
}

int client_send(int ci, char *buf, int len)
{
	return write(client[ci].fd, buf, len);
}

static int dump_debug(int ci)
{
	int rv, len;

	if (dump_wrap) {
		len = DUMP_SIZE - dump_point;
		rv = write(client[ci].fd, dump_buf + dump_point, len);
		if (rv != len)
			log_debug("write error %d errno %d", rv, errno);
	}
	len = dump_point;

	rv = write(client[ci].fd, dump_buf, len);
	if (rv != len)
		log_debug("write error %d errno %d", rv, errno);
	return 0;
}

/* mount.gfs sends us a special fd that it will write an error message to
   if mount(2) fails.  We can monitor this fd for an error message while
   waiting for the kernel mount outside our main poll loop */

void setup_mount_error_fd(struct mountgroup *mg)
{
	struct msghdr msg;
	struct cmsghdr *cmsg;
	struct iovec vec;
	char tmp[CMSG_SPACE(sizeof(int))];
	int fd, socket = client[mg->mount_client].fd;
	char ch;
	ssize_t n;

	memset(&msg, 0, sizeof(msg));

	vec.iov_base = &ch;
	vec.iov_len = 1;
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;
	msg.msg_control = tmp;
	msg.msg_controllen = sizeof(tmp);

	n = recvmsg(socket, &msg, 0);
	if (n < 0) {
		log_group(mg, "setup_mount_error_fd recvmsg err %d errno %d",
			  n, errno);
		return;
	}
	if (n != 1) {
		log_group(mg, "setup_mount_error_fd recvmsg got %ld", (long)n);
		return;
	}

	cmsg = CMSG_FIRSTHDR(&msg);

	if (cmsg->cmsg_type != SCM_RIGHTS) {
		log_group(mg, "setup_mount_error_fd expected type %d got %d",
			  SCM_RIGHTS, cmsg->cmsg_type);
		return;
	}

	fd = (*(int *)CMSG_DATA(cmsg));
	mg->mount_error_fd = fd;

	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

	log_group(mg, "setup_mount_error_fd got fd %d", fd);
}

static int process_client(int ci)
{
	char *cmd, *dir, *type, *proto, *table, *extra;
	char buf[MAXLINE], *argv[MAXARGS], out[MAXLINE];
	int argc = 0, rv;

	cmd = dir = type = proto = table = extra = NULL;
	memset(buf, 0, MAXLINE);
	memset(out, 0, MAXLINE);
	memset(argv, 0, sizeof(char *) * MAXARGS);

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

	get_args(buf, &argc, argv, ' ', 6);
	cmd = argv[0];
	dir = argv[1];
	type = argv[2];
	proto = argv[3];
	table = argv[4];
	extra = argv[5];

	if (!strcmp(cmd, "join"))
		rv = do_mount(ci, dir, type, proto, table, extra);
	else if (!strcmp(cmd, "leave"))
		rv = do_unmount(ci, dir, atoi(proto));
	else if (!strcmp(cmd, "remount"))
		rv = do_remount(ci, dir, argv[3]);
	else if (!strcmp(cmd, "dump")) {
		dump_debug(ci);
		return 0;
	} else if (!strcmp(cmd, "plocks")) {
		dump_plocks(dir, client[ci].fd);
		client_dead(ci);
		return 0;
	} else
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
	memset(argv, 0, sizeof(char *) * MAXARGS);

	rv = recv(uevent_fd, &buf, sizeof(buf), 0);
	if (rv < 0) {
		log_error("uevent recv error %d errno %d", rv, errno);
		return -1;
	}

	if (!strstr(buf, "gfs") || !strstr(buf, "lock_module"))
		return 0;

	get_args(buf, &argc, argv, '/', 4);
	if (argc != 4)
		log_error("uevent message has %d args", argc);
	act = argv[0];

	log_debug("kernel: %s %s", act, argv[3]);

	if (!strcmp(act, "change@"))
		kernel_recovery_done(argv[3]);
	else if (!strcmp(act, "offline@"))
		do_withdraw(argv[3]);
	else
		ping_kernel_mount(argv[3]);

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

	rv = listen_fd = setup_listen();
	if (rv < 0)
		goto out;
	client_add(listen_fd, &maxi);

	rv = cman_fd = setup_cman();
	if (rv < 0)
		goto out;
	client_add(cman_fd, &maxi);

	rv = cpg_fd = setup_cpg();
	if (rv < 0)
		goto out;
	client_add(cpg_fd, &maxi);

	rv = groupd_fd = setup_groupd();
	if (rv < 0)
		goto out;
	client_add(groupd_fd, &maxi);

	rv = uevent_fd = setup_uevent();
	if (rv < 0)
		goto out;
	client_add(uevent_fd, &maxi);

	rv = plocks_fd = setup_plocks();
	if (rv < 0)
		goto out;
	client_add(plocks_fd, &maxi);

	log_debug("setup done");

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
				else if (pollfd[i].fd == cman_fd)
					process_cman();
				else if (pollfd[i].fd == cpg_fd)
					process_cpg();
				else if (pollfd[i].fd == uevent_fd)
					process_uevent();
				else if (pollfd[i].fd == plocks_fd)
					process_plocks();
				else
					process_client(i);
			}

			if (pollfd[i].revents & POLLHUP) {
				if (pollfd[i].fd == cman_fd) {
					log_error("cman connection died");
					exit_cman();
				} else if (pollfd[i].fd == groupd_fd) {
					log_error("groupd connection died");
					exit_cman();
				} else if (pollfd[i].fd == cpg_fd) {
					log_error("cpg connection died");
					exit_cman();
				}
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
		fprintf(stderr, "gfs_controld is already running\n");
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
	openlog("gfs_controld", LOG_PID, LOG_DAEMON);

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
	printf("  -D	       Enable debugging code and don't fork\n");
	printf("  -h	       Print this help, then exit\n");
	printf("  -V	       Print program version information, then exit\n");
}

static void decode_arguments(int argc, char **argv)
{
	int cont = 1;
	int optchar;

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {

		case 'w':
			no_withdraw = 1;
			break;

		case 'D':
			daemon_debug_opt = 1;
			break;

		case 'P':
			plock_debug_opt = 1;
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'V':
			printf("gfs_controld (built %s %s)\n", __DATE__, __TIME__);
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
	INIT_LIST_HEAD(&withdrawn_mounts);

	decode_arguments(argc, argv);

	if (!daemon_debug_opt)
		daemonize();

	return loop();
}

void daemon_dump_save(void)
{
	int len, i;

	len = strlen(daemon_debug_buf);

	for (i = 0; i < len; i++) {
		dump_buf[dump_point++] = daemon_debug_buf[i];

		if (dump_point == DUMP_SIZE) {
			dump_point = 0;
			dump_wrap = 1;
		}
	}
}

char *prog_name;
int plock_debug_opt;
int daemon_debug_opt;
char daemon_debug_buf[256];
char dump_buf[DUMP_SIZE];
int dump_point;
int dump_wrap;

