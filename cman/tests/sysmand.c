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

#include "cnxman-socket.h"
#define LOCAL_SOCKNAME "/var/run/sysman"

static struct cl_cluster_node *nodes = NULL;
static int num_nodes;
static int cluster_sock;

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

static void signal_handler(int sig)
{
    get_members();
    return;
}

int main(int argc, char *argv[])
{
    struct sockaddr_cl saddr;
    unsigned char port = CLUSTER_PORT_SYSMAN;
    int local_sock;
    struct read_fd *newfd;
    struct utsname nodeinfo;
    int expected_responses;

    cluster_sock = socket(AF_CLUSTER, SOCK_DGRAM, CLPROTO_CLIENT);
    if (cluster_sock == -1)
    {
        perror("Can't open cluster socket");
        return -1;
    }

    uname(&nodeinfo);

    /* Just a sensible default, we work out just how many
     responses we expectect properly later */
    expected_responses = num_nodes;

    /* Bind to our port number on the cluster.
       Writes to this will block if the cluster loses quorum */
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
    read_fd_head.fd   = cluster_sock;
    read_fd_head.type = CLUSTER_SOCK;

    /* Preload cluster members list */
    get_members();
    signal(SIGUSR1, signal_handler);

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
	char buf[MAX_CLUSTER_MESSAGE];
	fd_set in;
	int len;
	struct iovec iov[2];
	struct read_fd *thisfd;
	struct msghdr msg;
	struct sockaddr_cl saddr;
	struct timeval tv = {10,0};

	FD_ZERO(&in);
	for (thisfd = &read_fd_head; thisfd != NULL; thisfd = thisfd->next)
	{
	    FD_SET(thisfd->fd, &in);
	}

	if (select(FD_SETSIZE, &in, NULL, NULL, &tv) > 0)
	{
	    struct read_fd *lastfd = NULL;
	    struct read_fd *replyfd = NULL;
	    char reply[MAX_CLUSTER_MESSAGE];
	    char title[MAX_CLUSTER_MESSAGE];
	    char nodename[MAX_CLUSTER_MEMBER_NAME_LEN];
	    struct sysman_header header;
	    struct sysman_header *inheader;
	    int    status;
	    int    title_len;

	    for (thisfd = &read_fd_head; thisfd != NULL; thisfd = thisfd->next)
	    {
		if (FD_ISSET(thisfd->fd, &in))
		{
		    switch(thisfd->type)
		    {
		    /* Request or response from another cluster node */
		    case CLUSTER_SOCK:
			msg.msg_control    = NULL;
			msg.msg_controllen = 0;
			msg.msg_iovlen     = 1;
			msg.msg_iov        = iov;
			msg.msg_name       = &saddr;
			msg.msg_flags      = O_NONBLOCK;
			msg.msg_namelen    = sizeof(saddr);
			iov[0].iov_len     = sizeof(buf);
			iov[0].iov_base    = buf;

			len = recvmsg(cluster_sock, &msg, O_NONBLOCK);
			if (len < 0 && (errno == EAGAIN || errno == EINTR)) continue;
			if (len < 0)
			{
			    perror("read");
			    close(cluster_sock);
			    exit(-1);
			}
			if (len == 0)
			    goto closedown; // EOF - we have left the cluster

			/* Make sure we get more than just a header(!) */
			if (len == sizeof(struct sysman_header))
			{
			    len += recvmsg(cluster_sock, &msg, O_NONBLOCK);
			}
			inheader = (struct sysman_header *)iov[0].iov_base;

			switch (inheader->cmd)
			{
			case SYSMAN_CMD_REQUEST:
			    /* Execute command and capture stdout/stderr into 'reply'*/
			    status = exec_command(iov[0].iov_base+sizeof(struct sysman_header), reply, &len);

			    /* Send reply */
			    header.fd  = inheader->fd; /* Already in the right format */
			    header.cmd = SYSMAN_CMD_REPLY;
			    header.ret = htonl(status);

			    iov[0].iov_len  = sizeof(struct sysman_header);
			    iov[0].iov_base = &header;
			    iov[1].iov_len  = len;
			    iov[1].iov_base = reply;
			    msg.msg_iovlen  = 2;
			    if (sendmsg(cluster_sock, &msg, 0) < 0)
			    {
				perror("write");
				close(cluster_sock);
				exit(-1);
			    }
			    break;

			case SYSMAN_CMD_REPLY:
			    name_from_nodeid(saddr.scl_nodeid, nodename);
			    title_len = sprintf(title, "\nReply from %s:", nodename);
			    if (inheader->ret != 0)
				title_len += sprintf(title+title_len, " (ret=%d)", ntohl(inheader->ret));
			    strcat(title, "\n"); title_len++;
			    write(ntohl(inheader->fd), title, title_len);
			    write(ntohl(inheader->fd), iov[0].iov_base+sizeof(struct sysman_header),
				  len - sizeof(struct sysman_header));

			    replyfd = find_by_fd(ntohl(inheader->fd));
			    if (replyfd)
			    {
				/* If we've done all nodes then close the client down */
				if (++replyfd->nodes_done == expected_responses)
				{
				    close (replyfd->fd);
				    remove_sock(replyfd);
				}
			    }
			    break;

			default:
			    name_from_nodeid(saddr.scl_nodeid, nodename);
			    syslog(LOG_ERR, "Unknown sysman command received from %s: %d\n",
				   nodename, inheader->cmd);
			    break;
			}
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
			char buffer[1024];
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
			    struct sysman_header header;

			    expected_responses = nodes_listening(thisfd->fd);

			    header.fd  = htonl(thisfd->fd);
			    header.cmd = SYSMAN_CMD_REQUEST;

			    iov[0].iov_len  = sizeof(struct sysman_header);
			    iov[0].iov_base = &header;
			    iov[1].iov_len  = len;
			    iov[1].iov_base = buffer;
			    msg.msg_control    = NULL;
			    msg.msg_controllen = 0;
			    msg.msg_iovlen     = 2;
			    msg.msg_iov        = iov;
			    msg.msg_name       = NULL;
			    msg.msg_flags      = O_NONBLOCK;
			    msg.msg_namelen    = 0;

			    /* Send command to cluster */
			    if (sendmsg(cluster_sock, &msg, 0) < 0)
			    {
				perror("write");
				close(cluster_sock);
				exit(-1);
			    }

			    /* Also do local execution on this node */
			    status = exec_command(buffer, reply, &len);
			    title_len = sprintf(title, "Reply from %s:", nodeinfo.nodename);
			    if (status != 0)
				title_len += sprintf(title+title_len, " (ret=%d)", status);
			    strcat(title, "\n"); title_len++;
			    write(thisfd->fd, title, title_len);
			    write(thisfd->fd, reply, len);

				/* If we've done all nodes then close the client down */
			    if (++thisfd->nodes_done == expected_responses)
			    {
				close (thisfd->fd);
				remove_sock(thisfd);
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
    close(cluster_sock);
    close(local_sock);

    return 0;
}

/* Get a list of members */
static void get_members()
{
    struct cl_cluster_nodelist nodelist;

    num_nodes = ioctl(cluster_sock, SIOCCLUSTER_GETMEMBERS, 0);
    if (num_nodes == -1)
    {
	perror("get nodes");
    }
    else
    {
	if (nodes) free(nodes);

	nodes = malloc(num_nodes * sizeof(struct cl_cluster_node));

	nodelist.nodes = nodes;
	nodelist.max_members = num_nodes;
	num_nodes = ioctl(cluster_sock, SIOCCLUSTER_GETMEMBERS, &nodelist);
	if (num_nodes < 0)
	    perror("Error getting node list");
    }
}

/* Convert a nodeid to a node name */
static int name_from_nodeid(int nodeid, char *name)
{
    int i;

    for (i=0; i<num_nodes; i++)
    {
	if (nodeid == nodes[i].node_id)
	{
	    strcpy(name, nodes[i].name);
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
	struct cl_listen_request rq;
	int listening;

	rq.port=CLUSTER_PORT_SYSMAN;
	rq.nodeid = nodes[i].node_id;

	listening = ioctl(cluster_sock, SIOCCLUSTER_ISLISTENING, &rq);

	if (listening)
	{
	    num_listening++;
	}
	else
	{
	    char errstring[1024];
	    int len;
	    len = snprintf(errstring, sizeof(errstring),
			   "WARNING: node %s is not listening for SYSMAN requests\n",
			   nodes[i].name);
	    write(errfd, errstring, len);
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
    int avail = MAX_CLUSTER_MESSAGE-sizeof(struct sysman_header)-1;
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
