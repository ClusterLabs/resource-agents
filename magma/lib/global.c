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
 * Global Cluster Shared Storage/State Initialization/cleanup functions
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

static cluster_plugin_t *__cpp = NULL;
static char __connected = 0;
static pthread_mutex_t dflt_mutex = PTHREAD_MUTEX_INITIALIZER;
static void __clu_set_default(cluster_plugin_t *cpp);
static void __clu_clear_default(void);


/**
 * Searches IOLIBDIR for a plugin which actually connects to whatever cluster
 * software is running locally.
 *
 * @return		0 on success; -1 on failure.
 */
int
clu_connect(char *groupname, int login)
{
	DIR *dir;
	int fd, ret;
	struct dirent *entry;
	cluster_plugin_t *cp;
	char filename[1024];

	pthread_mutex_lock(&dflt_mutex);
	if (__cpp) {
		pthread_mutex_unlock(&dflt_mutex);
		return 0;
	}

	dir = opendir(PLUGINDIR);
	if (!dir)
		return -1;

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

		__cpp = cp;
		__clu_set_default(__cpp);
		__connected = 1;
		
		pthread_mutex_unlock(&dflt_mutex);

		/* Don't allow msg_close() to close this socket */
		if (fd >= 0)
			clist_insert(fd, MSG_CONNECTED);
		return fd;
	}

	pthread_mutex_unlock(&dflt_mutex);
	return -1;
}


/**
 * clu_connected - returns
 */
int
clu_connected(void)
{
	int ret;
	
	pthread_mutex_lock(&dflt_mutex);
	ret = !!__connected;
	pthread_mutex_unlock(&dflt_mutex);

	return ret;
}


/**
 * Unloads and cleans up cluster plugin.
 *
 * @return		As cp_unload.
 * @see csd_unload
 */
int
clu_disconnect(int fd)
{
	int rv;

	if (fd >= 0)
		clist_delete(fd);

	pthread_mutex_lock(&dflt_mutex);
	if (__cpp) {
		cp_logout(__cpp, fd);
		cp_close(__cpp, fd);
		rv = cp_unload(__cpp);
		if (rv == 0)
			__cpp = NULL;
		__connected = 0;
	}
	pthread_mutex_unlock(&dflt_mutex);

	return rv;
}


/*
 * Wrappers around default object, if one exists.
 */
int
clu_null(void)
{
	int ret;
	pthread_mutex_lock(&dflt_mutex);
	assert(__cpp);
	ret = cp_null(__cpp);
	pthread_mutex_unlock(&dflt_mutex);
	return ret;
}


cluster_member_list_t *
clu_member_list(char *groupname)
{
	cluster_member_list_t * ret;
	pthread_mutex_lock(&dflt_mutex);
	assert(__cpp);
	ret = cp_member_list(__cpp, groupname);
	pthread_mutex_unlock(&dflt_mutex);
	return ret;
}

int
clu_quorum_status(char *groupname)
{
	int ret;
	pthread_mutex_lock(&dflt_mutex);
	assert(__cpp);
	ret = cp_quorum_status(__cpp, groupname);
	pthread_mutex_unlock(&dflt_mutex);
	return ret;
}


char *
clu_plugin_version(void)
{
	char *ret;
	pthread_mutex_lock(&dflt_mutex);
	assert(__cpp);
	ret = cp_plugin_version(__cpp);
	pthread_mutex_unlock(&dflt_mutex);
	return ret;
}


int
clu_get_event(int fd)
{
	int ret;
	pthread_mutex_lock(&dflt_mutex);
	assert(__cpp);
	ret = cp_get_event(__cpp, fd);
	pthread_mutex_unlock(&dflt_mutex);
	return ret;
}


int
clu_open(void)
{
	int ret;
	pthread_mutex_lock(&dflt_mutex);
	assert(__cpp);
	ret = cp_open(__cpp);
	pthread_mutex_unlock(&dflt_mutex);

	if (ret >= 0)
		clist_insert(ret, MSG_CONNECTED);

	return ret;
}


int
clu_login(int fd, char *groupname)
{
	int ret;
	pthread_mutex_lock(&dflt_mutex);
	assert(__cpp);
	ret = cp_login(__cpp, fd, groupname);
	pthread_mutex_unlock(&dflt_mutex);
	return ret;
}


int
clu_logout(int fd)
{
	int ret;
	pthread_mutex_lock(&dflt_mutex);
	assert(__cpp);
	ret = cp_logout(__cpp, fd);
	pthread_mutex_unlock(&dflt_mutex);
	return ret;
}


int
clu_close(int fd)
{
	int ret;

	if (fd >= 0)
		clist_delete(fd);

	pthread_mutex_lock(&dflt_mutex);
	assert(__cpp);
	ret = cp_close(__cpp, fd);
	pthread_mutex_unlock(&dflt_mutex);
	return ret;
}


int
clu_fence(cluster_member_t *node)
{
	int ret;
	pthread_mutex_lock(&dflt_mutex);
	assert(__cpp);
	ret = cp_fence(__cpp, node);
	pthread_mutex_unlock(&dflt_mutex);
	return ret;
}


int
clu_lock(char *resource, int flags, void **lockpp)
{
	int ret;
	pthread_mutex_lock(&dflt_mutex);
	assert(__cpp);
	ret = cp_lock(__cpp, resource, flags, lockpp);
	pthread_mutex_unlock(&dflt_mutex);
	return ret;
}


int
clu_unlock(char *resource, void *lockp)
{
	int ret;
	pthread_mutex_lock(&dflt_mutex);
	ret = cp_unlock(__cpp, resource, lockp);
	pthread_mutex_unlock(&dflt_mutex);
	return ret;
}


static void
__clu_set_default(cluster_plugin_t *cpp)
{
	__cpp = cpp;
}


void
clu_set_default(cluster_plugin_t *cpp)
{
	pthread_mutex_lock(&dflt_mutex);
	__clu_set_default(cpp);
	pthread_mutex_unlock(&dflt_mutex);
}


static void
__clu_clear_default(void)
{
	__cpp = NULL;
}


void
clu_clear_default(void)
{
	pthread_mutex_lock(&dflt_mutex);
	__clu_clear_default();
	pthread_mutex_unlock(&dflt_mutex);
}

