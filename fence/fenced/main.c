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


/* static pthread_t recv_thread; */
static int quit = 0;
static int leave_finished = 0;


#define OPTION_STRING			("Dn:hV")
#define LOCKFILE_NAME			"/var/run/fenced.pid"


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
	printf("  -n <name>        Name of the fence domain, \"default\" if none\n");
	printf("  -V               Print program version information, then exit\n");
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


/* SIGUSR1 will cause this program to look for a new service event from SM
   using the GETEVENT ioctl.
 
   SIGTERM will cause this program to leave the service group cleanly; it will
   do a LEAVE ioctl, get a stop event and then exit.
   
   SIGKILL will cause the program to exit without first leaving the service
   group.  In that case the kernel will clean up and leave the service group
   (as a part of cl_release on the cluster socket). */


static void sigusr1_handler(int sig)
{
}

static void sigterm_handler(int sig)
{
	quit = 1;
}

#if 0
/* This thread receives messages on the cluster socket and prints them. */
static void *recv_thread_fn(void *arg)
{
	fd_t *fd = arg;
	struct iovec iov[2];
	struct msghdr msg;
	struct sockaddr_cl saddr;
	char buf[256];
	int len;
	int nodeid;

	for (;;) {
		memset(buf, 0, 256);

		msg.msg_control    = NULL;
		msg.msg_controllen = 0;
		msg.msg_iovlen     = 1;
		msg.msg_iov        = iov;
		msg.msg_name       = &saddr;
		msg.msg_flags      = 0;
		msg.msg_namelen    = sizeof(saddr);
		iov[0].iov_len     = sizeof(buf);
		iov[0].iov_base    = buf;

		len = recvmsg(fd->cl_sock, &msg, MSG_OOB);

		if (len < 0 && errno == EAGAIN)
			continue;

		if (!len || len < 0)
			continue;

		memcpy(&nodeid, &saddr.scl_csid, sizeof(int));

		if (buf[0] == CLUSTER_OOB_MSG_PORTCLOSED)
			log_debug("message: oob port-closed from nodeid %d",
				  nodeid);

		else if (buf[0] == CLUSTER_OOB_MSG_SERVICEEVENT)
			log_debug("message: oob service-event");

		else if (!strcmp(buf, "hello"))
			log_debug("message: \"%s\" from nodeid %d", buf, nodeid);

		else
			log_debug("message: unknown len %d byte0 %x nodeid %d",
				len, buf[0], nodeid);
	}
}

static void send_group_message(fd_t *fd)
{
	struct iovec iov[2];
	struct msghdr msg;
	char buf[256];
	int len;

	strcpy(buf, "hello");

	iov[0].iov_len     = strlen(buf);
	iov[0].iov_base    = buf;
	msg.msg_control    = NULL;
	msg.msg_controllen = 0;
	msg.msg_iovlen     = 1;
	msg.msg_iov        = iov;
	msg.msg_name       = NULL;
	msg.msg_flags      = O_NONBLOCK;
	msg.msg_namelen    = 0;

	len = sendmsg(fd->cl_sock, &msg, 0);
}
#endif

static void print_ev(struct cl_service_event *ev)
{
	switch (ev->type) {
	case SERVICE_EVENT_STOP:
		log_debug("stop:");
		break;
	case SERVICE_EVENT_START:
		log_debug("start:");
		break;
	case SERVICE_EVENT_FINISH:
		log_debug("finish:");
		break;
	case SERVICE_EVENT_LEAVEDONE:
		log_debug("leavedone:");
		break;
	}
	log_debug("  event_id    = %u", ev->event_id);
	log_debug("  last_stop   = %u", ev->last_stop);
	log_debug("  last_start  = %u", ev->last_start);
	log_debug("  last_finish = %u", ev->last_finish);
	log_debug("  node_count  = %u", ev->node_count);

	if (ev->type != SERVICE_EVENT_START)
		return;

	switch (ev->start_type) {
	case SERVICE_START_FAILED:
		log_debug("  start_type  = %s", "failed");
		break;
	case SERVICE_START_JOIN:
		log_debug("  start_type  = %s", "join");
		break;
	case SERVICE_START_LEAVE:
		log_debug("  start_type  = %s", "leave");
		break;
	}
}

static void print_members(int count, struct cl_cluster_node *nodes)
{
	int i;

	log_debug("members:");
	for (i = 0; i < count; i++) {
		log_debug("  nodeid = %u \"%s\"", nodes->node_id, nodes->name);
		nodes++;
	}
}

static void process_event(fd_t *fd, struct cl_service_event *ev)
{
	struct cl_cluster_nodelist nodelist;
	struct cl_cluster_node *nodes;
	int error = 0, n;

	print_ev(ev);

	if (ev->type == SERVICE_EVENT_START) {
		fd->last_start = ev->event_id;

		/* space for two extra to be sure it's not too small */
		n = ev->node_count + 2;

		FENCE_RETRY(nodes = malloc(n * sizeof(struct cl_cluster_node)),
			    nodes);
		memset(nodes, 0, n * sizeof(struct cl_cluster_node));

		nodelist.max_members = n;
		nodelist.nodes = nodes;

		error = ioctl(fd->cl_sock, SIOCCLUSTER_SERVICE_GETMEMBERS,
			      &nodelist);
		if (error < 0)
			die("process_event: service get members failed");

		print_members(ev->node_count, nodes);

		do_recovery(fd, ev, nodes);

		error = ioctl(fd->cl_sock, SIOCCLUSTER_SERVICE_STARTDONE,
			      ev->event_id);
		if (error < 0)
			log_debug("process_event: start done error");

		free(nodes);
	}

	else if (ev->type == SERVICE_EVENT_LEAVEDONE)
		leave_finished = 1;

	else if (ev->type == SERVICE_EVENT_STOP)
		fd->last_stop = fd->last_start;

	else if (ev->type == SERVICE_EVENT_FINISH) {
		fd->last_finish = ev->event_id;
		do_recovery_done(fd);
	}
}


static void process_events(fd_t *fd)
{	
	struct cl_service_event event;
	int error;

	for (;;) {
		memset(&event, 0, sizeof(struct cl_service_event));

		error = ioctl(fd->cl_sock, SIOCCLUSTER_SERVICE_GETEVENT, &event);
		if (error < 0)
			die("process_events: service get event failed");

		if (!error)
			pause();
		else
			process_event(fd, &event);

		if (quit) {
			quit = 0;
			leave_finished = 0;

			error = ioctl(fd->cl_sock, SIOCCLUSTER_SERVICE_LEAVE, 0);
			if (error < 0)
				die("process_events: service leave failed");
		}

		if (leave_finished)
			break;
	}
}

static int init_nodes(fd_t *fd)
{
	char path[256];
	char *name = NULL;
	int error, cd;

	cd = ccs_connect();
	if (cd < 0)
		return -1;

	memset(path, 0, 256);
	sprintf(path, "//nodes/node/@name");

	for (;;) {
		error = ccs_get(cd, path, &name);
		if (error || !name)
			break;

		add_complete_node(fd, 0, strlen(name), name);
		free(name);
		name = NULL;
	}

	ccs_disconnect(cd);
	return 0;
}

int fence_domain_add(commandline_t *comline)
{
	int cl_sock;
	int namelen = strlen(comline->name);
	fd_t *fd;
	int error;

	if (namelen > MAX_NAME_LEN-1)
		return -ENAMETOOLONG;

	fd = (fd_t *) malloc(sizeof(fd_t) + MAX_NAME_LEN);
	if (!fd)
		return -ENOMEM;

	memset(fd, 0, sizeof(fd_t) + MAX_NAME_LEN);
	memcpy(fd->name, comline->name, namelen);
	fd->namelen = namelen;

	fd->first_recovery = FALSE;
	fd->last_stop = 0;
	fd->last_start = 0;
	fd->last_finish = 0;
	fd->prev_count = 0;
	INIT_LIST_HEAD(&fd->prev);
	INIT_LIST_HEAD(&fd->victims);
	INIT_LIST_HEAD(&fd->leaving);
	INIT_LIST_HEAD(&fd->complete);

	init_nodes(fd);

	cl_sock = socket(AF_CLUSTER, SOCK_DGRAM, CLPROTO_CLIENT);
	if (cl_sock < 0)
		die("fence_domain_add: can't create cluster socket");

	error = ioctl(cl_sock, SIOCCLUSTER_SERVICE_REGISTER, fd->name);
	if (error < 0)
		die("fence_domain_add: service register failed");

	/* FIXME: SERVICE_LEVEL_FENCE is 0 but defined in service.h */
	error = ioctl(cl_sock, SIOCCLUSTER_SERVICE_SETLEVEL, 0);
	if (error < 0)
		die("fence_domain_add: service set level failed");

	signal(SIGUSR1, sigusr1_handler);
	signal(SIGTERM, sigterm_handler);

	error = ioctl(cl_sock, SIOCCLUSTER_SERVICE_SETSIGNAL, SIGUSR1);
	if (error < 0)
		die("fence_domain_add: service set signal failed");

	error = ioctl(cl_sock, SIOCCLUSTER_SERVICE_JOIN, NULL);
	if (error < 0)
		die("fence_domain_add: service join failed");

	fd->cl_sock = cl_sock;

	process_events(fd);

	error = ioctl(cl_sock, SIOCCLUSTER_SERVICE_UNREGISTER, NULL);
	if (error < 0)
		die("fence_domain_add: unregister failed");

	free(fd);
	return 0;
}

static void check_cluster(void)
{
	int cl_sock, active;

	cl_sock = socket(AF_CLUSTER, SOCK_DGRAM, CLPROTO_CLIENT);
	if (cl_sock < 0)
		die("can't create cman cluster socket");

	active = ioctl(cl_sock, SIOCCLUSTER_ISACTIVE, 0);
	if (!active)
		die("CMAN cluster manager is not running");

	/* FIXME: check if fence service is registered and exit if so */

	close(cl_sock);
}

static void decode_arguments(int argc, char **argv, commandline_t *comline)
{
	int cont = TRUE;
	int optchar;

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {

		case 'D':
			comline->debug = TRUE;
			break;

		case 'n':
			strncpy(comline->name, optarg, MAX_NAME_LEN);
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'V':
			printf("fenced %s (built %s %s)\n", FENCE_RELEASE_NAME,
				 __DATE__, __TIME__);
			printf("%s\n", REDHAT_COPYRIGHT);
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
			die("unknown option: %c", optchar);
			break;
		};
	}

	if (!strcmp(comline->name, ""))
		strcpy(comline->name, "default");

	if (comline->debug) {
		printf("Command Line Arguments:\n");
		printf("  name = %s\n", comline->name);
		printf("  debug = %d\n", comline->debug);
	}
}

int main(int argc, char **argv)
{
	commandline_t comline;

	prog_name = argv[0];

	memset(&comline, 0, sizeof(commandline_t));
	decode_arguments(argc, argv, &comline);

	check_cluster();

	if (!comline.debug) {
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

	fence_domain_add(&comline);
	return 0;
}

char *prog_name;

