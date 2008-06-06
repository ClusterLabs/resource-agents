#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "cnxman-socket.h"

static pthread_t recv_thread;
static int cl_sock;
static int quit = 0;
static int leave_finished = 0;
static pid_t our_pid;


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

/* This thread receives messages on the cluster socket and prints them. */

static void *recv_thread_fn(void *arg)
{
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

		len = recvmsg(cl_sock, &msg, MSG_OOB);

		if (len < 0 && errno == EAGAIN)
			continue;

		if (!len || len < 0)
			continue;

		nodeid = saddr.scl_nodeid;

		if (buf[0] == CLUSTER_OOB_MSG_PORTCLOSED)
			printf("message: oob port-closed from nodeid %d\n",
				nodeid);

		else if (buf[0] == CLUSTER_OOB_MSG_SERVICEEVENT)
			printf("message: oob service-event\n");

		else if (!strcmp(buf, "hello"))
			printf("message: \"%s\" from nodeid %d\n", buf, nodeid);

		else
			printf("message: unknown len %d byte0 %x nodeid %d\n",
				len, buf[0], nodeid);
	}
}

static void send_group_message(void)
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

	len = sendmsg(cl_sock, &msg, 0);
}

static void print_ev(struct cl_service_event *ev)
{
	switch (ev->type) {
	case SERVICE_EVENT_STOP:
		printf("stop:\n");
		break;
	case SERVICE_EVENT_START:
		printf("start:\n");
		break;
	case SERVICE_EVENT_FINISH:
		printf("finish:\n");
		break;
	case SERVICE_EVENT_LEAVEDONE:
		printf("leavedone:\n");
		break;
	}
	printf("  event_id    = %u\n", ev->event_id);
	printf("  last_stop   = %u\n", ev->last_stop);
	printf("  last_start  = %u\n", ev->last_start);
	printf("  last_finish = %u\n", ev->last_finish);
	printf("  node_count  = %u\n", ev->node_count);
}

static void print_members(int count, struct cl_cluster_node *nodes)
{
	int i;

	printf("members:\n");
	for (i = 0; i < count; i++) {
		printf("  nodeid = %u \"%s\"\n", nodes->node_id, nodes->name);
		nodes++;
	}
}

static int process_event(struct cl_service_event *ev)
{
	struct cl_cluster_node *nodes;
	int error = 0;

	print_ev(ev);

	if (ev->type == SERVICE_EVENT_START) {

		nodes = malloc(ev->node_count * sizeof(struct cl_cluster_node));
		if (!nodes) {
			perror("process_event: malloc failed");
			return -ENOMEM;
		}

		memset(nodes, 0, ev->node_count*sizeof(struct cl_cluster_node));

		error = ioctl(cl_sock, SIOCCLUSTER_SERVICE_GETMEMBERS, nodes);
		if (error < 0)
			perror("process_event: service get members failed");

		print_members(ev->node_count, nodes);

		error = ioctl(cl_sock, SIOCCLUSTER_SERVICE_STARTDONE,
			      ev->event_id);
		if (error < 0)
			perror("process_event: start done error");

		/* send_group_message(); */

		free(nodes);
	}

	if (ev->type == SERVICE_EVENT_LEAVEDONE)
		leave_finished = 1;

	return error;
}

int main(int argc, char **argv)
{
	struct cl_service_event event;
	struct sockaddr_cl saddr;
	char *name;
	int error;

	our_pid = getpid();

	if (argc > 1)
		name = argv[1];
	else
		name = "example";


	cl_sock = socket(AF_CLUSTER, SOCK_DGRAM, CLPROTO_CLIENT);
	if (cl_sock < 0) {
		perror("main: can't create cluster socket");
		return -1;
	}


	error = ioctl(cl_sock, SIOCCLUSTER_SERVICE_REGISTER, name);
	if (error < 0) {
		perror("main: service register failed");
		return -1;
	}


	/* binding to an address is only needed if we want to send/recv
	   messages to other nodes on the cluster socket. */

#if 0
	saddr.scl_family = AF_CLUSTER;
	saddr.scl_port = 13; /* CLUSTER_PORT_USER_SERVICE */

	error = bind(cl_sock, (struct sockaddr *) &saddr,
		     sizeof(struct sockaddr_cl));
	if (error < 0) {
		perror("main: can't bind to cluster socket");
		return -1;
	}
	pthread_create(&recv_thread, NULL, recv_thread_fn, 0);
#endif

	signal(SIGUSR1, sigusr1_handler);
	signal(SIGTERM, sigterm_handler);

	error = ioctl(cl_sock, SIOCCLUSTER_SERVICE_SETSIGNAL, SIGUSR1);
	if (error < 0) {
		perror("main: service set signal failed");
		return -1;
	}

	error = ioctl(cl_sock, SIOCCLUSTER_SERVICE_JOIN, NULL);
	if (error < 0) {
		perror("main: service join failed");
		return -1;
	}


	for (;;) {
		memset(&event, 0, sizeof(struct cl_service_event));

		error = ioctl(cl_sock, SIOCCLUSTER_SERVICE_GETEVENT, &event);
		if (error < 0) {
			perror("main: service get event failed");
			return -1;
		}

		if (!error)
			pause();
		else
			process_event(&event);


		if (quit) {
			quit = 0;
			leave_finished = 0;

			error = ioctl(cl_sock, SIOCCLUSTER_SERVICE_LEAVE, NULL);
			if (error < 0) {
				perror("main: service leave failed");
				return -1;
			}
		}

		if (leave_finished)
			break;
	}

	error = ioctl(cl_sock, SIOCCLUSTER_SERVICE_UNREGISTER, NULL);
	if (error < 0)
		perror("main: unregister failed");

	return 0;
}
