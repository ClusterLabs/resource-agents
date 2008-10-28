#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <corosync/saAis.h>
#include <corosync/confdb.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#ifdef EXPERIMENTAL_BUILD
#include <time.h>
#endif

#include "ccs.h"

#ifdef EXPERIMENTAL_BUILD
#ifndef CCS_HANDLE_TIMEOUT
#define CCS_HANDLE_TIMEOUT 60 /* 60 seconds */
#endif
#endif

#ifndef XMLBUFSIZE
#define XMLBUFSIZE 64000 
#endif

#ifdef EXPERIMENTAL_BUILD
int ccs_persistent_conn = 0;
#endif

int fullxpath = 0;

static xmlDocPtr doc = NULL;
static xmlXPathContextPtr ctx = NULL;

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

static int add_to_buffer(char *data, char **buffer, int *bufsize)
{
	int datalen = 0, bufferlen = 0;
	char *newbuf = NULL;

	datalen = strlen(data);
	bufferlen = strlen(*buffer);

	if(datalen) {
		if((bufferlen + datalen) >= *bufsize) {
			newbuf = malloc((*bufsize * 2));
			if(!newbuf) {
				errno = ENOMEM;
				return -1;
			}
			*bufsize = *bufsize * 2;
			memset(newbuf, 0, *bufsize);
			memcpy(newbuf, *buffer, bufferlen);
			free(*buffer);
			*buffer = newbuf;
		}
		strncpy(*buffer + bufferlen, data, datalen);
	}
	return 0;
}

static int dump_objdb_buff(confdb_handle_t dump_handle, unsigned int parent_object_handle, char **buffer, int *bufsize)
{
	unsigned int object_handle;
	char temp[PATH_MAX];
	char object_name[PATH_MAX];
	int object_name_len;
	char key_name[PATH_MAX];
	int key_name_len;
	char key_value[PATH_MAX];
	int key_value_len;
	int res;

	res = confdb_key_iter_start(dump_handle, parent_object_handle);
	if (res != SA_AIS_OK) {
		errno = ENOMEM;
		return -1;
	}

	if (!*buffer || ((*buffer) && !strlen(*buffer))) {
		snprintf(temp, PATH_MAX - 1, "<?xml version=\"1.0\"?>\n<objdbmaindoc>\n");
		if(add_to_buffer(temp, buffer, bufsize))
			return -1;
	}

	while ( (res = confdb_key_iter(dump_handle, parent_object_handle, key_name, &key_name_len,
					key_value, &key_value_len)) == SA_AIS_OK) {
		key_name[key_name_len] = '\0';
		key_value[key_value_len] = '\0';

		if (!strncmp(key_name, "service_id", key_name_len))
			continue;
		if (!strncmp(key_name, "handle", key_name_len))
			continue;
		if (!strncmp(key_name, "next_handle", key_name_len))
			continue;

		snprintf(temp, PATH_MAX - 1, " %s=\"%s\"", key_name, key_value);
		if(add_to_buffer(temp, buffer, bufsize))
			return -1;
	}

	if (parent_object_handle > 0) {
		snprintf(temp, PATH_MAX - 1, ">\n");
		if(add_to_buffer(temp, buffer, bufsize))
			return -1;
	}

	res = confdb_object_iter_start(dump_handle, parent_object_handle);
	if (res != SA_AIS_OK) {
		errno = ENOMEM;
		return -1;
	}

	while ( (res = confdb_object_iter(dump_handle, parent_object_handle, &object_handle, object_name, &object_name_len)) == SA_AIS_OK)   {
		unsigned int parent;

		res = confdb_object_parent_get(dump_handle, object_handle, &parent);
		if (res != SA_AIS_OK) {
			errno = EINVAL;
			return -1;
		}

		object_name[object_name_len] = '\0';

		/* we need to skip the top level services because they have invalid
		 * xml chars */

		snprintf(temp, PATH_MAX - 1, "<%s", object_name);
		if(add_to_buffer(temp, buffer, bufsize))
			return -1;

		res = dump_objdb_buff(dump_handle, object_handle, buffer, bufsize);
		if(res) {
			errno = res;
			return res;
		}

		if (object_handle != parent_object_handle) {
			snprintf(temp, PATH_MAX - 1, "</%s>\n", object_name);
			if(add_to_buffer(temp, buffer, bufsize))
				return -1;
		} else {
			snprintf(temp, PATH_MAX - 1, ">\n");
			if(add_to_buffer(temp, buffer, bufsize))
				return -1;
		}
	}

	if (parent_object_handle == OBJECT_PARENT_HANDLE) {
		snprintf(temp, PATH_MAX - 1, "</objdbmaindoc>\n");
		if(add_to_buffer(temp, buffer, bufsize))
			return -1;
	}

	return 0;
}

static int xpathfull_init(confdb_handle_t handle, int ccs_handle) {
	int size = XMLBUFSIZE;
	char *buffer, *newbuf;

	newbuf = buffer = malloc(XMLBUFSIZE);
	if(!buffer) {
		errno = ENOMEM;
		goto fail;
	}

	memset(buffer, 0, XMLBUFSIZE);

	if (dump_objdb_buff(handle, OBJECT_PARENT_HANDLE, &newbuf, &size))
		goto fail;

	if (newbuf != buffer) {
		buffer = newbuf;
		newbuf = NULL;
	}

	doc = xmlParseMemory(buffer,strlen(buffer));
	if(!doc)
		goto fail;

	free(buffer);

	ctx = xmlXPathNewContext(doc);
	if(!ctx) {
		xmlFreeDoc(doc);
		goto fail;
	}

	return 0;

fail:
	return -1;
}

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

	if (fullxpathint) {
		if (ctx) {
			xmlXPathFreeContext(ctx);
			ctx = NULL;
		}
		if (doc) {
			xmlFreeDoc(doc);
			doc = NULL;
		}
	}

	ret = destroy_ccs_handle(handle, connection_handle);
	confdb_disconnect(handle);
	return ret;
}

static int get_previous_query(confdb_handle_t handle, unsigned int connection_handle, char *previous_query, unsigned int *query_handle)
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

static int set_previous_query(confdb_handle_t handle, unsigned int connection_handle, char *previous_query, unsigned int query_handle)
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

static int tokenizer(char *current_query) {
	int index = 0;
	char *curpos = current_query;
	char *next = NULL;
	char *end;

	end = current_query + strlen(current_query);

	while (curpos <= end) {
		index++;

		if (strncmp(curpos, "/", 1)) {
			errno = EINVAL;
			return -1;
		}

		memset(curpos, 0, 1);
		curpos = curpos + 1;

		next = strstr(curpos, "/");
		if (next == curpos) {
			errno = EINVAL;
			return -1;
		}

		if(!next)
			return index;

		if ((strstr(curpos, "[") > next) || !strstr(curpos, "["))
			curpos = next;
		else
			curpos = strstr(strstr(curpos, "]"), "/");

	}
	errno = EINVAL;
	return -1;
}

/*
 * return 0 on success
 * return -1 on errors
 */
static int path_dive(confdb_handle_t handle, unsigned int *query_handle, char *current_query, int tokens)
{
	char *pos = NULL, *next = NULL;
	int i;
	unsigned int new_obj_handle;

	pos = current_query + 1;

	for (i = 1; i <= tokens; i++)
	{
		if(confdb_object_find_start(handle, *query_handle) != SA_AIS_OK)
			goto fail;

		next = pos + strlen(pos) + 1;

		if (!strstr(pos, "[")) {
			/* straight path diving */
			if (confdb_object_find(handle, *query_handle, pos, strlen(pos), &new_obj_handle) != SA_AIS_OK)
				goto fail;
			else {
				confdb_object_find_destroy(handle, *query_handle);
				*query_handle = new_obj_handle;
			}
		} else {
			/*
			 * /something[int]/ or /something[@foo="bar"]/
			 * start and end will identify []
			 * middle will point to the inside request
			 */

			char *start = NULL, *middle = NULL, *end = NULL;
			char data[PATH_MAX];
			int datalen;

			/*
			 * those ones should be always good because
			 * the tokenizer takes care of them
			 */

			start=strstr(pos, "[");
			if (!start)
				goto fail;

			end=strstr(pos, "]");
			if (!end)
				goto fail;

			middle=start+1;
			memset(start, 0, 1);
			memset(end, 0, 1);

			if (!strcmp(pos, "child::*")) {
				int val, i;

				val = atoi(middle);

				if(val < 1)
					goto fail;

				if(confdb_object_iter_start(handle, *query_handle) != SA_AIS_OK)
					goto fail;

				for (i = 1; i <= val; i++) {
					if(confdb_object_iter(handle, *query_handle, &new_obj_handle, data, &datalen) != SA_AIS_OK)
						goto fail;
				}
				confdb_object_iter_destroy(handle, *query_handle);
				confdb_object_find_destroy(handle, *query_handle);
				*query_handle = new_obj_handle;

			} else if (!strstr(middle, "@")) {
				/* lookup something with index num = int */
				int val, i;

				val = atoi(middle);

				if(val < 1)
					goto fail;

				for (i = 1; i <= val; i++) {
					if (confdb_object_find(handle, *query_handle, pos, strlen(pos), &new_obj_handle) != SA_AIS_OK)
						goto fail;
				}
				confdb_object_find_destroy(handle, *query_handle);
				*query_handle = new_obj_handle;

			} else {
				/* lookup something with obj foo = bar */
				char *equal = NULL, *value = NULL, *tmp = NULL;
				int goout = 0;

				equal=strstr(middle, "=");
				if(!equal)
					goto fail;

				memset(equal, 0, 1);

				value=strstr(equal + 1, "\"");
				if(!value)
					goto fail;

				value = value + 1;

				tmp=strstr(value, "\"");
				if(!tmp)
					goto fail;

				memset(tmp, 0, 1);

				middle=strstr(middle, "@") + 1;
				if (!middle)
					goto fail;

				// middle points to foo
				// value to bar

				memset(data, 0, PATH_MAX);
				while(!goout) {
					if (confdb_object_find(handle, *query_handle, pos, strlen(pos), &new_obj_handle) != SA_AIS_OK)
						goto fail;
					else {
						if(confdb_key_get(handle, new_obj_handle, middle, strlen(middle), data, &datalen) == SA_AIS_OK) {
							if (!strcmp(data, value))
								goout=1;
						}
					}
				}
				confdb_object_find_destroy(handle, *query_handle);
				*query_handle = new_obj_handle;
			}
		}

		pos = next;
	}

	return 0;

fail:
	errno = EINVAL;
	return -1;
}

static void reset_iterator(confdb_handle_t handle, unsigned int connection_handle)
{
	unsigned int value = 0;

	if (confdb_key_increment(handle, connection_handle, "iterator_tracker", strlen("iterator_tracker"), &value) != SA_AIS_OK)
		return;

	confdb_key_delete(handle, connection_handle, "iterator_tracker", strlen("iterator_tracker"), &value, sizeof(unsigned int));

	return;
}

static int get_data(confdb_handle_t handle, unsigned int connection_handle, unsigned int query_handle, unsigned int *list_handle, char **rtn, char *curpos, int list, int is_oldlist)
{
	int datalen, cmp;
	char data[PATH_MAX];
	char resval[PATH_MAX];
	char keyval[PATH_MAX];
	int keyvallen = PATH_MAX;
	unsigned int new_obj_handle;
	unsigned int value = 0;

	memset(data, 0, PATH_MAX);
	memset(resval, 0, PATH_MAX);
	memset(keyval, 0, PATH_MAX);

	// we need to handle child::*[int value] in non list mode.
	cmp = strcmp(curpos, "child::*");
	if (cmp >= 0) {
		char *start = NULL, *end=NULL;

		// a pure child::* request should come down as list
		if (!cmp && !list)
			goto fail;

		if(confdb_object_iter_start(handle, query_handle) != SA_AIS_OK)
			goto fail;

		if(!is_oldlist)
			*list_handle = query_handle;

		if(cmp) {
			start=strstr(curpos, "[");
			if (!start)
				goto fail;

			start = start + 1;

			end=strstr(start, "]");
			if (!end)
				goto fail;

			memset(end, 0, 1);
			value=atoi(start);
			if (value <= 0)
				goto fail;
		} else {
			if (confdb_key_increment(handle, connection_handle, "iterator_tracker", strlen("iterator_tracker"), &value) != SA_AIS_OK)
				value = 1;
		}

		while (value != 0) {
			memset(data, 0, PATH_MAX);
			if(confdb_object_iter(handle, query_handle, &new_obj_handle, data, &datalen) != SA_AIS_OK) {
				reset_iterator(handle, connection_handle);
				goto fail;
			}

			value--;
		}

		snprintf(resval, sizeof(resval), "%s=%s", data, keyval);
		*rtn = strndup(resval, datalen + keyvallen + 2);

	} else if (!strncmp(curpos, "@*", strlen("@*"))) {

		// this query makes sense only if we are in list mode
		if(!list)
			goto fail;

		if(confdb_key_iter_start(handle, query_handle) != SA_AIS_OK)
			goto fail;

		*list_handle = query_handle;

		if (confdb_key_increment(handle, connection_handle, "iterator_tracker", strlen("iterator_tracker"), &value) != SA_AIS_OK)
			value = 1;

		while (value != 0) {
			memset(data, 0, PATH_MAX);
			if(confdb_key_iter(handle, query_handle, data, &datalen, keyval, &keyvallen) != SA_AIS_OK) {
				reset_iterator(handle, connection_handle);
				goto fail;
			}

			value--;
		}

		snprintf(resval, sizeof(resval), "%s=%s", data, keyval);
		*rtn = strndup(resval, datalen+keyvallen+2);

	} else { /* pure data request */
		char *query;

		// this query doesn't make sense in list mode
		if(list)
			goto fail;

		if(confdb_object_find_start(handle, query_handle) != SA_AIS_OK)
			goto fail;

		query = strstr(curpos, "@");
		if (!query)
			goto fail;

		query = query + 1;

		if(confdb_key_get(handle, query_handle, query, strlen(query), data, &datalen) != SA_AIS_OK)
			goto fail;

		*rtn = strndup(data, datalen);
	}

	return 0;

fail:
	errno = EINVAL;
	return -1;
}


/**
 * _ccs_get_xpathlite
 * @handle:
 * @connection_handle:
 * @query:
 * @list: 1 to operate in list fashion
 *
 * This function will allocate space for the value that is the result
 * of the given query.  It is the user's responsibility to ensure that
 * the data returned is freed.
 *
 * Returns: 0 on success, < 0 on failure
 */
static char * _ccs_get_xpathlite(confdb_handle_t handle, unsigned int connection_handle, const char *query, int list)
{
	char current_query[PATH_MAX];
	char *datapos, *rtn;
	char previous_query[PATH_MAX];
	unsigned int list_handle = 0;
	unsigned int query_handle = 0;
	int prev = 0, is_oldlist = 0;
	int tokens, i;

	memset(current_query, 0, PATH_MAX);
	strncpy(current_query, query, PATH_MAX - 1);

	memset(previous_query, 0, PATH_MAX);

	datapos = current_query + 1;

	prev = get_previous_query(handle, connection_handle, previous_query, &list_handle);

	if(list && !prev && !strcmp(current_query, previous_query)) {
		query_handle = list_handle;
		is_oldlist = 1;
	} else {
		reset_iterator(handle, connection_handle);
		query_handle = OBJECT_PARENT_HANDLE;
	}

	if (confdb_object_find_start(handle, query_handle) != SA_AIS_OK) {
		errno = ENOENT;
		goto fail;
	}

	tokens = tokenizer(current_query);
	if(tokens < 1)
		goto fail;

	for (i = 1; i < tokens; i++)
		datapos = datapos + strlen(datapos) + 1;

	if(!is_oldlist)
		if (path_dive(handle, &query_handle, current_query, tokens - 1) < 0) /* path dive can mangle tokens */
			goto fail;

	if (get_data(handle, connection_handle, query_handle, &list_handle, &rtn, datapos, list, is_oldlist) < 0)
		goto fail;

	if(list)
		if (set_previous_query(handle, connection_handle, (char *)query, list_handle))
			goto fail;

	return rtn; 

fail:
	return NULL;
}

/**
 * _ccs_get_fullxpath
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
static char * _ccs_get_fullxpath(confdb_handle_t handle, unsigned int connection_handle, const char *query, int list)
{
	xmlXPathObjectPtr obj = NULL;
	char realquery[PATH_MAX + 16];
	char previous_query[PATH_MAX];
	unsigned int list_handle = 0;
	unsigned int xmllistindex = 0;
	int prev = 0;
	char *rtn = NULL;

	errno = 0;

	if(strncmp(query, "/", 1)) {
		errno = EINVAL;
		goto fail;
	}

	memset(previous_query, 0, PATH_MAX);

	prev = get_previous_query(handle, connection_handle, previous_query, &list_handle);

	if (list && !prev && !strcmp(query, previous_query)) {
		if (confdb_key_increment(handle, connection_handle, "iterator_tracker", strlen("iterator_tracker"), &xmllistindex) != SA_AIS_OK) {
			xmllistindex = 0;
		} else {
			xmllistindex--;
		}
	} else {
		reset_iterator(handle, connection_handle);
		xmllistindex = 0;
	}

	memset(realquery, 0, PATH_MAX + 16);
	snprintf(realquery, PATH_MAX + 16 - 1, "/objdbmaindoc%s", query);

	obj = xmlXPathEvalExpression((xmlChar *)realquery, ctx);

	if(!obj) {
		errno = EINVAL;
		goto fail;
	}

	if (obj->nodesetval && (obj->nodesetval->nodeNr > 0)) {
		xmlNodePtr node;
		int size = 0, nnv = 0;

		if(xmllistindex >= obj->nodesetval->nodeNr){
			reset_iterator(handle, connection_handle);
			errno = ENODATA;
			goto fail;
		}

		node = obj->nodesetval->nodeTab[xmllistindex];

		if(!node) {
			errno = ENODATA;
			goto fail;
		}

		if (((node->type == XML_ATTRIBUTE_NODE) && strstr(query, "@*")) ||
		    ((node->type == XML_ELEMENT_NODE) && strstr(query, "child::*"))) {
			if (node->children && node->children->content)
				size = strlen((char *)node->children->content) +
					strlen((char *)node->name)+2;
			else
				size = strlen((char *)node->name)+2;

			nnv = 1;
		} else {
			if (node->children && node->children->content)
				size = strlen((char *)node->children->content)+1;

			else {
				errno = ENODATA;
				goto fail;
			}
		}

		rtn = malloc(size);

		if (!rtn) {
			errno = ENOMEM;
			goto fail;
		}

		if (nnv)
			sprintf(rtn, "%s=%s", node->name, node->children ? (char *)node->children->content:"");
		else
			sprintf(rtn, "%s", node->children ? node->children->content : node->name);

		if(list)
			set_previous_query(handle, connection_handle, (char *)query, OBJECT_PARENT_HANDLE);

	} else
		errno = EINVAL;

fail:
	if(obj)
		xmlXPathFreeObject(obj);

	return rtn;
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
