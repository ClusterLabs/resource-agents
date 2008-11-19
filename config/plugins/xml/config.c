#include <string.h>
#include <syslog.h>

#include <libxml/tree.h>

#include <corosync/lcr/lcr_comp.h>
#include <corosync/engine/objdb.h>
#include <corosync/engine/config.h>

#include "logging.h"

static int xml_readconfig(struct objdb_iface_ver0 *objdb, char **error_string);
static int xml_reloadconfig(struct objdb_iface_ver0 *objdb, int flush,
			    char **error_string);
static int init_config(struct objdb_iface_ver0 *objdb, char *configfile,
		       char *error_string);
static char error_reason[1024];

#define DEFAULT_CONFIG DEFAULT_CONFIG_DIR "/" DEFAULT_CONFIG_FILE

/*
 * Exports the interface for the service
 */

static struct config_iface_ver0 xmlconfig_iface_ver0 = {
	.config_readconfig = xml_readconfig,
	.config_reloadconfig = xml_reloadconfig
};

static struct lcr_iface ifaces_ver0[2] = {
	{
	 .name = "xmlconfig",
	 .version = 0,
	 .versions_replace = 0,
	 .versions_replace_count = 0,
	 .dependencies = 0,
	 .dependency_count = 0,
	 .constructor = NULL,
	 .destructor = NULL,
	 .interfaces = NULL,
	 }
};

static struct lcr_comp xml_comp_ver0 = {
	.iface_count = 1,
	.ifaces = ifaces_ver0,
};

__attribute__ ((constructor))
static void xml_comp_register(void)
{
	lcr_interfaces_set(&ifaces_ver0[0], &xmlconfig_iface_ver0);
	lcr_component_register(&xml_comp_ver0);
};

static void addkeys(xmlAttrPtr tmpattr, struct objdb_iface_ver0 *objdb,
		    unsigned int object_handle)
{
	for (tmpattr = tmpattr; tmpattr; tmpattr = tmpattr->next) {
		if (tmpattr->type == XML_ATTRIBUTE_NODE)
			objdb->object_key_create(object_handle,
						 (char *)tmpattr->name,
						 strlen((char *)tmpattr->name),
						 (char *)tmpattr->children->
						 content,
						 strlen((char *)tmpattr->
							children->content) + 1);
	}
}

static void xml2objdb(xmlNodePtr tmpnode, struct objdb_iface_ver0 *objdb,
		      unsigned int parent)
{
	unsigned int object_handle = 0;

	for (tmpnode = tmpnode; tmpnode; tmpnode = tmpnode->next) {
		if (tmpnode->type == XML_ELEMENT_NODE) {
			objdb->object_create(parent, &object_handle,
					     (char *)tmpnode->name,
					     strlen((char *)tmpnode->name));
			if (tmpnode->properties)
				addkeys(tmpnode->properties, objdb,
					object_handle);
		}
		xml2objdb(tmpnode->children, objdb, object_handle);
	}
}

static int xml_reloadconfig(struct objdb_iface_ver0 *objdb, int flush,
			    char **error_string)
{
	return xml_readconfig(objdb, error_string);
}

static int xml_readconfig(struct objdb_iface_ver0 *objdb, char **error_string)
{
	int ret = 0;
	char *configfile = DEFAULT_CONFIG;

	/* We need to set this up to internal defaults too early */
	openlog("corosync", LOG_CONS | LOG_PID, SYSLOGFACILITY);

	if (getenv("COROSYNC_CLUSTER_CONFIG_FILE"))
		configfile = getenv("COROSYNC_CLUSTER_CONFIG_FILE");

	/* Read low-level totem/aisexec etc config from cluster.conf */
	if (!(ret = init_config(objdb, configfile, error_reason)))
		sprintf(error_reason, "Successfully read config from %s\n",
			configfile);
	else
		sprintf(error_reason, "Unable to read config from %s\n",
			configfile);

	*error_string = error_reason;

	return ret;
}

static int init_config(struct objdb_iface_ver0 *objdb, char *configfile,
		       char *error_string)
{
	int err = 0;
	xmlDocPtr doc = NULL;
	xmlNodePtr root_node = NULL;

	/* openfile */

	doc = xmlParseFile(configfile);
	if (!doc) {
		err = -1;
		goto fail;
	}

	root_node = xmlDocGetRootElement(doc);
	if (!root_node) {
		err = -1;
		goto fail;
	}

	/* load it in objdb */
	xml2objdb(root_node, objdb, OBJECT_PARENT_HANDLE);

fail:
	if (doc)
		xmlFreeDoc(doc);

	xmlCleanupParser();

	return err;
}
