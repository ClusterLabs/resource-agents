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

#ifndef _LIBCMAN_H_
#define _LIBCMAN_H_

#define CMAN_MAX_ADDR_LEN sizeof(struct sockaddr_in6)
#define CMAN_MAX_NODENAME_LEN 255
#define MAX_CLUSTER_NAME_LEN   16
#define CMAN_NODEID_US 0

typedef void *cman_handle_t;

typedef enum {CMAN_REASON_PORTCLOSED,
	      CMAN_REASON_STATECHANGE,
	      CMAN_REASON_SERVICEEVENT} cman_call_reason_t;


/* Flags passed to cman_dispatch() */
#define CMAN_DISPATCH_ONE 0
#define CMAN_DISPATCH_ALL 1
#define CMAN_DISPATCH_BLOCKING 2

/* A node address. this is a complete sockaddr_in[6] */
typedef struct cman_node_address
{
	int  cna_addrlen;
	char cna_address[CMAN_MAX_ADDR_LEN];
} cman_node_address_t;

/* Return from cman_get_node() */
typedef struct cman_node
{
	int cn_nodeid;
	cman_node_address_t cn_address;
	char cn_name[CMAN_MAX_NODENAME_LEN+1];
	int cn_member;
	int cn_incarnation;
} cman_node_t;

/* Returned from cman_get_version(), input to cman_set_version() */
typedef struct cman_version
{
	unsigned int cv_major;
	unsigned int cv_minor;
	unsigned int cv_patch;
	unsigned int cv_config;
} cman_version_t;

/* Return from cman_get_cluster() */
typedef struct cman_cluster
{
	char name[MAX_CLUSTER_NAME_LEN+1];
	uint16_t number;
} cman_cluster_t;

/* Callback routine for a membership event */
typedef void (*cman_callback_t)(cman_handle_t handle, void *private, int reason, int arg);

/* Callback routine for data received */
typedef void (*cman_datacallback_t)(cman_handle_t handle, void *private,
				    char *buf, int len, uint8_t port, int nodeid);


/* cman_init    returns the handle you need to pass to the other API calls,
   cman_finish  destroys that handle
*/
cman_handle_t cman_init(void *private);
int cman_finish(cman_handle_t handle);

/* Notification of membership change events. NOte that these are sent after
   a transition so multiple nodes may have left the cluster (but a maximum of
   one will have joined) for each callback.
*/
int cman_start_notification(cman_handle_t handle, cman_callback_t callback);
int cman_stop_notification(cman_handle_t handle);

/* Get the internal CMAN fd so you can pass it into poll() or select().
   if it's active then call cman_dispatch() on the handle to process the event */
int cman_get_fd(cman_handle_t handle);
int cman_dispatch(cman_handle_t handle, int flags);

/* Get info calls, self-explanatory I hope. nodeid can be CMAN_NODEID_US */

int cman_get_node_count(cman_handle_t handle);
int cman_get_nodes(cman_handle_t handle, int maxnodes, int *retnodes, cman_node_t *nodes);
int cman_get_node(cman_handle_t handle, int nodeid, cman_node_t *node);
int cman_is_active(cman_handle_t handle);
int cman_is_listening(cman_handle_t handle, int nodeid, uint8_t port);
int cman_is_quorate(cman_handle_t handle);
int cman_get_version(cman_handle_t handle, cman_version_t *version);
int cman_get_cluster(cman_handle_t handle, cman_cluster_t *clinfo);

/* You can only set the config version via this call, not the
   software/protocol version !
 */
int cman_set_version(cman_handle_t handle, cman_version_t *version);

/* Data transmission API. Uses the same FD as the rest of the calls.
   If the nodeid passed to cman_send_data() is zero then it will be
   broadcast to all nodes in the cluster.
   cman_start_recv_data() is like a bind(), and marks the port
   as "listening". See cman_is_listening() above.
*/
int cman_send_data(cman_handle_t handle, char *buf, int len, int flags, uint8_t port, int nodeid);
int cman_start_recv_data(cman_handle_t handle, cman_datacallback_t, uint8_t port);
int cman_end_recv_data(cman_handle_t handle);

#endif
