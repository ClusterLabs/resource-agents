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

/*
 * lowcomms.c
 *
 * This is the "low-level" comms layer.
 *
 * It is responsible for sending/receiving messages
 * from other nodes in the cluster.
 *
 * Cluster nodes are referred to by their nodeids. nodeids are
 * simply 32 bit numbers to the locking module - if they need to
 * be expanded for the cluster infrastructure then that is it's
 * responsibility. It is this layer's
 * responsibility to resolve these into IP address or
 * whatever it needs for inter-node communication.
 *
 * The comms level is two kernel threads that deal mainly with
 * the receiving of messages from other nodes and passing them
 * up to the mid-level comms layer (which understands the
 * message format) for execution by the locking core, and
 * a send thread which does all the setting up of connections
 * to remote nodes and the sending of data. Threads are not allowed
 * to send their own data because it may cause them to wait in times
 * of high load. Also, this way, the sending thread can collect together
 * messages bound for one node and send them in one block.
 *
 * I don't see any problem with the recv thread executing the locking
 * code on behalf of remote processes as the locking code is
 * short, efficient and never waits.
 *
 */


#include <asm/ioctls.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <linux/pagemap.h>
#include <cluster/cnxman.h>

#include "dlm_internal.h"
#include "lowcomms.h"
#include "midcomms.h"
#include "config.h"

struct cbuf {
	unsigned base;
	unsigned len;
	unsigned mask;
};

#define CBUF_INIT(cb, size) do { (cb)->base = (cb)->len = 0; (cb)->mask = ((size)-1); } while(0)
#define CBUF_ADD(cb, n) do { (cb)->len += n; } while(0)
#define CBUF_EMPTY(cb) ((cb)->len == 0)
#define CBUF_MAY_ADD(cb, n) (((cb)->len + (n)) < ((cb)->mask + 1))
#define CBUF_EAT(cb, n) do { (cb)->len  -= (n); \
                             (cb)->base += (n); (cb)->base &= (cb)->mask; } while(0)
#define CBUF_DATA(cb) (((cb)->base + (cb)->len) & (cb)->mask)

struct connection {
	struct socket *sock;	/* NULL if not connected */
	uint32_t nodeid;	/* So we know who we are in the list */
	struct rw_semaphore sock_sem;	/* Stop connect races */
	struct list_head read_list;	/* On this list when ready for reading */
	struct list_head write_list;	/* On this list when ready for writing */
	struct list_head state_list;	/* On this list when ready to connect */
	unsigned long flags;	/* bit 1,2 = We are on the read/write lists */
#define CF_READ_PENDING 1
#define CF_WRITE_PENDING 2
#define CF_CONNECT_PENDING 3
#define CF_IS_OTHERSOCK 4
	struct list_head writequeue;	/* List of outgoing writequeue_entries */
	struct list_head listenlist;    /* List of allocated listening sockets */
	spinlock_t writequeue_lock;
	int (*rx_action) (struct connection *);	/* What to do when active */
	struct page *rx_page;
	struct cbuf cb;
	int retries;
	atomic_t waiting_requests;
#define MAX_CONNECT_RETRIES 3
	struct connection *othersock;
};
#define sock2con(x) ((struct connection *)(x)->sk_user_data)
#define nodeid2con(x) (&connections[(x)])

/* An entry waiting to be sent */
struct writequeue_entry {
	struct list_head list;
	struct page *page;
	int offset;
	int len;
	int end;
	int users;
	struct connection *con;
};

/* "Template" structure for IPv4 and IPv6 used to fill
 * in the missing bits when converting between cman (which knows
 * nothing about sockaddr structs) and real life where we actually
 * have to connect to these addresses. Also one of these structs
 * will hold the cached "us" address.
 *
 * It's an in6 sockaddr just so there's enough space for anything
 * we're likely to see here.
 */
static struct sockaddr_in6 local_addr;

/* Manage daemons */
static struct semaphore thread_lock;
static struct completion thread_completion;
static atomic_t send_run;
static atomic_t recv_run;

/* An array of connections, indexed by NODEID */
static struct connection *connections;
static int conn_array_size;
static atomic_t writequeue_length;
static atomic_t accepting;

static wait_queue_t lowcomms_send_waitq_head;
static wait_queue_head_t lowcomms_send_waitq;

static wait_queue_t lowcomms_recv_waitq_head;
static wait_queue_head_t lowcomms_recv_waitq;

/* List of sockets that have reads pending */
static struct list_head read_sockets;
static spinlock_t read_sockets_lock;

/* List of sockets which have writes pending */
static struct list_head write_sockets;
static spinlock_t write_sockets_lock;

/* List of sockets which have connects pending */
static struct list_head state_sockets;
static spinlock_t state_sockets_lock;

/* List of allocated listen sockets */
static struct list_head listen_sockets;

static int lowcomms_ipaddr_from_nodeid(int nodeid, struct sockaddr *retaddr);
static int lowcomms_nodeid_from_ipaddr(struct sockaddr *addr, int addr_len);


/* Data available on socket or listen socket received a connect */
static void lowcomms_data_ready(struct sock *sk, int count_unused)
{
	struct connection *con = sock2con(sk);

	atomic_inc(&con->waiting_requests);
	if (test_and_set_bit(CF_READ_PENDING, &con->flags))
		return;

	spin_lock_bh(&read_sockets_lock);
	list_add_tail(&con->read_list, &read_sockets);
	spin_unlock_bh(&read_sockets_lock);

	wake_up_interruptible(&lowcomms_recv_waitq);
}

static void lowcomms_write_space(struct sock *sk)
{
	struct connection *con = sock2con(sk);

	if (test_and_set_bit(CF_WRITE_PENDING, &con->flags))
		return;

	spin_lock_bh(&write_sockets_lock);
	list_add_tail(&con->write_list, &write_sockets);
	spin_unlock_bh(&write_sockets_lock);

	wake_up_interruptible(&lowcomms_send_waitq);
}

static inline void lowcomms_connect_sock(struct connection *con)
{
	if (test_and_set_bit(CF_CONNECT_PENDING, &con->flags))
		return;
	if (!atomic_read(&accepting))
		return;

	spin_lock_bh(&state_sockets_lock);
	list_add_tail(&con->state_list, &state_sockets);
	spin_unlock_bh(&state_sockets_lock);

	wake_up_interruptible(&lowcomms_send_waitq);
}

static void lowcomms_state_change(struct sock *sk)
{
/*	struct connection *con = sock2con(sk); */

	switch (sk->sk_state) {
	case TCP_ESTABLISHED:
		lowcomms_write_space(sk);
		break;

	case TCP_FIN_WAIT1:
	case TCP_FIN_WAIT2:
	case TCP_TIME_WAIT:
	case TCP_CLOSE:
	case TCP_CLOSE_WAIT:
	case TCP_LAST_ACK:
	case TCP_CLOSING:
		/* FIXME: I think this causes more trouble than it solves.
		   lowcomms wil reconnect anyway when there is something to
		   send. This just attempts reconnection if a node goes down!
		*/
		/* lowcomms_connect_sock(con); */
		break;

	default:
		printk("dlm: lowcomms_state_change: state=%d\n", sk->sk_state);
		break;
	}
}

/* Make a socket active */
static int add_sock(struct socket *sock, struct connection *con)
{
	con->sock = sock;

	/* Install a data_ready callback */
	con->sock->sk->sk_data_ready = lowcomms_data_ready;
	con->sock->sk->sk_write_space = lowcomms_write_space;
	con->sock->sk->sk_state_change = lowcomms_state_change;

	return 0;
}

/* Add the port number to an IP6 or 4 sockaddr and return the address
   length */
static void make_sockaddr(struct sockaddr_in6 *saddr, uint16_t port,
			  int *addr_len)
{
        saddr->sin6_family = local_addr.sin6_family;
        if (local_addr.sin6_family == AF_INET) {
	    struct sockaddr_in *in4_addr = (struct sockaddr_in *)saddr;
	    in4_addr->sin_port = cpu_to_be16(port);
	    *addr_len = sizeof(struct sockaddr_in);
	}
	else {
	    saddr->sin6_port = cpu_to_be16(port);
	    *addr_len = sizeof(struct sockaddr_in6);
	}
}

/* Close a remote connection and tidy up */
static void close_connection(struct connection *con)
{
	if (test_bit(CF_IS_OTHERSOCK, &con->flags))
		return;

	down_write(&con->sock_sem);

	if (con->sock) {
		sock_release(con->sock);
		con->sock = NULL;
		if (con->othersock) {
			down_write(&con->othersock->sock_sem);
			sock_release(con->othersock->sock);
			con->othersock->sock = NULL;
			up_write(&con->othersock->sock_sem);
			kfree(con->othersock);
			con->othersock = NULL;
		}
	}
	if (con->rx_page) {
		__free_page(con->rx_page);
		con->rx_page = NULL;
	}
	up_write(&con->sock_sem);
}

/* Data received from remote end */
static int receive_from_sock(struct connection *con)
{
	int ret = 0;
	struct msghdr msg;
	struct iovec iov[2];
	mm_segment_t fs;
	unsigned len;
	int r;
	int call_again_soon = 0;

	down_read(&con->sock_sem);

	if (con->sock == NULL)
		goto out;
	if (con->rx_page == NULL) {
		/*
		 * This doesn't need to be atomic, but I think it should
		 * improve performance if it is.
		 */
		con->rx_page = alloc_page(GFP_ATOMIC);
		if (con->rx_page == NULL)
			goto out_resched;
		CBUF_INIT(&con->cb, PAGE_CACHE_SIZE);
	}
	/*
	 * To avoid doing too many short reads, we will reschedule for another
	 * another time if there are less than 32 bytes left in the buffer.
	 */
	if (!CBUF_MAY_ADD(&con->cb, 32))
		goto out_resched;

	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_iovlen = 1;
	msg.msg_iov = iov;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_flags = 0;

	/*
	 * iov[0] is the bit of the circular buffer between the current end
	 * point (cb.base + cb.len) and the end of the buffer.
	 */
	iov[0].iov_len = con->cb.base - CBUF_DATA(&con->cb);
	iov[0].iov_base = page_address(con->rx_page) + CBUF_DATA(&con->cb);
	iov[1].iov_len = 0;

	/*
	 * iov[1] is the bit of the circular buffer between the start of the
	 * buffer and the start of the currently used section (cb.base)
	 */
	if (CBUF_DATA(&con->cb) >= con->cb.base) {
		iov[0].iov_len = PAGE_CACHE_SIZE - CBUF_DATA(&con->cb);
		iov[1].iov_len = con->cb.base;
		iov[1].iov_base = page_address(con->rx_page);
		msg.msg_iovlen = 2;
	}
	len = iov[0].iov_len + iov[1].iov_len;

	fs = get_fs();
	set_fs(get_ds());
	r = ret = sock_recvmsg(con->sock, &msg, len,
			       MSG_DONTWAIT | MSG_NOSIGNAL);
	set_fs(fs);

	if (ret <= 0)
		goto out_close;
	if (ret == len)
		call_again_soon = 1;
	CBUF_ADD(&con->cb, ret);
	ret = midcomms_process_incoming_buffer(con->nodeid,
					       page_address(con->rx_page),
					       con->cb.base, con->cb.len,
					       PAGE_CACHE_SIZE);
	if (ret == -EBADMSG) {
		printk(KERN_INFO "dlm: lowcomms: addr=%p, base=%u, len=%u, "
		       "iov_len=%u, iov_base[0]=%p, read=%d\n",
		       page_address(con->rx_page), con->cb.base, con->cb.len,
		       len, iov[0].iov_base, r);
	}
	if (ret < 0)
		goto out_close;
	CBUF_EAT(&con->cb, ret);

	if (CBUF_EMPTY(&con->cb) && !call_again_soon) {
		__free_page(con->rx_page);
		con->rx_page = NULL;
	}

      out:
	if (call_again_soon)
		goto out_resched;
	up_read(&con->sock_sem);
	ret = 0;
	goto out_ret;

      out_resched:
	lowcomms_data_ready(con->sock->sk, 0);
	up_read(&con->sock_sem);
	ret = 0;
	goto out_ret;

      out_close:
	up_read(&con->sock_sem);
	if (ret != -EAGAIN && !test_bit(CF_IS_OTHERSOCK, &con->flags)) {
		close_connection(con);
		lowcomms_connect_sock(con);
	}

      out_ret:
	return ret;
}

/* Listening socket is busy, accept a connection */
static int accept_from_sock(struct connection *con)
{
	int result;
	struct sockaddr_in6 peeraddr;
	struct socket *newsock;
	int len;
	int nodeid;
	struct connection *newcon;

	memset(&peeraddr, 0, sizeof(peeraddr));
	newsock = sock_alloc();
	if (!newsock)
		return -ENOMEM;

	down_read(&con->sock_sem);

	result = -ENOTCONN;
	if (con->sock == NULL)
		goto accept_err;

	newsock->type = con->sock->type;
	newsock->ops = con->sock->ops;

	result = con->sock->ops->accept(con->sock, newsock, O_NONBLOCK);
	if (result < 0)
		goto accept_err;

	/* Get the connected socket's peer */
	if (newsock->ops->getname(newsock, (struct sockaddr *)&peeraddr,
				  &len, 2)) {
		result = -ECONNABORTED;
		goto accept_err;
	}

	/* Get the new node's NODEID */
	nodeid = lowcomms_nodeid_from_ipaddr((struct sockaddr *)&peeraddr, len);
	if (nodeid == 0) {
	    	printk("dlm: connect from non cluster node\n");
		sock_release(newsock);
		up_read(&con->sock_sem);
		return -1;
	}

	log_print("got connection from %d", nodeid);

	/*  Check to see if we already have a connection to this node. This
	 *  could happen if the two nodes initiate a connection at roughly
	 *  the same time and the connections cross on the wire.
	 * TEMPORARY FIX:
	 *  In this case we store the incoming one in "othersock"
	 */
	newcon = nodeid2con(nodeid);
	down_write(&newcon->sock_sem);
	if (newcon->sock) {
	        struct connection *othercon;

		othercon = kmalloc(sizeof(struct connection), GFP_KERNEL);
		if (!othercon) {
		        printk("dlm: failed to allocate incoming socket\n");
			up_write(&newcon->sock_sem);
			result = -ENOMEM;
			goto accept_err;
		}
		memset(othercon, 0, sizeof(*othercon));
		newcon->othersock = othercon;
		othercon->nodeid = nodeid;
		othercon->sock = newsock;
		othercon->rx_action = receive_from_sock;
		init_rwsem(&othercon->sock_sem);
		set_bit(CF_IS_OTHERSOCK, &othercon->flags);
		newsock->sk->sk_user_data = othercon;
		add_sock(newsock, othercon);
	}
	else {
		newsock->sk->sk_user_data = newcon;
		newcon->rx_action = receive_from_sock;
		add_sock(newsock, newcon);

	}

	up_write(&newcon->sock_sem);

	/*
	 * Add it to the active queue in case we got data
	 * beween processing the accept adding the socket
	 * to the read_sockets list
	 */
	lowcomms_data_ready(newsock->sk, 0);
	up_read(&con->sock_sem);

	return 0;

      accept_err:
	up_read(&con->sock_sem);
	sock_release(newsock);

	if (result != -EAGAIN)
		printk("dlm: error accepting connection from node: %d\n", result);
	return result;
}

/* Connect a new socket to its peer */
static int connect_to_sock(struct connection *con)
{
	int result = -EHOSTUNREACH;
	struct sockaddr_in6 saddr;
	int addr_len;
	struct socket *sock;

	if (con->nodeid == 0) {
		log_print("attempt to connect sock 0 foiled");
		return 0;
	}

	down_write(&con->sock_sem);
	if (con->retries++ > MAX_CONNECT_RETRIES)
		goto out;

	// FIXME not sure this should happen, let alone like this.
	if (con->sock) {
		sock_release(con->sock);
		con->sock = NULL;
	}

	/* Create a socket to communicate with */
	result = sock_create_kern(local_addr.sin6_family, SOCK_STREAM, IPPROTO_TCP, &sock);
	if (result < 0)
		goto out_err;

	if (lowcomms_ipaddr_from_nodeid(con->nodeid, (struct sockaddr *)&saddr) < 0)
	        goto out_err;

	sock->sk->sk_user_data = con;
	con->rx_action = receive_from_sock;

	make_sockaddr(&saddr, dlm_config.tcp_port, &addr_len);

	add_sock(sock, con);
	result =
	    sock->ops->connect(sock, (struct sockaddr *) &saddr, addr_len,
			       O_NONBLOCK);
	if (result == -EINPROGRESS)
		result = 0;
	if (result != 0)
		goto out_err;

      out:
	up_write(&con->sock_sem);
	/*
	 * Returning an error here means we've given up trying to connect to
	 * a remote node, otherwise we return 0 and reschedule the connetion
	 * attempt
	 */
	return result;

      out_err:
	if (con->sock) {
		sock_release(con->sock);
		con->sock = NULL;
	}
	/*
	 * Some errors are fatal and this list might need adjusting. For other
	 * errors we try again until the max number of retries is reached.
	 */
	if (result != -EHOSTUNREACH && result != -ENETUNREACH &&
	    result != -ENETDOWN && result != EINVAL
	    && result != -EPROTONOSUPPORT) {
		lowcomms_connect_sock(con);
		result = 0;
	}
	goto out;
}

static struct socket *create_listen_sock(struct connection *con, char *addr, int addr_len)
{
        struct socket *sock = NULL;
	mm_segment_t fs;
	int result = 0;
	int one = 1;
	struct sockaddr_in6 *saddr = (struct sockaddr_in6 *)addr;

	/* Create a socket to communicate with */
	result = sock_create_kern(local_addr.sin6_family, SOCK_STREAM, IPPROTO_TCP, &sock);
	if (result < 0) {
		printk("dlm: Can't create listening comms socket\n");
		goto create_out;
	}

	fs = get_fs();
	set_fs(get_ds());
	result = sock_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one));
	set_fs(fs);
	if (result < 0) {
		printk("dlm: Failed to set SO_REUSEADDR on socket: result=%d\n",result);
	}
	sock->sk->sk_user_data = con;
	con->rx_action = accept_from_sock;
	con->sock = sock;

	/* Bind to our port */
	make_sockaddr(saddr, dlm_config.tcp_port, &addr_len);
	result = sock->ops->bind(sock, (struct sockaddr *) saddr, addr_len);
	if (result < 0) {
		printk("dlm: Can't bind to port %d\n", dlm_config.tcp_port);
		sock_release(sock);
		sock = NULL;
		goto create_out;
	}

	fs = get_fs();
	set_fs(get_ds());

	result = sock_setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char *)&one, sizeof(one));
	set_fs(fs);
	if (result < 0) {
		printk("dlm: Set keepalive failed: %d\n", result);
	}

	result = sock->ops->listen(sock, 5);
	if (result < 0) {
		printk("dlm: Can't listen on port %d\n", dlm_config.tcp_port);
		sock_release(sock);
		sock = NULL;
		goto create_out;
	}

      create_out:
	return sock;
}


/* Listen on all interfaces */
static int listen_for_all(void)
{
	int result = 0;
	int nodeid;
	struct socket *sock = NULL;
	struct list_head *addr_list;
	struct connection *con = nodeid2con(0);
	struct connection *temp;
	struct cluster_node_addr *node_addr;
	char local_addr[sizeof(struct sockaddr_in6)];

	/* This will also fill in local_addr */
	nodeid = lowcomms_our_nodeid();

	addr_list = kcl_get_node_addresses(nodeid);
	if (!addr_list) {
	        printk("dlm: cannot initialise comms layer\n");
		result = -ENOTCONN;
		goto create_out;
	}

	list_for_each_entry(node_addr, addr_list, list) {

		if (!con) {
			con = kmalloc(sizeof(struct connection), GFP_KERNEL);
			if (!con) {
				printk("dlm: failed to allocate listen socket\n");
				result = -ENOMEM;
				goto create_free;
			}
			memset(con, 0, sizeof(*con));
			init_rwsem(&con->sock_sem);
			spin_lock_init(&con->writequeue_lock);
			INIT_LIST_HEAD(&con->writequeue);
			set_bit(CF_IS_OTHERSOCK, &con->flags);
		}

		memcpy(local_addr, node_addr->addr, node_addr->addr_len);
	        sock = create_listen_sock(con, local_addr,
					  node_addr->addr_len);
		if (sock) {
			add_sock(sock, con);

			/* Keep a list of dynamically allocated listening sockets
			   so we can free them at shutdown */
			if (test_bit(CF_IS_OTHERSOCK, &con->flags)) {
				list_add_tail(&con->listenlist, &listen_sockets);
			}
		}
		else {
			result = -EADDRINUSE;
			kfree(con);
			goto create_free;
		}

		con = NULL;
	}

      create_out:
	return result;

      create_free:
	/* Free up any dynamically allocated listening sockets */
	list_for_each_entry_safe(con, temp, &listen_sockets, listenlist) {
		sock_release(con->sock);
		kfree(con);
	}
	return result;
}



static struct writequeue_entry *new_writequeue_entry(struct connection *con,
						     int allocation)
{
	struct writequeue_entry *entry;

	entry = kmalloc(sizeof(struct writequeue_entry), allocation);
	if (!entry)
		return NULL;

	entry->page = alloc_page(allocation);
	if (!entry->page) {
		kfree(entry);
		return NULL;
	}

	entry->offset = 0;
	entry->len = 0;
	entry->end = 0;
	entry->users = 0;
	entry->con = con;

	return entry;
}

struct writequeue_entry *lowcomms_get_buffer(int nodeid, int len,
					     int allocation, char **ppc)
{
	struct connection *con = nodeid2con(nodeid);
	struct writequeue_entry *e;
	int offset = 0;
	int users = 0;

	if (!atomic_read(&accepting))
		return NULL;

	spin_lock(&con->writequeue_lock);
	e = list_entry(con->writequeue.prev, struct writequeue_entry, list);
	if (((struct list_head *) e == &con->writequeue) ||
	    (PAGE_CACHE_SIZE - e->end < len)) {
		e = NULL;
	} else {
		offset = e->end;
		e->end += len;
		users = e->users++;
	}
	spin_unlock(&con->writequeue_lock);

	if (e) {
	      got_one:
		if (users == 0)
			kmap(e->page);
		*ppc = page_address(e->page) + offset;
		return e;
	}

	e = new_writequeue_entry(con, allocation);
	if (e) {
		spin_lock(&con->writequeue_lock);
		offset = e->end;
		e->end += len;
		users = e->users++;
		list_add_tail(&e->list, &con->writequeue);
		spin_unlock(&con->writequeue_lock);
		atomic_inc(&writequeue_length);
		goto got_one;
	}
	return NULL;
}

void lowcomms_commit_buffer(struct writequeue_entry *e)
{
	struct connection *con = e->con;
	int users;

	if (!atomic_read(&accepting))
		return;

	spin_lock(&con->writequeue_lock);
	users = --e->users;
	if (users)
		goto out;
	e->len = e->end - e->offset;
	kunmap(e->page);
	spin_unlock(&con->writequeue_lock);

	if (test_and_set_bit(CF_WRITE_PENDING, &con->flags) == 0) {
		spin_lock_bh(&write_sockets_lock);
		list_add_tail(&con->write_list, &write_sockets);
		spin_unlock_bh(&write_sockets_lock);

		wake_up_interruptible(&lowcomms_send_waitq);
	}
	return;

      out:
	spin_unlock(&con->writequeue_lock);
	return;
}

static void free_entry(struct writequeue_entry *e)
{
	__free_page(e->page);
	kfree(e);
	atomic_dec(&writequeue_length);
}

/* Send a message */
static int send_to_sock(struct connection *con)
{
	int ret = 0;
	ssize_t(*sendpage) (struct socket *, struct page *, int, size_t, int);
	const int msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL;
	struct writequeue_entry *e;
	int len, offset;

	down_read(&con->sock_sem);
	if (con->sock == NULL)
		goto out_connect;

	sendpage = con->sock->ops->sendpage;

	spin_lock(&con->writequeue_lock);
	for (;;) {
		e = list_entry(con->writequeue.next, struct writequeue_entry,
			       list);
		if ((struct list_head *) e == &con->writequeue)
			break;

		len = e->len;
		offset = e->offset;
		BUG_ON(len == 0 && e->users == 0);
		spin_unlock(&con->writequeue_lock);

		ret = 0;
		if (len) {
			ret = sendpage(con->sock, e->page, offset, len,
				       msg_flags);
			if (ret == -EAGAIN || ret == 0)
				goto out;
			if (ret <= 0)
				goto send_error;
		}

		spin_lock(&con->writequeue_lock);
		e->offset += ret;
		e->len -= ret;

		if (e->len == 0 && e->users == 0) {
			list_del(&e->list);
			free_entry(e);
			continue;
		}
	}
	spin_unlock(&con->writequeue_lock);
      out:
	up_read(&con->sock_sem);
	return ret;

      send_error:
	up_read(&con->sock_sem);
	close_connection(con);
	lowcomms_connect_sock(con);
	return ret;

      out_connect:
	up_read(&con->sock_sem);
	lowcomms_connect_sock(con);
	return 0;
}

/* Called from recoverd when it knows that a node has
   left the cluster */
int lowcomms_close(int nodeid)
{
	struct connection *con;

	if (!connections)
		goto out;

	con = nodeid2con(nodeid);
	if (con->sock) {
		close_connection(con);
		return 0;
	}

      out:
	return -1;
}

/* API send message call, may queue the request */
/* N.B. This is the old interface - use the new one for new calls */
int lowcomms_send_message(int nodeid, char *buf, int len, int allocation)
{
	struct writequeue_entry *e;
	char *b;

	DLM_ASSERT(nodeid < dlm_config.max_connections,
		    printk("nodeid=%u\n", nodeid););

	e = lowcomms_get_buffer(nodeid, len, allocation, &b);
	if (e) {
		memcpy(b, buf, len);
		lowcomms_commit_buffer(e);
		return 0;
	}
	return -ENOBUFS;
}

/* Look for activity on active sockets */
static void process_sockets(void)
{
	struct list_head *list;
	struct list_head *temp;

	spin_lock_bh(&read_sockets_lock);
	list_for_each_safe(list, temp, &read_sockets) {
		struct connection *con =
		    list_entry(list, struct connection, read_list);
		list_del(&con->read_list);
		clear_bit(CF_READ_PENDING, &con->flags);

		spin_unlock_bh(&read_sockets_lock);

		/* This can reach zero if we a reprocessing requests
		 * as they come in.
		 */
		if (atomic_read(&con->waiting_requests) == 0) {
			spin_lock_bh(&read_sockets_lock);
			continue;
		}

		do {
			con->rx_action(con);
		} while (!atomic_dec_and_test(&con->waiting_requests));

		/* Don't starve out everyone else */
		schedule();
		spin_lock_bh(&read_sockets_lock);
	}
	spin_unlock_bh(&read_sockets_lock);
}

/* Try to send any messages that are pending
 */
static void process_output_queue(void)
{
	struct list_head *list;
	struct list_head *temp;
	int ret;

	spin_lock_bh(&write_sockets_lock);
	list_for_each_safe(list, temp, &write_sockets) {
		struct connection *con =
		    list_entry(list, struct connection, write_list);
		list_del(&con->write_list);
		clear_bit(CF_WRITE_PENDING, &con->flags);

		spin_unlock_bh(&write_sockets_lock);

		ret = send_to_sock(con);
		if (ret < 0) {
		}
		spin_lock_bh(&write_sockets_lock);
	}
	spin_unlock_bh(&write_sockets_lock);
}

static void process_state_queue(void)
{
	struct list_head *list;
	struct list_head *temp;
	int ret;

	spin_lock_bh(&state_sockets_lock);
	list_for_each_safe(list, temp, &state_sockets) {
		struct connection *con =
		    list_entry(list, struct connection, state_list);
		list_del(&con->state_list);
		clear_bit(CF_CONNECT_PENDING, &con->flags);
		spin_unlock_bh(&state_sockets_lock);

		ret = connect_to_sock(con);
		if (ret < 0) {
		}
		spin_lock_bh(&state_sockets_lock);
	}
	spin_unlock_bh(&state_sockets_lock);
}

/* Discard all entries on the write queues */
static void clean_writequeues(void)
{
	struct list_head *list;
	struct list_head *temp;
	int nodeid;

	for (nodeid = 1; nodeid < dlm_config.max_connections; nodeid++) {
		struct connection *con = nodeid2con(nodeid);

		spin_lock(&con->writequeue_lock);
		list_for_each_safe(list, temp, &con->writequeue) {
			struct writequeue_entry *e =
			    list_entry(list, struct writequeue_entry, list);
			list_del(&e->list);
			free_entry(e);
		}
		spin_unlock(&con->writequeue_lock);
	}
}

static int read_list_empty(void)
{
	int status;

	spin_lock_bh(&read_sockets_lock);
	status = list_empty(&read_sockets);
	spin_unlock_bh(&read_sockets_lock);

	return status;
}

/* DLM Transport comms receive daemon */
static int dlm_recvd(void *data)
{
	daemonize("dlm_recvd");
	atomic_set(&recv_run, 1);

	init_waitqueue_head(&lowcomms_recv_waitq);
	init_waitqueue_entry(&lowcomms_recv_waitq_head, current);
	add_wait_queue(&lowcomms_recv_waitq, &lowcomms_recv_waitq_head);

	complete(&thread_completion);

	while (atomic_read(&recv_run)) {

		set_task_state(current, TASK_INTERRUPTIBLE);

		if (read_list_empty())
			schedule();

		set_task_state(current, TASK_RUNNING);

		process_sockets();
	}

	down(&thread_lock);
	up(&thread_lock);

	complete(&thread_completion);

	return 0;
}

static int write_and_state_lists_empty(void)
{
	int status;

	spin_lock_bh(&write_sockets_lock);
	status = list_empty(&write_sockets);
	spin_unlock_bh(&write_sockets_lock);

	spin_lock_bh(&state_sockets_lock);
	if (list_empty(&state_sockets) == 0)
		status = 0;
	spin_unlock_bh(&state_sockets_lock);

	return status;
}

/* DLM Transport send daemon */
static int dlm_sendd(void *data)
{
	daemonize("dlm_sendd");
	atomic_set(&send_run, 1);

	init_waitqueue_head(&lowcomms_send_waitq);
	init_waitqueue_entry(&lowcomms_send_waitq_head, current);
	add_wait_queue(&lowcomms_send_waitq, &lowcomms_send_waitq_head);

	complete(&thread_completion);

	while (atomic_read(&send_run)) {

		set_task_state(current, TASK_INTERRUPTIBLE);

		if (write_and_state_lists_empty())
			schedule();

		set_task_state(current, TASK_RUNNING);

		process_state_queue();
		process_output_queue();
	}

	down(&thread_lock);
	up(&thread_lock);

	complete(&thread_completion);

	return 0;
}

static void daemons_stop(void)
{
	if (atomic_read(&recv_run)) {
		down(&thread_lock);
		atomic_set(&recv_run, 0);
		wake_up_interruptible(&lowcomms_recv_waitq);
		up(&thread_lock);
		wait_for_completion(&thread_completion);
	}

	if (atomic_read(&send_run)) {
		down(&thread_lock);
		atomic_set(&send_run, 0);
		wake_up_interruptible(&lowcomms_send_waitq);
		up(&thread_lock);
		wait_for_completion(&thread_completion);
	}
}

static int daemons_start(void)
{
	int error;

	error = kernel_thread(dlm_recvd, NULL, 0);
	if (error < 0) {
		log_print("can't start recvd thread: %d", error);
		goto out;
	}
	wait_for_completion(&thread_completion);

	error = kernel_thread(dlm_sendd, NULL, 0);
	if (error < 0) {
		log_print("can't start sendd thread: %d", error);
		daemons_stop();
		goto out;
	}
	wait_for_completion(&thread_completion);

	error = 0;
 out:
	return error;
}

/*
 * Return the largest buffer size we can cope with.
 */
int lowcomms_max_buffer_size(void)
{
	return PAGE_CACHE_SIZE;
}

void lowcomms_stop(void)
{
	int i;
	struct connection *temp;
	struct connection *lcon;

	atomic_set(&accepting, 0);

	/* Set all the activity flags to prevent any
	   socket activity.
	*/
	for (i = 0; i < conn_array_size; i++) {
	        connections[i].flags = 0x7;
	}
	daemons_stop();
	clean_writequeues();

	for (i = 0; i < conn_array_size; i++) {
		close_connection(nodeid2con(i));
	}

	kfree(connections);
	connections = NULL;

	/* Free up any dynamically allocated listening sockets */
	list_for_each_entry_safe(lcon, temp, &listen_sockets, listenlist) {
		sock_release(lcon->sock);
		kfree(lcon);
	}

	kcl_releaseref_cluster();
}

/* This is quite likely to sleep... */
int lowcomms_start(void)
{
	int error = 0;
	int i;

	INIT_LIST_HEAD(&read_sockets);
	INIT_LIST_HEAD(&write_sockets);
	INIT_LIST_HEAD(&state_sockets);
	INIT_LIST_HEAD(&listen_sockets);

	spin_lock_init(&read_sockets_lock);
	spin_lock_init(&write_sockets_lock);
	spin_lock_init(&state_sockets_lock);

	init_completion(&thread_completion);
	init_MUTEX(&thread_lock);
	atomic_set(&send_run, 0);
	atomic_set(&recv_run, 0);

	error = -ENOTCONN;
	if (kcl_addref_cluster())
		goto out;

	/*
	 * Temporarily initialise the waitq head so that lowcomms_send_message
	 * doesn't crash if it gets called before the thread is fully
	 * initialised
	 */
	init_waitqueue_head(&lowcomms_send_waitq);

	error = -ENOMEM;

	connections = kmalloc(sizeof(struct connection) *
			      dlm_config.max_connections, GFP_KERNEL);
	if (!connections)
		goto out;

	memset(connections, 0,
	       sizeof(struct connection) * dlm_config.max_connections);
	for (i = 0; i < dlm_config.max_connections; i++) {
		connections[i].nodeid = i;
		init_rwsem(&connections[i].sock_sem);
		INIT_LIST_HEAD(&connections[i].writequeue);
		spin_lock_init(&connections[i].writequeue_lock);
	}
	conn_array_size = dlm_config.max_connections;

	/* Start listening */
	error = listen_for_all();
	if (error)
		goto fail_free_conn;

	error = daemons_start();
	if (error)
		goto fail_free_conn;

	atomic_set(&accepting, 1);

	return 0;

      fail_free_conn:
	kfree(connections);

      out:
	return error;
}

/* Don't accept any more outgoing work */
void lowcomms_stop_accept()
{
        atomic_set(&accepting, 0);
}

/* Cluster Manager interface functions for looking up
   nodeids and IP addresses by each other
*/

/* Return the IP address of a node given its NODEID */
static int lowcomms_ipaddr_from_nodeid(int nodeid, struct sockaddr *retaddr)
{
	struct list_head *addrs;
	struct cluster_node_addr *node_addr;
	struct cluster_node_addr *current_addr = NULL;
	struct sockaddr_in6 *saddr;
	int interface;
	int i;

	addrs = kcl_get_node_addresses(nodeid);
	if (!addrs)
		return -1;

	interface = kcl_get_current_interface();

	/* Look for address number <interface> */
	i=0; /* i/f numbers start at 1 */
	list_for_each_entry(node_addr, addrs, list) {
	        if (interface == ++i) {
		        current_addr = node_addr;
			break;
		}
	}

	/* If that failed then just use the first one */
	if (!current_addr)
 	        current_addr = (struct cluster_node_addr *)addrs->next;

	saddr = (struct sockaddr_in6 *)current_addr->addr;

	/* Extract the IP address */
	if (saddr->sin6_family == AF_INET) {
	        struct sockaddr_in *in4  = (struct sockaddr_in *)saddr;
		struct sockaddr_in *ret4 = (struct sockaddr_in *)retaddr;
		ret4->sin_addr.s_addr = in4->sin_addr.s_addr;
	}
	else {
	        struct sockaddr_in6 *ret6 = (struct sockaddr_in6 *)retaddr;
		memcpy(&ret6->sin6_addr, &saddr->sin6_addr, sizeof(saddr->sin6_addr));
	}

	return 0;
}

/* Return the NODEID for a node given its sockaddr */
static int lowcomms_nodeid_from_ipaddr(struct sockaddr *addr, int addr_len)
{
	struct kcl_cluster_node node;
	struct sockaddr_in6 ipv6_addr;
	struct sockaddr_in  ipv4_addr;

	if (addr->sa_family == AF_INET) {
	        struct sockaddr_in *in4 = (struct sockaddr_in *)addr;
		memcpy(&ipv4_addr, &local_addr, addr_len);
		memcpy(&ipv4_addr.sin_addr, &in4->sin_addr, sizeof(ipv4_addr.sin_addr));

		addr = (struct sockaddr *)&ipv4_addr;
	}
	else {
	        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)addr;
		memcpy(&ipv6_addr, &local_addr, addr_len);
		memcpy(&ipv6_addr.sin6_addr, &in6->sin6_addr, sizeof(ipv6_addr.sin6_addr));

		addr = (struct sockaddr *)&ipv6_addr;
	}

	if (kcl_get_node_by_addr((char *)addr, addr_len, &node) == 0)
		return node.node_id;
	else
		return 0;
}

int lowcomms_our_nodeid(void)
{
	struct kcl_cluster_node node;
	struct list_head *addrs;
	struct cluster_node_addr *first_addr;
	static int our_nodeid = 0;

	if (our_nodeid)
		return our_nodeid;

	if (kcl_get_node_by_nodeid(0, &node) == -1)
		return 0;

	our_nodeid = node.node_id;

	/* Fill in the "template" structure */
	addrs = kcl_get_node_addresses(our_nodeid);
	if (!addrs)
		return 0;

	first_addr = (struct cluster_node_addr *) addrs->next;
	memcpy(&local_addr, &first_addr->addr, first_addr->addr_len);

	return node.node_id;
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
