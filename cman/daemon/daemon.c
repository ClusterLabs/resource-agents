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
#include <getopt.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/errno.h>

#include "list.h"
#include "cnxman-socket.h"
#include "cnxman-private.h"
#include "cnxman.h"
#include "daemon.h"
#include "logging.h"
#include "commands.h"
#include "barrier.h"
#include "config.h"

struct queued_reply
{
	struct list list;
	char buf[1];
};

/* Events queued by membership thread to be delivered by daemon thread */
struct queued_event
{
	struct list list;
	int event;
	int arg;
};
static pthread_mutex_t event_lock;
static LIST_INIT(event_list);

static struct connection con_list_head =
{
	.fd = -1,
	.next = NULL,
};

/* Things to wake up for */
volatile sig_atomic_t quit_threads=0;

int num_connections = 0;
static pthread_t daemon_pthread;

/* List of timers */
static LIST_INIT(timer_list);

static inline void add_tv(struct timeval *tv, struct timeval *addme)
{
	tv->tv_sec += addme->tv_sec;
	tv->tv_usec += addme->tv_usec;
	if (tv->tv_usec > 1000000)
	{
		tv->tv_sec++;
		tv->tv_usec -= 1000000;
	}
}

static inline void sub_tv(struct timeval *tv, struct timeval *subme)
{
	tv->tv_sec -= subme->tv_sec;

	if (tv->tv_usec < subme->tv_usec)
	{
		tv->tv_sec--;
		tv->tv_usec = tv->tv_usec + 1000000 - subme->tv_usec;
	}
	else
		tv->tv_usec -= subme->tv_usec;
}

static void add_ordered_timer(struct cman_timer *new)
{
        struct cman_timer *te = NULL;
	int gotit = 0;

        list_iterate_items(te, &timer_list) {

		assert(new != te);

                if ((new->tv.tv_sec < te->tv.tv_sec) ||
		    (new->tv.tv_sec == te->tv.tv_sec &&
		     new->tv.tv_usec < te->tv.tv_usec))
                        break;
		else
			sub_tv(&new->tv, &te->tv);
        }

        if (!te)
                list_add(&timer_list, &new->list);
        else {
                new->list.p = te->list.p;
                new->list.n = &te->list;
                te->list.p->n = &new->list;
                te->list.p = &new->list;
	}

	/* Adjust the remaining timers */
	list_iterate_items(te, &timer_list) {
		if (te == new) {
			gotit = 1;
		}
		else {
			if (gotit)
				sub_tv(&te->tv, &new->tv);
		}

	}
}

void add_timer(struct cman_timer *t, time_t sec, int usec)
{
	t->tv.tv_sec = sec;
	t->tv.tv_usec = usec;

	add_ordered_timer(t);
}

void del_timer(struct cman_timer *t)
{
	int gotit = 0;
	struct cman_timer *te;

	list_iterate_items(te, &timer_list) {
		if (te == t) {
			gotit = 1;
		}
		else {
			if (gotit)
				add_tv(&te->tv, &t->tv);
		}
	}
	list_del(&t->list);
}

/* None of our threads is CPU intensive, but if they don't run when they are supposed
   to, the node can get kicked out of the cluster.
*/
void cman_set_realtime()
{
#if 0 // Until debugged!
	struct sched_param s;

	s.sched_priority = 1;
	if (sched_setscheduler(0, SCHED_FIFO, &s))
		log_msg(LOG_WARNING, "Cannot set priority: %s\n", strerror(errno));
#endif
}

static void process_comms(struct connection *con)
{
	comms_receive_message(con->clsock);
}

/*
 * Fork into the background and detach from our parent process.
 */
static void be_daemon()
{
        pid_t pid;
	int devnull = open("/dev/null", O_RDWR);
	if (devnull == -1) {
		perror("Can't open /dev/null");
		exit(3);
	}

	switch (pid = fork()) {
	case -1:
		perror("cman: can't fork");
		exit(2);

	case 0:		/* Child */
		break;

	default:       /* Parent */
		exit(0);
	}

	/* Detach ourself from the calling environment */
	if (close(0) || close(1) || close(2)) {
		syslog(LOG_ERR, "Error closing terminal FDs");
		exit(4);
	}
	setsid();

	if (dup2(devnull, 0) < 0 || dup2(devnull, 1) < 0
	    || dup2(devnull, 2) < 0) {
		syslog(LOG_ERR, "Error setting terminal FDs to /dev/null: %m");
		exit(5);
	}
	if (chdir("/")) {
		syslog(LOG_ERR, "Error setting current directory to /: %m");
		exit(6);
	}
}

static int open_local_sock(const char *name, int name_len, mode_t mode)
{
	int local_socket;
	struct sockaddr_un sockaddr;

	/* Open local socket */
	if (name[0] != '\0')
		unlink(name);
	local_socket = socket(PF_UNIX, SOCK_STREAM, 0);
	if (local_socket < 0) {
		log_msg(LOG_ERR, "Can't create local socket %s: %s\n", name, strerror(errno));
		return -1;
	}
	/* Set Close-on-exec */
	fcntl(local_socket, F_SETFD, 1);

	memset(&sockaddr, 0, sizeof(sockaddr));
	memcpy(sockaddr.sun_path, name, name_len);
	sockaddr.sun_family = AF_UNIX;
	if (bind(local_socket, (struct sockaddr *) &sockaddr, sizeof(sockaddr))) {
		log_msg(LOG_ERR, "can't bind local socket to %s: %s\n", name, strerror(errno));
		close(local_socket);
		return -1;
	}
	if (listen(local_socket, 1) != 0) {
		log_msg(LOG_ERR, "listen on %s failed: %s\n", name, strerror(errno));
		close(local_socket);
		return -1;
	}
	if (name[0] != '\0')
		chmod(name, mode);

	return local_socket;
}

static struct connection *add_connection(int fd, con_type_t type)
{
	struct connection *newcon;

	if (con_list_head.fd == -1) {
		con_list_head.fd = fd;
		con_list_head.type = type;
		list_init(&con_list_head.write_msgs);
		con_list_head.next = NULL;
		newcon = &con_list_head;
	}
	else
	{
		newcon = malloc(sizeof(struct connection));
		if (!newcon) {
			log_msg(LOG_ERR, "malloc failed for new connection\n");
			return NULL;
		}
		/* We should be the only thread that walks this list so no locking needed */
		newcon->fd = fd;
		newcon->type = type;
		newcon->next = con_list_head.next;
		list_init(&newcon->write_msgs);
		con_list_head.next = newcon;
	}
	P_DAEMON("added connection on fd %d, type %d\n", fd, type);

	return newcon;
}

/* Both client and admin rendezvous sockets use this */
static void process_rendezvous(struct connection *con, con_type_t newtype, struct connection **retcon)
{
	struct sockaddr_un socka;
	struct connection *newcon;
	socklen_t sl = sizeof(socka);
	int client_fd;

	client_fd = accept(con->fd, (struct sockaddr *) &socka, &sl);

	if (client_fd >= 0) {
		newcon = malloc(sizeof(struct connection));
		if (!newcon) {
			close(client_fd);
			return;
		}
		newcon->fd = client_fd;
		newcon->type = newtype;
		newcon->port = 0;
		newcon->next = NULL;
		list_init(&newcon->write_msgs);
		*retcon = newcon;
		P_DAEMON("got new connection on fd %d, type %d\n", newcon->fd, newtype);

		num_connections++;
	}
}

/* Send it, or queue it for later if the socket is busy */
static int send_reply_message(struct connection *con, struct sock_header *msg)
{
	int ret;

	P_DAEMON("sending reply %x to fd %d\n", msg->command, con->fd);
	ret = send(con->fd, (char *)msg, msg->length, MSG_DONTWAIT);
	if (ret == -1 && errno == EAGAIN) {
		/* Queue it */
		struct queued_reply *qm = malloc(sizeof(struct queued_reply) + msg->length);
		if (!qm)
		{
			perror("Error allocating queued message");
			return -1;
		}
		memcpy(qm->buf, msg, msg->length);
		list_add(&con->write_msgs, &qm->list);
		P_DAEMON("queued last message\n");
	}
	return 0;
}

/* Dispatch a request from a CLIENT or ADMIN socket */
static void process_client(struct connection *con, struct sock_header *msg)
{
	if (msg->magic != CMAN_MAGIC) {
		P_DAEMON("bad magic in client command %x\n", msg->magic);
		send_status_return(con, msg->command, EINVAL);
		return;
	}

	P_DAEMON("client command is %x\n", msg->command);

	/* Privileged functions can only be done on ADMIN sockets */
	if (msg->command & CMAN_CMDFLAG_PRIV && con->type != CON_ADMIN) {
		P_DAEMON("command disallowed from non-admin client\n");
		send_status_return(con, msg->command, EPERM);
		return;
	}

	/* Slightly arbitrary this one, don't allow ADMIN sockets to
	   send/receive data. The main loop doesn't keep a backlog queue
	   of messages for ADMIN sockets
	*/
	if ((msg->command == CMAN_CMD_DATA || msg->command == CMAN_CMD_BIND) &&
	    con->type == CON_ADMIN) {
		P_DAEMON("can't send data down an admin socket, sorry\n");
		send_status_return(con, msg->command, EINVAL);
		return;
	}

	if (msg->command == CMAN_CMD_DATA) {
		char *buf = (char *)msg;
		int ret;
		struct sock_data_header *dmsg = (struct sock_data_header *)msg;

		P_DAEMON("sending %d bytes of data to node %d, port %d\n",
			 msg->length - sizeof(struct sock_data_header), dmsg->nodeid, dmsg->port);

		buf += sizeof(struct sock_data_header);
		ret = send_data_msg(con, dmsg->port, dmsg->nodeid, msg->flags,
				    buf, msg->length - sizeof(struct sock_data_header));
		if (ret) {
			send_status_return(con, msg->command, ret);
		}
	}
	else {
		char *cmdbuf = (char *)msg;
		char small_retbuf[1024]; /* Enough for most needs */
		char *retbuf = small_retbuf;
		struct sock_reply_header *reply;
		int ret;
		int retlen = 0;

		P_DAEMON("About to process command\n");

		cmdbuf += sizeof(struct sock_header);
		ret = process_command(con, msg->command, cmdbuf,
				      &retbuf, &retlen, sizeof(small_retbuf),
				      sizeof(struct sock_reply_header));

		/* Reply message will come later on */
		if (ret == -EWOULDBLOCK)
			return;

		reply = (struct sock_reply_header *)retbuf;

		reply->header.magic = CMAN_MAGIC;
		reply->header.flags = 0;
		reply->header.command = msg->command | CMAN_CMDFLAG_REPLY;
		reply->header.length = retlen + sizeof(struct sock_reply_header);
		reply->status = ret;

		P_DAEMON("Returning command data. length = %d\n", retlen);
		send_reply_message(con, (struct sock_header *)reply);

		if (retbuf != small_retbuf)
			free(retbuf);
	}
}

/* Returning -1 from this tells the main loop to remove the connection */
static int read_client(struct connection *con)
{
	char buf[MAX_CLUSTER_MESSAGE + sizeof(struct sock_header)];
	struct sock_header *header = (struct sock_header *)buf;
	int len;

	len = read(con->fd, buf, sizeof(struct sock_header));

	P_DAEMON("read %d bytes from fd %d\n", len, con->fd);

	if (len == 0)
		return -1;

	if (len < 0 &&
	    (errno == EINTR || errno == EAGAIN))
		return 0;

	if (len < 0)
		return -1;

	/* Read the rest */
	if (len != header->length) {
		len = read(con->fd, buf+len, header->length-len);
		if (len == 0)
			return -1;

		if (len < 0 &&
		    (errno == EINTR || errno == EAGAIN))
			return 0;

		if (len < 0)
			return -1;
	}

	process_client(con, header);
	return 0;
}

/* Socket demux */
static int process_socket(struct connection *con, struct connection **newcon)
{
	int ret = 0;

	switch (con->type) {
	case CON_CLIENT_RENDEZVOUS:
		process_rendezvous(con, CON_CLIENT, newcon);
		break;

	case CON_ADMIN_RENDEZVOUS:
		process_rendezvous(con, CON_ADMIN, newcon);
		break;

	case CON_COMMS:
		process_comms(con);
		break;

	case CON_CLIENT:
	case CON_ADMIN:
		ret = read_client(con);
		break;
	}
	return ret;
}


/* Send a simple return - usually just a failure status */
int send_status_return(struct connection *con, uint32_t cmd, int status)
{
	struct sock_reply_header msg;

	P_DAEMON("send status return: %d\n", status);
	msg.header.magic = CMAN_MAGIC;
	msg.header.command = cmd | CMAN_CMDFLAG_REPLY;
	msg.header.length = sizeof(msg);
	msg.header.flags = 0;
	msg.status = status;

	return send_reply_message(con, (struct sock_header *)&msg);
}

int send_data_reply(struct connection *con, int nodeid, int port, char *data, int len)
{
	char buf[len + sizeof(struct sock_data_header)];
	struct sock_data_header *msg = (struct sock_data_header *)buf;

	msg->header.magic = CMAN_MAGIC;
	msg->header.command = CMAN_CMD_DATA | CMAN_CMDFLAG_REPLY;
	msg->header.length = sizeof(*msg)+len;
	msg->header.flags = 0;
	msg->nodeid = nodeid;
	msg->port = port;

	memcpy(buf+sizeof(struct sock_data_header), data, len);
	return send_reply_message(con, (struct sock_header *)msg);
}

/* This can be called by the membership thread as well as the daemon thread. */
void notify_listeners(struct connection *con, int event, int arg)
{
	struct sock_event_message msg;
	struct connection *thiscon;

	msg.header.magic = CMAN_MAGIC;
	msg.header.command = CMAN_CMD_EVENT;
	msg.header.length = sizeof(msg);
	msg.header.flags = 0;
	msg.reason = event;
	msg.arg = arg;

	/* If there's just one recipient then it doesn't matter who sends it */
	if (con) {
		send_reply_message(con, (struct sock_header *)&msg);
		return;
	}

	/* If it's a broadcast event then we need to walk the connections list.
	 * As this must only be done in the daemon thread, if we are in another thread
	 * then just queue the event and wake the daemon thread up.
	 * This is much more efficient than locking the sockets list because events
	 * are infrequent, the sockets list is /very/ busy
	 */
	if (pthread_self() == daemon_pthread) {

		for (thiscon = &con_list_head; thiscon != NULL; thiscon = thiscon->next) 	{
			if (thiscon->type == CON_CLIENT)
				send_reply_message(thiscon, (struct sock_header *)&msg);
		}
	}
	else {
		struct queued_event *qe = malloc(sizeof(struct queued_event));
		if (qe)
		{
			P_DAEMON("Queuing event\n");
			qe->event = event;
			qe->arg = arg;
			pthread_mutex_lock(&event_lock);
			list_add(&event_list, &qe->list);
			pthread_mutex_unlock(&event_lock);
			pthread_kill(daemon_pthread, SIGUSR1);
		}
	}
}

void wake_daemon(void)
{
	pthread_kill(daemon_pthread, SIGUSR1);
}

static void send_queued_events()
{
	struct queued_event *qe;

	pthread_mutex_lock(&event_lock);
	if (!list_empty(&event_list)) {
		struct list *tmp, *tmp1;

		P_DAEMON("Sending queued events\n");
		list_iterate_safe(tmp, tmp1, &event_list) {
			qe = list_item(tmp, struct queued_event);
			notify_listeners(NULL, qe->event, qe->arg);
			list_del(&qe->list);
			free(qe);
		}
	}
	pthread_mutex_unlock(&event_lock);
}

/* Send as many as we can */
static void send_queued_reply(struct connection *con)
{
	struct queued_reply *qm;
	struct sock_header *msg;
	struct list *tmp, *qmh;
	int ret;

	list_iterate_safe(qmh, tmp, &con->write_msgs) {
		qm = list_item(qmh, struct queued_reply);
		msg = (struct sock_header *)qm->buf;
		ret = send(con->fd, qm->buf, msg->length, MSG_DONTWAIT);
		if (ret == msg->length)
		{
			list_del(&qm->list);
			free(qm);
		}
		else
		{
			break;
		}
	}
}

static int main_loop()
{
	sigset_t ss;
	sigfillset(&ss);
        sigdelset(&ss, SIGUSR1);
	daemon_pthread = pthread_self();

	while (!quit_threads) {
		fd_set in;
		fd_set out;
		int select_status;
		struct timeval *timeout;
		struct connection *thiscon, *lastcon = NULL;
		struct cman_timer *timer_entry = NULL;

		FD_ZERO(&in);
		FD_ZERO(&out);
		for (thiscon = &con_list_head; thiscon != NULL; thiscon = thiscon->next) {
			FD_SET(thiscon->fd, &in);

			if (thiscon->type == CON_CLIENT && !list_empty(&thiscon->write_msgs))
				FD_SET(thiscon->fd, &out);
		}

		if (list_empty(&timer_list)) {
			timeout = NULL;
			P_DAEMON("No timer for this select\n");
		}
		else {
			timer_entry = list_item(timer_list.n, struct cman_timer);
			timeout = &timer_entry->tv;
			P_DAEMON("Using timer of %ld/%ld\n", timeout->tv_sec, timeout->tv_usec);
		}

		if ((select_status = select(FD_SETSIZE, &in, &out, NULL, timeout)) > 0)	{
			struct connection *newcon;
			for (thiscon = &con_list_head; thiscon != NULL; thiscon = thiscon->next) {
				if (FD_ISSET(thiscon->fd, &in))
				{
					newcon = NULL;

					if (process_socket(thiscon, &newcon) < 0) {
						/* Socket EOF or error */
						struct connection *free_con;

						num_connections--;

						close(thiscon->fd);
						lastcon->next = thiscon->next;
						free_con = thiscon;
						thiscon = lastcon;

						if (free_con->port)
							unbind_con(free_con);

						free(free_con);
						continue;
					}
					if (newcon) {
						newcon->next = thiscon->next;
						thiscon->next = newcon;
					}
				}
				if (FD_ISSET(thiscon->fd, &out)) {
					send_queued_reply(thiscon);
				}
				lastcon = thiscon;
			}
		}

		check_mainloop_flags();
		send_queued_events();

		if (timer_entry &&
		    (timer_entry->tv.tv_sec == 0 && timer_entry->tv.tv_usec == 0)) {

			P_DAEMON("calling callback for timer\n");
			if (timer_entry->callback)
				timer_entry->callback(timer_entry->arg);
			list_del(&timer_entry->list);
		}

		if (select_status < 0 && errno != EINTR) {
			perror("Error in select");
			quit_threads = 1;
		}
	}

	return 0;
}

static void sigint_handler(int ignored)
{
	quit_threads = 1;
}

/* USR1 is used to inter-thread signalling */
void sigusr1_handler()
{
}

static void add_cluster_sockets(char *option, int if_num)
{
	char scratch[strlen(option)+1];
	struct connection *con;
	char *comma;
	int fd;

	strcpy(scratch, option);
	comma = strchr(scratch, ',');
	if (!comma) {
		log_msg(LOG_ERR, "Invalid cluster FDs option %s\n", optarg);
		exit(1);
	}
	strcpy(scratch, option);
	*comma = '\0';

	fd = atoi(scratch);
	con = add_connection(fd, CON_COMMS);
	con->clsock = add_clsock(1, if_num, fd);

	fd = atoi(comma+1);
	con = add_connection(fd, CON_COMMS);
	con->clsock = add_clsock(0, if_num, fd);
}

int main(int argc, char *argv[])
{
	int fd;
	int opt;
	struct connection *thiscon;
	int no_fork = 0;
	int interface_no = 1;
	sigset_t ss;
	struct sigaction sa;

	cluster_init();	
	barrier_init();
	commands_init();

	while ((opt=getopt(argc,argv,"dh?s:")) != EOF) {
		switch (opt) {

		case 'h':
		case '?':
			printf("Usage\n");
			break;

		case 'd':
			no_fork = 1;
			break;

		case 's': /* Cluster sockets -s b,l where b is the broadcast & l is the listening socket fd */
			add_cluster_sockets(optarg, interface_no++);
			break;
		}
	}

	init_config();
	init_log(no_fork);
	init_debug(cman_config.debug_mask);

	log_msg(LOG_INFO, "CMAN %s (built %s %s) started\n",
		CMAN_RELEASE_NAME, __DATE__, __TIME__);

	fd = open_local_sock(CLIENT_SOCKNAME, sizeof(CLIENT_SOCKNAME), 0660);
	if (fd >= 0)
		add_connection(fd, CON_CLIENT_RENDEZVOUS);
	else
		exit(2);

	fd = open_local_sock(ADMIN_SOCKNAME, sizeof(ADMIN_SOCKNAME), 0600);
	if (fd >= 0)
		add_connection(fd, CON_ADMIN_RENDEZVOUS);
	else
		exit(2);

	if (!no_fork)
		be_daemon();

	/* Wake up on SIGUSR1 */
	sigfillset(&ss);
	sa.sa_handler = sigusr1_handler;
	sa.sa_mask = ss;
	sa.sa_flags = 0;
	sigaction(SIGUSR1, &sa, NULL);

	/* Shutdown trap */
	sa.sa_handler = sigint_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	signal(SIGPIPE, SIG_IGN);

	pthread_mutex_init(&event_lock, NULL);

	cman_set_realtime();
	main_loop();

	for (thiscon = &con_list_head; thiscon != NULL; thiscon = thiscon->next) {
		close(thiscon->fd);
	}
	unlink(CLIENT_SOCKNAME);
 	unlink(ADMIN_SOCKNAME);

	return 0;
}
