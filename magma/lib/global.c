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
#include <stdint.h>
#include <dirent.h>
#include <magma.h>
#include <pthread.h>
#include <errno.h>
#include <magmamsg.h>
#include <unistd.h>
#include "clist.h"

static cluster_plugin_t *_cpp = NULL;	/** Default cluster plugin pointer */
static char _connected = 0;		/** Did we connect? */
static pthread_rwlock_t dflt_lock = PTHREAD_RWLOCK_INITIALIZER;
static void _clu_set_default(cluster_plugin_t *cpp);
static void _clu_clear_default(void);


/**
  Thread-safe wrapper using default plugin around cp_connect.
 
  @param groupname	Node group to connect to.
  @param login		If set to nonzero, actually try to log in to the
  			node or service group.
  @return		A file descriptor on success; -1 on failure. 
  			select(2) may be used to wait for events on this
			file descriptor.
  @see clu_disconnect
 */
int
clu_connect(char *groupname, int login)
{
	int e;
	int fd;

	pthread_rwlock_wrlock(&dflt_lock);
	if (_cpp) {
		pthread_rwlock_unlock(&dflt_lock);
		return -1;
	}

	fd = cp_connect(&_cpp, groupname, login);
	e = errno;
	if (fd >= 0) {
		_clu_set_default(_cpp);
		_connected = 1;
	}

	pthread_rwlock_unlock(&dflt_lock);

	/* Don't allow msg_close() to close this socket */
	/* XXX this should probably be removed and have the app do it */
	if (fd >= 0)
		clist_insert(fd, MSG_CONNECTED);
	else
		errno = e;
	return fd;
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
	int e, rv;

	if (fd >= 0)
		clist_delete(fd);

	pthread_rwlock_wrlock(&dflt_lock);
	if (_cpp) {
		cp_logout(_cpp, fd);
		cp_close(_cpp, fd);
		rv = cp_unload(_cpp);
		e = errno;
		if (rv == 0)
			_cpp = NULL;
		_connected = 0;
	}
	pthread_rwlock_unlock(&dflt_lock);

	if (rv)
		errno = e;
	return rv;
}


/**
  Wrapper around the default plugin for calling the null function.
 */
int
clu_null(void)
{
	int ret, e;

	pthread_rwlock_rdlock(&dflt_lock);
	ret = cp_null(_cpp);
	e = errno;
	pthread_rwlock_unlock(&dflt_lock);

	errno = e;
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
	int e;
	cluster_member_list_t * ret;

	pthread_rwlock_rdlock(&dflt_lock);
	ret = cp_member_list(_cpp, groupname);
	e = errno;
	pthread_rwlock_unlock(&dflt_lock);

	errno = e;
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
	int ret, e;

	pthread_rwlock_rdlock(&dflt_lock);
	ret = cp_quorum_status(_cpp, groupname);
	e = errno;
	pthread_rwlock_unlock(&dflt_lock);

	errno = e;
	return ret;
}


/**
  Call the default plugin's version function.  This can be used to find out
  which cluster plugin is in use by applications, but should not actually
  affect the operation of the application.

  @return		Immutable cluster plugin version string.
 */
char *
clu_plugin_version(void)
{
	char *ret;
	int e;

	pthread_rwlock_rdlock(&dflt_lock);
	ret = cp_plugin_version(_cpp);
	e = errno;
	pthread_rwlock_unlock(&dflt_lock);

	errno = e;
	return ret;
}


/**
  Obtain an event from the cluster file descriptor specified using the default
  cluster plugin. If no events are waiting, this call may block.  After an
  event is received, it is up to the application to handle it properly.  For
  instance, when a membership change event occurs, it is up to the application
  to attain a new membership list and update magmamsg's internal table (if in
  use).  See
  @ref magma.h
  for events.

  @param fd		File descriptor to obtain event from.
  @return		Event identifier.
 */
int
clu_get_event(int fd)
{
	int ret, e;

	pthread_rwlock_rdlock(&dflt_lock);
	ret = cp_get_event(_cpp, fd);
	e = errno;
	pthread_rwlock_unlock(&dflt_lock);

	errno = e;
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
	int ret, e;

	pthread_rwlock_wrlock(&dflt_lock);
	ret = cp_login(_cpp, fd, groupname);
	e = errno;
	pthread_rwlock_unlock(&dflt_lock);

	errno = e;
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
	int ret, e;

	pthread_rwlock_wrlock(&dflt_lock);
	ret = cp_logout(_cpp, fd);
	e = errno;
	pthread_rwlock_unlock(&dflt_lock);

	errno = e;
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
	int ret, e;

	if (fd >= 0)
		clist_delete(fd);

	pthread_rwlock_wrlock(&dflt_lock);
	ret = cp_close(_cpp, fd);
	e = errno;
	pthread_rwlock_unlock(&dflt_lock);

	errno = e;
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
	int ret, e;
	pthread_rwlock_wrlock(&dflt_lock);
	ret = cp_fence(_cpp, node);
	e = errno;
	pthread_rwlock_unlock(&dflt_lock);

	errno = e;
	return ret;
}


/**
  Obtain a cluster lock using the default plugin.  This uses a silly trick
  for preventing starvation and ensuring other threads get a real chance to
  get the lock.  Basically, if we can't get the lock immediately and we were
  called as a blocking lock call (that is, without the CLK_NOWAIT flag), we
  sleep for a random few milliseconds.  This keeps us from spinning waiting
  for the lock, but can hurt performance when lock contention is fairly low
  (i.e., the holder releases immediately after we go to sleep).

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
	int ret, block = 0, err;

	block = !(flags & CLK_NOWAIT);

	while (1) {
		pthread_rwlock_wrlock(&dflt_lock);
		ret = cp_lock(_cpp, resource, flags | CLK_NOWAIT, lockpp);
		err = errno;
		pthread_rwlock_unlock(&dflt_lock);

		if ((ret != 0) && (err == EAGAIN) && block) {
			usleep(random()&32767);
			continue;
		}

		break;
	}
			
	return ret;
}


/**
  Release a cluster lock using the default plugin.  This uses a silly trick
  for preventing starvation and ensuring other threads get a real chance to
  get the lock.  We sleep for a random few milliseconds to allow other
  threads which might be sleeping in clu_lock a chance to get the lock.
  Most of the time, they will get the lock before us.  This is not very
  good for performance, but prevents starvation and provides a thread
  safe interface without the need for an inter-thread queue as part of
  the magma library.

  @param resource	Symbolic resource name to unlock.
  @param lockp		Opaque data structure which was allocated and
  			by clu_lock.  Frees this structure.
  @return		0 on success, other value on failure.
 */
int
clu_unlock(char *resource, void *lockp)
{
	int ret, err;

	pthread_rwlock_wrlock(&dflt_lock);
	ret = cp_unlock(_cpp, resource, lockp);
	err = errno;
	pthread_rwlock_unlock(&dflt_lock);
	usleep(random()&32767);

	errno = err;
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


/**
  Returns the local node name using the default plugin as the data source.
  This function caches this information in the default plugin structure
  for future use.

  @param groupname	Group name.  If the local node is not a member
  			of this group, the call will fail.
  @param name		Preallocated char array into which the local member's
  			node name is copied.
  @param namelen	Size, in bytes, of name parameter.
  @return		0 on success, or -1 if the node is not a member of
  			the specified group.
  @see clu_local_nodeid cp_local_nodename
 */
int
clu_local_nodename(char *groupname, char *name, size_t namelen)
{
	int ret, e;

	pthread_rwlock_wrlock(&dflt_lock);
	ret = cp_local_nodename(_cpp, groupname, name, namelen);
	e = errno;
	pthread_rwlock_unlock(&dflt_lock);

	errno = e;
	return ret;
}


/**
  Returns the local node ID using the default plugin as the data source.
  This function caches this information in the default plugin structure
  for future use.

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
	int ret, e;

	pthread_rwlock_wrlock(&dflt_lock);
	ret = cp_local_nodeid(_cpp, groupname, nodeid);
	e = errno;
	pthread_rwlock_unlock(&dflt_lock);

	errno = e;
	return ret;
}

