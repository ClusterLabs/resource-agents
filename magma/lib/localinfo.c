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
#include <pthread.h>
#include <string.h>

static pthread_mutex_t localid_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint64_t _local_nodeid = (uint64_t)0;
static char *_local_nodename = NULL;


static int
_get_local_info(char *groupname)
{
	int x;

	cluster_member_list_t *members;

	if (_local_nodeid != (uint64_t)0) {
		return 0;
	}
		
	members = clu_member_list(groupname);

	if (!members)
		return -1;

        for (x=0; x < members->cml_count; x++) {
		if (ip_lookup(members->cml_members[x].cm_name, NULL) == 0) {
			_local_nodeid = members->cml_members[x].cm_id;
			_local_nodename =
				strdup(members->cml_members[x].cm_name);
			break;
		}
	}

	free(members);

	return (_local_nodename ? 0 : -1);
}


/**
  Returns the local node name using the default plugin as the data source.
  This function caches this information for future use.

  @param groupname	Group name.  If the local node is not a member
  			of this group, the call will fail.
  @param name		Preallocated char array into which the local member's
  			node name is copied.
  @param namelen	Size, in bytes, of name parameter.
  @return		0 on success, or -1 if the node is not a member of
  			the specified group.
 */
int
clu_local_nodename(char *groupname, char *name, size_t namelen)
{
	pthread_mutex_lock(&localid_mutex);

	if (!_local_nodename) {
		if (_get_local_info(groupname) < 0) {
			pthread_mutex_unlock(&localid_mutex);
			return -1;
		}
	}

	strncpy(name, _local_nodename, namelen);

	pthread_mutex_unlock(&localid_mutex);
	
	return 0;
}


/**
  Returns the local node ID using the default plugin as the data source.
  This function caches this information for future use.

  @param groupname	Group name.  If the local node is not a member
  			of this group, the call will fail.
  @param nodeid		Pointer to node ID (uint64_t).  Node ID
  			is copied in here.
  @return		0 on success, or -1 if the node is not a member of
  			the specified group.
 */
int
clu_local_nodeid(char *groupname, uint64_t *nodeid)
{
	pthread_mutex_lock(&localid_mutex);

	if (!_local_nodeid) {
		if (_get_local_info(groupname) < 0) {
			pthread_mutex_unlock(&localid_mutex);
			return -1;
		}
	}

	*nodeid = _local_nodeid;
	pthread_mutex_unlock(&localid_mutex);

	return 0;
}
