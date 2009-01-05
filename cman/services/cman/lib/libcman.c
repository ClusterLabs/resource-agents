/*
 * Provides a cman API using the corosync executive
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

#include <corosync/corotypes.h>
#include <corosync/mar_gen.h>
#include <corosync/ipc_gen.h>
#include <corosync/ais_util.h>
#include <corosync/cfg.h>
#include <corosync/confdb.h>
#include <corosync/cmanquorum.h>
#include <corosync/ipc_cman.h>

#include "libcman.h"

#define CMAN_MAGIC 0x434d414e

#define CMAN_SHUTDOWN_ANYWAY   1
#define CMAN_SHUTDOWN_REMOVED  2

struct cman_inst {
	int magic;
	int response_fd;
	int dispatch_fd;
	int finalize;
	void *privdata;
	cman_datacallback_t data_callback;
	cman_callback_t notify_callback;
	pthread_mutex_t response_mutex;
	pthread_mutex_t dispatch_mutex;

	corosync_cfg_handle_t cfg_handle;
	cmanquorum_handle_t cmq_handle;
	confdb_handle_t confdb_handle;
};

static void cfg_shutdown_callback(
	corosync_cfg_handle_t handle,
	CorosyncCfgShutdownFlagsT flags);

static void cmanquorum_notification_callback(
        cmanquorum_handle_t handle,
        uint32_t quorate,
        uint32_t node_list_entries,
        cmanquorum_node_t node_list[]);

static cmanquorum_callbacks_t cmq_callbacks =
{
	.cmanquorum_notify_fn = cmanquorum_notification_callback,
};

static CorosyncCfgCallbacksT cfg_callbacks =
{
	.corosyncCfgStateTrackCallback = NULL,
	.corosyncCfgShutdownCallback = cfg_shutdown_callback,
};

static void cman_instance_destructor (void *instance);


#define VALIDATE_HANDLE(h) do {if (!(h) || (h)->magic != CMAN_MAGIC) {errno = EINVAL; return -1;}} while (0)

static struct cman_inst *admin_inst;

static void cfg_shutdown_callback(
	corosync_cfg_handle_t handle,
	CorosyncCfgShutdownFlagsT flags)
{
	int cman_flags = 0;

	if (!admin_inst)
		return;

	if (flags == COROSYNC_CFG_SHUTDOWN_FLAG_REGARDLESS)
		cman_flags = CMAN_SHUTDOWN_ANYWAY;

	if (admin_inst->notify_callback)
		admin_inst->notify_callback((void *)admin_inst, admin_inst->privdata, CMAN_REASON_TRY_SHUTDOWN, cman_flags);

}

static void cmanquorum_notification_callback(
        cmanquorum_handle_t handle,
        uint32_t quorate,
        uint32_t node_list_entries,
        cmanquorum_node_t node_list[])
{
	struct cman_inst *cman_inst;

	cmanquorum_context_get(handle, (void **)&cman_inst);

	if (cman_inst->notify_callback)
		cman_inst->notify_callback((void*)cman_inst, cman_inst->privdata, CMAN_REASON_STATECHANGE, quorate);
}


/*
 * Clean up function for a cman instance (cman_init) handle
 */
static void cman_instance_destructor (void *instance)
{
	struct cman_inst *cman_inst = instance;

	pthread_mutex_destroy (&cman_inst->response_mutex);
}

static int cmanquorum_check_and_start(struct cman_inst *cman_inst)
{
	if (!cman_inst->cmq_handle) {
		if (cmanquorum_initialize(&cman_inst->cmq_handle, &cmq_callbacks) != SA_AIS_OK) {
			errno = ENOMEM;
			return -1;
		}
		cmanquorum_context_set(cman_inst->cmq_handle, (void*)cman_inst);
	}
	return 0;
}

cman_handle_t cman_init (
	void *privdata)
{
	SaAisErrorT error;
	struct cman_inst *cman_inst;

	cman_inst = malloc(sizeof(struct cman_inst));
	if (!cman_inst)
		return NULL;

	error = saServiceConnect (&cman_inst->dispatch_fd,
				  &cman_inst->response_fd,
				  CMAN_SERVICE);
	if (error != SA_AIS_OK) {
		goto error;
	}

	cman_inst->privdata = privdata;
	pthread_mutex_init (&cman_inst->response_mutex, NULL);
	pthread_mutex_init (&cman_inst->dispatch_mutex, NULL);

	return (void *)cman_inst;

error:
	free(cman_inst);
	errno = ENOMEM;
	return NULL;
}

cman_handle_t cman_admin_init (
	void *privdata)
{
	if (admin_inst) {
		errno = EBUSY;
		return -1;
	}

	admin_inst = cman_init(NULL);
}


int cman_finish (
	cman_handle_t handle)
{
	struct cman_inst *cman_inst;
	SaAisErrorT error;

	cman_inst = (struct cman_inst *)handle;
	VALIDATE_HANDLE(cman_inst);

	if (cman_inst->cmq_handle) {
		quorum_finalize(cman_inst->cmq_handle);
		cman_inst->cmq_handle = 0;
	}
	if (cman_inst->cfg_handle) {
		corosync_cfg_finalize(cman_inst->cfg_handle);
		cman_inst->cfg_handle = 0;
	}

	if (handle == admin_inst)
		admin_inst = NULL;

	pthread_mutex_lock (&cman_inst->response_mutex);

	/*
	 * Another thread has already started finalizing
	 */
	if (cman_inst->finalize) {
		pthread_mutex_unlock (&cman_inst->response_mutex);
		errno = EINVAL;
		return -1;
	}

	cman_inst->finalize = 1;

	pthread_mutex_unlock (&cman_inst->response_mutex);

	/*
	 * Disconnect from the server
	 */
	if (cman_inst->response_fd != -1) {
		shutdown(cman_inst->response_fd, 0);
		close(cman_inst->response_fd);
	}

	return 0;
}


int cman_start_recv_data (
	cman_handle_t handle,
	cman_datacallback_t callback,
	uint8_t port)
{
	int error;
	struct cman_inst *cman_inst;
	struct iovec iov[2];
	struct req_lib_cman_bind req_lib_cman_bind;
	mar_res_header_t res;

	cman_inst = (struct cman_inst *)handle;
	VALIDATE_HANDLE(cman_inst);

	pthread_mutex_lock (&cman_inst->response_mutex);

	req_lib_cman_bind.header.size = sizeof (struct req_lib_cman_bind);
	req_lib_cman_bind.header.id = MESSAGE_REQ_CMAN_BIND;
	req_lib_cman_bind.port = port;

	iov[0].iov_base = (char *)&req_lib_cman_bind;
	iov[0].iov_len = sizeof (struct req_lib_cman_bind);

	error = saSendMsgReceiveReply (cman_inst->response_fd, iov, 1,
		&res, sizeof (mar_res_header_t));

	pthread_mutex_unlock (&cman_inst->response_mutex);

	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	errno = error = res.error;

error_exit:
	return (error?-1:0);
}

int cman_end_recv_data (
	cman_handle_t handle)
{
	int error;
	struct cman_inst *cman_inst;
	struct iovec iov[2];
	mar_req_header_t req;
	mar_res_header_t res;

	cman_inst = (struct cman_inst *)handle;
	VALIDATE_HANDLE(cman_inst);

	pthread_mutex_lock (&cman_inst->response_mutex);

	req.size = sizeof (mar_req_header_t);
	req.id = MESSAGE_REQ_CMAN_UNBIND;

	iov[0].iov_base = (char *)&req;
	iov[0].iov_len = sizeof (mar_req_header_t);

	error = saSendMsgReceiveReply (cman_inst->response_fd, iov, 1,
		&res, sizeof (mar_res_header_t));

	pthread_mutex_unlock (&cman_inst->response_mutex);

	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	errno = error = res.error;

error_exit:

	return (error?-1:0);
}

int cman_send_data(cman_handle_t handle, const void *message, int len, int flags, uint8_t port, int nodeid)
{
	int error;
	struct cman_inst *cman_inst;
	struct iovec iov[2];
	char buf[len+sizeof(struct req_lib_cman_sendmsg)];
	struct req_lib_cman_sendmsg *req_lib_cman_sendmsg = (struct req_lib_cman_sendmsg *)buf;
	mar_res_header_t res;

	cman_inst = (struct cman_inst *)handle;
	VALIDATE_HANDLE(cman_inst);

	pthread_mutex_lock (&cman_inst->response_mutex);

	req_lib_cman_sendmsg->header.size = sizeof (mar_req_header_t);
	req_lib_cman_sendmsg->header.id = MESSAGE_REQ_CMAN_SENDMSG;
	req_lib_cman_sendmsg->to_port = port;
	req_lib_cman_sendmsg->to_node = nodeid;
	req_lib_cman_sendmsg->msglen = len;
	memcpy(req_lib_cman_sendmsg->message, message, len);

	iov[0].iov_base = buf;
	iov[0].iov_len = len+sizeof(struct req_lib_cman_sendmsg);

	error = saSendMsgReceiveReply (cman_inst->response_fd, iov, 1,
		&res, sizeof (mar_res_header_t));

	pthread_mutex_unlock (&cman_inst->response_mutex);

	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	errno = error = res.error;

error_exit:

	return (error?-1:0);
}

int cman_is_listening (
	cman_handle_t handle,
	int nodeid,
	uint8_t port)
{
	int error;
	struct cman_inst *cman_inst;
	struct iovec iov[2];
	struct res_lib_cman_is_listening res_lib_cman_is_listening;
	struct req_lib_cman_is_listening req_lib_cman_is_listening;

	cman_inst = (struct cman_inst *)handle;
	VALIDATE_HANDLE(cman_inst);

	pthread_mutex_lock (&cman_inst->response_mutex);

	req_lib_cman_is_listening.header.size = sizeof (struct req_lib_cman_is_listening);
	req_lib_cman_is_listening.header.id = MESSAGE_REQ_CMAN_IS_LISTENING;
	req_lib_cman_is_listening.nodeid = nodeid;
	req_lib_cman_is_listening.port = port;

	iov[0].iov_base = (char *)&req_lib_cman_is_listening;
	iov[0].iov_len = sizeof (struct req_lib_cman_is_listening);

	error = saSendMsgReceiveReply (cman_inst->response_fd, iov, 1,
		&res_lib_cman_is_listening, sizeof (struct res_lib_cman_is_listening));

	pthread_mutex_unlock (&cman_inst->response_mutex);

	if (error != SA_AIS_OK) {
		goto error_exit;
	}

	errno = error = res_lib_cman_is_listening.header.error;

error_exit:

	return (error?-1:0);
}

/* an example of how we would query the quorum service */
int cman_is_quorate(cman_handle_t handle)
{
	struct cman_inst *cman_inst;
	int quorate = -1;
	int error;
	struct cmanquorum_info info;

	cman_inst = (struct cman_inst *)handle;
	VALIDATE_HANDLE(cman_inst);

	if (!cmanquorum_check_and_start(cman_inst))
		return -1;

	if (cmanquorum_getinfo(cman_inst->cmq_handle, 0, &info) != SA_AIS_OK)
		errno = EINVAL;
	else
		quorate = ((info.flags & CMANQUORUM_INFO_FLAG_QUORATE) != 0);

	return quorate;
}


int cman_shutdown(cman_handle_t handle, int flags)
{
	struct cman_inst *cman_inst;
	int error;
	CorosyncCfgShutdownFlagsT cfg_flags = 0;

	cman_inst = (struct cman_inst *)handle;
	VALIDATE_HANDLE(cman_inst);

	if (!cman_inst->cfg_handle) {
		if (corosync_cfg_initialize(&cman_inst->cfg_handle, &cfg_callbacks) != SA_AIS_OK) {
			errno = ENOMEM;
			return -1;
		}
	}

	if (flags == CMAN_SHUTDOWN_ANYWAY)
		cfg_flags = COROSYNC_CFG_SHUTDOWN_FLAG_REGARDLESS;

	error = corosync_cfg_try_shutdown(cman_inst->cfg_handle, cfg_flags);

	return error;
}

int cman_leave_cluster(cman_handle_t handle, int flags)
{
	struct cman_inst *cman_inst;
	int error;
	CorosyncCfgShutdownFlagsT cfg_flags = 0;

	cman_inst = (struct cman_inst *)handle;
	VALIDATE_HANDLE(cman_inst);

	if (!cman_inst->cfg_handle) {
		if (corosync_cfg_initialize(&cman_inst->cfg_handle, &cfg_callbacks) != SA_AIS_OK) {
			errno = ENOMEM;
			return -1;
		}
	}

	cfg_flags = COROSYNC_CFG_SHUTDOWN_FLAG_IMMEDIATE;

	error = corosync_cfg_try_shutdown(cman_inst->cfg_handle, cfg_flags);

	return error;
}

int cman_replyto_shutdown(cman_handle_t handle, int flags)
{
	struct cman_inst *cman_inst;
	int error;

	cman_inst = (struct cman_inst *)handle;
	VALIDATE_HANDLE(cman_inst);

	if (!cman_inst->cfg_handle) {
		if (corosync_cfg_initialize(&cman_inst->cfg_handle, &cfg_callbacks) != SA_AIS_OK) {
			errno = ENOMEM;
			return -1;
		}
	}

	error = corosync_cfg_replyto_shutdown(cman_inst->cfg_handle, flags);

	return error;
}

int cman_killnode(cman_handle_t handle, unsigned int nodeid)
{
	struct cman_inst *cman_inst;
	int error;

	cman_inst = (struct cman_inst *)handle;
	VALIDATE_HANDLE(cman_inst);

	if (!cman_inst->cfg_handle) {
		if (corosync_cfg_initialize(&cman_inst->cfg_handle, &cfg_callbacks) != SA_AIS_OK) {
			errno = ENOMEM;
			return -1;
		}
	}

	error = corosync_cfg_kill_node(cman_inst->cfg_handle, nodeid, "Killed by cman_tool");

	return error;
}

int cman_set_votes(cman_handle_t handle, int votes, int nodeid)
{
	struct cman_inst *cman_inst;
	int error;

	cman_inst = (struct cman_inst *)handle;
	VALIDATE_HANDLE(cman_inst);

	if (!cmanquorum_check_and_start(cman_inst))
		return -1;

	error = cmanquorum_setvotes(cman_inst->cmq_handle, nodeid, votes);

	return error;
}

int cman_set_expected_votes(cman_handle_t handle, int expected)
{
	struct cman_inst *cman_inst;
	int error;

	cman_inst = (struct cman_inst *)handle;
	VALIDATE_HANDLE(cman_inst);

	if (!cmanquorum_check_and_start(cman_inst))
		return -1;

	error = cmanquorum_setexpected(cman_inst->cmq_handle, expected);

	return error;
}

int cman_get_fd (
	cman_handle_t handle)
{
	struct cman_inst *cman_inst;
	int fd;
	int error;

	cman_inst = (struct cman_inst *)handle;
	VALIDATE_HANDLE(cman_inst);

	fd = cman_inst->dispatch_fd;

	return fd;
}

int cman_getprivdata(
	cman_handle_t handle,
	void **context)
{
	SaAisErrorT error;
	struct cman_inst *cman_inst;

	cman_inst = (struct cman_inst *)handle;
	VALIDATE_HANDLE(cman_inst);

	*context = cman_inst->privdata;

	return (SA_AIS_OK);
}

int cman_setprivdata(
	cman_handle_t handle,
	void *context)
{
	SaAisErrorT error;
	struct cman_inst *cman_inst;

	cman_inst = (struct cman_inst *)handle;
	VALIDATE_HANDLE(cman_inst);

	cman_inst->privdata = context;

	return (SA_AIS_OK);
}


int cman_register_quorum_device(cman_handle_t handle, char *name, int votes)
{
	struct cman_inst *cman_inst;
	int error;

	cman_inst = (struct cman_inst *)handle;
	VALIDATE_HANDLE(cman_inst);

	if (!cmanquorum_check_and_start(cman_inst))
		return -1;

	error = cmanquorum_qdisk_register(cman_inst->cmq_handle, name, votes);

	return error;
}

int cman_unregister_quorum_device(cman_handle_t handle)
{
	struct cman_inst *cman_inst;
	int error;

	cman_inst = (struct cman_inst *)handle;
	VALIDATE_HANDLE(cman_inst);

	if (!cmanquorum_check_and_start(cman_inst))
		return -1;

	error = cmanquorum_qdisk_unregister(cman_inst->cmq_handle);

	return error;
}
int cman_poll_quorum_device(cman_handle_t handle, int isavailable)
{
	struct cman_inst *cman_inst;
	int error;

	cman_inst = (struct cman_inst *)handle;
	VALIDATE_HANDLE(cman_inst);

	if (!cmanquorum_check_and_start(cman_inst))
		return -1;

	error = cmanquorum_qdisk_poll(cman_inst->cmq_handle, 1);

	return error;
}

int cman_get_quorum_device(cman_handle_t handle, struct cman_qdev_info *info)
{
	struct cman_inst *cman_inst;
	int error;
	struct cmanquorum_qdisk_info qinfo;

	cman_inst = (struct cman_inst *)handle;
	VALIDATE_HANDLE(cman_inst);

	if (!cmanquorum_check_and_start(cman_inst))
		return -1;

	error = cmanquorum_qdisk_getinfo(cman_inst->cmq_handle, &qinfo);

	if (!error) {
		info->qi_state = qinfo.state;
		info->qi_votes = qinfo.votes;
		strcpy(info->qi_name, qinfo.name);
	}

	return error;
}

int cman_set_dirty(cman_handle_t handle)
{
	struct cman_inst *cman_inst;
	int error;

	cman_inst = (struct cman_inst *)handle;
	VALIDATE_HANDLE(cman_inst);

	if (!cmanquorum_check_and_start(cman_inst))
		return -1;

	error = cmanquorum_setdirty(cman_inst->cmq_handle);

	return error;
}


struct res_overlay {
	mar_res_header_t header __attribute__((aligned(8)));
	char data[512000];
};


int cman_dispatch (
	cman_handle_t handle,
	int dispatch_types)
{
	struct pollfd ufds;
	int timeout = -1;
	SaAisErrorT error;
	int cont = 1; /* always continue do loop except when set to 0 */
	int dispatch_avail;
	struct cman_inst *cman_inst;
	struct res_overlay dispatch_data;
	struct res_lib_cman_sendmsg *res_lib_cman_sendmsg;

	if (dispatch_types != SA_DISPATCH_ONE &&
		dispatch_types != SA_DISPATCH_ALL &&
		dispatch_types != SA_DISPATCH_BLOCKING) {

		return (SA_AIS_ERR_INVALID_PARAM);
	}

	cman_inst = (struct cman_inst *)handle;
	VALIDATE_HANDLE(cman_inst);

	/*
	 * Timeout instantly for SA_DISPATCH_ONE or SA_DISPATCH_ALL and
	 * wait indefinately for SA_DISPATCH_BLOCKING
	 */
	if (dispatch_types == SA_DISPATCH_ALL) {
		timeout = 0;
	}

	do {
		ufds.fd = cman_inst->dispatch_fd;
		ufds.events = POLLIN;
		ufds.revents = 0;

		pthread_mutex_lock (&cman_inst->dispatch_mutex);

		error = saPollRetry (&ufds, 1, timeout);
		if (error != SA_AIS_OK) {
			goto error_unlock;
		}

		/*
		 * Handle has been finalized in another thread
		 */
		if (cman_inst->finalize == 1) {
			error = SA_AIS_OK;
			goto error_unlock;
		}

		if ((ufds.revents & (POLLERR|POLLHUP|POLLNVAL)) != 0) {
			error = SA_AIS_ERR_BAD_HANDLE;
			goto error_unlock;
		}

		dispatch_avail = ufds.revents & POLLIN;
		if (dispatch_avail == 0 && dispatch_types == SA_DISPATCH_ALL) {
			pthread_mutex_unlock (&cman_inst->dispatch_mutex);
			break; /* exit do while cont is 1 loop */
		} else
		if (dispatch_avail == 0) {
			pthread_mutex_unlock (&cman_inst->dispatch_mutex);
			continue; /* next poll */
		}

		if (ufds.revents & POLLIN) {
			error = saRecvRetry (cman_inst->dispatch_fd, &dispatch_data.header,
				sizeof (mar_res_header_t));
			if (error != SA_AIS_OK) {
				goto error_unlock;
			}
			if (dispatch_data.header.size > sizeof (mar_res_header_t)) {
				error = saRecvRetry (cman_inst->dispatch_fd, &dispatch_data.data,
					dispatch_data.header.size - sizeof (mar_res_header_t));
				if (error != SA_AIS_OK) {
					goto error_unlock;
				}
			}
		} else {
			pthread_mutex_unlock (&cman_inst->dispatch_mutex);
			continue;
		}

		/*
		 * Make copy of callbacks, message data, unlock instance, and call callback
		 * A risk of this dispatch method is that the callback routines may
		 * operate at the same time that cman_finalize has been called in another thread.
		 */
		pthread_mutex_unlock (&cman_inst->dispatch_mutex);

		/*
		 * Dispatch incoming message
		 */
		switch (dispatch_data.header.id) {

		case MESSAGE_RES_CMAN_SENDMSG:
			if (cman_inst->data_callback == NULL) {
				continue;
			}
			res_lib_cman_sendmsg = (struct res_lib_cman_sendmsg *)&dispatch_data;

			cman_inst->data_callback ( handle,
						   cman_inst->privdata,
						   res_lib_cman_sendmsg->message,
						   res_lib_cman_sendmsg->msglen,
						   res_lib_cman_sendmsg->from_port,
						   res_lib_cman_sendmsg->from_node);
			break;

		default:
			error = SA_AIS_ERR_LIBRARY;
			goto error_put;
			break;
		}

		/*
		 * Determine if more messages should be processed
		 * */
		switch (dispatch_types) {
		case SA_DISPATCH_ONE:
			cont = 0;
			break;
		case SA_DISPATCH_ALL:
			break;
		case SA_DISPATCH_BLOCKING:
			break;
		}
	} while (cont);

	goto error_put;

error_unlock:
	pthread_mutex_unlock (&cman_inst->dispatch_mutex);

error_put:
	return (error);
}


int cman_get_node_count(cman_handle_t handle)
{
	struct cman_inst *cman_inst;
	int error;

	cman_inst = (struct cman_inst *)handle;
	VALIDATE_HANDLE(cman_inst);

	if (!cmanquorum_check_and_start(cman_inst))
		return -1;

	cmanquorum_trackstart(cman_inst->cmq_handle, CS_TRACK_CURRENT);

	return error;
}

int cman_get_nodes(cman_handle_t handle, int maxnodes, int *retnodes, cman_node_t *nodes)
{}
int cman_get_disallowed_nodes(cman_handle_t handle, int maxnodes, int *retnodes, cman_node_t *nodes)
{}
int cman_get_node(cman_handle_t handle, int nodeid, cman_node_t *node)
{}
