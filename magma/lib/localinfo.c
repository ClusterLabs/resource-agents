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
static uint64_t __local_nodeid = (uint64_t)0;
static char *__local_nodename = NULL;


static int
__get_local_info(char *groupname)
{
	int x;

	cluster_member_list_t *members;

	if (__local_nodeid != (uint64_t)0) {
		return 0;
	}
		
	members = clu_member_list(groupname);

	if (!members)
		return -1;

        for (x=0; x < members->cml_count; x++) {
		if (ip_lookup(members->cml_members[x].cm_name, NULL) == 0) {
			__local_nodeid = members->cml_members[x].cm_id;
			__local_nodename =
				strdup(members->cml_members[x].cm_name);
			break;
		}
	}

	free(members);

	return (__local_nodename ? 0 : -1);
}


int
clu_local_nodename(char *groupname, char *name, size_t namelen)
{
	pthread_mutex_lock(&localid_mutex);

	if (!__local_nodename) {
		if (__get_local_info(groupname) < 0) {
			pthread_mutex_unlock(&localid_mutex);
			return -1;
		}
	}

	strncpy(name, __local_nodename, namelen);

	pthread_mutex_unlock(&localid_mutex);
	
	return 0;
}


int
clu_local_nodeid(char *groupname, uint64_t *nodeid)
{
	pthread_mutex_lock(&localid_mutex);

	if (!__local_nodeid) {
		if (__get_local_info(groupname) < 0) {
			pthread_mutex_unlock(&localid_mutex);
			return -1;
		}
	}

	*nodeid = __local_nodeid;
	pthread_mutex_unlock(&localid_mutex);

	return 0;
}
