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

int incoming(unsigned sock)
{
	struct messagebuf message;
	int err;

	if ((err = readpipe(sock, &message.head, sizeof(message.head))))
		goto pipe_error;
	if (message.head.length > maxbody)
		goto message_too_long;
	if ((err = readpipe(sock, &message.body, message.head.length)))
		goto pipe_error;

	switch (message.head.code) {
	case REPLY_CONNECT_SERVER:
		warn("Everything connected properly, all is well");
		break;
	default: 
		warn("Unknown message %x", message.head.code);
		goto pipe_error;
	}
	return 0;

message_too_long:
	warn("message %x too long (%u bytes)\n", message.head.code, message.head.length);
pipe_error:
	return -1;
}

int main(int argc, char *argv[])
{
	int sock, sockpair[2], pipepair[2];

	if (argc != 3)
		error("usage: %s <device> host:port", argv[0]);

	char *host = argv[2];
	int len = strlen(host), port = parse_port(host, &len);

	if (port < 0)
		error("expected host:port, not %s", host);
	host[len] = 0;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockpair) == -1)
		error("Can't create socket pair");

	if (!(sock = open_socket(host, port)))
		error("Can't connect to %s:%i", host, port);

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

	if (outbead(sockpair[1], CONNECT_SERVER, struct { }) < 0)
		error("Could not send connect message");

	if (send_fd(sockpair[1], sock, "fark", 4) < 0)
		error("Could not pass server connection to target");

	return incoming(sockpair[1]);
}
