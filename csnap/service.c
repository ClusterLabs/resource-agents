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

int accept_client(char *sockname)
{
	struct pollfd pollfd = { .events = POLLIN|POLLPRI };
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	int addr_len = sizeof(addr) - sizeof(addr.sun_path) + strlen(sockname);
	int n, listener = socket(AF_UNIX, SOCK_STREAM, 0);

	strncpy(addr.sun_path, sockname, sizeof(addr.sun_path));
	if (sockname[0] == '@')
		addr.sun_path[0] = 0;

	assert(listener > 0);
	bind(listener, (struct sockaddr *)&addr, addr_len);
	n = listen(listener, 5);
	assert(!n);

	pollfd.fd = accept(listener, NULL, NULL);
	assert(pollfd.fd > 0);

	n = poll(&pollfd, 1, -1);
	assert(n == 1 && (pollfd.revents & POLLIN) != 0);

	return pollfd.fd;
}

int monitor(int control, char *host, int port)
{
	int server, err;
	struct messagebuf message;

	while (1) {
		if ((err = readpipe(control, &message.head, sizeof(message.head))))
			goto pipe_error;
		if (message.head.length > maxbody)
			goto message_too_long;
		if ((err = readpipe(control, &message.body, message.head.length)))
			goto pipe_error;
	
		switch (message.head.code) {
		case NEED_SERVER:
			server = open_socket(host, port);
			if (!server)
				error("Can't connect to %s:%i", host, port);
			if (outbead(control, CONNECT_SERVER, struct { }) < 0)
				error("Could not send connect message");
			if (send_fd(control, server, "fark", 4) < 0)
				error("Could not pass server connection to target");
			continue;
		case REPLY_CONNECT_SERVER:
			warn("Everything connected properly, all is well");
			continue;
		default: 
			warn("Unknown message %x", message.head.code);
			continue;
		}
	}
message_too_long:
	warn("message %x too long (%u bytes)\n", message.head.code, message.head.length);
pipe_error:
	return err;
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

	switch (fork()) {
	case -1:
		error("fork failed");
	case 0: 
		return monitor(accept_client(argv[1]), host, port);
	}
	return 0;
}
