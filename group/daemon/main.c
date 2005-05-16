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

#include "gd_internal.h"

#define OPTION_STRING			"DhV"
#define LOCKFILE_NAME			"/var/run/groupd.pid"

static int client_size;
static struct client *client;
static struct pollfd *pollfd;
static char *prog_name;
static int debug;

/*
 * gd_nodes
 * List of all nodes who have been in cluster.
 * Those who are members have NFL_CLUSTER_MEMBER set.
 * This assumes a nodeid is permanently attached to a particular
 * node for the life of the cluster.
 *
 * gd_node_count
 * number of nodes in gd_nodes list
 *
 * gd_member_count
 * number of nodes in gd_nodes list that are members
 */

struct list_head        gd_nodes;
int                     gd_node_count;
int                     gd_member_count;
int                     gd_quorate;
int                     gd_nodeid;
int                     gd_barrier_time = 2;
struct list_head        gd_barriers;

int			gd_event_barriers;
int			gd_update_barriers;
int			gd_recover_barriers;

struct client {
	int fd;
	int level;
	char type[32];
};


static void group_action(int ci, char *buf)
{
	int rv;

	log_out("%s", buf);

	rv = write(client[ci].fd, buf, MAXLINE);
	if (rv != MAXLINE)
		log_print("write error %d errno %d", rv, errno);
}

void group_terminate(group_t *g)
{
	char buf[MAXLINE];
	snprintf(buf, sizeof(buf), "terminate %s", g->name);
	group_action(g->client, buf);
}

void group_stop(group_t *g)
{
	char buf[MAXLINE];
	snprintf(buf, sizeof(buf), "stop %s", g->name);
	group_action(g->client, buf);
}

void group_setid(group_t *g)
{
	char buf[MAXLINE];
	snprintf(buf, sizeof(buf), "set_id %s %u", g->name, g->global_id);
	group_action(g->client, buf);
}

void group_start(group_t *g, int *memb, int count, int event_nr, int type)
{
	char buf[MAXLINE];
	int i, len;

	len = snprintf(buf, sizeof(buf), "start %s %d %d", g->name, event_nr, type);
	for (i = 0; i < count; i++)
		len += sprintf(buf+len, " %d", memb[i]);
	group_action(g->client, buf);
}

void group_finish(group_t *g, int event_nr)
{
	char buf[MAXLINE];
	snprintf(buf, sizeof(buf), "finish %s %d", g->name, event_nr);
	group_action(g->client, buf);
}

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

static void client_alloc(void)
{
	int i;

	if (!client)
		client = malloc(NALLOC * sizeof(struct client));
	else
		client = realloc(client, (client_size + NALLOC) *
				         sizeof(struct client));
	if (!client)
		log_print("can't alloc for client array");

	for (i = client_size; i < client_size + NALLOC; i++) {
		client[i].fd = -1;
		client[i].level = -1;
		memset(client[i].type, 0, sizeof(client[i].type));
	}
	client_size += NALLOC;
}

static int client_add(int fd, int *maxi)
{
	int i;

	if (!client)
		client_alloc();
 again:
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

	client_alloc();
	goto again;
}

static void client_dead(int ci)
{
	close(client[ci].fd);
	client[ci].fd = -1;
	pollfd[ci].fd = -1;
}

static int client_process_setup(int ci, int argc, char **argv)
{
	log_in("setup %s %s", argv[1], argv[2]);

	strcpy(client[ci].type, argv[1]);
	client[ci].level = atoi(argv[2]);

	return 0;
}

static int client_process_join(int ci, int argc, char **argv)
{
	char buf[MAXLINE];

	log_in("local %s join %s", client[ci].type, argv[1]);

	do_join(argv[1], client[ci].level, ci);

	return 0;
}

static int client_process_leave(int ci, int argc, char **argv)
{
	char buf[MAXLINE];

	log_in("local %s leave %s", client[ci].type, argv[1]);

	do_leave(argv[1], client[ci].level, 0);

	return 0;
}

static int client_process_done(int ci, int argc, char **argv)
{
	char buf[MAXLINE];

	log_in("local %s done %s %s", client[ci].type, argv[1], argv[2]);

	do_done(argv[1], client[ci].level, atoi(argv[2]));

	return 0;
}

static int client_process(int ci)
{
	char buf[MAXLINE], *argv[MAXARGS], *cmd;
	int argc = 0, rv;

	memset(buf, 0, MAXLINE);

	rv = read(client[ci].fd, buf, MAXLINE);
	if (!rv) {
		client_dead(ci);
		return 0;
	}
	if (rv < 0) {
		log_print("client %d fd %d read error %d %d", ci,
			  client[ci].fd, rv, errno);
		return 0;
	}

	/* printf("client %d rv %d: %s\n", ci, rv, buf); */

	make_args(buf, &argc, argv, ' ');
	cmd = argv[0];

	if (!strcmp(cmd, "setup"))
		client_process_setup(ci, argc, argv);
	else if (!strcmp(cmd, "join"))
		client_process_join(ci, argc, argv);
	else if (!strcmp(cmd, "leave"))
		client_process_leave(ci, argc, argv);
	else if (!strcmp(cmd, "done"))
		client_process_done(ci, argc, argv);
	else
		log_print("unknown cmd %s client %d", cmd, ci);

	return 0;
}

static int setup_listener(void)
{
	struct sockaddr_un addr;
	socklen_t addrlen;
	int rv, s;

	/* we listen for new client connections on socket s */

	s = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (s < 0) {
		log_print("socket error %d %d", s, errno);
		return s;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_LOCAL;
	strcpy(&addr.sun_path[1], GROUPD_SOCK_PATH);
	addrlen = sizeof(sa_family_t) + strlen(addr.sun_path+1) + 1;

	rv = bind(s, (struct sockaddr *) &addr, addrlen);
	if (rv < 0) {
		log_print("bind error %d %d", rv, errno);
		close(s);
		return rv;
	}

	rv = listen(s, 5);
	if (rv < 0) {
		log_print("listen error %d %d", rv, errno);
		close(s);
		return rv;
	}

	return s;
}

void setup_single_node(void)
{
	node_t *node;

	log_print("running single node mode");
	gd_quorate = 1;
	gd_node_count = 1;
	gd_member_count = 1;
	gd_nodeid = 1;

	node = new_node(1);
	set_bit(NFL_CLUSTER_MEMBER, &node->flags);
	list_add(&node->list, &gd_nodes);
}

static int loop(void)
{
	int rv, fd, i, maxi = 0, timeout = -1;


	/*
	 * client[0] -- the socket listening for new connections
	 */

	fd = setup_listener();
	if (fd < 0)
		return fd;
	client_add(fd, &maxi);


	/*
	 * client[1] -- the fd for membership events and messages
	 */

	fd = setup_member_message();
	if (fd < 0)
		return fd;
	client_add(fd, &maxi);


	for (;;) {
		rv = poll(pollfd, maxi + 1, timeout);
		if (rv < 0)
			log_print("poll error %d %d", rv, errno);


		/* client[0] is listening for new connections */

		if (pollfd[0].revents & POLLIN) {
			fd = accept(client[0].fd, NULL, NULL);
			if (fd < 0)
				log_print("accept error %d %d", fd, errno);
			else
				client_add(fd, &maxi);
		}

		for (i = 1; i <= maxi; i++) {
			if (client[i].fd < 0)
				continue;
			if (pollfd[i].revents & POLLHUP)
				client_dead(i);
			else if (pollfd[i].revents & POLLIN) {
				if (i == 1)
					process_member_message();
				else
					client_process(i);
			}
		}

		if (!gd_quorate)
			continue;

		do {
			rv = 0;
			rv += process_recoveries();
			rv += process_joinleave();
			rv += process_updates();
			rv += process_barriers();
			rv += process_member_message();
		} while (rv);


		if (gd_event_barriers || gd_update_barriers ||
		    gd_recover_barriers) {
			log_debug("barriers pending ev %d up %d rev %d",
				  gd_event_barriers, gd_update_barriers,
				  gd_recover_barriers);
			timeout = 2;
		} else
			timeout = -1;
	}

	return 0;
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
		fprintf(stderr, "groupd is already running\n");
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
	int cont = TRUE;
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
			printf("groupd (built %s %s)\n", __DATE__, __TIME__);
			/* printf("%s\n", REDHAT_COPYRIGHT); */
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
			fprintf(stderr, "unknown option: %c\n", optchar);
			exit(EXIT_FAILURE);
			break;
		};
	}
}

/*
  Input:
  - local client messages (dlm_controld, fenced, ...)
  - remote groupd messages
  - membership events and messages from membership manager

  Output:
  - start/stop/finish over a client connection
  - messages to remote groupd's
*/

int main(int argc, char *argv[])
{
	prog_name = argv[0];

	decode_arguments(argc, argv);

	if (!debug)
		daemonize();

	pollfd = malloc(MAXCON * sizeof(struct pollfd));
	if (!pollfd)
		return -1;

	init_recovery();
	init_joinleave();

	return loop();
}

