#include <stdio.h>
#include <stdlib.h>
#include <errno.h> 
#include <unistd.h> // read
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/un.h>
#include <netinet/in.h>
#include "../dm-csnap.h" // message codes
#include "csnap.h" // outbead
#include "trace.h"
#include "sock.h" // send_fd, read/writepipe, connect_socket

#define trace trace_on

struct client { int sock; };
struct server { char *host; int port; int waiters; struct client *waiting[100]; };

int connect_clients(struct server *serv)
{
	while (serv->waiters)
	{
		struct client *client = serv->waiting[0];
		int control = client->sock;
		int server = open_socket(serv->host, serv->port);
		if (server < 0) {
			warn("Can't connect to %s:%i", serv->host, serv->port);
			return server;
		}
		if (outbead(control, CONNECT_SERVER, struct { }) < 0)
			error("Could not send connect message");
		if (send_fd(control, server, "fark", 4) < 0)
			error("Could not pass server connection to target");
		serv->waiting[0] = serv->waiting[--serv->waiters];
	}

	return 0;
}

int incoming(struct server *serv, struct client *client)
{
	int err;
	struct messagebuf message;
	int control = client->sock;

	if ((err = readpipe(control, &message.head, sizeof(message.head))))
		goto pipe_error;
	if (message.head.length > maxbody)
		goto message_too_long;
	if ((err = readpipe(control, &message.body, message.head.length)))
		goto pipe_error;

	switch (message.head.code) {
	case NEED_SERVER:
		serv->waiting[serv->waiters++] = client;
		connect_clients(serv);
		break;
	case REPLY_CONNECT_SERVER:
		warn("Everything connected properly, all is well");
		break;
	case TEST_SERVER: {
		struct { int port; char name[]; } *body = (void *)&message.body;
		int len = message.head.length - sizeof(body->port);
		char *nameleak = malloc(len);
		warn("Use server %.*s:%i", len, body->name, body->port);
		memcpy(nameleak, body->name, len);
		nameleak[len] = 0;
		serv->host = nameleak;
		serv->port = body->port;
		connect_clients(serv);
		break;
		}
	default: 
		warn("Unknown message %x", message.head.code);
		break;
	}
	return 0;

message_too_long:
	warn("message %x too long (%u bytes)\n", message.head.code, message.head.length);
pipe_error:
	return -1;
}

int monitor(char *sockname, struct server *serv)
{
	unsigned maxclients = 100, clients = 0, others = 1;
	struct pollfd pollvec[others+maxclients];
	struct client clientvec[maxclients];
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	int addr_len = sizeof(addr) - sizeof(addr.sun_path) + strlen(sockname);
	int listener = socket(AF_UNIX, SOCK_STREAM, 0);

	assert(listener > 0);
	strncpy(addr.sun_path, sockname, sizeof(addr.sun_path));
	if (sockname[0] == '@')
		addr.sun_path[0] = 0;
	else
		unlink(sockname);

	if (bind(listener, (struct sockaddr *)&addr, addr_len) || listen(listener, 5))
		error("Can't bind to control socket (is it in use?)");

	switch (fork()) {
	case -1:
		error("fork failed");
	case 0:
		break;
	default:
		return 0;
	}

	pollvec[0] = (struct pollfd){ .fd = listener, .events = POLLIN };
	assert(pollvec[0].fd > 0);

	while (1) {
		int activity = poll(pollvec, others+clients, -1);

		if (!activity)
			continue;

		if (activity < 0) {
			if (errno == EINTR)
				continue;
			error("poll failed, %s", strerror(errno));
		}

		/* New connection? */
		if (pollvec[0].revents) {
			struct sockaddr_in addr;
			int addr_len = sizeof(addr), sock;

			if (!(sock = accept(listener, (struct sockaddr *)&addr, &addr_len)))
				error("Cannot accept connection");
			trace_on(printf("Device %i connecting\n", clients);)
			assert(clients < maxclients); // !!! make the array bigger
			pollvec[others+clients] = (struct pollfd){ .fd = sock, .events = POLLIN };
			clientvec[clients] = (struct client){ .sock = sock };
			clients++;
		}

		/* Client activity? */
		unsigned i = 0;
		while (i < clients) {
			if (pollvec[others+i].revents) { // !!! check for poll error
				trace_off(printf("event on socket %i = %x\n",
					clientvec[i].sock, pollvec[others+i].revents);)
				if (incoming(serv, clientvec + i) == -1) {
					warn("Device %i disconnected", i);
					close(clientvec[i].sock);
					memmove(clientvec + i, clientvec + i + 1,
						sizeof(struct client) * --clients);
					continue;
				}
			}
			i++;
		}
	}
}

int main(int argc, char *argv[])
{
	if (argc != 3)
		error("usage: %s sockname host:port", argv[0]);

	char *host = argv[2];
	int len = strlen(host), port = parse_port(host, &len);
	if (port < 0)
		error("expected host:port, not %s", host);
	host[len] = 0;

	return monitor(argv[1], &(struct server){ .host = host, .port = port });
}
