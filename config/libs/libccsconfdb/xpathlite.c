#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <corosync/corotypes.h>
#include <corosync/confdb.h>

#include "ccs.h"
#include "ccs_internal.h"

static int tokenizer(char *current_query)
{
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

		if (!next)
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
static int path_dive(confdb_handle_t handle, unsigned int *query_handle,
		     char *current_query, int tokens)
{
	char *pos = NULL, *next = NULL;
	int i;
	unsigned int new_obj_handle;

	pos = current_query + 1;

	for (i = 1; i <= tokens; i++) {
		if (confdb_object_find_start(handle, *query_handle) !=
		    CS_OK)
			goto fail;

		next = pos + strlen(pos) + 1;

		if (!strstr(pos, "[")) {
			/* straight path diving */
			if (confdb_object_find
			    (handle, *query_handle, pos, strlen(pos),
			     &new_obj_handle) != CS_OK)
				goto fail;
			else {
				confdb_object_find_destroy(handle,
							   *query_handle);
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

			start = strstr(pos, "[");
			if (!start)
				goto fail;

			end = strstr(pos, "]");
			if (!end)
				goto fail;

			middle = start + 1;
			memset(start, 0, 1);
			memset(end, 0, 1);

			if (!strcmp(pos, "child::*")) {
				int val, i;

				val = atoi(middle);

				if (val < 1)
					goto fail;

				if (confdb_object_iter_start
				    (handle, *query_handle) != CS_OK)
					goto fail;

				for (i = 1; i <= val; i++) {
					if (confdb_object_iter
					    (handle, *query_handle,
					     &new_obj_handle, data,
					     &datalen) != CS_OK)
						goto fail;
				}
				confdb_object_iter_destroy(handle,
							   *query_handle);
				confdb_object_find_destroy(handle,
							   *query_handle);
				*query_handle = new_obj_handle;

			} else if (!strstr(middle, "@")) {
				/* lookup something with index num = int */
				int val, i;

				val = atoi(middle);

				if (val < 1)
					goto fail;

				for (i = 1; i <= val; i++) {
					if (confdb_object_find
					    (handle, *query_handle, pos,
					     strlen(pos),
					     &new_obj_handle) != CS_OK)
						goto fail;
				}
				confdb_object_find_destroy(handle,
							   *query_handle);
				*query_handle = new_obj_handle;

			} else {
				/* lookup something with obj foo = bar */
				char *equal = NULL, *value = NULL, *tmp = NULL;
				int goout = 0;

				equal = strstr(middle, "=");
				if (!equal)
					goto fail;

				memset(equal, 0, 1);

				value = strstr(equal + 1, "\"");
				if (!value)
					goto fail;

				value = value + 1;

				tmp = strstr(value, "\"");
				if (!tmp)
					goto fail;

				memset(tmp, 0, 1);

				middle = strstr(middle, "@") + 1;
				if (!middle)
					goto fail;

				// middle points to foo
				// value to bar

				memset(data, 0, PATH_MAX);
				while (!goout) {
					if (confdb_object_find
					    (handle, *query_handle, pos,
					     strlen(pos),
					     &new_obj_handle) != CS_OK)
						goto fail;
					else {
						if (confdb_key_get
						    (handle, new_obj_handle,
						     middle, strlen(middle),
						     data,
						     &datalen) == CS_OK) {
							if (!strcmp
							    (data, value))
								goout = 1;
						}
					}
				}
				confdb_object_find_destroy(handle,
							   *query_handle);
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

static int get_data(confdb_handle_t handle, unsigned int connection_handle,
		    unsigned int query_handle, unsigned int *list_handle,
		    char **rtn, char *curpos, int list, int is_oldlist)
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
		char *start = NULL, *end = NULL;

		// a pure child::* request should come down as list
		if (!cmp && !list)
			goto fail;

		if (confdb_object_iter_start(handle, query_handle) != CS_OK)
			goto fail;

		if (!is_oldlist)
			*list_handle = query_handle;

		if (cmp) {
			start = strstr(curpos, "[");
			if (!start)
				goto fail;

			start = start + 1;

			end = strstr(start, "]");
			if (!end)
				goto fail;

			memset(end, 0, 1);
			value = atoi(start);
			if (value <= 0)
				goto fail;
		} else {
			if (confdb_key_increment
			    (handle, connection_handle, "iterator_tracker",
			     strlen("iterator_tracker"), &value) != CS_OK)
				value = 1;
		}

		while (value != 0) {
			memset(data, 0, PATH_MAX);
			if (confdb_object_iter
			    (handle, query_handle, &new_obj_handle, data,
			     &datalen) != CS_OK) {
				reset_iterator(handle, connection_handle);
				goto fail;
			}

			value--;
		}

		snprintf(resval, sizeof(resval), "%s=%s", data, keyval);
		*rtn = strndup(resval, datalen + keyvallen + 2);

	} else if (!strncmp(curpos, "@*", strlen("@*"))) {

		// this query makes sense only if we are in list mode
		if (!list)
			goto fail;

		if (confdb_key_iter_start(handle, query_handle) != CS_OK)
			goto fail;

		*list_handle = query_handle;

		if (confdb_key_increment
		    (handle, connection_handle, "iterator_tracker",
		     strlen("iterator_tracker"), &value) != CS_OK)
			value = 1;

		while (value != 0) {
			memset(data, 0, PATH_MAX);
			if (confdb_key_iter
			    (handle, query_handle, data, &datalen, keyval,
			     &keyvallen) != CS_OK) {
				reset_iterator(handle, connection_handle);
				goto fail;
			}

			value--;
		}

		snprintf(resval, sizeof(resval), "%s=%s", data, keyval);
		*rtn = strndup(resval, datalen + keyvallen + 2);

	} else {		/* pure data request */
		char *query;

		// this query doesn't make sense in list mode
		if (list)
			goto fail;

		if (confdb_object_find_start(handle, query_handle) != CS_OK)
			goto fail;

		query = strstr(curpos, "@");
		if (!query)
			goto fail;

		query = query + 1;

		if (confdb_key_get
		    (handle, query_handle, query, strlen(query), data,
		     &datalen) != CS_OK)
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
 * Returns: char * to result or NULL in case of failure.
 */
char *_ccs_get_xpathlite(confdb_handle_t handle, unsigned int connection_handle,
			 const char *query, int list)
{
	char current_query[PATH_MAX];
	char *datapos, *rtn = NULL;
	char previous_query[PATH_MAX];
	unsigned int list_handle = 0;
	unsigned int query_handle = 0;
	int prev = 0, is_oldlist = 0;
	int tokens, i;

	memset(current_query, 0, PATH_MAX);
	strncpy(current_query, query, PATH_MAX - 1);

	memset(previous_query, 0, PATH_MAX);

	datapos = current_query + 1;

	prev =
	    get_previous_query(handle, connection_handle, previous_query,
			       &list_handle);

	if (list && !prev && !strcmp(current_query, previous_query)) {
		query_handle = list_handle;
		is_oldlist = 1;
	} else {
		reset_iterator(handle, connection_handle);
		query_handle = OBJECT_PARENT_HANDLE;
	}

	if (confdb_object_find_start(handle, query_handle) != CS_OK) {
		errno = ENOENT;
		goto fail;
	}

	tokens = tokenizer(current_query);
	if (tokens < 1)
		goto fail;

	for (i = 1; i < tokens; i++)
		datapos = datapos + strlen(datapos) + 1;

	if (!is_oldlist)
		if (path_dive(handle, &query_handle, current_query, tokens - 1) < 0)	/* path dive can mangle tokens */
			goto fail;

	if (get_data
	    (handle, connection_handle, query_handle, &list_handle, &rtn,
	     datapos, list, is_oldlist) < 0)
		goto fail;

	if (list)
		if (set_previous_query
		    (handle, connection_handle, (char *)query, list_handle))
			goto fail;

	return rtn;

fail:
	return NULL;
}
