/* test client */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>


#include "cnxman-socket.h"

int cluster_sock;
void get_members(void);

void signal_handler(int sig)
{
    get_members();
    return;
}

int main(int argc, char *argv[])
{

    struct sockaddr_cl saddr;
    unsigned char port = 100;
    char message[256];
    struct utsname ubuf;

    if (argc >= 2)
	port = atoi(argv[1]);

    if (argc >= 3)
	strcpy(message, argv[2]);

    printf("Cluster port number is %d\n", port);
    uname(&ubuf);
    sprintf(message, "Hello from %s", ubuf.nodename);

    signal(SIGUSR1, signal_handler);

    cluster_sock = socket(AF_CLUSTER, SOCK_DGRAM, CLPROTO_CLIENT);
    if (cluster_sock == -1)
    {
        perror("Can't open cluster socket");
        return -1;
    }

    /* Bind to our port number on the cluster. Ports < 10 are special
       and will not be blocked if the cluster loses quorum */
    saddr.scl_family = AF_CLUSTER;
    saddr.scl_port = port;

    if (bind(cluster_sock, (struct sockaddr *)&saddr, sizeof(struct sockaddr_cl)))
    {
	perror("Can't bind cluster socket");
	return -1;
    }
    fcntl(cluster_sock, F_SETFL, fcntl(cluster_sock, F_GETFL, 0) | O_NONBLOCK);

    /* Get the cluster to send us SIGUSR1 if the configuration changes */
    ioctl(cluster_sock, SIOCCLUSTER_NOTIFY, SIGUSR1);

    while (1)
    {
	char buf[1024];
	fd_set in;
	int len;
	struct iovec iov;
	struct msghdr msg;
	struct sockaddr saddr;
	struct timeval tv = {2,0};

	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_iovlen = 1;
	msg.msg_iov = &iov;
	msg.msg_name = &saddr;
	msg.msg_flags = O_NONBLOCK;
	msg.msg_namelen = sizeof(saddr);
	iov.iov_len = sizeof(buf);
	iov.iov_base = buf;

	if (write(cluster_sock, message, strlen(message)+1) < 0)
	{
	    perror("write");
	    close(cluster_sock);
	    exit(-1);
	}

	FD_ZERO(&in);
	FD_SET(cluster_sock, &in);
	while (select(cluster_sock+1, &in, NULL, NULL, &tv) > 0)
	{
	    int i;
	    struct sockaddr_cl *scl = (struct sockaddr_cl *)msg.msg_name;

	    len = recvmsg(cluster_sock, &msg, O_NONBLOCK);
	    if (len < 0)
	    {
		perror("read");
		close(cluster_sock);
		exit(-1);
	    }
	    if (len == 0)
		break; // EOF

	    buf[len] = '\0';
	    fprintf(stderr, "\nRead %d: '%s'\n", len, buf);
	    fprintf(stderr, "port=%d, nodeid = %d\n", scl->scl_port, scl->scl_nodeid);
	    for (i=0; i<14; i++)
	    {
		fprintf(stderr, "%02x  ", scl->scl_nodeid);
	    }
	    fprintf(stderr, "\n");
	}
    }
    fprintf(stderr, "EOF: finished\n");
}


void get_members(void)
{
    struct cl_cluster_node *nodes;
    int i;
    int num_nodes = ioctl(cluster_sock, SIOCCLUSTER_GETMEMBERS, 0);

    if (num_nodes == -1)
    {
	perror("get nodes");
    }
    else
    {

	printf("There are %d nodes: \n", num_nodes);

	nodes = malloc(num_nodes * sizeof(struct cl_cluster_node));
	if ( (num_nodes = ioctl(cluster_sock, SIOCCLUSTER_GETMEMBERS, nodes) >= 0) )
	{
	    for (i=0; i<num_nodes; i++)
	    {
		printf("%s %d\n", nodes[i].name, nodes[i].votes);
	    }
	}
	else
	{
	    perror("get node details");
	}
    }
}
