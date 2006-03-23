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

extern struct list_head recovery_sets;

struct list_head        gd_groups;
struct list_head        gd_levels[MAX_LEVELS];
uint32_t		gd_event_nr;
int			our_nodeid;
int			cman_quorate;

static int client_maxi;
static int client_size;
static struct client *client;
static struct pollfd *pollfd;
static char last_action[16];

struct client {
	int fd;
	int level;
	char type[32];
	void *workfn;
	void *deadfn;
};


static void app_action(app_t *a, char *buf)
{
	int rv;

	log_group(a->g, "action for app: %s", buf);

	rv = write(client[a->client].fd, buf, GROUPD_MSGLEN);
	if (rv != GROUPD_MSGLEN)
		log_print("write error %d errno %d", rv, errno);
}

void app_deliver(app_t *a, struct save_msg *save)
{
	char buf[GROUPD_MSGLEN];
	int rv;

	rv = snprintf(buf, sizeof(buf), "deliver %s %d %d",
		      a->g->name, save->nodeid, save->msg_len - sizeof(msg_t));

	log_group(a->g, "deliver to app: %s", buf);

	memcpy(buf + rv + 1, save->msg_long + sizeof(msg_t),
	       save->msg_len - sizeof(msg_t));

	/*
	log_group(a->g, "app_deliver body len %d \"%s\"",
		  save->msg_len - sizeof(msg_t),
		  save->msg_long + sizeof(msg_t));
	*/

	rv = write(client[a->client].fd, buf, GROUPD_MSGLEN);
	if (rv != GROUPD_MSGLEN)
		log_print("write error %d errno %d", rv, errno);
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

	if (!strncmp(act, "dump", 16))
		return DO_DUMP;

        return -1;
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
		client[i].workfn = NULL;
		client[i].fd = -1;
		client[i].level = -1;
		memset(client[i].type, 0, sizeof(client[i].type));
	}
	client_size += NALLOC;
}

void client_dead(int ci)
{
	log_print("client %d fd %d dead", ci, client[ci].fd);
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

static void do_setup(int ci, int argc, char **argv)
{
	log_debug("setup %s %s", argv[1], argv[2]);

	strcpy(client[ci].type, argv[1]);
	client[ci].level = atoi(argv[2]);
}

static void copy_group_data(group_t *g, group_data_t *data)
{
	node_t *node;
	int i = 0;

	strncpy(data->client_name, client[g->app->client].type, 32);
	strncpy(data->name, g->name, MAX_GROUP_NAME_LEN);
	data->level = g->level;
	data->id = g->global_id;
	data->member_count = g->memb_count;

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

	list_for_each_entry(node, &g->memb, list) {
		data->members[i] = node->nodeid;
		i++;
	}
}

static int do_get_groups(int ci, int argc, char **argv)
{
	group_t *g;
	group_data_t *data;
	int i, rv, len, count = 0, max = atoi(argv[1]);

	list_for_each_entry(g, &gd_groups, list)
		count++;
	if (count > max)
		count = max;
	/* if no groups, send back one empty data struct */
	if (!count)
		count = 1;

	len = count * sizeof(group_data_t);
	data = malloc(len);
	memset(data, 0, len);

	i = 0;
	list_for_each_entry(g, &gd_groups, list) {
		copy_group_data(g, &data[i]);
		i++;
	}

	rv = write(client[ci].fd, data, len);
	if (rv != len)
		log_print("write error %d errno %d", rv, errno);

	free(data);
	return 0;
}

static int do_get_group(int ci, int argc, char **argv)
{
	group_t *g;
	group_data_t data;
	int rv;

	memset(&data, 0, sizeof(data));

	g = find_group_level(argv[2], atoi(argv[1]));
	if (!g)
		goto out;

	copy_group_data(g, &data);
 out:
	rv = write(client[ci].fd, &data, sizeof(data));
	if (rv != sizeof(data))
		log_print("write error %d errno %d", rv, errno);

	return 0;
}

static int do_dump(int ci, int argc, char **argv)
{
	int rv, len = DUMP_SIZE;

	if (dump_wrap) {
		len = DUMP_SIZE - dump_point;
		rv = write(client[ci].fd, dump_buf + dump_point, len);
		if (rv != len)
			log_print("write error %d errno %d", rv, errno);
		len = dump_point;
	}

	rv = write(client[ci].fd, dump_buf, len);
	if (rv != len)
		log_print("write error %d errno %d", rv, errno);
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

	log_print("%d:%s do_send %d bytes", level, name, total);

	send_message(g, msg, total);

	free(buf);
}

static void process_connection(int ci)
{
	char buf[GROUPD_MSGLEN], *argv[MAXARGS], *p;
	int argc = 0, rv, act;

	memset(buf, 0, sizeof(buf));

	rv = read(client[ci].fd, buf, GROUPD_MSGLEN);
	if (!rv) {
		client_dead(ci);
		return;
	}
	if (rv != GROUPD_MSGLEN) {
		log_print("client %d fd %d read error %d %d", ci,
			  client[ci].fd, rv, errno);
		return;
	}

	act = get_action(buf);

	log_debug("got %d bytes from client %d %s", rv, ci, last_action);

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
		get_args(buf, &argc, argv, ' ', 2);
		do_startdone(argv[1], client[ci].level);
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

	case DO_DUMP:
		do_dump(ci, argc, argv);
		break;

	default:
		log_print("unknown action %d client %d bytes %d", act, ci, rv);
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

	log_print("client connection %d", i);
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

	client_add(s, process_listener, NULL);

	return 0;
}

static int loop(void)
{
	int rv, fd, i, timeout = -1;
	void (*workfn) (int ci);
	void (*deadfn) (int ci);

	rv = setup_listener();
	if (rv < 0)
		return rv;

	rv = setup_cman();
	if (rv < 0)
		return rv;

	rv = setup_cpg();
	if (rv < 0)
		return rv;

	while (1) {
		rv = poll(pollfd, client_maxi + 1, timeout);
		if (rv < 0)
			log_print("poll error %d %d", rv, errno);

		for (i = 0; i <= client_maxi; i++) {
			if (client[i].fd < 0)
				continue;
			if (pollfd[i].revents & POLLHUP) {
				deadfn = client[i].deadfn;
				deadfn(i);
			} else if (pollfd[i].revents & POLLIN) {
				workfn = client[i].workfn;
				workfn(i);
			}
		}

		/* process_apps() returns non-zero if there may be
		   more work to do */

		do {
			rv = 0;
			rv += process_apps();
		} while (rv);
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
			groupd_debug_opt = 1;
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

int main(int argc, char *argv[])
{
	prog_name = argv[0];
	int i;

	INIT_LIST_HEAD(&recovery_sets);
	INIT_LIST_HEAD(&gd_groups);
	for (i = 0; i < MAX_LEVELS; i++)
		INIT_LIST_HEAD(&gd_levels[i]);

	decode_arguments(argc, argv);

	if (!groupd_debug_opt)
		daemonize();

	pollfd = malloc(MAXCON * sizeof(struct pollfd));
	if (!pollfd)
		return -1;

	return loop();
}

void groupd_dump_save(void)
{
	int len, i;

	len = strlen(groupd_debug_buf);

	for (i = 0; i < len; i++) {
		dump_buf[dump_point++] = groupd_debug_buf[i];

		if (dump_point == DUMP_SIZE) {
			dump_point = 0;
			dump_wrap = 1;
		}
	}
}

char *prog_name;
int groupd_debug_opt;
char groupd_debug_buf[256];
char dump_buf[DUMP_SIZE];
int dump_point;
int dump_wrap;

