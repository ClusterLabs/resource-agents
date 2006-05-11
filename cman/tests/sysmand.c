/* "sysman" server

   Listens on a cluster port and executes commands.

   This is just a demonstration piece of code, not for production use

   *************************************
   *** IT IS A MASSIVE SECURITY HOLE ***
   *************************************

   Any command passed to it will be run as root!

*/
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <sys/errno.h>
#include <syslog.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#include "libcman.h"
#define LOCAL_SOCKNAME "/var/run/sysman"
#define CLUSTER_PORT_SYSMAN 12

static cman_node_t *nodes = NULL;
static int num_nodes;
static cman_handle_t ch;
static int expected_responses;

/* Header for all commands sent to other sysmand servers */
struct sysman_header
{
    int  fd;  /* local FD to return output to. in network byte order */
    int  ret; /* Return code of command */
    char cmd;
#define SYSMAN_CMD_REQUEST 1
#define SYSMAN_CMD_REPLY   2
};

/* One of these for each fd we are listening on
   some fields are specific to particular types.
*/
struct read_fd
{
    int fd;
    enum {CLUSTER_SOCK, LOCAL_RENDEZVOUS, LOCAL_SOCK} type;
    int nodes_done;
    time_t start_time;
    struct read_fd *next;
};
/* Head of the fd list. Also contains
   the cluster_socket details */
static struct read_fd read_fd_head;


static void get_members(void);
static int open_local_sock(void);
static int exec_command(char *cmd, char *reply, int *len);
static int name_from_nodeid(int nodeid, char *name);
static void remove_sock(struct read_fd *deadfd);
static struct read_fd *find_by_fd(int fd);
static int nodes_listening(int);

static void event_callback(cman_handle_t handle, void *private, int reason, int arg)
{
	get_members();
}

static void data_callback(cman_handle_t handle, void *private,
			  char *buf, int len, uint8_t port, int nodeid)
{
	struct read_fd *replyfd = NULL;
	char reply[PIPE_BUF];
	char title[PIPE_BUF];
	char nodename[CMAN_MAX_NODENAME_LEN];
	struct sysman_header *header;
	int    status;
	int    title_len;
	struct sysman_header *inheader = (struct sysman_header *)buf;

	switch (inheader->cmd)
	{
	case SYSMAN_CMD_REQUEST:

		/* Execute command and capture stdout/stderr into 'reply'*/
		status = exec_command(buf+sizeof(struct sysman_header), reply+sizeof(struct sysman_header), &len);

		header = (struct sysman_header *)reply;

		/* Send reply */
		header->fd  = inheader->fd; /* Already in the right format */
		header->cmd = SYSMAN_CMD_REPLY;
		header->ret = htonl(status);

		cman_send_data(ch, reply, len, 0, port, nodeid);
		break;

	case SYSMAN_CMD_REPLY:
		name_from_nodeid(nodeid, nodename);
		title_len = sprintf(title, "\nReply from %s:", nodename);
		if (inheader->ret != 0)
			title_len += sprintf(title+title_len, " (ret=%d)", ntohl(inheader->ret));
		strcat(title, "\n"); title_len++;
		write(ntohl(inheader->fd), title, title_len);
		write(ntohl(inheader->fd), buf+sizeof(struct sysman_header),
		      len - sizeof(struct sysman_header));

		replyfd = find_by_fd(ntohl(inheader->fd));
		if (replyfd)
		{
			/* If we've done all nodes then close the client down */
			if (++replyfd->nodes_done == expected_responses)
			{
				close(replyfd->fd);
				remove_sock(replyfd);
			}
		}
		break;

	default:
		name_from_nodeid(nodeid, nodename);
		syslog(LOG_ERR, "Unknown sysman command received from %s: %d\n",
		       nodename, inheader->cmd);
		break;
	}
}

int main(int argc, char *argv[])
{
    unsigned char port = CLUSTER_PORT_SYSMAN;
    int local_sock;
    struct read_fd *newfd;
    struct utsname nodeinfo;

    ch = cman_init(NULL);
    if (!ch)
    {
        perror("Can't connect to cman");
        return -1;
    }

    uname(&nodeinfo);

    if (cman_start_recv_data(ch, data_callback, port))
    {
	perror("Can't bind cluster socket");
	return -1;
    }

    cman_start_notification(ch, event_callback);

    read_fd_head.fd   = cman_get_fd(ch);
    read_fd_head.type = CLUSTER_SOCK;

    /* Preload cluster members list */
    get_members();

    /* Just a sensible default, we work out just how many
       responses we expect properly later */
    expected_responses = num_nodes;

    /* Open the Unix socket we listen for commands on */
    local_sock = open_local_sock();
    if (local_sock < 0)
	exit(2);

    newfd = malloc(sizeof(struct read_fd));
    if (!newfd)
	exit(2);

    newfd->fd   = local_sock;
    newfd->type = LOCAL_RENDEZVOUS;
    newfd->next = NULL;
    read_fd_head.next = newfd;

    while (1)
    {
	fd_set in;
	struct read_fd *thisfd;
	struct timeval tv = {10,0};

	read_fd_head.fd   = cman_get_fd(ch);
	FD_ZERO(&in);
	for (thisfd = &read_fd_head; thisfd != NULL; thisfd = thisfd->next)
	{
	    FD_SET(thisfd->fd, &in);
	}

	if (select(FD_SETSIZE, &in, NULL, NULL, &tv) > 0)
	{
	    struct read_fd *lastfd = NULL;

	    for (thisfd = &read_fd_head; thisfd != NULL; thisfd = thisfd->next)
	    {
		if (FD_ISSET(thisfd->fd, &in))
		{
		    switch(thisfd->type)
		    {
		    /* Request or response from another cluster node */
		    case CLUSTER_SOCK:
			    if (cman_dispatch(ch, CMAN_DISPATCH_ONE) == -1)
				    goto closedown;
			    break;

		    /* Someone connected to our local socket */
		    case LOCAL_RENDEZVOUS:
		    {
			struct sockaddr_un socka;
			struct read_fd *newfd;
			socklen_t sl = sizeof(socka);
			int client_fd = accept(local_sock, (struct sockaddr *)&socka, &sl);

			if (client_fd >= 0)
			{
			    newfd = malloc(sizeof(struct read_fd));
			    if (!newfd)
			    {
				close(client_fd);
				break;
			    }
			    newfd->fd    = client_fd;
			    newfd->type  = LOCAL_SOCK;
			    newfd->next  = thisfd->next;
			    newfd->nodes_done = 0;
			    newfd->start_time = time(NULL);
			    thisfd->next = newfd;
			}
		    }
		    break;

		    /* Data on a connected socket */
		    case LOCAL_SOCK:
		    {
			int len;
			char buffer[PIPE_BUF];
			len = read(thisfd->fd, buffer, sizeof(buffer));

			/* EOF on socket */
			if (len <= 0)
			{
			    struct read_fd *free_fd;

			    close(thisfd->fd);
			    /* Remove it from the list safely */
			    lastfd->next = thisfd->next;
			    free_fd = thisfd;
			    thisfd = lastfd;
			    free(free_fd);
			}
			else
			{
			    char cman_buffer[PIPE_BUF];
			    struct sysman_header *header = (struct sysman_header *)cman_buffer;

			    expected_responses = nodes_listening(thisfd->fd);

			    header->fd  = htonl(thisfd->fd);
			    header->cmd = SYSMAN_CMD_REQUEST;
			    memcpy(cman_buffer+sizeof(*header), buffer, len);

			    if (!cman_send_data(ch, cman_buffer, sizeof(*header)+len, 0, port, 0))
			    {
				perror("write");
				goto closedown;
			    }
			}
		    }
		    break;

		    } /* switch */

		}
		lastfd = thisfd;
	    }
	}
	/* Check for timed-out connections */
	for (thisfd = &read_fd_head; thisfd != NULL; thisfd = thisfd->next)
	{
	    if (thisfd->type == LOCAL_SOCK && (thisfd->start_time <= time(NULL)-10))
	    {
		write(thisfd->fd,"Timed-out\n", 10);
		close(thisfd->fd);
		remove_sock(thisfd);

		/* Refresh members list in case a node has gone down
		   or a remote sysmand has crashed */
		get_members();
	    }
	}
    }
 closedown:
    cman_finish(ch);
    close(local_sock);

    return 0;
}

/* Get a list of members */
static void get_members()
{
    num_nodes = cman_get_node_count(ch);
    if (num_nodes == -1)
    {
	perror("get nodes");
    }
    else
    {
	if (nodes) free(nodes);

	nodes = malloc(num_nodes * sizeof(cman_node_t));

	if (cman_get_nodes(ch, num_nodes, &num_nodes, nodes))
	    perror("Error getting node list");
    }
}

/* Convert a nodeid to a node name */
static int name_from_nodeid(int nodeid, char *name)
{
    int i;

    for (i=0; i<num_nodes; i++)
    {
	if (nodeid == nodes[i].cn_nodeid)
	{
	    strcpy(name, nodes[i].cn_name);
	    return 0;
	}
    }
    /* Who?? */
    strcpy(name, "Unknown");
    return -1;
}

/* Check which nodes are listening on the SYSMAN port */
static int nodes_listening(int errfd)
{
    int i;
    int num_listening = 0;

    for (i=0; i<num_nodes; i++)
    {
	int listening;

	listening = cman_is_listening(ch, nodes[i].cn_nodeid, CLUSTER_PORT_SYSMAN);

	if (listening > 0)
	{
	    num_listening++;
	}
	else
	{
		if (listening == 0)
		{
			char errstring[1024];
			int len;
			len = snprintf(errstring, sizeof(errstring),
				       "WARNING: node %s is not listening for SYSMAN requests\n",
				       nodes[i].cn_name);
			write(errfd, errstring, len);
		}
	}
    }
    return num_listening;
}

static int open_local_sock()
{
    int local_socket;
    struct sockaddr_un sockaddr;

    // Open local socket
    unlink(LOCAL_SOCKNAME);
    local_socket = socket(PF_UNIX, SOCK_STREAM, 0);
    if (local_socket < 0)
    {
	syslog(LOG_ERR, "Can't create local socket: %m");
	return -1;
    }

    strcpy(sockaddr.sun_path, LOCAL_SOCKNAME);
    sockaddr.sun_family = AF_UNIX;
    if (bind(local_socket, (struct sockaddr *)&sockaddr, sizeof(sockaddr)))
    {
	syslog(LOG_ERR, "can't bind local socket: %m");
	close(local_socket);
        return -1;
    }
    if (listen(local_socket, 1) != 0)
    {
	syslog(LOG_ERR, "listen local: %m");
	close(local_socket);
	return -1;
    }
    // Make sure only root can talk to us via the local socket.
    // Considering the rest of the security implications of
    // this code, this is simply pathetic!
    chmod(LOCAL_SOCKNAME, 0600);

    return local_socket;
}

static struct read_fd *find_by_fd(int fd)
{
    struct read_fd *thisfd;

    for (thisfd = &read_fd_head; thisfd != NULL; thisfd = thisfd->next)
	if (fd == thisfd->fd) return thisfd;

    return NULL;
}

static void remove_sock(struct read_fd *deadfd)
{
    struct read_fd *thisfd;
    struct read_fd *lastfd=NULL;

    for (thisfd = &read_fd_head; thisfd != NULL; thisfd = thisfd->next)
    {
	if (thisfd == deadfd)
	{
	    lastfd->next = deadfd->next;
	    free(deadfd);
	}
	lastfd = thisfd;
    }
}

static int exec_command(char *cmd, char *reply, int *len)
{
    FILE *pipe;
    int readlen;
    int avail = PIPE_BUF-sizeof(struct sysman_header)-1;
    char realcmd[strlen(cmd)+25];

    /* Send stderr back to the caller, and make stdin /dev/null */
    snprintf(realcmd, sizeof(realcmd), "%s </dev/null 2>&1", cmd);

    *len = 0;
    pipe = popen(realcmd, "r");

    /* Fill the buffer as full as possible */
    do
    {
	readlen = fread(reply + *len, 1, avail, pipe);
	if (readlen > 0)
	{
	    *len += readlen;
	    avail -= readlen;
	}
    }
    while (avail>0 && readlen > 0);

    reply[*len] ='\0';

    /* Return completion status of command */
    return pclose(pipe);
}
