/*
  Copyright Red Hat, Inc. 2002-2003

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to the
  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge,
  MA 02139, USA.
*/
/** @file
 * High level Magma APIs
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <magma.h>
#include <pthread.h>
#include <assert.h>
#include <errno.h>
#include <magmamsg.h>
#include "clist.h"

static cluster_plugin_t *_cpp = NULL;	/** Default cluster plugin pointer */
static char _connected = 0;		/** Did we connect? */
static pthread_rwlock_t dflt_lock = PTHREAD_RWLOCK_INITIALIZER;
static void _clu_set_default(cluster_plugin_t *cpp);
static void _clu_clear_default(void);


/**
  Searches PLUGINDIR for a plugin which can successfully connect to the
  cluster infrastructure (locking/fencing/quorum/membership) which is 
  running locally.
 
  @return		A file descriptor on success; -1 on failure.
  @see clu_disconnect
 */
int
clu_connect(char *groupname, int login)
{
	DIR *dir;
	int fd, ret;
	struct dirent *entry;
	cluster_plugin_t *cp;
	char filename[1024];

	pthread_rwlock_wrlock(&dflt_lock);
	if (_cpp) {
		pthread_rwlock_unlock(&dflt_lock);
		return 0;
	}

	dir = opendir(PLUGINDIR);
	if (!dir){
		pthread_rwlock_unlock(&dflt_lock);
		return -1;
	}
	while ((entry = readdir(dir)) != NULL) {
		snprintf(filename, sizeof(filename), "%s/%s", PLUGINDIR,
			 entry->d_name);

		cp = cp_load(filename);
		if (cp == NULL)
			continue;

#ifdef DEBUG
		cp_null(cp);
		fflush(stdout);
#endif

		if (cp_init(cp, NULL, 0) < 0) {
			cp_unload(cp);
			cp = NULL;
			continue;
		}

		fd = cp_open(cp);
		if (fd < 0) {
			cp_unload(cp);
			cp = NULL;
			continue;
		}

		if (login) {
			ret = cp_login(cp, fd, groupname);
	       		if ((ret < 0) && (ret != -ENOSYS)) {
				cp_close(cp, fd);
				cp_unload(cp);
				cp = NULL;
				continue;
			}
		}

		_cpp = cp;
		_clu_set_default(_cpp);
		_connected = 1;
		
		closedir(dir);
		pthread_rwlock_unlock(&dflt_lock);

		/* Don't allow msg_close() to close this socket */
		if (fd >= 0)
			clist_insert(fd, MSG_CONNECTED);
		return fd;
	}

	closedir(dir);
	pthread_rwlock_unlock(&dflt_lock);
	return -1;
}


/**
  Returns nonzero if the default plugin is initialized and we are connected
  to a cluster infrastructure (or we think we are).

  @return	0 for not connected, 1 for connected.
 */
int
clu_connected(void)
{
	int ret;
	
	pthread_rwlock_rdlock(&dflt_lock);
	ret = !!_connected;
	pthread_rwlock_unlock(&dflt_lock);

	return ret;
}


/**
  Disconnects from the cluster, cleans up, and unloads the default cluster
  plugin.
 
  @return		Should always return 0, but implementation dependent
  			based on the cluster plugin.
  @see clu_connect
 */
int
clu_disconnect(int fd)
{
	int rv;

	if (fd >= 0)
		clist_delete(fd);

	pthread_rwlock_wrlock(&dflt_lock);
	if (_cpp) {
		cp_logout(_cpp, fd);
		cp_close(_cpp, fd);
		rv = cp_unload(_cpp);
		if (rv == 0)
			_cpp = NULL;
		_connected = 0;
	}
	pthread_rwlock_unlock(&dflt_lock);

	return rv;
}


/**
  Wrapper around the default plugin for calling the null function.
 */
int
clu_null(void)
{
	int ret;
	pthread_rwlock_rdlock(&dflt_lock);
	assert(_cpp);
	ret = cp_null(_cpp);
	pthread_rwlock_unlock(&dflt_lock);
	return ret;
}


/**
  Obtain a list of cluster members given the group name.  This should only
  return the nodes which are both online and a member of the specified
  group.  Infrastructures which do not support the notion of node groups
  should return the list of all cluster members.

  @param groupname	Name of group
  @return		NULL on failure (see errno for reason), or a newly
  			allocated cluster_member_list_t pointer.

 */
cluster_member_list_t *
clu_member_list(char *groupname)
{
	cluster_member_list_t * ret;
	pthread_rwlock_rdlock(&dflt_lock);
	assert(_cpp);
	ret = cp_member_list(_cpp, groupname);
	pthread_rwlock_unlock(&dflt_lock);
	return ret;
}


/**
  Obtain the cluster's status regarding quorum (e.g. quorate vs. not), as well
  as whether or not the local node is a member of the specified group.
  Infrastructures which do not support the notion of node groups
  should return the list of all cluster members.

  @param groupname	Name of group
  @return		-1 on failure (see errno for reason), or a mixture of
  			QF_QUORATE and QF_GROUPMEMBER flags.

 */
int
clu_quorum_status(char *groupname)
{
	int ret;
	pthread_rwlock_rdlock(&dflt_lock);
	assert(_cpp);
	ret = cp_quorum_status(_cpp, groupname);
	pthread_rwlock_unlock(&dflt_lock);
	return ret;
}


/**
  Call the default plugin's version function.  This can be used to find out
  which cluster plugin is in use by applications.

  @return		Immutable cluster plugin version string.
 */
char *
clu_plugin_version(void)
{
	char *ret;
	pthread_rwlock_rdlock(&dflt_lock);
	assert(_cpp);
	ret = cp_plugin_version(_cpp);
	pthread_rwlock_unlock(&dflt_lock);
	return ret;
}


/**
  Obtain an event from the cluster file descriptor specified using the default
  cluster plugin. If no events are waiting, this call may block.  After an
  event is received, it is up to the application to handle it properly.  For
  instance, when a membership change event occurs, it is up to the application
  to attain a new membership list and update magmamsg's internal table (if in
  use).

  @param fd		File descriptor to obtain event from.
  @return		Event identifier.
 */
int
clu_get_event(int fd)
{
	int ret;
	pthread_rwlock_rdlock(&dflt_lock);
	assert(_cpp);
	ret = cp_get_event(_cpp, fd);
	pthread_rwlock_unlock(&dflt_lock);
	return ret;
}


/**
  Open a connection to the cluster using the default plugin.  This is not
  needed if clu_connect is used.

  @return		File descriptor on success, or -1 on failure.
  @see			clu_connect
 */
int
clu_open(void)
{
	int ret;
	pthread_rwlock_wrlock(&dflt_lock);
	assert(_cpp);
	ret = cp_open(_cpp);
	pthread_rwlock_unlock(&dflt_lock);

	if (ret >= 0)
		clist_insert(ret, MSG_CONNECTED);

	return ret;
}


/**
  Open a connection to the cluster using the default plugin.  This is not
  needed if clu_connect is used.

  @return		File descriptor on success, or -1 on failure.
  @see			clu_connect
 */
int
clu_login(int fd, char *groupname)
{
	int ret;
	pthread_rwlock_wrlock(&dflt_lock);
	assert(_cpp);
	ret = cp_login(_cpp, fd, groupname);
	pthread_rwlock_unlock(&dflt_lock);
	return ret;
}


/**
  Log out of the subscribed group using the default plugin.  This is not
  needed if clu_disconnect is used.

  @return		File descriptor on success, or -1 on failure.
  @see			clu_disconnect
 */
int
clu_logout(int fd)
{
	int ret;
	pthread_rwlock_wrlock(&dflt_lock);
	assert(_cpp);
	ret = cp_logout(_cpp, fd);
	pthread_rwlock_unlock(&dflt_lock);
	return ret;
}


/**
  Close a connection to the cluster using the default plugin.  This is not
  needed if clu_disconnect is used.

  @return		File descriptor on success, or -1 on failure.
  @see			clu_open clu_connect
 */
int
clu_close(int fd)
{
	int ret;

	if (fd >= 0)
		clist_delete(fd);

	pthread_rwlock_wrlock(&dflt_lock);
	assert(_cpp);
	ret = cp_close(_cpp, fd);
	pthread_rwlock_unlock(&dflt_lock);
	return ret;
}


/**
  Fence a given cluster node via the default cluster plugin.  This requires
  the cluster_member_t structure because the infrastructure may use either
  the node name or the node ID to perform the fencing operation.

  @param node		cluster_member_t containing hostname/node ID of member
  			to fence.
  @return		0 on success, other value on failure.
 */
int
clu_fence(cluster_member_t *node)
{
	int ret;
	pthread_rwlock_wrlock(&dflt_lock);
	assert(_cpp);
	ret = cp_fence(_cpp, node);
	pthread_rwlock_unlock(&dflt_lock);
	return ret;
}


/**
  Obtain a cluster lock using the default plugin.

  @param resource	Symbolic resource name to lock.
  @param flags		Locking flags / mode
  @param lockpp		Void pointer to opaque data structure which is 
  			allocated and returned to the user.  Infastructure
			specific information should be returned.
  @return		0 on success, other value on failure.
 */
int
clu_lock(char *resource, int flags, void **lockpp)
{
	int ret;
	pthread_rwlock_rdlock(&dflt_lock);
	assert(_cpp);
	ret = cp_lock(_cpp, resource, flags, lockpp);
	pthread_rwlock_unlock(&dflt_lock);
	return ret;
}


/**
  Release a cluster lock using the default plugin.

  @param resource	Symbolic resource name to unlock.
  @param lockp		Opaque data structure which was allocated and
  			by clu_lock.  Frees this structure.
  @return		0 on success, other value on failure.
 */
int
clu_unlock(char *resource, void *lockp)
{
	int ret;
	pthread_rwlock_rdlock(&dflt_lock);
	ret = cp_unlock(_cpp, resource, lockp);
	pthread_rwlock_unlock(&dflt_lock);
	return ret;
}


static void
_clu_set_default(cluster_plugin_t *cpp)
{
	_cpp = cpp;
}


/**
  Make the specified plugin structure the default plugin for all of the
  clu_* calls. Not needed if clu_connect is used.

  @param cpp		cluster_plugin_t to make default.
  @see clu_connect
 */
void
clu_set_default(cluster_plugin_t *cpp)
{
	pthread_rwlock_wrlock(&dflt_lock);
	_clu_set_default(cpp);
	pthread_rwlock_unlock(&dflt_lock);
}


static void
_clu_clear_default(void)
{
	_cpp = NULL;
}


/**
  Clear out the default plugin. Not needed if clu_disconnect is used.

  @see clu_disconnect
 */
void
clu_clear_default(void)
{
	pthread_rwlock_wrlock(&dflt_lock);
	_clu_clear_default();
	pthread_rwlock_unlock(&dflt_lock);
}

