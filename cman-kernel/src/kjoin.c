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

#include <linux/socket.h>
#include <net/sock.h>
#include <linux/list.h>
#include <cluster/cnxman.h>
#include <linux/in.h>

#include "cnxman-private.h"

static struct socket *mcast_sock;
static struct socket *recv_sock;
static struct socket *cluster_sock;

extern short cluster_id;
extern int join_count;
extern struct semaphore join_count_lock;
extern atomic_t cnxman_running;

int kcl_join_cluster(struct cl_join_cluster_info *join_info)
{
	int result;
	int one = 1, error;
	unsigned int ipaddr = join_info->ipaddr, brdaddr = join_info->brdaddr;
	unsigned short port = join_info->port;
	mm_segment_t fs;
	struct sockaddr_in saddr;
	struct kcl_multicast_sock mcast_info;

	down(&join_count_lock);
	if (atomic_read(&cnxman_running))
	{
		error = 0;
		if (join_info->cluster_id == cluster_id)
			join_count++;
		else
			error = -EINVAL;
		up(&join_count_lock);
		return error;
	}
	up(&join_count_lock);

	result = sock_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP, &mcast_sock);
	if (result < 0)
	{
		printk(KERN_ERR CMAN_NAME ": Can't create Multicast socket\n");
		return result;
	}

	result = sock_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP, &recv_sock);
	if (result < 0)
	{
		printk(KERN_ERR CMAN_NAME ": Can't create Receive socket\n");
		return result;
	}

	fs = get_fs();
	set_fs(get_ds());

	if ((error = sock_setsockopt(mcast_sock, SOL_SOCKET, SO_BROADCAST,
				     (void *) &one, sizeof (int))))
	{
		set_fs(fs);
		printk("Error %d Setting master socket to SO_BROADCAST\n",
		       error);
		sock_release(mcast_sock);
		return -1;
	}
	set_fs(fs);

	/* Bind the multicast socket */
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(port);
	saddr.sin_addr.s_addr = cpu_to_be32(brdaddr);
	result =
	    mcast_sock->ops->bind(mcast_sock, (struct sockaddr *) &saddr,
				  sizeof (saddr));
	if (result < 0)
	{
		printk(KERN_ERR CMAN_NAME ": Can't bind multicast socket\n");
		sock_release(mcast_sock);
		sock_release(recv_sock);
		return result;
	}

	/* Bind the receive socket to our IP address */
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(port);
	saddr.sin_addr.s_addr = cpu_to_be32(ipaddr);
	result =
	    recv_sock->ops->bind(recv_sock, (struct sockaddr *) &saddr,
				 sizeof (saddr));
	if (result < 0)
	{
		printk(KERN_ERR CMAN_NAME ": Can't bind receive socket\n");
		sock_release(mcast_sock);
		sock_release(recv_sock);
		return result;
	}

	/* Create the cluster master socket */
	result =
	    sock_create(AF_CLUSTER, SOCK_DGRAM, CLPROTO_MASTER, &cluster_sock);
	if (result < 0)
	{
		printk(KERN_ERR CMAN_NAME
		       ": Can't create cluster master socket\n");
		sock_release(mcast_sock);
		sock_release(recv_sock);
		return result;
	}

	/* This is the broadcast transmit address */
	saddr.sin_addr.s_addr = cpu_to_be32(brdaddr);

	/* Pass the multicast socket to kernel space */
	mcast_info.sock = mcast_sock;
	mcast_info.number = 1;

	fs = get_fs();
	set_fs(get_ds());

	if ((error = cluster_sock->ops->setsockopt(cluster_sock, CLPROTO_MASTER,
						   KCL_SET_MULTICAST,
						   (void *) &mcast_info,
						   sizeof (mcast_info))))
	{
		set_fs(fs);
		printk(CMAN_NAME
		       ": Unable to pass multicast socket to cnxman, %d\n",
		       error);
		sock_release(mcast_sock);
		sock_release(recv_sock);
		sock_release(cluster_sock);
		return -1;
	}

	mcast_info.sock = recv_sock;
	if ((error =
	     cluster_sock->ops->setsockopt(cluster_sock, CLPROTO_MASTER,
					   KCL_SET_RCVONLY,
					   (void *) &mcast_info,
					   sizeof (mcast_info))))
	{
		set_fs(fs);
		printk(CMAN_NAME
		       ": Unable to pass receive socket to cnxman, %d\n",
		       error);
		sock_release(mcast_sock);
		sock_release(recv_sock);
		sock_release(cluster_sock);
		return -1;
	}

	/* This setsockopt expects usermode variables */

	if (cluster_sock->ops->
	    setsockopt(cluster_sock, CLPROTO_MASTER, CLU_JOIN_CLUSTER,
		       (void *) join_info,
		       sizeof (struct cl_join_cluster_info)))

	{
		set_fs(fs);
		printk(CMAN_NAME ": Unable to join cluster\n");
		sock_release(mcast_sock);
		sock_release(recv_sock);
		sock_release(cluster_sock);
		return -1;
	}
	set_fs(fs);

	return 0;
}

int kcl_leave_cluster(int remove)
{
	mm_segment_t fs;
	int rem = remove;
	int ret = 0;
	struct socket *shutdown_sock = cluster_sock;

	cluster_sock = NULL;

	if (!shutdown_sock)
	{
		/* Create the cluster master socket */
		int result =
		    sock_create(AF_CLUSTER, SOCK_DGRAM, CLPROTO_MASTER,
				&shutdown_sock);
		if (result < 0)
		{
			printk(KERN_ERR CMAN_NAME
			       ": Can't create cluster master socket\n");
			sock_release(mcast_sock);
			sock_release(recv_sock);
			return result;
		}
	}

	fs = get_fs();
	set_fs(get_ds());

	if ((ret =
	     shutdown_sock->ops->setsockopt(shutdown_sock, CLPROTO_MASTER,
					    CLU_LEAVE_CLUSTER, (void *) &rem,
					    sizeof (int))))
	{
		printk(KERN_ERR CMAN_NAME ": Unable to leave cluster, %d\n",
		       ret);
	}
	set_fs(fs);

	sock_release(shutdown_sock);

	return ret;
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
