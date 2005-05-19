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

#include "fd.h"
#include "ccs.h"
#include "copyright.cf"

#define OPTION_STRING			("cj:f:Dn:hVSwQ")
#define LOCKFILE_NAME			"/var/run/fenced.pid"

extern group_handle_t gh;
extern char *our_name;

struct client {
	int fd;
	char type[32];
};

static int client_size = MAX_CLIENTS;
static struct client client[MAX_CLIENTS];
static struct pollfd pollfd[MAX_CLIENTS];

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
			return i;
		}
	}
	return -1;
}

static void client_dead(int ci)
{
	log_debug("client %d fd %d dead", ci, client[ci].fd);
	close(client[ci].fd);
	client[ci].fd = -1;
	pollfd[ci].fd = -1;
}

static int client_process_join(int ci, int argc, char **argv)
{
	/* do group_join */
	return 0;
}

static int client_process_leave(int ci, int argc, char **argv)
{
	/* do group_leave */
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
		log_debug("client %d fd %d read error %d %d", ci,
			   client[ci].fd, rv, errno);
		return rv;
	}

	/* printf("client %d rv %d: %s\n", ci, rv, buf); */

	make_args(buf, &argc, argv, ' ');
	cmd = argv[0];

	if (!strcmp(cmd, "join"))
		client_process_join(ci, argc, argv);
	else if (!strcmp(cmd, "leave"))
		client_process_leave(ci, argc, argv);

	return 0;
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
	strcpy(&addr.sun_path[1], FENCED_SOCK_PATH);
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

	return s;
}

static int loop(fd_t *fd)
{
	int rv, i, f, maxi = 0, listen_fd, groupd_fd;

	rv = listen_fd = setup_listen();
	if (rv < 0)
		goto out;
	client_add(listen_fd, &maxi);

	rv = groupd_fd = setup_groupd();
	if (rv < 0)
		goto out;
	client_add(groupd_fd, &maxi);

	rv = group_join(gh, fd->name, NULL);
	if (rv < 0)
		goto out;

	for (;;) {
		rv = poll(pollfd, maxi + 1, -1);
		if (rv < 0)
			break;

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
			if (pollfd[i].revents & POLLHUP)
				client_dead(i);
			else if (pollfd[i].revents & POLLIN) {
				if (pollfd[i].fd == groupd_fd)
					process_groupd(fd);
				else
					client_process(i);
			}
		}

		if (fd->leave)
			group_leave(gh, fd->name, NULL);

		if (fd->leave_done)
			break;
	}

	group_exit(gh);
 out:
	return rv;
}

static int setup_ccs(fd_t *fd)
{
	char path[256];
	char *name = NULL, *str = NULL;
	int error, cd, i = 0, count = 0;


	while ((cd = ccs_connect()) < 0) {
		sleep(1);
		if (++i > 9 && !(i % 10))
			log_error("connect to ccs error %d, "
				  "check ccsd or cluster status", cd);
	}


	/* Our own nodename must be in cluster.conf before we're allowed to
	   join the fence domain and then mount gfs; other nodes need this to
	   fence us. */

	memset(path, 0, 256);
	snprintf(path, 256, "/cluster/clusternodes/clusternode[@name=\"%s\"]",
		 our_name);

	error = ccs_get(cd, path, &str);
	if (error)
		die1("local cman node name \"%s\" not found in cluster.conf",
		     our_name);


	/* If an option was set on the command line, don't set it from ccs. */

	if (fd->comline->clean_start_opt == FALSE) {
		str = NULL;
		memset(path, 0, 256);
		sprintf(path, "/cluster/fence_daemon/@clean_start");

		error = ccs_get(cd, path, &str);
		if (!error)
			fd->comline->clean_start = atoi(str);
		else
			fd->comline->clean_start = DEFAULT_CLEAN_START;
		if (str)
			free(str);
	}

	if (fd->comline->post_join_delay_opt == FALSE) {
		str = NULL;
		memset(path, 0, 256);
		sprintf(path, "/cluster/fence_daemon/@post_join_delay");

		error = ccs_get(cd, path, &str);
		if (!error)
			fd->comline->post_join_delay = atoi(str);
		else
			fd->comline->post_join_delay = DEFAULT_POST_JOIN_DELAY;
		if (str)
			free(str);
	}

	if (fd->comline->post_fail_delay_opt == FALSE) {
		str = NULL;
		memset(path, 0, 256);
		sprintf(path, "/cluster/fence_daemon/@post_fail_delay");

		error = ccs_get(cd, path, &str);
		if (!error)
			fd->comline->post_fail_delay = atoi(str);
		else
			fd->comline->post_fail_delay = DEFAULT_POST_FAIL_DELAY;
		if (str)
			free(str);
	}

	log_debug("delay post_join %ds post_fail %ds",
		  fd->comline->post_join_delay, fd->comline->post_fail_delay);

	if (fd->comline->clean_start) {
		log_debug("clean start, skipping initial nodes");
		goto out;
	}

	for (i = 1; ; i++) {
		name = NULL;
		memset(path, 0, 256);
		sprintf(path, "/cluster/clusternodes/clusternode[%d]/@name", i);

		error = ccs_get(cd, path, &name);
		if (error || !name)
			break;

		add_complete_node(fd, 0, name);
		free(name);
		count++;
	}

	log_debug("added %d nodes from ccs", count);
 out:
	ccs_disconnect(cd);
	return 0;
}

static fd_t *new_fd(commandline_t *comline)
{
	int namelen = strlen(comline->name);
	fd_t *fd;

	if (namelen > MAX_GROUPNAME_LEN)
		die1("group name too long, max %d", MAX_GROUPNAME_LEN);

	fd = malloc(sizeof(fd_t));
	if (!fd)
		die1("malloc error");

	memset(fd, 0, sizeof(fd_t));
	strncpy(fd->name, comline->name, namelen);

	fd->comline = comline;
	fd->first_recovery = FALSE;
	fd->last_stop = 0;
	fd->last_start = 0;
	fd->last_finish = 0;
	fd->prev_count = 0;
	INIT_LIST_HEAD(&fd->prev);
	INIT_LIST_HEAD(&fd->victims);
	INIT_LIST_HEAD(&fd->leaving);
	INIT_LIST_HEAD(&fd->complete);

	return fd;
}

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s [options]\n", prog_name);
	printf("\n");
	printf("Options:\n");
	printf("\n");
	printf("  -c	       All nodes are in a clean state to start\n");
	printf("  -j <secs>	Post-join fencing delay (default %d)\n",
				   DEFAULT_POST_JOIN_DELAY);
	printf("  -f <secs>	Post-fail fencing delay (default %d)\n",
				   DEFAULT_POST_FAIL_DELAY);
	printf("  -D	       Enable debugging code and don't fork\n");
	printf("  -h	       Print this help, then exit\n");
	printf("  -n <name>	Name of the fence domain, \"default\" if none\n");
	printf("  -V	       Print program version information, then exit\n");
	printf("\n");
	printf("Command line values override those in cluster.conf.\n");
	printf("For an unbounded delay use <secs> value of -1.\n");
	printf("\n");
}

static void lockfile(void)
{
	int fd, error;
	struct flock lock;
	char buf[33];

	memset(buf, 0, 33);

	fd = open(LOCKFILE_NAME, O_CREAT|O_WRONLY,
		  S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (fd < 0)
		die("cannot open/create lock file %s", LOCKFILE_NAME);

	lock.l_type = F_WRLCK;
	lock.l_start = 0;
	lock.l_whence = SEEK_SET;
	lock.l_len = 0;

	error = fcntl(fd, F_SETLK, &lock);
	if (error)
		die("fenced is already running");

	error = ftruncate(fd, 0);
	if (error)
		die("cannot clear lock file %s", LOCKFILE_NAME);

	sprintf(buf, "%d\n", getpid());

	error = write(fd, buf, strlen(buf));
	if (error <= 0)
		die("cannot write lock file %s", LOCKFILE_NAME);
}

static void decode_arguments(int argc, char **argv, commandline_t *comline)
{
	int cont = TRUE;
	int optchar;

	comline->post_join_delay_opt = FALSE;
	comline->post_fail_delay_opt = FALSE;
	comline->clean_start_opt = FALSE;

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {

		case 'c':
			comline->clean_start = 1;
			comline->clean_start_opt = TRUE;
			break;

		case 'j':
			comline->post_join_delay = atoi(optarg);
			comline->post_join_delay_opt = TRUE;
			break;

		case 'f':
			comline->post_fail_delay = atoi(optarg);
			comline->post_fail_delay_opt = TRUE;
			break;

		case 'D':
			comline->debug = TRUE;
			fenced_debug_opt = TRUE;
			break;

		case 'n':
			strncpy(comline->name, optarg, MAX_GROUPNAME_LEN);
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'V':
			printf("fenced %s (built %s %s)\n", FENCE_RELEASE_NAME,
				 __DATE__, __TIME__);
			printf("%s\n", REDHAT_COPYRIGHT);
			exit(EXIT_SUCCESS);
			break;

		case 'S':
		case 'w':
		case 'Q':
			/* do nothing, this is a fence_tool option that
			   we ignore when fence_tool starts us */
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
			die1("unknown option: %c", optchar);
			break;
		};
	}

	if (!strcmp(comline->name, ""))
		strcpy(comline->name, "default");
}

int main(int argc, char **argv)
{
	commandline_t comline;
	fd_t *fd;
	int error;

	prog_name = argv[0];
	memset(&comline, 0, sizeof(commandline_t));
	decode_arguments(argc, argv, &comline);

	fd = new_fd(&comline);

	error = setup_member();
	if (error)
		die1("setup_member error %d", error);

	error = setup_ccs(fd);
	if (error)
		die1("setup_ccs error %d", error);

	if (!fenced_debug_opt) {
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
		openlog("fenced", LOG_PID, LOG_DAEMON);
	}

	lockfile();

	error = loop(fd);

	free(fd);
	free(pollfd);
	exit_groupd();
	exit_member();
	return error;
}

char *prog_name;
int fenced_debug_opt;
char fenced_debug_buf[256];

