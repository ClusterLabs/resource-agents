/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-5 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/errno.h>

#include "list.h"
#include "cnxman-socket.h"
#include "cnxman-private.h"
#include "cnxman.h"
#include "daemon.h"

#include "config.h"

static void process_incoming_packet(struct cl_comms_socket *csock,
				    struct msghdr *msg, char *data, int len);
static int cl_sendack(struct cl_comms_socket *sock, unsigned short seq,
		      int addr_len, char *addr, unsigned char remport,
		      unsigned char flag);
static void send_listen_request(int tag, int nodeid, unsigned char port);
static void send_listen_response(struct cl_comms_socket *csock, int nodeid,
				 unsigned char port, unsigned short tag);
static void resend_last_message(void);
static void start_ack_timer(void);
static int send_queued_message(struct queued_message *qmsg);
static void send_port_close_msg(unsigned char port);
static void post_close_event(unsigned char port, int nodeid);
static void process_barrier_msg(struct cl_barriermsg *msg,
				struct cluster_node *node);
static struct cl_barrier *find_barrier(char *name);
static int send_or_queue_message(void *buf, int len, struct sockaddr_cl *caddr,
				 unsigned int flags);
static struct cl_comms_socket *get_next_interface(struct cl_comms_socket *cur);
static void check_for_unacked_nodes(void);
static uint16_t generate_cluster_id(char *name);
static int is_valid_temp_nodeid(int nodeid);

static int barrier_register(struct connection *con, char *name, unsigned int flags, unsigned int nodes);
static int barrier_setattr(char *name, unsigned int attr, unsigned long arg);
static int barrier_wait(char *name);
static int barrier_delete(char *name);

extern int start_membership_services();
extern int kcl_leave_cluster(int remove);
extern int send_kill(int nodeid, int needack);
extern long gettime();

/* Pointer to the pseudo node that maintains quorum in a 2node system */
struct cluster_node *quorum_device = NULL;

/* Array of "ports" allocated. This is just a list of pointers to the sock that
 * has this port bound. Speed is a major issue here so 1-2K of allocated
 * storage is worth sacrificing. Port 0 is reserved for protocol messages */
static struct connection *port_array[256];
static pthread_mutex_t port_array_lock;

/* Our cluster name & number */
uint16_t cluster_id;
char cluster_name[MAX_CLUSTER_NAME_LEN+1];

/* Two-node mode: causes cluster to remain quorate if one of two nodes fails.
 * No more than two nodes are permitted to join the cluster. */
unsigned short two_node;

/* Cluster configuration version that must be the same among members. */
unsigned int config_version;

/* Reference counting for cluster applications */
int use_count;

/* Length of sockaddr address for our comms protocol */
unsigned int address_length;

/* Message sending */
static unsigned short cur_seq;	/* Last message sent */
static unsigned int ack_count;	/* Number of acks received for message
				 * 'cur_seq' */
static unsigned int acks_expected;	/* Number of acks we expect to receive */
static pthread_mutex_t send_lock;

/* Saved packet information in case we need to resend it */
static char saved_msg_buffer[MAX_CLUSTER_MESSAGE];
static int saved_msg_len;
static int retry_count;

/* Task variables */
extern pthread_mutex_t membership_task_lock;
extern int quit_threads;

extern int num_connections;

/* Variables owned by membership services */
extern int cluster_members;
extern struct list cluster_members_list;
extern pthread_mutex_t cluster_members_lock;
extern int we_are_a_cluster_member;
extern int cluster_is_quorate;
extern struct cluster_node *us;
extern struct list new_dead_node_list;
extern pthread_mutex_t new_dead_node_lock;
extern char nodename[];
extern int wanted_nodeid;
extern int cluster_generation;
extern enum {GROT} node_state;
extern struct cluster_node *master_node;

/* A list of processes listening for membership events */
static struct list event_listener_list;
static pthread_mutex_t event_listener_lock;

/* A list of kernel callbacks listening for membership events */
static struct list kernel_listener_list;
static pthread_mutex_t kernel_listener_lock;

/* A list of sockets we are listening on (and can transmit on...later) */
static struct list socket_list;

/* A list of all open cluster client sockets */
static struct list client_socket_list;
static pthread_mutex_t client_socket_lock;

/* A list of all current barriers */
static struct list barrier_list;
static pthread_mutex_t barrier_list_lock;

/* When a socket is read for reading it goes on this queue */
static pthread_mutex_t active_socket_lock;
static struct list active_socket_list;

/* If the cnxman process is running and available for work */
int cnxman_running;

/* Flags set by timers etc for the mainloop to detect and act upon */
static unsigned long mainloop_flags;

static struct cman_timer ack_timer;
#define ACK_TIMEOUT   1
#define RESEND_NEEDED 2

/* A queue of messages waiting to be sent */
static struct list messages_list;
static pthread_mutex_t messages_list_lock;

static pthread_mutex_t start_thread_sem;

/* List of outstanding ISLISTENING requests */
static struct list listenreq_list;
static pthread_mutex_t listenreq_lock;

/* The resend delay to use, We increase this geometrically(word?) each time a
 * send is delayed. in deci-seconds */
static int resend_delay = 1;

/* Highest numbered interface and the current default */
static int num_interfaces;
static struct cl_comms_socket *current_interface = NULL;

struct temp_node
{
	int nodeid;
	char addr[sizeof(struct sockaddr_storage)];
	int addrlen;
	struct list list;
};
static struct list tempnode_list;
static pthread_mutex_t tempnode_lock;


/* Wake up any processes that are waiting to send. This is usually called when
 * all the ACKs have been gathered up or when a node has left the cluster
 * unexpectedly and we reckon there are no more acks to collect */
static void unjam(void)
{
	wake_daemon();
}

int comms_receive_message(struct cl_comms_socket *csock)
{
	struct msghdr msg;
	struct iovec vec;
	struct sockaddr_storage sin;
	char iobuf[MAX_CLUSTER_MESSAGE];
	int len;

	memset(&sin, 0, sizeof (sin));

	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_name = &sin;
	msg.msg_namelen = sizeof (sin);
	msg.msg_flags = 0;
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;

	vec.iov_len = MAX_CLUSTER_MESSAGE;
	vec.iov_base = iobuf;

	len = recvmsg(csock->fd, &msg, MSG_DONTWAIT);
	P_COMMS("recvmsg got %d bytes\n", len);

	vec.iov_base = iobuf;

	if (len > 0) {
		if (len > MAX_CLUSTER_MESSAGE) {
			log_msg(LOG_ERR, "%d byte message far too big\n", len);
			return 0;
		}
		process_incoming_packet(csock, &msg, iobuf, len);
	}
	else {
		if (len != EAGAIN)
			log_msg(LOG_ERR, "recvmsg failed: %d\n", len);
	}
	return len;
}


static void check_for_unacked_nodes()
{
	struct list *nodelist;
	struct list *temp;
	struct cluster_node *node;

	mainloop_flags &= ~RESEND_NEEDED;
	retry_count = 0;

	P_COMMS("Retry count exceeded -- looking for dead node\n");

	/* Node did not ACK a message after <n> tries, remove it from the
	 * cluster */
	pthread_mutex_lock(&cluster_members_lock);
	list_iterate_safe(nodelist, temp, &cluster_members_list) {
		node = list_item(nodelist, struct cluster_node);

		P_COMMS("checking node %s: last_acked = %d, last_seq_sent = %d\n",
			node->name, node->last_seq_acked, node->last_seq_sent);
		if (node->state != NODESTATE_DEAD &&
		    node->last_seq_acked != node->last_seq_sent && !node->us) {

			/* Drop this lock or we can deadlock with membership */
			pthread_mutex_unlock(&cluster_members_lock);

			/* Start a state transition */
			node->leave_reason = CLUSTER_LEAVEFLAG_NORESPONSE;
			a_node_just_died(node, 1);
			pthread_mutex_lock(&cluster_members_lock);
		}
	}
	pthread_mutex_unlock(&cluster_members_lock);
	acks_expected = ack_count = 0;
	unjam();
	return;
}

void check_mainloop_flags()
{
	if (mainloop_flags & ACK_TIMEOUT) {
		mainloop_flags &= ~ACK_TIMEOUT;
		check_for_unacked_nodes();
	}

	/* Resend any unacked messages */
	if (mainloop_flags & RESEND_NEEDED && acks_expected) {
		mainloop_flags &= ~RESEND_NEEDED;
		resend_last_message();
	}

	/* Send any queued messages */
	if (acks_expected == 0) {
		struct list *temp;
		struct list *msglist;

		pthread_mutex_lock(&messages_list_lock);
		list_iterate_safe(msglist, temp, &messages_list) {
			struct queued_message *qmsg =
				list_item(msglist, struct queued_message);
			int status = send_queued_message(qmsg);

			if (status >= 0) {
				/* Suceeded, remove it from the queue */
				list_del(&qmsg->list);
				free(qmsg);
			}
			if (status == EAGAIN)
				continue; /* Try another */

			/* Did it fail horribly ?? */
			if (status < 0 && status != EAGAIN) {
				log_msg(LOG_ERR, "send_queued_message failed, error %d\n", status);
				list_del(&qmsg->list);
				free(qmsg);
			}
			break;	/* Only send one message at a time */
		}
		pthread_mutex_unlock(&messages_list_lock);
	}
}

static void ack_timer_fn(void *argc)
{
	P_COMMS("%ld: ack_timer fired, retries=%d\n", time(NULL), retry_count);

	/* Too many retries ? */
	if (++retry_count > cman_config.max_retries) {
		mainloop_flags |= ACK_TIMEOUT;
	}
	else {
		/* Resend last message */
		mainloop_flags |= RESEND_NEEDED;
	}
}

/* Called to resend a packet if sock_sendmsg was busy */
static void short_timer_fn(void *arg)
{
	/* Resend last message */
	resend_delay <<= 1;
	mainloop_flags |= RESEND_NEEDED;

	P_COMMS("short_timer fired, resend delay = %d\n", resend_delay);
}


/* Move these into daemon.c and set the select timeout */
static void start_ack_timer()
{
	P_COMMS("Start ack timer\n");
	ack_timer.callback = ack_timer_fn;
	add_timer(&ack_timer, 1, 0);
}

static void start_short_timer(void)
{
	P_COMMS("Start short timer\n");
	ack_timer.callback = short_timer_fn;
	add_timer(&ack_timer, resend_delay, 0);
}

static struct cl_waiting_listen_request *find_listen_request(unsigned short tag)
{
	struct list *llist;
	struct cl_waiting_listen_request *listener;

	list_iterate(llist, &listenreq_list) {
		listener = list_item(llist, struct cl_waiting_listen_request);
		if (listener->tag == tag) {
			return listener;
		}
	}
	return NULL;
}

static void process_ack(struct cluster_node *rem_node, unsigned short seq)
{
	if (rem_node && rem_node->state != NODESTATE_DEAD) {
		/* This copes with duplicate acks from a multipathed
		 * host */
		if (rem_node->last_seq_acked !=
		    le16_to_cpu(seq)) {
			rem_node->last_seq_acked =
				le16_to_cpu(seq);

			/* Got em all */
			if (++ack_count >= acks_expected) {

				/* Cancel the timer */
				del_timer(&ack_timer);
				acks_expected = 0;
				unjam();
			}
		}
	}
}

static void process_cnxman_message(struct cl_comms_socket *csock, char *data,
				   int len, char *addr, int addrlen,
				   struct cluster_node *rem_node)
{
	struct cl_protmsg *msg = (struct cl_protmsg *) data;
	struct cl_protheader *header = (struct cl_protheader *) data;
	struct cl_ackmsg *ackmsg;
	struct cl_listenmsg *listenmsg;
	struct cl_closemsg *closemsg;
	struct cl_barriermsg *barriermsg;
	struct cl_waiting_listen_request *listen_request;

	P_COMMS("Message on port 0 is %d\n", msg->cmd);
	switch (msg->cmd) {
	case CLUSTER_CMD_ACK:
		ackmsg = (struct cl_ackmsg *) data;

		if (rem_node && (ackmsg->aflags & 1)) {
			log_msg(LOG_WARNING, "WARNING no listener for port %d on node %s\n",
				ackmsg->remport, rem_node->name);
		}
		P_COMMS("Got ACK from %s. seq=%d (cur=%d)\n",
			rem_node ? rem_node->name : "Unknown",
			le16_to_cpu(ackmsg->header.ack), cur_seq);

		/* ACK processing has already happened */
		break;

		/* Return 1 if we have a listener on this port, 0 if not */
	case CLUSTER_CMD_LISTENREQ:
		listenmsg =
		    (struct cl_listenmsg *) (data +
					     sizeof (struct cl_protheader));
		cl_sendack(csock, header->seq, addrlen, addr, header->tgtport, 0);
		send_listen_response(csock, le32_to_cpu(header->srcid),
				     listenmsg->target_port, listenmsg->tag);
		break;

	case CLUSTER_CMD_LISTENRESP:
		/* Wake up process waiting for listen response */
		listenmsg =
		    (struct cl_listenmsg *) (data +
					     sizeof (struct cl_protheader));
		cl_sendack(csock, header->seq, addrlen, addr, header->tgtport, 0);
		pthread_mutex_lock(&listenreq_lock);
		listen_request = find_listen_request(listenmsg->tag);
		if (listen_request) {
			send_status_return(listen_request->connection, CMAN_CMD_ISLISTENING, listenmsg->listening);
			list_del(&listen_request->list);
			free(listen_request);
		}
		pthread_mutex_unlock(&listenreq_lock);
		break;

	case CLUSTER_CMD_PORTCLOSED:
		closemsg =
		    (struct cl_closemsg *) (data +
					    sizeof (struct cl_protheader));
		cl_sendack(csock, header->seq, addrlen, addr, header->tgtport, 0);
		post_close_event(closemsg->port, le32_to_cpu(header->srcid));
		break;

	case CLUSTER_CMD_BARRIER:
		barriermsg =
		    (struct cl_barriermsg *) (data +
					      sizeof (struct cl_protheader));
		cl_sendack(csock, header->seq, addrlen, addr, header->tgtport, 0);
		if (rem_node)
			process_barrier_msg(barriermsg, rem_node);
		break;

	default:
		log_msg(LOG_WARNING, "Unknown protocol message %d received\n", msg->cmd);
		break;

	}
	return;
}

static int valid_addr_for_node(struct cluster_node *node, char *addr)
{
	struct list *addrlist;
	struct cluster_node_addr *nodeaddr;

	/* We don't compare the first two bytes of the address because it's
	 * the Address Family and always in native byte order...so it will
	 * not match if we have mixed big & little-endian machines in the cluster
	 */

	list_iterate(addrlist, &node->addr_list) {
		nodeaddr = list_item(addrlist, struct cluster_node_addr);

		if (memcmp(nodeaddr->addr+2, addr+2, address_length-2) == 0)
			return 1; /* TRUE */
	}
	return 0; /* FALSE */
}

static void memcpy_fromiovec(void *data, struct iovec *vec, int len)
{
        while (len > 0) {
                if (vec->iov_len) {
                        int copy = min(len, vec->iov_len);
                        memcpy(data, vec->iov_base, copy);
                        len -= copy;
                        data += copy;
                        vec->iov_base += copy;
                        vec->iov_len -= copy;
                }
                vec++;
        }
}

static int send_to_user_port(struct cl_comms_socket *csock,
			     struct cl_protheader *header,
			     struct msghdr *msg,
			     char *recv_buf, int len)
{
	int flags = le32_to_cpu(header->flags);

        /* Get the port number and look for a listener */
	pthread_mutex_lock(&port_array_lock);
	if (port_array[header->tgtport]) {
		struct connection *c = port_array[header->tgtport];

		/* ACK it */
		if (!(flags & MSG_NOACK) && !(flags & MSG_REPLYEXP)) {

			cl_sendack(csock, header->seq, msg->msg_namelen,
				   msg->msg_name, header->tgtport, 0);
		}


		send_data_reply(c, le32_to_cpu(header->srcid), header->srcport,
				recv_buf, len);

		pthread_mutex_unlock(&port_array_lock);
	}
	else {
		/* ACK it, but set the flag bit so remote end knows no-one
		 * caught it */
		if (!(flags & MSG_NOACK))
			cl_sendack(csock, header->seq,
				   msg->msg_namelen, msg->msg_name,
				   header->tgtport, 1);

		/* Nobody listening, drop it */
		pthread_mutex_unlock(&port_array_lock);
	}
	return 0;
}

static void process_incoming_packet(struct cl_comms_socket *csock,
				    struct msghdr *msg,
				    char *data, int len)
{
	char *addr = msg->msg_name;
	int addrlen = msg->msg_namelen;
	struct cl_protheader *header = (struct cl_protheader *) data;
	int flags = le32_to_cpu(header->flags);
	struct cluster_node *rem_node =
		find_node_by_nodeid(le32_to_cpu(header->srcid));

	P_COMMS("seen message, from %d for %d, sequence num = %d, rem_node=%p, state=%d, len=%d\n",
	     le32_to_cpu(header->srcid), le32_to_cpu(header->tgtid),
	     le16_to_cpu(header->seq), rem_node,
	     rem_node ? rem_node->state : -1, len);

	/* If the remote end is being coy about its node ID then look it up by
	 * address */
	if (!rem_node && header->srcid == 0) {
		rem_node = find_node_by_addr(addr, addrlen);
	}

	/* If this node is an ex-member then treat it as unknown */
	if (rem_node && rem_node->state != NODESTATE_MEMBER
	    && rem_node->state != NODESTATE_JOINING)
		rem_node = NULL;

	/* Ignore messages not for our cluster */
	if (le16_to_cpu(header->cluster) != cluster_id) {
		P_COMMS("Dumping message - wrong cluster ID (us=%d, msg=%d)\n",
			cluster_id, header->cluster);
		goto incoming_finish;
	}

	/* If the message is from us then just dump it */
	if (rem_node && rem_node->us)
		goto incoming_finish;

	/* If we can't find the nodeid then check for our own messages the hard
	 * way - this only happens during joining */
	if (!rem_node) {
		struct list *socklist;
		struct cl_comms_socket *clsock;

		list_iterate(socklist, &socket_list) {
			clsock =
			    list_item(socklist, struct cl_comms_socket);

			if (clsock->recv_only) {
				if (memcmp(addr, &clsock->saddr, address_length) == 0) {
					goto incoming_finish;
				}
			}
		}

	}

	/* Ignore messages not for us */
	if (le32_to_cpu(header->tgtid) > 0 && us
	    && le32_to_cpu(header->tgtid) != us->node_id) {
		goto incoming_finish;
	}

	P_COMMS("got message, from %d for %d, sequence num = %d\n",
		le32_to_cpu(header->srcid), le32_to_cpu(header->tgtid),
		le16_to_cpu(header->seq));

	if (header->ack && rem_node) {
		process_ack(rem_node, header->ack);
	}

        /* Have we received this message before ? If so just ignore it, it's a
	 * resend for someone else's benefit */
	if (!(flags & MSG_NOACK) &&
	    rem_node && le16_to_cpu(header->seq) == rem_node->last_seq_recv) {
		P_COMMS
		    ("Discarding message - Already seen this sequence number %d\n",
		     rem_node->last_seq_recv);
		/* Still need to ACK it though, in case it was the ACK that got
		 * lost */
		cl_sendack(csock, header->seq, addrlen, addr, header->tgtport, 0);
		goto incoming_finish;
	}

	/* Check that the message is from the node we think it is from */
	if (rem_node && !valid_addr_for_node(rem_node, addr)) {
		return;
	}

	/* If it's a new node then assign it a temporary node ID */
	if (!rem_node)
		header->srcid = cpu_to_le32(new_temp_nodeid(addr, addrlen));

	P_COMMS("Got message: flags = %x, port = %d, we_are_a_member = %d\n",
		flags, header->tgtport, we_are_a_cluster_member);


	/* If we are not part of the cluster then ignore multicast messages
	 * that need an ACK as we will confuse the sender who is only expecting
	 * ACKS from bona fide members */
	if ((flags & MSG_MULTICAST) &&
	    !(flags & MSG_NOACK) && !we_are_a_cluster_member) {
		P_COMMS
		    ("Discarding message - multicast and we are not a cluster member. port=%d flags=%x\n",
		     header->tgtport, flags);
		goto incoming_finish;
	}

	/* Save the sequence number of this message so we can ignore duplicates
	 * (above) */
	if (!(flags & MSG_NOACK) && rem_node) {
		P_COMMS("Saving seq %d for node %s\n", le16_to_cpu(header->seq),
			rem_node->name);
		rem_node->last_seq_recv = le16_to_cpu(header->seq);
	}

	/* Is it a protocol message? */
	if (header->tgtport == 0) {
		process_cnxman_message(csock, data, len, addr, addrlen,
				       rem_node);
		goto incoming_finish;
	}

	/* Skip past the header to the data */
	send_to_user_port(csock, header, msg,
			  data + sizeof (struct cl_protheader),
			  len - sizeof (struct cl_protheader));

      incoming_finish:
	return;
}



/* Copy internal node format to userland format */
void copy_to_usernode(struct cluster_node *node,
			     struct cl_cluster_node *unode)
{
	int i;
	struct cluster_node_addr *current_addr;
	struct cluster_node_addr *node_addr;

	strcpy(unode->name, node->name);
	unode->jointime = node->join_time;
	unode->size = sizeof (struct cl_cluster_node);
	unode->votes = node->votes;
	unode->state = node->state;
	unode->us = node->us;
	unode->node_id = node->node_id;
	unode->leave_reason = node->leave_reason;
	unode->incarnation = node->incarnation;

	/* Get the address that maps to our current interface */
	i=0; /* i/f numbers start at 1 */
	list_iterate_items(node_addr, &node->addr_list) {
	        if (current_interface->number == ++i) {
		        current_addr = node_addr;
			break;
		}
	}

	/* If that failed then just use the first one */
	if (!current_addr)
 	        current_addr = (struct cluster_node_addr *)node->addr_list.n;

	memcpy(unode->addr, current_addr->addr, sizeof(struct sockaddr_storage));
}

struct cl_comms_socket *add_clsock(int broadcast, int number, int fd)
{
	struct cl_comms_socket *newsock =
		malloc(sizeof (struct cl_comms_socket));
	if (!newsock)
		return NULL;

	memset(newsock, 0, sizeof (*newsock));
	newsock->number = number;
	newsock->fd = fd;
	if (broadcast) {
		newsock->broadcast = 1;
		newsock->recv_only = 0;
	}
	else {
		newsock->broadcast = 0;
		newsock->recv_only = 1;
	}

	newsock->addr_len = sizeof(struct sockaddr_storage);

	/* Find out what it's bound to */
	memset(&newsock->saddr, 0, sizeof(newsock->saddr));
	getsockname(newsock->fd,  (struct sockaddr *)&newsock->saddr,
		    &newsock->addr_len);

	num_interfaces = max(num_interfaces, newsock->number);
	if (!current_interface && newsock->broadcast)
		current_interface = newsock;

	list_add(&socket_list, &newsock->list);

	address_length = newsock->addr_len;
	return newsock;
}

/* command processing functions */

static int do_cmd_set_version(char *cmdbuf, int *retlen)
{
	struct cl_version *version = (struct cl_version *)cmdbuf;

	if (!we_are_a_cluster_member)
		return -ENOENT;

	if (version->major != CNXMAN_MAJOR_VERSION ||
	    version->minor != CNXMAN_MINOR_VERSION ||
	    version->patch != CNXMAN_PATCH_VERSION)
		return -EINVAL;

	if (config_version == version->config)
		return 0;

	config_version = version->config;
	send_reconfigure(RECONFIG_PARAM_CONFIG_VERSION, config_version);
	return 0;
}

static int do_cmd_get_extrainfo(char *cmdbuf, char **retbuf, int retsize, int *retlen, int offset)
{
	char *outbuf = *retbuf + offset;
	struct cl_extra_info *einfo = (struct cl_extra_info *)outbuf;
	int total_votes = 0;
	int max_expected = 0;
	struct cluster_node *node;
	struct cl_comms_socket *clsock;
	char *ptr;

	pthread_mutex_lock(&cluster_members_lock);
	list_iterate_items(node, &cluster_members_list) {
		if (node->state == NODESTATE_MEMBER) {
			total_votes += node->votes;
			max_expected = max(max_expected, node->expected_votes);

		}
	}
	pthread_mutex_unlock(&cluster_members_lock);
	if (quorum_device && quorum_device->state == NODESTATE_MEMBER)
		total_votes += quorum_device->votes;

        /* Enough room for addresses ? */
	if (retsize < (sizeof(struct cl_extra_info) +
		       sizeof(struct sockaddr_storage) * num_interfaces)) {

		*retbuf = malloc(sizeof(struct cl_extra_info) + sizeof(struct sockaddr_storage) * num_interfaces);
		outbuf = *retbuf + offset;
		einfo = (struct cl_extra_info *)outbuf;

		P_COMMS("get_extrainfo: allocated new buffer\n");
	}

	einfo->node_state = node_state;
	if (master_node)
		einfo->master_node = master_node->node_id;
	else
		einfo->master_node = 0;
	einfo->node_votes = us->votes;
	einfo->total_votes = total_votes;
	einfo->expected_votes = max_expected;
	einfo->quorum = get_quorum();
	einfo->members = cluster_members;
	einfo->num_addresses = num_interfaces;

	ptr = einfo->addresses;
	list_iterate_items(clsock, &socket_list) {
		if (clsock->recv_only) {
			memcpy(ptr, &clsock->saddr, clsock->addr_len);
			ptr += sizeof(struct sockaddr_storage);
		}
	}

	*retlen = ptr - outbuf;
	return 0;
}

static int do_cmd_get_all_members(char *cmdbuf, char **retbuf, int retsize, int *retlen, int offset)
{
	struct cluster_node *node;
	struct cl_cluster_node *user_node;
	struct list *nodelist;
	char *outbuf = *retbuf + offset;
	int num_nodes = 0;
	int i;
	int total_nodes = 0;
	int highest_node;

	if (!we_are_a_cluster_member)
		return -ENOENT;

	highest_node = get_highest_nodeid();

	/* Count nodes */
	pthread_mutex_lock(&cluster_members_lock);
	list_iterate(nodelist, &cluster_members_list) {
		total_nodes++;
	}
	pthread_mutex_unlock(&cluster_members_lock);
	if (quorum_device)
		total_nodes++;

	/* If there is not enough space in the default buffer, allocate some more. */
	if ((retsize / sizeof(struct cl_cluster_node)) < total_nodes) {
		*retbuf = malloc(sizeof(struct cl_cluster_node) * total_nodes + offset);
		outbuf = *retbuf + offset;
		P_COMMS("get_all_members: allocated new buffer\n");
	}

	user_node = (struct cl_cluster_node *)outbuf;

	for (i=1; i <= highest_node; i++) {
		node = find_node_by_nodeid(i);
		if (node) {
			copy_to_usernode(node, user_node);

			user_node++;
			num_nodes++;
		}
	}
	if (quorum_device) {
		copy_to_usernode(quorum_device, user_node);
		user_node++;
		num_nodes++;
	}

	*retlen = sizeof(struct cl_cluster_node) * num_nodes;
	P_COMMS("get_all_members: retlen = %d\n", *retlen);
	return num_nodes;
}


static int do_cmd_get_cluster(char *cmdbuf, char *retbuf, int *retlen)
{
	struct cl_cluster_info *info = (struct cl_cluster_info *)retbuf;

	info->number = cluster_id;
	info->generation = cluster_generation;
	memcpy(&info->name, cluster_name, strlen(cluster_name)+1);
	*retlen = sizeof(struct cl_cluster_info);

	return 0;
}

static int do_cmd_get_node(char *cmdbuf, char *retbuf, int *retlen)
{
	struct cluster_node *node;
	struct cl_cluster_node *u_node = (struct cl_cluster_node *)cmdbuf;
	struct cl_cluster_node *r_node = (struct cl_cluster_node *)retbuf;

	if (!we_are_a_cluster_member)
		return -ENOENT;

	if (!u_node->name[0]) {
		if (u_node->node_id == 0)
			u_node->node_id = us->node_id;
		node = find_node_by_nodeid(u_node->node_id);
	}
	else
		node = find_node_by_name(u_node->name);

	if (!node)
		return -ENOENT;

	copy_to_usernode(node, r_node);
	*retlen = sizeof(struct cl_cluster_node);

	return 0;
}

static int do_cmd_set_expected(char *cmdbuf, int *retlen)
{
	struct list *nodelist;
	struct cluster_node *node;
	unsigned int total_votes;
	unsigned int newquorum;
	unsigned int newexp;

	if (!we_are_a_cluster_member)
		return -ENOENT;
	memcpy(&newexp, cmdbuf, sizeof(int));
	newquorum = calculate_quorum(1, newexp, &total_votes);

	if (newquorum < total_votes / 2
	    || newquorum > total_votes) {
		return -EINVAL;
	}

	/* Now do it */
	pthread_mutex_lock(&cluster_members_lock);
	list_iterate(nodelist, &cluster_members_list) {
		node = list_item(nodelist, struct cluster_node);
		if (node->state == NODESTATE_MEMBER
		    && node->expected_votes > newexp) {
			node->expected_votes = newexp;
		}
	}
	pthread_mutex_unlock(&cluster_members_lock);

	recalculate_quorum(1);

	send_reconfigure(RECONFIG_PARAM_EXPECTED_VOTES, newexp);

	return 0;
}

static int do_cmd_kill_node(char *cmdbuf, int *retlen)
{
	struct cluster_node *node;
	int nodeid;

	if (!we_are_a_cluster_member)
		return -ENOENT;

	memcpy(&nodeid, cmdbuf, sizeof(int));

	if ((node = find_node_by_nodeid(nodeid)) == NULL)
		return -EINVAL;

	/* Can't kill us */
	if (node->us)
		return -EINVAL;

	if (node->state != NODESTATE_MEMBER)
		return -EINVAL;

	/* Just in case it is alive, send a KILL message */
	send_kill(nodeid, 1);

	node->leave_reason = CLUSTER_LEAVEFLAG_KILLED;
	a_node_just_died(node, 1);

	return 0;
}

static int do_cmd_barrier(struct connection *con, char *cmdbuf, int *retlen)
{
	struct cl_barrier_info info;

	if (!we_are_a_cluster_member)
		return -ENOENT;

	memcpy(&info, cmdbuf, sizeof(info));

	switch (info.cmd) {
	case BARRIER_CMD_REGISTER:
		return barrier_register(con,
					info.name,
					info.flags,
					info.arg);
	case BARRIER_CMD_CHANGE:
		return barrier_setattr(info.name,
				       info.flags,
				       info.arg);
	case BARRIER_CMD_WAIT:
		return barrier_wait(info.name);
	case BARRIER_CMD_DELETE:
		return barrier_delete(info.name);
	default:
		return -EINVAL;
	}
}

static int do_cmd_islistening(struct connection *con, char *cmdbuf, int *retlen)
{
	struct cl_listen_request rq;
	struct cluster_node *rem_node;
	int nodeid;
	struct cl_waiting_listen_request *listen_request;

	if (!we_are_a_cluster_member)
		return -ENOENT;

	memcpy(&rq, cmdbuf, sizeof (rq));

	nodeid = rq.nodeid;
	if (!nodeid)
		nodeid = us->node_id;

	rem_node = find_node_by_nodeid(nodeid);

	/* Node not in the cluster */
	if (!rem_node)
		return -ENOENT;

	if (rem_node->state != NODESTATE_MEMBER)
		return -ENOTCONN;

	/* If the request is for us then just look in the ports
	 * array */
	if (rem_node->us)
		return (port_array[rq.port] != 0) ? 1 : 0;

	/* For a remote node we need to send a request out */
	listen_request = malloc(sizeof (struct cl_waiting_listen_request));
	if (!listen_request)
		return -ENOMEM;

	/* Build the request */
	listen_request->waiting = 1;
	listen_request->result = 0;
	listen_request->tag = con->fd;
	listen_request->nodeid = nodeid;
	listen_request->connection = con;

	pthread_mutex_lock(&listenreq_lock);
	list_add(&listenreq_list, &listen_request->list);
	pthread_mutex_unlock(&listenreq_lock);

	/* Now wait for the response to come back */
	send_listen_request(con->fd, rq.nodeid, rq.port);

	/* We don't actually return anything to the user until
	   the reply comes back */
	return -EWOULDBLOCK;
}

static int do_cmd_set_votes(char *cmdbuf, int *retlen)
{
	unsigned int total_votes;
	unsigned int newquorum;
	int saved_votes;
	int arg;

	if (!we_are_a_cluster_member)
		return -ENOENT;

	memcpy(&arg, cmdbuf, sizeof(int));

	/* Check votes is valid */
	saved_votes = us->votes;
	us->votes = arg;

	newquorum = calculate_quorum(1, 0, &total_votes);

	if (newquorum < total_votes / 2 || newquorum > total_votes) {
		us->votes = saved_votes;
		return -EINVAL;
	}

	recalculate_quorum(1);

	send_reconfigure(RECONFIG_PARAM_NODE_VOTES, arg);

	return 0;
}

static int do_cmd_set_nodename(char *cmdbuf, int *retlen)
{
	if (cnxman_running)
		return -EALREADY;

	strncpy(nodename, cmdbuf, MAX_CLUSTER_MEMBER_NAME_LEN);
	return 0;
}

static int do_cmd_set_nodeid(char *cmdbuf, int *retlen)
{
	int nodeid;

	memcpy(&nodeid, cmdbuf, sizeof(int));

	if (cnxman_running)
		return -EALREADY;

	if (nodeid < 0 || nodeid > 4096)
		return -EINVAL;

	wanted_nodeid = nodeid;
	return 0;
}


static int do_cmd_bind(struct connection *con, char *cmdbuf)
{
	int port;
	int ret = -EADDRINUSE;

	memcpy(&port, cmdbuf, sizeof(int));

	/* TODO: the kernel version caused a wait here. I don't
	   think we really need it though */
	if (port > HIGH_PROTECTED_PORT &&
	    (!cluster_is_quorate || in_transition())) {
	}

	pthread_mutex_lock(&port_array_lock);
	if (port_array[port])
		goto out;

	ret = 0;
	port_array[port] = con;
	con->port = port;

	pthread_mutex_unlock(&port_array_lock);

 out:
	return ret;
}

static int do_cmd_join_cluster(char *cmdbuf, int *retlen)
{
	struct cl_join_cluster_info *join_info = (struct cl_join_cluster_info *)cmdbuf;
	struct utsname un;

	if (cnxman_running)
		return -EALREADY;

	if (strlen(join_info->cluster_name) > MAX_CLUSTER_NAME_LEN)
		return -EINVAL;

	if (list_empty(&socket_list))
		return -ENOTCONN;

	set_votes(join_info->votes, join_info->expected_votes);
	cluster_id = generate_cluster_id(join_info->cluster_name);
	strncpy(cluster_name, join_info->cluster_name, MAX_CLUSTER_NAME_LEN);
	two_node = join_info->two_node;
	config_version = join_info->config_version;

	quit_threads = 0;
	acks_expected = 0;
	if (allocate_nodeid_array())
		return -ENOMEM;

	cnxman_running = 1;

	/* Make sure we have a node name */
	if (nodename[0] == '\0') {
		uname(&un);
		strcpy(nodename, un.nodename);
	}


	if (start_membership_services()) {
		return -ENOMEM;
	}

	return 0;
}

static int do_cmd_leave_cluster(char *cmdbuf, int *retlen)
{
	int leave_flags;

	if (!cnxman_running)
		return -ENOTCONN;

	if (!we_are_a_cluster_member)
		return -ENOENT;

	if (in_transition())
		return -EBUSY;

	memcpy(&leave_flags, cmdbuf, sizeof(int));

	/* Ignore the use count if FORCE is set */
	if (!(leave_flags & CLUSTER_LEAVEFLAG_FORCE)) {
		if (use_count)
			return -ENOTCONN;
	}

	us->leave_reason = leave_flags;
	quit_threads = 1;

	stop_membership_thread();
	use_count = 0;
	return 0;
}

static int do_cmd_register_quorum_device(char *cmdbuf, int *retlen)
{
	int votes;
	char *name = cmdbuf+sizeof(int);

	if (!cnxman_running)
		return -ENOTCONN;

	if (!we_are_a_cluster_member)
		return -ENOENT;

	if (quorum_device)
                return -EBUSY;

	if (strlen(name) > MAX_CLUSTER_MEMBER_NAME_LEN)
		return -EINVAL;

	if (find_node_by_name(name))
                return -EALREADY;

	memcpy(&votes, cmdbuf, sizeof(int));

	quorum_device = malloc(sizeof (struct cluster_node));
        if (!quorum_device)
                return -ENOMEM;
        memset(quorum_device, 0, sizeof (struct cluster_node));

        quorum_device->name = malloc(strlen(name) + 1);
        if (!quorum_device->name) {
                free(quorum_device);
                quorum_device = NULL;
                return -ENOMEM;
        }

        strcpy(quorum_device->name, name);
        quorum_device->votes = votes;
        quorum_device->state = NODESTATE_DEAD;
	gettimeofday(&quorum_device->join_time, NULL);

        /* Keep this list valid so it doesn't confuse other code */
        list_init(&quorum_device->addr_list);

        return 0;
}

static int do_cmd_unregister_quorum_device(char *cmdbuf, int *retlen)
{
        if (!quorum_device)
                return -EINVAL;

        if (quorum_device->state == NODESTATE_MEMBER)
                return -EBUSY;

	free(quorum_device->name);
	free(quorum_device);

        quorum_device = NULL;

        return 0;
}

static int do_cmd_poll_quorum_device(char *cmdbuf, int *retlen)
{
	int yesno;

        if (!quorum_device)
                return -EINVAL;

	memcpy(&yesno, cmdbuf, sizeof(int));

        if (yesno) {
                quorum_device->last_hello = gettime();
                if (quorum_device->state == NODESTATE_DEAD) {
                        quorum_device->state = NODESTATE_MEMBER;
                        recalculate_quorum(0);
                }
        }
        else {
                if (quorum_device->state == NODESTATE_MEMBER) {
                        quorum_device->state = NODESTATE_DEAD;
                        recalculate_quorum(0);
                }
        }
	return 0;
}

int process_command(struct connection *con, int cmd, char *cmdbuf,
		    char **retbuf, int *retlen, int retsize, int offset)
{
	int err = -EINVAL;
	struct cl_version cnxman_version;
	char *outbuf = *retbuf;

	P_COMMS("command to process is %x\n", cmd);

	switch (cmd) {

		/* Return the cnxman version number */
	case CMAN_CMD_GET_VERSION:
		err = 0;
		cnxman_version.major = CNXMAN_MAJOR_VERSION;
		cnxman_version.minor = CNXMAN_MINOR_VERSION;
		cnxman_version.patch = CNXMAN_PATCH_VERSION;
		cnxman_version.config = config_version;
		memcpy(outbuf+offset, &cnxman_version, sizeof (struct cl_version));
		*retlen = sizeof(struct cl_version);
		break;

		/* Set the cnxman config version number */
	case CMAN_CMD_SET_VERSION:
		err = do_cmd_set_version(cmdbuf, retlen);
		break;

		/* Bind to a "port" */
	case CMAN_CMD_BIND:
		err = do_cmd_bind(con, cmdbuf);
		break;

		/* Return the full membership list including dead nodes */
	case CMAN_CMD_GETALLMEMBERS:
		err = do_cmd_get_all_members(cmdbuf, retbuf, retsize, retlen, offset);
		break;

	case CMAN_CMD_GETNODE:
		err = do_cmd_get_node(cmdbuf, outbuf+offset, retlen);
		break;

	case CMAN_CMD_GETCLUSTER:
		err = do_cmd_get_cluster(cmdbuf, outbuf+offset, retlen);
		break;

	case CMAN_CMD_GETEXTRAINFO:
		err = do_cmd_get_extrainfo(cmdbuf, retbuf, retsize, retlen, offset);
		break;

	case CMAN_CMD_ISQUORATE:
		return cluster_is_quorate;

	case CMAN_CMD_ISACTIVE:
		return cnxman_running;

	case CMAN_CMD_SETEXPECTED_VOTES:
		err = do_cmd_set_expected(cmdbuf, retlen);
		break;

		/* Change the number of votes for this node */
	case CMAN_CMD_SET_VOTES:
		err = do_cmd_set_votes(cmdbuf, retlen);
		break;

		/* Return 1 if the specified node is listening on a given port */
	case CMAN_CMD_ISLISTENING:
		err = do_cmd_islistening(con, cmdbuf, retlen);
		break;

		/* Forcibly kill a node */
	case CMAN_CMD_KILLNODE:
		err = do_cmd_kill_node(cmdbuf, retlen);
		break;

	case CMAN_CMD_BARRIER:
		err = do_cmd_barrier(con, cmdbuf, retlen);
		break;

	case CMAN_CMD_SET_NODENAME:
		err = do_cmd_set_nodename(cmdbuf, retlen);
		break;

	case CMAN_CMD_SET_NODEID:
		err = do_cmd_set_nodeid(cmdbuf, retlen);
		break;

	case CMAN_CMD_JOIN_CLUSTER:
		err = do_cmd_join_cluster(cmdbuf, retlen);
		break;

	case CMAN_CMD_LEAVE_CLUSTER:
		err = do_cmd_leave_cluster(cmdbuf, retlen);
		break;

	case CMAN_CMD_GET_JOINCOUNT:
		err = num_connections;
		break;

	case CMAN_CMD_REG_QUORUMDEV:
		err = do_cmd_register_quorum_device(cmdbuf, retlen);
		break;

	case CMAN_CMD_UNREG_QUORUMDEV:
		err = do_cmd_unregister_quorum_device(cmdbuf, retlen);
		break;

	case CMAN_CMD_POLL_QUORUMDEV:
		err = do_cmd_poll_quorum_device(cmdbuf, retlen);
		break;
	}
	P_COMMS("command return code is %d\n", err);
	return err;
}

/* We'll be giving out reward points next... */
/* Send the packet and save a copy in case someone loses theirs. Should be
 * protected by the send semaphore */
static int __send_and_save(struct cl_comms_socket *csock, struct msghdr *msg,
			   struct iovec *vec, int veclen,
			   int size, int needack)
{
	int result;
	int saved_errno;
	struct iovec save_vectors[veclen];

	/* Save a copy of the IO vectors as sendmsg mucks around with them and
	 * we might want to send the same stuff out more than once (for different
	 * interfaces)
	 */
	memcpy(save_vectors, vec,
	       sizeof (struct iovec) * veclen);

	result = sendmsg(csock->fd, msg, msg->msg_flags);
	saved_errno = errno;

	if (result >= 0 && acks_expected && needack) {

		/* Start retransmit timer if it didn't go */
		if (result == 0) {
			start_short_timer();
		}
		else {
			resend_delay = 1;
		}
	}
	if (result < 0)
		log_msg(LOG_ERR, "sendmsg failed: %d\n", result);

	/* Restore IOVs */
	memcpy(vec, save_vectors,
	       sizeof (struct iovec) * veclen);

	return (result>0)?0:-saved_errno;
}

static void resend_last_message()
{
	struct msghdr msg;
	struct iovec vec[1];
	int result;

	P_COMMS("%ld resending last message: %d bytes: port=%d, len=%d\n",
		time(NULL), saved_msg_len, saved_msg_buffer[0],
		saved_msg_len);

	/* Assume there is something wrong with the last interface */
	current_interface = get_next_interface(current_interface);
	if (num_interfaces > 1)
		log_msg(LOG_INFO, "Now using interface %d\n",
		       current_interface->number);

	vec[0].iov_base = saved_msg_buffer;
	vec[0].iov_len = saved_msg_len;

	memset(&msg, 0, sizeof (msg));
	msg.msg_name = &current_interface->saddr;
	msg.msg_namelen = current_interface->addr_len;
	msg.msg_iov = vec;
	msg.msg_iovlen = 1;

	result = sendmsg(current_interface->fd, &msg, msg.msg_flags);

	if (result < 0)
		log_msg(LOG_ERR, "resend failed: %d\n", result);

	/* Try indefinitely to send this, the backlog must die down eventually
	 * !? */
	if (result == 0)
		start_short_timer();

	/* Send succeeded, continue waiting for ACKS */
	if (result > 0)
		start_ack_timer();

}

/* Send a message out on all interfaces */
static int send_to_all_ints(int nodeid, struct msghdr *our_msg,
			    struct iovec *vec, int veclen, int size, int flags)
{
	struct sockaddr_storage daddr;
	struct cl_comms_socket *clsock;
	struct list *tmp;
	int result = 0;

	our_msg->msg_name = &daddr;

	list_iterate(tmp, &socket_list) {
		clsock = list_item(tmp, struct cl_comms_socket);

		/* Don't send out a recv-only socket */
		if (!clsock->recv_only) {

			/* For temporary node IDs send to the node's real IP address */
			if (nodeid < 0) {
				get_addr_from_temp_nodeid(nodeid, (char *)&daddr, &our_msg->msg_namelen);
			}
			else {
				memcpy(&daddr, &clsock->saddr, clsock->addr_len);
				our_msg->msg_namelen = clsock->addr_len;
			}

			result = __send_and_save(clsock, our_msg, vec, veclen,
						 size + sizeof (struct cl_protheader),
						 !(flags & MSG_NOACK));
		}
	}
	return result;
}

/* Internal common send message routine */
static int __sendmsg(struct connection *con,
		     char *msg, ssize_t size, int flags,
		     int nodeid, unsigned char port)
{
	int result = 0;
	struct msghdr our_msg;
	struct cl_protheader header;
	struct iovec vectors[2];
	unsigned char srcport;

	if (size > MAX_CLUSTER_MESSAGE)
		return EINVAL;
	if (!cnxman_running)
		return ENOTCONN;

	/* Check that the node id (if present) is valid */
	if (nodeid && (!find_node_by_nodeid(nodeid) &&
		       !is_valid_temp_nodeid(nodeid))) {
		return ENOTCONN;
	}

	/* If there's no sending client socket then the source
	   port is 0: "us" */
	if (con) {
		srcport = con->port;
	}
	else {
		srcport = 0;
	}

	/* We can only have one send outstanding at a time so we might as well
	 * lock the whole send mechanism */
	pthread_mutex_lock(&send_lock);

	memset(&our_msg, 0, sizeof (our_msg));

	/* Build the header */
	header.tgtport = port;
	header.srcport = srcport;
	header.flags = flags; /* byte-swapped later */
	header.cluster = cpu_to_le16(cluster_id);
	header.srcid = us ? cpu_to_le32(us->node_id) : 0;
	header.tgtid = cpu_to_le32(nodeid);

	if (++cur_seq == 0)
		++cur_seq;

	header.seq = cpu_to_le16(cur_seq);
	header.ack = 0;

	if (header.tgtid) {
		struct cluster_node *remnode;

		remnode = find_node_by_nodeid(nodeid);
		if (remnode)  {
			header.ack = cpu_to_le16(remnode->last_seq_recv);
		}
	}

	/* Set the MULTICAST flag on messages with no particular destination */
	if (!nodeid) {
		header.flags |= MSG_MULTICAST;
		header.tgtid = 0;
	}

	/* Loopback shortcut */
	if (nodeid == us->node_id && nodeid != 0) {

		pthread_mutex_unlock(&send_lock);
		header.flags |= MSG_NOACK; /* Don't ack it! */

		return send_to_user_port(NULL, &header, &our_msg, msg, size);
	}

	/* Copy the existing iovecs into our array and add the header on at the
	 * beginning */
	vectors[0].iov_base = &header;
	vectors[0].iov_len = sizeof (header);
	vectors[1].iov_base = msg;
	vectors[1].iov_len = size;
	our_msg.msg_iov = vectors;
	our_msg.msg_iovlen = 2;

        /* Work out how many ACKS are wanted - *don't* reset acks_expected to
	 * zero if no acks are required as an ACK-needed message may still be
	 * outstanding */
	if (!(flags & MSG_NOACK)) {
		if (nodeid)
			acks_expected = 1;	/* Unicast */
		else
			acks_expected = max(cluster_members - 1, 0);

	}

	P_COMMS("Sending message - tgt=%d port %d required %d acks, seq=%d, flags=%x\n",
		nodeid, header.tgtport,
		(flags & MSG_NOACK) ? 0 : acks_expected,
		le16_to_cpu(header.seq), header.flags);

	/* Don't include temp nodeids in the message itself */
	if (header.tgtid < 0)
		header.tgtid = 0;

	header.flags = cpu_to_le32(header.flags);

	/* For non-member sends we use all the interfaces */
	if ((nodeid < 0) || (flags & MSG_ALLINT)) {

		result = send_to_all_ints(nodeid, &our_msg, vectors, 2,
					  size, flags);
	}
	else {
		/* Send to only the current socket - resends will use the
		 * others if necessary */
		our_msg.msg_name = &current_interface->saddr;
		our_msg.msg_namelen = current_interface->addr_len;

		result =
		    __send_and_save(current_interface, &our_msg,
				    vectors, 2,
				    size + sizeof (header),
				    !(flags & MSG_NOACK));
	}

	/* Make a note in each nodes' structure that it has been sent a message
	 * so we can see which ones went astray */
	if (!(flags & MSG_NOACK) && nodeid >= 0) {
		if (nodeid) {
			struct cluster_node *node;

			node = find_node_by_nodeid(le32_to_cpu(header.tgtid));
			if (node)
				node->last_seq_sent = cur_seq;
		}
		else {
			struct cluster_node *node;
			struct list *nodelist;

			list_iterate(nodelist, &cluster_members_list) {
				node = list_item(nodelist, struct cluster_node);
				if (node->state == NODESTATE_MEMBER) {
					node->last_seq_sent = cur_seq;
				}
			}
		}
	}

	/* if the client wants a broadcast message sending back to itself
	   then loop it back */
	if (nodeid == 0 && (flags & MSG_BCASTSELF)) {
		header.flags |= cpu_to_le32(MSG_NOACK); /* Don't ack it! */

		result = send_to_user_port(NULL, &header, &our_msg, msg, size);
	}

	/* Save a copy of the message if we're expecting an ACK */
	if (!(flags & MSG_NOACK) && acks_expected) {
		struct cl_protheader *savhdr = (struct cl_protheader *) saved_msg_buffer;

		memcpy_fromiovec(saved_msg_buffer, vectors,
				size + sizeof (header));

		saved_msg_len = size + sizeof (header);
		retry_count = ack_count = 0;
		mainloop_flags &= ~RESEND_NEEDED;

		/* Clear the REPLYEXPected flag so we force a real ACK
		   if it's necessary to resend this packet */
		savhdr->flags &= ~MSG_REPLYEXP;
		start_ack_timer();
	}

	pthread_mutex_unlock(&send_lock);
	return result;
}

static int queue_message(struct connection *con, void *buf, int len,
			 struct sockaddr_cl *caddr,
			 unsigned char port, int flags)
{
	struct queued_message *qmsg;

	qmsg = malloc(sizeof (struct queued_message));
	if (qmsg == NULL)
		return -1;

	memcpy(qmsg->msg_buffer, buf, len);
	qmsg->msg_len = len;
	if (caddr) {
		memcpy(&qmsg->addr, caddr, sizeof (struct sockaddr_cl));
		qmsg->addr_len = sizeof (struct sockaddr_cl);
	}
	else {
		qmsg->addr_len = 0;
		qmsg->addr.scl_port = 0;
		qmsg->addr.scl_nodeid = 0;
	}
	qmsg->flags = flags;
	qmsg->connection = con;

	pthread_mutex_lock(&messages_list_lock);
	list_add(&messages_list, &qmsg->list);
	pthread_mutex_unlock(&messages_list_lock);
	unjam();
	return 0;
}

int cl_sendmsg(struct connection *con,
	       int port, int nodeid, int flags,
	       char *msg, size_t size)
{
	int status;

	if (port == 0 && con)
		port = con->port;
	if (port == 0)
		return -EDESTADDRREQ;

	/* Queue the message if we're not prepared to send it */
	if ((port > HIGH_PROTECTED_PORT
	     && (!cluster_is_quorate || in_transition()))
	    || (acks_expected > 0 && !(flags & MSG_NOACK))) {
		struct sockaddr_cl saddr;

		saddr.scl_port = port;
		saddr.scl_nodeid = nodeid;

		return queue_message(con, msg, size, &saddr, port, flags);
	}

	status = __sendmsg(con, msg, size, flags, nodeid, port);

	return status;
}


static int send_queued_message(struct queued_message *qmsg)
{
	/* Don't send blocked messages */
	if (qmsg->addr.scl_port > HIGH_PROTECTED_PORT
	    && (!cluster_is_quorate || in_transition()))
		return EAGAIN;

	return __sendmsg(qmsg->connection, qmsg->msg_buffer, qmsg->msg_len,
			 qmsg->flags, qmsg->addr.scl_nodeid, qmsg->addr.scl_port);
}


/* Used where we are in kclusterd context and we can't allow the task to wait
 * as we are also responsible to processing the ACKs that do the wake up. Try
 * to send the message immediately and queue it if that's not possible */
static int send_or_queue_message(void *buf, int len,
				 struct sockaddr_cl *caddr,
				 unsigned int flags)
{
	/* Are we busy ? */
	if (!(flags & MSG_NOACK) && acks_expected)
		return queue_message(NULL, buf, len, caddr, 0, flags);

	else
		return __sendmsg(NULL, buf, len, MSG_DONTWAIT | flags,
				caddr->scl_nodeid, caddr->scl_port);
}

/* Send a listen request to a node */
static void send_listen_request(int tag, int nodeid, unsigned char port)
{
	struct cl_listenmsg listenmsg;
	struct sockaddr_cl caddr;

	memset(&caddr, 0, sizeof (caddr));

	/* Build the header */
	listenmsg.cmd = CLUSTER_CMD_LISTENREQ;
	listenmsg.target_port = port;
	listenmsg.listening = 0;
	listenmsg.tag = tag;

	caddr.scl_port = 0;
	caddr.scl_nodeid = nodeid;

	send_or_queue_message(&listenmsg, sizeof(listenmsg), &caddr, MSG_REPLYEXP);
	return;
}

/* Return 1 or 0 to indicate if we have a listener on the requested port */
static void send_listen_response(struct cl_comms_socket *csock, int nodeid,
				 unsigned char port, unsigned short tag)
{
	struct cl_listenmsg listenmsg;
	struct sockaddr_cl caddr;
	int status;

	memset(&caddr, 0, sizeof (caddr));

	/* Build the message */
	listenmsg.cmd = CLUSTER_CMD_LISTENRESP;
	listenmsg.target_port = port;
	listenmsg.tag = tag;
	listenmsg.listening = (port_array[port] != 0) ? 1 : 0;

	caddr.scl_port = 0;
	caddr.scl_nodeid = nodeid;

	status = send_or_queue_message(&listenmsg,
				       sizeof (listenmsg),
				       &caddr, 0);

	return;
}

/* Send an ACK */
static int cl_sendack(struct cl_comms_socket *csock, unsigned short seq,
		      int addr_len, char *addr, unsigned char remport,
		      unsigned char flag)
{
	struct iovec vec;
	struct cl_ackmsg ackmsg;
	struct msghdr msg;
	struct sockaddr_storage daddr;
	int result;

	P_COMMS("Sending ACK seq=%d\n", le16_to_cpu(seq));

	if (addr) {
		memcpy(&daddr, addr, addr_len);
	}
	else {
		memcpy(&daddr, &csock->saddr, csock->addr_len);
		addr_len = csock->addr_len;
	}

	/* Build the header */
	ackmsg.header.tgtport = 0;	/* Protocol port */
	ackmsg.header.srcport = 0;
	ackmsg.header.seq = 0;
	ackmsg.header.flags = MSG_NOACK;
	ackmsg.header.cluster = cpu_to_le16(cluster_id);
	ackmsg.header.srcid = us ? cpu_to_le32(us->node_id) : 0;
	ackmsg.header.ack = seq; /* already in LE order */
	ackmsg.header.tgtid = 0;	/* ACKS are unicast so we don't bother
					 * to look this up */
	ackmsg.cmd = CLUSTER_CMD_ACK;
	ackmsg.remport = remport;
	ackmsg.aflags = flag;
	vec.iov_base = &ackmsg;
	vec.iov_len = sizeof (ackmsg);

	memset(&msg, 0, sizeof (msg));
	msg.msg_name = &daddr;
	msg.msg_namelen = addr_len;
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;

	result = sendmsg(csock->fd, &msg, 0);

	if (result < 0)
		log_msg(LOG_ERR, "error sending ACK: %d\n", result);

	return result;

}

/* Send a port closedown message to all cluster nodes - this tells them that a
 * port listener has gone away */
static void send_port_close_msg(unsigned char port)
{
	struct cl_closemsg closemsg;
	struct sockaddr_cl caddr;

	caddr.scl_port = 0;
	caddr.scl_nodeid = 0;

	/* Build the header */
	closemsg.cmd = CLUSTER_CMD_PORTCLOSED;
	closemsg.port = port;

	send_or_queue_message(&closemsg, sizeof (closemsg), &caddr, 0);
	return;
}

/* A remote port has been closed - post an OOB message to the local listen on
 * that port (if there is one) */
static void post_close_event(unsigned char port, int nodeid)
{
	struct connection *con;

	pthread_mutex_lock(&port_array_lock);
	con = port_array[port];
	pthread_mutex_unlock(&port_array_lock);

	if (con)
		notify_listeners(con, EVENT_REASON_PORTCLOSED, nodeid);
}

/* If "cluster_is_quorate" is 0 then all activity apart from protected ports is
 * blocked. */
void set_quorate(int total_votes)
{
	int quorate;

	if (get_quorum() > total_votes) {
		quorate = 0;
	}
	else {
		quorate = 1;
	}

	if (cluster_is_quorate && !quorate)
		log_msg(LOG_INFO, "quorum lost, blocking activity\n");
	if (!cluster_is_quorate && quorate)
		log_msg(LOG_INFO, "quorum regained, resuming activity\n");

	cluster_is_quorate = quorate;

	/* Wake up any sleeping processes */
	if (cluster_is_quorate) {
		unjam();
	}

}

/* This fills in the list of all addresses for the local node */
void get_local_addresses(struct cluster_node *node)
{
	struct list *socklist;
	struct cl_comms_socket *sock;

	list_iterate(socklist, &socket_list) {
		sock = list_item(socklist, struct cl_comms_socket);

		if (sock->recv_only) {
			add_node_address(node, (unsigned char *) &sock->saddr, address_length);
		}
	}
}


static uint16_t generate_cluster_id(char *name)
{
	int i;
	int value = 0;

	for (i=0; i<strlen(name); i++) {
		value <<= 1;
		value += name[i];
	}
	P_COMMS("Generated cluster id for '%s' is %d\n", name, value & 0xFFFF);
	return value & 0xFFFF;
}

/* Return the next comms socket we can use. */
static struct cl_comms_socket *get_next_interface(struct cl_comms_socket *cur)
{
	int next;
	struct list *socklist;

	/* Fast path for single interface systems */
	if (num_interfaces <= 1)
		return cur;

	/* Next number */
	next = cur->number + 1;
	if (next > num_interfaces)
		next = 1;

	/* Find the socket with this number, I could optimise this by starting
	 * at the current i/f but most systems are going to have a small number
	 * of them anyway */
	list_iterate(socklist, &socket_list) {
		struct cl_comms_socket *sock;
		sock = list_item(socklist, struct cl_comms_socket);

		if (!sock->recv_only && sock->number == next)
			return sock;
	}

	return NULL;
}


static void send_barrier_complete_msg(struct cl_barrier *barrier)
{
	if (barrier->timeout) {
		del_timer(&barrier->timer);
		barrier->timeout = 0;
	}

	if (!barrier->client_complete) {
		send_status_return(barrier->con, CMAN_CMD_BARRIER, barrier->endreason);
		barrier->client_complete = 1;
	}
}

/* MUST be called with the barrier list lock held */
static struct cl_barrier *find_barrier(char *name)
{
	struct list *blist;
	struct cl_barrier *bar;

	list_iterate(blist, &barrier_list) {
		bar = list_item(blist, struct cl_barrier);

		if (strcmp(name, bar->name) == 0)
			return bar;
	}
	return NULL;
}

/* Do the stuff we need to do when the barrier has completed phase 1 */
static void check_barrier_complete_phase1(struct cl_barrier *barrier)
{
	if (barrier->got_nodes == ((barrier->expected_nodes != 0)
				   ? barrier->expected_nodes :
				   cluster_members)) {

		struct cl_barriermsg bmsg;

		barrier->completed_nodes++;	/* We have completed */
		barrier->phase = 2;	/* Wait for complete phase II */

		/* Send completion message, remember: we are in cnxman context
		 * and must not block */
		bmsg.cmd = CLUSTER_CMD_BARRIER;
		bmsg.subcmd = BARRIER_COMPLETE;
		strcpy(bmsg.name, barrier->name);

		P_BARRIER("Sending COMPLETE for %s\n", barrier->name);
		queue_message(NULL, (char *) &bmsg, sizeof (bmsg), NULL, 0, 0);
	}
}

/* Do the stuff we need to do when the barrier has been reached */
/* Return 1 if we deleted the barrier */
static int check_barrier_complete_phase2(struct cl_barrier *barrier, int status)
{
	P_BARRIER("check_complete_phase2 for %s\n", barrier->name);
	pthread_mutex_lock(&barrier->phase2_lock);

	P_BARRIER("check_complete_phase2 status=%d, c_nodes=%d, e_nodes=%d, state=%d\n",
		  status,barrier->completed_nodes, barrier->completed_nodes, barrier->state);

	if (barrier->state != BARRIER_STATE_COMPLETE &&
	    (status == -ETIMEDOUT ||
	     barrier->completed_nodes == ((barrier->expected_nodes != 0)
					  ? barrier->expected_nodes : cluster_members))) {

		barrier->endreason = status;

		/* Wake up listener */
		if (barrier->state == BARRIER_STATE_WAITING) {
			send_barrier_complete_msg(barrier);
		}
		barrier->state = BARRIER_STATE_COMPLETE;
	}
	pthread_mutex_unlock(&barrier->phase2_lock);

	/* Delete barrier if autodelete */
	if (barrier->flags & BARRIER_ATTR_AUTODELETE) {
		pthread_mutex_lock(&barrier_list_lock);
		list_del(&barrier->list);
		free(barrier);
		pthread_mutex_unlock(&barrier_list_lock);
		return 1;
	}

	return 0;
}

/* Called if a barrier timeout happens */
static void barrier_timer_fn(void *arg)
{
	struct cl_barrier *barrier = arg;

	P_BARRIER("Barrier timer_fn called for %s\n", barrier->name);

	/* Ignore any futher messages, they are too late. */
	barrier->phase = 0;

	/* and cause it to timeout */
	check_barrier_complete_phase2(barrier, -ETIMEDOUT);
}

/* Process BARRIER messages from other nodes */
static void process_barrier_msg(struct cl_barriermsg *msg,
				struct cluster_node *node)
{
	struct cl_barrier *barrier;

	pthread_mutex_lock(&barrier_list_lock);
	barrier = find_barrier(msg->name);
	pthread_mutex_unlock(&barrier_list_lock);

	/* Ignore other peoples messages, in_transition() is needed here so
	 * that joining nodes will see their barrier messages before the
	 * we_are_a_cluster_member is set */
	if (!we_are_a_cluster_member && !in_transition())
		return;
	if (!barrier)
		return;

	P_BARRIER("Got %d for %s, from node %s\n", msg->subcmd, msg->name,
		  node ? node->name : "unknown");

	switch (msg->subcmd) {
	case BARRIER_WAIT:
		pthread_mutex_lock(&barrier->lock);
		if (barrier->phase == 0)
			barrier->phase = 1;

		if (barrier->phase == 1) {
			barrier->got_nodes++;
			check_barrier_complete_phase1(barrier);
		}
		else {
			log_msg(LOG_WARNING, "got WAIT barrier not in phase 1 %s (%d)\n",
			       msg->name, barrier->phase);

		}
		pthread_mutex_unlock(&barrier->lock);
		break;

	case BARRIER_COMPLETE:
		pthread_mutex_lock(&barrier->lock);
		barrier->completed_nodes++;

		/* First node to get all the WAIT messages sends COMPLETE, so
		 * we all complete */
		if (barrier->phase == 1) {
			barrier->got_nodes = barrier->expected_nodes;
			check_barrier_complete_phase1(barrier);
		}

		if (barrier->phase == 2) {
			/* If it was deleted (ret==1) then no need to unlock
			 * the mutex */
			if (check_barrier_complete_phase2(barrier, 0) == 1)
				return;
		}
		pthread_mutex_unlock(&barrier->lock);
		break;
	}
}



/* Barrier API */
static int barrier_register(struct connection *con, char *name, unsigned int flags, unsigned int nodes)
{
	struct cl_barrier *barrier;

	/* We are not joined to a cluster */
	if (!we_are_a_cluster_member)
		return -ENOTCONN;

	/* Must have a valid name */
	if (name == NULL || strlen(name) > MAX_BARRIER_NAME_LEN - 1)
		return -EINVAL;

	/* We don't do this yet */
	if (flags & BARRIER_ATTR_MULTISTEP)
		return -EINVAL;

	P_BARRIER("barrier_register %s, nodes = %d, flags =%x\n", name, nodes, flags);
	pthread_mutex_lock(&barrier_list_lock);

	/* See if it already exists */
	if ((barrier = find_barrier(name))) {
		pthread_mutex_unlock(&barrier_list_lock);
		if (nodes != barrier->expected_nodes) {
			log_msg(LOG_ERR, "Barrier registration failed for '%s', expected nodes=%d, requested=%d\n",
			       name, barrier->expected_nodes, nodes);
			return -EINVAL;
		}
		else {
			/* Fill this is as it may have been remote registered */
			barrier->con = con;
			return 0;
		}
	}

	/* Build a new struct and add it to the list */
	barrier = malloc(sizeof (struct cl_barrier));
	if (barrier == NULL) {
		pthread_mutex_unlock(&barrier_list_lock);
		return -ENOMEM;
	}
	memset(barrier, 0, sizeof (*barrier));

	strcpy(barrier->name, name);
	barrier->flags = flags;
	barrier->expected_nodes = nodes;
	barrier->got_nodes = 0;
	barrier->completed_nodes = 0;
	barrier->endreason = 0;
	barrier->registered_nodes = 1;
	barrier->con = con;
	pthread_mutex_init(&barrier->phase2_lock, NULL);
	barrier->state = BARRIER_STATE_INACTIVE;
	pthread_mutex_init(&barrier->lock, NULL);

	list_add(&barrier_list, &barrier->list);
	pthread_mutex_unlock(&barrier_list_lock);

	return 0;
}

static int barrier_setattr_enabled(struct cl_barrier *barrier,
				   unsigned int attr, unsigned long arg)
{
	int status;

	/* Can't disable a barrier */
	if (!arg) {
		pthread_mutex_unlock(&barrier->lock);
		return -EINVAL;
	}

	/* We need to send WAIT now because the user may not
	 * actually call barrier_wait() */
	if (!barrier->waitsent) {
		struct cl_barriermsg bmsg;

		/* Send it to the rest of the cluster */
		bmsg.cmd = CLUSTER_CMD_BARRIER;
		bmsg.subcmd = BARRIER_WAIT;
		strcpy(bmsg.name, barrier->name);

		barrier->waitsent = 1;
		barrier->phase = 1;

		barrier->got_nodes++;

		/* Start the timer if one was wanted */
		if (barrier->timeout) {
			barrier->timer.callback = barrier_timer_fn;
			barrier->timer.arg =  barrier;
			add_timer(&barrier->timer, barrier->timeout, 0);
		}

		/* Barrier WAIT and COMPLETE messages are
		 * always queued - that way they always get
		 * sent out in the right order. If we don't do
		 * this then one can get sent out in the
		 * context of the user process and the other in
		 * cnxman and COMPLETE may /just/ slide in
		 * before WAIT if its in the queue
		 */
		P_BARRIER("Sending WAIT for %s\n", barrier->name);
		status = queue_message(NULL, &bmsg, sizeof (bmsg), NULL, 0, 0);
		if (status < 0) {
			pthread_mutex_unlock(&barrier->lock);
			return status;
		}

		/* It might have been reached now */
		if (barrier
		    && barrier->state != BARRIER_STATE_COMPLETE
		    && barrier->phase == 1)
			check_barrier_complete_phase1(barrier);
	}
	if (barrier && barrier->state == BARRIER_STATE_COMPLETE) {
		pthread_mutex_unlock(&barrier->lock);
		return barrier->endreason;
	}
	pthread_mutex_unlock(&barrier->lock);
	return 0;	/* Nothing to propogate */
}

static int barrier_setattr(char *name, unsigned int attr, unsigned long arg)
{
	struct cl_barrier *barrier;

	/* See if it already exists */
	pthread_mutex_lock(&barrier_list_lock);
	if (!(barrier = find_barrier(name))) {
		pthread_mutex_unlock(&barrier_list_lock);
		return -ENOENT;
	}
	pthread_mutex_unlock(&barrier_list_lock);

	pthread_mutex_lock(&barrier->lock);
	if (barrier->state == BARRIER_STATE_COMPLETE) {
		pthread_mutex_unlock(&barrier->lock);
		return 0;
	}

	switch (attr) {
	case BARRIER_SETATTR_AUTODELETE:
		if (arg)
			barrier->flags |= BARRIER_ATTR_AUTODELETE;
		else
			barrier->flags &= ~BARRIER_ATTR_AUTODELETE;
		pthread_mutex_unlock(&barrier->lock);
		return 0;
		break;

	case BARRIER_SETATTR_TIMEOUT:
		/* Can only change the timout of an inactive barrier */
		if (barrier->state == BARRIER_STATE_WAITING
		    || barrier->waitsent) {
			pthread_mutex_unlock(&barrier->lock);
			return -EINVAL;
		}
		barrier->timeout = arg;
		pthread_mutex_unlock(&barrier->lock);
		return 0;

	case BARRIER_SETATTR_MULTISTEP:
		pthread_mutex_unlock(&barrier->lock);
		return -EINVAL;

	case BARRIER_SETATTR_ENABLED:
		return barrier_setattr_enabled(barrier, attr, arg);

	case BARRIER_SETATTR_NODES:
		/* Can only change the expected node count of an inactive
		 * barrier */
		if (barrier->state == BARRIER_STATE_WAITING
		    || barrier->waitsent)
			return -EINVAL;
		barrier->expected_nodes = arg;
		break;
	}

	pthread_mutex_unlock(&barrier->lock);
	return 0;
}

static int barrier_delete(char *name)
{
	struct cl_barrier *barrier;

	pthread_mutex_lock(&barrier_list_lock);

	/* See if it exists */
	if (!(barrier = find_barrier(name))) {
		pthread_mutex_unlock(&barrier_list_lock);
		return -ENOENT;
	}

	/* Delete it */
	list_del(&barrier->list);
	free(barrier);
	pthread_mutex_unlock(&barrier_list_lock);
	return 0;
}

static int barrier_wait(char *name)
{
	struct cl_barrier *barrier;

	if (!cnxman_running)
		return -ENOTCONN;

	/* Enable it */
	barrier_setattr(name, BARRIER_SETATTR_ENABLED, 1L);

	pthread_mutex_lock(&barrier_list_lock);

	/* See if it still exists - enable may have deleted it! */
	if (!(barrier = find_barrier(name))) {
		pthread_mutex_unlock(&barrier_list_lock);
		return -ENOENT;
	}

	pthread_mutex_lock(&barrier->lock);

	pthread_mutex_unlock(&barrier_list_lock);

	/* If it has already completed then return the status */
	if (barrier->state == BARRIER_STATE_COMPLETE) {
		pthread_mutex_unlock(&barrier->lock);
		send_barrier_complete_msg(barrier);
	}
	else {
		barrier->state = BARRIER_STATE_WAITING;
	}
	pthread_mutex_unlock(&barrier->lock);

	/* User will wait */
	return -EWOULDBLOCK;
}

/* This is called from membership services when a node has left the cluster -
 * we signal all waiting barriers with ESRCH so they know to do something
 * else, if the number of nodes is left at 0 then we compare the new number of
 * nodes in the cluster with that at the barrier and return 0 (success) in that
 * case */
void check_barrier_returns()
{
	struct list *blist;
	struct list *llist;
	struct cl_barrier *barrier;
	int status = 0;

	pthread_mutex_lock(&barrier_list_lock);
	list_iterate(blist, &barrier_list) {
		barrier = list_item(blist, struct cl_barrier);

		if (barrier->waitsent) {
			int wakeit = 0;

			/* Check for a dynamic member barrier */
			if (barrier->expected_nodes == 0) {
				if (barrier->registered_nodes ==
				    cluster_members) {
					status = 0;
					wakeit = 1;
				}
			}
			else {
				status = ESRCH;
				wakeit = 1;
			}

			/* Do we need to tell the barrier? */
			if (wakeit) {
				if (barrier->state == BARRIER_STATE_WAITING) {
					barrier->endreason = status;
					send_barrier_complete_msg(barrier);
				}
			}
		}
	}
	pthread_mutex_unlock(&barrier_list_lock);

	/* Part 2 check for outstanding listen requests for dead nodes and
	 * cancel them */
	pthread_mutex_lock(&listenreq_lock);
	list_iterate(llist, &listenreq_list) {
		struct cl_waiting_listen_request *lrequest =
		    list_item(llist, struct cl_waiting_listen_request);
		struct cluster_node *node =
		    find_node_by_nodeid(lrequest->nodeid);

		if (node && node->state != NODESTATE_MEMBER) {
			send_status_return(lrequest->connection, CMAN_CMD_ISLISTENING, -ENOTCONN);
		}
	}
	pthread_mutex_unlock(&listenreq_lock);
}


int get_addr_from_temp_nodeid(int nodeid, char *addr, unsigned int *addrlen)
{
	struct temp_node *tn;
	struct list *tmp;
	int err = 1; /* true */

	pthread_mutex_lock(&tempnode_lock);

	list_iterate(tmp, &tempnode_list) {
		tn = list_item(tmp, struct temp_node);
		if (tn->nodeid == nodeid) {
			memcpy(addr, tn->addr, tn->addrlen);
			*addrlen = tn->addrlen;

			goto out;
		}
	}
	err = 0;

 out:
	pthread_mutex_unlock(&tempnode_lock);
	return err;
}

/* Create a new temporary node ID. This list will only ever be very small
   (usaully only 1 item) but I can't take the risk that someone won't try to
   boot 128 nodes all at exactly the same time. */
int new_temp_nodeid(char *addr, int addrlen)
{
	struct temp_node *tn;
	struct list *tmp;
	int err = -1;
	int try_nodeid = 0;

	pthread_mutex_lock(&tempnode_lock);

	/* First see if we already know about this node */
	list_iterate(tmp, &tempnode_list) {
		tn = list_item(tmp, struct temp_node);

		/* We're already in here... */
		if (tn->addrlen == addrlen &&
		    memcmp(tn->addr, addr, addrlen) == 0) {
			P_COMMS("reused temp node ID %d\n", tn->nodeid);
			err = tn->nodeid;
			goto out;
		}
	}

	/* Nope, OK, invent a suitable number */
 retry:
	try_nodeid -= 1;
	list_iterate(tmp, &tempnode_list) {
		tn = list_item(tmp, struct temp_node);

		if (tn->nodeid == try_nodeid)
			goto retry;
	}

	tn = malloc(sizeof(struct temp_node));
	if (!tn)
		goto out;

	memcpy(tn->addr, addr, addrlen);
	tn->addrlen = addrlen;
	tn->nodeid = try_nodeid;
	list_add(&tempnode_list, &tn->list);
	err = try_nodeid;
	P_COMMS("new temp nodeid = %d\n", try_nodeid);
 out:
	pthread_mutex_unlock(&tempnode_lock);
	return err;
}

void unbind_con(struct connection *con)
{
	pthread_mutex_lock(&port_array_lock);
	if (con->port) {
		port_array[con->port] = NULL;
		send_port_close_msg(con->port);
	}
	pthread_mutex_unlock(&port_array_lock);

	con->port = 0;
}

static int is_valid_temp_nodeid(int nodeid)
{
	struct temp_node *tn;
	struct list *tmp;
	int err = 1; /* true */

	pthread_mutex_lock(&tempnode_lock);

	list_iterate(tmp, &tempnode_list) {
		tn = list_item(tmp, struct temp_node);
		if (tn->nodeid == nodeid)
			goto out;
	}
	err = 0;

 out:
	P_COMMS("is_valid_temp_nodeid. %d = %d\n", nodeid, err);
	pthread_mutex_unlock(&tempnode_lock);
	return err;
}

/*
 * Remove any temp nodeIDs that refer to now-valid cluster members.
 */
void purge_temp_nodeids()
{
	struct temp_node *tn;
	struct list *listtmp;
	struct list *listtmp1;
	struct cluster_node *node;
	struct cluster_node_addr *nodeaddr;

	pthread_mutex_lock(&tempnode_lock);
	pthread_mutex_lock(&cluster_members_lock);

	/*
	 * The ordering of these nested lists is deliberately
	 * arranged for the fewest list traversals overall
	 */

	/* For each node... */
	list_iterate_items(node, &cluster_members_list) {
		if (node->state == NODESTATE_MEMBER) {
			/* ...We check the temp node ID list... */
			list_iterate_safe(listtmp, listtmp1, &tempnode_list) {
				tn = list_item(listtmp, struct temp_node);

				/* ...against that node's address */
				list_iterate_items(nodeaddr, &node->addr_list) {

					if (memcmp(nodeaddr->addr, tn->addr, tn->addrlen) == 0) {
						list_del(&tn->list);
						free(tn);
					}
				}
			}
		}
	}
	pthread_mutex_unlock(&cluster_members_lock);
	pthread_mutex_unlock(&tempnode_lock);
}


int cluster_init(void)
{
	pthread_mutex_init(&start_thread_sem, NULL);
	pthread_mutex_init(&send_lock, NULL);
	pthread_mutex_init(&barrier_list_lock, NULL);
	pthread_mutex_init(&cluster_members_lock, NULL);
	pthread_mutex_init(&port_array_lock, NULL);
	pthread_mutex_init(&messages_list_lock, NULL);
	pthread_mutex_init(&listenreq_lock, NULL);
	pthread_mutex_init(&client_socket_lock, NULL);
	pthread_mutex_init(&event_listener_lock, NULL);
	pthread_mutex_init(&kernel_listener_lock, NULL);
	pthread_mutex_init(&tempnode_lock, NULL);
	pthread_mutex_init(&active_socket_lock, NULL);
	pthread_mutex_init(&new_dead_node_lock, NULL);
	pthread_mutex_init(&membership_task_lock, NULL);

	list_init(&event_listener_list);
	list_init(&kernel_listener_list);
	list_init(&socket_list);
	list_init(&client_socket_list);
	list_init(&active_socket_list);
	list_init(&barrier_list);
	list_init(&messages_list);
	list_init(&listenreq_list);
	list_init(&cluster_members_list);
	list_init(&new_dead_node_list);
	list_init(&tempnode_list);

	cnxman_running = 0;

	return 0;
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
