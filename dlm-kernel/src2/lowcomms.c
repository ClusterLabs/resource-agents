/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
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
 * short, efficient and never (well, hardly ever) waits.
 *
 */


#include <asm/ioctls.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/sctp/user.h>
#include <linux/pagemap.h>
#include <linux/socket.h>
#include <linux/idr.h>

#include "dlm_internal.h"
#include "lowcomms.h"
#include "midcomms.h"
#include "config.h"
#include "member.h"

/* One of these per connected node */
struct nodeinfo
{
	spinlock_t   lock;
	sctp_assoc_t assoc_id;
	unsigned long  flags;
	struct list_head write_list;    /* on list of nodes with pending writes */
	struct list_head writequeue;	/* List of outgoing writequeue_entries */
	spinlock_t   writequeue_lock;
	int          nodeid;
#define NI_INIT_PENDING 1
#define NI_WRITE_PENDING 2
};

static DEFINE_IDR(nodeinfo_idr);
static struct rw_semaphore nodeinfo_lock;
static int max_nodeid;

struct cbuf {
        unsigned base;
        unsigned len;
        unsigned mask;
};

/* Just the one of these, now. But this struct keeps
   the connection-specific variables together */
struct connection {
	struct socket *sock;
	unsigned long flags;
#define CF_READ_PENDING 1
	struct page *rx_page;
	atomic_t waiting_requests;
	struct cbuf cb;
};

/* An entry waiting to be sent */
struct writequeue_entry {
	struct list_head list;
	struct page *page;
	int offset;
	int len;
	int end;
	int users;
	struct nodeinfo *ni;
};

#define CBUF_INIT(cb, size) do { (cb)->base = (cb)->len = 0; (cb)->mask = ((size)-1); } while(0)
#define CBUF_ADD(cb, n) do { (cb)->len += n; } while(0)
#define CBUF_EMPTY(cb) ((cb)->len == 0)
#define CBUF_MAY_ADD(cb, n) (((cb)->len + (n)) < ((cb)->mask + 1))
#define CBUF_EAT(cb, n) do { (cb)->len  -= (n); \
                             (cb)->base += (n); (cb)->base &= (cb)->mask; } while(0)
#define CBUF_DATA(cb) (((cb)->base + (cb)->len) & (cb)->mask)

/* List of nodes which have writes pending */
static struct list_head write_nodes;
static spinlock_t write_nodes_lock;

/* Maximum number of incoming messages to process before
 * doing a schedule()
 */
#define MAX_RX_MSG_COUNT 25

/* "Template" structure for IPv4 and IPv6 used to fill
 * in the missing bits when converting between cman (which knows
 * nothing about sockaddr structs) and real life where we actually
 * have to connect to these addresses. Also one of these structs
 * will hold the cached "us" address.
 *
 */
static struct sockaddr_storage local_addr;

/* Manage daemons */
static struct task_struct *recv_task;
static struct task_struct *send_task;
static atomic_t accepting;

static wait_queue_t lowcomms_send_waitq_head;
static wait_queue_head_t lowcomms_send_waitq;
static wait_queue_t lowcomms_recv_waitq_head;
static wait_queue_head_t lowcomms_recv_waitq;

/* The SCTP connection */
static struct connection sctp_con;

static int lowcomms_ipaddr_from_nodeid(int nodeid, struct sockaddr *retaddr);
static struct nodeinfo *nodeid2nodeinfo(int nodeid, int alloc)
{
	struct nodeinfo *ni;
	int r;
	int n;

	down_read(&nodeinfo_lock);
	ni = idr_find(&nodeinfo_idr, nodeid);
	up_read(&nodeinfo_lock);

	if (!ni && alloc) {
		down_write(&nodeinfo_lock);

		ni = idr_find(&nodeinfo_idr, nodeid);
		if (ni)
			goto out_up;

		r = idr_pre_get(&nodeinfo_idr, alloc);
		if (!r)
			goto out_up;

		ni = kmalloc(sizeof(struct nodeinfo), alloc);
		if (!ni)
			goto out_up;

		r = idr_get_new_above(&nodeinfo_idr, ni, nodeid, &n);
		if (r) {
			kfree(ni);
			ni = NULL;
			goto out_up;
		}
		if (n != nodeid) {
			idr_remove(&nodeinfo_idr, n);
			kfree(ni);
			ni = NULL;
			goto out_up;
		}
		memset(ni, 0, sizeof(struct nodeinfo));
		spin_lock_init(&ni->lock);
		INIT_LIST_HEAD(&ni->writequeue);
		spin_lock_init(&ni->writequeue_lock);
		ni->nodeid = nodeid;

		if (nodeid > max_nodeid)
			max_nodeid = nodeid;
	out_up:
		up_write(&nodeinfo_lock);
	}

	return ni;
}

/* Don't call this too often... */
static struct nodeinfo *assoc2nodeinfo(sctp_assoc_t assoc)
{
	int i;
	struct nodeinfo *ni;

	for (i=1; i<=max_nodeid; i++) {
		ni = nodeid2nodeinfo(i, 0);
		if (ni && ni->assoc_id == assoc)
			return ni;
	}
	return NULL;
}


/* Data or notification available on socket */
static void lowcomms_data_ready(struct sock *sk, int count_unused)
{
	atomic_inc(&sctp_con.waiting_requests);
	if (test_and_set_bit(CF_READ_PENDING, &sctp_con.flags))
		return;

	wake_up_interruptible(&lowcomms_recv_waitq);
}

static void lowcomms_write_space(struct sock *sk)
{
	wake_up_interruptible(&lowcomms_send_waitq);
}


/* Add the port number to an IP6 or 4 sockaddr and return the address
   length */
static void make_sockaddr(struct sockaddr_storage *saddr, uint16_t port,
			  int *addr_len)
{
	/* If port is 0 then use the CMAN port */
	if (!port) {
		if (local_addr.ss_family == AF_INET) {
			struct sockaddr_in *local4_addr = (struct sockaddr_in *)&local_addr;
			port = be16_to_cpu(local4_addr->sin_port);
		}
		else {
			struct sockaddr_in6 *local6_addr = (struct sockaddr_in6 *)&local_addr;
			port = be16_to_cpu(local6_addr->sin6_port);
		}
	}

        saddr->ss_family = local_addr.ss_family;
        if (local_addr.ss_family == AF_INET) {
		struct sockaddr_in *in4_addr = (struct sockaddr_in *)saddr;
		in4_addr->sin_port = cpu_to_be16(port);
		memset(&in4_addr->sin_zero, 0, sizeof(in4_addr->sin_zero));
		*addr_len = sizeof(struct sockaddr_in);
	}
	else {
		struct sockaddr_in6 *in6_addr = (struct sockaddr_in6 *)saddr;
		in6_addr->sin6_port = cpu_to_be16(port);
		*addr_len = sizeof(struct sockaddr_in6);
	}
}

/* Close the connection and tidy up */
static void close_connection(void)
{
	if (sctp_con.sock) {
		sock_release(sctp_con.sock);
		sctp_con.sock = NULL;
	}

	if (sctp_con.rx_page) {
		__free_page(sctp_con.rx_page);
		sctp_con.rx_page = NULL;
	}
}

/* We only send shutdown messages to nodes that are not part of the cluster */
static void send_shutdown(sctp_assoc_t associd)
{
	static char outcmsg[CMSG_SPACE(sizeof(struct sctp_sndrcvinfo))];
        struct msghdr outmessage;
        struct cmsghdr *cmsg;
        struct sctp_sndrcvinfo *sinfo;
	int ret;

	outmessage.msg_name = NULL;
        outmessage.msg_namelen = 0;
        outmessage.msg_control = outcmsg;
        outmessage.msg_controllen = sizeof(outcmsg);
        outmessage.msg_flags = MSG_EOR;

        cmsg = CMSG_FIRSTHDR(&outmessage);
        cmsg->cmsg_level = IPPROTO_SCTP;
        cmsg->cmsg_type = SCTP_SNDRCV;
        cmsg->cmsg_len = CMSG_LEN(sizeof(struct sctp_sndrcvinfo));
        outmessage.msg_controllen = cmsg->cmsg_len;
        sinfo = (struct sctp_sndrcvinfo *)CMSG_DATA(cmsg);
        memset(sinfo, 0x00, sizeof(struct sctp_sndrcvinfo));

        sinfo->sinfo_flags |= MSG_EOF;
        sinfo->sinfo_assoc_id = associd;

	ret = kernel_sendmsg(sctp_con.sock, &outmessage, NULL, 0, 0);

	if (ret != 0) {
		printk(KERN_WARNING "dlm: send EOF to node failed: %d\n", ret);
	}
}


/* INIT failed but we don't know which node...
   restart INIT on all pending nodes */
static void init_failed(void)
{
	int i;
	struct nodeinfo *ni;

	for (i=1; i<=max_nodeid; i++) {
		ni = nodeid2nodeinfo(i, 0);
		if (!ni)
			continue;

		if (test_and_clear_bit(NI_INIT_PENDING, &ni->flags)) {
			ni->assoc_id = 0;
			if (!test_and_set_bit(NI_WRITE_PENDING, &ni->flags)) {
				spin_lock_bh(&write_nodes_lock);
				list_add_tail(&ni->write_list, &write_nodes);
				spin_unlock_bh(&write_nodes_lock);
			}
		}
	}
	wake_up_interruptible(&lowcomms_send_waitq);
}

/* Something happened to an association */
static void process_sctp_notification(struct msghdr *msg, char *buf)
{
	union sctp_notification *sn = (union sctp_notification *)buf;

	if (sn->sn_header.sn_type == SCTP_ASSOC_CHANGE) {
		switch (sn->sn_assoc_change.sac_state) {

		case SCTP_COMM_UP:
		case SCTP_RESTART:
		{
			/* Check that the new node is in the same cluster as us */
			struct sctp_prim prim;
			mm_segment_t fs;
			int nodeid;
			int prim_len, ret;
			int addr_len;
			struct nodeinfo *ni;

			/* This seems to happen when we received a connection too early... or something...
			   anyway, it happens but we always seem to get a real message too, see receive_from_sock
			*/
			if ((int)sn->sn_assoc_change.sac_assoc_id <= 0) {
				printk(KERN_WARNING "dlm: got COMM_UP for invalid assoc ID %d\n",
				       (int)sn->sn_assoc_change.sac_assoc_id);
				init_failed();
				return;
			}
			memset(&prim, 0, sizeof(struct sctp_prim));
			prim_len = sizeof(struct sctp_prim);
			prim.ssp_assoc_id = sn->sn_assoc_change.sac_assoc_id;


			fs = get_fs();
			set_fs(get_ds());
			ret = sctp_con.sock->ops->getsockopt(sctp_con.sock, IPPROTO_SCTP, SCTP_PRIMARY_ADDR,
							     (char*)&prim, &prim_len);
			set_fs(fs);
			if (ret < 0) {
				struct nodeinfo *ni;

				printk(KERN_ERR "dlm: getsockopt/sctp_primary_addr on new assoc %d failed : %d\n",
				       (int)sn->sn_assoc_change.sac_assoc_id, ret);

				/* Retry INIT later */
				ni = assoc2nodeinfo(sn->sn_assoc_change.sac_assoc_id);
				if (ni)
					clear_bit(NI_INIT_PENDING, &ni->flags);
				return;
			}
			make_sockaddr(&prim.ssp_addr, 0, &addr_len);
			if (dlm_addr_nodeid((char *)&prim.ssp_addr, &nodeid)) {

				printk(KERN_WARNING "dlm: got connection from non-cluster node, rejecting\n");
				send_shutdown(prim.ssp_assoc_id);
				return;
			}

			ni = nodeid2nodeinfo(nodeid, GFP_KERNEL);
			if (!ni)
				return;

			/* Save the assoc ID */
			spin_lock(&ni->lock);
			ni->assoc_id = sn->sn_assoc_change.sac_assoc_id;
			spin_unlock(&ni->lock);

			printk(KERN_DEBUG "dlm: got new/restarted association %d for nodeid %d\n",
			       (int)sn->sn_assoc_change.sac_assoc_id, nodeid);

			/* Send any pending writes */
			clear_bit(NI_INIT_PENDING, &ni->flags);
			if (!test_and_set_bit(NI_WRITE_PENDING, &ni->flags)) {
				spin_lock_bh(&write_nodes_lock);
				list_add_tail(&ni->write_list, &write_nodes);
				spin_unlock_bh(&write_nodes_lock);
			}
			wake_up_interruptible(&lowcomms_send_waitq);
		}
		break;

		case SCTP_COMM_LOST:
		case SCTP_SHUTDOWN_COMP:
		{
			struct nodeinfo *ni;

			ni = assoc2nodeinfo(sn->sn_assoc_change.sac_assoc_id);
			if (ni) {
				spin_lock(&ni->lock);
				ni->assoc_id = 0;
				spin_unlock(&ni->lock);
			}
		}
		break;

		/* We don't know which INIT failed, so clear the PENDING flags on them all.
		   if assoc_id is zero then it will then try again */
		case SCTP_CANT_STR_ASSOC:
		{
			printk(KERN_WARNING "dlm: Can't start SCTP association - retrying\n");
			init_failed();
		}
		break;

		default:
			printk(KERN_DEBUG "dlm: got unexpected SCTP assoc change, id=%d, state=%d\n",
			       (int)sn->sn_assoc_change.sac_assoc_id, sn->sn_assoc_change.sac_state);

		}
	}
}

/* Data received from remote end */
static int receive_from_sock(void)
{
	int ret = 0;
	struct msghdr msg;
	struct kvec iov[2];
	unsigned len;
	int r;
        struct sctp_sndrcvinfo *sinfo;
	struct cmsghdr *cmsg;
	struct nodeinfo *ni;
	/* These two are marginally too big for stack allocation, but this function
	 * is (currently) only called by dlm_recvd so static should be OK.
	 */
	static struct sockaddr_storage msgname;
	static char incmsg[CMSG_SPACE(sizeof(struct sctp_sndrcvinfo))];

	if (sctp_con.sock == NULL)
		goto out;

	if (sctp_con.rx_page == NULL) {
		/*
		 * This doesn't need to be atomic, but I think it should
		 * improve performance if it is.
		 */
		sctp_con.rx_page = alloc_page(GFP_ATOMIC);
		if (sctp_con.rx_page == NULL)
			goto out_resched;
		CBUF_INIT(&sctp_con.cb, PAGE_CACHE_SIZE);
	}

	memset(&incmsg, 0, sizeof(incmsg));
	memset(&msgname, 0, sizeof(msgname));

	memset(incmsg, 0, sizeof(incmsg));
	msg.msg_name = &msgname;
	msg.msg_namelen = sizeof(msgname);
	msg.msg_flags = 0;
	msg.msg_control = incmsg;
	msg.msg_controllen = sizeof(incmsg);

	/* I don't see why this circular buffer stuff is necessary for SCTP which
	 * is a packet-based protocol, but the whole thing breaks under load
	 * without it! The overhead is minimal (and is in the TCP lowcomms
	 * anyway, of course) so I'll leave it in until I can figure out
	 * what's really happening.
	 */

        /*
         * iov[0] is the bit of the circular buffer between the current end
         * point (cb.base + cb.len) and the end of the buffer.
         */
        iov[0].iov_len = sctp_con.cb.base - CBUF_DATA(&sctp_con.cb);
        iov[0].iov_base = page_address(sctp_con.rx_page) + CBUF_DATA(&sctp_con.cb);
        iov[1].iov_len = 0;

        /*
         * iov[1] is the bit of the circular buffer between the start of the
         * buffer and the start of the currently used section (cb.base)
         */
        if (CBUF_DATA(&sctp_con.cb) >= sctp_con.cb.base) {
                iov[0].iov_len = PAGE_CACHE_SIZE - CBUF_DATA(&sctp_con.cb);
                iov[1].iov_len = sctp_con.cb.base;
                iov[1].iov_base = page_address(sctp_con.rx_page);
                msg.msg_iovlen = 2;
        }
        len = iov[0].iov_len + iov[1].iov_len;

	r = ret = kernel_recvmsg(sctp_con.sock, &msg, iov, 1, len, MSG_NOSIGNAL | MSG_DONTWAIT);
	if (ret <= 0)
		goto out_close;

	msg.msg_control = incmsg;
	msg.msg_controllen = sizeof(incmsg);
	cmsg = CMSG_FIRSTHDR(&msg);
	sinfo = (struct sctp_sndrcvinfo *)CMSG_DATA(cmsg);

	if (msg.msg_flags & MSG_NOTIFICATION) {
		process_sctp_notification(&msg, page_address(sctp_con.rx_page));
		return 0;
	}

	/* Is this a new association ? */
	ni = nodeid2nodeinfo(le32_to_cpu(sinfo->sinfo_ppid), GFP_KERNEL);
	if (ni) {
		ni->assoc_id = sinfo->sinfo_assoc_id;
		if (test_and_clear_bit(NI_INIT_PENDING, &ni->flags)) {

			if (!test_and_set_bit(NI_WRITE_PENDING, &ni->flags)) {
				spin_lock_bh(&write_nodes_lock);
				list_add_tail(&ni->write_list, &write_nodes);
				spin_unlock_bh(&write_nodes_lock);
			}
			wake_up_interruptible(&lowcomms_send_waitq);
		}
	}

	/* INIT sends a message with length of 1 - ignore it */
	if (r == 1)
		return 0;

	CBUF_ADD(&sctp_con.cb, ret);
	ret = midcomms_process_incoming_buffer(cpu_to_le32(sinfo->sinfo_ppid),
                                               page_address(sctp_con.rx_page),
                                               sctp_con.cb.base, sctp_con.cb.len,
                                               PAGE_CACHE_SIZE);

	if (ret == -EBADMSG) {
		printk(KERN_INFO "dlm: lowcomms: addr=%p, len=%u, "
		       "iov_len=%u, iov_base[0]=%p, read=%d\n",
		       page_address(sctp_con.rx_page), r,
		       len, iov[0].iov_base, r);
	}
	if (ret < 0)
		goto out_close;
	CBUF_EAT(&sctp_con.cb, ret);

      out:
	ret = 0;
	goto out_ret;

      out_resched:
	lowcomms_data_ready(sctp_con.sock->sk, 0);
	ret = 0;
	schedule();
	goto out_ret;

      out_close:
	// TODO: What??
	if (ret != -EAGAIN) {
		printk(KERN_INFO "dlm: Error reading from sctp socket: %d\n", ret);
	}
      out_ret:
	return ret;
}

/* Bind to an IP address. SCTP allows multiple address so it can do multi-homing */
static int add_bind_addr(struct sockaddr_storage *addr, int addr_len, int num)
{
	mm_segment_t fs;
	int result = 0;

	fs = get_fs();
	set_fs(get_ds());
	if (num == 1)
		result = sctp_con.sock->ops->bind(sctp_con.sock, (struct sockaddr *) addr, addr_len);
	else
		result = sctp_con.sock->ops->setsockopt(sctp_con.sock, SOL_SCTP, SCTP_SOCKOPT_BINDX_ADD,
							(char *)addr, addr_len);
	set_fs(fs);

	if (result < 0)
		printk(KERN_ERR "dlm: Can't bind to port %d, address number %d\n", dlm_config.tcp_port, num);

	return result;
}


/* Initialise SCTP socket and bind to all interfaces that cman knows about. */
static int init_sock(void)
{
	int result = 0;
	int nodeid;
	int num = 0;
	mm_segment_t fs;
	struct socket *sock = NULL;
	struct sockaddr_storage localaddr;
	struct sctp_event_subscribe subscribe;

	/* This will also fill in local_addr */
	nodeid = lowcomms_our_nodeid();

	/* Create a socket to communicate with */
	result = sock_create_kern(local_addr.ss_family, SOCK_SEQPACKET, IPPROTO_SCTP, &sock);
	if (result < 0) {
		printk(KERN_ERR "dlm: Can't create comms socket, check SCTP is loaded\n");
		goto create_out;
	}

	/* Listen for events */
	memset(&subscribe, 0, sizeof(subscribe));
	subscribe.sctp_data_io_event = 1;
	subscribe.sctp_association_event = 1;
	subscribe.sctp_send_failure_event = 1;
	subscribe.sctp_shutdown_event = 1;
        subscribe.sctp_partial_delivery_event = 1;

	fs = get_fs();
	set_fs(get_ds());
	result = sock->ops->setsockopt(sock, SOL_SCTP, SCTP_EVENTS, (char *)&subscribe, sizeof(subscribe));
	set_fs(fs);

	if (result < 0) {
		printk(KERN_ERR "dlm: Failed to set SCTP_EVENTS on socket: result=%d\n", result);
		goto create_delsock;
	}

        /* Init con struct */
	sock->sk->sk_user_data = &sctp_con;
	sctp_con.sock = sock;
	sctp_con.sock->sk->sk_data_ready = lowcomms_data_ready;
	sctp_con.sock->sk->sk_write_space = lowcomms_write_space;

        /* Bind to all interfaces. */
	do {
		int addr_len;

		result = dlm_our_addr(num, (char *)&localaddr);
		make_sockaddr(&localaddr, dlm_config.tcp_port, &addr_len);

		result = add_bind_addr(&localaddr, addr_len, num);
		if (result)
			goto create_delsock;
		++num;
	} while (!result);

	result = sock->ops->listen(sock, 5);
	if (result < 0) {
		printk(KERN_ERR "dlm: Can't set socket listening\n");
		goto create_delsock;
	}

	return 0;

 create_delsock:
	sock_release(sock);
	sctp_con.sock = NULL;

 create_out:
	return result;
}


static struct writequeue_entry *new_writequeue_entry(int allocation)
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

	return entry;
}

struct writequeue_entry *lowcomms_get_buffer(int nodeid, int len,
					     int allocation, char **ppc)
{
	struct writequeue_entry *e;
	int offset = 0;
	int users = 0;
	struct nodeinfo *ni;

	if (!atomic_read(&accepting))
		return NULL;

	ni = nodeid2nodeinfo(nodeid, allocation);
	if (!ni)
		return NULL;

	spin_lock(&ni->writequeue_lock);
	e = list_entry(ni->writequeue.prev, struct writequeue_entry, list);
	if (((struct list_head *) e == &ni->writequeue) ||
	    (PAGE_CACHE_SIZE - e->end < len)) {
		e = NULL;
	} else {
		offset = e->end;
		e->end += len;
		users = e->users++;
	}
	spin_unlock(&ni->writequeue_lock);

	if (e) {
	      got_one:
		if (users == 0)
			kmap(e->page);
		*ppc = page_address(e->page) + offset;
		return e;
	}

	e = new_writequeue_entry(allocation);
	if (e) {
		spin_lock(&ni->writequeue_lock);
		offset = e->end;
		e->end += len;
		e->ni = ni;
		users = e->users++;
		list_add_tail(&e->list, &ni->writequeue);
		spin_unlock(&ni->writequeue_lock);
		goto got_one;
	}
	return NULL;
}

void lowcomms_commit_buffer(struct writequeue_entry *e)
{
	int users;
	struct nodeinfo *ni = e->ni;

	if (!atomic_read(&accepting))
		return;

	spin_lock(&ni->writequeue_lock);
	users = --e->users;
	if (users)
		goto out;
	e->len = e->end - e->offset;
	kunmap(e->page);
	spin_unlock(&ni->writequeue_lock);

	if (!test_and_set_bit(NI_WRITE_PENDING, &ni->flags)) {
		spin_lock_bh(&write_nodes_lock);
		list_add_tail(&ni->write_list, &write_nodes);
		spin_unlock_bh(&write_nodes_lock);
		wake_up_interruptible(&lowcomms_send_waitq);
	}
	return;

      out:
	spin_unlock(&ni->writequeue_lock);
	return;
}

static void free_entry(struct writequeue_entry *e)
{
	__free_page(e->page);
	kfree(e);
}

/* Initiate an SCTP association. In theory we could just use sendmsg() on
   the first IP address and it should work, but this allows us to set up the
   association before sending any valuable data that we can't afford to lose.
   It also keeps the send path clean as it can now always use the association ID */
static void initiate_association(int nodeid)
{
	struct sockaddr_storage rem_addr;
	static char outcmsg[CMSG_SPACE(sizeof(struct sctp_sndrcvinfo))];
        struct msghdr outmessage;
        struct cmsghdr *cmsg;
        struct sctp_sndrcvinfo *sinfo;
	int ret;
	int addrlen;
	char buf[1];
	struct kvec iov[1];
	struct nodeinfo *ni;

	printk(KERN_DEBUG "dlm: Initiating association with node %d\n", nodeid);

	ni = nodeid2nodeinfo(nodeid, GFP_KERNEL);
	if (!ni)
		return;

	if (lowcomms_ipaddr_from_nodeid(nodeid, (struct sockaddr *)&rem_addr)) {
		printk(KERN_WARNING "dlm: CMAN has no node addresses for nodeid %d\n", nodeid);
		return;
	}

	make_sockaddr(&rem_addr, dlm_config.tcp_port, &addrlen);

	outmessage.msg_name = &rem_addr;
        outmessage.msg_namelen = addrlen;
        outmessage.msg_control = outcmsg;
        outmessage.msg_controllen = sizeof(outcmsg);
        outmessage.msg_flags = MSG_EOR;

	iov[0].iov_base = buf;
	iov[0].iov_len = 1;

	/* Real INIT messages seem to cause trouble. Just send a 1 byte message
	   we can afford to lose */
	cmsg = CMSG_FIRSTHDR(&outmessage);
	cmsg->cmsg_level = IPPROTO_SCTP;
	cmsg->cmsg_type = SCTP_SNDRCV;
	cmsg->cmsg_len = CMSG_LEN(sizeof(struct sctp_sndrcvinfo));
	sinfo = (struct sctp_sndrcvinfo *)CMSG_DATA(cmsg);
	memset(sinfo, 0x00, sizeof(struct sctp_sndrcvinfo));
	sinfo->sinfo_ppid = cpu_to_le32(lowcomms_our_nodeid());

	outmessage.msg_controllen = cmsg->cmsg_len;
	ret = kernel_sendmsg(sctp_con.sock, &outmessage, iov, 1, 1);
	if (ret < 0) {
		printk(KERN_WARNING "dlm: send INIT to node failed: %d\n", ret);

		/* Try again later */
		clear_bit(NI_INIT_PENDING, &ni->flags);
	}
}

/* Send a message */
static int send_to_sock(struct nodeinfo *ni)
{
	int ret = 0;
	struct writequeue_entry *e;
	int len, offset;
	struct msghdr outmsg;
	static char outcmsg[CMSG_SPACE(sizeof(struct sctp_sndrcvinfo))];
        struct cmsghdr *cmsg;
        struct sctp_sndrcvinfo *sinfo;
	struct kvec iov;

	/* See if we need to init an association before we start
	   sending precious messages */
	spin_lock(&ni->lock);
	if (!ni->assoc_id &&
	    !test_and_set_bit(NI_INIT_PENDING, &ni->flags)) {
		spin_unlock(&ni->lock);
		initiate_association(ni->nodeid);

		return 0;
	}
	spin_unlock(&ni->lock);

	outmsg.msg_name = NULL; /* We use assoc_id */
	outmsg.msg_namelen = 0;
	outmsg.msg_control = outcmsg;
	outmsg.msg_controllen = sizeof(outcmsg);
	outmsg.msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL | MSG_EOR;

	cmsg = CMSG_FIRSTHDR(&outmsg);
	cmsg->cmsg_level = IPPROTO_SCTP;
	cmsg->cmsg_type = SCTP_SNDRCV;
	cmsg->cmsg_len = CMSG_LEN(sizeof(struct sctp_sndrcvinfo));
	sinfo = (struct sctp_sndrcvinfo *)CMSG_DATA(cmsg);
	memset(sinfo, 0x00, sizeof(struct sctp_sndrcvinfo));
	sinfo->sinfo_ppid = cpu_to_le32(lowcomms_our_nodeid());
	sinfo->sinfo_assoc_id = ni->assoc_id;
        outmsg.msg_controllen = cmsg->cmsg_len;

	spin_lock(&ni->writequeue_lock);
	for (;;) {
		e = list_entry(ni->writequeue.next, struct writequeue_entry,
			       list);
		if ((struct list_head *) e == &ni->writequeue)
			break;

		kmap(e->page);
		len = e->len;
		offset = e->offset;
		BUG_ON(len == 0 && e->users == 0);
		spin_unlock(&ni->writequeue_lock);

		ret = 0;
		if (len) {
			iov.iov_base = page_address(e->page)+offset;
			iov.iov_len = len;

			ret = kernel_sendmsg(sctp_con.sock, &outmsg, &iov, 1, len);

			if (ret == -EAGAIN || ret == 0)
				goto out;
			if (ret <= 0)
				goto send_error;
		}
		else {
			/* Don't starve people filling buffers */
			schedule();
		}

		spin_lock(&ni->writequeue_lock);
		e->offset += ret;
		e->len -= ret;

		if (e->len == 0 && e->users == 0) {
			list_del(&e->list);
			free_entry(e);
			continue;
		}
	}
	spin_unlock(&ni->writequeue_lock);
      out:
	return ret;

      send_error:
	printk(KERN_INFO "dlm: Error sending to node %d: %d\n", ni->nodeid, ret);
	spin_lock(&ni->lock);
	if (!test_and_set_bit(NI_INIT_PENDING, &ni->flags)) {
		ni->assoc_id = 0;
		spin_unlock(&ni->lock);
		initiate_association(ni->nodeid);
	}
	else {
		spin_unlock(&ni->lock);
	}

	return ret;
}

/* Try to send any messages that are pending
 */
static void process_output_queue(void)
{
	struct list_head *list;
	struct list_head *temp;
	int ret;

	spin_lock_bh(&write_nodes_lock);
	list_for_each_safe(list, temp, &write_nodes) {
		struct nodeinfo *ni =
		    list_entry(list, struct nodeinfo, write_list);
		list_del(&ni->write_list);
		clear_bit(NI_WRITE_PENDING, &ni->flags);

		spin_unlock_bh(&write_nodes_lock);

		ret = send_to_sock(ni);
		if (ret < 0) {
		}
		spin_lock_bh(&write_nodes_lock);
	}
	spin_unlock_bh(&write_nodes_lock);
}

static void clean_one_writequeue(struct nodeinfo *ni)
{
	struct list_head *list;
	struct list_head *temp;

	spin_lock(&ni->writequeue_lock);
	list_for_each_safe(list, temp, &ni->writequeue) {
		struct writequeue_entry *e =
			list_entry(list, struct writequeue_entry, list);
		list_del(&e->list);
		free_entry(e);
	}
	spin_unlock(&ni->writequeue_lock);
}

static void clean_writequeues(void)
{
	int i;

	for (i=1; i<=max_nodeid; i++) {
		struct nodeinfo *ni = nodeid2nodeinfo(i, 0);
		if (ni)
			clean_one_writequeue(ni);
	}
}

static void dealloc_nodeinfo(void)
{
	int i;

	for (i=1; i<=max_nodeid; i++) {
		struct nodeinfo *ni = nodeid2nodeinfo(i, 0);
		if (ni) {
			idr_remove(&nodeinfo_idr, i);
			kfree(ni);
		}
	}
}


/* Called when a node leaves the service */
int lowcomms_close(int nodeid)
{
	struct nodeinfo *ni;

	ni = nodeid2nodeinfo(nodeid, 0);
	if (!ni)
		return -1;

	spin_lock(&ni->lock);
	if (ni->assoc_id) {
		ni->assoc_id = 0;
		/* Don't send shutdown here, sctp will just queue it
		   till the node comes back up! */
	}
	spin_unlock(&ni->lock);

	clean_one_writequeue(ni);
	clear_bit(NI_INIT_PENDING, &ni->flags);
	return 0;
}

/* API send message call, may queue the request */
/* N.B. This is the old interface - use the new one for new calls */
int lowcomms_send_message(int nodeid, char *buf, int len, int allocation)
{
	struct writequeue_entry *e;
	char *b;

	e = lowcomms_get_buffer(nodeid, len, allocation, &b);
	if (e) {
		memcpy(b, buf, len);
		lowcomms_commit_buffer(e);
		return 0;
	}
	return -ENOBUFS;
}

static int write_list_empty(void)
{
	int status;

	spin_lock_bh(&write_nodes_lock);
	status = list_empty(&write_nodes);
	spin_unlock_bh(&write_nodes_lock);

	return status;
}

/* DLM Transport comms receive daemon */
static int dlm_recvd(void *data)
{
	init_waitqueue_head(&lowcomms_recv_waitq);
	init_waitqueue_entry(&lowcomms_recv_waitq_head, current);
	add_wait_queue(&lowcomms_recv_waitq, &lowcomms_recv_waitq_head);

	/* Just keep waiting for data */
	while (!kthread_should_stop()) {
		int count = 0;

		set_current_state(TASK_INTERRUPTIBLE);

		if (!test_bit(CF_READ_PENDING, &sctp_con.flags)) {
			schedule();
		}
		set_current_state(TASK_RUNNING);

		if (test_and_clear_bit(CF_READ_PENDING, &sctp_con.flags)) {
			int ret;

			do {
				ret = receive_from_sock();

				/* Don't starve out everyone else */
				if (++count >= MAX_RX_MSG_COUNT) {
					schedule();
					count = 0;
				}

			} while (/*!atomic_dec_and_test(&sctp_con.waiting_requests) && */
				 !kthread_should_stop() && ret >=0);
		}
		schedule();
	}

	return 0;
}

/* DLM Transport send daemon */
static int dlm_sendd(void *data)
{
	init_waitqueue_head(&lowcomms_send_waitq);
	init_waitqueue_entry(&lowcomms_send_waitq_head, current);
	add_wait_queue(&lowcomms_send_waitq, &lowcomms_send_waitq_head);

	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (write_list_empty())
			schedule();
		set_current_state(TASK_RUNNING);

		process_output_queue();
	}

	return 0;
}

static void daemons_stop(void)
{
	kthread_stop(recv_task);
	kthread_stop(send_task);
}

static int daemons_start(void)
{
	struct task_struct *p;
	int error;

	p = kthread_run(dlm_recvd, NULL, "dlm_recvd");
	error = IS_ERR(p);
       	if (error) {
		log_print("can't start dlm_recvd %d", error);
		return error;
	}
	recv_task = p;

	p = kthread_run(dlm_sendd, NULL, "dlm_sendd");
	error = IS_ERR(p);
       	if (error) {
		log_print("can't start dlm_sendd %d", error);
		kthread_stop(recv_task);
		return error;
	}
	send_task = p;

	return 0;
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
	atomic_set(&accepting, 0);

	/* Set all the activity flags to prevent any
	   socket activity.
	*/
	sctp_con.flags = 0x7;
	daemons_stop();
	clean_writequeues();
	close_connection();
	dealloc_nodeinfo();
	max_nodeid = 0;

}

/* This is quite likely to sleep... */
int lowcomms_start(void)
{
	int error = 0;

	error = -ENOTCONN;

	/*
	 * Temporarily initialise the waitq head so that lowcomms_send_message
	 * doesn't crash if it gets called before the thread is fully
	 * initialised
	 */
	init_waitqueue_head(&lowcomms_send_waitq);
	spin_lock_init(&write_nodes_lock);
	INIT_LIST_HEAD(&write_nodes);
	init_rwsem(&nodeinfo_lock);

	error = -ENOMEM;

	/* Start socket */
	error = init_sock();
	if (error)
		goto fail_sock;
	error = daemons_start();
	if (error)
		goto fail_sock;
	atomic_set(&accepting, 1);

	return 0;

      fail_sock:
	close_connection();

	return error;
}

/* Don't accept any more outgoing work */
void lowcomms_stop_accept(void)
{
        atomic_set(&accepting, 0);
}

/* Cluster Manager interface functions for looking up
   nodeids and IP addresses by each other
*/

/* Return the IP address of a node given its NODEID */
static int lowcomms_ipaddr_from_nodeid(int nodeid, struct sockaddr *retaddr)
{
	struct sockaddr_storage *saddr;

	if (dlm_nodeid_addr(nodeid, (char *)&saddr))
		return -1;

	/* Extract the IP address */
	if (local_addr.ss_family == AF_INET) {
	        struct sockaddr_in *in4  = (struct sockaddr_in *)saddr;
		struct sockaddr_in *ret4 = (struct sockaddr_in *)retaddr;
		ret4->sin_addr.s_addr = in4->sin_addr.s_addr;
	}
	else {
	        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)saddr;
		struct sockaddr_in6 *ret6 = (struct sockaddr_in6 *)retaddr;
		memcpy(&ret6->sin6_addr, &in6->sin6_addr, sizeof(in6->sin6_addr));
	}

	return 0;
}

int lowcomms_our_nodeid(void)
{
	static int our_nodeid = 0;

	if (our_nodeid)
		return our_nodeid;

	our_nodeid = dlm_our_nodeid();

	/* Fill in the "template" structure */
	dlm_our_addr(0, (char *)&local_addr);
	return our_nodeid;
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
