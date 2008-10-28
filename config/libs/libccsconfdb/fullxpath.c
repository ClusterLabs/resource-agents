#include <string.h>
#include <errno.h>
#include <limits.h>
#include <corosync/saAis.h>
#include <corosync/confdb.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>

#include "ccs.h"
#include "ccs_internal.h"

#ifndef XMLBUFSIZE
#define XMLBUFSIZE 64000
#endif

int fullxpath = 0;

static xmlDocPtr doc = NULL;
static xmlXPathContextPtr ctx = NULL;

static int add_to_buffer(char *data, char **buffer, int *bufsize)
{
	int datalen = 0, bufferlen = 0;
	char *newbuf = NULL;

	datalen = strlen(data);
	bufferlen = strlen(*buffer);

	if (datalen) {
		if ((bufferlen + datalen) >= *bufsize) {
			newbuf = malloc((*bufsize * 2));
			if (!newbuf) {
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

static int dump_objdb_buff(confdb_handle_t dump_handle,
			   unsigned int parent_object_handle, char **buffer,
			   int *bufsize)
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
		snprintf(temp, PATH_MAX - 1,
			 "<?xml version=\"1.0\"?>\n<objdbmaindoc>\n");
		if (add_to_buffer(temp, buffer, bufsize))
			return -1;
	}

	while ((res =
		confdb_key_iter(dump_handle, parent_object_handle, key_name,
				&key_name_len, key_value,
				&key_value_len)) == SA_AIS_OK) {
		key_name[key_name_len] = '\0';
		key_value[key_value_len] = '\0';

		if (!strncmp(key_name, "service_id", key_name_len))
			continue;
		if (!strncmp(key_name, "handle", key_name_len))
			continue;
		if (!strncmp(key_name, "next_handle", key_name_len))
			continue;

		snprintf(temp, PATH_MAX - 1, " %s=\"%s\"", key_name, key_value);
		if (add_to_buffer(temp, buffer, bufsize))
			return -1;
	}

	if (parent_object_handle > 0) {
		snprintf(temp, PATH_MAX - 1, ">\n");
		if (add_to_buffer(temp, buffer, bufsize))
			return -1;
	}

	res = confdb_object_iter_start(dump_handle, parent_object_handle);
	if (res != SA_AIS_OK) {
		errno = ENOMEM;
		return -1;
	}

	while ((res =
		confdb_object_iter(dump_handle, parent_object_handle,
				   &object_handle, object_name,
				   &object_name_len)) == SA_AIS_OK) {
		unsigned int parent;

		res =
		    confdb_object_parent_get(dump_handle, object_handle,
					     &parent);
		if (res != SA_AIS_OK) {
			errno = EINVAL;
			return -1;
		}

		object_name[object_name_len] = '\0';

		/* we need to skip the top level services because they have invalid
		 * xml chars */

		snprintf(temp, PATH_MAX - 1, "<%s", object_name);
		if (add_to_buffer(temp, buffer, bufsize))
			return -1;

		res =
		    dump_objdb_buff(dump_handle, object_handle, buffer,
				    bufsize);
		if (res) {
			errno = res;
			return res;
		}

		if (object_handle != parent_object_handle) {
			snprintf(temp, PATH_MAX - 1, "</%s>\n", object_name);
			if (add_to_buffer(temp, buffer, bufsize))
				return -1;
		} else {
			snprintf(temp, PATH_MAX - 1, ">\n");
			if (add_to_buffer(temp, buffer, bufsize))
				return -1;
		}
	}

	if (parent_object_handle == OBJECT_PARENT_HANDLE) {
		snprintf(temp, PATH_MAX - 1, "</objdbmaindoc>\n");
		if (add_to_buffer(temp, buffer, bufsize))
			return -1;
	}

	return 0;
}

int xpathfull_init(confdb_handle_t handle, int ccs_handle)
{
	int size = XMLBUFSIZE;
	char *buffer, *newbuf;

	newbuf = buffer = malloc(XMLBUFSIZE);
	if (!buffer) {
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

	doc = xmlParseMemory(buffer, strlen(buffer));
	if (!doc)
		goto fail;

	free(buffer);

	ctx = xmlXPathNewContext(doc);
	if (!ctx) {
		xmlFreeDoc(doc);
		goto fail;
	}

	return 0;

fail:
	return -1;
}

void xpathfull_finish()
{
	if (ctx) {
		xmlXPathFreeContext(ctx);
		ctx = NULL;
	}
	if (doc) {
		xmlFreeDoc(doc);
		doc = NULL;
	}
	return;
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
char *_ccs_get_fullxpath(confdb_handle_t handle, unsigned int connection_handle,
			 const char *query, int list)
{
	xmlXPathObjectPtr obj = NULL;
	char realquery[PATH_MAX + 16];
	char previous_query[PATH_MAX];
	unsigned int list_handle = 0;
	unsigned int xmllistindex = 0;
	int prev = 0;
	char *rtn = NULL;

	errno = 0;

	if (strncmp(query, "/", 1)) {
		errno = EINVAL;
		goto fail;
	}

	memset(previous_query, 0, PATH_MAX);

	prev =
	    get_previous_query(handle, connection_handle, previous_query,
			       &list_handle);

	if (list && !prev && !strcmp(query, previous_query)) {
		if (confdb_key_increment
		    (handle, connection_handle, "iterator_tracker",
		     strlen("iterator_tracker"), &xmllistindex) != SA_AIS_OK) {
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

	obj = xmlXPathEvalExpression((xmlChar *) realquery, ctx);

	if (!obj) {
		errno = EINVAL;
		goto fail;
	}

	if (obj->nodesetval && (obj->nodesetval->nodeNr > 0)) {
		xmlNodePtr node;
		int size = 0, nnv = 0;

		if (xmllistindex >= obj->nodesetval->nodeNr) {
			reset_iterator(handle, connection_handle);
			errno = ENODATA;
			goto fail;
		}

		node = obj->nodesetval->nodeTab[xmllistindex];

		if (!node) {
			errno = ENODATA;
			goto fail;
		}

		if (((node->type == XML_ATTRIBUTE_NODE) && strstr(query, "@*"))
		    || ((node->type == XML_ELEMENT_NODE)
			&& strstr(query, "child::*"))) {
			if (node->children && node->children->content)
				size = strlen((char *)node->children->content) +
				    strlen((char *)node->name) + 2;
			else
				size = strlen((char *)node->name) + 2;

			nnv = 1;
		} else {
			if (node->children && node->children->content)
				size =
				    strlen((char *)node->children->content) + 1;

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
			sprintf(rtn, "%s=%s", node->name,
				node->children ? (char *)node->children->
				content : "");
		else
			sprintf(rtn, "%s",
				node->children ? node->children->
				content : node->name);

		if (list)
			set_previous_query(handle, connection_handle,
					   (char *)query, OBJECT_PARENT_HANDLE);

	} else
		errno = EINVAL;

fail:
	if (obj)
		xmlXPathFreeObject(obj);

	return rtn;
}
