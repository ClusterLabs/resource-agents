#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h> 
#include <unistd.h> // read
#include <netinet/in.h>
#include "trace.h"
#include "sock.h"
#include "../dm-csnap.h"
#include "csnap.h" // outbead
#include "../../../include/linux/dm-ioctl.h"

int send_fd(int sock, int fd, char *tag, unsigned len)
{
	char payload[CMSG_SPACE(sizeof(int))];
	struct msghdr msg = {
		.msg_control = payload,
		.msg_controllen = sizeof(payload),
		.msg_iov = &(struct iovec){ .iov_base = tag, .iov_len = len },
		.msg_iovlen = 1,
	};

	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
	*cmsg = (struct cmsghdr){ CMSG_LEN(sizeof(int)), SOL_SOCKET, SCM_RIGHTS };
	*((int *)CMSG_DATA(cmsg)) = fd; // this is really an array, .cmsg_len gives count (??)

	return sendmsg(sock, &msg, 0) != len? -EIO: len;
}

int main(int argc, char *argv[])
{
	int sock, sockpair[2];

	if (argc != 3)
		error("usage: %s <device> host:port", argv[0]);

	char *host = argv[2];
	int len = strlen(host), port = strscan_port(host, &len);

	if (!len)
		error("expected host:port, not %s", host);
	host[len] = 0;

	if (!(sock = open_socket(host, port)))
		error("Can't connect to %s:%i", host, port);

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockpair) == -1)
		error("Can't create socket pair");

	if (ioctl(open(argv[1], O_RDWR), DM_MESSAGE, (int[4]){ 0, 9, sizeof(int), sockpair[1]}))
		error("Socket connect ioctl on %s failed: %s", argv[1], strerror(errno));

	outbead(sockpair[0], SERVER_CONNECT, struct { });
	send_fd(sockpair[0], sock, "fark", 4);

	return 0;
}
