/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This library is free software; you can redistribute it and/or
**  modify it under the terms of the GNU Lesser General Public
**  License as published by the Free Software Foundation; either
**  version 2 of the License, or (at your option) any later version.
**
**  This library is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
**  Lesser General Public License for more details.
**
**  You should have received a copy of the GNU Lesser General Public
**  License along with this library; if not, write to the Free Software
**  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
**
*******************************************************************************
******************************************************************************/

#include <sys/types.h>
#include <sys/ioctl.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <cluster/cnxman-socket.h>
#include "libcman.h"

struct cman_handle
{
	int fd;
	void *private;
	cman_callback_t callback;
};


/* TODO: Get the address(s) for the node too */
static void copy_node(cman_node_t *unode, struct cl_cluster_node *knode)
{
	unode->cn_nodeid = knode->node_id;
	unode->cn_member = knode->state == NODESTATE_MEMBER?1:0;
	strcpy(unode->cn_name, knode->name);
	unode->cn_incarnation = knode->incarnation;
	memset(&unode->cn_address, 0, sizeof(cman_node_address_t));
}


cman_handle_t cman_init(void *private)
{
	struct cman_handle *h;

	h = malloc(sizeof(struct cman_handle));
	if (!h)
		return NULL;

	h->private = private;
	h->callback = NULL;

	h->fd = socket(AF_CLUSTER, SOCK_DGRAM, CLPROTO_CLIENT);
	if (h->fd == -1)
	{
		int saved_errno = errno;
		free(h);
		errno = saved_errno;
		return NULL;
	}
	return (cman_handle_t)h;
}

int cman_finish(cman_handle_t handle)
{
	struct cman_handle *h = (struct cman_handle *)handle;

	close(h->fd);
	free(h);

	return 0;
}

int cman_start_notification(cman_handle_t handle, cman_callback_t callback)
{
	struct cman_handle *h = (struct cman_handle *)handle;

	if (!callback)
	{
		errno = EINVAL;
		return -1;
	}

	h->callback = callback;

	return 0;
}

int cman_stop_notification(cman_handle_t handle)
{
	struct cman_handle *h = (struct cman_handle *)handle;

	h->callback = NULL;

	return 0;
}


int cman_get_fd(cman_handle_t handle)
{
	struct cman_handle *h = (struct cman_handle *)handle;
	return h->fd;
}

int cman_dispatch(cman_handle_t handle, int flags)
{
	struct cman_handle *h = (struct cman_handle *)handle;
	struct iovec iov[1];
	struct msghdr msg;
	struct sockaddr_cl saddr;
	int len;
	int recv_flags = MSG_OOB;
	char buf[128];

	if (!(flags & CMAN_DISPATCH_BLOCKING))
		recv_flags |= MSG_DONTWAIT;


	do {
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		msg.msg_iovlen = 1;
		msg.msg_iov = iov;
		msg.msg_name = &saddr;
		msg.msg_flags = 0;
		msg.msg_namelen = sizeof(saddr);
		iov[0].iov_len = sizeof(buf);
		iov[0].iov_base = buf;

		len = recvmsg(h->fd, &msg, recv_flags);
		if (len < 0 && errno == EAGAIN)
			return len;

		/* Send a callback if registered */
		if (msg.msg_flags & MSG_OOB && h->callback)
		{
			int reason;
			int arg = 0;
			struct cl_portclosed_oob *portmsg =
				(struct cl_portclosed_oob *)buf;
			switch (buf[0])
			{
			case CLUSTER_OOB_MSG_PORTCLOSED:
				reason = CMAN_REASON_PORTCLOSED;
				arg = portmsg->port;
				break;

			case CLUSTER_OOB_MSG_STATECHANGE:
				reason = CMAN_REASON_STATECHANGE;
				break;

			case CLUSTER_OOB_MSG_SERVICEEVENT:
				reason = CMAN_REASON_SERVICEEVENT;
				break;
			default:
				continue;
			}
			h->callback(h, h->private, reason, arg);
		}
		/* Else throw it awy... */
	}
	while ( flags & CMAN_DISPATCH_ALL &&
		(len < 0 && errno == EAGAIN) );

	return len;
}

int cman_get_node_count(cman_handle_t handle)
{
	struct cman_handle *h = (struct cman_handle *)handle;
	return ioctl(h->fd, SIOCCLUSTER_GETALLMEMBERS, 0);
}

int cman_get_nodes(cman_handle_t handle, int maxnodes, int *retnodes, cman_node_t *nodes)
{
	struct cman_handle *h = (struct cman_handle *)handle;
	struct cl_cluster_node *cman_nodes;
	struct cl_cluster_nodelist cman_req;
	int status;
	int count = 0;

	if (!retnodes || !nodes || maxnodes < 1)
	{
		errno = EINVAL;
		return -1;
	}

	cman_nodes = malloc(sizeof(struct cl_cluster_node) * maxnodes);
	if (!cman_nodes)
		return -1;

	cman_req.max_members = maxnodes;
	cman_req.nodes = cman_nodes;
	status = ioctl(h->fd, SIOCCLUSTER_GETALLMEMBERS, &cman_req);
	if (status < 0)
	{
		int saved_errno = errno;
		free(cman_nodes);
		errno = saved_errno;
		return -1;
	}
	if (cman_nodes[0].size != sizeof(struct cl_cluster_node))
	{
		free(cman_nodes);
		errno = EINVAL;
		return -1;
	}

	for (count = 0; count < status; count++)
	{
		copy_node(&nodes[count], &cman_nodes[count]);
	}
	free(cman_nodes);
	*retnodes = count;
	return 0;
}

int cman_get_node(cman_handle_t handle, int nodeid, cman_node_t *node)
{
	struct cman_handle *h = (struct cman_handle *)handle;
	struct cl_cluster_node cman_node;
	int status;

	if (!node)
	{
		errno = EINVAL;
		return -1;
	}

	cman_node.node_id = nodeid;
	cman_node.name[0] = 0;/* Get by id */
	status = ioctl(h->fd, SIOCCLUSTER_GETNODE, &cman_node);
	if (status < 0)
		return -1;

	copy_node(node, &cman_node);

	return 0;
}

int cman_is_active(cman_handle_t handle)
{
	struct cman_handle *h = (struct cman_handle *)handle;
	return ioctl(h->fd, SIOCCLUSTER_ISACTIVE, 0);
}

int cman_is_listening(cman_handle_t handle, int nodeid, uint8_t port)
{
	struct cman_handle *h = (struct cman_handle *)handle;
	struct cl_listen_request req;

	req.port = port;
	req.nodeid = nodeid;
	return ioctl(h->fd, SIOCCLUSTER_ISLISTENING, &req);

}
int cman_is_quorate(cman_handle_t handle)
{
	struct cman_handle *h = (struct cman_handle *)handle;
	return ioctl(h->fd, SIOCCLUSTER_ISQUORATE, 0);
}


int cman_get_version(cman_handle_t handle, cman_version_t *version)
{
	struct cman_handle *h = (struct cman_handle *)handle;

	if (!version)
	{
		errno = EINVAL;
		return -1;
	}
	return ioctl(h->fd, SIOCCLUSTER_GET_VERSION, version);
}

int cman_set_version(cman_handle_t handle, cman_version_t *version)
{
	struct cman_handle *h = (struct cman_handle *)handle;

	if (!version)
	{
		errno = EINVAL;
		return -1;
	}
	return ioctl(h->fd, SIOCCLUSTER_SET_VERSION, version);
}

int cman_get_cluster(cman_handle_t handle, cman_cluster_t *clinfo)
{
	struct cman_handle *h = (struct cman_handle *)handle;

	if (!clinfo)
	{
		errno = EINVAL;
		return -1;
	}
	return ioctl(h->fd, SIOCCLUSTER_GETCLUSTER, clinfo);
}
