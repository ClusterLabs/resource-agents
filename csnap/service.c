#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h> 
#include <netinet/in.h>
#include "trace.h"
#include "sock.h"
#include "../../../include/linux/dm-ioctl.h"

int main(int argc, char *argv[])
{
	int sock;

	if (argc != 3)
		error("usage: %s <device> host:port", argv[0]);

	char *host = argv[2];
	int len = strlen(host), port = strscan_port(host, &len);

	if (!len)
		error("expected host:port, not %s", host);
	host[len] = 0;

	if (!(sock = open_socket(host, port)))
		error("Can't connect to %s:%i", host, port);

	if (ioctl(open(argv[1], O_RDWR), DM_MESSAGE, (int[4]){ 0, 9, sizeof(int), sock}))
		error("Socket connect ioctl on %s failed: %s", argv[1], strerror(errno));

	return 0;
}
