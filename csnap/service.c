#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h> 
#include <unistd.h> // read
#include <sys/wait.h>
#include <netinet/in.h>
#include "trace.h"
#include "sock.h" // send_fd, read/writepipe, connect_socket
#include "../dm-csnap.h" // message codes
#include "csnap.h" // outbead

#define trace trace_on

int monitor(int control, char *host, int port)
{
	struct messagebuf message;
	int server, err;

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
			warn("Client wants a server connection");
			if (!server)
				error("Can't connect to %s:%i", host, port);
			if (outbead(control, CONNECT_SERVER, struct { }) < 0)
				error("Could not send connect message");
			if (send_fd(control, server, "fark", 4) < 0)
				error("Could not pass server connection to target");
			break;
		case REPLY_CONNECT_SERVER:
			warn("Everything connected properly, all is well");
			break;
		default: 
			warn("Unknown message %x", message.head.code);
			goto pipe_error;
		}
	}

message_too_long:
	warn("message %x too long (%u bytes)\n", message.head.code, message.head.length);
pipe_error:
	return -1;
}

int main(int argc, char *argv[])
{
	int sockpair[2], pipepair[2];

	if (argc != 3)
		error("usage: %s <device> host:port", argv[0]);

	char *host = argv[2];
	int len = strlen(host), port = parse_port(host, &len);

	if (port < 0)
		error("expected host:port, not %s", host);
	host[len] = 0;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockpair) == -1)
		error("Can't create socket pair");

	switch (fork()) {
	case -1:
		error("fork failed");
	case 0: 
		pipe(pipepair);
		dup2(pipepair[0], 0);
		close(pipepair[0]);
		if (outbead(pipepair[1], CONTROL_SOCKET, struct {int fd;} PACKED, 3) == -1)
			error("pipe error %i, %s", errno, strerror(errno));
		close(sockpair[1]);
		execl("/sbin/dmsetup", "/sbin/dmsetup", "create", "testdev", "test.dm", NULL);
		error("exec failed, %s", strerror(errno));
	}

	close(sockpair[0]);
	if (wait(NULL) == -1)
		error("Device create failed, %s", strerror(errno));

	switch (fork()) {
	case -1:
		error("fork failed");
	case 0: 
		return monitor(sockpair[1], host, port);
	}

	warn("monitor started");
	return 0;
}
