#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <openais/saAis.h>
#include <openais/confdb.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>

#include "ccs.h"

/* Callbacks are not supported - we will use them to update fullxml doc/ctx */
static confdb_callbacks_t callbacks = {
	.confdb_change_notify_fn = NULL,
};

static confdb_handle_t handle = 0;

static char current_query[PATH_MAX];
static char previous_query[PATH_MAX];
static unsigned int query_handle;
static unsigned int list_handle;

int fullxpath = 0;
static int fullxpathint;

static char *buffer = NULL;
static xmlDocPtr doc = NULL;
static xmlXPathContextPtr ctx = NULL;
static int xmllistindex = 0;

static void xpathlite_init() {
	memset(current_query, 0, PATH_MAX);
	memset(previous_query, 0, PATH_MAX);
	query_handle = OBJECT_PARENT_HANDLE;
	list_handle = OBJECT_PARENT_HANDLE;
}

static void add_to_buffer(char *data, char **buffer, int *size)
{
	int len;

	if((len = strlen(data))) {
		*size = *size + len;
		if (*buffer)
			strncpy(*buffer + strlen(*buffer), data, len);
	}
	return;
}

static int dump_objdb_buff(confdb_handle_t dump_handle, unsigned int parent_object_handle, char **buffer, int *size)
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
	if (res != SA_AIS_OK)
		return -1;

	if (!*buffer || ((*buffer) && !strlen(*buffer))) {
		snprintf(temp, PATH_MAX - 1, "<?xml version=\"1.0\"?>\n<objdbmaindoc>\n");
		add_to_buffer(temp, buffer, size);
	}

	while ( (res = confdb_key_iter(dump_handle, parent_object_handle, key_name, &key_name_len,
					key_value, &key_value_len)) == SA_AIS_OK) {
		key_name[key_name_len] = '\0';
		key_value[key_value_len] = '\0';
		snprintf(temp, PATH_MAX - 1, " %s=\"%s\"", key_name, key_value);
		add_to_buffer(temp, buffer, size);
	}

	if (parent_object_handle > 0) {
		snprintf(temp, PATH_MAX - 1, ">\n");
		add_to_buffer(temp, buffer, size);
	}

	res = confdb_object_iter_start(dump_handle, parent_object_handle);
	if (res != SA_AIS_OK)
		return -1;

	while ( (res = confdb_object_iter(dump_handle, parent_object_handle, &object_handle, object_name, &object_name_len)) == SA_AIS_OK)   {
		unsigned int parent;

		res = confdb_object_parent_get(dump_handle, object_handle, &parent);
		if (res != SA_AIS_OK)
			return -1;

		object_name[object_name_len] = '\0';

		/* we need to skip the top level services because they have invalid
		 * xml chars */
		if(!strncmp(object_name, "service", object_name_len))
			continue;

		snprintf(temp, PATH_MAX - 1, "<%s", object_name);
		add_to_buffer(temp, buffer, size);

		res = dump_objdb_buff(dump_handle, object_handle, buffer, size);
		if(res)
			return res;

		if (object_handle != parent_object_handle) {
			snprintf(temp, PATH_MAX - 1, "</%s>\n", object_name);
			add_to_buffer(temp, buffer, size);
		} else {
			snprintf(temp, PATH_MAX - 1, ">\n");
			add_to_buffer(temp, buffer, size);
		}
	}

	if (parent_object_handle == OBJECT_PARENT_HANDLE) {
		snprintf(temp, PATH_MAX - 1, "</objdbmaindoc>\n");
		add_to_buffer(temp, buffer, size);
	}

	return 0;
}

static int xpathfull_init() {
	int size = 0;

	if (dump_objdb_buff(handle, OBJECT_PARENT_HANDLE, &buffer, &size))
		goto fail;

	buffer=malloc(2 * size);
	if(!buffer)
		goto fail;

	memset(buffer, 0, 2 * size);

	if (dump_objdb_buff(handle, OBJECT_PARENT_HANDLE, &buffer, &size))
		goto fail;

	doc = xmlParseMemory(buffer,strlen(buffer));
	if(!doc)
		goto fail;

	ctx = xmlXPathNewContext(doc);
	if(!ctx)
		goto fail;

	memset(previous_query, 0, PATH_MAX);

	return 0;

fail:
	return -1;
}

/**
 * ccs_connect
 *
 * Returns: ccs_desc on success, < 0 on failure
 */
int ccs_connect(void){
	int res;

	if(handle)
		return 1;

	res = confdb_initialize (&handle, &callbacks);
	if (res != SA_AIS_OK)
		return -1;

	if(!fullxpath)
		xpathlite_init();
	else
		if (xpathfull_init() < 0) {
			ccs_disconnect(1);
			return -1;
		}

	fullxpathint = fullxpath;

	return 1;
}

/**
 * ccs_force_connect
 *
 * Returns: ccs_desc on success, < 0 on failure
 */
int ccs_force_connect(const char *cluster_name, int blocking){
	int res = -1;

	if (blocking) {
		while ( res < 0 ) {
			res = ccs_connect();
			if (res < 0)
				sleep(1);
		}
		return res;
	} else
		return ccs_connect();
}

/**
 * ccs_disconnect
 * @desc: the descriptor returned by ccs_connect
 *
 * This function frees all associated state kept with an open connection
 *
 * Returns: 0 on success, < 0 on error
 */
int ccs_disconnect(int desc){
	int res;

	if (!handle)
		return 0;

	if (fullxpathint) {
		if(ctx) {
			xmlXPathFreeContext(ctx);
			ctx = NULL;
		}
		if(doc) {
			xmlFreeDoc(doc);
			doc = NULL;
		}
		if(buffer) {
			free(buffer);
			buffer = NULL;
		}
	}

	res = confdb_finalize (handle);
	if (res != CONFDB_OK)
		return -1;

	handle = 0;

	return 0;
}

static int tokenizer() {
	int index = 0;
	char *curpos = current_query;
	char *next = NULL;
	char *end;

	end = current_query + strlen(current_query);

	while (curpos <= end) {
		index++;

		if (strncmp(curpos, "/", 1))
			return -1;

		memset(curpos, 0, 1);
		curpos = curpos + 1;

		next = strstr(curpos, "/");
		if (next == curpos)
			return -1;

		if(!next)
			return index;

		if ((strstr(curpos, "[") > next) || !strstr(curpos, "["))
			curpos = next;
		else
			curpos = strstr(strstr(curpos, "]"), "/");

	}
	return -1;
}

/*
 * return 0 on success
 * return -1 on errors
 */
static int path_dive(int tokens)
{
	char *pos = NULL, *next = NULL;
	int i;
	unsigned int new_obj_handle;

	pos = current_query + 1;

	for (i = 1; i <= tokens; i++)
	{
		if(confdb_object_find_start(handle, query_handle) != SA_AIS_OK)
			goto fail;

		next = pos + strlen(pos) + 1;

		if (!strstr(pos, "[")) {
			/* straight path diving */
			if (confdb_object_find(handle, query_handle, pos, strlen(pos), &new_obj_handle) != SA_AIS_OK)
				goto fail;
			else
				query_handle = new_obj_handle;
		} else {
			/*
			 * /something[int]/ or /something[@foo="bar"]/
			 * start and end will identify []
			 * middle will point to the inside request
			 */

			char *start = NULL, *middle = NULL, *end = NULL;

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

			if (!strstr(middle, "@")) {
				/* lookup something with index num = int */
				int val, i;

				val = atoi(middle);

				if(val < 1)
					goto fail;

				for (i = 1; i <= val; i++) {
					if (confdb_object_find(handle, query_handle, pos, strlen(pos), &new_obj_handle) != SA_AIS_OK)
						goto fail;
				}
				query_handle = new_obj_handle;

			} else {
				/* lookup something with obj foo = bar */
				char *equal = NULL, *value = NULL, *tmp = NULL;
				char data[PATH_MAX];
				int goout = 0, datalen;

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
					if (confdb_object_find(handle, query_handle, pos, strlen(pos), &new_obj_handle) != SA_AIS_OK)
						goto fail;
					else {
						if(confdb_key_get(handle, new_obj_handle, middle, strlen(middle), data, &datalen) == SA_AIS_OK) {
							if (!strcmp(data, value))
								goout=1;
						}
					}
				}
				query_handle=new_obj_handle;
			}
		}

		pos = next;
	}

	return 0;

fail:
	return -1;
}

static int get_data(char **rtn, char *curpos, int list, int is_oldlist)
{
	int datalen, cmp;
	char data[PATH_MAX];
	char resval[PATH_MAX];
	char keyval[PATH_MAX];
	int keyvallen = PATH_MAX;
	unsigned int new_obj_handle;

	memset(data, 0, PATH_MAX);
	memset(resval, 0, PATH_MAX);
	memset(keyval, 0, PATH_MAX);

	// we need to handle child::*[int value] in non list mode.
	cmp = strcmp(curpos, "child::*");
	if (cmp >= 0) {
		char *start = NULL, *end=NULL;
		int value = 1;

		// a pure child::* request should come down as list
		if (!cmp && !list)
			goto fail;

		if (!is_oldlist || cmp) {
			if(confdb_object_iter_start(handle, query_handle) != SA_AIS_OK)
				goto fail;

			list_handle = query_handle;
		}

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
		}

		while (value) {
			memset(data, 0, PATH_MAX);
			if(confdb_object_iter(handle, query_handle, &new_obj_handle, data, &datalen) != SA_AIS_OK)
				goto fail;

			value--;
		}

		snprintf(resval, sizeof(resval), "%s=%s", data, keyval);
		*rtn = strndup(resval, datalen + keyvallen + 2);

	} else if (!strncmp(curpos, "@*", strlen("@*"))) {

		// this query makes sense only if we are in list mode
		if(!list)
			goto fail;

		if (!is_oldlist)
			if(confdb_key_iter_start(handle, query_handle) != SA_AIS_OK)
				goto fail;

		list_handle = query_handle;

		if(confdb_key_iter(handle, query_handle, data, &datalen, keyval, &keyvallen) != SA_AIS_OK)
			goto fail;

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
	return -1;
}

/**
 * _ccs_get_xpathlite
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
int _ccs_get_xpathlite(int desc, const char *query, char **rtn, int list)
{
	int res = 0, confdbres = 0, is_oldlist = 0;
	int tokens, i;
	char *datapos = current_query + 1; 

	/* we should be able to mangle the world here without destroying anything */
	strncpy(current_query, query, PATH_MAX - 1);

	/* we need to check list mode */
	if (list && !strcmp(current_query, previous_query)) {
		query_handle = list_handle;
		is_oldlist = 1;
	} else {
		query_handle = OBJECT_PARENT_HANDLE;
		memset(previous_query, 0, PATH_MAX);
	}

	confdbres = confdb_object_find_start(handle, query_handle);
	if (confdbres != SA_AIS_OK) {
		res = -1;
		goto fail;
	}

	res = tokens = tokenizer();
	if (res < 1)
		goto fail;

	for (i = 1; i < tokens; i++) {
		datapos = datapos + strlen(datapos) + 1;
	}

	if(!is_oldlist) {
		res = path_dive(tokens - 1); /* path dive can mangle tokens */
		if (res < 0)
			goto fail;

	}

	res = get_data(rtn, datapos, list, is_oldlist);
	if (res < 0)
		goto fail;

	if(list)
		strncpy(previous_query, query, PATH_MAX-1);

fail:
	return res;
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
int _ccs_get_fullxpath(int desc, const char *query, char **rtn, int list)
{
	int res = 0;
	xmlXPathObjectPtr obj = NULL;
	char realquery[PATH_MAX + 16];

	if(strncmp(query, "/", 1))
		return -EINVAL;

	if (list && !strcmp(query, previous_query))
		xmllistindex++;

	memset(realquery, 0, PATH_MAX + 16);
	snprintf(realquery, PATH_MAX + 16 - 1, "/objdbmaindoc%s", query);

	obj = xmlXPathEvalExpression((xmlChar *)realquery, ctx);

	if(!obj)
		return -EINVAL;

	if (obj->nodesetval && (obj->nodesetval->nodeNr > 0) ) {
		xmlNodePtr node;
		int size = 0, nnv = 0;

		if(xmllistindex >= obj->nodesetval->nodeNr){
			xmllistindex = 0;
			res = -1;
			goto fail;
		}

		node = obj->nodesetval->nodeTab[xmllistindex];

		if(!node) {
			res = -ENODATA;
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
				res = -ENODATA;
				goto fail;
			}
		}

		*rtn = malloc(size);

		if (!*rtn) {
			res = -ENOMEM;
			goto fail;
		}

		if (nnv)
			sprintf(*rtn, "%s=%s", node->name, node->children ? (char *)node->children->content:"");
		else
			sprintf(*rtn, "%s", node->children ? node->children->content : node->name);

		if(list)
			strncpy(previous_query, query, PATH_MAX-1);

	}

fail:
	if(obj)
		xmlXPathFreeObject(obj);

	return res;
}

int ccs_get(int desc, const char *query, char **rtn){
	if(!fullxpathint)
		return _ccs_get_xpathlite(desc, query, rtn, 0);
	return _ccs_get_fullxpath(desc, query, rtn, 0);
}

int ccs_get_list(int desc, const char *query, char **rtn){
	if(!fullxpathint)
		return _ccs_get_xpathlite(desc, query, rtn, 1);
	return _ccs_get_fullxpath(desc, query, rtn, 1);
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
	return -ENOSYS;
}

/**
 * ccs_lookup_nodename
 * @cd: ccs descriptor
 * @nodename: node name string
 * @retval: pointer to location to assign the result, if found
 *
 * This function takes any valid representation (FQDN, non-qualified
 * hostname, IP address, IPv6 address) of a node's name and finds its
 * canonical name (per cluster.conf). This function will find the primary
 * node name if passed a node's "altname" or any valid representation
 * of it.
 *
 * Returns: 0 on success, < 0 on failure
 */
int ccs_lookup_nodename(int cd, const char *nodename, char **retval) {
	char path[256];
	char host_only[128];
	char *str;
	char *p;
	int error;
	int ret;
	unsigned int i;
	size_t nodename_len;
	struct addrinfo hints;

	if (nodename == NULL)
		return (-1);

	nodename_len = strlen(nodename);
	ret = snprintf(path, sizeof(path),
			"/cluster/clusternodes/clusternode[@name=\"%s\"]/@name", nodename);
	if (ret < 0 || (size_t) ret >= sizeof(path))
		return (-E2BIG);

	str = NULL;
	error = ccs_get(cd, path, &str);
	if (!error) {
		*retval = str;
		return (0);
	}

	if (nodename_len >= sizeof(host_only))
		return (-E2BIG);

	/* Try just the hostname */
	strcpy(host_only, nodename);
	p = strchr(host_only, '.');
	if (p != NULL) {
		*p = '\0';

		ret = snprintf(path, sizeof(path),
				"/cluster/clusternodes/clusternode[@name=\"%s\"]/@name",
				host_only);
		if (ret < 0 || (size_t) ret >= sizeof(path))
			return (-E2BIG);

		str = NULL;
		error = ccs_get(cd, path, &str);
		if (!error) {
			*retval = str;
			return (0);
		}
	}

	memset(&hints, 0, sizeof(hints));
	if (strchr(nodename, ':') != NULL)
		hints.ai_family = AF_INET6;
	else if (isdigit(nodename[nodename_len - 1]))
		hints.ai_family = AF_INET;
	else
		hints.ai_family = AF_UNSPEC;

	/*
	** Try to match against each clusternode in cluster.conf.
	*/
	for (i = 1 ; ; i++) {
		char canonical_name[128];
		unsigned int altcnt;

		ret = snprintf(path, sizeof(path),
				"/cluster/clusternodes/clusternode[%u]/@name", i);
		if (ret < 0 || (size_t) ret >= sizeof(path))
			continue;

		for (altcnt = 0 ; ; altcnt++) {
			size_t len;
			struct addrinfo *ai = NULL;
			char cur_node[128];

			if (altcnt != 0) {
				ret = snprintf(path, sizeof(path), 
					"/cluster/clusternodes/clusternode[%u]/altname[%u]/@name",
					i, altcnt);
				if (ret < 0 || (size_t) ret >= sizeof(path))
					continue;
			}

			str = NULL;
			error = ccs_get(cd, path, &str);
			if (error || !str) {
				if (altcnt == 0)
					goto out_fail;
				break;
			}

			if (altcnt == 0) {
				if (strlen(str) >= sizeof(canonical_name)) {
					free(str);
					return (-E2BIG);
				}
				strcpy(canonical_name, str);
			}

			if (strlen(str) >= sizeof(cur_node)) {
				free(str);
				return (-E2BIG);
			}

			strcpy(cur_node, str);

			p = strchr(cur_node, '.');
			if (p != NULL)
				len = p - cur_node;
			else
				len = strlen(cur_node);

			if (strlen(host_only) == len &&
				!strncasecmp(host_only, cur_node, len))
			{
				free(str);
				*retval = strdup(canonical_name);
				if (*retval == NULL)
					return (-ENOMEM);
				return (0);
			}

			if (getaddrinfo(str, NULL, &hints, &ai) == 0) {
				struct addrinfo *cur;

				for (cur = ai ; cur != NULL ; cur = cur->ai_next) {
					char hostbuf[512];
					if (getnameinfo(cur->ai_addr, cur->ai_addrlen,
							hostbuf, sizeof(hostbuf),
							NULL, 0,
							hints.ai_family != AF_UNSPEC ? NI_NUMERICHOST : 0))
					{
						continue;
					}

					if (!strcasecmp(hostbuf, nodename)) {
						freeaddrinfo(ai);
						free(str);
						*retval = strdup(canonical_name);
						if (*retval == NULL)
							return (-ENOMEM);
						return (0);
					}
				}
				freeaddrinfo(ai);
			}

			free(str);

			/* Now try any altnames */
		}
	}

out_fail:
	*retval = NULL;
	return (-1);
}
