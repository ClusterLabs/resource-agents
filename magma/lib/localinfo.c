/*
  Copyright Red Hat, Inc. 2004

  The Magma Cluster API Library is free software; you can redistribute
  it and/or modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either version
  2.1 of the License, or (at your option) any later version.

  The Magma Cluster API Library is distributed in the hope that it will
  be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
  of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
 */
/** @file
  Local information lookup code for Magma.
 */
#include <magma.h>
#include <stdint.h>
#include <ip_lookup.h>
#include <string.h>

static int
_get_local_info(cluster_plugin_t *cpp, char *groupname)
{
	int x, ret = -1;
	cluster_member_list_t *members;

	if (cpp->cp_private.p_localid != NODE_ID_NONE) {
		return 0;
	}
		
	members = cp_member_list(cpp, groupname);

	if (!members) 
		return -1;

        for (x=0; x < members->cml_count; x++) {
		if (ip_lookup(members->cml_members[x].cm_name, NULL) == 0) {
			cpp->cp_private.p_localid =
				members->cml_members[x].cm_id;
			strncpy(cpp->cp_private.p_localname,
				members->cml_members[x].cm_name,
				sizeof(cpp->cp_private.p_localname)-1);
			ret = 0;
			break;
		}
	}

	free(members);

	return ret;
}


/**
  Returns the local node name using the given plugin as the data source.
  This function caches this information in the provided cpp plugin structure
  for future use.

  @param cpp		Cluster plugin structure.  Should already be opened,
  			but does not need to be logged in to a group.
  @param groupname	Group name.  If the local node is not a member
  			of this group, the call will fail.
  @param name		Preallocated char array into which the local member's
  			node name is copied.
  @param namelen	Size, in bytes, of name parameter.
  @return		0 on success, or -1 if the node is not a member of
  			the specified group.
 */
int
cp_local_nodename(cluster_plugin_t *cpp, char *groupname, char *name,
		  size_t namelen)
{
	if (!cpp) {
		errno = EINVAL;
		return -1;
	}

	if (cpp->cp_private.p_localid == NODE_ID_NONE) {
		if (_get_local_info(cpp, groupname) < 0) {
			return -1;
		}
	}

	strncpy(name, cpp->cp_private.p_localname, namelen);

	return 0;
}


/**
  Returns the local node ID using the given plugin as the data source.
  This function caches this information for future use.

  @param cpp		Cluster plugin structure.  Should already be opened,
  			but does not need to be logged in to a group.
  @param groupname	Group name.  If the local node is not a member
  			of this group, the call will fail.
  @param nodeid		Pointer to node ID (uint64_t).  Node ID
  			is copied in here.
  @return		0 on success, or -1 if the node is not a member of
  			the specified group.
 */
int
cp_local_nodeid(cluster_plugin_t *cpp, char *groupname,
		uint64_t *nodeid)
{
	if (!cpp) {
		errno = EINVAL;
		return -1;
	}

	if (cpp->cp_private.p_localid == NODE_ID_NONE) {
		if (_get_local_info(cpp, groupname) < 0) {
			return -1;
		}
	}

	*nodeid = cpp->cp_private.p_localid;

	return 0;
}
