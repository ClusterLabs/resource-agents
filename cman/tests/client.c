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

#include "libcman.h"

static cman_handle_t handle;
static void get_members(void);

static void event_callback(cman_handle_t handle, void *private, int reason, int arg)
{
	get_members();
}


static void data_callback(cman_handle_t handle, void *private,
			  char *buf, int len, uint8_t port, int nodeid)
{
	printf("Received from node %d port %d: '%s'\n", nodeid, port, buf);
}


int main(int argc, char *argv[])
{

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

    handle = cman_init(NULL);
    if (!handle)
    {
        perror("Can't connect to cman");
        return -1;
    }


    if (cman_start_recv_data(handle, data_callback, port))
    {
	perror("Can't bind cluster socket");
	return -1;
    }
    cman_start_notification(handle, event_callback);

    while (1)
    {

	if (cman_send_data(handle, message, strlen(message)+1,0, port, 0) < 0)
	{
	    perror("write");
	    cman_finish(handle);
	    exit(-1);
	}

	while (1)
	{
		if (cman_dispatch(handle, CMAN_DISPATCH_ALL|CMAN_DISPATCH_BLOCKING) == -1)
			break;
	}
    }
    fprintf(stderr, "EOF: finished\n");
}


void get_members(void)
{
    cman_node_t *nodes;
    int i;
    int num_nodes = cman_get_node_count(handle);

    if (num_nodes == -1)
    {
	perror("get nodes");
    }
    else
    {
	printf("There are %d nodes: \n", num_nodes);

	nodes = malloc(num_nodes * sizeof(cman_node_t));
	if ( (cman_get_nodes(handle, num_nodes, &num_nodes, nodes)))
	{
	    for (i=0; i<num_nodes; i++)
	    {
		printf("%s %d\n", nodes[i].cn_name, nodes[i].cn_nodeid);
	    }
	}
	else
	{
	    perror("get node details");
	}
    }
}
