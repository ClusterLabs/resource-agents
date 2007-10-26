/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005-2007 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "dlm_daemon.h"

#include <linux/netlink.h>
#include <linux/genetlink.h>
#include <linux/dlm.h>
#include <linux/dlm_netlink.h>

#define OPTION_STRING			"KDhVd:"
#define LOCKFILE_NAME			"/var/run/dlm_controld.pid"

#define DEADLOCK_CHECK_SECS		10

#define NALLOC 16

struct list_head lockspaces;

extern group_handle_t gh;
extern int deadlock_enabled;

static int daemon_quit;
static int client_maxi;
static int client_size = 0;
static struct client *client = NULL;
static struct pollfd *pollfd = NULL;

struct client {
	int fd;
	void *workfn;
	void *deadfn;
	struct lockspace *ls;
};

static void client_alloc(void)
{
	int i;

	if (!client) {
		client = malloc(NALLOC * sizeof(struct client));
		pollfd = malloc(NALLOC * sizeof(struct pollfd));
	} else {
		client = realloc(client, (client_size + NALLOC) *
					 sizeof(struct client));
		pollfd = realloc(pollfd, (client_size + NALLOC) *
					 sizeof(struct pollfd));
		if (!pollfd)
			log_error("can't alloc for pollfd");
	}
	if (!client || !pollfd)
		log_error("can't alloc for client array");

	for (i = client_size; i < client_size + NALLOC; i++) {
		client[i].workfn = NULL;
		client[i].deadfn = NULL;
		client[i].fd = -1;
		pollfd[i].fd = -1;
		pollfd[i].revents = 0;
	}
	client_size += NALLOC;
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

void set_client_lockspace(int ci, struct lockspace *ls)
{
	client[ci].ls = ls;
}

struct lockspace *get_client_lockspace(int ci)
{
	return client[ci].ls;
}

static void sigterm_handler(int sig)
{
	daemon_quit = 1;
}

struct lockspace *create_ls(char *name)
{
	struct lockspace *ls;

	ls = malloc(sizeof(*ls));
	if (!ls)
		goto out;
	memset(ls, 0, sizeof(*ls));
	strncpy(ls->name, name, MAXNAME);
	INIT_LIST_HEAD(&ls->transactions);
	INIT_LIST_HEAD(&ls->resources);
	INIT_LIST_HEAD(&ls->nodes);
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
		goto out;
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

		ls->joining = 1;
		list_add(&ls->list, &lockspaces);

		rv = group_join(gh, argv[3]);

	} else if (!strcmp(act, "offline@")) {
		ls = find_ls(argv[3]);
		if (!ls) {
			rv = -ENOENT;
			goto out;
		}

		rv = group_leave(gh, argv[3]);
	} else
		rv = 0;
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

/* FIXME: look into using libnl/libnetlink */

#define GENLMSG_DATA(glh)       ((void *)(NLMSG_DATA(glh) + GENL_HDRLEN))
#define GENLMSG_PAYLOAD(glh)    (NLMSG_PAYLOAD(glh, 0) - GENL_HDRLEN)
#define NLA_DATA(na)	    	((void *)((char*)(na) + NLA_HDRLEN))
#define NLA_PAYLOAD(len)	(len - NLA_HDRLEN)

/* Maximum size of response requested or message sent */
#define MAX_MSG_SIZE    1024

struct msgtemplate {
	struct nlmsghdr n;
	struct genlmsghdr g;
	char buf[MAX_MSG_SIZE];
};

static int send_genetlink_cmd(int sd, uint16_t nlmsg_type, uint32_t nlmsg_pid,
			      uint8_t genl_cmd, uint16_t nla_type,
			      void *nla_data, int nla_len)
{
	struct nlattr *na;
	struct sockaddr_nl nladdr;
	int r, buflen;
	char *buf;

	struct msgtemplate msg;

	msg.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	msg.n.nlmsg_type = nlmsg_type;
	msg.n.nlmsg_flags = NLM_F_REQUEST;
	msg.n.nlmsg_seq = 0;
	msg.n.nlmsg_pid = nlmsg_pid;
	msg.g.cmd = genl_cmd;
	msg.g.version = 0x1;
	na = (struct nlattr *) GENLMSG_DATA(&msg);
	na->nla_type = nla_type;
	na->nla_len = nla_len + 1 + NLA_HDRLEN;
	if (nla_data)
		memcpy(NLA_DATA(na), nla_data, nla_len);
	msg.n.nlmsg_len += NLMSG_ALIGN(na->nla_len);

	buf = (char *) &msg;
	buflen = msg.n.nlmsg_len ;
	memset(&nladdr, 0, sizeof(nladdr));
	nladdr.nl_family = AF_NETLINK;
	while ((r = sendto(sd, buf, buflen, 0, (struct sockaddr *) &nladdr,
			   sizeof(nladdr))) < buflen) {
		if (r > 0) {
			buf += r;
			buflen -= r;
		} else if (errno != EAGAIN)
			return -1;
	}
	return 0;
}

/*
 * Probe the controller in genetlink to find the family id
 * for the DLM family
 */
static int get_family_id(int sd)
{
	char genl_name[100];
	struct {
		struct nlmsghdr n;
		struct genlmsghdr g;
		char buf[256];
	} ans;

	int id, rc;
	struct nlattr *na;
	int rep_len;

	strcpy(genl_name, DLM_GENL_NAME);
	rc = send_genetlink_cmd(sd, GENL_ID_CTRL, getpid(), CTRL_CMD_GETFAMILY,
				CTRL_ATTR_FAMILY_NAME, (void *)genl_name,
				strlen(DLM_GENL_NAME)+1);

	rep_len = recv(sd, &ans, sizeof(ans), 0);
	if (ans.n.nlmsg_type == NLMSG_ERROR ||
	    (rep_len < 0) || !NLMSG_OK((&ans.n), rep_len))
		return 0;

	na = (struct nlattr *) GENLMSG_DATA(&ans);
	na = (struct nlattr *) ((char *) na + NLA_ALIGN(na->nla_len));
	if (na->nla_type == CTRL_ATTR_FAMILY_ID) {
		id = *(uint16_t *) NLA_DATA(na);
	}
	return id;
}

/* genetlink messages are timewarnings used as part of deadlock detection */

static int setup_netlink(void)
{
	struct sockaddr_nl snl;
	int s, rv;
	uint16_t id;

	s = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
	if (s < 0) {
		log_error("generic netlink socket");
		return s;
	}

	memset(&snl, 0, sizeof(snl));
	snl.nl_family = AF_NETLINK;

	rv = bind(s, (struct sockaddr *) &snl, sizeof(snl));
	if (rv < 0) {
		log_error("gen netlink bind error %d errno %d", rv, errno);
		close(s);
		return rv;
	}

	id = get_family_id(s);
	if (!id) {
		log_error("Error getting family id, errno %d", errno);
		close(s);
		return -1;
	}

	rv = send_genetlink_cmd(s, id, getpid(), DLM_CMD_HELLO, 0, NULL, 0);
	if (rv < 0) {
		log_error("error sending hello cmd, errno %d", errno);
		close(s);
		return -1;
	}

	return s;
}

static void process_timewarn(struct dlm_lock_data *data)
{
	struct lockspace *ls;
	struct timeval now;
	unsigned int sec;

	ls = find_ls_id(data->lockspace_id);
	if (!ls)
		return;

	data->resource_name[data->resource_namelen] = '\0';

	log_group(ls, "timewarn: lkid %x pid %d name %s",
		  data->id, data->ownpid, data->resource_name);

	/* Problem: we don't want to get a timewarn, assume it's resolved
	   by the current cycle, but in fact it's from a deadlock that
	   formed after the checkpoints for the current cycle.  Then we'd
	   have to hope for another warning (that may not come) to trigger
	   a new cycle to catch the deadlock.  If our last cycle ckpt
	   was say N (~5?) sec before we receive the timewarn, then we
	   can be confident that the cycle included the lock in question.
	   Otherwise, we're not sure if the warning is for a new deadlock
	   that's formed since our last cycle ckpt (unless it's a long
	   enough time since the last cycle that we're confident it *is*
	   a new deadlock).  When there is a deadlock, I suspect it will
	   be common to receive warnings before, during, and possibly
	   after the cycle that resolves it.  Wonder if we should record
	   timewarns and match them with deadlock cycles so we can tell
	   which timewarns are addressed by a given cycle and which aren't.  */


	gettimeofday(&now, NULL);

	/* don't send a new start until at least SECS after the last
	   we sent, and at least SECS after the last completed cycle */

	sec = now.tv_sec - ls->last_send_cycle_start.tv_sec;

	if (sec < DEADLOCK_CHECK_SECS) {
		log_group(ls, "skip send: recent send cycle %d sec", sec);
		return;
	}

	sec = now.tv_sec - ls->cycle_end_time.tv_sec;

	if (sec < DEADLOCK_CHECK_SECS) {
		log_group(ls, "skip send: recent cycle end %d sec", sec);
		return;
	}

	gettimeofday(&ls->last_send_cycle_start, NULL);
	send_cycle_start(ls);
}

static void process_netlink(int ci)
{
	struct msgtemplate msg;
	struct nlattr *na;
	int len;

	len = recv(client[ci].fd, &msg, sizeof(msg), 0);

	if (len < 0) {
		log_error("nonfatal netlink error: errno %d", errno);
		return;
	}

	if (msg.n.nlmsg_type == NLMSG_ERROR || !NLMSG_OK((&msg.n), len)) {
		struct nlmsgerr *err = NLMSG_DATA(&msg);
		log_error("fatal netlink error: errno %d", err->error);
		return;
	}

	na = (struct nlattr *) GENLMSG_DATA(&msg);

	process_timewarn((struct dlm_lock_data *) NLA_DATA(na));
}

static void process_connection(int ci)
{
	char buf[DLM_CONTROLD_MSGLEN], *argv[MAXARGS];
	int argc = 0, rv;
	struct lockspace *ls;

	memset(buf, 0, sizeof(buf));
	memset(argv, 0, sizeof(char *) * MAXARGS);

	rv = do_read(client[ci].fd, buf, DLM_CONTROLD_MSGLEN);
	if (rv < 0) {
		log_error("client %d fd %d read error %d %d", ci,
			  client[ci].fd, rv, errno);
		client_dead(ci);
		return;
	}

	log_debug("ci %d read %s", ci, buf);

	get_args(buf, &argc, argv, ' ', 2);

	if (!strncmp(argv[0], "deadlock_check", 14)) {
		ls = find_ls(argv[1]);
		if (ls)
			send_cycle_start(ls);
		else
			log_debug("deadlock_check ls name not found");
	}
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
	strcpy(&addr.sun_path[1], DLM_CONTROLD_SOCK_PATH);
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

void cluster_dead(int ci)
{
	log_error("cluster is down, exiting");
	clear_configfs();
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

	rv = setup_groupd();
	if (rv < 0)
		goto out;
	client_add(rv, process_groupd, cluster_dead);

	rv = setup_uevent();
	if (rv < 0)
		goto out;
	client_add(rv, process_uevent, NULL);

	rv = setup_member();
	if (rv < 0)
		goto out;
	client_add(rv, process_member, cluster_dead);

	/* netlink stuff is only used for deadlock detection */
	if (!deadlock_enabled)
		goto for_loop;

	rv = setup_netlink();
	if (rv < 0)
		goto for_loop;
	client_add(rv, process_netlink, NULL);

 for_loop:

	for (;;) {
		rv = poll(pollfd, client_maxi + 1, -1);
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

		for (i = 0; i <= client_maxi; i++) {
			if (client[i].fd < 0)
				continue;
			if (pollfd[i].revents & POLLIN) {
				workfn = client[i].workfn;
				workfn(i);
			}
			if (pollfd[i].revents & POLLHUP) {
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
	openlog("dlm_controld", LOG_PID, LOG_DAEMON);

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
	printf("  -d <num>     Enable (1) or disable (0, default) deadlock code\n");     
	printf("  -D	       Enable debugging code and don't fork\n");
	printf("  -K	       Enable kernel dlm debugging messages\n");
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

		case 'D':
			daemon_debug_opt = 1;
			break;

		case 'K':
			kernel_debug_opt = 1;
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'd':
			deadlock_enabled = atoi(optarg);
			break;

		case 'V':
			printf("dlm_controld (built %s %s)\n", __DATE__, __TIME__);
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

void set_oom_adj(int val)
{
	FILE *fp;

	fp = fopen("/proc/self/oom_adj", "w");
	if (!fp)
		return;

	fprintf(fp, "%i", val);
	fclose(fp);
}

void set_scheduler(void)
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
	prog_name = argv[0];

	INIT_LIST_HEAD(&lockspaces);

	decode_arguments(argc, argv);

	if (!daemon_debug_opt)
		daemonize();

	setup_deadlock();

	signal(SIGTERM, sigterm_handler);

	set_scheduler();
	set_oom_adj(-16);

	/* if this daemon was killed and the cluster shut down, and
	   then the cluster brought back up and this daemon restarted,
	   there will be old configfs entries we need to clear out */
	clear_configfs();

	set_ccs_options();

	return loop();
}

char *prog_name;
int daemon_debug_opt;
char daemon_debug_buf[256];
int kernel_debug_opt;

