#include <stdio.h>
#include <stdlib.h>
#include <errno.h> 
#include <unistd.h> // read, write
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/un.h>
#include <netinet/in.h>
#include "../dm-csnap.h" // message codes
//#include "csnap.h" // outbead
#include "trace.h"
#include "sock.h" // send_fd, read/writepipe, connect_socket

static int open_control_socket(char *sockname)
{
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	int addr_len = sizeof(addr) - sizeof(addr.sun_path) + strlen(sockname);
	int sock = socket(AF_UNIX, SOCK_STREAM, 0), err = 0;

	if (sock <= 0)
		error("Could not open control socket %s", sockname);

	strncpy(addr.sun_path, sockname, sizeof(addr.sun_path));
	if (sockname[0] == '@')
		addr.sun_path[0] = 0;

	if ((err = connect(sock, (struct sockaddr *)&addr, addr_len)))
		error("Could not open control socket, %s", strerror(err));

	return sock;
}

int main(int argc, char *argv[])
{
	int control = open_control_socket(argv[1]);

	char *host = argv[2];
	int len = strlen(host), port = parse_port(host, &len);
	struct { struct head head; int port; char name[len]; } PACKED message;

	if (control <= 0)
		error("Could not open control socket");

	memcpy(message.name, host, len);
	message.port = port;
	message.head = (struct head){ TEST_SERVER, sizeof(message) - sizeof(struct head) };
	if (write(control, &message, sizeof(message)) != sizeof(message))
		error("Could not send message");
	return 0;
}
