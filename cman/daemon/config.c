/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openais/service/objdb.h>
#include "ccs.h"
#include "logging.h"

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

	if (always_create) {
		objdb->object_create(parent, &object_handle, object, strlen(object));
	}
	sprintf(path, "/cluster/%s/@*", key);

	/* Get the keys */
	for (;;)
	{
		char *equal;

		error = ccs_get_list(ccs_fd, path, &str);
		if (error || !str)
                        break;

		if (!object_handle) {
			objdb->object_create(parent, &object_handle, object, strlen(object));
		}

		equal = strchr(str, '=');
		if (equal)
		{
			*equal = 0;
			P_DAEMON("CCS: got config item %s: '%s' = '%s'\n", object, str, equal+1);
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
	sprintf(path, "/cluster/%s/child::*", key);
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

void init_config(struct objdb_iface_ver0 *objdb)
{
	int cd;

	cd = ccs_force_connect(NULL, 0);
	if (cd < 0)
		return;

	/* These first few are just versions of openais.conf */
	read_config_for(cd, objdb, OBJECT_PARENT_HANDLE, "totem", "totem", 1);
	read_config_for(cd, objdb, OBJECT_PARENT_HANDLE, "logging", "logging", 1);
	read_config_for(cd, objdb, OBJECT_PARENT_HANDLE, "event", "event", 1);
	read_config_for(cd, objdb, OBJECT_PARENT_HANDLE, "aisexec", "aisexec", 1);
	read_config_for(cd, objdb, OBJECT_PARENT_HANDLE, "group", "group", 1);

	/* This is stuff specific to us, eg quorum device timeout */
	read_config_for(cd, objdb, OBJECT_PARENT_HANDLE, "cman", "cman", 1);

	ccs_disconnect(cd);
}
