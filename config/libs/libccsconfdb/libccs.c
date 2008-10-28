#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <corosync/saAis.h>
#include <corosync/confdb.h>
#ifdef EXPERIMENTAL_BUILD
#include <time.h>
#endif

#include "ccs.h"
#include "ccs_internal.h"

#ifdef EXPERIMENTAL_BUILD
#ifndef CCS_HANDLE_TIMEOUT
#define CCS_HANDLE_TIMEOUT 60 /* 60 seconds */
#endif
#endif

#ifdef EXPERIMENTAL_BUILD
int ccs_persistent_conn = 0;
#endif

/* Callbacks are not supported - we will use them to update fullxml doc/ctx */
static confdb_callbacks_t callbacks = {
};

/* confdb helper functions */

static confdb_handle_t confdb_connect(void)
{
	confdb_handle_t handle = 0;

	if (confdb_initialize (&handle, &callbacks) != SA_AIS_OK) {
		errno = ENOMEM;
		return -1;
	}

	return handle;
}

static int confdb_disconnect(confdb_handle_t handle)
{
	if (confdb_finalize (handle) != CONFDB_OK) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}

static unsigned int find_libccs_handle(confdb_handle_t handle)
{
	unsigned int libccs_handle = 0;

	if (confdb_object_find_start(handle, OBJECT_PARENT_HANDLE) != SA_AIS_OK) {
		errno = ENOMEM;
		return -1;
	}

	if (confdb_object_find(handle, OBJECT_PARENT_HANDLE, "libccs", strlen("libccs"), &libccs_handle) != SA_AIS_OK) {
		errno = ENOENT;
		return -1;
	}

	confdb_object_find_destroy(handle, OBJECT_PARENT_HANDLE);

	return libccs_handle;
}

static unsigned int find_ccs_handle(confdb_handle_t handle, int ccs_handle)
{
	int res, datalen = 0, found = 0;
	unsigned int libccs_handle = 0, connection_handle = 0;
	char data[128];

	libccs_handle = find_libccs_handle(handle);
	if (libccs_handle == -1)
		return -1;

	if (confdb_object_find_start(handle, libccs_handle) != SA_AIS_OK) {
		errno = ENOMEM;
		return -1;
	}

	while (confdb_object_find(handle, libccs_handle, "connection", strlen("connection"), &connection_handle) == SA_AIS_OK) {
		memset(data, 0, sizeof(data));
		if (confdb_key_get(handle, connection_handle, "ccs_handle", strlen("ccs_handle"), data, &datalen) == SA_AIS_OK) {
			res = atoi(data);
			if (res == ccs_handle) {
				found = 1;
				break;
			}
		}
	}

	confdb_object_find_destroy(handle, libccs_handle);

	if (found) {
		return connection_handle;
	} else {
		errno = ENOENT;
		return -1;
	}
}

static int destroy_ccs_handle(confdb_handle_t handle, unsigned int connection_handle)
{
	if (confdb_object_destroy(handle, connection_handle) != SA_AIS_OK) {
		errno = EINVAL;
		return -1;
	}

	return 0;
}

static unsigned int create_ccs_handle(confdb_handle_t handle, int ccs_handle, int fullxpath)
{
	unsigned int libccs_handle = 0, connection_handle = 0;
	char buf[128];
#ifdef EXPERIMENTAL_BUILD
	time_t current_time;
#endif

	libccs_handle = find_libccs_handle(handle);
	if (libccs_handle == -1)
		return -1;

	if (confdb_object_create(handle, libccs_handle, "connection", strlen("connection"), &connection_handle) != SA_AIS_OK) {
		errno = ENOMEM;
		return -1;
	}

	memset(buf, 0, sizeof(buf));
	snprintf(buf, sizeof(buf), "%d", ccs_handle);
	if (confdb_key_create(handle, connection_handle, "ccs_handle", strlen("ccs_handle"), buf, strlen(buf) + 1) != SA_AIS_OK) {
		destroy_ccs_handle(handle, connection_handle);
		errno = ENOMEM;
		return -1;
	}

	memset(buf, 0, sizeof(buf));
	snprintf(buf, sizeof(buf), "%d", fullxpath);
	if (confdb_key_create(handle, connection_handle, "fullxpath", strlen("fullxpath"), buf, strlen(buf) + 1) != SA_AIS_OK) {
		destroy_ccs_handle(handle, connection_handle);
		errno = ENOMEM;
		return -1;
	}

#ifdef EXPERIMENTAL_BUILD
	if (ccs_persistent_conn)
		return connection_handle;

	memset(buf, 0, sizeof(buf));
	time(&current_time);
	memcpy(buf, &current_time, sizeof(time_t));
	if (confdb_key_create(handle, connection_handle, "last_access", strlen("last_access"), buf, sizeof(time_t)) != SA_AIS_OK) {
		destroy_ccs_handle(handle, connection_handle);
		errno = ENOMEM;
		return -1;
	}
#endif

	return connection_handle;
}

static unsigned int get_ccs_handle(confdb_handle_t handle, int *ccs_handle, int fullxpath)
{
	unsigned int next_handle;
	unsigned int libccs_handle = 0;
	unsigned int ret = 0;

	libccs_handle = find_libccs_handle(handle);
	if (libccs_handle == -1)
		return -1;

	if (confdb_key_increment(handle, libccs_handle, "next_handle", strlen("next_handle"), &next_handle) == SA_AIS_OK) {
		ret = create_ccs_handle(handle, (int)next_handle, fullxpath);
		if (ret == -1) {
			*ccs_handle = -1;
			return ret;
		}

		*ccs_handle = (int)next_handle;
		return ret;
	}

	*ccs_handle = -1;
	errno = ENOMEM;
	return -1;
}

#ifdef EXPERIMENTAL_BUILD
static int clean_stalled_ccs_handles(confdb_handle_t handle)
{
	int datalen = 0;
	unsigned int libccs_handle = 0, connection_handle = 0;
	time_t current_time, stored_time;

	libccs_handle = find_libccs_handle(handle);
	if (libccs_handle == -1)
		return -1;

	if (confdb_object_find_start(handle, libccs_handle) != SA_AIS_OK) {
		errno = ENOMEM;
		return -1;
	}

	time(&current_time);

	while (confdb_object_find(handle, libccs_handle, "connection", strlen("connection"), &connection_handle) == SA_AIS_OK) {
		if (confdb_key_get(handle, connection_handle, "last_access", strlen("last_access"), &stored_time, &datalen) == SA_AIS_OK) {
			if ((current_time - stored_time) > CCS_HANDLE_TIMEOUT)
				destroy_ccs_handle(handle, connection_handle);
		}
	}

	confdb_object_find_destroy(handle, libccs_handle);

	return 0;
}
#endif

/**
 * ccs_connect
 *
 * Returns: ccs_desc on success, < 0 on failure
 */
int ccs_connect(void) {
	confdb_handle_t handle = 0;
	int ccs_handle = 0;

	handle = confdb_connect();
	if(handle == -1)
		return handle;

#ifdef EXPERIMENTAL_BUILD
	clean_stalled_ccs_handles(handle);
#endif

	get_ccs_handle(handle, &ccs_handle, fullxpath);
	if (ccs_handle < 0)
		goto fail;

	if (fullxpath) {
		if (xpathfull_init(handle, ccs_handle)) {
			ccs_disconnect(ccs_handle);
			return -1;
		}
	}

fail:
	confdb_disconnect(handle);

	return ccs_handle;
}

static int check_cluster_name(int ccs_handle, const char *cluster_name)
{
	confdb_handle_t handle = 0;
	unsigned int cluster_handle;
	char data[128];
	int found = 0, datalen = 0;

	handle = confdb_connect();
	if (handle < 0)
		return -1;

	if (confdb_object_find_start(handle, OBJECT_PARENT_HANDLE) != SA_AIS_OK) {
		errno = ENOMEM;
		return -1;
	}

	while (confdb_object_find(handle, OBJECT_PARENT_HANDLE, "cluster", strlen("cluster"), &cluster_handle) == SA_AIS_OK) {
		memset(data, 0, sizeof(data));
		if (confdb_key_get(handle, cluster_handle, "name", strlen("name"), data, &datalen) == SA_AIS_OK) {
			if(!strncmp(data, cluster_name, datalen)) {
				found = 1;
				break;
			}
		}
	}

	confdb_disconnect(handle);

	if (found) {
		return ccs_handle;
	} else {
		errno = ENOENT;
		return -1;
	}
}

/**
 * ccs_force_connect
 *
 * @cluster_name: verify that we are trying to connect to the requested cluster (tbd)
 * @blocking: retry connection forever
 *
 * Returns: ccs_desc on success, < 0 on failure
 */
int ccs_force_connect(const char *cluster_name, int blocking)
{
	int res = -1;

	if (blocking) {
		while ( res < 0 ) {
			res = ccs_connect();
			if (res < 0)
				sleep(1);
		}
	} else {
		res = ccs_connect();
		if (res < 0)
			return res;
	}
	if(cluster_name)
		return check_cluster_name(res, cluster_name);
	else
		return res;
}

/**
 * ccs_disconnect
 *
 * @desc: the descriptor returned by ccs_connect
 *
 * Returns: 0 on success, < 0 on error
 */
int ccs_disconnect(int desc)
{
	confdb_handle_t handle = 0;
	unsigned int connection_handle = 0;
	int ret;
	char data[128];
	int datalen = 0;
	int fullxpathint = 0;

	handle = confdb_connect();
	if (handle <= 0)
		return handle;

	connection_handle = find_ccs_handle(handle, desc);
	if (connection_handle == -1)
		return -1;

	memset(data, 0, sizeof(data));
	if (confdb_key_get(handle, connection_handle, "fullxpath", strlen("fullxpath"), &data, &datalen) != SA_AIS_OK) {
		errno = EINVAL;
		return -1;
	} else
		fullxpathint = atoi(data);

	if (fullxpathint)
		xpathfull_finish();

	ret = destroy_ccs_handle(handle, connection_handle);
	confdb_disconnect(handle);
	return ret;
}

int get_previous_query(confdb_handle_t handle, unsigned int connection_handle, char *previous_query, unsigned int *query_handle)
{
	int datalen;

	if (confdb_key_get(handle, connection_handle, "previous_query", strlen("previous_query"), previous_query, &datalen) == SA_AIS_OK) {
		if (confdb_key_get(handle, connection_handle, "query_handle", strlen("query_handle"), query_handle, &datalen) == SA_AIS_OK) {
			return 0;
		}
	}
	errno = ENOENT;
	return -1;
}

int set_previous_query(confdb_handle_t handle, unsigned int connection_handle, char *previous_query, unsigned int query_handle)
{
	char temp[PATH_MAX];
	int templen;
	unsigned int temphandle;

	if (confdb_key_get(handle, connection_handle, "previous_query", strlen("previous_query"), temp, &templen) == SA_AIS_OK) {
		if (strcmp(previous_query, temp)) {
			if (confdb_key_replace(handle, connection_handle, "previous_query", strlen("previous_query"), temp, templen, previous_query, strlen(previous_query) + 1) != SA_AIS_OK) {
				errno = ENOMEM;
				return -1;
			}
		}
	} else {
		if (confdb_key_create(handle, connection_handle, "previous_query", strlen("previous_query"), previous_query, strlen(previous_query) + 1) != SA_AIS_OK) {
			errno = ENOMEM;
			return -1;
		}
	}

	if (confdb_key_get(handle, connection_handle, "query_handle", strlen("query_handle"), &temphandle, &templen) == SA_AIS_OK) {
		if (temphandle != query_handle) {
			if (confdb_key_replace(handle, connection_handle, "query_handle", strlen("query_handle"), &temphandle, sizeof(unsigned int), &query_handle, sizeof(unsigned int)) != SA_AIS_OK) {
				errno = ENOMEM;
				return -1;
			}
		}
	} else {
		if (confdb_key_create(handle, connection_handle, "query_handle", strlen("query_handle"), &query_handle, sizeof(unsigned int)) != SA_AIS_OK) {
			errno = ENOMEM;
			return -1;
		}
	}

	if (confdb_key_get(handle, connection_handle, "iterator_tracker", strlen("iterator_tracker"), &temphandle, &templen) != SA_AIS_OK) {
		temphandle = 1;
		if (confdb_key_create(handle, connection_handle, "iterator_tracker", strlen("iterator_tracker"), &temphandle, sizeof(unsigned int)) != SA_AIS_OK) {
			errno = ENOMEM;
			return -1;
		}
	}

	return 0;
}

void reset_iterator(confdb_handle_t handle, unsigned int connection_handle)
{
	unsigned int value = 0;

	if (confdb_key_increment(handle, connection_handle, "iterator_tracker", strlen("iterator_tracker"), &value) != SA_AIS_OK)
		return;

	confdb_key_delete(handle, connection_handle, "iterator_tracker", strlen("iterator_tracker"), &value, sizeof(unsigned int));

	return;
}

/**
 * _ccs_get
 * @desc:
 * @query:
 * @rtn: value returned
 * @list: 1 to operate in list fashion
 *
 * This function will allocate space for the value that is the result
 * of the given query.  It is the user's responsibility to ensure that
 * the data returned is freed.
 *
 * Returns: 0 on success, < 0 on failure
 */
int _ccs_get(int desc, const char *query, char **rtn, int list)
{
	confdb_handle_t handle = 0;
	unsigned int connection_handle = 0;
	char data[128];
	int datalen = 0;
	int fullxpathint = 0;

	handle = confdb_connect();
	if (handle <= 0)
		return handle;

	connection_handle = find_ccs_handle(handle, desc);
	if (connection_handle == -1)
		return -1;

	memset(data, 0, sizeof(data));
	if (confdb_key_get(handle, connection_handle, "fullxpath", strlen("fullxpath"), &data, &datalen) != SA_AIS_OK) {
		errno = EINVAL;
		return -1;
	} else
		fullxpathint = atoi(data);

	if (!fullxpathint)
		*rtn = _ccs_get_xpathlite(handle, connection_handle, query, list);
	else
		*rtn = _ccs_get_fullxpath(handle, connection_handle, query, list);

	confdb_disconnect(handle);

	if(!*rtn)
		return -1;

	return 0;
}

/* see _ccs_get */
int ccs_get(int desc, const char *query, char **rtn)
{
	return _ccs_get(desc, query, rtn, 0);
}

/* see _ccs_get */
int ccs_get_list(int desc, const char *query, char **rtn)
{
	return _ccs_get(desc, query, rtn, 1);
}

/**
 * ccs_set: set an individual element's value in the config file.
 * @desc:
 * @path:
 * @val:
 *
 * This function is used to update individual elements in a config file.
 * It's effects are cluster wide.  It only succeeds when the node is part
 * of a quorate cluster.
 *
 * Note currently implemented.
 * 
 * Returns: 0 on success, < 0 on failure
 */
int ccs_set(int desc, const char *path, char *val){
	errno = ENOSYS;
	return -1;
}
