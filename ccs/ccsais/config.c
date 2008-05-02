/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2007-2008 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <netinet/in.h>

/* openais headers */
#include <openais/service/objdb.h>
#include "list.h"
#include "cnxman-socket.h"
#include "cnxman-private.h"
#include <openais/service/swab.h>
#include <openais/totem/totemip.h>
#include <openais/totem/totempg.h>
#include <openais/totem/aispoll.h>
#include <openais/service/service.h>
#include <openais/service/config.h>
#include <openais/lcr/lcr_comp.h>
#include <openais/service/swab.h>

#include "libccscompat.h"
#include "logging.h"

#define CONFIG_VERSION_PATH	"/cluster/@config_version"
#define CONFIG_NAME_PATH	"/cluster/@name"

static int ccs_readconfig(struct objdb_iface_ver0 *objdb, char **error_string);
static int init_config(struct objdb_iface_ver0 *objdb, char *error_string);
static char error_reason[1024];

/*
 * Exports the interface for the service
 */

static struct config_iface_ver0 ccsconfig_iface_ver0 = {
	.config_readconfig        = ccs_readconfig
};

static struct lcr_iface ifaces_ver0[2] = {
	{
		.name		       	= "ccsconfig",
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

static struct lcr_comp ccs_comp_ver0 = {
	.iface_count				= 1,
	.ifaces					= ifaces_ver0,
};



__attribute__ ((constructor)) static void ccs_comp_register(void) {
	lcr_interfaces_set(&ifaces_ver0[0], &ccsconfig_iface_ver0);
	lcr_component_register(&ccs_comp_ver0);
};

static int should_alloc(int ccs_fd, char *key)
{
	int keyerror, childerr;
	char path[256];
	char *str = NULL;

	sprintf(path, "%s/@*", key);
	keyerror = ccs_get_list(ccs_fd, path, &str);
	if(str) {
		free(str);
		str = NULL;
	}

	sprintf(path, "%s/child::*", key);
	childerr = ccs_get_list(ccs_fd, path, &str);
	if(str)
		free(str);

	if (childerr && keyerror)
		return 0;

	return 1;
}

static int read_config_for(int ccs_fd, struct objdb_iface_ver0 *objdb, unsigned int parent,
			   char *object, char *key, int always_create)
{
	int error;
	char *str;
	unsigned int object_handle = 0;
	char path[256];
	int gotcount = 0;
	char *subkeys[52];
	int subkeycount = 0;
	int i;

	if (should_alloc(ccs_fd, key) || always_create)
		objdb->object_create(parent, &object_handle, object, strlen(object));

	sprintf(path, "%s/@*", key);

	/* Get the keys */
	for (;;)
	{
		char *equal;

		error = ccs_get_list(ccs_fd, path, &str);
		if (error || !str)
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
	memset(subkeys, 0, sizeof(subkeys));
	sprintf(path, "%s/child::*", key);
	for (;;)
	{
		char *equal;

		error = ccs_get_list(ccs_fd, path, &str);
		if (error || !str)
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
			if (!read_config_for(ccs_fd, objdb, object_handle, str, subpath, 0))
				break;
		}
		free(str);
	}
	return gotcount;
}

static int ccs_readconfig(struct objdb_iface_ver0 *objdb, char **error_string)
{
	int ret;

	/* We need to set this up to internal defaults too early */
	openlog("openais", LOG_CONS|LOG_PID, LOG_LOCAL4);

	/* Read low-level totem/aisexec etc config from CCS */
	if ( !(ret = init_config(objdb, error_reason)) )
	    sprintf (error_reason, "%s", "Successfully read config from CCS\n");

        *error_string = error_reason;

	return ret;
}


static int init_config(struct objdb_iface_ver0 *objdb, char *error_string)
{
	int cd;
	char *cname = NULL;

	/* Connect to ccsd */
	if (getenv("CCS_CLUSTER_NAME")) {
		cname = getenv("CCS_CLUSTER_NAME");
	}

	cd = ccs_force_connect(cname, 0);
	if (cd < 0) {
		strcpy(error_string, "Error connecting to CCS to get configuration. Check ccsd is running");
		return -1;
	}

	/* These first few are just versions of openais.conf */
	read_config_for(cd, objdb, OBJECT_PARENT_HANDLE, "totem", "/cluster/totem", 1);
	read_config_for(cd, objdb, OBJECT_PARENT_HANDLE, "logging", "/cluster/logging", 1);
	read_config_for(cd, objdb, OBJECT_PARENT_HANDLE, "event", "/cluster/event", 1);
	read_config_for(cd, objdb, OBJECT_PARENT_HANDLE, "aisexec", "/cluster/aisexec", 1);
	read_config_for(cd, objdb, OBJECT_PARENT_HANDLE, "amf", "/cluster/amf", 1);

	/* This is stuff specific to us, eg quorum device timeout */
	read_config_for(cd, objdb, OBJECT_PARENT_HANDLE, "cluster", "/cluster", 1);

	ccs_disconnect(cd);
	return 0;
}
