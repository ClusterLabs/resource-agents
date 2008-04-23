/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2008 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "fd.h"
#include "copyright.cf"

#define LOCKFILE_NAME		"/var/run/fenced.pid"
#define CLIENT_NALLOC		32

static int client_maxi;
static int client_size = 0;
static struct client *client = NULL;
static struct pollfd *pollfd = NULL;

struct client {
	int fd;
	void *workfn;
	void *deadfn;
};

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

static int do_write(int fd, void *buf, size_t count)
{
	int rv, off = 0;

 retry:
	rv = write(fd, buf + off, count);
	if (rv == -1 && errno == EINTR)
		goto retry;
	if (rv < 0) {
		log_error("write errno %d", errno);
		return rv;
	}

	if (rv != count) {
		count -= rv;
		off += rv;
		goto retry;
	}
	return 0;
}

static void do_dump(int fd)
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
}

static void client_alloc(void)
{
	int i;

	if (!client) {
		client = malloc(CLIENT_NALLOC * sizeof(struct client));
		pollfd = malloc(CLIENT_NALLOC * sizeof(struct pollfd));
	} else {
		client = realloc(client, (client_size + CLIENT_NALLOC) *
					 sizeof(struct client));
		pollfd = realloc(pollfd, (client_size + CLIENT_NALLOC) *
					 sizeof(struct pollfd));
		if (!pollfd)
			log_error("can't alloc for pollfd");
	}
	if (!client || !pollfd)
		log_error("can't alloc for client array");

	for (i = client_size; i < client_size + CLIENT_NALLOC; i++) {
		client[i].workfn = NULL;
		client[i].deadfn = NULL;
		client[i].fd = -1;
		pollfd[i].fd = -1;
		pollfd[i].revents = 0;
	}
	client_size += CLIENT_NALLOC;
}

void client_dead(int ci)
{
	close(client[ci].fd);
	client[ci].workfn = NULL;
	client[ci].fd = -1;
	pollfd[ci].fd = -1;
}

int client_add(int fd, void (*workfn)(int ci), void (*deadfn)(int ci))
{
	int i;

	if (!client)
		client_alloc();
 again:
	for (i = 0; i < client_size; i++) {
		if (client[i].fd == -1) {
			client[i].workfn = workfn;
			if (deadfn)
				client[i].deadfn = deadfn;
			else
				client[i].deadfn = client_dead;
			client[i].fd = fd;
			pollfd[i].fd = fd;
			pollfd[i].events = POLLIN;
			if (i > client_maxi)
				client_maxi = i;
			return i;
		}
	}

	client_alloc();
	goto again;
}

static void sigterm_handler(int sig)
{
	daemon_quit = 1;
}

static struct fd *create_fd(char *name)
{
	struct fd *fd;

	if (strlen(name) > MAX_GROUPNAME_LEN)
		return NULL;

	fd = malloc(sizeof(struct fd));
	if (!fd)
		return NULL;

	memset(fd, 0, sizeof(struct fd));
	strcpy(fd->name, name);

	INIT_LIST_HEAD(&fd->changes);
	INIT_LIST_HEAD(&fd->node_history);
	INIT_LIST_HEAD(&fd->victims);
	INIT_LIST_HEAD(&fd->complete);
	INIT_LIST_HEAD(&fd->prev);
	INIT_LIST_HEAD(&fd->leaving);

	return fd;
}

void free_fd(struct fd *fd)
{
	struct change *cg, *cg_safe;
	struct node_history *nodeh, *nodeh_safe;

	list_for_each_entry_safe(cg, cg_safe, &fd->changes, list) {
		list_del(&cg->list);
		free_cg(cg);
	}
	if (fd->started_change)
		free_cg(fd->started_change);

	list_for_each_entry_safe(nodeh, nodeh_safe, &fd->node_history, list) {
		list_del(&nodeh->list);
		free(nodeh);
	}

	free_node_list(&fd->victims);
	free_node_list(&fd->complete);
	free_node_list(&fd->prev);
	free_node_list(&fd->leaving);

	free(fd);
}

struct fd *find_fd(char *name)
{
	struct fd *fd;

	list_for_each_entry(fd, &domains, list) {
		if (strlen(name) == strlen(fd->name) &&
		    !strncmp(fd->name, name, strlen(name)))
			return fd;
	}
	return NULL;
}

static int do_join(char *name)
{
	struct fd *fd;
	int rv;

	fd = find_fd(name);
	if (fd) {
		log_debug("join error: domain %s exists", name);
		rv = -EEXIST;
		goto out;
	}

	fd = create_fd(name);
	if (!fd) {
		rv = -ENOMEM;
		goto out;
	}

	rv = read_ccs(fd);
	if (rv) {
		free(fd);
		goto out;
	}

	if (group_mode == GROUP_LIBGROUP)
		rv = fd_join_group(fd);
	else
		rv = fd_join(fd);
 out:
	return rv;
}

static int do_leave(char *name)
{
	struct fd *fd;
	int rv;

	fd = find_fd(name);
	if (!fd)
		return -EINVAL;

	if (group_mode == GROUP_LIBGROUP)
		rv = fd_leave_group(fd);
	else
		rv = fd_leave(fd);

	return rv;
}

#define MAXARGS 8

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

static void process_connection(int ci)
{
	char buf[FENCED_MSGLEN];
	char out[FENCED_MSGLEN];
	char *argv[MAXARGS];
	char *cmd, *name;
	int argc = 0, rv;

	memset(buf, 0, sizeof(buf));
	memset(out, 0, sizeof(out));
	memset(argv, 0, sizeof(char *) * MAXARGS);

	rv = do_read(client[ci].fd, buf, FENCED_MSGLEN);
	if (rv < 0) {
		log_error("client %d fd %d read error %d %d", ci,
			   client[ci].fd, rv, errno);
		client_dead(ci);
		return;
	}

	log_debug("ci %d read %s", ci, buf);

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
	}

	sprintf(out, "%d", rv);
	write(client[ci].fd, out, FENCED_MSGLEN);

	/* exit: cause fenced loop to exit */
}

static void process_listener(int ci)
{
	int fd, i;

	fd = accept(client[ci].fd, NULL, NULL);
	if (fd < 0) {
		log_error("process_listener: accept error %d %d", fd, errno);
		return;
	}
	
	i = client_add(fd, process_connection, NULL);

	log_debug("client connection %d fd %d", i, fd);
}

static int setup_listener(void)
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

static void cluster_dead(int ci)
{
	log_error("cluster is down, exiting");
	exit(1);
}

static int loop(void)
{
	int rv, i;
	void (*workfn) (int ci);
	void (*deadfn) (int ci);

	rv = setup_listener();
	if (rv < 0)
		goto out;
	client_add(rv, process_listener, NULL);

	rv = setup_cman();
	if (rv < 0)
		goto out;
	client_add(rv, process_cman, cluster_dead);

	group_mode = GROUP_LIBCPG;

	if (comline.groupd_compat) {
		rv = setup_groupd();
		if (rv < 0)
			goto out;
		client_add(rv, process_groupd, cluster_dead);

		group_mode = GROUP_LIBGROUP;

		if (comline.groupd_compat == 2) {
			/* set_group_mode(); */
			group_mode = GROUP_LIBGROUP;
		}
	}

	if (group_mode == GROUP_LIBCPG) {
		/*
		rv = setup_cpg();
		if (rv < 0)
			goto out;
		client_add(rv, process_cpg, cluster_dead);
		*/
	}

	for (;;) {
		rv = poll(pollfd, client_maxi + 1, -1);
		if (rv == -1 && errno == EINTR) {
			if (daemon_quit && list_empty(&domains)) {
				exit(1);
			}
			daemon_quit = 0;
			continue;
		}
		if (rv < 0) {
			log_error("poll errno %d", errno);
			goto out;
		}

		for (i = 0; i <= client_maxi; i++) {
			if (client[i].fd < 0)
				continue;
			if (pollfd[i].revents & POLLIN) {
				workfn = client[i].workfn;
				workfn(i);
			}
			if (pollfd[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
				deadfn = client[i].deadfn;
				deadfn(i);
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
		fprintf(stderr, "is already running\n");
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

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("fenced [options]\n");
	printf("\n");
	printf("Options:\n");
	printf("\n");
	printf("  -D           Enable debugging code and don't fork\n");
	printf("  -g <num>     groupd compatibility, 0 off, 1 on\n");
	printf("               on: use libgroup, compat with cluster2/stable2/rhel5\n");
	printf("               off: use libcpg, no backward compatability\n");
	printf("               Default is %d\n", DEFAULT_GROUPD_COMPAT);
	printf("  -c	       All nodes are in a clean state to start\n");
	printf("  -j <secs>    Post-join fencing delay (default %d)\n", DEFAULT_POST_JOIN_DELAY);
	printf("  -f <secs>    Post-fail fencing delay (default %d)\n", DEFAULT_POST_FAIL_DELAY);
	printf("  -R <secs>    Override time (default %d)\n", DEFAULT_OVERRIDE_TIME);

	printf("  -O <path>    Override path (default %s)\n", DEFAULT_OVERRIDE_PATH);
	printf("  -h           Print this help, then exit\n");
	printf("  -V           Print program version information, then exit\n");
	printf("\n");
	printf("Command line values override those in " DEFAULT_CONFIG_DIR "/" DEFAULT_CONFIG_FILE ".\n");
	printf("For an unbounded delay use <secs> value of -1.\n");
	printf("\n");
}

#define OPTION_STRING	"g:cj:f:Dn:O:T:hVS"

static void read_arguments(int argc, char **argv)
{
	int cont = 1;
	int optchar;

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {

		case 'D':
			daemon_debug_opt = 1;
			break;

		case 'g':
			comline.groupd_compat = atoi(optarg);
			comline.groupd_compat_opt = 1;
			break;

		case 'c':
			comline.clean_start = 1;
			comline.clean_start_opt = 1;
			break;

		case 'j':
			comline.post_join_delay = atoi(optarg);
			comline.post_join_delay_opt = 1;
			break;

		case 'f':
			comline.post_fail_delay = atoi(optarg);
			comline.post_fail_delay_opt = 1;
			break;

		case 'R':
			comline.override_time = atoi(optarg);
			if (comline.override_time < 3)
				comline.override_time = 3;
			comline.override_time_opt = 1;
			break;

		case 'O':
			if (comline.override_path)
				free(comline.override_path);
			comline.override_path = strdup(optarg);
			comline.override_path_opt = 1;
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
			cont = 0;
			break;

		default:
			fprintf(stderr, "unknown option: %c", optchar);
			exit(EXIT_FAILURE);
		};
	}
}

static void set_oom_adj(int val)
{
	FILE *fp;

	fp = fopen("/proc/self/oom_adj", "w");
	if (!fp)
		return;

	fprintf(fp, "%i", val);
	fclose(fp);
}

int main(int argc, char **argv)
{
	INIT_LIST_HEAD(&domains);

	memset(&comline, 0, sizeof(comline));
	comline.groupd_compat = DEFAULT_GROUPD_COMPAT;
	comline.clean_start = DEFAULT_CLEAN_START;
	comline.post_join_delay = DEFAULT_POST_JOIN_DELAY;
	comline.post_fail_delay = DEFAULT_POST_FAIL_DELAY;
	comline.override_time = DEFAULT_OVERRIDE_TIME;
	comline.override_path = strdup(DEFAULT_OVERRIDE_PATH);

	read_arguments(argc, argv);

	if (!daemon_debug_opt) {
		if (daemon(0, 0) < 0) {
			perror("main: cannot fork");
			exit(EXIT_FAILURE);
		}
		chdir("/");
		umask(0);
		openlog("fenced", LOG_PID, LOG_DAEMON);
	}
	lockfile();
	signal(SIGTERM, sigterm_handler);

	set_oom_adj(-16);

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

int daemon_debug_opt;
int daemon_quit;
struct list_head domains;
int cman_quorate;
int our_nodeid;
char our_name[MAX_NODENAME_LEN+1];
char daemon_debug_buf[256];
char dump_buf[DUMP_SIZE];
int dump_point;
int dump_wrap;
int group_mode;
struct commandline comline;

#if 0
   libfenced

   struct fenced_node:
   nodeid, name,
   given node is pending victim?,
   last time given node was successfully fenced, how, and by whom,
   last failed fence time (only master will know),
   last domain join time, last domain leave time

   struct fenced_domain
   name,
   current number of members,
   master nodeid
   current number of victims,
   current pending victim,
   state

   /* tell fenced that an external program has fenced a node, e.g. fence_node;
      fenced will try to suppress its own fencing of this node a second time */
   fenced_external(int nodeid);

   /* fenced gives info about a single node */
   fenced_node_info(int nodeid, char *name, struct fenced_node *info);

   /* fenced gives info about the domain */
   fenced_domain_info(struct fenced_domain *info);

   /* fenced copies a node struct for each member */
   fenced_domain_members(int num, struct fenced_node **info);

   fenced_debug_dump(char **buf, int len);
   fenced_join(void);
   fenced_leave(void);

   for all of these, libfenced connects to fenced, writes a structure that
   defines the type, then for some, reads back data, copies data into
   buffers provided by caller, disconnects from fenced

#endif

