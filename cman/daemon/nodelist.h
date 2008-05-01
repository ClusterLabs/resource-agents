/******************************************************************************
 *******************************************************************************
 ***
 ***  Copyright (C) 2005-2008 Red Hat, Inc.  All rights reserved.
 ***
 ***  This copyrighted material is made available to anyone wishing to use,
 ***  modify, copy, or redistribute it subject to the terms and conditions
 ***  of the GNU General Public License v.2.
 ***
 *******************************************************************************
 ******************************************************************************/


/* Helper functions for navigating the nodes list */
static unsigned int nodeslist_init(struct objdb_iface_ver0 *objdb,
				   unsigned int cluster_parent_handle,
				   unsigned int *parent_handle)
{
	unsigned int object_handle;

	objdb->object_find_reset(cluster_parent_handle);
	if (objdb->object_find(cluster_parent_handle,
			       "clusternodes", strlen("clusternodes"),
			       &object_handle) == 0)
	{
		unsigned int nodes_handle;
		objdb->object_find_reset(object_handle);

		if (objdb->object_find(object_handle,
				       "clusternode", strlen("clusternode"),
				       &nodes_handle) == 0)
		{
			*parent_handle = object_handle;
			return nodes_handle;
		}
	}
	return 0;
}

static unsigned int nodeslist_next(struct objdb_iface_ver0 *objdb, unsigned int parent_handle)
{
	unsigned int nodes_handle;

	if (objdb->object_find(parent_handle,
			       "clusternode", strlen("clusternode"),
			       &nodes_handle) == 0)
		return nodes_handle;
	else
		return 0;
}

static unsigned int nodelist_byname(struct objdb_iface_ver0 *objdb,
				    unsigned int cluster_parent_handle,
				    char *name)
{
	char *nodename;
	unsigned int nodes_handle;
	unsigned int parent_handle;

	nodes_handle = nodeslist_init(objdb, cluster_parent_handle, &parent_handle);
	while (nodes_handle) {
		if (objdb_get_string(objdb, nodes_handle, "name", &nodename)) {
			break;
		}
		if (strcmp(nodename, name) == 0)
			return nodes_handle;

		nodes_handle = nodeslist_next(objdb, parent_handle);
	}

	return 0;
}
