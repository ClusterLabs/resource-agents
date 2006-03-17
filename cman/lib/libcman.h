/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
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

#include <sys/socket.h>

#define LIBCMAN_VERSION 2

/*
 * Some maxima
 */
#define CMAN_MAX_ADDR_LEN sizeof(struct sockaddr_in6)
#define CMAN_MAX_NODENAME_LEN 255
#define MAX_CLUSTER_NAME_LEN   16

/*
 * Pass this into cman_get_node() as the nodeid to get local node information
 */
#define CMAN_NODEID_US 0

/* Pass this into cman_send_data to send a message to all nodes */
#define CMAN_NODEID_ALL 0

/*
 * Hang onto this, it's your key into the library. get one from cman_init() or cman_admin_init()
 */
typedef void *cman_handle_t;

/*
 * Reasons we get an event callback
 * PORTOPENED & TRY_SHUTDOWN only exist in libcman 2
 */
typedef enum {CMAN_REASON_PORTCLOSED,
	      CMAN_REASON_STATECHANGE,
              CMAN_REASON_PORTOPENED,
              CMAN_REASON_TRY_SHUTDOWN} cman_call_reason_t;

/*
 * Reason flags for cman_leave
 */
#define CMAN_LEAVEFLAG_DOWN    0
#define CMAN_LEAVEFLAG_REMOVED 3
#define CMAN_LEAVEFLAG_FORCE   0x10

/*
 * Flags for cman_shutdown
 */
#define CMAN_SHUTDOWN_ANYWAY   1
#define CMAN_SHUTDOWN_REMOVED  3

/*
 * Flags passed to cman_dispatch():
 * CMAN_DISPATCH_ONE dispatches a single message then returns,
 * CMAN_DISPATCH_ALL dispatches all outstanding messages (ie till EAGAIN) then returns,
 * CMAN_DISPATCH_BLOCKING forces it to wait for a message (cleans MSG_DONTWAIT in recvmsg)
 * CMAN_DISPATCH_IGNORE_* allows the caller to select which messages to process.
 */
#define CMAN_DISPATCH_ONE           0
#define CMAN_DISPATCH_ALL           1
#define CMAN_DISPATCH_BLOCKING      2
#define CMAN_DISPATCH_IGNORE_REPLY  4
#define CMAN_DISPATCH_IGNORE_DATA   8
#define CMAN_DISPATCH_IGNORE_EVENT 16
#define CMAN_DISPATCH_TYPE_MASK     3
#define CMAN_DISPATCH_IGNORE_MASK  46

/*
 * A node address. This is a complete sockaddr_in[6]
 */
typedef struct cman_node_address
{
	int  cna_addrlen;
	char cna_address[CMAN_MAX_ADDR_LEN];
} cman_node_address_t;

/*
 * Return from cman_get_node()
 */
typedef struct cman_node
{
	int cn_nodeid;
	cman_node_address_t cn_address;
	char cn_name[CMAN_MAX_NODENAME_LEN+1];
	int cn_member;
	int cn_incarnation;
	struct timeval cn_jointime;
} cman_node_t;

/*
 * Returned from cman_get_version(),
 * input to cman_set_version(), though only cv_config can be changed
 */
typedef struct cman_version
{
	unsigned int cv_major;
	unsigned int cv_minor;
	unsigned int cv_patch;
	unsigned int cv_config;
} cman_version_t;

/*
 * Return from cman_get_cluster()
 */
typedef struct cman_cluster
{
	char     ci_name[MAX_CLUSTER_NAME_LEN+1];
	uint16_t ci_number;
	uint32_t ci_generation;
} cman_cluster_t;

/*
 * This is returned from cman_get_extra_info - it's really
 * only for use by cman_tool, don't depend on this not changing
 */

/* Flags in ei_flags */
#define CMAN_EXTRA_FLAG_2NODE    1
#define CMAN_EXTRA_FLAG_ERROR    2
#define CMAN_EXTRA_FLAG_SHUTDOWN 4

typedef struct cman_extra_info {
	int           ei_node_state;
	int           ei_flags;
	int           ei_node_votes;
	int           ei_total_votes;
	int           ei_expected_votes;
	int           ei_quorum;
	int           ei_members;
	int           ei_num_addresses;
	char          ei_addresses[1]; /* Array of num_addresses*sockaddr_storage */
} cman_extra_info_t;

/*
 * NOTE: Apart from cman_replyto_shutdown(), you must not
 * call other cman_* functions in these callbacks:
 */

/*
 * Callback routine for a membership event
 */
typedef void (*cman_callback_t)(cman_handle_t handle, void *private, int reason, int arg);

/*
 * Callback routine for data received
 */
typedef void (*cman_datacallback_t)(cman_handle_t handle, void *private,
				    char *buf, int len, uint8_t port, int nodeid);


/*
 * cman_init        returns the handle you need to pass to the other API calls.
 * cman_admin_init  opens admin socket for privileged operations.
 * cman_finish      destroys that handle.
 *
 * Note that admin sockets can't send data messages.
 *
 */
cman_handle_t cman_init(void *private);
cman_handle_t cman_admin_init(void *private);
int cman_finish(cman_handle_t handle);

/* Update/retreive private data */
int cman_set_private(cman_handle_t *h, void *private);
int cman_get_private(cman_handle_t *h, void **private);

/*
 * Notification of membership change events. Note that these are sent after
 * a transition, so multiple nodes may have left the cluster (but a maximum of
 * one will have joined) for each callback.
 */
int cman_start_notification(cman_handle_t handle, cman_callback_t callback);
int cman_stop_notification(cman_handle_t handle);

/*
 * Get the internal CMAN fd so you can pass it into poll() or select().
 * When it's active then call cman_dispatch() on the handle to process the event.
 * NOTE: This fd can change between calls to cman_dispatch() so always call this
 * routine to get the latest one. (This is mainly due to message caching).
 * One upshot of this is that you must never read or write this FD (it may on occasion
 * point to /dev/zero if you have messages cached!)
 */
int cman_get_fd(cman_handle_t handle);
int cman_dispatch(cman_handle_t handle, int flags);

/*
 * Get info calls, self-explanatory I hope. nodeid can be CMAN_NODEID_US
 */
int cman_get_node_count(cman_handle_t handle);
int cman_get_subsys_count(cman_handle_t handle);
int cman_get_nodes(cman_handle_t handle, int maxnodes, int *retnodes, cman_node_t *nodes);
int cman_get_node(cman_handle_t handle, int nodeid, cman_node_t *node);
int cman_is_active(cman_handle_t handle);
int cman_is_listening(cman_handle_t handle, int nodeid, uint8_t port);
int cman_is_quorate(cman_handle_t handle);
int cman_get_version(cman_handle_t handle, cman_version_t *version);
int cman_get_cluster(cman_handle_t handle, cman_cluster_t *clinfo);
int cman_get_extra_info(cman_handle_t handle, cman_extra_info_t *info, int maxlen);

/*
 * Admin functions. You will need privileges and have a handle created by cman_admin_init()
 * to use them.
 */
int cman_set_version(cman_handle_t handle, cman_version_t *version);
int cman_leave_cluster(cman_handle_t handle, int reason);
int cman_set_votes(cman_handle_t handle, int votes, int nodeid);
int cman_set_expected_votes(cman_handle_t handle, int expected_votes);
int cman_kill_node(cman_handle_t handle, int nodeid);
int cman_shutdown(cman_handle_t, int flags);
/*
 * cman_shutdown() will send a REASON_TRY_SHUTDOWN event to all
 * clients registered for notifications. They should respond by calling
 * cman_replyto_shutdown() to indicate whether they will allow
 * cman to close down or not. If cman gets >=1 "no" (0) or the
 * request times out (default 5 seconds) then shutdown will be
 * cancelled and cman_shutdown() will return -1 with errno == EBUSY.
 */


/* Call this if you get a TRY_SHUTDOWN event. To signal whether you
 * will let cman shutdown or not
 */
int cman_replyto_shutdown(cman_handle_t, int yesno);

/*
 * Data transmission API. Uses the same FD as the rest of the calls.
 * If the nodeid passed to cman_send_data() is zero then it will be
 * broadcast to all nodes in the cluster.
 * cman_start_recv_data() is like a bind(), and marks the port
 * as "listening". See cman_is_listening() above.
 */
int cman_send_data(cman_handle_t handle, void *buf, int len, int flags, uint8_t port, int nodeid);
int cman_start_recv_data(cman_handle_t handle, cman_datacallback_t, uint8_t port);
int cman_end_recv_data(cman_handle_t handle);

/*
 * Barrier API
 */
int cman_barrier_register(cman_handle_t handle, char *name, int flags, int nodes);
int cman_barrier_change(cman_handle_t handle, char *name, int flags, int arg);
int cman_barrier_wait(cman_handle_t handle, char *name);
int cman_barrier_delete(cman_handle_t handle, char *name);

/*
 * Add your own quorum device here, needs an admin socket
 */
int cman_register_quorum_device(cman_handle_t handle, char *name, int votes);
int cman_unregister_quorum_device(cman_handle_t handle);
int cman_poll_quorum_device(cman_handle_t handle, int isavailable);

#endif
