#include <getopt.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/errno.h>

#include <corosync/ipc_gen.h>
#include <corosync/engine/coroapi.h>
#include <corosync/engine/logsys.h>
#include <corosync/totem/coropoll.h>

#include "list.h"
#include "cnxman-socket.h"
#include "cnxman-private.h"
#include "daemon.h"
#include "logging.h"
#include "commands.h"
#include "barrier.h"
#include "ais.h"
#include "cman.h"

LOGSYS_DECLARE_SUBSYS (CMAN_NAME, LOG_INFO);

struct queued_reply
{
	struct list list;
	int offset;
	char buf[1];
};

/* We need to keep these in a list so we can notify of
   cluster events */
static LIST_INIT(client_list);

/* Things to wake up for */
volatile sig_atomic_t quit_threads=0;

int num_connections = 0;
poll_handle ais_poll_handle;
uint32_t max_outstanding_messages = DEFAULT_MAX_QUEUED;

static int process_client(poll_handle handle, int fd, int revent, void *data);
static void remove_client(poll_handle handle, struct connection *con);

/* Send it, or queue it for later if the socket is busy */
static int send_reply_message(struct connection *con, struct sock_header *msg)
{
	int ret;

	P_DAEMON("sending reply %x to fd %d\n", msg->command, con->fd);

	/* If there are already queued messages then don't send this one
	   out of order */
	if (!list_empty(&con->write_msgs)) {
		ret = -1;
		errno = EAGAIN;
	}
	else {
		ret = send(con->fd, (char *)msg, msg->length, MSG_DONTWAIT);
	}

	if ((ret > 0 && ret != msg->length) ||
	    (ret == -1 && errno == EAGAIN)) {
		struct queued_reply *qm;

		/* Have we exceeded the allowed number of queued messages ? */
		if (con->num_write_msgs > max_outstanding_messages) {
			P_DAEMON("Disconnecting. client has more that %d replies outstanding (%d)\n", max_outstanding_messages, con->num_write_msgs);
			remove_client(ais_poll_handle, con);
			return -1;
		}

		/* Queue it */
		qm = malloc(sizeof(struct queued_reply) + msg->length);
		if (!qm)
		{
			perror("Error allocating queued message");
			return -1;
		}
		memcpy(qm->buf, msg, msg->length);
		if (ret > 0)
			qm->offset = ret;
		else
			qm->offset = 0;
		list_add(&con->write_msgs, &qm->list);
		con->num_write_msgs++;
		P_DAEMON("queued last message, count is %d\n", con->num_write_msgs);
		poll_dispatch_modify(ais_poll_handle, con->fd, POLLIN | POLLOUT, process_client);
	}
	return 0;
}

static void remove_client(poll_handle handle, struct connection *con)
{
	struct list *tmp, *qmh;
	struct queued_reply *qm;
	int msgs=0;

	poll_dispatch_delete(handle, con->fd);
	close(con->fd);
	if (con->type == CON_CLIENT)
		list_del(&con->list);

	unbind_con(con);
	remove_barriers(con);

	list_iterate_safe(qmh, tmp, &con->write_msgs) {
		qm = list_item(qmh, struct queued_reply);

		list_del(&qm->list);
		free(qm);
		msgs++;
	}

	P_DAEMON("Freed %d queued messages\n", msgs);
	free(con);
	num_connections--;
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
		ret = send(con->fd, qm->buf + qm->offset, msg->length - qm->offset, MSG_DONTWAIT);
		if (ret == msg->length - qm->offset)
		{
			list_del(&qm->list);
			free(qm);
			con->num_write_msgs--;
		}
		else
		{
			if (ret > 0)
				qm->offset += ret;
			break;
		}
	}
	if (list_empty(&con->write_msgs)) {
		/* Remove POLLOUT callback */
		P_DAEMON("Removing POLLOUT from fd %d\n", con->fd);
		poll_dispatch_modify(ais_poll_handle, con->fd, POLLIN, process_client);
	}
}

/* Dispatch a request from a CLIENT or ADMIN socket */
static int process_client(poll_handle handle, int fd, int revent, void *data)
{
	struct connection *con = data;

	if (revent == POLLOUT) {
		send_queued_reply(con);
	} else {
		char buf[MAX_CLUSTER_MESSAGE + sizeof(struct sock_header)];
		struct sock_header *msg = (struct sock_header *)buf;
		int len;
		int totallen = 0;

		memset(buf, 0, (MAX_CLUSTER_MESSAGE + sizeof(struct sock_header)));

		len = read(fd, buf, sizeof(struct sock_header));

		P_DAEMON("read %d bytes from fd %d\n", len, fd);

		if (len == 0) {
			remove_client(handle, con);
			return -1;
		}

		if (len < 0 &&
		    (errno == EINTR || errno == EAGAIN))
			return 0;

		if (len < 0) {
			remove_client(handle, con);
			return 0;
		}

		if (msg->magic != CMAN_MAGIC) {
			P_DAEMON("bad magic in client command %x\n", msg->magic);
			send_status_return(con, msg->command, -EINVAL);
			return 0;
		}
		if (msg->version != CMAN_VERSION) {
			P_DAEMON("bad version in client command. msg = 0x%x, us = 0x%x\n", msg->version, CMAN_VERSION);
			send_status_return(con, msg->command, -EINVAL);
			return 0;
		}
		if ((msg->length-len) > MAX_CLUSTER_MESSAGE) {
			P_DAEMON("message on socket is too big\n");
			send_status_return(con, msg->command, -EINVAL);
			return 0;
		}

		totallen = len;

		/* Read the rest */
		while (totallen != msg->length) {
			len = read(fd, buf+len, msg->length-len);
			if (len == 0)
				return -1;

			if (len < 0 &&
			    (errno == EINTR || errno == EAGAIN))
				return 0;

			if (len < 0) {
				remove_client(handle, con);
				return -1;
			}
			totallen += len;
		}

		P_DAEMON("client command is %x\n", msg->command);

		/* Privileged functions can only be done on ADMIN sockets */
		if (msg->command & CMAN_CMDFLAG_PRIV && con->type != CON_ADMIN) {
			P_DAEMON("command disallowed from non-admin client\n");
			send_status_return(con, msg->command, -EPERM);
			return 0;
		}

		/* Slightly arbitrary this one, don't allow ADMIN sockets to
		   send/receive data. The main loop doesn't keep a backlog queue
		   of messages for ADMIN sockets
		*/
		if ((msg->command == CMAN_CMD_DATA || msg->command == CMAN_CMD_BIND ||
		     msg->command == CMAN_CMD_NOTIFY) && con->type == CON_ADMIN) {
			P_DAEMON("can't send data down an admin socket, sorry\n");
			send_status_return(con, msg->command, -EINVAL);
			return 0;
		}

		if (msg->command == CMAN_CMD_DATA) {
			char *buf = (char *)msg;
			int ret;
			uint8_t port;
			struct sock_data_header *dmsg = (struct sock_data_header *)msg;

			P_DAEMON("sending %lu bytes of data to node %d, port %d\n",
				 (unsigned long)(msg->length - sizeof(struct sock_data_header)), dmsg->nodeid, dmsg->port);

			buf += sizeof(struct sock_data_header);

			if (dmsg->port > 255) {
				send_status_return(con, msg->command, -EINVAL);
				return 0;
			}

			if (dmsg->port)
				port = dmsg->port;
			else
				port = con->port;

			ret = comms_send_message(buf, msg->length - sizeof(struct sock_data_header),
						 port, con->port,
						 dmsg->nodeid,
						 msg->flags);
			if (ret) {
				send_status_return(con, msg->command, -EIO);
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
				return 0;

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
	return 0;
}


/* Both client and admin rendezvous sockets use this */
static int process_rendezvous(poll_handle handle, int fd, int revent, void *data)
{
	struct sockaddr_un socka;
	struct connection *con = data;
	socklen_t sl = sizeof(socka);
	int client_fd;

	client_fd = accept(fd, (struct sockaddr *) &socka, &sl);
	if (client_fd >= 0) {
		struct connection *newcon = malloc(sizeof(struct connection));
		if (!newcon) {
			close(client_fd);
			return 0; /* returning -1 will remove us */
		}

		newcon->fd = client_fd;
		newcon->type = con->type;
		newcon->port = 0;
		newcon->events = 0;
		newcon->num_write_msgs = 0;
		list_init(&newcon->write_msgs);
		fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL, 0) | O_NONBLOCK);

		poll_dispatch_add(handle, client_fd, POLLIN, newcon, process_client);
		num_connections++;
		if (newcon->type == CON_CLIENT)
			list_add(&client_list, &newcon->list);
	}
	return 0;
}

static int open_local_sock(const char *name, int name_len, mode_t mode, poll_handle handle, int type)
{
	int local_socket;
	struct sockaddr_un sockaddr;
	struct connection *con;

	/* Open local socket */
	if (name[0] != '\0')
		unlink(name);
	local_socket = socket(PF_UNIX, SOCK_STREAM, 0);
	if (local_socket < 0) {
		log_printf(LOG_ERR, "Can't create local socket %s: %s\n", name, strerror(errno));
		write_cman_pipe("Can't create local cman socket");
		return -1;
	}
	/* Set Close-on-exec */
	fcntl(local_socket, F_SETFD, 1);
	fcntl(local_socket, F_SETFL, fcntl(local_socket, F_GETFL, 0) | O_NONBLOCK);

	memset(&sockaddr, 0, sizeof(sockaddr));
	memcpy(sockaddr.sun_path, name, name_len);
	sockaddr.sun_family = AF_UNIX;
	if (bind(local_socket, (struct sockaddr *) &sockaddr, sizeof(sockaddr))) {
		log_printf(LOG_ERR, "can't bind local socket to %s: %s\n", name, strerror(errno));
		write_cman_pipe("Can't bind to local cman socket");
		close(local_socket);
		return -1;
	}
	if (listen(local_socket, 1) != 0) {
		log_printf(LOG_ERR, "listen on %s failed: %s\n", name, strerror(errno));
		write_cman_pipe("listen failed on local cman socket");
		close(local_socket);
		return -1;
	}
	if (name[0] != '\0')
		chmod(name, mode);


	con = malloc(sizeof(struct connection));
	if (!con) {
		log_printf(LOG_ERR, "Can't allocate space for local connection: %s\n", strerror(errno));
		write_cman_pipe("malloc failed for connection info");
		close(local_socket);
		return -1;
	}
	con->type = type;
	con->fd = local_socket;
	con->num_write_msgs = 0;

	poll_dispatch_add(handle, con->fd, POLLIN, con, process_rendezvous);

	return 0;
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

	/* Unicast message */
	if (con) {
		send_reply_message(con, (struct sock_header *)&msg);
		return;
	}

	/* Broadcast message */
	list_iterate_items(thiscon, &client_list) {
		if (thiscon->events)
			send_reply_message(thiscon, (struct sock_header *)&msg);
	}
}

void notify_confchg(struct sock_header *message)
{
	struct connection *thiscon;

	list_iterate_items(thiscon, &client_list) {
		if (thiscon->confchg)
			send_reply_message(thiscon, message);
	}
}

void wake_daemon(void)
{
	P_DAEMON("Wake daemon called\n");
}


int num_listeners(void)
{
	int count = 0;
	struct connection *thiscon;

	list_iterate_items(thiscon, &client_list) {
		thiscon->shutdown_reply = SHUTDOWN_REPLY_UNK; /* Clear out for new shutdown request */
		if (thiscon->events)
			count++;
	}
	return count;
}

static void sigint_handler(int ignored)
{
	quit_threads = 1;
}

extern poll_handle aisexec_poll_handle;
int cman_init()
{
	int fd;
	struct sigaction sa;

	ais_poll_handle = aisexec_poll_handle;
	barrier_init();

	log_printf(LOG_INFO, "CMAN %s (built %s %s) started\n",
		   RELEASE_VERSION, __DATE__, __TIME__);

	fd = open_local_sock(CLIENT_SOCKNAME, sizeof(CLIENT_SOCKNAME), 0660, ais_poll_handle, CON_CLIENT);
	if (fd < 0)
		return -2;

	fd = open_local_sock(ADMIN_SOCKNAME, sizeof(ADMIN_SOCKNAME), 0600, ais_poll_handle, CON_ADMIN);
	if (fd < 0)
		return -2;

	/* Shutdown trap */
	sa.sa_handler = sigint_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, NULL);

	return 0;
}

int cman_finish()
{
	/* Stop */
	unlink(CLIENT_SOCKNAME);
 	unlink(ADMIN_SOCKNAME);

	return 0;
}

