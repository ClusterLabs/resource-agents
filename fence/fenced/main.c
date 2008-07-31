#include "fd.h"
#include "config.h"
#include <pthread.h>
#include "copyright.cf"

#define LOCKFILE_NAME		"/var/run/fenced.pid"
#define CLIENT_NALLOC		32

static int client_maxi;
static int client_size = 0;
static struct client *client = NULL;
static struct pollfd *pollfd = NULL;
static pthread_t query_thread;
static pthread_mutex_t query_mutex;

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
		return rv;
	}

	if (rv != count) {
		count -= rv;
		off += rv;
		goto retry;
	}
	return 0;
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

static int do_external(char *name, char *extra, int extra_len)
{
	struct fd *fd;
	int rv = 0;

	fd = find_fd(name);
	if (!fd)
		return -EINVAL;

	if (group_mode == GROUP_LIBGROUP)
		rv = -ENOSYS;
	else
		send_external(fd, name_to_nodeid(extra));

	return rv;
}

static void init_header(struct fenced_header *h, int cmd, int result,
			int extra_len)
{
	memset(h, 0, sizeof(struct fenced_header));

	h->magic = FENCED_MAGIC;
	h->version = FENCED_VERSION;
	h->len = sizeof(struct fenced_header) + extra_len;
	h->command = cmd;
	h->data = result;
}

/* combines a header and the data and sends it back to the client in
   a single do_write() call */

static void do_reply(int f, int cmd, int result, char *buf, int buflen)
{
	char *reply;
	int reply_len;

	reply_len = sizeof(struct fenced_header) + buflen;
	reply = malloc(reply_len);
	if (!reply)
		return;
	memset(reply, 0, reply_len);

	init_header((struct fenced_header *)reply, cmd, result, buflen);

	if (buf && buflen)
		memcpy(reply + sizeof(struct fenced_header), buf, buflen);

	do_write(f, reply, reply_len);

	free(reply);
}

static void query_dump_debug(int f)
{
	struct fenced_header h;
	int extra_len;
	int len;

	/* in the case of dump_wrap, extra_len will go in two writes,
	   first the log tail, then the log head */
	if (dump_wrap)
		extra_len = FENCED_DUMP_SIZE;
	else
		extra_len = dump_point;

	init_header(&h, FENCED_CMD_DUMP_DEBUG, 0, extra_len);
	do_write(f, &h, sizeof(h));

	if (dump_wrap) {
		len = FENCED_DUMP_SIZE - dump_point;
		do_write(f, dump_buf + dump_point, len);
		len = dump_point;
	} else
		len = dump_point;

	/* NUL terminate the debug string */
	dump_buf[dump_point] = '\0';

	do_write(f, dump_buf, len);
}

static void query_node_info(int f, int data_nodeid)
{
	struct fd *fd;
	struct fenced_node node;
	int nodeid, rv;

	fd = find_fd("default");
	if (!fd) {
		rv = -ENOENT;
		goto out;
	}

	if (data_nodeid == FENCED_NODEID_US)
		nodeid = our_nodeid;
	else
		nodeid = data_nodeid;

	if (group_mode == GROUP_LIBGROUP)
		rv = set_node_info_group(fd, nodeid, &node);
	else
		rv = set_node_info(fd, nodeid, &node);
 out:
	do_reply(f, FENCED_CMD_NODE_INFO, rv, (char *)&node, sizeof(node));
}

static void query_domain_info(int f)
{
	struct fd *fd;
	struct fenced_domain domain;
	int rv;

	fd = find_fd("default");
	if (!fd) {
		rv = -ENOENT;
		goto out;
	}

	if (group_mode == GROUP_LIBGROUP)
		rv = set_domain_info_group(fd, &domain);
	else
		rv = set_domain_info(fd, &domain);
 out:
	do_reply(f, FENCED_CMD_DOMAIN_INFO, rv, (char *)&domain, sizeof(domain));
}

static void query_domain_nodes(int f, int option, int max)
{
	struct fd *fd;
	int node_count = 0;
	struct fenced_node *nodes = NULL;
	int rv, result;

	fd = find_fd("default");
	if (!fd) {
		result = -ENOENT;
		node_count = 0;
		goto out;
	}

	if (group_mode == GROUP_LIBGROUP)
		rv = set_domain_nodes_group(fd, option, &node_count, &nodes);
	else
		rv = set_domain_nodes(fd, option, &node_count, &nodes);

	if (rv < 0) {
		result = rv;
		node_count = 0;
		goto out;
	}

	/* node_count is the number of structs copied/returned; the caller's
	   max may be less than that, in which case we copy as many as they
	   asked for and return -E2BIG */

	if (node_count > max) {
		result = -E2BIG;
		node_count = max;
	} else {
		result = node_count;
	}
 out:
	do_reply(f, FENCED_CMD_DOMAIN_NODES, result,
	         (char *)nodes, node_count * sizeof(struct fenced_node));

	if (nodes)
		free(nodes);
}

static void process_connection(int ci)
{
	struct fenced_header h;
	char *extra = NULL;
	int rv, extra_len;

	rv = do_read(client[ci].fd, &h, sizeof(h));
	if (rv < 0) {
		log_debug("connection %d read error %d", ci, rv);
		goto out;
	}

	if (h.magic != FENCED_MAGIC) {
		log_debug("connection %d magic error %x", ci, h.magic);
		goto out;
	}

	if ((h.version & 0xFFFF0000) != (FENCED_VERSION & 0xFFFF0000)) {
		log_debug("connection %d version error %x", ci, h.version);
		goto out;
	}

	if (h.len > sizeof(h)) {
		extra_len = h.len - sizeof(h);
		extra = malloc(extra_len);
		if (!extra) {
			log_error("process_connection no mem %d", extra_len);
			goto out;
		}
		memset(extra, 0, extra_len);

		rv = do_read(client[ci].fd, extra, extra_len);
		if (rv < 0) {
			log_debug("connection %d extra read error %d", ci, rv);
			goto out;
		}
	}

	switch (h.command) {
	case FENCED_CMD_JOIN:
		do_join("default");
		break;
	case FENCED_CMD_LEAVE:
		do_leave("default");
		break;
	case FENCED_CMD_EXTERNAL:
		do_external("default", extra, extra_len);
		break;
	case FENCED_CMD_DUMP_DEBUG:
	case FENCED_CMD_NODE_INFO:
	case FENCED_CMD_DOMAIN_INFO:
	case FENCED_CMD_DOMAIN_NODES:
		log_error("process_connection query on wrong socket");
		break;
	default:
		log_error("process_connection %d unknown command %d",
			  ci, h.command);
	}
 out:
	if (extra)
		free(extra);
	client_dead(ci);
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

static int setup_listener(char *sock_path)
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
	strcpy(&addr.sun_path[1], sock_path);
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

void query_lock(void)
{
	pthread_mutex_lock(&query_mutex);
}

void query_unlock(void)
{
	pthread_mutex_unlock(&query_mutex);
}

/* This is a thread, so we have to be careful, don't call log_ functions.
   We need a thread to process queries because the main thread will block
   for long periods when running fence agents. */

static void *process_queries(void *arg)
{
	struct fenced_header h;
	int s = *((int *)arg);
	int f, rv;

	for (;;) {
		f = accept(s, NULL, NULL);

		rv = do_read(f, &h, sizeof(h));
		if (rv < 0) {
			goto out;
		}

		if (h.magic != FENCED_MAGIC) {
			goto out;
		}

		if ((h.version & 0xFFFF0000) != (FENCED_VERSION & 0xFFFF0000)) {
			goto out;
		}

		pthread_mutex_lock(&query_mutex);

		switch (h.command) {
		case FENCED_CMD_DUMP_DEBUG:
			query_dump_debug(f);
			break;
		case FENCED_CMD_NODE_INFO:
			query_node_info(f, h.data);
			break;
		case FENCED_CMD_DOMAIN_INFO:
			query_domain_info(f);
			break;
		case FENCED_CMD_DOMAIN_NODES:
			query_domain_nodes(f, h.option, h.data);
			break;
		default:
			break;
		}
		pthread_mutex_unlock(&query_mutex);

 out:
		close(f);
	}
}

static int setup_queries(void)
{
	int rv, s;

	rv = setup_listener(FENCED_QUERY_SOCK_PATH);
	if (rv < 0)
		return rv;
	s = rv;

	pthread_mutex_init(&query_mutex, NULL);

	rv = pthread_create(&query_thread, NULL, process_queries, &s);
	if (rv < 0) {
		log_error("can't create query thread");
		close(s);
		return rv;
	}
	return 0;
}

void cluster_dead(int ci)
{
	log_error("cluster is down, exiting");
	daemon_quit = 1;
}

static void loop(void)
{
	int rv, i;
	void (*workfn) (int ci);
	void (*deadfn) (int ci);

	rv = setup_queries();
	if (rv < 0)
		goto out;

	rv = setup_listener(FENCED_SOCK_PATH);
	if (rv < 0)
		goto out;
	client_add(rv, process_listener, NULL);

	rv = setup_cman();
	if (rv < 0)
		goto out;
	client_add(rv, process_cman, cluster_dead);

	rv = setup_ccs();
	if (rv < 0)
		goto out;

	setup_logging();

	group_mode = GROUP_LIBCPG;

	if (cfgd_groupd_compat) {
		rv = setup_groupd();
		if (rv < 0)
			goto out;
		client_add(rv, process_groupd, cluster_dead);

		group_mode = GROUP_LIBGROUP;
		if (cfgd_groupd_compat == 2)
			set_group_mode();
	}
	log_debug("group_mode %d compat %d", group_mode, cfgd_groupd_compat);

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
			if (daemon_quit && list_empty(&domains))
				goto out;
			daemon_quit = 0;
			continue;
		}
		if (rv < 0) {
			log_error("poll errno %d", errno);
			goto out;
		}

		pthread_mutex_lock(&query_mutex);

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
		pthread_mutex_unlock(&query_mutex);

		if (daemon_quit)
			break;
	}
 out:
	if (cfgd_groupd_compat)
		close_groupd();
	close_logging();
	close_ccs();
	close_cman();

	if (!list_empty(&domains))
		log_error("domain abandoned");
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
	printf("  -L <num>     Enable (1) or disable (0) debugging to logsys (default %d)\n", DEFAULT_DEBUG_LOGSYS);
	printf("  -g <num>     groupd compatibility mode, 0 off, 1 on, 2 detect (default %d)\n", DEFAULT_GROUPD_COMPAT);
	printf("               0: use libcpg, no backward compat, best performance\n");
	printf("               1: use libgroup for compat with cluster2/rhel5\n");
	printf("               2: use groupd to detect old, or mode 1, nodes that\n"
	       "               require compat, use libcpg if none found\n");
	printf("  -c           All nodes are in a clean state to start\n");
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

#define OPTION_STRING	"L:g:cj:f:Dn:O:T:hVS"

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

		case 'L':
			optd_debug_logsys = 1;
			cfgd_debug_logsys = atoi(optarg);
			break;

		case 'g':
			optd_groupd_compat = 1;
			cfgd_groupd_compat = atoi(optarg);
			break;

		case 'c':
			optd_clean_start = 1;
			cfgd_clean_start = 1;
			break;

		case 'j':
			optd_post_join_delay = 1;
			cfgd_post_join_delay = atoi(optarg);
			break;

		case 'f':
			optd_post_fail_delay = 1;
			cfgd_post_fail_delay = atoi(optarg);
			break;

		case 'R':
			optd_override_time = 1;
			cfgd_override_time = atoi(optarg);
			if (cfgd_override_time < 3)
				cfgd_override_time = 3;
			break;

		case 'O':
			optd_override_path = 1;
			cfgd_override_path = strdup(optarg);
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

	if (!optd_debug_logsys && getenv("FENCED_DEBUG")) {
		optd_debug_logsys = 1;
		cfgd_debug_logsys = atoi(getenv("FENCED_DEBUG"));
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

	init_logging();

	read_arguments(argc, argv);

	lockfile();

	if (!daemon_debug_opt) {
		if (daemon(0, 0) < 0) {
			perror("main: cannot fork");
			exit(EXIT_FAILURE);
		}
		umask(0);
	}
	signal(SIGTERM, sigterm_handler);

	set_oom_adj(-16);

	loop();

	return 0;
}

void daemon_dump_save(void)
{
	int len, i;

	len = strlen(daemon_debug_buf);

	for (i = 0; i < len; i++) {
		dump_buf[dump_point++] = daemon_debug_buf[i];

		if (dump_point == FENCED_DUMP_SIZE) {
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
char dump_buf[FENCED_DUMP_SIZE];
int dump_point;
int dump_wrap;
int group_mode;

