#include <string.h>
#include <limits.h>
#include <syslog.h>
#include <arpa/inet.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include <corosync/lcr/lcr_comp.h>
#include <corosync/engine/objdb.h>
#include <corosync/engine/config.h>

#include "logging.h"

static int xml_readconfig(struct objdb_iface_ver0 *objdb, char **error_string);
static int init_config(struct objdb_iface_ver0 *objdb, char *configfile, char *error_string);
static char error_reason[1024];
static int xmllistindex = 0;
static char previous_query[PATH_MAX];

#define DEFAULT_CONFIG DEFAULT_CONFIG_DIR "/" DEFAULT_CONFIG_FILE

/*
 * Exports the interface for the service
 */

static struct config_iface_ver0 xmlconfig_iface_ver0 = {
	.config_readconfig        = xml_readconfig
};

static struct lcr_iface ifaces_ver0[2] = {
	{
		.name		       	= "xmlconfig",
		.version	       	= 0,
		.versions_replace      	= 0,
		.versions_replace_count	= 0,
		.dependencies	       	= 0,
		.dependency_count      	= 0,
		.constructor	       	= NULL,
		.destructor	       	= NULL,
		.interfaces	       	= NULL,
	}
};

static struct lcr_comp xml_comp_ver0 = {
	.iface_count				= 1,
	.ifaces					= ifaces_ver0,
};

__attribute__ ((constructor)) static void xml_comp_register(void) {
	lcr_interfaces_set(&ifaces_ver0[0], &xmlconfig_iface_ver0);
	lcr_component_register(&xml_comp_ver0);
};

static char *do_xml_query(xmlXPathContextPtr ctx, char *query, int list) {
	xmlXPathObjectPtr obj = NULL;
	xmlNodePtr node = NULL;
	char *rtn = NULL;
	int size = 0, nnv = 0;

	if (list && !strcmp(query, previous_query))
		xmllistindex++;
	else {
		memset(previous_query, 0, PATH_MAX);
		xmllistindex = 0;
	}

	obj = xmlXPathEvalExpression((xmlChar *)query, ctx);
	if (obj && obj->nodesetval && (obj->nodesetval->nodeNr > 0)) {
		if (xmllistindex >= obj->nodesetval->nodeNr) {
			memset(previous_query, 0, PATH_MAX);
			xmllistindex = 0;
			goto fail;
		}

		node = obj->nodesetval->nodeTab[xmllistindex];
		if (!node)
			goto fail;

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
			else
				goto fail;
		}

		rtn = malloc(size);
		if (!rtn)
			goto fail;

		if (nnv)
			sprintf(rtn, "%s=%s", node->name, node->children ? (char *)node->children->content:"");
		else
			sprintf(rtn, "%s", node->children ? node->children->content : node->name);

		if(list)
			strncpy(previous_query, query, PATH_MAX-1);
	}

fail:
	if (obj)
		xmlXPathFreeObject(obj);

	return rtn;
}

static int should_alloc(xmlXPathContextPtr ctx, char *key)
{
	int keyerror = 1, childerr = 1;
	char path[256];
	char *str = NULL;

	sprintf(path, "%s/@*", key);
	str = do_xml_query(ctx, path, 1);
	if (str) {
		keyerror = 0;
		free(str);
		str = NULL;
	}

	sprintf(path, "%s/child::*", key);
	str = do_xml_query(ctx, path, 1);
	if(str) {
		childerr = 0;
		free(str);
		str = NULL;
	}

	if (childerr && keyerror)
		return 0;

	return 1;
}

static int read_config_for(xmlXPathContextPtr ctx, struct objdb_iface_ver0 *objdb, unsigned int parent,
			   char *object, char *key, int always_create)
{
	char *str;
	unsigned int object_handle = 0;
	char path[256];
	int gotcount = 0;
	char *subkeys[52];
	int subkeycount = 0;
	int i;

	if (should_alloc(ctx, key) || always_create) {
		objdb->object_create(parent, &object_handle, object, strlen(object));
	}

	memset(previous_query, 0, PATH_MAX);
	sprintf(path, "%s/@*", key);
	/* Get the keys */
	for (;;)
	{
		char *equal;

		str = do_xml_query(ctx, path, 1);
		if (!str)
                        break;

		equal = strchr(str, '=');
		if (equal)
		{
			*equal = 0;
			objdb->object_key_create(object_handle, str, strlen(str),
						 equal+1, strlen(equal+1)+1);
			gotcount++;
		}
		free(str);
	}

	/* Now look for sub-objects.
	   CCS can't cope with recursive queries so we have to store the result of
	   the subkey search */
	memset(previous_query, 0, PATH_MAX);
	memset(subkeys, 0, sizeof(subkeys));
	sprintf(path, "%s/child::*", key);
	for (;;)
	{
		char *equal;

		str = do_xml_query(ctx, path, 1);
		if (!str)
                        break;

		/* CCS returns duplicate values for the numbered entries we use below.
		   eg. if there are 4 <clusternode/> entries it will return
		     clusternode=
		     clusternode=
		     clusternode=
		     clusternode=
		   which is not helpful to us cos we retrieve them as
		     clusternode[1]
		     clusternode[2]
		     clusternode[3]
		     clusternode[4]
		   so we just store unique keys.
		*/
		equal = strchr(str, '=');
		if (equal)
			*equal = 0;

		if (subkeycount > 0 && strcmp(str, subkeys[subkeycount-1]) == 0)
		{
			free(str);
			break;
		}
		subkeys[subkeycount++] = str;
	}

	for (i=0; i<subkeycount; i++)
	{
		int count = 0;
		str = subkeys[i];
		gotcount++;

		for (;;)
		{
			char subpath[1024];

			/* Found a subkey, iterate through it's sub sections */
			sprintf(subpath, "%s/%s[%d]", key, str, ++count);
			if (!read_config_for(ctx, objdb, object_handle, str, subpath, 0))
				break;
		}
		free(str);
	}
	return gotcount;
}

static int xml_readconfig(struct objdb_iface_ver0 *objdb, char **error_string)
{
	int ret = 0;
	char *configfile = DEFAULT_CONFIG;

	/* We need to set this up to internal defaults too early */
	openlog("corosync", LOG_CONS|LOG_PID, SYSLOGFACILITY);

	if(getenv("COROSYNC_CLUSTER_CONFIG_FILE"))
		configfile = getenv("COROSYNC_CLUSTER_CONFIG_FILE");

	/* Read low-level totem/aisexec etc config from cluster.conf */
	if ( !(ret = init_config(objdb, configfile, error_reason)) )
		sprintf (error_reason, "Successfully read config from %s\n", configfile);
	else
		sprintf (error_reason, "Unable to read config from %s\n", configfile);

        *error_string = error_reason;

	return ret;
}


static int init_config(struct objdb_iface_ver0 *objdb, char *configfile, char *error_string)
{
	int err = 0;
	xmlDocPtr doc = NULL;
	xmlXPathContextPtr ctx = NULL;

	/* openfile */

	doc = xmlParseFile(configfile);
	if (!doc) {
		err = -1;
		goto fail;
	}

	ctx = xmlXPathNewContext(doc);
	if (!ctx) {
		err = -1;
		goto fail;
	}

	/* load it in objdb */

	/* These first few are just versions of openais.conf */
	read_config_for(ctx, objdb, OBJECT_PARENT_HANDLE, "totem", "/cluster/totem", 1);
	read_config_for(ctx, objdb, OBJECT_PARENT_HANDLE, "logging", "/cluster/logging", 1);
	read_config_for(ctx, objdb, OBJECT_PARENT_HANDLE, "event", "/cluster/event", 1);
	read_config_for(ctx, objdb, OBJECT_PARENT_HANDLE, "aisexec", "/cluster/aisexec", 1);
	read_config_for(ctx, objdb, OBJECT_PARENT_HANDLE, "amf", "/cluster/amf", 1);

	/* This is stuff specific to us, eg quorum device timeout */
	read_config_for(ctx, objdb, OBJECT_PARENT_HANDLE, "cluster", "/cluster", 1);

fail:

	if (ctx)
		xmlXPathFreeContext(ctx);

	if (doc)
		xmlFreeDoc(doc);

	return err;
}
