#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h> 
#include <unistd.h> // read
#include <sys/wait.h>
#include <netinet/in.h>
#include "trace.h"
#include "sock.h"
#include "../dm-csnap.h"
#include "csnap.h" // outbead
#include "../../../include/linux/dm-ioctl.h"

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
	int sock, sockpair[2];

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
	case 0: 
		close(sockpair[0]);
		close(sockpair[1]);
		execl("/sbin/dmsetup", "/sbin/dmsetup", "create", "testdev", "test.dm", NULL);
		error("exec failed, %s", strerror(errno));
	case -1:
		error("fork failed");
	}
	if (wait(NULL) == -1)
		error("Device create failed, %s", strerror(errno));

	if (ioctl(open(argv[1], O_RDWR), DM_MESSAGE, (int[4]){ 0, 9, sizeof(int), sockpair[0]}))
		error("Socket connect ioctl on %s failed: %s", argv[1], strerror(errno));

	if (outbead(sockpair[1], CONNECT_SERVER, struct { }) < 0)
		error("Could not send connect message");

	if (send_fd(sockpair[1], sock, "fark", 4) < 0)
		error("Could not pass server connection to target");

	return incoming(sockpair[1]);
}
