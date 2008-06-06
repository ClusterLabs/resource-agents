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

#include "cnxman-socket.h"

static int cluster_sock;

static void signal_handler(int sig)
{

    return;
}


int main(int argc, char *argv[])
{
    struct sigaction sa;
    sigset_t ss;

    cluster_sock = socket(AF_CLUSTER, SOCK_DGRAM, CLPROTO_CLIENT);
    if (cluster_sock == -1)
    {
        perror("Can't open cluster socket");
        return -1;
    }
    sa.sa_handler = signal_handler;
    sa.sa_mask = ss;
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);

    if (ioctl(cluster_sock, SIOCCLUSTER_NOTIFY, SIGUSR1) == -1)
    {
	perror("Can't set up cluster notification");
	close(cluster_sock);
	return -1;
    }

    while (!ioctl(cluster_sock, SIOCCLUSTER_ISQUORATE, 0))
    {
	pause();
    }

    close(cluster_sock);

    return 0;
}
