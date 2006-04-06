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

#include "ccs.h"
#include "objdb.h"
#include "config.h"
#include "logging.h"

static void read_config_for(int ccs_fd, struct objdb_iface_ver0 *objdb, char *key)
{
	int error;
	char *str;
	unsigned int object_handle;
	char path[256];

	sprintf(path, "/cluster/%s/@*", key);

	objdb->object_create (OBJECT_PARENT_HANDLE, &object_handle,
			      key, strlen (key));

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
			P_DAEMON("got config item %s: '%s' = '%s'\n", key, str, equal+1);
			objdb->object_key_create (object_handle, str, strlen (str),
						  equal+1, strlen(equal+1)+1);
		}
		free(str);

		/* TODO Hierarchies ?? */
	}
}

void init_config(struct objdb_iface_ver0 *objdb)
{
	int cd;

	cd = ccs_force_connect(NULL, 0);
	if (cd < 0)
		return;

	/* These first few are just versions of openais.conf */
	read_config_for(cd, objdb, "totem");
	read_config_for(cd, objdb, "logging");
	read_config_for(cd, objdb, "event");
	read_config_for(cd, objdb, "aisexec");
	/* "group" isn't really useful without hierarchies */
	read_config_for(cd, objdb, "group");

	/* This is stuff specific to us, eg quorum device timeout */
	read_config_for(cd, objdb, "cman");

	ccs_disconnect(cd);
}
