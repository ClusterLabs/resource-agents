/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005-2008 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "dlm_daemon.h"
#include "config.h"
#include <pthread.h>
#include <linux/dlmconstants.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>
#include <linux/dlm_netlink.h>

#define LOCKFILE_NAME	"/var/run/dlm_controld.pid"
#define CLIENT_NALLOC	32
#define GROUP_LIBGROUP	2
#define GROUP_LIBCPG	3

static int client_maxi;
static int client_size = 0;
static struct client *client = NULL;
static struct pollfd *pollfd = NULL;
static int group_mode;
static pthread_t query_thread;
static pthread_mutex_t query_mutex;

struct client {
	int fd;
	void *workfn;
	void *deadfn;
	struct lockspace *ls;
};

int do_read(int fd, void *buf, size_t count)
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

int do_write(int fd, void *buf, size_t count)
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

int client_fd(int ci)
{
	return client[ci].fd;
}

void client_ignore(int ci, int fd)
{
	pollfd[ci].fd = -1;
	pollfd[ci].events = 0;
}

void client_back(int ci, int fd)
{
	pollfd[ci].fd = fd;
	pollfd[ci].events = POLLIN;
}

static void sigterm_handler(int sig)
{
	daemon_quit = 1;
}

static struct lockspace *create_ls(char *name)
{
	struct lockspace *ls;

	ls = malloc(sizeof(*ls));
	if (!ls)
		goto out;
	memset(ls, 0, sizeof(*ls));
	strncpy(ls->name, name, DLM_LOCKSPACE_LEN);

	INIT_LIST_HEAD(&ls->changes);
	INIT_LIST_HEAD(&ls->node_history);
	INIT_LIST_HEAD(&ls->saved_messages);
	INIT_LIST_HEAD(&ls->plock_resources);
	INIT_LIST_HEAD(&ls->deadlk_nodes);
	INIT_LIST_HEAD(&ls->transactions);
	INIT_LIST_HEAD(&ls->resources);
 out:
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

struct lockspace *find_ls_id(uint32_t id)
{
	struct lockspace *ls;

	list_for_each_entry(ls, &lockspaces, list) {
		if (ls->global_id == id)
			return ls;
	}
	return NULL;
}

#define MAXARGS 8

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

char *dlm_mode_str(int mode)
{
	switch (mode) {
	case DLM_LOCK_IV:
		return "IV";
	case DLM_LOCK_NL:
		return "NL";
	case DLM_LOCK_CR:
		return "CR";
	case DLM_LOCK_CW:
		return "CW";
	case DLM_LOCK_PR:
		return "PR";
	case DLM_LOCK_PW:
		return "PW";
	case DLM_LOCK_EX:
		return "EX";
	}
	return "??";
}

/* recv "online" (join) and "offline" (leave) 
   messages from dlm via uevents and pass them on to groupd */

static void process_uevent(int ci)
{
	struct lockspace *ls;
	char buf[MAXLINE];
	char *argv[MAXARGS], *act, *sys;
	int rv, argc = 0;

	memset(buf, 0, sizeof(buf));
	memset(argv, 0, sizeof(char *) * MAXARGS);

 retry_recv:
	rv = recv(client[ci].fd, &buf, sizeof(buf), 0);
	if (rv == -1 && rv == EINTR)
		goto retry_recv;
	if (rv == -1 && rv == EAGAIN)
		return;
	if (rv < 0) {
		log_error("uevent recv error %d errno %d", rv, errno);
		return;
	}

	if (!strstr(buf, "dlm"))
		return;

	log_debug("uevent: %s", buf);

	get_args(buf, &argc, argv, '/', 4);
	if (argc != 4)
		log_error("uevent message has %d args", argc);
	act = argv[0];
	sys = argv[2];

	if ((strlen(sys) != strlen("dlm")) || strcmp(sys, "dlm"))
		return;

	log_debug("kernel: %s %s", act, argv[3]);

	rv = 0;

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

		if (group_mode == GROUP_LIBGROUP)
			rv = dlm_join_lockspace_group(ls);
		else
			rv = dlm_join_lockspace(ls);
		if (rv) {
			/* ls already freed */
			goto out;
		}

	} else if (!strcmp(act, "offline@")) {
		ls = find_ls(argv[3]);
		if (!ls) {
			rv = -ENOENT;
			goto out;
		}

		if (group_mode == GROUP_LIBGROUP)
			dlm_leave_lockspace_group(ls);
		else
			dlm_leave_lockspace(ls);
	}
 out:
	if (rv < 0)
		log_error("process_uevent %s error %d errno %d",
			  act, rv, errno);
}

static int setup_uevent(void)
{
	struct sockaddr_nl snl;
	int s, rv;

	s = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
	if (s < 0) {
		log_error("uevent netlink socket");
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

static void init_header(struct dlmc_header *h, int cmd, char *name, int result,
			int extra_len)
{
	memset(h, 0, sizeof(struct dlmc_header));

	h->magic = DLMC_MAGIC;
	h->version = DLMC_VERSION;
	h->len = sizeof(struct dlmc_header) + extra_len;
	h->command = cmd;
	h->data = result;

	if (name)
		strncpy(h->name, name, DLM_LOCKSPACE_LEN);
}

static void query_dump_debug(int fd)
{
	struct dlmc_header h;
	int extra_len;
	int len;

	/* in the case of dump_wrap, extra_len will go in two writes,
	   first the log tail, then the log head */
	if (dump_wrap)
		extra_len = DLMC_DUMP_SIZE;
	else
		extra_len = dump_point;

	init_header(&h, DLMC_CMD_DUMP_DEBUG, NULL, 0, extra_len);
	do_write(fd, &h, sizeof(h));

	if (dump_wrap) {
		len = DLMC_DUMP_SIZE - dump_point;
		do_write(fd, dump_buf + dump_point, len);
		len = dump_point;
	} else
		len = dump_point;

	/* NUL terminate the debug string */
	dump_buf[dump_point] = '\0';

	do_write(fd, dump_buf, len);
}

static void query_dump_plocks(int fd, char *name)
{
	struct lockspace *ls;
	struct dlmc_header h;
	int rv;

	ls = find_ls(name);
	if (!ls) {
		plock_dump_len = 0;
		rv = -ENOENT;
	} else {
		/* writes to plock_dump_buf and sets plock_dump_len */
		rv = fill_plock_dump_buf(ls);
	}

	init_header(&h, DLMC_CMD_DUMP_PLOCKS, name, rv, plock_dump_len);

	do_write(fd, &h, sizeof(h));

	if (plock_dump_len)
		do_write(fd, plock_dump_buf, plock_dump_len);
}

/* combines a header and the data and sends it back to the client in
   a single do_write() call */

static void do_reply(int fd, int cmd, char *name, int result, char *buf,
		     int buflen)
{
	char *reply;
	int reply_len;

	reply_len = sizeof(struct dlmc_header) + buflen;
	reply = malloc(reply_len);
	if (!reply)
		return;
	memset(reply, 0, reply_len);

	init_header((struct dlmc_header *)reply, cmd, name, result, buflen);

	if (buf && buflen)
		memcpy(reply + sizeof(struct dlmc_header), buf, buflen);

	do_write(fd, reply, reply_len);

	free(reply);
}

static void query_lockspace_info(int fd, char *name)
{
	struct lockspace *ls;
	struct dlmc_lockspace lockspace;
	int rv;

	ls = find_ls(name);
	if (!ls) {
		rv = -ENOENT;
		goto out;
	}

	if (group_mode == GROUP_LIBGROUP)
		rv = set_lockspace_info_group(ls, &lockspace);
	else
		rv = set_lockspace_info(ls, &lockspace);
 out:
	do_reply(fd, DLMC_CMD_LOCKSPACE_INFO, name, rv,
		 (char *)&lockspace, sizeof(lockspace));
}

static void query_node_info(int fd, char *name, int nodeid)
{
	struct lockspace *ls;
	struct dlmc_node node;
	int rv;

	ls = find_ls(name);
	if (!ls) {
		rv = -ENOENT;
		goto out;
	}

	if (group_mode == GROUP_LIBGROUP)
		rv = set_node_info_group(ls, nodeid, &node);
	else
		rv = set_node_info(ls, nodeid, &node);
 out:
	do_reply(fd, DLMC_CMD_NODE_INFO, name, rv,
		 (char *)&node, sizeof(node));
}

static void query_lockspaces(int fd, int max)
{
	int ls_count = 0;
	struct dlmc_lockspace *lss = NULL;
	int rv, result;

	if (group_mode == GROUP_LIBGROUP)
		rv = set_lockspaces_group(&ls_count, &lss);
	else
		rv = set_lockspaces(&ls_count, &lss);

	if (rv < 0) {
		result = rv;
		ls_count = 0;
		goto out;
	}

	if (ls_count > max) {
		result = -E2BIG;
		ls_count = max;
	} else {
		result = ls_count;
	}
 out:
	do_reply(fd, DLMC_CMD_LOCKSPACES, NULL, result,
		 (char *)lss, ls_count * sizeof(struct dlmc_lockspace));

	if (lss)
		free(lss);
}

static void query_lockspace_nodes(int fd, char *name, int option, int max)
{
	struct lockspace *ls;
	int node_count = 0;
	struct dlmc_node *nodes = NULL;
	int rv, result;

	ls = find_ls(name);
	if (!ls) {
		result = -ENOENT;
		node_count = 0;
		goto out;
	}

	if (group_mode == GROUP_LIBGROUP)
		rv = set_lockspace_nodes_group(ls, option, &node_count, &nodes);
	else
		rv = set_lockspace_nodes(ls, option, &node_count, &nodes);

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
	do_reply(fd, DLMC_CMD_LOCKSPACE_NODES, name, result,
		 (char *)nodes, node_count * sizeof(struct dlmc_node));

	if (nodes)
		free(nodes);
}

static void process_connection(int ci)
{
	struct dlmc_header h;
	char *extra = NULL;
	int rv, extra_len;
	struct lockspace *ls;

	rv = do_read(client[ci].fd, &h, sizeof(h));
	if (rv < 0) {
		log_debug("connection %d read error %d", ci, rv);
		goto out;
	}

	if (h.magic != DLMC_MAGIC) {
		log_debug("connection %d magic error %x", ci, h.magic);
		goto out;
	}

	if ((h.version & 0xFFFF0000) != (DLMC_VERSION & 0xFFFF0000)) {
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
	case DLMC_CMD_FS_REGISTER:
		ls = find_ls(h.name);
		if (group_mode == GROUP_LIBGROUP) {
			rv = -EINVAL;
		} else if (!ls) {
			rv = -ENOENT;
		} else {
			ls->fs_registered = 1;
			rv = 0;
		}
		do_reply(client[ci].fd, DLMC_CMD_FS_REGISTER, h.name, rv,
			 NULL, 0);
		break;

	case DLMC_CMD_FS_UNREGISTER:
		ls = find_ls(h.name);
		if (ls)
			ls->fs_registered = 0;
		break;

	case DLMC_CMD_FS_NOTIFIED:
		ls = find_ls(h.name);
		if (ls)
			rv = set_fs_notified(ls, h.data);
		else
			rv = -ENOENT;
		do_reply(client[ci].fd, DLMC_CMD_FS_NOTIFIED, h.name, rv,
			 NULL, 0);
		break;

	case DLMC_CMD_DEADLOCK_CHECK:
		ls = find_ls(h.name);
		if (ls)
			send_cycle_start(ls);
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
   We need a thread to process queries because the main thread may block
   for long periods when writing to sysfs to stop dlm-kernel (any maybe
   other places). */

static void *process_queries(void *arg)
{
	struct dlmc_header h;
	int s = *((int *)arg);
	int f, rv;

	for (;;) {
		f = accept(s, NULL, NULL);

		rv = do_read(f, &h, sizeof(h));
		if (rv < 0) {
			goto out;
		}

		if (h.magic != DLMC_MAGIC) {
			goto out;
		}

		if ((h.version & 0xFFFF0000) != (DLMC_VERSION & 0xFFFF0000)) {
			goto out;
		}

		query_lock();

		switch (h.command) {
		case DLMC_CMD_DUMP_DEBUG:
			query_dump_debug(f);
			break;
		case DLMC_CMD_DUMP_PLOCKS:
			query_dump_plocks(f, h.name);
			break;
		case DLMC_CMD_LOCKSPACE_INFO:
			query_lockspace_info(f, h.name);
			break;
		case DLMC_CMD_NODE_INFO:
			query_node_info(f, h.name, h.data);
			break;
		case DLMC_CMD_LOCKSPACES:
			query_lockspaces(f, h.data);
			break;
		case DLMC_CMD_LOCKSPACE_NODES:
			query_lockspace_nodes(f, h.name, h.option, h.data);
			break;
		default:
			break;
		}
		query_unlock();

 out:
		close(f);
	}
}

static int setup_queries(void)
{
	int rv, s;

	rv = setup_listener(DLMC_QUERY_SOCK_PATH);
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

static void cluster_dead(int ci)
{
	log_error("cluster is down, exiting");
	clear_configfs();
	exit(1);
}

static int loop(void)
{
	int poll_timeout = -1;
	int rv, i;
	void (*workfn) (int ci);
	void (*deadfn) (int ci);

	rv = setup_queries();
	if (rv < 0)
		goto out;

	rv = setup_listener(DLMC_SOCK_PATH);
	if (rv < 0)
		goto out;
	client_add(rv, process_listener, NULL);

	rv = setup_uevent();
	if (rv < 0)
		goto out;
	client_add(rv, process_uevent, NULL);

	rv = setup_cman();
	if (rv < 0)
		goto out;
	client_add(rv, process_cman, cluster_dead);

	group_mode = GROUP_LIBCPG;

	if (cfgd_groupd_compat) {
		rv = setup_groupd();
		if (rv < 0)
			goto out;
		client_add(rv, process_groupd, cluster_dead);

		group_mode = GROUP_LIBGROUP;

		if (cfgd_groupd_compat == 2) {
			/* cfgd_groupd_compat of 2 uses new groupd feature that
			   figures out whether all other groupd's in the cluster
			   are in LIGCPG mode, and if they are group_mode is
			   changed to LIBCPG.  If any groupd in the cluster
			   is from cluster2/stable2/rhel5, or any groupd is
			   in LIBGROUP mode, then group_mode remains LIBGROUP.

			   set_group_mode() figures this out by joining the
			   groupd cpg, sending a new "mode" message, and
			   waiting to see if it gets a mode reply from
			   all other groupd's.  If it does, and all modes
			   are LIGCPG, then we set groupd_mode to LIBCPG.
			   Any previous generation groupd's won't recognize
			   the new mode message and won't reply; their lack
			   of reply (or seeing an old-style message from them)
			   indicates they are a cluster2 version.

			   Not yet implemented.  In the future, it may set
			   group_mode to GROUP_LIBCPG. */

			/* set_group_mode(); */
			group_mode = GROUP_LIBGROUP;
		}
	}

	if (group_mode == GROUP_LIBCPG) {
		rv = setup_cpg();
		if (rv < 0)
			goto out;
		/* client_add(rv, process_cpg, cluster_dead); */

		if (cfgd_enable_deadlk) {
			rv = setup_netlink();
			if (rv < 0)
				goto out;
			client_add(rv, process_netlink, NULL);

			setup_deadlock();
		}

		rv = setup_plocks();
		if (rv < 0)
			goto out;
		plock_fd = rv;
		plock_ci = client_add(rv, process_plocks, NULL);
	}

	for (;;) {
		rv = poll(pollfd, client_maxi + 1, poll_timeout);
		if (rv == -1 && errno == EINTR) {
			if (daemon_quit && list_empty(&lockspaces)) {
				clear_configfs();
				exit(1);
			}
			daemon_quit = 0;
			continue;
		}
		if (rv < 0) {
			log_error("poll errno %d", errno);
			goto out;
		}

		/* FIXME: lock/unlock around operations that take a while */
		query_lock();

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

		poll_timeout = -1;

		if (poll_fencing || poll_quorum || poll_fs) {
			process_lockspace_changes();
			poll_timeout = 1000;
		}

		if (poll_ignore_plock) {
			if (!limit_plocks()) {
				poll_ignore_plock = 0;
				client_back(plock_ci, plock_fd);
			}
			poll_timeout = 1000;
		}

		query_unlock();
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

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("dlm_controld [options]\n");
	printf("\n");
	printf("Options:\n");
	printf("\n");
	printf("  -D		Enable daemon debugging and don't fork\n");
	printf("  -K		Enable kernel dlm debugging messages\n");
	printf("  -g <num>	groupd compatibility, 0 off, 1 on\n");
	printf("		on: use libgroup, compat with cluster2/stable2/rhel5\n");
	printf("		off: use libcpg, no backward compatability\n");
	printf("		Default is %d\n", DEFAULT_GROUPD_COMPAT);
	printf("  -f <num>	Enable (1) or disable (0) fencing recovery dependency\n");
	printf("		Default is %d\n", DEFAULT_ENABLE_FENCING);
	printf("  -q <num>	Enable (1) or disable (0) quorum recovery dependency\n");
	printf("		Default is %d\n", DEFAULT_ENABLE_QUORUM);
	printf("  -d <num>	Enable (1) or disable (0) deadlock detection code\n");
	printf("		Default is %d\n", DEFAULT_ENABLE_DEADLK);
	printf("  -p <num>	Enable (1) or disable (0) plock code for cluster fs\n");
	printf("		Default is %d\n", DEFAULT_ENABLE_PLOCK);
	printf("  -P		Enable plock debugging\n");
	printf("  -l <limit>	Limit the rate of plock operations\n");
	printf("		Default is %d, set to 0 for no limit\n", DEFAULT_PLOCK_RATE_LIMIT);
	printf("  -o <n>	Enable (1) or disable (0) plock ownership\n");
	printf("		Default is %d\n", DEFAULT_PLOCK_OWNERSHIP);
	printf("  -t <ms>	plock ownership drop resources time (milliseconds)\n");
	printf("		Default is %u\n", DEFAULT_DROP_RESOURCES_TIME);
	printf("  -c <num>	plock ownership drop resources count\n");
	printf("		Default is %u\n", DEFAULT_DROP_RESOURCES_COUNT);
	printf("  -a <ms>	plock ownership drop resources age (milliseconds)\n");
	printf("		Default is %u\n", DEFAULT_DROP_RESOURCES_AGE);
	printf("  -h		Print this help, then exit\n");
	printf("  -V		Print program version information, then exit\n");
}

#define OPTION_STRING			"DKg:f:q:d:p:Pl:o:t:c:a:hV"

static void read_arguments(int argc, char **argv)
{
	int cont = 1;
	int optchar;

	/* we don't allow these to be set on command line, should we? */
	optk_timewarn = 0;
	optk_timewarn = 0;

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {

		case 'D':
			daemon_debug_opt = 1;
			break;

		case 'g':
			optd_groupd_compat = 1;
			cfgd_groupd_compat = atoi(optarg);
			break;

		case 'K':
			optk_debug = 1;
			cfgk_debug = 1;
			break;

		case 'f':
			optd_enable_fencing = 1;
			cfgd_enable_fencing = atoi(optarg);
			break;

		case 'q':
			optd_enable_quorum = 1;
			cfgd_enable_quorum = atoi(optarg);
			break;

		case 'd':
			optd_enable_deadlk = 1;
			cfgd_enable_deadlk = atoi(optarg);
			break;

		case 'p':
			optd_enable_plock = 1;
			cfgd_enable_plock = atoi(optarg);
			break;

		case 'P':
			optd_plock_debug = 1;
			cfgd_plock_debug = 1;
			break;

		case 'l':
			optd_plock_rate_limit = 1;
			cfgd_plock_rate_limit = atoi(optarg);
			break;

		case 'o':
			optd_plock_ownership = 1;
			cfgd_plock_ownership = atoi(optarg);
			break;

		case 't':
			optd_drop_resources_time = 1;
			cfgd_drop_resources_time = atoi(optarg);
			break;

		case 'c':
			optd_drop_resources_count = 1;
			cfgd_drop_resources_count = atoi(optarg);
			break;

		case 'a':
			optd_drop_resources_age = 1;
			cfgd_drop_resources_age = atoi(optarg);
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'V':
			printf("dlm_controld %s (built %s %s)\n",
				RELEASE_VERSION, __DATE__, __TIME__);
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
			log_error("could not set SCHED_RR priority %d err %d",
				   sched_param.sched_priority, errno);
	} else {
		log_error("could not get maximum scheduler priority err %d",
			  errno);
	}
}

int main(int argc, char **argv)
{
	INIT_LIST_HEAD(&lockspaces);

	read_arguments(argc, argv);

	lockfile();

	if (!daemon_debug_opt) {
		if (daemon(0, 0) < 0) {
			perror("daemon error");
			exit(EXIT_FAILURE);
		}
	}
	openlog("dlm_controld", LOG_PID, LOG_DAEMON);
	signal(SIGTERM, sigterm_handler);

	read_ccs();

	clear_configfs();

	/* the kernel has its own defaults for these values which we
	   don't want to change unless these have been set; -1 means
	   they have not been set on command line or config file */
	if (cfgk_debug != -1)
		set_configfs_debug(cfgk_debug);
	if (cfgk_timewarn != -1)
		set_configfs_timewarn(cfgk_timewarn);
	if (cfgk_protocol != -1)
		set_configfs_protocol(cfgk_protocol);

	set_scheduler();
	set_oom_adj(-16);

	return loop();
}

void daemon_dump_save(void)
{
	int len, i;

	len = strlen(daemon_debug_buf);

	for (i = 0; i < len; i++) {
		dump_buf[dump_point++] = daemon_debug_buf[i];

		if (dump_point == DLMC_DUMP_SIZE) {
			dump_point = 0;
			dump_wrap = 1;
		}
	}
}

int daemon_debug_opt;
int daemon_quit;
int poll_fencing;
int poll_quorum;
int poll_fs;
int poll_ignore_plock;
int plock_fd;
int plock_ci;
struct list_head lockspaces;
int cman_quorate;
int our_nodeid;
char daemon_debug_buf[256];
char dump_buf[DLMC_DUMP_SIZE];
int dump_point;
int dump_wrap;
char plock_dump_buf[DLMC_DUMP_SIZE];
int plock_dump_len;

