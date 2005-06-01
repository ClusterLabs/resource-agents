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
#include "barrier.h"
#include "commands.h"
#include "config.h"
#include "membership.h"
#include "logging.h"

static void process_incoming_packet(struct cl_comms_socket *csock,
				    struct msghdr *msg, char *data, int len);
static int cl_sendack(struct cl_comms_socket *sock, unsigned short seq,
		      int addr_len, char *addr, unsigned char remport,
		      unsigned char flag);
static void resend_last_message(void);
static void start_ack_timer(void);
static int send_queued_message(struct queued_message *qmsg);
static struct cl_comms_socket *get_next_interface(struct cl_comms_socket *cur);
static void check_for_unacked_nodes(void);
static int is_valid_temp_nodeid(int nodeid);

/* Pointer to the pseudo node that maintains quorum in a 2node system */
struct cluster_node *quorum_device = NULL;

/* Our cluster name & number */
uint16_t cluster_id;
char cluster_name[MAX_CLUSTER_NAME_LEN+1];

/* Two-node mode: causes cluster to remain quorate if one of two nodes fails.
 * No more than two nodes are permitted to join the cluster. */
unsigned short two_node;

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

/* The resend delay to use, We increase this geometrically(word?) each time a
 * send is delayed. in deci-seconds */
static int resend_delay = 1;

/* Highest numbered interface and the current default */
int num_interfaces;
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


static void process_incoming_packet(struct cl_comms_socket *csock,
				    struct msghdr *msg,
				    char *data, int len)
{
	char *addr = msg->msg_name;
	int addrlen = msg->msg_namelen;
	struct cl_protheader *header = (struct cl_protheader *) data;
	int flags = le32_to_cpu(header->flags);
	int caught;
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
		if (process_cnxman_message(data, len, addr, addrlen,
					   rem_node)) {
			cl_sendack(csock, header->seq, msg->msg_namelen,
				   msg->msg_name, header->tgtport, 0);
		}
		goto incoming_finish;
	}

	/* Skip past the header to the data */
	caught = send_to_user_port(header, msg,
				   data + sizeof (struct cl_protheader),
				   len - sizeof (struct cl_protheader));

	/* ACK it */
	if (!(flags & MSG_NOACK) && !(flags & MSG_REPLYEXP)) {
		cl_sendack(csock, header->seq, msg->msg_namelen,
			   msg->msg_name, header->tgtport, caught);
	}

      incoming_finish:
	return;
}

/* The return length has already been calculated by the caller */
char *get_interface_addresses(char *ptr)
{
	struct cl_comms_socket *clsock;

	list_iterate_items(clsock, &socket_list) {
		if (clsock->recv_only) {
			memcpy(ptr, &clsock->saddr, clsock->addr_len);
			ptr += sizeof(struct sockaddr_storage);
		}
	}
	return ptr;
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
		return send_to_user_port(&header, &our_msg, msg, size);
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
		result = send_to_user_port(&header, &our_msg, msg, size);
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

int queue_message(struct connection *con, void *buf, int len,
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
int send_or_queue_message(void *buf, int len,
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

int current_interface_num()
{
	return current_interface->number;
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
	pthread_mutex_init(&cluster_members_lock, NULL);
	pthread_mutex_init(&messages_list_lock, NULL);
	pthread_mutex_init(&client_socket_lock, NULL);
	pthread_mutex_init(&event_listener_lock, NULL);
	pthread_mutex_init(&kernel_listener_lock, NULL);
	pthread_mutex_init(&tempnode_lock, NULL);
	pthread_mutex_init(&active_socket_lock, NULL);


	list_init(&event_listener_list);
	list_init(&kernel_listener_list);
	list_init(&socket_list);
	list_init(&client_socket_list);
	list_init(&active_socket_list);
	list_init(&messages_list);
	list_init(&cluster_members_list);
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
