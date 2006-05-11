/* "sysman" client */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#define LOCAL_SOCKNAME "/var/run/sysman"
static int open_local_sock(void);

int main(int argc, char *argv[])
{
    char message[PIPE_BUF];
    int local_sock;
    int len;

    if (argc < 2)
    {
	printf("usage: sysman \"command\"\n");
	return 0;
    }

    local_sock = open_local_sock();
    if (local_sock < 0)
	exit(2);

    /* Send the command */
    write(local_sock, argv[1], strlen(argv[1])+1);

    /* Print the replies */
    while ( (len = read(local_sock, message, sizeof(message))) )
    {
	write(STDOUT_FILENO, message, len);
    }
    printf("\n");
    return 0;
}


static int open_local_sock(void)
{
    int local_socket;
    struct sockaddr_un sockaddr;

    // Open local socket
    local_socket = socket(PF_UNIX, SOCK_STREAM, 0);
    if (local_socket < 0)
    {
	perror("Can't create local socket");
	return -1;
    }

    strcpy(sockaddr.sun_path, LOCAL_SOCKNAME);
    sockaddr.sun_family = AF_UNIX;
    if (connect(local_socket, (struct sockaddr *)&sockaddr, sizeof(sockaddr)))
    {
	fprintf(stderr, "sysmand is not running\n");
	close(local_socket);
        return -1;
    }
    return local_socket;
}

