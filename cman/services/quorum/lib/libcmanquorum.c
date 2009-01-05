/*
 * Provides a quorum API using the corosync executive
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

#include <corosync/mar_gen.h>
#include <corosync/ipc_gen.h>
#include <corosync/ais_util.h>
#include "corosync/cmanquorum.h"
#include "corosync/ipc_cmanquorum.h"

struct cmanquorum_inst {
	int response_fd;
	int dispatch_fd;
	int finalize;
	void *context;
	cmanquorum_callbacks_t callbacks;
	pthread_mutex_t response_mutex;
	pthread_mutex_t dispatch_mutex;
};

static void cmanquorum_instance_destructor (void *instance);

static struct saHandleDatabase cmanquorum_handle_t_db = {
	.handleCount		        = 0,
	.handles			= 0,
	.mutex				= PTHREAD_MUTEX_INITIALIZER,
	.handleInstanceDestructor	= cmanquorum_instance_destructor
};

/*
 * Clean up function for a quorum instance (cmanquorum_initialize) handle
 */
static void cmanquorum_instance_destructor (void *instance)
{
	struct cmanquorum_inst *cmanquorum_inst = instance;

	pthread_mutex_destroy (&cmanquorum_inst->response_mutex);
}

cs_error_t cmanquorum_initialize (
	cmanquorum_handle_t *handle,
	cmanquorum_callbacks_t *callbacks)
{
	cs_error_t error;
	struct cmanquorum_inst *cmanquorum_inst;

	error = saHandleCreate (&cmanquorum_handle_t_db, sizeof (struct cmanquorum_inst), handle);
	if (error != CS_OK) {
		goto error_no_destroy;
	}

	error = saHandleInstanceGet (&cmanquorum_handle_t_db, *handle, (void *)&cmanquorum_inst);
	if (error != CS_OK) {
		goto error_destroy;
	}

	error = saServiceConnect (&cmanquorum_inst->dispatch_fd,
				  &cmanquorum_inst->response_fd,
				  CMANQUORUM_SERVICE);
	if (error != CS_OK) {
		goto error_put_destroy;
	}

	pthread_mutex_init (&cmanquorum_inst->response_mutex, NULL);
	pthread_mutex_init (&cmanquorum_inst->dispatch_mutex, NULL);
	if (callbacks)
		memcpy(&cmanquorum_inst->callbacks, callbacks, sizeof (callbacks));
	else
		memset(&cmanquorum_inst->callbacks, 0, sizeof (callbacks));

	saHandleInstancePut (&cmanquorum_handle_t_db, *handle);

	return (CS_OK);

error_put_destroy:
	saHandleInstancePut (&cmanquorum_handle_t_db, *handle);
error_destroy:
	saHandleDestroy (&cmanquorum_handle_t_db, *handle);
error_no_destroy:
	return (error);
}

cs_error_t cmanquorum_finalize (
	cmanquorum_handle_t handle)
{
	struct cmanquorum_inst *cmanquorum_inst;
	cs_error_t error;

	error = saHandleInstanceGet (&cmanquorum_handle_t_db, handle, (void *)&cmanquorum_inst);
	if (error != CS_OK) {
		return (error);
	}

	pthread_mutex_lock (&cmanquorum_inst->response_mutex);

	/*
	 * Another thread has already started finalizing
	 */
	if (cmanquorum_inst->finalize) {
		pthread_mutex_unlock (&cmanquorum_inst->response_mutex);
		saHandleInstancePut (&cmanquorum_handle_t_db, handle);
		return (CS_ERR_BAD_HANDLE);
	}

	cmanquorum_inst->finalize = 1;

	pthread_mutex_unlock (&cmanquorum_inst->response_mutex);

	saHandleDestroy (&cmanquorum_handle_t_db, handle);

	/*
	 * Disconnect from the server
	 */
	if (cmanquorum_inst->response_fd != -1) {
		shutdown(cmanquorum_inst->response_fd, 0);
		close(cmanquorum_inst->response_fd);
	}
	saHandleInstancePut (&cmanquorum_handle_t_db, handle);

	return (CS_OK);
}


cs_error_t cmanquorum_getinfo (
	cmanquorum_handle_t handle,
	int nodeid,
	struct cmanquorum_info *info)
{
	cs_error_t error;
	struct cmanquorum_inst *cmanquorum_inst;
	struct iovec iov[2];
	struct req_lib_cmanquorum_getinfo req_lib_cmanquorum_getinfo;
	struct res_lib_cmanquorum_getinfo res_lib_cmanquorum_getinfo;

	error = saHandleInstanceGet (&cmanquorum_handle_t_db, handle, (void *)&cmanquorum_inst);
	if (error != CS_OK) {
		return (error);
	}

	pthread_mutex_lock (&cmanquorum_inst->response_mutex);

	req_lib_cmanquorum_getinfo.header.size = sizeof (struct req_lib_cmanquorum_getinfo);
	req_lib_cmanquorum_getinfo.header.id = MESSAGE_REQ_CMANQUORUM_GETINFO;
	req_lib_cmanquorum_getinfo.nodeid = nodeid;

	iov[0].iov_base = (char *)&req_lib_cmanquorum_getinfo;
	iov[0].iov_len = sizeof (struct req_lib_cmanquorum_getinfo);

	error = saSendMsgReceiveReply (cmanquorum_inst->response_fd, iov, 1,
		&res_lib_cmanquorum_getinfo, sizeof (struct res_lib_cmanquorum_getinfo));

	pthread_mutex_unlock (&cmanquorum_inst->response_mutex);

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_cmanquorum_getinfo.header.error;

	info->node_id = res_lib_cmanquorum_getinfo.nodeid;
	info->node_votes = res_lib_cmanquorum_getinfo.votes;
	info->node_expected_votes = res_lib_cmanquorum_getinfo.expected_votes;
	info->highest_expected = res_lib_cmanquorum_getinfo.highest_expected;
	info->total_votes = res_lib_cmanquorum_getinfo.total_votes;
	info->quorum = res_lib_cmanquorum_getinfo.quorum;
	info->flags = res_lib_cmanquorum_getinfo.flags;

error_exit:
	saHandleInstancePut (&cmanquorum_handle_t_db, handle);

	return (error);
}

cs_error_t cmanquorum_setexpected (
	cmanquorum_handle_t handle,
	unsigned int expected_votes)
{
	cs_error_t error;
	struct cmanquorum_inst *cmanquorum_inst;
	struct iovec iov[2];
	struct req_lib_cmanquorum_setexpected req_lib_cmanquorum_setexpected;
	struct res_lib_cmanquorum_status res_lib_cmanquorum_status;

	error = saHandleInstanceGet (&cmanquorum_handle_t_db, handle, (void *)&cmanquorum_inst);
	if (error != CS_OK) {
		return (error);
	}

	pthread_mutex_lock (&cmanquorum_inst->response_mutex);

	req_lib_cmanquorum_setexpected.header.size = sizeof (struct req_lib_cmanquorum_setexpected);
	req_lib_cmanquorum_setexpected.header.id = MESSAGE_REQ_CMANQUORUM_SETEXPECTED;
	req_lib_cmanquorum_setexpected.expected_votes = expected_votes;

	iov[0].iov_base = (char *)&req_lib_cmanquorum_setexpected;
	iov[0].iov_len = sizeof (struct req_lib_cmanquorum_setexpected);

	error = saSendMsgReceiveReply (cmanquorum_inst->response_fd, iov, 1,
		&res_lib_cmanquorum_status, sizeof (struct res_lib_cmanquorum_status));

	pthread_mutex_unlock (&cmanquorum_inst->response_mutex);

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_cmanquorum_status.header.error;

error_exit:
	saHandleInstancePut (&cmanquorum_handle_t_db, handle);

	return (error);
}

cs_error_t cmanquorum_setvotes (
	cmanquorum_handle_t handle,
	int nodeid,
	unsigned int votes)
{
	cs_error_t error;
	struct cmanquorum_inst *cmanquorum_inst;
	struct iovec iov[2];
	struct req_lib_cmanquorum_setvotes req_lib_cmanquorum_setvotes;
	struct res_lib_cmanquorum_status res_lib_cmanquorum_status;

	error = saHandleInstanceGet (&cmanquorum_handle_t_db, handle, (void *)&cmanquorum_inst);
	if (error != CS_OK) {
		return (error);
	}

	pthread_mutex_lock (&cmanquorum_inst->response_mutex);

	req_lib_cmanquorum_setvotes.header.size = sizeof (struct req_lib_cmanquorum_setvotes);
	req_lib_cmanquorum_setvotes.header.id = MESSAGE_REQ_CMANQUORUM_SETVOTES;
	req_lib_cmanquorum_setvotes.nodeid = nodeid;
	req_lib_cmanquorum_setvotes.votes = votes;

	iov[0].iov_base = (char *)&req_lib_cmanquorum_setvotes;
	iov[0].iov_len = sizeof (struct req_lib_cmanquorum_setvotes);

	error = saSendMsgReceiveReply (cmanquorum_inst->response_fd, iov, 1,
		&res_lib_cmanquorum_status, sizeof (struct res_lib_cmanquorum_status));

	pthread_mutex_unlock (&cmanquorum_inst->response_mutex);

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_cmanquorum_status.header.error;

error_exit:
	saHandleInstancePut (&cmanquorum_handle_t_db, handle);

	return (error);
}

cs_error_t cmanquorum_qdisk_register (
	cmanquorum_handle_t handle,
	char *name,
	unsigned int votes)
{
	cs_error_t error;
	struct cmanquorum_inst *cmanquorum_inst;
	struct iovec iov[2];
	struct req_lib_cmanquorum_qdisk_register req_lib_cmanquorum_qdisk_register;
	struct res_lib_cmanquorum_status res_lib_cmanquorum_status;

	if (strlen(name) > CMANQUORUM_MAX_QDISK_NAME_LEN)
		return CS_ERR_INVALID_PARAM;

	error = saHandleInstanceGet (&cmanquorum_handle_t_db, handle, (void *)&cmanquorum_inst);
	if (error != CS_OK) {
		return (error);
	}

	pthread_mutex_lock (&cmanquorum_inst->response_mutex);

	req_lib_cmanquorum_qdisk_register.header.size = sizeof (struct req_lib_cmanquorum_qdisk_register);
	req_lib_cmanquorum_qdisk_register.header.id = MESSAGE_REQ_CMANQUORUM_QDISK_REGISTER;
	strcpy(req_lib_cmanquorum_qdisk_register.name, name);
	req_lib_cmanquorum_qdisk_register.votes = votes;

	iov[0].iov_base = (char *)&req_lib_cmanquorum_qdisk_register;
	iov[0].iov_len = sizeof (struct req_lib_cmanquorum_qdisk_register);

	error = saSendMsgReceiveReply (cmanquorum_inst->response_fd, iov, 1,
		&res_lib_cmanquorum_status, sizeof (struct res_lib_cmanquorum_status));

	pthread_mutex_unlock (&cmanquorum_inst->response_mutex);

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_cmanquorum_status.header.error;

error_exit:
	saHandleInstancePut (&cmanquorum_handle_t_db, handle);

	return (error);
}

cs_error_t cmanquorum_qdisk_poll (
	cmanquorum_handle_t handle,
	int state)
{
	cs_error_t error;
	struct cmanquorum_inst *cmanquorum_inst;
	struct iovec iov[2];
	struct req_lib_cmanquorum_qdisk_poll req_lib_cmanquorum_qdisk_poll;
	struct res_lib_cmanquorum_status res_lib_cmanquorum_status;

	error = saHandleInstanceGet (&cmanquorum_handle_t_db, handle, (void *)&cmanquorum_inst);
	if (error != CS_OK) {
		return (error);
	}

	pthread_mutex_lock (&cmanquorum_inst->response_mutex);

	req_lib_cmanquorum_qdisk_poll.header.size = sizeof (struct req_lib_cmanquorum_qdisk_poll);
	req_lib_cmanquorum_qdisk_poll.header.id = MESSAGE_REQ_CMANQUORUM_QDISK_POLL;
	req_lib_cmanquorum_qdisk_poll.state = state;

	iov[0].iov_base = (char *)&req_lib_cmanquorum_qdisk_poll;
	iov[0].iov_len = sizeof (struct req_lib_cmanquorum_qdisk_poll);

	error = saSendMsgReceiveReply (cmanquorum_inst->response_fd, iov, 1,
		&res_lib_cmanquorum_status, sizeof (struct res_lib_cmanquorum_status));

	pthread_mutex_unlock (&cmanquorum_inst->response_mutex);

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_cmanquorum_status.header.error;

error_exit:
	saHandleInstancePut (&cmanquorum_handle_t_db, handle);

	return (error);
}

cs_error_t cmanquorum_qdisk_unregister (
	cmanquorum_handle_t handle)
{
	cs_error_t error;
	struct cmanquorum_inst *cmanquorum_inst;
	struct iovec iov[2];
	struct req_lib_cmanquorum_general req_lib_cmanquorum_general;
	struct res_lib_cmanquorum_status res_lib_cmanquorum_status;

	error = saHandleInstanceGet (&cmanquorum_handle_t_db, handle, (void *)&cmanquorum_inst);
	if (error != CS_OK) {
		return (error);
	}

	pthread_mutex_lock (&cmanquorum_inst->response_mutex);

	req_lib_cmanquorum_general.header.size = sizeof (struct req_lib_cmanquorum_general);
	req_lib_cmanquorum_general.header.id = MESSAGE_REQ_CMANQUORUM_QDISK_UNREGISTER;

	iov[0].iov_base = (char *)&req_lib_cmanquorum_general;
	iov[0].iov_len = sizeof (struct req_lib_cmanquorum_general);

	error = saSendMsgReceiveReply (cmanquorum_inst->response_fd, iov, 1,
		&res_lib_cmanquorum_status, sizeof (struct res_lib_cmanquorum_status));

	pthread_mutex_unlock (&cmanquorum_inst->response_mutex);

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_cmanquorum_status.header.error;

error_exit:
	saHandleInstancePut (&cmanquorum_handle_t_db, handle);

	return (error);
}



cs_error_t cmanquorum_qdisk_getinfo (
	cmanquorum_handle_t handle,
	struct cmanquorum_qdisk_info *qinfo)
{
	cs_error_t error;
	struct cmanquorum_inst *cmanquorum_inst;
	struct iovec iov[2];
	struct req_lib_cmanquorum_general req_lib_cmanquorum_general;
	struct res_lib_cmanquorum_qdisk_getinfo res_lib_cmanquorum_qdisk_getinfo;

	error = saHandleInstanceGet (&cmanquorum_handle_t_db, handle, (void *)&cmanquorum_inst);
	if (error != CS_OK) {
		return (error);
	}

	pthread_mutex_lock (&cmanquorum_inst->response_mutex);

	req_lib_cmanquorum_general.header.size = sizeof (struct req_lib_cmanquorum_general);
	req_lib_cmanquorum_general.header.id = MESSAGE_REQ_CMANQUORUM_QDISK_GETINFO;

	iov[0].iov_base = (char *)&req_lib_cmanquorum_general;
	iov[0].iov_len = sizeof (struct req_lib_cmanquorum_general);

	error = saSendMsgReceiveReply (cmanquorum_inst->response_fd, iov, 1,
		&res_lib_cmanquorum_qdisk_getinfo, sizeof (struct res_lib_cmanquorum_qdisk_getinfo));

	pthread_mutex_unlock (&cmanquorum_inst->response_mutex);

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_cmanquorum_qdisk_getinfo.header.error;

	qinfo->votes = res_lib_cmanquorum_qdisk_getinfo.votes;
	qinfo->state = res_lib_cmanquorum_qdisk_getinfo.state;
	strcpy(qinfo->name, res_lib_cmanquorum_qdisk_getinfo.name);


error_exit:
	saHandleInstancePut (&cmanquorum_handle_t_db, handle);

	return (error);
}

cs_error_t cmanquorum_setdirty (
	cmanquorum_handle_t handle)
{
	cs_error_t error;
	struct cmanquorum_inst *cmanquorum_inst;
	struct iovec iov[2];
	struct req_lib_cmanquorum_general req_lib_cmanquorum_general;
	struct res_lib_cmanquorum_status res_lib_cmanquorum_status;

	error = saHandleInstanceGet (&cmanquorum_handle_t_db, handle, (void *)&cmanquorum_inst);
	if (error != CS_OK) {
		return (error);
	}

	pthread_mutex_lock (&cmanquorum_inst->response_mutex);

	req_lib_cmanquorum_general.header.size = sizeof (struct req_lib_cmanquorum_general);
	req_lib_cmanquorum_general.header.id = MESSAGE_REQ_CMANQUORUM_SETDIRTY;

	iov[0].iov_base = (char *)&req_lib_cmanquorum_general;
	iov[0].iov_len = sizeof (struct req_lib_cmanquorum_general);

	error = saSendMsgReceiveReply (cmanquorum_inst->response_fd, iov, 1,
		&res_lib_cmanquorum_status, sizeof (struct res_lib_cmanquorum_status));

	pthread_mutex_unlock (&cmanquorum_inst->response_mutex);

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_cmanquorum_status.header.error;

error_exit:
	saHandleInstancePut (&cmanquorum_handle_t_db, handle);

	return (error);
}

cs_error_t cmanquorum_leaving (
	cmanquorum_handle_t handle)
{
	cs_error_t error;
	struct cmanquorum_inst *cmanquorum_inst;
	struct iovec iov[2];
	struct req_lib_cmanquorum_general req_lib_cmanquorum_general;
	struct res_lib_cmanquorum_status res_lib_cmanquorum_status;

	error = saHandleInstanceGet (&cmanquorum_handle_t_db, handle, (void *)&cmanquorum_inst);
	if (error != CS_OK) {
		return (error);
	}

	pthread_mutex_lock (&cmanquorum_inst->response_mutex);

	req_lib_cmanquorum_general.header.size = sizeof (struct req_lib_cmanquorum_general);
	req_lib_cmanquorum_general.header.id = MESSAGE_REQ_CMANQUORUM_LEAVING;

	iov[0].iov_base = (char *)&req_lib_cmanquorum_general;
	iov[0].iov_len = sizeof (struct req_lib_cmanquorum_general);

	error = saSendMsgReceiveReply (cmanquorum_inst->response_fd, iov, 1,
		&res_lib_cmanquorum_status, sizeof (struct res_lib_cmanquorum_status));

	pthread_mutex_unlock (&cmanquorum_inst->response_mutex);

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_cmanquorum_status.header.error;

error_exit:
	saHandleInstancePut (&cmanquorum_handle_t_db, handle);

	return (error);
}

cs_error_t cmanquorum_killnode (
	cmanquorum_handle_t handle,
	int nodeid,
	unsigned int reason)
{
	cs_error_t error;
	struct cmanquorum_inst *cmanquorum_inst;
	struct iovec iov[2];
	struct req_lib_cmanquorum_killnode req_lib_cmanquorum_killnode;
	struct res_lib_cmanquorum_status res_lib_cmanquorum_status;

	error = saHandleInstanceGet (&cmanquorum_handle_t_db, handle, (void *)&cmanquorum_inst);
	if (error != CS_OK) {
		return (error);
	}

	pthread_mutex_lock (&cmanquorum_inst->response_mutex);

	req_lib_cmanquorum_killnode.header.size = sizeof (struct req_lib_cmanquorum_killnode);
	req_lib_cmanquorum_killnode.header.id = MESSAGE_REQ_CMANQUORUM_KILLNODE;
	req_lib_cmanquorum_killnode.nodeid = nodeid;
	req_lib_cmanquorum_killnode.reason = reason;

	iov[0].iov_base = (char *)&req_lib_cmanquorum_killnode;
	iov[0].iov_len = sizeof (struct req_lib_cmanquorum_killnode);

	error = saSendMsgReceiveReply (cmanquorum_inst->response_fd, iov, 1,
		&res_lib_cmanquorum_status, sizeof (struct res_lib_cmanquorum_status));

	pthread_mutex_unlock (&cmanquorum_inst->response_mutex);

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_cmanquorum_status.header.error;

error_exit:
	saHandleInstancePut (&cmanquorum_handle_t_db, handle);

	return (error);
}

cs_error_t cmanquorum_trackstart (
	cmanquorum_handle_t handle,
	unsigned int flags )
{
	cs_error_t error;
	struct cmanquorum_inst *cmanquorum_inst;
	struct iovec iov[2];
	struct req_lib_cmanquorum_trackstart req_lib_cmanquorum_trackstart;
	struct res_lib_cmanquorum_status res_lib_cmanquorum_status;

	error = saHandleInstanceGet (&cmanquorum_handle_t_db, handle, (void *)&cmanquorum_inst);
	if (error != CS_OK) {
		return (error);
	}

	pthread_mutex_lock (&cmanquorum_inst->response_mutex);

	req_lib_cmanquorum_trackstart.header.size = sizeof (struct req_lib_cmanquorum_trackstart);
	req_lib_cmanquorum_trackstart.header.id = MESSAGE_REQ_CMANQUORUM_TRACKSTART;
	req_lib_cmanquorum_trackstart.track_flags = flags;

	iov[0].iov_base = (char *)&req_lib_cmanquorum_trackstart;
	iov[0].iov_len = sizeof (struct req_lib_cmanquorum_trackstart);

	error = saSendMsgReceiveReply (cmanquorum_inst->response_fd, iov, 1,
		&res_lib_cmanquorum_status, sizeof (struct res_lib_cmanquorum_status));

	pthread_mutex_unlock (&cmanquorum_inst->response_mutex);

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_cmanquorum_status.header.error;

error_exit:
	saHandleInstancePut (&cmanquorum_handle_t_db, handle);

	return (error);
}

cs_error_t cmanquorum_trackstop (
	cmanquorum_handle_t handle)
{
	cs_error_t error;
	struct cmanquorum_inst *cmanquorum_inst;
	struct iovec iov[2];
	struct req_lib_cmanquorum_general req_lib_cmanquorum_general;
	struct res_lib_cmanquorum_status res_lib_cmanquorum_status;

	error = saHandleInstanceGet (&cmanquorum_handle_t_db, handle, (void *)&cmanquorum_inst);
	if (error != CS_OK) {
		return (error);
	}

	pthread_mutex_lock (&cmanquorum_inst->response_mutex);

	req_lib_cmanquorum_general.header.size = sizeof (struct req_lib_cmanquorum_general);
	req_lib_cmanquorum_general.header.id = MESSAGE_REQ_CMANQUORUM_TRACKSTOP;

	iov[0].iov_base = (char *)&req_lib_cmanquorum_general;
	iov[0].iov_len = sizeof (struct req_lib_cmanquorum_general);

	error = saSendMsgReceiveReply (cmanquorum_inst->response_fd, iov, 1,
		&res_lib_cmanquorum_status, sizeof (struct res_lib_cmanquorum_status));

	pthread_mutex_unlock (&cmanquorum_inst->response_mutex);

	if (error != CS_OK) {
		goto error_exit;
	}

	error = res_lib_cmanquorum_status.header.error;

error_exit:
	saHandleInstancePut (&cmanquorum_handle_t_db, handle);

	return (error);
}


cs_error_t cmanquorum_context_get (
	cmanquorum_handle_t handle,
	void **context)
{
	cs_error_t error;
	struct cmanquorum_inst *cmanquorum_inst;

	error = saHandleInstanceGet (&cmanquorum_handle_t_db, handle, (void *)&cmanquorum_inst);
	if (error != CS_OK) {
		return (error);
	}

	*context = cmanquorum_inst->context;

	saHandleInstancePut (&cmanquorum_handle_t_db, handle);

	return (CS_OK);
}

cs_error_t cmanquorum_context_set (
	cmanquorum_handle_t handle,
	void *context)
{
	cs_error_t error;
	struct cmanquorum_inst *cmanquorum_inst;

	error = saHandleInstanceGet (&cmanquorum_handle_t_db, handle, (void *)&cmanquorum_inst);
	if (error != CS_OK) {
		return (error);
	}

	cmanquorum_inst->context = context;

	saHandleInstancePut (&cmanquorum_handle_t_db, handle);

	return (CS_OK);
}


struct res_overlay {
	mar_res_header_t header __attribute__((aligned(8)));
	char data[512000];
};

cs_error_t cmanquorum_dispatch (
	cmanquorum_handle_t handle,
	cs_dispatch_flags_t dispatch_types)
{
	struct pollfd ufds;
	int timeout = -1;
	cs_error_t error;
	int cont = 1; /* always continue do loop except when set to 0 */
	int dispatch_avail;
	struct cmanquorum_inst *cmanquorum_inst;
	cmanquorum_callbacks_t callbacks;
	struct res_overlay dispatch_data;
	struct res_lib_cmanquorum_notification *res_lib_cmanquorum_notification;

	if (dispatch_types != CS_DISPATCH_ONE &&
		dispatch_types != CS_DISPATCH_ALL &&
		dispatch_types != CS_DISPATCH_BLOCKING) {

		return (CS_ERR_INVALID_PARAM);
	}

	error = saHandleInstanceGet (&cmanquorum_handle_t_db, handle,
		(void *)&cmanquorum_inst);
	if (error != CS_OK) {
		return (error);
	}

	/*
	 * Timeout instantly for CS_DISPATCH_ONE or CS_DISPATCH_ALL and
	 * wait indefinately for CS_DISPATCH_BLOCKING
	 */
	if (dispatch_types == CS_DISPATCH_ALL) {
		timeout = 0;
	}

	do {
		ufds.fd = cmanquorum_inst->dispatch_fd;
		ufds.events = POLLIN;
		ufds.revents = 0;

		pthread_mutex_lock (&cmanquorum_inst->dispatch_mutex);

		error = saPollRetry (&ufds, 1, timeout);
		if (error != CS_OK) {
			goto error_unlock;
		}

		/*
		 * Handle has been finalized in another thread
		 */
		if (cmanquorum_inst->finalize == 1) {
			error = CS_OK;
			goto error_unlock;
		}

		if ((ufds.revents & (POLLERR|POLLHUP|POLLNVAL)) != 0) {
			error = CS_ERR_BAD_HANDLE;
			goto error_unlock;
		}

		dispatch_avail = ufds.revents & POLLIN;
		if (dispatch_avail == 0 && dispatch_types == CS_DISPATCH_ALL) {
			pthread_mutex_unlock (&cmanquorum_inst->dispatch_mutex);
			break; /* exit do while cont is 1 loop */
		} else
		if (dispatch_avail == 0) {
			pthread_mutex_unlock (&cmanquorum_inst->dispatch_mutex);
			continue; /* next poll */
		}

		if (ufds.revents & POLLIN) {
			error = saRecvRetry (cmanquorum_inst->dispatch_fd, &dispatch_data.header,
				sizeof (mar_res_header_t));
			if (error != CS_OK) {
				goto error_unlock;
			}
			if (dispatch_data.header.size > sizeof (mar_res_header_t)) {
				error = saRecvRetry (cmanquorum_inst->dispatch_fd, &dispatch_data.data,
					dispatch_data.header.size - sizeof (mar_res_header_t));
				if (error != CS_OK) {
					goto error_unlock;
				}
			}
		} else {
			pthread_mutex_unlock (&cmanquorum_inst->dispatch_mutex);
			continue;
		}

		/*
		 * Make copy of callbacks, message data, unlock instance, and call callback
		 * A risk of this dispatch method is that the callback routines may
		 * operate at the same time that cmanquorum_finalize has been called in another thread.
		 */
		memcpy (&callbacks, &cmanquorum_inst->callbacks, sizeof (cmanquorum_callbacks_t));
		pthread_mutex_unlock (&cmanquorum_inst->dispatch_mutex);

		/*
		 * Dispatch incoming message
		 */
		switch (dispatch_data.header.id) {

		case MESSAGE_RES_CMANQUORUM_NOTIFICATION:
			if (callbacks.cmanquorum_notify_fn == NULL) {
				continue;
			}
			res_lib_cmanquorum_notification = (struct res_lib_cmanquorum_notification *)&dispatch_data;

			callbacks.cmanquorum_notify_fn ( handle,
						     res_lib_cmanquorum_notification->quorate,
						     res_lib_cmanquorum_notification->node_list_entries,
						     (cmanquorum_node_t *)res_lib_cmanquorum_notification->node_list );
				;
			break;

		default:
			error = CS_ERR_LIBRARY;
			goto error_put;
			break;
		}

		/*
		 * Determine if more messages should be processed
		 * */
		switch (dispatch_types) {
		case CS_DISPATCH_ONE:
			cont = 0;
			break;
		case CS_DISPATCH_ALL:
			break;
		case CS_DISPATCH_BLOCKING:
			break;
		}
	} while (cont);

	goto error_put;

error_unlock:
	pthread_mutex_unlock (&cmanquorum_inst->dispatch_mutex);

error_put:
	saHandleInstancePut (&cmanquorum_handle_t_db, handle);
	return (error);
}
