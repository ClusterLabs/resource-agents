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

#define CMAN_DISPATCH_ONE 0
#define CMAN_DISPATCH_ALL 1
#define CMAN_DISPATCH_BLOCKING 2

typedef struct cman_node_address
{
	int  cna_addrlen;
	char cna_address[CMAN_MAX_ADDR_LEN];
} cman_node_address_t;

typedef struct cman_node
{
	int cn_nodeid;
	cman_node_address_t cn_address;
	char cn_name[CMAN_MAX_NODENAME_LEN+1];
	int cn_member;
	int cn_incarnation;
} cman_node_t;

typedef struct cman_version
{
	unsigned int cv_major;
	unsigned int cv_minor;
	unsigned int cv_patch;
	unsigned int cv_config;
} cman_version_t;

typedef struct cman_cluster
{
	char name[MAX_CLUSTER_NAME_LEN+1];
	uint16_t number;
} cman_cluster_t;

typedef void (*cman_callback_t)(cman_handle_t handle, void *private, int reason, int arg);


cman_handle_t cman_init(void *private);
int cman_finish(cman_handle_t handle);
int cman_start_notification(cman_handle_t handle, cman_callback_t callback);
int cman_stop_notification(cman_handle_t handle);
int cman_get_fd(cman_handle_t handle);
int cman_dispatch(cman_handle_t handle, int flags);
int cman_get_node_count(cman_handle_t handle);
int cman_get_nodes(cman_handle_t handle, int maxnodes, int *retnodes, cman_node_t *nodes);
int cman_get_node(cman_handle_t handle, int nodeid, cman_node_t *node);
int cman_is_active(cman_handle_t handle);
int cman_is_listening(cman_handle_t handle, int nodeid, uint8_t port);
int cman_is_quorate(cman_handle_t handle);
int cman_get_version(cman_handle_t handle, cman_version_t *version);
int cman_set_version(cman_handle_t handle, cman_version_t *version);
int cman_get_cluster(cman_handle_t handle, cman_cluster_t *clinfo);

#endif
