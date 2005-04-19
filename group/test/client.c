#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/errno.h>


#define COMMAND_SOCK_PATH "command_socket"


int main(int argc, char *argv[])
{
	int s, i, rv;
	struct sockaddr_un addr;
	socklen_t addrlen;
	char buf[256];

	s = socket(AF_LOCAL, SOCK_DGRAM, 0);
	if (s < 0) {
		printf("socket error %d errno %d\n", s, errno);
		exit(-1);
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_LOCAL;
	strcpy(&addr.sun_path[1], COMMAND_SOCK_PATH);
	addrlen = sizeof(sa_family_t) + strlen(addr.sun_path+1) + 1;

	memset(buf, 0, 256);

	for (i = 1; i < argc; i++) {
		if (i != 1)
			strcat(buf, " ");
		strcat(buf, argv[i]);
	}


	rv = sendto(s, buf, strlen(buf), 0, (struct sockaddr *)&addr, addrlen);

	printf("send %d \"%s\"\n", rv, buf);
	return 0;
}

