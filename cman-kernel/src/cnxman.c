/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#define EXPORT_SYMTAB
#include <linux/init.h>
#include <linux/socket.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/utsname.h>
#include <net/sock.h>
#include <linux/proc_fs.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/uio.h>
#include <cluster/cnxman.h>
#include <cluster/service.h>

#include "cnxman-private.h"
#include "sm_control.h"
#include "sm_user.h"
#include "config.h"

#define CMAN_RELEASE_NAME "<CVS>"

static void process_incoming_packet(struct cl_comms_socket *csock,
				    struct msghdr *msg, struct kvec *vec, int veclen, int len);
static int cl_sendack(struct cl_comms_socket *sock, unsigned short seq,
		      int addr_len, char *addr, unsigned char remport,
		      unsigned char flag);
static void send_listen_request(int nodeid, unsigned char port);
static void send_listen_response(struct cl_comms_socket *csock, int nodeid,
				 unsigned char port, unsigned short tag);
static void resend_last_message(void);
static void start_ack_timer(void);
static int send_queued_message(struct queued_message *qmsg);
static void send_port_close_oob(unsigned char port);
static void post_close_oob(unsigned char port, int nodeid);
static void process_barrier_msg(struct cl_barriermsg *msg,
				struct cluster_node *node);
static struct cl_barrier *find_barrier(char *name);
static void node_shutdown(void);
static void node_cleanup(void);
static int send_or_queue_message(struct socket *sock, void *buf, int len, struct sockaddr_cl *caddr,
				 unsigned int flags);
static struct cl_comms_socket *get_next_interface(struct cl_comms_socket *cur);
static void check_for_unacked_nodes(void);
static void free_cluster_sockets(void);
static uint16_t generate_cluster_id(char *name);
static int is_valid_temp_nodeid(int nodeid);

extern int start_membership_services(pid_t);
extern int kcl_leave_cluster(int remove);
extern int send_kill(int nodeid);

static struct proto_ops cl_proto_ops;
static struct sock *master_sock;
static kmem_cache_t *cluster_sk_cachep;

/* Pointer to the pseudo node that maintains quorum in a 2node system */
struct cluster_node *quorum_device = NULL;

/* Array of "ports" allocated. This is just a list of pointers to the sock that
 * has this port bound. Speed is a major issue here so 1-2K of allocated
 * storage is worth sacrificing. Port 0 is reserved for protocol messages */
static struct sock *port_array[256];
static struct semaphore port_array_lock;

/* Our cluster name & number */
uint16_t cluster_id;
char cluster_name[MAX_CLUSTER_NAME_LEN+1];

/* Two-node mode: causes cluster to remain quorate if one of two nodes fails.
 * No more than two nodes are permitted to join the cluster. */
unsigned short two_node;

/* Cluster configuration version that must be the same among members. */
unsigned int config_version;

/* Reference counting for cluster applications */
atomic_t use_count;

/* Length of sockaddr address for our comms protocol */
unsigned int address_length;

/* Message sending */
static unsigned short cur_seq;	/* Last message sent */
static unsigned int ack_count;	/* Number of acks received for message
				 * 'cur_seq' */
static unsigned int acks_expected;	/* Number of acks we expect to receive */
static struct semaphore send_lock;
static struct timer_list ack_timer;

/* Saved packet information in case we need to resend it */
static char saved_msg_buffer[MAX_CLUSTER_MESSAGE];
static int saved_msg_len;
static int retry_count;

/* Task variables */
static pid_t kcluster_pid;
static pid_t membership_pid;
extern struct task_struct *membership_task;
extern int quit_threads;

wait_queue_head_t cnxman_waitq;

/* Variables owned by membership services */
extern int cluster_members;
extern struct list_head cluster_members_list;
extern struct semaphore cluster_members_lock;
extern int we_are_a_cluster_member;
extern int cluster_is_quorate;
extern struct cluster_node *us;
extern struct list_head new_dead_node_list;
extern struct semaphore new_dead_node_lock;
extern char nodename[];
extern int wanted_nodeid;

/* A list of processes listening for membership events */
static struct list_head event_listener_list;
static struct semaphore event_listener_lock;

/* A list of kernel callbacks listening for membership events */
static struct list_head kernel_listener_list;
static struct semaphore kernel_listener_lock;

/* A list of sockets we are listening on (and can transmit on...later) */
static struct list_head socket_list;

/* A list of all open cluster client sockets */
static struct list_head client_socket_list;
static struct semaphore client_socket_lock;

/* A list of all current barriers */
static struct list_head barrier_list;
static struct semaphore barrier_list_lock;

/* When a socket is read for reading it goes on this queue */
static spinlock_t active_socket_lock;
static struct list_head active_socket_list;

/* If the cnxman process is running and available for work */
atomic_t cnxman_running;

/* Fkags set by timers etc for the mainloop to detect and act upon */
static unsigned long mainloop_flags;

#define ACK_TIMEOUT   1
#define RESEND_NEEDED 2

/* A queue of messages waiting to be sent. If kcl_sendmsg is called outside of
 * process context then the messages get put in here */
static struct list_head messages_list;
static struct semaphore messages_list_lock;

static struct semaphore start_thread_sem;

/* List of outstanding ISLISTENING requests */
static struct list_head listenreq_list;
static struct semaphore listenreq_lock;

/* Any sending requests wait on this queue if necessary (eg inquorate, waiting
 * ACK) */
static DECLARE_WAIT_QUEUE_HEAD(socket_waitq);

/* Wait for thread to exit properly */
struct completion cluster_thread_comp;
struct completion member_thread_comp;

/* The resend delay to use, We increase this geometrically(word?) each time a
 * send is delayed. in deci-seconds */
static int resend_delay = 1;

/* Highest numbered interface and the current default */
static int num_interfaces;
static struct cl_comms_socket *current_interface = NULL;

struct temp_node
{
	int nodeid;
	char addr[sizeof(struct sockaddr_in6)];
	int addrlen;
	struct list_head list;
};
static struct list_head tempnode_list;
static struct semaphore tempnode_lock;


/* This is what's squirrelled away in skb->cb */
struct cb_info
{
	int  orig_nodeid;
	char orig_port;
	char oob;
};


/* Wake up any processes that are waiting to send. This is usually called when
 * all the ACKs have been gathered up or when a node has left the cluster
 * unexpectedly and we reckon there are no more acks to collect */
static void unjam(void)
{
	wake_up_interruptible(&socket_waitq);
	wake_up_interruptible(&cnxman_waitq);
}

/* Used by the data_ready routine to locate a connection given the socket */
static inline struct cl_comms_socket *find_comms_by_sock(struct sock *sk)
{
	struct list_head *conlist;

	list_for_each(conlist, &socket_list) {
		struct cl_comms_socket *clsock =
		    list_entry(conlist, struct cl_comms_socket, list);
		if (clsock->sock->sk == sk) {
			return clsock;
		}
	}
	return NULL;
}

/* Data available on socket */
static void cnxman_data_ready(struct sock *sk, int count_unused)
{
	struct cl_comms_socket *clsock = find_comms_by_sock(sk);

	if (clsock == NULL)	/* ASSERT ?? */
		return;

	/* If we're already on the list then don't do it again */
	if (test_and_set_bit(1, &clsock->active))
		return;

	spin_lock_irq(&active_socket_lock);
	list_add(&clsock->active_list, &active_socket_list);
	spin_unlock_irq(&active_socket_lock);

	wake_up_interruptible(&cnxman_waitq);
}

static int receive_message(struct cl_comms_socket *csock, char *iobuf)
{
	struct msghdr msg;
	struct kvec vec;
	struct sockaddr_in6 sin;
	int len;

	memset(&sin, 0, sizeof (sin));

	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_name = &sin;
	msg.msg_namelen = sizeof (sin);
	msg.msg_flags = 0;

	vec.iov_len = MAX_CLUSTER_MESSAGE;
	vec.iov_base = iobuf;

	len = kernel_recvmsg(csock->sock, &msg,
			     &vec, 1, MAX_CLUSTER_MESSAGE, MSG_DONTWAIT);

	vec.iov_base = iobuf;

	if (len > 0) {
		if (len > MAX_CLUSTER_MESSAGE) {
			printk(KERN_CRIT CMAN_NAME
			       ": %d byte message far too big\n", len);
			return 0;
		}
		process_incoming_packet(csock, &msg, &vec, 1, len);
	}
	else {
		if (len != -EAGAIN)
			printk(KERN_CRIT CMAN_NAME ": recvmsg failed: %d\n",
			       len);
	}
	return len;
}

static int cluster_kthread(void *unused)
{
	int len;
	char *iobuf;
	struct list_head *socklist;
	struct cl_comms_socket *csock;
	wait_queue_t cnxman_waitq_head;
	sigset_t tmpsig;

	daemonize("cman_comms");

	/* Block everything but SIGKILL/SIGSTOP/SIGTERM */
	siginitset(&tmpsig, SIGKILL | SIGSTOP | SIGTERM);
	sigprocmask(SIG_BLOCK, &tmpsig, NULL);

	/* This is the waitq we can wake the process up with */
	init_waitqueue_head(&cnxman_waitq);
	init_waitqueue_entry(&cnxman_waitq_head, current);
	add_wait_queue(&cnxman_waitq, &cnxman_waitq_head);

	set_user_nice(current, -6);

	/* Allow the sockets to start receiving */
	list_for_each(socklist, &socket_list) {
		csock = list_entry(socklist, struct cl_comms_socket, list);

		clear_bit(1, &csock->active);
	}

	iobuf = kmalloc(MAX_CLUSTER_MESSAGE, GFP_KERNEL);
	if (!iobuf) {
		printk(KERN_CRIT CMAN_NAME
		       ": Cannot allocate receive buffer for cluster comms\n");
		return -1;
	}

	complete(&cluster_thread_comp);

	for (;;) {
		struct list_head *temp;

		/* Wait for activity on any of the sockets */
		set_task_state(current, TASK_INTERRUPTIBLE);

		if (list_empty(&active_socket_list))
			schedule();
		set_task_state(current, TASK_RUNNING);

		if (quit_threads)
			break;

		if (test_and_clear_bit(ACK_TIMEOUT, &mainloop_flags)) {
			check_for_unacked_nodes();
		}

		/* Now receive any messages waiting for us */
		spin_lock_irq(&active_socket_lock);
		list_for_each_safe(socklist, temp, &active_socket_list) {
			csock =
			    list_entry(socklist, struct cl_comms_socket,
				       active_list);

			list_del(&csock->active_list);
			clear_bit(1, &csock->active);

			spin_unlock_irq(&active_socket_lock);

			do {
				len = receive_message(csock, iobuf);
			}
			while (len > 0);

			spin_lock_irq(&active_socket_lock);

			if (len == 0)
				break;	/* EOF on socket */
		}
		spin_unlock_irq(&active_socket_lock);

		/* Resend any unacked messages */
		if (test_and_clear_bit(RESEND_NEEDED, &mainloop_flags)
		    && acks_expected) {
			resend_last_message();
		}

		/* Send any queued messages */
		if (acks_expected == 0) {
			struct list_head *temp;
			struct list_head *msglist;

			down(&messages_list_lock);
			list_for_each_safe(msglist, temp, &messages_list) {
				struct queued_message *qmsg =
				    list_entry(msglist, struct queued_message,
					       list);
				int status = send_queued_message(qmsg);

				if (status >= 0) {
					/* Suceeded, remove it from the queue */
					list_del(&qmsg->list);
					kfree(qmsg);
				}
				/* Did it fail horribly ?? */
				if (status < 0 && status != -EAGAIN) {
					printk(KERN_INFO CMAN_NAME
					       ": send_queued_message failed, error %d\n",
					       status);
					list_del(&qmsg->list);
					kfree(qmsg);
				}
				break;	/* Only send one message at a time */
			}
			up(&messages_list_lock);
		}

		if (signal_pending(current))
			break;
	}
	P_COMMS("closing down\n");

	quit_threads = 1;	/* force other thread to die too */

	/* Wait for membership thread to finish, that way any
	   LEAVE message will get sent. */
	wake_up_process(membership_task);
	wait_for_completion(&member_thread_comp);

	node_shutdown();

	if (timer_pending(&ack_timer))
		del_timer(&ack_timer);

	node_cleanup();
	kfree(iobuf);

	complete(&cluster_thread_comp);
	return 0;
}

void notify_kernel_listeners(kcl_callback_reason reason, long arg)
{
	struct kernel_notify_struct *knotify;
	struct list_head *proclist;

	down(&kernel_listener_lock);
	list_for_each(proclist, &kernel_listener_list) {
		knotify =
		    list_entry(proclist, struct kernel_notify_struct, list);
		knotify->callback(reason, arg);
	}
	up(&kernel_listener_lock);
}

static void check_for_unacked_nodes()
{
	struct list_head *nodelist;
	struct list_head *temp;
	struct cluster_node *node;

	clear_bit(RESEND_NEEDED, &mainloop_flags);
	retry_count = 0;

	P_COMMS("Retry count exceeded -- looking for dead node\n");

	/* Node did not ACK a message after <n> tries, remove it from the
	 * cluster */
	down(&cluster_members_lock);
	list_for_each_safe(nodelist, temp, &cluster_members_list) {
		node = list_entry(nodelist, struct cluster_node, list);

		P_COMMS("checking node %s: last_acked = %d, last_seq_sent = %d\n",
			node->name, node->last_seq_acked, node->last_seq_sent);
		if (node->state != NODESTATE_DEAD &&
		    node->last_seq_acked != node->last_seq_sent && !node->us) {
			printk(KERN_WARNING CMAN_NAME
			       ": node %s is not responding - removing from the cluster\n",
			       node->name);

			/* Drop this lock or we can deadlock with membership */
			up(&cluster_members_lock);

			/* Start a state transition */
			a_node_just_died(node);
			down(&cluster_members_lock);
		}
	}
	up(&cluster_members_lock);
	acks_expected = ack_count = 0;
	unjam();
	return;
}

static void ack_timer_fn(unsigned long arg)
{
	P_COMMS("%ld: ack_timer fired, retries=%d\n", jiffies, retry_count);

	/* Too many retries ? */
	if (++retry_count > MAX_RETRIES) {
		set_bit(ACK_TIMEOUT, &mainloop_flags);
		wake_up_interruptible(&cnxman_waitq);
	}
	else {
		/* Resend last message */
		set_bit(RESEND_NEEDED, &mainloop_flags);
		wake_up_interruptible(&cnxman_waitq);
	}
}

/* Called to resend a packet if sock_sendmsg was busy */
static void short_timer_fn(unsigned long arg)
{
	P_COMMS("short_timer fired\n");

	/* Resend last message */
	resend_delay <<= 1;
	set_bit(RESEND_NEEDED, &mainloop_flags);
	wake_up_interruptible(&cnxman_waitq);
}

static void start_ack_timer()
{
	ack_timer.function = ack_timer_fn;
	ack_timer.data = 0L;
	mod_timer(&ack_timer, jiffies + HZ);
}

static void start_short_timer(void)
{
	ack_timer.function = short_timer_fn;
	ack_timer.data = 0L;
	mod_timer(&ack_timer, jiffies + (resend_delay * HZ));
}


static struct cl_waiting_listen_request *find_listen_request(unsigned short tag)
{
	struct list_head *llist;
	struct cl_waiting_listen_request *listener;

	list_for_each(llist, &listenreq_list) {
		listener = list_entry(llist, struct cl_waiting_listen_request,
				      list);
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
			if (net_ratelimit())
				printk(KERN_INFO CMAN_NAME
				       ": WARNING no listener for port %d on node %s\n",
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
		down(&listenreq_lock);
		listen_request = find_listen_request(listenmsg->tag);
		if (listen_request) {
			listen_request->result = listenmsg->listening;
			listen_request->waiting = 0;
			wake_up_interruptible(&listen_request->waitq);
		}
		up(&listenreq_lock);
		break;

	case CLUSTER_CMD_PORTCLOSED:
		closemsg =
		    (struct cl_closemsg *) (data +
					    sizeof (struct cl_protheader));
		cl_sendack(csock, header->seq, addrlen, addr, header->tgtport, 0);
		post_close_oob(closemsg->port, le32_to_cpu(header->srcid));
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
		printk(KERN_ERR CMAN_NAME
		       ": Unknown protocol message %d received\n", msg->cmd);
		break;

	}
	return;
}

static int valid_addr_for_node(struct cluster_node *node, char *addr)
{
	struct list_head *addrlist;
	struct cluster_node_addr *nodeaddr;

	/* We don't compare the first two bytes of the address because it's
	 * the Address Family and always in native byte order...so it will
	 * not match if we have mixed big & little-endian machines in the cluster
	 */

	list_for_each(addrlist, &node->addr_list) {
		nodeaddr = list_entry(addrlist, struct cluster_node_addr, list);

		if (memcmp(nodeaddr->addr+2, addr+2, address_length-2) == 0)
			return 1; /* TRUE */
	}
	return 0; /* FALSE */
}

static void memcpy_fromkvec(void *data, struct kvec *vec, int len)
{
        while (len > 0) {
                if (vec->iov_len) {
                        int copy = min_t(unsigned int, len, vec->iov_len);
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
			     struct kvec *iov, int veclen,
			     int len)
{
	struct sk_buff *skb;
	struct cb_info *cbinfo;
	int err;

        /* Get the port number and look for a listener */
	down(&port_array_lock);
	if (port_array[header->tgtport]) {
		struct cluster_sock *c = cluster_sk(port_array[header->tgtport]);

		/* ACK it */
		if (!(header->flags & MSG_NOACK) &&
		    !(header->flags & MSG_REPLYEXP)) {

			cl_sendack(csock, header->seq, msg->msg_namelen,
				   msg->msg_name, header->tgtport, 0);
		}

		/* Call a callback if there is one */
		if (c->kernel_callback) {
			up(&port_array_lock);
			if (veclen == 1) {
				c->kernel_callback(iov->iov_base,
						   iov->iov_len,
						   msg->msg_name, msg->msg_namelen,
						   le32_to_cpu(header->srcid));

			}
			else { /* Unroll iov, this Hardly ever Happens */
				char *data;
				data = kmalloc(len, GFP_KERNEL);
				if (!data)
					return -ENOMEM;

				memcpy_fromkvec(data, iov, len);
				c->kernel_callback(data, len,
						   msg->msg_name, msg->msg_namelen,
						   le32_to_cpu(header->srcid));
				kfree(data);
			}
			return len;
		}

		/* Otherwise put it into an SKB and pass it onto the recvmsg
		 * mechanism */
		skb = alloc_skb(len, GFP_KERNEL);
		if (!skb) {
			up(&port_array_lock);
			printk(KERN_INFO CMAN_NAME
			       ": Failed to allocate skb\n");
			return -ENOMEM;
		}

		skb_put(skb, len);
		memcpy_fromkvec(skb->data, iov, len);

		/* Put metadata into cb[] */
		cbinfo = (struct cb_info *)skb->cb;
		cbinfo->orig_nodeid = le32_to_cpu(header->srcid);
		cbinfo->orig_port = header->srcport;
		cbinfo->oob = 0;

		if ((err =
		     sock_queue_rcv_skb(port_array[header->tgtport], skb)) < 0) {

			printk(KERN_INFO CMAN_NAME
			       ": Error queueing request to port %d: %d\n",
			       header->tgtport, err);
			kfree_skb(skb);

			/* If the port was MEMBERSHIP then we have to die */
			if (header->tgtport == CLUSTER_PORT_MEMBERSHIP) {
				up(&port_array_lock);
				send_leave(CLUSTER_LEAVEFLAG_PANIC);
				panic("membership stopped responding");
			}
		}
		up(&port_array_lock);

	}
	else {
		/* ACK it, but set the flag bit so remote end knows no-one
		 * caught it */
		if (!(header->flags & MSG_NOACK))
			cl_sendack(csock, header->seq,
				   msg->msg_namelen, msg->msg_name,
				   header->tgtport, 1);

		/* Nobody listening, drop it */
		up(&port_array_lock);
	}
	return len;
}

/* NOTE: This routine knows (assumes!) that there is only one
   iov element passed into it. */
static void process_incoming_packet(struct cl_comms_socket *csock,
				    struct msghdr *msg,
				    struct kvec *vec, int veclen, int len)
{
	char *data = vec->iov_base;
	char *addr = msg->msg_name;
	int addrlen = msg->msg_namelen;
	struct cl_protheader *header = (struct cl_protheader *) data;
	struct cluster_node *rem_node =
		find_node_by_nodeid(le32_to_cpu(header->srcid));

	P_COMMS("seen message, from %d for %d, sequence num = %d, rem_node=%p, state=%d\n",
	     le32_to_cpu(header->srcid), le32_to_cpu(header->tgtid),
	     le16_to_cpu(header->seq), rem_node,
	     rem_node ? rem_node->state : -1);

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
		struct list_head *socklist;
		struct cl_comms_socket *clsock;

		list_for_each(socklist, &socket_list) {
			clsock =
			    list_entry(socklist, struct cl_comms_socket, list);

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
	if (!(header->flags & MSG_NOACK) &&
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
		header->flags, header->tgtport, we_are_a_cluster_member);


	/* If we are not part of the cluster then ignore multicast messages
	 * that need an ACK as we will confuse the sender who is only expecting
	 * ACKS from bona fide members */
	if ((header->flags & MSG_MULTICAST) &&
	    !(header->flags & MSG_NOACK) && !we_are_a_cluster_member) {
		P_COMMS
		    ("Discarding message - multicast and we are not a cluster member. port=%d flags=%x\n",
		     header->tgtport, header->flags);
		goto incoming_finish;
	}

	/* Save the sequence number of this message so we can ignore duplicates
	 * (above) */
	if (!(header->flags & MSG_NOACK) && rem_node) {
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
	vec[0].iov_base = data + sizeof (struct cl_protheader);
	vec[0].iov_len -= sizeof (struct cl_protheader);
	len -= sizeof (struct cl_protheader);

	send_to_user_port(csock, header, msg, vec, veclen, len);

      incoming_finish:
	return;
}

static struct sock *cl_alloc_sock(struct socket *sock, int gfp)
{
	struct sock *sk;
	struct cluster_sock *c;

	if ((sk =
	     sk_alloc(AF_CLUSTER, gfp, sizeof (struct cluster_sock),
		      cluster_sk_cachep)) == NULL)
		goto no_sock;

	if (sock) {
		sock->ops = &cl_proto_ops;
	}
	sock_init_data(sock, sk);

	sk->sk_destruct = NULL;
	sk->sk_no_check = 1;
	sk->sk_family = PF_CLUSTER;
	sk->sk_allocation = gfp;

	c = cluster_sk(sk);
	c->port = 0;
	c->service_data = NULL;

	return sk;
      no_sock:
	return NULL;
}

static int cl_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct cl_client_socket *csock;
	struct list_head *socklist;
	struct list_head *tmp;

	down(&client_socket_lock);
	if (sk) {
		/* Remove port allocations if it's a bound socket */
		struct cluster_sock *c = cluster_sk(sk);

		down(&port_array_lock);
		if (c->port) {
			port_array[c->port] = NULL;
		}
		up(&port_array_lock);

		/* Tell other nodes in the cluster that this listener is going
		 * away */
		if (atomic_read(&cnxman_running) && c->port)
			send_port_close_oob(c->port);

		if (c->service_data)
			sm_sock_release(sock);

		/* Master socket released ? */
		if (sk->sk_protocol == CLPROTO_MASTER) {
			master_sock = NULL;

			/* If this socket is being freed and cnxman is not
			 * started then free all the comms sockets as either
			 * the userland "join" process has crashed or the
			 * join failed.
			 */
			if (!atomic_read(&cnxman_running)) {
				quit_threads = 1;
				free_cluster_sockets();
			}
		}

		sock_orphan(sk);
		sock_hold(sk);
		lock_sock(sk);
		release_sock(sk);
		sock_put(sk);
		sock_put(sk);
		sock->sk = NULL;
	}

	/* Remove it from the list of clients */
	list_for_each_safe(socklist, tmp, &client_socket_list) {
		csock = list_entry(socklist, struct cl_client_socket, list);

		if (csock->sock == sock) {
			list_del(&csock->list);
			kfree(csock);
			break;
		}
	}
	up(&client_socket_lock);

	return 0;
}

static int cl_create(struct socket *sock, int protocol)
{
	struct sock *sk;

	/* All are datagrams */
	if (sock->type != SOCK_DGRAM)
		return -ESOCKTNOSUPPORT;

	if (protocol == CLPROTO_MASTER && !capable(CAP_CLUSTER))
		return -EPERM;

	/* Can only have one master socket */
	if (master_sock && protocol == CLPROTO_MASTER)
		return -EBUSY;

	/* cnxman not running and a client was requested */
	if (!atomic_read(&cnxman_running) && protocol != CLPROTO_MASTER)
		return -ENETDOWN;

	if ((sk = cl_alloc_sock(sock, GFP_KERNEL)) == NULL)
		return -ENOBUFS;

	sk->sk_protocol = protocol;

	if (protocol == CLPROTO_MASTER)
		master_sock = sk;

	/* Add client sockets to the list */
	if (protocol == CLPROTO_CLIENT) {
		struct cl_client_socket *clsock =
		    kmalloc(sizeof (struct cl_client_socket), GFP_KERNEL);
		if (!clsock) {
			cl_release(sock);
			return -ENOMEM;
		}
		clsock->sock = sock;
		down(&client_socket_lock);
		list_add(&clsock->list, &client_socket_list);
		up(&client_socket_lock);
	}

	return 0;
}

static int cl_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk = sock->sk;
	struct sockaddr_cl *saddr = (struct sockaddr_cl *) uaddr;
	struct cluster_sock *c = cluster_sk(sk);

	if (!capable(CAP_NET_BIND_SERVICE))
		return -EPERM;

	if (sk->sk_zapped == 0)
		return -EINVAL;

	if (addr_len != sizeof (struct sockaddr_cl))
		return -EINVAL;

	if (saddr->scl_family != AF_CLUSTER)
		return -EINVAL;

	if (saddr->scl_port == 0)
		return -EINVAL;	/* Port 0 is reserved for protocol messages */

	down(&port_array_lock);

	if (port_array[saddr->scl_port]) {
		up(&port_array_lock);
		return -EADDRINUSE;
	}

	port_array[saddr->scl_port] = sk;

	up(&port_array_lock);

	c->port = saddr->scl_port;
	sk->sk_zapped = 0;

	/* If we are not a cluster member yet then make the client wait until
	 * we are, this allows nodes to start cluster clients at the same time
	 * as cluster services but they will wait until membership is achieved.
	 * This looks odd in bind() (open would seem more obvious) but we need
	 * to know which port number is being used so that things like
	 * membership services don't get blocked
	 */

	if (saddr->scl_port > HIGH_PROTECTED_PORT)
		while (!we_are_a_cluster_member || !cluster_is_quorate
		       || in_transition()) {
			DECLARE_WAITQUEUE(wq, current);
			struct task_struct *tsk = current;

			set_task_state(tsk, TASK_INTERRUPTIBLE);
			add_wait_queue(&socket_waitq, &wq);

			if (!we_are_a_cluster_member || !cluster_is_quorate
			    || in_transition())
				schedule();

			set_task_state(tsk, TASK_RUNNING);
			remove_wait_queue(&socket_waitq, &wq);

			/* We were woken up because the cluster is going down,
			 * ...and we never got a chance to do any work! (sob) */
			if (atomic_read(&cnxman_running) == 0 || quit_threads) {
				return -ENOTCONN;
			}
		}

	return 0;
}

static int cl_getname(struct socket *sock, struct sockaddr *uaddr,
		      int *uaddr_len, int peer)
{
	struct sockaddr_cl *sa = (struct sockaddr_cl *) uaddr;
	struct sock *sk = sock->sk;
	struct cluster_sock *c = cluster_sk(sk);

	*uaddr_len = sizeof (struct sockaddr_cl);

	lock_sock(sk);

	sa->scl_port = c->port;
	sa->scl_flags = 0;
	sa->scl_family = AF_CLUSTER;

	release_sock(sk);

	return 0;
}

static unsigned int cl_poll(struct file *file, struct socket *sock,
			    poll_table * wait)
{
	return datagram_poll(file, sock, wait);
}

/* Copy internal node format to userland format */
void copy_to_usernode(struct cluster_node *node,
			     struct cl_cluster_node *unode)
{
	strcpy(unode->name, node->name);
	unode->size = sizeof (struct cl_cluster_node);
	unode->votes = node->votes;
	unode->state = node->state;
	unode->us = node->us;
	unode->node_id = node->node_id;
	unode->leave_reason = node->leave_reason;
	unode->incarnation = node->incarnation;
}

static int add_clsock(int broadcast, int number, struct socket *sock,
		      struct file *file)
{
	struct cl_comms_socket *newsock =
	    kmalloc(sizeof (struct cl_comms_socket), GFP_KERNEL);
	if (!newsock)
		return -ENOMEM;

	memset(newsock, 0, sizeof (*newsock));
	newsock->number = number;
	newsock->sock = sock;
	if (broadcast) {
		newsock->broadcast = 1;
		newsock->recv_only = 0;
	}
	else {
		newsock->broadcast = 0;
		newsock->recv_only = 1;
	}

	newsock->file = file;
	newsock->addr_len = sizeof(struct sockaddr_in6);

	/* Mark it active until cnxman thread is running and ready to process
	 * messages */
	set_bit(1, &newsock->active);

	/* Find out what it's bound to */
	newsock->sock->ops->getname(newsock->sock,
				    (struct sockaddr *)&newsock->saddr,
				    &newsock->addr_len, 0);

	num_interfaces = max(num_interfaces, newsock->number);
	if (!current_interface && newsock->broadcast)
		current_interface = newsock;

	/* Hook data_ready */
	newsock->sock->sk->sk_data_ready = cnxman_data_ready;

	/* Make an attempt to keep them in order */
	list_add_tail(&newsock->list, &socket_list);

	address_length = newsock->addr_len;
	return 0;
}

/* ioctl processing functions */

static int do_ioctl_set_version(unsigned long arg)
{
	struct cl_version version, *u_version;

	if (!capable(CAP_CLUSTER))
		return -EPERM;
	if (arg == 0)
		return -EINVAL;

	u_version = (struct cl_version *) arg;

	if (copy_from_user(&version, u_version, sizeof(struct cl_version)))
		return -EFAULT;

	if (version.major != CNXMAN_MAJOR_VERSION ||
	    version.minor != CNXMAN_MINOR_VERSION ||
	    version.patch != CNXMAN_PATCH_VERSION)
		return -EINVAL;

	if (config_version == version.config)
		return 0;

	config_version = version.config;
	send_reconfigure(RECONFIG_PARAM_CONFIG_VERSION, config_version);
	return 0;
}

static int do_ioctl_get_members(unsigned long arg)
{
	struct cluster_node *node;
	/* Kernel copies */
	struct cl_cluster_node user_format_node;
	struct cl_cluster_nodelist user_format_nodelist;
	/* User space array ptr */
	struct cl_cluster_node *user_node;
	struct list_head *nodelist;
	int num_nodes = 0;

	if (arg == 0)
		return cluster_members;

	if (copy_from_user(&user_format_nodelist, (void __user *)arg, sizeof(struct cl_cluster_nodelist)))
		return -EFAULT;

	down(&cluster_members_lock);

	if (user_format_nodelist.max_members < cluster_members) {
		up(&cluster_members_lock);
		return -E2BIG;
	}

	user_node = user_format_nodelist.nodes;

	list_for_each(nodelist, &cluster_members_list) {
		node = list_entry(nodelist, struct cluster_node, list);
		if (node->state == NODESTATE_MEMBER) {
			copy_to_usernode(node, &user_format_node);
			if (copy_to_user(user_node, &user_format_node,
					 sizeof (struct cl_cluster_node))) {
				up(&cluster_members_lock);
				return -EFAULT;
			}
			user_node++;
			num_nodes++;
		}
	}
	up(&cluster_members_lock);

	return num_nodes;
}

static int do_ioctl_get_all_members(unsigned long arg)
{
	struct cluster_node *node;
	/* Kernel copies */
	struct cl_cluster_node user_format_node;
	struct cl_cluster_nodelist user_format_nodelist;
	/* User space array ptr*/
	struct cl_cluster_node *user_node;
	struct list_head *nodelist;
	int num_nodes = 0;

	if (arg &&
	    copy_from_user(&user_format_nodelist,
			   (void __user *)arg, sizeof(struct cl_cluster_nodelist)))
		return -EFAULT;

	down(&cluster_members_lock);

	user_node = user_format_nodelist.nodes;

	list_for_each(nodelist, &cluster_members_list) {
		node = list_entry(nodelist, struct cluster_node, list);
		if (arg) {
			copy_to_usernode(node,
					 &user_format_node);

			if (copy_to_user(user_node, &user_format_node,
					 sizeof (struct cl_cluster_node))) {
				up(&cluster_members_lock);
				return -EFAULT;
			}
			user_node++;
			if (--user_format_nodelist.max_members < 0) {
				num_nodes = -EFAULT;
				goto err_exit;
			}

		}
		num_nodes++;
	}
	err_exit:
	up(&cluster_members_lock);

	return num_nodes;
}


static int do_ioctl_get_cluster(unsigned long arg)
{
	struct cl_cluster_info __user *info;

	info = (struct cl_cluster_info *)arg;

	if (copy_to_user(&info->number, &cluster_id, sizeof(cluster_id)))
	    return -EFAULT;

	if (copy_to_user(&info->name, cluster_name, strlen(cluster_name)+1))
		return -EFAULT;

	return 0;
}

static int do_ioctl_get_node(unsigned long arg)
{
	struct cluster_node *node;
	struct cl_cluster_node k_node, *u_node;

	u_node = (struct cl_cluster_node *) arg;

	if (copy_from_user(&k_node, u_node, sizeof(struct cl_cluster_node)))
		return -EFAULT;

	if (!k_node.name[0]) {
		if (k_node.node_id == 0)
			k_node.node_id = us->node_id;
		node = find_node_by_nodeid(k_node.node_id);
	}
	else
		node = find_node_by_name(k_node.name);

	if (!node)
		return -ENOENT;

	copy_to_usernode(node, &k_node);

	if (copy_to_user(u_node, &k_node, sizeof(struct cl_cluster_node)))
		return -EFAULT;

	return 0;
}

static int do_ioctl_set_expected(unsigned long arg)
{
	struct list_head *nodelist;
	struct cluster_node *node;
	unsigned int total_votes;
	unsigned int newquorum;

	if (!capable(CAP_CLUSTER))
		return -EPERM;
	if (arg == 0)
		return -EINVAL;

	newquorum = calculate_quorum(1, arg, &total_votes);

	if (newquorum < total_votes / 2
	    || newquorum > total_votes) {
		return -EINVAL;
	}

	/* Now do it */
	down(&cluster_members_lock);
	list_for_each(nodelist, &cluster_members_list) {
		node = list_entry(nodelist, struct cluster_node, list);
		if (node->state == NODESTATE_MEMBER
		    && node->expected_votes > arg) {
			node->expected_votes = arg;
		}
	}
	up(&cluster_members_lock);

	recalculate_quorum(1);

	send_reconfigure(RECONFIG_PARAM_EXPECTED_VOTES, arg);
	sm_member_update(cluster_is_quorate);

	return 0;
}

static int do_ioctl_kill_node(unsigned long arg)
{
	struct cluster_node *node;

	if (!capable(CAP_CLUSTER))
		return -EPERM;


	if ((node = find_node_by_nodeid(arg)) == NULL)
		return -EINVAL;

	/* Can't kill us */
	if (node->us)
		return -EINVAL;

	if (node->state != NODESTATE_MEMBER)
		return -EINVAL;

	/* Just in case it is alive, send a KILL message */
	send_kill(arg);

	node->leave_reason = CLUSTER_LEAVEFLAG_KILLED;
	a_node_just_died(node);

	return 0;
}

static int do_ioctl_barrier(unsigned long arg)
{
	struct cl_barrier_info info;

	if (!capable(CAP_CLUSTER))
			return -EPERM;

	if (copy_from_user(&info, (void *)arg, sizeof(info))  != 0)
		return -EFAULT;

	switch (info.cmd) {
	case BARRIER_IOCTL_REGISTER:
		return kcl_barrier_register(info.name,
					    info.flags,
					    info.arg);
	case BARRIER_IOCTL_CHANGE:
		return kcl_barrier_setattr(info.name,
					   info.flags,
					   info.arg);
	case BARRIER_IOCTL_WAIT:
		return kcl_barrier_wait(info.name);
	case BARRIER_IOCTL_DELETE:
		return kcl_barrier_delete(info.name);
	default:
		return -EINVAL;
	}
}

static int do_ioctl_islistening(unsigned long arg)
{
	DECLARE_WAITQUEUE(wq, current);
	struct cl_listen_request rq;
	struct cluster_node *rem_node;
	int nodeid;
	int result;
	struct cl_waiting_listen_request *listen_request;

	if (!arg)
		return -EINVAL;

	if (copy_from_user(&rq, (void *) arg, sizeof (rq)) != 0)
		return -EFAULT;

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

	/* If we are in transition then wait until we are not */
	while (in_transition()) {
		set_task_state(current, TASK_INTERRUPTIBLE);
		add_wait_queue(&socket_waitq, &wq);

		if (in_transition())
			schedule();

		set_task_state(current, TASK_RUNNING);
		remove_wait_queue(&socket_waitq, &wq);

		if (signal_pending(current))
			return -EINTR;
	}

	/* Were we shut down before it completed ? */
	if (!atomic_read(&cnxman_running))
		return -ENOTCONN;

	listen_request =
		kmalloc(sizeof (struct cl_waiting_listen_request),
			GFP_KERNEL);
	if (!listen_request)
		return -ENOMEM;

	/* Build the request */
	listen_request->waiting = 1;
	listen_request->result = 0;
	listen_request->tag = current->pid;
	listen_request->nodeid = nodeid;
	init_waitqueue_head(&listen_request->waitq);

	down(&listenreq_lock);
	list_add(&listen_request->list, &listenreq_list);
	up(&listenreq_lock);

	/* Now wait for the response to come back */
	send_listen_request(rq.nodeid, rq.port);

	while (listen_request->waiting) {
		set_task_state(current, TASK_INTERRUPTIBLE);
		add_wait_queue(&listen_request->waitq, &wq);

		if (listen_request->waiting)
			schedule();

		set_task_state(current, TASK_RUNNING);
		remove_wait_queue(&listen_request->waitq, &wq);

		if (signal_pending(current)) {
			result = -ERESTARTSYS;
			goto end_listen;
		}
	}
	result = listen_request->result;

 end_listen:
	down(&listenreq_lock);
	list_del(&listen_request->list);
	kfree(listen_request);
	up(&listenreq_lock);
	return result;
}

static int do_ioctl_set_votes(unsigned long arg)
{
	unsigned int total_votes;
	unsigned int newquorum;
	int saved_votes;

	if (!capable(CAP_CLUSTER))
		return -EPERM;

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

static int do_ioctl_pass_socket(unsigned long arg)
{
	struct cl_passed_sock sock_info;
	struct file *file;
	int error;

	if (!capable(CAP_CLUSTER))
		return -EPERM;

	if (atomic_read(&cnxman_running))
		return -EINVAL;

	error = -EBADF;

	if (copy_from_user(&sock_info, (void *)arg, sizeof(sock_info)))
		return -EFAULT;

	file = fget(sock_info.fd);
	if (file) {
		struct inode *inode = file->f_dentry->d_inode;

		error =	add_clsock(sock_info.multicast,
				   sock_info.number, SOCKET_I(inode),
				   file);
		if (error)
			fput(file);
	}
	return error;

}

static int do_ioctl_set_nodename(unsigned long arg)
{
	if (!capable(CAP_CLUSTER))
		return -EPERM;
	if (atomic_read(&cnxman_running))
		return -EINVAL;
	if (strncpy_from_user(nodename, (void *)arg, MAX_CLUSTER_MEMBER_NAME_LEN) < 0)
		return -EFAULT;
	return 0;
}

static int do_ioctl_set_nodeid(unsigned long arg)
{
	int nodeid = (int)arg;

	if (!capable(CAP_CLUSTER))
		return -EPERM;
	if (atomic_read(&cnxman_running))
		return -EINVAL;
	if (nodeid < 0 || nodeid > 4096)
		return -EINVAL;

	wanted_nodeid = (int)arg;
	return 0;
}

static int do_ioctl_join_cluster(unsigned long arg)
{
	struct cl_join_cluster_info join_info;

	if (!capable(CAP_CLUSTER))
		return -EPERM;

	if (atomic_read(&cnxman_running))
		return -EALREADY;

	if (copy_from_user(&join_info, (void *)arg, sizeof (struct cl_join_cluster_info) ))
		return -EFAULT;

	if (strlen(join_info.cluster_name) > MAX_CLUSTER_NAME_LEN)
		return -EINVAL;

	if (list_empty(&socket_list))
		return -ENOTCONN;

	set_votes(join_info.votes, join_info.expected_votes);
	cluster_id = generate_cluster_id(join_info.cluster_name);
	strncpy(cluster_name, join_info.cluster_name, MAX_CLUSTER_NAME_LEN);
	two_node = join_info.two_node;
	config_version = join_info.config_version;

	quit_threads = 0;
	acks_expected = 0;
	init_completion(&cluster_thread_comp);
	init_completion(&member_thread_comp);
	if (allocate_nodeid_array())
		return -ENOMEM;

	kcluster_pid = kernel_thread(cluster_kthread, NULL, 0);
	if (kcluster_pid < 0)
		return kcluster_pid;

	wait_for_completion(&cluster_thread_comp);
	init_completion(&cluster_thread_comp);

	atomic_set(&cnxman_running, 1);

	/* Make sure we have a node name */
	if (nodename[0] == '\0')
		strcpy(nodename, system_utsname.nodename);

	membership_pid = start_membership_services(kcluster_pid);
	if (membership_pid < 0) {
		quit_threads = 1;
		wait_for_completion(&cluster_thread_comp);
		init_completion(&member_thread_comp);
		return membership_pid;
	}

	sm_start();
	return 0;
}

static int do_ioctl_leave_cluster(unsigned long leave_flags)
{
	if (!capable(CAP_CLUSTER))
		return -EPERM;

	if (!atomic_read(&cnxman_running))
		return -ENOTCONN;

	if (in_transition())
		return -EBUSY;

	/* Ignore the use count if FORCE is set */
	if (!(leave_flags & CLUSTER_LEAVEFLAG_FORCE)) {
		if (atomic_read(&use_count))
			return -ENOTCONN;
	}

	us->leave_reason = leave_flags;
	quit_threads = 1;
	wake_up_interruptible(&cnxman_waitq);

	wait_for_completion(&cluster_thread_comp);
	atomic_set(&use_count, 0);
	return 0;
}

static int cl_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	int err = -EOPNOTSUPP;
	struct list_head *proclist;
	struct list_head *tmp;
	struct notify_struct *notify;
	struct cl_version cnxman_version;

	switch (cmd) {
		/* Process requests notification of cluster events */
	case SIOCCLUSTER_NOTIFY:
		notify = kmalloc(sizeof (struct notify_struct), GFP_KERNEL);
		if (!notify)
			return -ENOMEM;
		notify->pid = current->pid;
		notify->signal = arg;
		down(&event_listener_lock);
		list_add(&notify->list, &event_listener_list);
		up(&event_listener_lock);
		err = 0;
		break;

		/* Process is no longer interested cluster events */
	case SIOCCLUSTER_REMOVENOTIFY:
		err = EINVAL;

		down(&event_listener_lock);
		list_for_each_safe(proclist, tmp, &event_listener_list) {
			notify =
			    list_entry(proclist, struct notify_struct, list);
			if (notify->pid == current->pid) {
				list_del(&notify->list);
				kfree(notify);
				err = 0;
			}
		}
		up(&event_listener_lock);
		break;

		/* Return the cnxman version number */
	case SIOCCLUSTER_GET_VERSION:
		if (!arg)
			return -EINVAL;
		err = 0;
		cnxman_version.major = CNXMAN_MAJOR_VERSION;
		cnxman_version.minor = CNXMAN_MINOR_VERSION;
		cnxman_version.patch = CNXMAN_PATCH_VERSION;
		cnxman_version.config = config_version;
		if (copy_to_user((void *) arg, &cnxman_version,
				 sizeof (struct cl_version))) {
			return -EFAULT;
		}
		break;

		/* Set the cnxman config version number */
	case SIOCCLUSTER_SET_VERSION:
		err = do_ioctl_set_version(arg);
		break;

		/* Return the active membership list */
	case SIOCCLUSTER_GETMEMBERS:
		err = do_ioctl_get_members(arg);
		break;

		/* Return the full membership list include dead nodes */
	case SIOCCLUSTER_GETALLMEMBERS:
		err = do_ioctl_get_all_members(arg);
		break;

	case SIOCCLUSTER_GETNODE:
		err = do_ioctl_get_node(arg);
		break;

	case SIOCCLUSTER_GETCLUSTER:
		err = do_ioctl_get_cluster(arg);
		break;

	case SIOCCLUSTER_ISQUORATE:
		return cluster_is_quorate;

	case SIOCCLUSTER_ISACTIVE:
		return atomic_read(&cnxman_running);

	case SIOCCLUSTER_SETEXPECTED_VOTES:
		err = do_ioctl_set_expected(arg);
		break;

		/* Change the number of votes for this node */
	case SIOCCLUSTER_SET_VOTES:
		err = do_ioctl_set_votes(arg);
		break;

		/* Return 1 if the specified node is listening on a given port */
	case SIOCCLUSTER_ISLISTENING:
		err = do_ioctl_islistening(arg);
		break;

		/* Forcibly kill a node */
	case SIOCCLUSTER_KILLNODE:
		err = do_ioctl_kill_node(arg);
		break;

	case SIOCCLUSTER_GET_JOINCOUNT:
		if (!capable(CAP_CLUSTER))
			return -EPERM;
		else
			return atomic_read(&use_count);

		/* ioctl interface to the barrier system */
	case SIOCCLUSTER_BARRIER:
		err = do_ioctl_barrier(arg);
		break;

	case SIOCCLUSTER_PASS_SOCKET:
		if (sock->sk->sk_protocol != CLPROTO_MASTER)
			err = -EOPNOTSUPP;
		else
			err = do_ioctl_pass_socket(arg);
		break;

	case SIOCCLUSTER_SET_NODENAME:
		if (sock->sk->sk_protocol != CLPROTO_MASTER)
			err = -EOPNOTSUPP;
		else
			err = do_ioctl_set_nodename(arg);
		break;

	case SIOCCLUSTER_SET_NODEID:
		if (sock->sk->sk_protocol != CLPROTO_MASTER)
			err = -EOPNOTSUPP;
		else
			err = do_ioctl_set_nodeid(arg);
		break;

	case SIOCCLUSTER_JOIN_CLUSTER:
		if (sock->sk->sk_protocol != CLPROTO_MASTER)
			err = -EOPNOTSUPP;
		else
			err = do_ioctl_join_cluster(arg);
		break;

	case SIOCCLUSTER_LEAVE_CLUSTER:
		err = do_ioctl_leave_cluster(arg);
		break;

	default:
		err = sm_ioctl(sock, cmd, arg);
	}
	return err;
}

static int cl_shutdown(struct socket *sock, int how)
{
	struct sock *sk = sock->sk;
	int err = -ENOTCONN;

	lock_sock(sk);

	if (sock->state == SS_UNCONNECTED)
		goto out;

	err = 0;
	if (sock->state == SS_DISCONNECTING)
		goto out;

	err = -EINVAL;

	if (how != SHUTDOWN_MASK)
		goto out;

	sk->sk_shutdown = how;
	err = 0;

      out:
	release_sock(sk);

	return err;
}


/* We'll be giving out reward points next... */
/* Send the packet and save a copy in case someone loses theirs. Should be
 * protected by the send mutexphore */
static int __send_and_save(struct cl_comms_socket *csock, struct msghdr *msg,
			   struct kvec *vec, int veclen,
			   int size, int needack)
{
	int result;
	struct kvec save_vectors[veclen];

	/* Save a copy of the IO vectors as sendmsg mucks around with them and
	 * we might want to send the same stuff out more than once (for different
	 * interfaces)
	 */
	memcpy(save_vectors, vec,
	       sizeof (struct kvec) * veclen);

	result = kernel_sendmsg(csock->sock, msg, vec, veclen, size);

	if (result >= 0 && acks_expected && needack) {

		/* Start retransmit timer if it didn't go */
		if (result == 0) {
			start_short_timer();
		}
		else {
			resend_delay = 1;
		}
	}

	/* Restore IOVs */
	memcpy(vec, save_vectors,
	       sizeof (struct kvec) * veclen);

	return result;
}

static void resend_last_message()
{
	struct msghdr msg;
	struct kvec vec[1];
	int result;

	P_COMMS("%ld resending last message: %d bytes: port=%d, cmd=%d\n",
		jiffies, saved_msg_len, saved_msg_buffer[0],
		saved_msg_buffer[6]);

	/* Assume there is something wrong with the last interface */
	current_interface = get_next_interface(current_interface);
	if (num_interfaces > 1)
		printk(KERN_WARNING CMAN_NAME ": Now using interface %d\n",
		       current_interface->number);

	vec[0].iov_base = saved_msg_buffer;
	vec[0].iov_len = saved_msg_len;

	memset(&msg, 0, sizeof (msg));
	msg.msg_name = &current_interface->saddr;
	msg.msg_namelen = current_interface->addr_len;

	result = kernel_sendmsg(current_interface->sock, &msg, vec, 1, saved_msg_len);

	if (result < 0)
		printk(KERN_ERR CMAN_NAME ": resend failed: %d\n", result);

	/* Try indefinitely to send this, the backlog must die down eventually
	 * !? */
	if (result == 0)
		start_short_timer();

	/* Send succeeded, continue waiting for ACKS */
	if (result > 0)
		start_ack_timer();

}

static int cl_recvmsg(struct kiocb *iocb, struct socket *sock,
		      struct msghdr *msg, size_t size, int flags)
{
	struct sock *sk = sock->sk;
	struct sockaddr_cl *sin = (struct sockaddr_cl *) msg->msg_name;
	struct sk_buff *skb;
	struct cb_info *cbinfo;
	int copied, err = 0;
	int isoob = 0;

	/* Socket was notified of shutdown, remove any pending skbs and return
	 * EOF */
	if (!atomic_read(&cnxman_running)) {
		while ((skb = skb_recv_datagram(sk, flags, MSG_DONTWAIT, &err)))
			skb_free_datagram(sk, skb);
		return 0;	/* cnxman has left the building */
	}

	/* Generic datagram code does most of the work. If the user is not
	 * interested in OOB messages then ignore them */
	do {
		skb = skb_recv_datagram(sk, flags, flags & MSG_DONTWAIT, &err);
		if (!skb)
			goto out;

		cbinfo = (struct cb_info *)skb->cb;
		isoob = cbinfo->oob;

		/* If it is OOB and the user doesn't want it, then throw it away. */
		if (isoob && !(flags & MSG_OOB)) {
			skb_free_datagram(sk, skb);

			/* If we peeked (?) an OOB but the user doesn't want it
			   then we need to discard it or we'll loop forever */
			if (flags & MSG_PEEK) {
				skb = skb_recv_datagram(sk, flags & ~MSG_PEEK,
							MSG_DONTWAIT, &err);
				if (skb)
					skb_free_datagram(sk, skb);
			}
		}
	}
	while (isoob && !(flags & MSG_OOB));

	copied = skb->len;
	if (copied > size) {
		copied = size;
		msg->msg_flags |= MSG_TRUNC;
	}
	err = memcpy_toiovec(msg->msg_iov, skb->data, copied);

	if (err)
		goto out_free;

	if (msg->msg_name && msg->msg_namelen) {
		memset(msg->msg_name, 0, msg->msg_namelen);

		if (msg->msg_namelen >= sizeof (struct sockaddr_cl)) {

			/* Nodeid is in native byte order - anything else is just
			 * perverse */
			sin->scl_nodeid = cbinfo->orig_nodeid;
		}
		msg->msg_namelen = sizeof (struct sockaddr_cl);
		sin->scl_port = cbinfo->orig_port;
	}

	if (isoob) {
		msg->msg_flags |= MSG_OOB;
	}

	sock_recv_timestamp(msg, sk, skb);

	err = copied;

      out_free:
	skb_free_datagram(sk, skb);

      out:
	return err;
}

/* Send a message out on all interfaces */
static int send_to_all_ints(int nodeid, struct msghdr *our_msg,
			    struct kvec *vec, int veclen, int size, int flags)
{
	struct sockaddr_in6 daddr;
	struct cl_comms_socket *clsock;
	int result = 0;

	our_msg->msg_name = &daddr;

	list_for_each_entry(clsock, &socket_list, list) {

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
static int __sendmsg(struct socket *sock, struct msghdr *msg,
		     struct kvec *vec, int veclen, int size,
		     unsigned char port)
{
	int result = 0, i;
	int flags = msg->msg_flags;
	struct msghdr our_msg;
	struct sockaddr_cl *caddr = msg->msg_name;
	struct cl_protheader header;
	struct kvec vectors[veclen + 1];
	unsigned char srcport;
	int nodeid = 0;

	if (size > MAX_CLUSTER_MESSAGE)
		return -EINVAL;
	if (!atomic_read(&cnxman_running))
		return -ENOTCONN;

	if (caddr)
		nodeid = caddr->scl_nodeid;

	/* Check that the node id (if present) is valid */
	if (msg->msg_namelen && (!find_node_by_nodeid(nodeid) &&
				 !is_valid_temp_nodeid(nodeid))) {
		return -ENOTCONN;
	}

	/* If there's no sending client socket then the source
	   port is 0: "us" */
	if (sock) {
		struct cluster_sock *csock = cluster_sk(sock->sk);
		srcport = csock->port;
	}
	else {
		srcport = 0;
	}

	/* We can only have one send outstanding at a time so we might as well
	 * lock the whole send mechanism */
	down(&send_lock);

	while ((port > HIGH_PROTECTED_PORT
		&& (!cluster_is_quorate || in_transition()))
	       || (acks_expected > 0 && !(msg->msg_flags & MSG_NOACK))) {

		DECLARE_WAITQUEUE(wq, current);
		struct task_struct *tsk = current;

		if (flags & MSG_DONTWAIT) {
			up(&send_lock);
			return -EAGAIN;
		}

		if (current->pid == kcluster_pid) {
			P_COMMS
			    ("Tried to make kclusterd wait, port=%d, acks_count=%d, expected=%d\n",
			     port, ack_count, acks_expected);
			up(&send_lock);
			return -EAGAIN;
		}

		P_COMMS("%s process waiting. acks=%d, expected=%d\n", tsk->comm,
			ack_count, acks_expected);

		set_task_state(tsk, TASK_INTERRUPTIBLE);
		add_wait_queue(&socket_waitq, &wq);

		if ((port > HIGH_PROTECTED_PORT
		     && (!cluster_is_quorate || in_transition()))
		    || (acks_expected > 0)) {

			up(&send_lock);
			schedule();
			down(&send_lock);
		}

		set_task_state(tsk, TASK_RUNNING);
		remove_wait_queue(&socket_waitq, &wq);

		/* Going down */
		if (quit_threads) {
			up(&send_lock);
			return -ENOTCONN;
		}

		if (signal_pending(current)) {
			up(&send_lock);
			return -ERESTARTSYS;
		}

		/* Were we shut down in the meantime ? */
		if (!atomic_read(&cnxman_running)) {
			up(&send_lock);
			return -ENOTCONN;
		}

	}

	memset(&our_msg, 0, sizeof (our_msg));

	/* Build the header */
	header.tgtport = port;
	header.srcport = srcport;
	header.flags = msg->msg_flags;
	header.cluster = cpu_to_le16(cluster_id);
	header.srcid = us ? cpu_to_le32(us->node_id) : 0;
	header.tgtid = caddr ? cpu_to_le32(nodeid) : 0;

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
	if (!msg->msg_namelen) {
		header.flags |= MSG_MULTICAST;
		header.tgtid = 0;
	}

	/* Loopback shortcut */
	if (nodeid == us->node_id && nodeid != 0) {

		up(&send_lock);
		header.flags |= MSG_NOACK; /* Don't ack it! */

		return send_to_user_port(NULL, &header, msg, vec, veclen, size);
	}

	/* Copy the existing kvecs into our array and add the header on at the
	 * beginning */
	vectors[0].iov_base = &header;
	vectors[0].iov_len = sizeof (header);
	for (i = 0; i < veclen; i++) {
		vectors[i + 1] = vec[i];
	}


        /* Work out how many ACKS are wanted - *don't* reset acks_expected to
	 * zero if no acks are required as an ACK-needed message may still be
	 * outstanding */
	if (!(msg->msg_flags & MSG_NOACK)) {
		if (msg->msg_namelen)
			acks_expected = 1;	/* Unicast */
		else
			acks_expected = max(cluster_members - 1, 0);

	}

	P_COMMS
	    ("Sending message - tgt=%d port %d required %d acks, seq=%d, flags=%x\n",
	     nodeid, header.port,
	     (msg->msg_flags & MSG_NOACK) ? 0 : acks_expected,
	     le16_to_cpu(header.seq), header.flags);

	/* Don't include temp nodeids in the message itself */
	if (header.tgtid < 0)
		header.tgtid = 0;

	/* For non-member sends we use all the interfaces */
	if ((nodeid < 0) || (flags & MSG_ALLINT)) {

		result = send_to_all_ints(nodeid, &our_msg, vectors, veclen+1,
					  size, msg->msg_flags);
	}
	else {
		/* Send to only the current socket - resends will use the
		 * others if necessary */
		our_msg.msg_name = &current_interface->saddr;
		our_msg.msg_namelen = current_interface->addr_len;

		result =
		    __send_and_save(current_interface, &our_msg,
				    vectors, veclen+1,
				    size + sizeof (header),
				    !(msg->msg_flags & MSG_NOACK));
	}

	/* Make a note in each nodes' structure that it has been sent a message
	 * so we can see which ones went astray */
	if (!(flags & MSG_NOACK) && nodeid >= 0) {
		if (msg->msg_namelen) {
			struct cluster_node *node;

			node = find_node_by_nodeid(le32_to_cpu(header.tgtid));
			if (node)
				node->last_seq_sent = cur_seq;
		}
		else {
			struct cluster_node *node;
			struct list_head *nodelist;

			list_for_each(nodelist, &cluster_members_list) {
				node =
				    list_entry(nodelist, struct cluster_node,
					       list);
				if (node->state == NODESTATE_MEMBER) {
					node->last_seq_sent = cur_seq;
				}
			}
		}
	}

	/* if the client wants a broadcast message sending back to itself
	   then loop it back */
	if (nodeid == 0 && (flags & MSG_BCASTSELF)) {
		header.flags |= MSG_NOACK; /* Don't ack it! */

		result = send_to_user_port(NULL, &header, msg, vec, veclen, size);
	}

	/* Save a copy of the message if we're expecting an ACK */
	if (!(flags & MSG_NOACK) && acks_expected) {
		struct cl_protheader *savhdr = (struct cl_protheader *) saved_msg_buffer;

		memcpy_fromkvec(saved_msg_buffer, vectors,
				size + sizeof (header));

		saved_msg_len = size + sizeof (header);
		retry_count = ack_count = 0;
		clear_bit(RESEND_NEEDED, &mainloop_flags);

		/* Clear the REPLYEXPected flag so we force a real ACK
		   if it's necessary to resend this packet */
		savhdr->flags &= ~MSG_REPLYEXP;
		start_ack_timer();
	}

	up(&send_lock);
	return result;
}

static int queue_message(struct socket *sock, void *buf, int len,
			 struct sockaddr_cl *caddr,
			 unsigned char port, int flags)
{
	struct queued_message *qmsg;

	qmsg = kmalloc(sizeof (struct queued_message),
		       (in_atomic()
			|| irqs_disabled())? GFP_ATOMIC : GFP_KERNEL);
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
	}
	qmsg->flags = flags;
	qmsg->port = port;
	qmsg->socket = sock;

	down(&messages_list_lock);
	list_add_tail(&qmsg->list, &messages_list);
	up(&messages_list_lock);

	wake_up_interruptible(&cnxman_waitq);

	return 0;
}

static int cl_sendmsg(struct kiocb *iocb, struct socket *sock,
		      struct msghdr *msg, size_t size)
{
	struct cluster_sock *c = cluster_sk(sock->sk);
	char *buffer;
	int status;
	uint8_t port;
	struct kvec vec;
	struct sockaddr_cl *caddr = msg->msg_name;

	if (sock->sk->sk_protocol == CLPROTO_MASTER)
		return -EOPNOTSUPP;

	port = c->port;

	/* Only capable users can override the port number */
	if (caddr && capable(CAP_CLUSTER) && caddr->scl_port)
		port = caddr->scl_port;

	if (port == 0)
		return -EDESTADDRREQ;

	/* Allocate a kernel buffer for the data so we can put it into a kvec */
	buffer = kmalloc(size, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	if (memcpy_fromiovec(buffer, msg->msg_iov, size)) {
		status = -EFAULT;
		goto end_send;
	}

	vec.iov_len = size;
	vec.iov_base = buffer;

	status = __sendmsg(sock, msg, &vec, 1, size, port);

 end_send:
	kfree(buffer);

	return status;
}

/* Kernel call to sendmsg */
int kcl_sendmsg(struct socket *sock, void *buf, int size,
		struct sockaddr_cl *caddr, int addr_len, unsigned int flags)
{
	struct kvec vecs[1];
	struct msghdr msg;
	struct cluster_sock *c = cluster_sk(sock->sk);
	unsigned char port;

	if (size > MAX_CLUSTER_MESSAGE)
		return -EINVAL;
	if (!atomic_read(&cnxman_running))
		return -ENOTCONN;

	port = c->port;
	if (caddr && caddr->scl_port)
		port = caddr->scl_port;

	if (port == 0)
		return -EDESTADDRREQ;

	/* If we have no process context then queue it up for kclusterd to
	 * send. */
	if (in_interrupt() || flags & MSG_QUEUE) {
		return queue_message(sock, buf, size, caddr, port,
				     flags & ~MSG_QUEUE);
	}

	vecs[0].iov_base = buf;
	vecs[0].iov_len = size;

	memset(&msg, 0, sizeof (msg));
	msg.msg_name = caddr;
	msg.msg_namelen = addr_len;
	msg.msg_flags = flags;

	return __sendmsg(sock, &msg, vecs, 1, size, port);
}

static int send_queued_message(struct queued_message *qmsg)
{
	struct kvec vecs[1];
	struct msghdr msg;

	/* Don't send blocked messages */
	if (qmsg->port > HIGH_PROTECTED_PORT
	    && (!cluster_is_quorate || in_transition()))
		return -EAGAIN;

	vecs[0].iov_base = qmsg->msg_buffer;
	vecs[0].iov_len = qmsg->msg_len;

	memset(&msg, 0, sizeof (msg));
	msg.msg_name = qmsg->addr_len ? &qmsg->addr : NULL;
	msg.msg_namelen = qmsg->addr_len;
	msg.msg_flags = qmsg->flags;

	return __sendmsg(qmsg->socket, &msg, vecs, 1,
			 qmsg->msg_len, qmsg->port);
}

int kcl_register_read_callback(struct socket *sock,
			       int (*routine) (char *, int, char *, int,
					       unsigned int))
{
	struct cluster_sock *c = cluster_sk(sock->sk);

	c->kernel_callback = routine;

	return 0;
}

/* Used where we are in kclusterd context and we can't allow the task to wait
 * as we are also responsible to processing the ACKs that do the wake up. Try
 * to send the message immediately and queue it if that's not possible */
static int send_or_queue_message(struct socket *sock, void *buf, int len,
				 struct sockaddr_cl *caddr,
				 unsigned int flags)
{
	struct kvec vecs[1];
	struct msghdr msg;
	int status;

	vecs[0].iov_base = buf;
	vecs[0].iov_len = len;

	memset(&msg, 0, sizeof (msg));
	msg.msg_name = caddr;
	msg.msg_namelen = caddr ? sizeof (struct sockaddr_cl) : 0;
	msg.msg_flags = MSG_DONTWAIT | flags;

	status = __sendmsg(NULL, &msg, vecs, 1, len, 0);

	/* Did it work ? */
	if (status > 0) {
		return 0;
	}

	/* Failure other than EAGAIN is fatal */
	if (status != -EAGAIN) {
		return status;
	}

	return queue_message(sock, buf, len, caddr, 0, flags);
}

/* Send a listen request to a node */
static void send_listen_request(int nodeid, unsigned char port)
{
	struct cl_listenmsg listenmsg;
	struct sockaddr_cl caddr;

	memset(&caddr, 0, sizeof (caddr));

	/* Build the header */
	listenmsg.cmd = CLUSTER_CMD_LISTENREQ;
	listenmsg.target_port = port;
	listenmsg.listening = 0;
	listenmsg.tag = current->pid;

	caddr.scl_family = AF_CLUSTER;
	caddr.scl_port = 0;
	caddr.scl_nodeid = nodeid;

	send_or_queue_message(NULL, &listenmsg, sizeof(listenmsg), &caddr, MSG_REPLYEXP);
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

	caddr.scl_family = AF_CLUSTER;
	caddr.scl_port = 0;
	caddr.scl_nodeid = nodeid;

	status = send_or_queue_message(NULL, &listenmsg,
				       sizeof (listenmsg),
				       &caddr, 0);

	return;
}

/* Send an ACK */
static int cl_sendack(struct cl_comms_socket *csock, unsigned short seq,
		      int addr_len, char *addr, unsigned char remport,
		      unsigned char flag)
{
	struct kvec vec;
	struct cl_ackmsg ackmsg;
	struct msghdr msg;
	struct sockaddr_in6 daddr;
	int result;

#ifdef DEBUG_COMMS
	char buf[MAX_ADDR_PRINTED_LEN];

	P_COMMS("Sending ACK to %s, seq=%d\n",
		print_addr(addr, address_length, buf), le16_to_cpu(seq));
#endif

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

	result = kernel_sendmsg(csock->sock, &msg, &vec, 1, sizeof (ackmsg));

	if (result < 0)
		printk(KERN_CRIT CMAN_NAME ": error sending ACK: %d\n", result);

	return result;

}

/* Wait for all ACKS to be gathered */
void kcl_wait_for_all_acks()
{
	while (ack_count < acks_expected) {

		DECLARE_WAITQUEUE(wq, current);
		struct task_struct *tsk = current;

		set_task_state(tsk, TASK_INTERRUPTIBLE);
		add_wait_queue(&socket_waitq, &wq);

		if (ack_count < acks_expected) {
			schedule();
		}

		set_task_state(tsk, TASK_RUNNING);
		remove_wait_queue(&socket_waitq, &wq);
	}
}

/* Send a closedown OOB message to all cluster nodes - this tells them that a
 * port listener has gone away */
static void send_port_close_oob(unsigned char port)
{
	struct cl_closemsg closemsg;

	/* Build the header */
	closemsg.cmd = CLUSTER_CMD_PORTCLOSED;
	closemsg.port = port;

	send_or_queue_message(NULL, &closemsg, sizeof (closemsg), NULL, 0);
	return;
}

/* A remote port has been closed - post an OOB message to the local listen on
 * that port (if there is one) */
static void post_close_oob(unsigned char port, int nodeid)
{
	struct cl_portclosed_oob *oobmsg;
	struct sk_buff *skb;
	struct sock *sock = port_array[port];
	struct cb_info *cbinfo;

	if (!sock) {
		return;		/* No-one listening */
	}

	skb = alloc_skb(sizeof (*oobmsg), GFP_KERNEL);
	if (!skb)
		return;

	skb_put(skb, sizeof (*oobmsg));
	oobmsg = (struct cl_portclosed_oob *) skb->data;
	oobmsg->port = port;
	oobmsg->cmd = CLUSTER_OOB_MSG_PORTCLOSED;

	cbinfo = (struct cb_info *)skb->cb;
	cbinfo->oob = 1;
	cbinfo->orig_nodeid = nodeid;
	cbinfo->orig_port = port;

	sock_queue_rcv_skb(sock, skb);

}

/* Leave the cluster */
static void node_shutdown()
{
	struct cl_barrier *barrier;
	struct list_head *blist;
	struct list_head *temp;
	struct list_head *socklist;
	struct cl_client_socket *csock;
	struct sk_buff *null_skb;

	if (we_are_a_cluster_member)
		printk(KERN_INFO CMAN_NAME ": we are leaving the cluster. %s\n",
		       us->leave_reason?leave_string(us->leave_reason):"");

	atomic_set(&cnxman_running, 0);
	unjam();

	/* Notify kernel listeners first */
	notify_kernel_listeners(LEAVING, 0);

	/* Notify client sockets */
	down(&client_socket_lock);
	list_for_each_safe(socklist, temp, &client_socket_list) {
		csock = list_entry(socklist, struct cl_client_socket, list);

		null_skb = alloc_skb(0, GFP_KERNEL);
		if (null_skb)
			sock_queue_rcv_skb(csock->sock->sk, null_skb);
		list_del(&csock->list);
		kfree(csock);
	}
	up(&client_socket_lock);
	we_are_a_cluster_member = 0;
	cluster_is_quorate = 0;

	sm_stop(1);

	/* Wake up any processes waiting for barriers */
	down(&barrier_list_lock);
	list_for_each(blist, &barrier_list) {
		barrier = list_entry(blist, struct cl_barrier, list);

		/* Cancel any timers */
		if (timer_pending(&barrier->timer))
			del_timer(&barrier->timer);

		/* Force it to be auto-delete so it discards itself */
		if (barrier->state == BARRIER_STATE_WAITING) {
			barrier->flags |= BARRIER_ATTR_AUTODELETE;
			wake_up_interruptible(&barrier->waitq);
		}
		else {
			if (barrier->callback) {
				barrier->callback(barrier->name, -ENOTCONN);
				barrier->callback = NULL;
			}
		}
	}
	up(&barrier_list_lock);

	/* Wake up any processes waiting for ISLISTENING requests */
	down(&listenreq_lock);
	list_for_each(blist, &listenreq_list) {
		struct cl_waiting_listen_request *lrequest =
		    list_entry(blist, struct cl_waiting_listen_request, list);

		if (lrequest->waiting)
			wake_up_interruptible(&lrequest->waitq);
	}
	up(&listenreq_lock);
}

static void free_cluster_sockets()
{
	struct list_head *socklist;
	struct cl_comms_socket *sock;
	struct list_head *temp;

	list_for_each_safe(socklist, temp, &socket_list) {
		sock = list_entry(socklist, struct cl_comms_socket, list);

		list_del(&sock->list);
		fput(sock->file);
		kfree(sock);
	}
	num_interfaces = 0;
	current_interface = NULL;
}

/* Tidy up after all the rest of the cluster bits have shut down */
static void node_cleanup()
{
	struct list_head *nodelist;
	struct list_head *proclist;
	struct list_head *temp;
	struct list_head *socklist;
	struct list_head *blist;
	struct temp_node *tn;
	struct temp_node *tmp;
	struct cl_comms_socket *sock;
	struct kernel_notify_struct *knotify;

	/* Free list of kernel listeners */
	list_for_each_safe(proclist, temp, &kernel_listener_list) {
		knotify =
		    list_entry(proclist, struct kernel_notify_struct, list);
		list_del(&knotify->list);
		kfree(knotify);
	}

	/* Mark the sockets as busy so they don't get added to the active
	 * sockets list in the next few lines of code before we free them */
	list_for_each_safe(socklist, temp, &socket_list) {
		sock = list_entry(socklist, struct cl_comms_socket, list);

		set_bit(1, &sock->active);
	}

	/* Tidy the active sockets list */
	list_for_each_safe(socklist, temp, &active_socket_list) {
		sock =
		    list_entry(socklist, struct cl_comms_socket, active_list);
		list_del(&sock->active_list);
	}

	/* Free the memory allocated to cluster nodes */
	free_nodeid_array();
	down(&cluster_members_lock);
	us = NULL;
	list_for_each_safe(nodelist, temp, &cluster_members_list) {

		struct list_head *addrlist;
		struct list_head *addrtemp;
		struct cluster_node *node;
		struct cluster_node_addr *nodeaddr;

		node = list_entry(nodelist, struct cluster_node, list);

		list_for_each_safe(addrlist, addrtemp, &node->addr_list) {
			nodeaddr =
			    list_entry(addrlist, struct cluster_node_addr,
				       list);

			list_del(&nodeaddr->list);
			kfree(nodeaddr);
		}
		list_del(&node->list);
		kfree(node->name);
		kfree(node);
	}
	cluster_members = 0;
	up(&cluster_members_lock);

	/* Clean the temp node IDs list. */
	down(&tempnode_lock);
	list_for_each_entry_safe(tn, tmp, &tempnode_list, list) {
		list_del(&tn->list);
		kfree(tn);
	}
	up(&tempnode_lock);

	/* Free the memory allocated to the outgoing sockets */
	free_cluster_sockets();

	/* Make sure that all the barriers are deleted */
	down(&barrier_list_lock);
	list_for_each_safe(blist, temp, &barrier_list) {
		struct cl_barrier *barrier =
		    list_entry(blist, struct cl_barrier, list);

		list_del(&barrier->list);
		kfree(barrier);
	}
	up(&barrier_list_lock);

	kcluster_pid = 0;
	clear_bit(RESEND_NEEDED, &mainloop_flags);
	acks_expected = 0;
	wanted_nodeid = 0;
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

	/* Hide messages during startup state transition */
	if (we_are_a_cluster_member) {
		if (cluster_is_quorate && !quorate)
			printk(KERN_CRIT CMAN_NAME
			       ": quorum lost, blocking activity\n");
		if (!cluster_is_quorate && quorate)
			printk(KERN_CRIT CMAN_NAME
			       ": quorum regained, resuming activity\n");
	}
	cluster_is_quorate = quorate;

	/* Wake up any sleeping processes */
	if (cluster_is_quorate) {
		unjam();
	}

}

void queue_oob_skb(struct socket *sock, int cmd)
{
	struct sk_buff *skb;
	struct cb_info *cbinfo;
	struct cl_portclosed_oob *oobmsg;

	skb = alloc_skb(sizeof (*oobmsg), GFP_KERNEL);
	if (!skb)
		return;

	skb_put(skb, sizeof (*oobmsg));
	oobmsg = (struct cl_portclosed_oob *) skb->data;
	oobmsg->port = 0;
	oobmsg->cmd = cmd;

	/* There is no remote node associated with this so
	   clear out the field to avoid any accidents */
	cbinfo = (struct cb_info *)skb->cb;
	cbinfo->oob = 1;
	cbinfo->orig_nodeid = 0;
	cbinfo->orig_port = 0;

	sock_queue_rcv_skb(sock->sk, skb);
}

/* Notify interested parties that the cluster configuration has changed */
void notify_listeners()
{
	struct notify_struct *notify;
	struct list_head *proclist;
	struct list_head *socklist;
	struct list_head *temp;

	/* Do kernel listeners first */
	notify_kernel_listeners(CLUSTER_RECONFIG, 0);

	/* Now we deign to tell userspace */
	down(&event_listener_lock);
	list_for_each_safe(proclist, temp, &event_listener_list) {
		notify = list_entry(proclist, struct notify_struct, list);

		/* If the kill fails then remove the process from the list */
		if (kill_proc(notify->pid, notify->signal, 0) == -ESRCH) {
			list_del(&notify->list);
			kfree(notify);
		}
	}
	up(&event_listener_lock);

	/* Tell userspace processes which want OOB messages */
	down(&client_socket_lock);
	list_for_each(socklist, &client_socket_list) {
		struct cl_client_socket *csock;
		csock = list_entry(socklist, struct cl_client_socket, list);
		queue_oob_skb(csock->sock, CLUSTER_OOB_MSG_STATECHANGE);
	}
	up(&client_socket_lock);
}

/* This fills in the list of all addresses for the local node */
void get_local_addresses(struct cluster_node *node)
{
	struct list_head *socklist;
	struct cl_comms_socket *sock;

	list_for_each(socklist, &socket_list) {
		sock = list_entry(socklist, struct cl_comms_socket, list);

		if (sock->recv_only) {
			add_node_address(node, (char *) &sock->saddr, address_length);
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
	return value & 0xFFFF;
}

/* Return the next comms socket we can use. */
static struct cl_comms_socket *get_next_interface(struct cl_comms_socket *cur)
{
	int next;
	struct list_head *socklist;

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
	list_for_each(socklist, &socket_list) {
		struct cl_comms_socket *sock;
		sock = list_entry(socklist, struct cl_comms_socket, list);

		if (!sock->recv_only && sock->number == next)
			return sock;
	}

	BUG();
	return NULL;
}

/* MUST be called with the barrier list lock held */
static struct cl_barrier *find_barrier(char *name)
{
	struct list_head *blist;
	struct cl_barrier *bar;

	list_for_each(blist, &barrier_list) {
		bar = list_entry(blist, struct cl_barrier, list);

		if (strcmp(name, bar->name) == 0)
			return bar;
	}
	return NULL;
}

/* Do the stuff we need to do when the barrier has completed phase 1 */
static void check_barrier_complete_phase1(struct cl_barrier *barrier)
{
	if (atomic_read(&barrier->got_nodes) == ((barrier->expected_nodes != 0)
						 ? barrier->expected_nodes :
						 cluster_members)) {

		struct cl_barriermsg bmsg;

		atomic_inc(&barrier->completed_nodes);	/* We have completed */
		barrier->phase = 2;	/* Wait for complete phase II */

		/* Send completion message, remember: we are in cnxman context
		 * and must not block */
		bmsg.cmd = CLUSTER_CMD_BARRIER;
		bmsg.subcmd = BARRIER_COMPLETE;
		bmsg.flags = 0;
		strcpy(bmsg.name, barrier->name);

		P_BARRIER("Sending COMPLETE for %s\n", barrier->name);
		queue_message(NULL, (char *) &bmsg, sizeof (bmsg), NULL, 0, 0);
	}
}

/* Do the stuff we need to do when the barrier has been reached */
/* Return 1 if we deleted the barrier */
static int check_barrier_complete_phase2(struct cl_barrier *barrier, int status)
{
	spin_lock_irq(&barrier->phase2_spinlock);

	if (barrier->state != BARRIER_STATE_COMPLETE &&
	    (status == -ETIMEDOUT ||
	     atomic_read(&barrier->completed_nodes) ==
	     ((barrier->expected_nodes != 0)
	      ? barrier->expected_nodes : cluster_members))) {

		if (status == 0 && barrier->timeout)
			del_timer(&barrier->timer);
		barrier->endreason = status;

		/* Wake up listener */
		if (barrier->state == BARRIER_STATE_WAITING) {
			wake_up_interruptible(&barrier->waitq);
		}
		else {
			/* Additional tasks we have to do if the user was not
			 * waiting... */
			/* Call the callback */
			if (barrier->callback) {
				barrier->callback(barrier->name, 0);
				barrier->callback = NULL;
			}
			/* Remove it if it's AUTO-DELETE */
			if (barrier->flags & BARRIER_ATTR_AUTODELETE) {
				list_del(&barrier->list);
				spin_unlock_irq(&barrier->phase2_spinlock);
				kfree(barrier);
				return 1;
			}
		}
		barrier->state = BARRIER_STATE_COMPLETE;
	}
	spin_unlock_irq(&barrier->phase2_spinlock);
	return 0;
}

/* Called if a barrier timeout happens */
static void barrier_timer_fn(unsigned long arg)
{
	struct cl_barrier *barrier = (struct cl_barrier *) arg;

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

	down(&barrier_list_lock);
	barrier = find_barrier(msg->name);
	up(&barrier_list_lock);

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
		down(&barrier->lock);
		if (barrier->phase == 0)
			barrier->phase = 1;

		if (barrier->phase == 1) {
			atomic_inc(&barrier->got_nodes);
			check_barrier_complete_phase1(barrier);
		}
		else {
			printk(KERN_WARNING CMAN_NAME
			       ": got WAIT barrier not in phase 1 %s (%d)\n",
			       msg->name, barrier->phase);

		}
		up(&barrier->lock);
		break;

	case BARRIER_COMPLETE:
		down(&barrier->lock);
		atomic_inc(&barrier->completed_nodes);

		/* First node to get all the WAIT messages sends COMPLETE, so
		 * we all complete */
		if (barrier->phase == 1) {
			atomic_set(&barrier->got_nodes,
				   barrier->expected_nodes);
			check_barrier_complete_phase1(barrier);
		}

		if (barrier->phase == 2) {
			/* If it was deleted (ret==1) then no need to unlock
			 * the mutex */
			if (check_barrier_complete_phase2(barrier, 0) == 1)
				return;
		}
		up(&barrier->lock);
		break;
	}
}

/* In-kernel membership API */
int kcl_add_callback(void (*callback) (kcl_callback_reason, long arg))
{
	struct kernel_notify_struct *notify;

	notify = kmalloc(sizeof (struct kernel_notify_struct), GFP_KERNEL);
	if (!notify)
		return -ENOMEM;
	notify->callback = callback;

	down(&kernel_listener_lock);
	list_add(&notify->list, &kernel_listener_list);
	up(&kernel_listener_lock);

	return 0;
}

int kcl_remove_callback(void (*callback) (kcl_callback_reason, long arg))
{
	struct list_head *calllist;
	struct list_head *temp;
	struct kernel_notify_struct *notify;

	down(&kernel_listener_lock);
	list_for_each_safe(calllist, temp, &kernel_listener_list) {
		notify = list_entry(calllist, struct kernel_notify_struct, list);
		if (notify->callback == callback){
			list_del(&notify->list);
			kfree(notify);
			up(&kernel_listener_lock);
			return 0;
		}
	}
	up(&kernel_listener_lock);
	return -EINVAL;
}

/* Return quorate status */
int kcl_is_quorate()
{
	return cluster_is_quorate;
}

/* Return the address list for a node */
struct list_head *kcl_get_node_addresses(int nodeid)
{
	struct cluster_node *node = find_node_by_nodeid(nodeid);

	if (node)
		return &node->addr_list;
	else
		return NULL;
}

static void copy_to_kclnode(struct cluster_node *node,
			    struct kcl_cluster_node *knode)
{
	strcpy(knode->name, node->name);
	knode->size = sizeof (struct kcl_cluster_node);
	knode->votes = node->votes;
	knode->state = node->state;
	knode->node_id = node->node_id;
	knode->us = node->us;
	knode->leave_reason = node->leave_reason;
	knode->incarnation = node->incarnation;
}

/* Return the info for a node given it's address. if addr is NULL then return
 * OUR info */
int kcl_get_node_by_addr(unsigned char *addr, int addr_len,
			 struct kcl_cluster_node *n)
{
	struct cluster_node *node;

	/* They want us */
	if (addr == NULL) {
		node = us;
	}
	else {
		node = find_node_by_addr(addr, addr_len);
		if (!node)
			return -1;
	}

	/* Copy to user's buffer */
	copy_to_kclnode(node, n);
	return 0;
}

int kcl_get_node_by_name(unsigned char *name, struct kcl_cluster_node *n)
{
	struct cluster_node *node;

	/* They want us */
	if (name == NULL) {
		node = us;
		if (node == NULL)
			return -1;
	}
	else {
		node = find_node_by_name(name);
		if (!node)
			return -1;
	}

	/* Copy to user's buffer */
	copy_to_kclnode(node, n);
	return 0;
}

/* As above but by node id. MUCH faster */
int kcl_get_node_by_nodeid(int nodeid, struct kcl_cluster_node *n)
{
	struct cluster_node *node;

	/* They want us */
	if (nodeid == 0) {
		node = us;
		if (node == NULL)
			return -1;
	}
	else {
		node = find_node_by_nodeid(nodeid);
		if (!node)
			return -1;
	}

	/* Copy to user's buffer */
	copy_to_kclnode(node, n);
	return 0;
}

/* Return a list of all cluster members ever */
int kcl_get_all_members(struct list_head *list)
{
	struct list_head *nodelist;
	struct cluster_node *node;
	struct kcl_cluster_node *newnode;
	int num_nodes = 0;

	down(&cluster_members_lock);
	list_for_each(nodelist, &cluster_members_list) {
		if (list) {
			node = list_entry(nodelist, struct cluster_node, list);
			newnode =
			    kmalloc(sizeof (struct kcl_cluster_node),
				    GFP_KERNEL);
			if (newnode) {
				copy_to_kclnode(node, newnode);
				list_add(&newnode->list, list);
				num_nodes++;
			}
		}
		else {
			num_nodes++;
		}
	}
	up(&cluster_members_lock);

	return num_nodes;
}

/* Return a list of cluster members */
int kcl_get_members(struct list_head *list)
{
	struct list_head *nodelist;
	struct cluster_node *node;
	struct kcl_cluster_node *newnode;
	int num_nodes = 0;

	down(&cluster_members_lock);
	list_for_each(nodelist, &cluster_members_list) {
		node = list_entry(nodelist, struct cluster_node, list);

		if (node->state == NODESTATE_MEMBER) {
			if (list) {
				newnode =
				    kmalloc(sizeof (struct kcl_cluster_node),
					    GFP_KERNEL);
				if (newnode) {
					copy_to_kclnode(node, newnode);
					list_add(&newnode->list, list);
					num_nodes++;
				}
			}
			else {
				num_nodes++;
			}
		}
	}
	up(&cluster_members_lock);

	return num_nodes;
}

/* Copy current member's nodeids into buffer */
int kcl_get_member_ids(uint32_t *idbuf, int size)
{
	struct list_head *nodelist;
	struct cluster_node *node;
	int num_nodes = 0;

	down(&cluster_members_lock);
	list_for_each(nodelist, &cluster_members_list) {
		node = list_entry(nodelist, struct cluster_node, list);

		if (node->state == NODESTATE_MEMBER) {
			if (idbuf && size) {
				idbuf[num_nodes] = node->node_id;
				num_nodes++;
				size--;
			}
			else {
				num_nodes++;
			}
		}
	}
	up(&cluster_members_lock);

	return num_nodes;
}

/* Barrier API */
int kcl_barrier_register(char *name, unsigned int flags, unsigned int nodes)
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
		return -ENOTSUPP;

	down(&barrier_list_lock);

	/* See if it already exists */
	if ((barrier = find_barrier(name))) {
		up(&barrier_list_lock);
		if (nodes != barrier->expected_nodes) {
			printk(KERN_WARNING CMAN_NAME
			       ": Barrier registration failed for '%s', expected nodes=%d, requested=%d\n",
			       name, barrier->expected_nodes, nodes);
			up(&barrier_list_lock);
			return -EINVAL;
		}
		else
			return 0;
	}

	/* Build a new struct and add it to the list */
	barrier = kmalloc(sizeof (struct cl_barrier), GFP_KERNEL);
	if (barrier == NULL) {
		up(&barrier_list_lock);
		return -ENOMEM;
	}
	memset(barrier, 0, sizeof (*barrier));

	strcpy(barrier->name, name);
	barrier->flags = flags;
	barrier->expected_nodes = nodes;
	atomic_set(&barrier->got_nodes, 0);
	atomic_set(&barrier->completed_nodes, 0);
	barrier->endreason = 0;
	barrier->registered_nodes = 1;
	spin_lock_init(&barrier->phase2_spinlock);
	barrier->state = BARRIER_STATE_INACTIVE;
	init_MUTEX(&barrier->lock);

	list_add(&barrier->list, &barrier_list);
	up(&barrier_list_lock);

	return 0;
}

static int barrier_setattr_enabled(struct cl_barrier *barrier,
				   unsigned int attr, unsigned long arg)
{
	int status;

	/* Can't disable a barrier */
	if (!arg) {
		up(&barrier->lock);
		return -EINVAL;
	}

	/* We need to send WAIT now because the user may not
	 * actually call kcl_barrier_wait() */
	if (!barrier->waitsent) {
		struct cl_barriermsg bmsg;

		/* Send it to the rest of the cluster */
		bmsg.cmd = CLUSTER_CMD_BARRIER;
		bmsg.subcmd = BARRIER_WAIT;
		strcpy(bmsg.name, barrier->name);

		barrier->waitsent = 1;
		barrier->phase = 1;

		atomic_inc(&barrier->got_nodes);

		/* Start the timer if one was wanted */
		if (barrier->timeout) {
			init_timer(&barrier->timer);
			barrier->timer.function = barrier_timer_fn;
			barrier->timer.data = (long) barrier;
			mod_timer(&barrier->timer, jiffies + (barrier->timeout * HZ));
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
			up(&barrier->lock);
			return status;
		}

		/* It might have been reached now */
		if (barrier
		    && barrier->state != BARRIER_STATE_COMPLETE
		    && barrier->phase == 1)
			check_barrier_complete_phase1(barrier);
	}
	if (barrier && barrier->state == BARRIER_STATE_COMPLETE) {
		up(&barrier->lock);
		return barrier->endreason;
	}
	up(&barrier->lock);
	return 0;	/* Nothing to propogate */
}

int kcl_barrier_setattr(char *name, unsigned int attr, unsigned long arg)
{
	struct cl_barrier *barrier;

	/* See if it already exists */
	down(&barrier_list_lock);
	if (!(barrier = find_barrier(name))) {
		up(&barrier_list_lock);
		return -ENOENT;
	}
	up(&barrier_list_lock);

	down(&barrier->lock);
	if (barrier->state == BARRIER_STATE_COMPLETE) {
		up(&barrier->lock);
		return 0;
	}

	switch (attr) {
	case BARRIER_SETATTR_AUTODELETE:
		if (arg)
			barrier->flags |= BARRIER_ATTR_AUTODELETE;
		else
			barrier->flags &= ~BARRIER_ATTR_AUTODELETE;
		up(&barrier->lock);
		return 0;
		break;

	case BARRIER_SETATTR_TIMEOUT:
		/* Can only change the timout of an inactive barrier */
		if (barrier->state == BARRIER_STATE_WAITING
		    || barrier->waitsent) {
			up(&barrier->lock);
			return -EINVAL;
		}
		barrier->timeout = arg;
		up(&barrier->lock);
		return 0;

	case BARRIER_SETATTR_MULTISTEP:
		up(&barrier->lock);
		return -ENOTSUPP;

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

	case BARRIER_SETATTR_CALLBACK:
		if (barrier->state == BARRIER_STATE_WAITING
		    || barrier->waitsent)
			return -EINVAL;
		barrier->callback = (void (*)(char *, int)) arg;
		up(&barrier->lock);
		return 0;	/* Don't propgate this to other nodes */
	}

	up(&barrier->lock);
	return 0;
}

int kcl_barrier_delete(char *name)
{
	struct cl_barrier *barrier;

	down(&barrier_list_lock);
	/* See if it exists */
	if (!(barrier = find_barrier(name))) {
		up(&barrier_list_lock);
		return -ENOENT;
	}

	/* Delete it */
	list_del(&barrier->list);
	kfree(barrier);

	up(&barrier_list_lock);

	return 0;
}

int kcl_barrier_cancel(char *name)
{
	struct cl_barrier *barrier;

	/* See if it exists */
	down(&barrier_list_lock);
	if (!(barrier = find_barrier(name))) {
		up(&barrier_list_lock);
		return -ENOENT;
	}
	down(&barrier->lock);

	barrier->endreason = -ENOTCONN;

	if (barrier->callback) {
		barrier->callback(barrier->name, -ECONNRESET);
		barrier->callback = NULL;
	}

	if (barrier->timeout)
		del_timer(&barrier->timer);

	/* Remove it if it's AUTO-DELETE */
	if (barrier->flags & BARRIER_ATTR_AUTODELETE) {
		list_del(&barrier->list);
		up(&barrier->lock);
		kfree(barrier);
		up(&barrier_list_lock);
		return 0;
	}

	if (barrier->state == BARRIER_STATE_WAITING)
		wake_up_interruptible(&barrier->waitq);

	up(&barrier->lock);
	up(&barrier_list_lock);
	return 0;
}

int kcl_barrier_wait(char *name)
{
	struct cl_barrier *barrier;
	int ret;

	if (!atomic_read(&cnxman_running))
		return -ENOTCONN;

	/* Enable it */
	kcl_barrier_setattr(name, BARRIER_SETATTR_ENABLED, 1L);

	down(&barrier_list_lock);

	/* See if it still exists - enable may have deleted it! */
	if (!(barrier = find_barrier(name))) {
		up(&barrier_list_lock);
		return -ENOENT;
	}

	down(&barrier->lock);

	up(&barrier_list_lock);

	/* If it has already completed then return the status */
	if (barrier->state == BARRIER_STATE_COMPLETE) {
		up(&barrier->lock);
		return barrier->endreason;
	}

	barrier->state = BARRIER_STATE_WAITING;

	/* Have we all reached the barrier? */
	while (atomic_read(&barrier->completed_nodes) !=
	       ((barrier->expected_nodes == 0)
		? cluster_members : barrier->expected_nodes)
	       && barrier->endreason == 0) {

		wait_queue_t wq;

		init_waitqueue_entry(&wq, current);
		init_waitqueue_head(&barrier->waitq);

		/* Wait for em all */
		set_task_state(current, TASK_INTERRUPTIBLE);
		add_wait_queue(&barrier->waitq, &wq);

		if (atomic_read(&barrier->completed_nodes) !=
		    ((barrier->expected_nodes ==
		      0) ? cluster_members : barrier->expected_nodes)
		    && barrier->endreason == 0) {
			up(&barrier->lock);
			schedule();
			down(&barrier->lock);
		}

		remove_wait_queue(&barrier->waitq, &wq);
		set_task_state(current, TASK_RUNNING);

		if (signal_pending(current)) {
			barrier->endreason = -EINTR;
			break;
		}
	}
	barrier->state = BARRIER_STATE_INACTIVE;

	if (barrier->timeout)
		del_timer(&barrier->timer);

	/* Barrier has been reached on all nodes, call the callback */
	if (barrier->callback) {
		barrier->callback(barrier->name, barrier->endreason);
		barrier->callback = NULL;
	}

	atomic_set(&barrier->got_nodes, 0);

	/* Return the reason we were woken */
	ret = barrier->endreason;

	/* Remove it if it's AUTO-DELETE */
	if (barrier->flags & BARRIER_ATTR_AUTODELETE) {
		down(&barrier_list_lock);
		list_del(&barrier->list);
		up(&barrier_list_lock);
		up(&barrier->lock);
		kfree(barrier);
	}
	else {
		up(&barrier->lock);
	}

	/* We were woken up because the node left the cluster ? */
	if (!atomic_read(&cnxman_running))
		ret = -ENOTCONN;

	return ret;
}

/* This is called from membership services when a node has left the cluster -
 * we signal all waiting barriers with -ESRCH so they know to do something
 * else, if the number of nodes is left at 0 then we compare the new number of
 * nodes in the cluster with that at the barrier and return 0 (success) in that
 * case */
void check_barrier_returns()
{
	struct list_head *blist;
	struct list_head *llist;
	struct cl_barrier *barrier;
	int status = 0;

	down(&barrier_list_lock);
	list_for_each(blist, &barrier_list) {
		barrier = list_entry(blist, struct cl_barrier, list);

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
				status = -ESRCH;
				wakeit = 1;
			}

			/* Do we need to tell the barrier? */
			if (wakeit) {
				if (barrier->state == BARRIER_STATE_WAITING) {
					barrier->endreason = status;
					wake_up_interruptible(&barrier->waitq);
				}
				else {
					if (barrier->callback) {
						barrier->callback(barrier->name,
								  status);
					}
				}
			}
		}
	}
	up(&barrier_list_lock);

	/* Part 2 check for outstanding listen requests for dead nodes and
	 * cancel them */
	down(&listenreq_lock);
	list_for_each(llist, &listenreq_list) {
		struct cl_waiting_listen_request *lrequest =
		    list_entry(llist, struct cl_waiting_listen_request, list);
		struct cluster_node *node =
		    find_node_by_nodeid(lrequest->nodeid);

		if (node && node->state != NODESTATE_MEMBER) {
			lrequest->result = -ENOTCONN;
			lrequest->waiting = 0;
			wake_up_interruptible(&lrequest->waitq);
		}
	}
	up(&listenreq_lock);
}

int get_addr_from_temp_nodeid(int nodeid, char *addr, int *addrlen)
{
	struct temp_node *tn;
	int err = 1; /* true */
#ifdef DEBUG_COMMS
	char buf[MAX_ADDR_PRINTED_LEN];
#endif

	down(&tempnode_lock);

	list_for_each_entry(tn, &tempnode_list, list) {
		if (tn->nodeid == nodeid) {
			memcpy(addr, tn->addr, tn->addrlen);
			*addrlen = tn->addrlen;
			P_COMMS("get_temp_nodeid. id %d:\n: %s\n",
				tn->nodeid, print_addr(tn->addr, tn->addrlen, buf));

			goto out;
		}
	}
	err = 0;

 out:
	up(&tempnode_lock);
	return err;
}

/* Create a new temporary node ID. This list will only ever be very small
   (usaully only 1 item) but I can't take the risk that someone won't try to
   boot 128 nodes all at exactly the same time. */
int new_temp_nodeid(char *addr, int addrlen)
{
	struct temp_node *tn;
	int err = -1;
	int try_nodeid = 0;
#ifdef DEBUG_COMMS
	char buf[MAX_ADDR_PRINTED_LEN];
#endif

	P_COMMS("new_temp_nodeid needed for\n: %s\n",
		print_addr(addr, addrlen, buf));

	down(&tempnode_lock);

	/* First see if we already know about this node */
	list_for_each_entry(tn, &tempnode_list, list) {

		P_COMMS("new_temp_nodeid list. id %d:\n: %s\n",
			tn->nodeid, print_addr(tn->addr, tn->addrlen, buf));

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
	list_for_each_entry(tn, &tempnode_list, list) {

		if (tn->nodeid == try_nodeid)
			goto retry;
	}

	tn = kmalloc(sizeof(struct temp_node), GFP_KERNEL);
	if (!tn)
		goto out;

	memcpy(tn->addr, addr, addrlen);
	tn->addrlen = addrlen;
	tn->nodeid = try_nodeid;
	list_add_tail(&tn->list, &tempnode_list);
	err = try_nodeid;
	P_COMMS("new temp nodeid = %d\n", try_nodeid);
 out:
	up(&tempnode_lock);
	return err;
}

static int is_valid_temp_nodeid(int nodeid)
{
	struct temp_node *tn;
	int err = 1; /* true */

	down(&tempnode_lock);

	list_for_each_entry(tn, &tempnode_list, list) {
		if (tn->nodeid == nodeid)
			goto out;
	}
	err = 0;

 out:
	P_COMMS("is_valid_temp_nodeid. %d = %d\n", nodeid, err);
	up(&tempnode_lock);
	return err;
}

/*
 * Remove any temp nodeIDs that refer to now-valid cluster members.
 */
void purge_temp_nodeids()
{
	struct temp_node *tn;
	struct temp_node *tmp;
	struct cluster_node *node;
	struct cluster_node_addr *nodeaddr;


	down(&tempnode_lock);
	down(&cluster_members_lock);

	/*
	 * The ordering of these nested lists is deliberately
	 * arranged for the fewest list traversals overall
	 */

	/* For each node... */
	list_for_each_entry(node, &cluster_members_list, list) {
		if (node->state == NODESTATE_MEMBER) {
			/* ...We check the temp node ID list... */
			list_for_each_entry_safe(tn, tmp, &tempnode_list, list) {

				/* ...against that node's address */
				list_for_each_entry(nodeaddr, &node->addr_list, list) {

					if (memcmp(nodeaddr->addr, tn->addr, tn->addrlen) == 0) {
						list_del(&tn->list);
						kfree(tn);
					}
				}
			}
		}
	}
	up(&cluster_members_lock);
	up(&tempnode_lock);
}


/* Quorum device functions */
int kcl_register_quorum_device(char *name, int votes)
{
	if (quorum_device)
		return -EBUSY;

	if (find_node_by_name(name))
		return -EINVAL;

	quorum_device = kmalloc(sizeof (struct cluster_node), GFP_KERNEL);
	if (!quorum_device)
		return -ENOMEM;
	memset(quorum_device, 0, sizeof (struct cluster_node));

	quorum_device->name = kmalloc(strlen(name) + 1, GFP_KERNEL);
	if (!quorum_device->name) {
		kfree(quorum_device);
		quorum_device = NULL;
		return -ENOMEM;
	}

	strcpy(quorum_device->name, name);
	quorum_device->votes = votes;
	quorum_device->state = NODESTATE_DEAD;

	/* Keep this list valid so it doesn't confuse other code */
	INIT_LIST_HEAD(&quorum_device->addr_list);

	return 0;
}

int kcl_unregister_quorum_device(void)
{
	if (!quorum_device)
		return -EINVAL;
	if (quorum_device->state == NODESTATE_MEMBER)
		return -EINVAL;

	quorum_device = NULL;

	return 0;
}

int kcl_quorum_device_available(int yesno)
{
	if (!quorum_device)
		return -EINVAL;

	if (yesno) {
		quorum_device->last_hello = jiffies;
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

/* APIs for cluster ref counting. */
int kcl_addref_cluster()
{
	int ret = -ENOTCONN;

	if (!atomic_read(&cnxman_running))
		goto addref_ret;

	if (try_module_get(THIS_MODULE)) {
		atomic_inc(&use_count);
		ret = 0;
	}

      addref_ret:
	return ret;
}

int kcl_releaseref_cluster()
{
	if (!atomic_read(&cnxman_running))
		return -ENOTCONN;
	atomic_dec(&use_count);
	module_put(THIS_MODULE);
	return 0;
}

int kcl_cluster_name(char **cname)
{
	char *name;

	name = kmalloc(strlen(cluster_name) + 1, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	strncpy(name, cluster_name, strlen(cluster_name)+1);
	*cname = name;
	return 0;
}

int kcl_get_current_interface(void)
{
	return current_interface->number;
}

/* Socket registration stuff */
static struct net_proto_family cl_family_ops = {
	.family = AF_CLUSTER,
	.create = cl_create,
	.owner  = THIS_MODULE,
};

static struct proto_ops cl_proto_ops = {
	.family      = AF_CLUSTER,

	.release     = cl_release,
	.bind        = cl_bind,
	.connect     = sock_no_connect,
	.socketpair  = sock_no_socketpair,
	.accept      = sock_no_accept,
	.getname     = cl_getname,
	.poll        = cl_poll,
	.ioctl       = cl_ioctl,
	.listen      = sock_no_listen,
	.shutdown    = cl_shutdown,
	.setsockopt  = sock_no_setsockopt,
	.getsockopt  = sock_no_getsockopt,
	.sendmsg     = cl_sendmsg,
	.recvmsg     = cl_recvmsg,
	.mmap        = sock_no_mmap,
	.sendpage    = sock_no_sendpage,
	.owner       = THIS_MODULE,
};

#ifdef MODULE
MODULE_DESCRIPTION("Cluster Connection and Service Manager");
MODULE_AUTHOR("Red Hat, Inc");
MODULE_LICENSE("GPL");
#endif

static int __init cluster_init(void)
{
	printk("CMAN %s (built %s %s) installed\n",
	       CMAN_RELEASE_NAME, __DATE__, __TIME__);

	if (sock_register(&cl_family_ops)) {
		printk(KERN_INFO "Unable to register cluster socket type\n");
		return -1;
	}

	/* allocate our sock slab cache */
	cluster_sk_cachep = kmem_cache_create("cluster_sock",
					      sizeof (struct cluster_sock), 0,
					      SLAB_HWCACHE_ALIGN, 0, 0);
	if (!cluster_sk_cachep) {
		printk(KERN_CRIT
		       "cluster_init: Cannot create cluster_sock SLAB cache\n");
		sock_unregister(AF_CLUSTER);
		return -1;
	}

#ifdef CONFIG_PROC_FS
	create_proc_entries();
#endif

	init_MUTEX(&start_thread_sem);
	init_MUTEX(&send_lock);
	init_MUTEX(&barrier_list_lock);
	init_MUTEX(&cluster_members_lock);
	init_MUTEX(&port_array_lock);
	init_MUTEX(&messages_list_lock);
	init_MUTEX(&listenreq_lock);
	init_MUTEX(&client_socket_lock);
	init_MUTEX(&new_dead_node_lock);
	init_MUTEX(&event_listener_lock);
	init_MUTEX(&kernel_listener_lock);
	init_MUTEX(&tempnode_lock);
	spin_lock_init(&active_socket_lock);
	init_timer(&ack_timer);

	INIT_LIST_HEAD(&event_listener_list);
	INIT_LIST_HEAD(&kernel_listener_list);
	INIT_LIST_HEAD(&socket_list);
	INIT_LIST_HEAD(&client_socket_list);
	INIT_LIST_HEAD(&active_socket_list);
	INIT_LIST_HEAD(&barrier_list);
	INIT_LIST_HEAD(&messages_list);
	INIT_LIST_HEAD(&listenreq_list);
	INIT_LIST_HEAD(&cluster_members_list);
	INIT_LIST_HEAD(&new_dead_node_list);
	INIT_LIST_HEAD(&tempnode_list);

	atomic_set(&cnxman_running, 0);

	sm_init();

	return 0;
}

static void __exit cluster_exit(void)
{
#ifdef CONFIG_PROC_FS
	cleanup_proc_entries();
#endif

	sock_unregister(AF_CLUSTER);
	kmem_cache_destroy(cluster_sk_cachep);
}

module_init(cluster_init);
module_exit(cluster_exit);

EXPORT_SYMBOL(kcl_sendmsg);
EXPORT_SYMBOL(kcl_register_read_callback);
EXPORT_SYMBOL(kcl_add_callback);
EXPORT_SYMBOL(kcl_remove_callback);
EXPORT_SYMBOL(kcl_get_members);
EXPORT_SYMBOL(kcl_get_member_ids);
EXPORT_SYMBOL(kcl_get_all_members);
EXPORT_SYMBOL(kcl_is_quorate);
EXPORT_SYMBOL(kcl_get_node_by_addr);
EXPORT_SYMBOL(kcl_get_node_by_name);
EXPORT_SYMBOL(kcl_get_node_by_nodeid);
EXPORT_SYMBOL(kcl_get_node_addresses);
EXPORT_SYMBOL(kcl_addref_cluster);
EXPORT_SYMBOL(kcl_releaseref_cluster);
EXPORT_SYMBOL(kcl_cluster_name);

EXPORT_SYMBOL(kcl_barrier_register);
EXPORT_SYMBOL(kcl_barrier_setattr);
EXPORT_SYMBOL(kcl_barrier_delete);
EXPORT_SYMBOL(kcl_barrier_wait);
EXPORT_SYMBOL(kcl_barrier_cancel);

EXPORT_SYMBOL(kcl_register_quorum_device);
EXPORT_SYMBOL(kcl_unregister_quorum_device);
EXPORT_SYMBOL(kcl_quorum_device_available);

EXPORT_SYMBOL(kcl_register_service);
EXPORT_SYMBOL(kcl_unregister_service);
EXPORT_SYMBOL(kcl_join_service);
EXPORT_SYMBOL(kcl_leave_service);
EXPORT_SYMBOL(kcl_global_service_id);
EXPORT_SYMBOL(kcl_start_done);
EXPORT_SYMBOL(kcl_get_services);
EXPORT_SYMBOL(kcl_get_current_interface);

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
