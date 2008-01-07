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

#define OPTION_STRING			("cj:f:Dn:O:hVS")
#define LOCKFILE_NAME			"/var/run/fenced.pid"

struct client {
	int fd;
	char type[32];
};

extern group_handle_t gh;
extern char *our_name;

static int client_size = MAX_CLIENTS;
static struct client client[MAX_CLIENTS];
static struct pollfd pollfd[MAX_CLIENTS];
static int fenced_exit;
commandline_t comline;
struct list_head domains;

static int do_write(int fd, void *buf, size_t count)
{
	int rv, off = 0;

 retry:
	rv = write(fd, buf + off, count);
	if (rv == -1 && errno == EINTR)
		goto retry;
	if (rv < 0)
		return rv;

	if (rv != count) {
		count -= rv;
		off += rv;
		goto retry;
	}
	return 0;
}

/*
static int do_read(int fd, void *buf, size_t count)
{
	int rv, off = 0;

	while (off < count) {
		rv = read(fd, buf + off, count - off);
		if (rv == 0)
			return -1;
		if (rv == -1 && errno == EINTR)
			continue;
		if (rv == -1)
			return -1;
		off += rv;
	}
	return 0;
}
*/

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
	snprintf(path, 256,
		 "/cluster/clusternodes/clusternode[@name=\"%s\"]/@name",
		 our_name);

	error = ccs_get(cd, path, &str);
	if (error)
		die1("local cman node name \"%s\" not found in the configuration",
		     our_name);


	/* If an option was set on the command line, don't set it from ccs. */

	if (comline.clean_start_opt == FALSE) {
		str = NULL;
		memset(path, 0, 256);
		sprintf(path, "/cluster/fence_daemon/@clean_start");

		error = ccs_get(cd, path, &str);
		if (!error)
			comline.clean_start = atoi(str);
		else
			comline.clean_start = DEFAULT_CLEAN_START;
		if (str)
			free(str);
	}

	if (comline.post_join_delay_opt == FALSE) {
		str = NULL;
		memset(path, 0, 256);
		sprintf(path, "/cluster/fence_daemon/@post_join_delay");

		error = ccs_get(cd, path, &str);
		if (!error)
			comline.post_join_delay = atoi(str);
		else
			comline.post_join_delay = DEFAULT_POST_JOIN_DELAY;
		if (str)
			free(str);
	}

	if (comline.post_fail_delay_opt == FALSE) {
		str = NULL;
		memset(path, 0, 256);
		sprintf(path, "/cluster/fence_daemon/@post_fail_delay");

		error = ccs_get(cd, path, &str);
		if (!error)
			comline.post_fail_delay = atoi(str);
		else
			comline.post_fail_delay = DEFAULT_POST_FAIL_DELAY;
		if (str)
			free(str);
	}

	if (comline.override_path_opt == FALSE) {
		str = NULL;
		memset(path, 0, 256);
		sprintf(path, "/cluster/fence_daemon/@override_path");

		error = ccs_get(cd, path, &str);
		if (!error)
			/* XXX These are not explicitly freed on exit; if
			   we decide to make fenced handle SIGHUP at a later
			   time, we will need to free this. */
			comline.override_path = strdup(str);
		else
			comline.override_path = strdup(DEFAULT_OVERRIDE_PATH);
		if (str)
			free(str);
	}

	log_debug("delay post_join %ds post_fail %ds",
		  comline.post_join_delay, comline.post_fail_delay);

	if (comline.clean_start) {
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

fd_t *find_domain(char *name)
{
	fd_t *fd;

	list_for_each_entry(fd, &domains, list) {
		if (strlen(name) == strlen(fd->name) &&
		    !strncmp(fd->name, name, strlen(name)))
                        return fd;
	}
	return NULL;
}

static fd_t *create_domain(char *name)
{
	fd_t *fd;

	if (strlen(name) > MAX_GROUPNAME_LEN)
		return NULL;

	fd = malloc(sizeof(fd_t));
	if (!fd)
		return NULL;

	memset(fd, 0, sizeof(fd_t));
	strcpy(fd->name, name);

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

int do_join(char *name)
{
	fd_t *fd;
	int rv;

	fd = find_domain(name);
	if (fd) {
		log_debug("join error: domain %s exists", name);
		rv = -EEXIST;
		goto out;
	}

	fd = create_domain(name);
	if (!fd) {
		rv = -ENOMEM;
		goto out;
	}

	rv = setup_ccs(fd);
	if (rv) {
		free(fd);
		goto out;
	}

	list_add(&fd->list, &domains);

	rv = group_join(gh, name);
	if (rv) {
		log_error("group_join error %d", rv);
		list_del(&fd->list);
		free(fd);
	}
 out:
	return rv;
}

int do_leave(char *name)
{
	fd_t *fd;
	int rv;

	fd = find_domain(name);
	if (!fd)
		return -EINVAL;

	fd->leave = 1;

	rv = group_leave(gh, name);
	if (rv) {
		log_error("group_leave error %d", rv);
		fd->leave = 0;
	}

	return rv;
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

static int do_dump(int fd)
{
	int len;

	if (dump_wrap) {
		len = DUMP_SIZE - dump_point;
		do_write(fd, dump_buf + dump_point, len);
		len = dump_point;
	} else
		len = dump_point;

	/* NUL terminate the debug string */
	dump_buf[dump_point] = '\0';

	do_write(fd, dump_buf, len);

	return 0;
}

static int client_process(int ci)
{
	char buf[MAXLINE], *argv[MAXARGS], *cmd, *name, out[MAXLINE];
	int argc = 0, rv;

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
	name = argv[1];

	if (!strcmp(cmd, "join"))
		rv = do_join(name);
	else if (!strcmp(cmd, "leave"))
		rv = do_leave(name);
	else if (!strcmp(cmd, "dump")) {
		do_dump(client[ci].fd);
		close(client[ci].fd);
		return 0;
	} else
		rv = -EINVAL;

	sprintf(out, "%d", rv);
	rv = write(client[ci].fd, out, MAXLINE);

	/* exit: cause fenced loop to exit */

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

static int loop(void)
{
	int rv, i, f, maxi = 0, listen_fd, member_fd, groupd_fd;

	rv = listen_fd = setup_listen();
	if (rv < 0)
		goto out;
	client_add(listen_fd, &maxi);

	rv = member_fd = setup_member();
	if (rv < 0)
		goto out;
	client_add(member_fd, &maxi);

	rv = groupd_fd = setup_groupd();
	if (rv < 0)
		goto out;
	client_add(groupd_fd, &maxi);

	log_debug("listen %d member %d groupd %d",
		  listen_fd, member_fd, groupd_fd);

	for (;;) {
		rv = poll(pollfd, maxi + 1, -1);
		if (rv < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

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
			if (pollfd[i].revents & POLLHUP) {
				if (pollfd[i].fd == member_fd) {
					log_error("cluster is down, exiting");
					exit(1);
				}
				if (pollfd[i].fd == groupd_fd) {
					log_error("groupd is down, exiting");
					exit(1);
				}
				client_dead(i);
			} else if (pollfd[i].revents & POLLIN) {
				if (pollfd[i].fd == groupd_fd)
					process_groupd();
				else if (pollfd[i].fd == member_fd)
					process_member();
				else
					client_process(i);
			}
		}

		if (fenced_exit)
			break;
	}

	group_exit(gh);
 out:
	return rv;
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
	printf("  -O <path>    Override path (default %s)\n",
	       			   DEFAULT_OVERRIDE_PATH);
	printf("  -D	       Enable debugging code and don't fork\n");
	printf("  -h	       Print this help, then exit\n");
	printf("  -V	       Print program version information, then exit\n");
	printf("\n");
	printf("Command line values override those in " DEFAULT_CONFIG_DIR "/" DEFAULT_CONFIG_FILE ".\n");
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

	comline->override_path_opt = FALSE;
	comline->override_path = NULL;
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

		case 'O':
			comline->override_path = strdup(optarg);
			comline->override_path_opt = TRUE;
			break;

		case 'D':
			daemon_debug_opt = TRUE;
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'V':
			printf("fenced %s (built %s %s)\n", RELEASE_VERSION,
				 __DATE__, __TIME__);
			printf("%s\n", REDHAT_COPYRIGHT);
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
			die1("unknown option: %c", optchar);
			break;
		};
	}
}

int main(int argc, char **argv)
{
	int error;

	prog_name = argv[0];
	memset(&comline, 0, sizeof(commandline_t));
	decode_arguments(argc, argv, &comline);
	INIT_LIST_HEAD(&domains);
	client_init();

	if (!daemon_debug_opt) {
		if (daemon(0,0) < 0) {
			perror("main: cannot fork");
			exit(EXIT_FAILURE);
		}
		
		chdir("/");
		umask(0);
		openlog("fenced", LOG_PID, LOG_DAEMON);
	}

	lockfile();

	error = loop();

	exit_groupd();
	exit_member();
	return error;
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
int daemon_debug_opt;
char daemon_debug_buf[256];
char dump_buf[DUMP_SIZE];
int dump_point;
int dump_wrap;

