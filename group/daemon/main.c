#include <signal.h>
#include <time.h>

#include "gd_internal.h"
#include "ccs.h"
#include "copyright.cf"

#define LOCKFILE_NAME	"/var/run/groupd.pid"
#define CLIENT_NALLOC	32

extern struct list_head recovery_sets;

static int client_maxi;
static int client_size = 0;
static struct client *client = NULL;
static struct pollfd *pollfd = NULL;
static char last_action[16];
static int ccs_handle;

struct client {
	int fd;
	int level;
	char type[32];
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
		log_print("write fd %d errno %d", fd, errno);
		return rv;
	}

	if (rv != count) {
		count -= rv;
		off += rv;
		goto retry;
	}
	return 0;
}

void read_ccs_name(char *path, char *name)
{
	char *str;
	int error;

	error = ccs_get(ccs_handle, path, &str);
	if (error || !str)
		return;

	strcpy(name, str);

	free(str);
}

void read_ccs_yesno(char *path, int *yes, int *no)
{
	char *str;
	int error;

	*yes = 0;
	*no = 0;

	error = ccs_get(ccs_handle, path, &str);
	if (error || !str)
		return;

	if (!strcmp(str, "yes"))
		*yes = 1;

	else if (!strcmp(str, "no"))
		*no = 1;

	free(str);
}

void read_ccs_int(char *path, int *config_val)
{
	char *str;
	int val;
	int error;

	error = ccs_get(ccs_handle, path, &str);
	if (error || !str)
		return;

	val = atoi(str);

	if (val < 0) {
		log_print("ignore invalid value %d for %s", val, path);
		return;
	}

	*config_val = val;
	log_debug("%s is %u", path, val);
	free(str);
}

#define GROUPD_COMPAT_PATH "/cluster/group/@groupd_compat"
#define GROUPD_WAIT_PATH "/cluster/group/@groupd_wait"
#define GROUPD_MODE_DELAY_PATH "/cluster/group/@groupd_mode_delay"

int setup_ccs(void)
{
	int i = 0, cd;

	while ((cd = ccs_connect()) < 0) {
		sleep(1);
		if (++i > 9 && !(i % 10))
			log_print("connect to ccs error %d, "
				  "check cluster status", cd);
	}

	ccs_handle = cd;

	/* These config values are set from cluster.conf only if they haven't
	   already been set on the command line. */

	if (!optd_groupd_compat)
		read_ccs_int(GROUPD_COMPAT_PATH, &cfgd_groupd_compat);

	if (!optd_groupd_wait)
		read_ccs_int(GROUPD_WAIT_PATH, &cfgd_groupd_wait);

	if (!optd_groupd_mode_delay)
		read_ccs_int(GROUPD_MODE_DELAY_PATH, &cfgd_groupd_mode_delay);

	return 0;
}

void close_ccs(void)
{
	ccs_disconnect(ccs_handle);
}

static void app_action(app_t *a, char *buf)
{
	int rv;

	log_group(a->g, "action for app: %s", buf);

	rv = do_write(client[a->client].fd, buf, GROUPD_MSGLEN);
	if (rv < 0)
		log_error(a->g, "app_action write error");
}

void app_deliver(app_t *a, struct save_msg *save)
{
	char buf[GROUPD_MSGLEN];
	int rv;

	rv = snprintf(buf, sizeof(buf), "deliver %s %d %d",
		      a->g->name, save->nodeid,
		      (int)(save->msg_len - sizeof(msg_t)));

	log_group(a->g, "deliver to app: %s", buf);

	memcpy(buf + rv + 1, save->msg_long + sizeof(msg_t),
	       save->msg_len - sizeof(msg_t));

	/*
	log_group(a->g, "app_deliver body len %d \"%s\"",
		  save->msg_len - sizeof(msg_t),
		  save->msg_long + sizeof(msg_t));
	*/

	rv = do_write(client[a->client].fd, buf, GROUPD_MSGLEN);
	if (rv < 0)
		log_error(a->g, "app_deliver write error");
}

void app_terminate(app_t *a)
{
	char buf[GROUPD_MSGLEN];
	snprintf(buf, sizeof(buf), "terminate %s", a->g->name);
	app_action(a, buf);
}

void app_stop(app_t *a)
{
	char buf[GROUPD_MSGLEN];
	snprintf(buf, sizeof(buf), "stop %s", a->g->name);
	app_action(a, buf);
}

void app_setid(app_t *a)
{
	char buf[GROUPD_MSGLEN];
	snprintf(buf, sizeof(buf), "setid %s %u", a->g->name, a->g->global_id);
	app_action(a, buf);
}

void app_start(app_t *a)
{
	char buf[GROUPD_MSGLEN];
	int len = 0, type, count = 0;
	node_t *node;

	if (a->current_event->state == EST_JOIN_START_WAIT)
		type = NODE_JOIN;
	else if (a->current_event->state == EST_LEAVE_START_WAIT)
		type = NODE_LEAVE;
	else if (a->current_event->state == EST_FAIL_START_WAIT)
		type = NODE_FAILED;
	else {
		/* report error */
		type = -1;
	}

	/* start <name> <event_nr> <type> <count> <memb0> <memb1>... */

	list_for_each_entry(node, &a->nodes, list)
		count++;

	len = snprintf(buf, sizeof(buf), "start %s %d %d %d",
		       a->g->name, a->current_event->event_nr, type, count);

	list_for_each_entry(node, &a->nodes, list)
		len += sprintf(buf+len, " %d", node->nodeid);

	app_action(a, buf);
}

void app_finish(app_t *a)
{
	char buf[GROUPD_MSGLEN];
	snprintf(buf, sizeof(buf), "finish %s %d",
		 a->g->name, a->current_event->event_nr);
	app_action(a, buf);
}

#define MAXARGS 16

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

enum {
	DO_SETUP = 1,
	DO_JOIN,
	DO_LEAVE,
	DO_STOP_DONE,
	DO_START_DONE,
	DO_SEND,
	DO_GET_GROUPS,
	DO_GET_GROUP,
	DO_DUMP,
	DO_LOG,
	DO_GET_VERSION,
};

int get_action(char *buf)
{
	char act[16];
	int i;

	memset(act, 0, 16);

	for (i = 0; i < 16; i++) {
		if (isalnum(buf[i]) || ispunct(buf[i]))
			act[i] = buf[i];
		else
			break;
	}

	/* for debug message */
	memset(&last_action, 0, 16);
	memcpy(last_action, act, 16);

	if (!strncmp(act, "setup", 16))
		return DO_SETUP;

	if (!strncmp(act, "join", 16))
		return DO_JOIN;

	if (!strncmp(act, "leave", 16))
		return DO_LEAVE;

	if (!strncmp(act, "stop_done", 16))
		return DO_STOP_DONE;

	if (!strncmp(act, "start_done", 16))
		return DO_START_DONE;

	if (!strncmp(act, "send", 16))
		return DO_SEND;

	if (!strncmp(act, "get_groups", 16))
		return DO_GET_GROUPS;

	if (!strncmp(act, "get_group", 16))
		return DO_GET_GROUP;

	if (!strncmp(act, "get_version", 16))
		return DO_GET_VERSION;

	if (!strncmp(act, "dump", 16))
		return DO_DUMP;

	if (!strncmp(act, "log", 16))
		return DO_LOG;

	return -1;
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
			log_print("can't alloc for pollfd");
	}
	if (!client || !pollfd)
		log_print("can't alloc for client array");

	for (i = client_size; i < client_size + CLIENT_NALLOC; i++) {
		client[i].workfn = NULL;
		client[i].deadfn = NULL;
		client[i].fd = -1;
		client[i].level = -1;
		memset(client[i].type, 0, sizeof(client[i].type));
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

static void do_setup(int ci, int argc, char **argv)
{
	log_debug("setup %s %s", argv[1], argv[2]);

	strcpy(client[ci].type, argv[1]);
	client[ci].level = atoi(argv[2]);
}

static void copy_group_data(group_t *g, group_data_t *data)
{
	node_t *node;
	event_t *ev;
	int i = 0;

	strncpy(data->client_name, client[g->app->client].type, 32);
	strncpy(data->name, g->name, MAX_GROUP_NAME_LEN);
	data->level = g->level;
	data->id = g->global_id;

	if (g->app && g->app->current_event) {
		event_t *ev = g->app->current_event;
		data->event_state = ev->state;
		data->event_nodeid = ev->nodeid;
		data->event_id = ev->id;
		data->event_local_status = -2;

		node = find_app_node(g->app, our_nodeid);
		if (node) {
			if (event_state_stopping(g->app))
				data->event_local_status = node->stopped;
			else if (event_state_starting(g->app))
				data->event_local_status = node->started;
			else
				data->event_local_status = -1;
		}
	}

	data->member_count = g->app->node_count;
	list_for_each_entry(node, &g->app->nodes, list) {
		data->members[i] = node->nodeid;
		i++;

		if (node->nodeid == our_nodeid)
			data->member = 1;
	}

	/* we're in the member list but are still joining */
	if (data->member) {
		ev = g->app->current_event;
		if (ev && is_our_join(ev) &&
		    (ev->state <= EST_JOIN_ALL_STARTED))
			data->member = 0;
	}
}

static int do_get_groups(int ci, int argc, char **argv)
{
	group_t *g;
	group_data_t *data;
	int rv, count = 0, max = atoi(argv[1]);

	data = malloc(sizeof(group_data_t));
	count = 0;

	list_for_each_entry(g, &gd_groups, list) {
		memset(data, 0, sizeof(group_data_t));
		copy_group_data(g, data);
		rv = do_write(client[ci].fd, data, sizeof(group_data_t));
		if (rv < 0) {
			log_print("do_get_groups write error");
			break;
		}
		count++;
		if (count >= max)
			break;
	}
	/* Now write an empty one indicating there aren't anymore: */
	memset(data, 0, sizeof(group_data_t));
	rv = do_write(client[ci].fd, data, sizeof(group_data_t));
	free(data);
	return 0;
}

static int do_get_group(int ci, int argc, char **argv)
{
	group_t *g;
	group_data_t data;
	int rv;

	memset(&data, 0, sizeof(data));

	/* special case to get members of groupd cpg */
	if (atoi(argv[1]) == -1 && !strncmp(argv[2], "groupd", 6)) {
		copy_groupd_data(&data);
		goto out;
	}

	g = find_group_level(argv[2], atoi(argv[1]));
	if (!g)
		goto out;

	copy_group_data(g, &data);
 out:
	rv = do_write(client[ci].fd, &data, sizeof(data));
	if (rv < 0)
		log_print("do_get_group write error");

	return 0;
}

static int do_get_version(int ci)
{
	int mode;
	int rv;

	if (group_mode == GROUP_PENDING)
		mode = -EAGAIN;
	else
		mode = group_mode;

	rv = do_write(client[ci].fd, &mode, sizeof(mode));
	if (rv < 0)
		log_print("do_get_version write error");

	return 0;
}

static int do_dump(int fd)
{
	int len;

	if (dump_wrap) {
		len = GROUPD_DUMP_SIZE - dump_point;
		do_write(fd, dump_buf + dump_point, len);
		len = dump_point;
	} else
		len = dump_point;

	/* NUL terminate the debug string */
	dump_buf[dump_point] = '\0';

	do_write(fd, dump_buf, len);

	return 0;
}

static int do_log(int fd, const char *comment)
{
	log_print("%s", comment);
	return 0;
}

static void do_send(char *name, int level, int len, char *data)
{
	group_t *g;
	msg_t *msg;
	char *buf;
	int total;

	g = find_group_level(name, level);
	if (!g)
		return;

	total = sizeof(msg_t) + len;
	buf = malloc(total);
	memset(buf, 0, total);

	memcpy(buf + sizeof(msg_t), data, len);

	msg = (msg_t *) buf;
	msg->ms_type = MSG_APP_INTERNAL;
	msg->ms_global_id = g->global_id;

	log_debug("%d:%s do_send %d bytes", level, name, total);

	send_message(g, msg, total);

	free(buf);
}

static void process_connection(int ci)
{
	char buf[GROUPD_MSGLEN], *argv[MAXARGS], *p;
	int argc = 0, rv, act;

	memset(buf, 0, sizeof(buf));
	memset(argv, 0, sizeof(char *) * MAXARGS);

	rv = do_read(client[ci].fd, buf, GROUPD_MSGLEN);
	if (rv < 0) {
		client_dead(ci);
		return;
	}

	act = get_action(buf);

	log_debug("got client %d %s", ci, last_action);

	switch (act) {

	case DO_SETUP:
		get_args(buf, &argc, argv, ' ', 3);
		do_setup(ci, argc, argv);
		break;

	case DO_JOIN:
		get_args(buf, &argc, argv, ' ', 2);
		do_join(argv[1], client[ci].level, ci);
		break;

	case DO_LEAVE:
		get_args(buf, &argc, argv, ' ', 2);
		do_leave(argv[1], client[ci].level);
		break;

	case DO_STOP_DONE:
		get_args(buf, &argc, argv, ' ', 2);
		do_stopdone(argv[1], client[ci].level);
		break;

	case DO_START_DONE:
		get_args(buf, &argc, argv, ' ', 3);
		do_startdone(argv[1], client[ci].level, atoi(argv[2]));
		break;

	case DO_SEND:
		p = get_args(buf, &argc, argv, ' ', 3);
		do_send(argv[1], client[ci].level, atoi(argv[2]), p);
		break;

	case DO_GET_GROUPS:
		get_args(buf, &argc, argv, ' ', 2);
		do_get_groups(ci, argc, argv);
		break;

	case DO_GET_GROUP:
		get_args(buf, &argc, argv, ' ', 3);
		do_get_group(ci, argc, argv);
		break;

	case DO_GET_VERSION:
		do_get_version(ci);
		break;

	case DO_DUMP:
		do_dump(client[ci].fd);
		close(client[ci].fd);
		break;

	case DO_LOG:
		do_log(client[ci].fd, &buf[4]);
		break;

	default:
		log_print("unknown action %d client %d", act, ci);
		log_print("invalid message: \"%s\"", buf);
	}
}

static void process_listener(int ci)
{
	int fd, i;

	fd = accept(client[ci].fd, NULL, NULL);
	if (fd < 0) {
		log_print("process_listener: accept error %d %d", fd, errno);
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
		log_print("socket error %d %d", s, errno);
		return s;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_LOCAL;
	strcpy(&addr.sun_path[1], sock_path);
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

void cluster_dead(int ci)
{
	log_print("cluster is down, exiting");
	daemon_quit = 1;
}

#define min(x, y) ({                            \
	typeof(x) _min1 = (x);                  \
	typeof(y) _min2 = (y);                  \
	(void) (&_min1 == &_min2);              \
	_min1 < _min2 ? _min1 : _min2; })

static void loop(void)
{
	int poll_timeout = -1;	
	int rv, i;
	void (*workfn) (int ci);
	void (*deadfn) (int ci);

	rv = setup_listener(GROUPD_SOCK_PATH);
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

	if (cfgd_groupd_compat == 0)
		group_mode = GROUP_LIBCPG;
	else if (cfgd_groupd_compat == 1)
		group_mode = GROUP_LIBGROUP;
	else if (cfgd_groupd_compat == 2)
		group_mode = GROUP_PENDING;

	rv = setup_cpg();
	if (rv < 0)
		goto out;

	for (;;) {
		rv = poll(pollfd, client_maxi + 1, poll_timeout);
		if (rv == -1 && errno == EINTR) {
			if (daemon_quit && (group_mode == GROUP_LIBCPG ||
					    list_empty(&gd_groups)))
				goto out;
			daemon_quit = 0;
			continue;
		}
		if (rv < 0) {
			log_print("poll errno %d", errno);
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

		if (daemon_quit)
			break;

		/* process_apps() returns non-zero if there may be
		   more work to do */

		do {
			rv = 0;
			rv += process_apps();
		} while (rv);

		poll_timeout = -1;

		if (group_mode == GROUP_PENDING) {
			group_mode_check_timeout();
			poll_timeout = 1000 * min(cfgd_groupd_wait, cfgd_groupd_mode_delay);
		}
	}
 out:
	close_ccs();
	close_cman();

	if (!list_empty(&gd_groups))
		log_print("groups abandoned");
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

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("groupd [options]\n");
	printf("\n");
	printf("Options:\n");
	printf("\n");
	printf("  -D	       Enable debugging code and don't fork\n");
	printf("  -L <num>     Enable (1) or disable (0) debugging to logsys (default %d)\n", DEFAULT_DEBUG_LOGSYS);
	printf("  -g <num>     group compatibility mode, 0 off, 1 on, 2 detect\n");
	printf("               0: use libcpg, no backward compat, best performance\n");
	printf("               1: use libgroup for compat with cluster2/rhel5\n");
	printf("               2: detect old, or mode 1, nodes that require compat,\n"
	       "               use libcpg if none found\n");
	printf("               Default is %d\n", DEFAULT_GROUPD_COMPAT);
	printf("  -w <secs>    seconds to wait for a node's version message before\n"
	       "               assuming an old version requiring compat mode\n");
	printf("               Default is %d\n", DEFAULT_GROUPD_WAIT);
	printf("  -d <secs>    seconds to delay the mode selection to give time\n"
	       "               for an old version to join and force compat mode\n");
	printf("               Default is %d\n", DEFAULT_GROUPD_MODE_DELAY);
	printf("  -h	       Print this help, then exit\n");
	printf("  -V	       Print program version information, then exit\n");
}

#define OPTION_STRING "L:Dg:w:d:hVv"

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

		case 'w':
			optd_groupd_wait = 1;
			cfgd_groupd_wait = atoi(optarg);
			break;

		case 'd':
			optd_groupd_mode_delay = 1;
			cfgd_groupd_mode_delay = atoi(optarg);
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'v':
			daemon_debug_verbose++;
			break;

		case 'V':
			printf("groupd %s (built %s %s)\n",
				RELEASE_VERSION, __DATE__, __TIME__);
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
			fprintf(stderr, "unknown option: %c\n", optchar);
			exit(EXIT_FAILURE);
			break;
		};
	}

	if (!optd_debug_logsys && getenv("GROUPD_DEBUG")) {
		optd_debug_logsys = 1;
		cfgd_debug_logsys = atoi(getenv("GROUPD_DEBUG"));
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

static void set_scheduler(void)
{
	struct sched_param sched_param;
	int rv;

	rv = sched_get_priority_max(SCHED_RR);
	if (rv != -1) {
		sched_param.sched_priority = rv;
		rv = sched_setscheduler(0, SCHED_RR, &sched_param);
		if (rv == -1)
			log_print("could not set SCHED_RR priority %d err %d",
				   sched_param.sched_priority, errno);
	} else {
		log_print("could not get maximum scheduler priority err %d",
			  errno);
	}
}

int main(int argc, char *argv[])
{
	int i;

	INIT_LIST_HEAD(&recovery_sets);
	INIT_LIST_HEAD(&gd_groups);
	for (i = 0; i < MAX_LEVELS; i++)
		INIT_LIST_HEAD(&gd_levels[i]);

	init_logging();

	read_arguments(argc, argv);

	lockfile();

	if (!daemon_debug_opt) {
		if (daemon(0, 0) < 0) {
			perror("daemon error");
			exit(EXIT_FAILURE);
		}
	}
	signal(SIGTERM, sigterm_handler);

	set_scheduler();
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

		if (dump_point == GROUPD_DUMP_SIZE) {
			dump_point = 0;
			dump_wrap = 1;
		}
	}
}

int daemon_debug_opt;
int daemon_debug_verbose;
int daemon_quit;
int cman_quorate;
int our_nodeid;
char *our_name;
char daemon_debug_buf[256];
char dump_buf[GROUPD_DUMP_SIZE];
int dump_point;
int dump_wrap;
struct list_head gd_groups;
struct list_head gd_levels[MAX_LEVELS];
uint32_t gd_event_nr;
int group_mode;

int optd_groupd_compat;
int optd_groupd_wait;
int optd_groupd_mode_delay;
int optd_debug_logsys;

int cfgd_groupd_compat     = DEFAULT_GROUPD_COMPAT;
int cfgd_groupd_wait       = DEFAULT_GROUPD_WAIT;
int cfgd_groupd_mode_delay = DEFAULT_GROUPD_MODE_DELAY;
int cfgd_debug_logsys      = DEFAULT_DEBUG_LOGSYS;

