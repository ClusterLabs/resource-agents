/*
  Copyright Red Hat, Inc. 2002-2004

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
 * Plugin loading routines & No-op functions.
 */
#include <magma.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>

/**
  Unimplemented stub s_null function for plugins
 */
int
_U_clu_null(cluster_plugin_t __attribute__ ((unused)) *cpp)
{
	printf("Unimplemented NULL function called\n");
	return 0;
}


/** 
  Unimplemented stub s_member_list function for plugins
 */
cluster_member_list_t *
_U_clu_member_list(cluster_plugin_t __attribute__ ((unused)) *cpp,
		    char __attribute__ ((unused)) *groupname)
{
	errno = ENOSYS;
	return NULL;
}


/** 
  Unimplemented stub s_quorum_status function for plugins
 */
int
_U_clu_quorum_status(cluster_plugin_t __attribute__ ((unused)) *cpp,
		      char __attribute__ ((unused)) *groupname)
{
	errno = ENOSYS;
	return -ENOSYS;
}


/** 
  Unimplemented stub s_get_event function for plugins
 */
int
_U_clu_get_event(cluster_plugin_t __attribute__ ((unused)) *cpp,
		  int __attribute__((unused)) fd)
{
	errno = ENOSYS;
	return -ENOSYS;
}


/** 
  Unimplemented stub s_login function for plugins
 */
int
_U_clu_login(cluster_plugin_t __attribute__ ((unused)) *cpp,
	      int __attribute__ ((unused)) fd,
	      char __attribute__ ((unused)) *groupname)
{
	errno = ENOSYS;
	return -ENOSYS;
}


/** 
  Unimplemented stub s_logout function for plugins
 */
int
_U_clu_logout(cluster_plugin_t __attribute__ ((unused)) *cpp,
	       int __attribute__((unused)) fd)
{
	errno = ENOSYS;
	return -ENOSYS;
}


/** 
  Unimplemented stub s_close function for plugins
 */
int
_U_clu_close(cluster_plugin_t __attribute__ ((unused)) *cpp,
	      int __attribute__((unused)) fd)
{
	errno = ENOSYS;
	return -ENOSYS;
}


/** 
  Unimplemented stub s_fence function for plugins
 */
int
_U_clu_fence(cluster_plugin_t __attribute__ ((unused)) *cpp,
	      cluster_member_t __attribute__((unused)) *node)
{
	errno = ENOSYS;
	return -ENOSYS;
}


/** 
  Unimplemented stub s_lock function for plugins
 */
int
_U_clu_lock(cluster_plugin_t __attribute__ ((unused)) *cpp,
	     char *__attribute__ ((unused)) resource,
	     int __attribute__ ((unused)) flags,
	     void **__attribute__ ((unused)) lockpp)
{
	errno = ENOSYS;
	return -ENOSYS;
}


/** 
  Unimplemented stub s_unlock function for plugins
 */
int
_U_clu_unlock(cluster_plugin_t __attribute__ ((unused)) *cpp,
	     char *__attribute__ ((unused)) resource,
	     void *__attribute__ ((unused)) lockp)
{
	errno = ENOSYS;
	return -ENOSYS;
}


/** 
  Unimplemented stub s_plugin_version function for plugins
 */
char *
_U_clu_plugin_version(cluster_plugin_t __attribute__ ((unused)) *cpp)
{
	return "Unimplemented Version Function v1.0";
}


/**
  Searches PLUGINDIR for a plugin which can successfully connect to the
  cluster infrastructure (locking/fencing/quorum/membership) which is 
  running locally.  This connects, and tries to log in to the specified
  service or node group if specified.
 
  @param cpp		Newly allocated cluster plugin on success
  @param groupname	Node group to connect to.
  @param login		If set to nonzero, actually try to log in to the
  			node or service group.
  @return		A file descriptor on success; -1 on failure. 
  			select(2) may be used to wait for events on this
			file descriptor.
  @see clu_disconnect
 */
int
cp_connect(cluster_plugin_t **cpp, char *groupname, int login)
{
	DIR *dir;
	int fd, ret, found = 0;
	struct dirent *entry;
	cluster_plugin_t *cp;
	char filename[1024];

	if (*cpp) {
		errno = EINVAL;
		return -1;
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
		++found;

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

		*cpp = cp;
		closedir(dir);
		return fd;
	}

	closedir(dir);
	if (!found)
		errno = ELIBACC;
	else
		errno = ESRCH;
	return -1;
}


/**
 * Load a cluster plugin .so file and map all the functions
 * provided to entries in a cluster_plugin_t structure.  Maps all unimplemented
 * functions to their _U_* counterparts.
 *
 * @param libpath	Path to file.
 * @return		NULL on failure, or a newly allocated
 *			cluster_plugin_t structure on success.
 */
cluster_plugin_t *
cp_load(const char *libpath)
{
	void *handle = NULL;
	cluster_plugin_t *cpp = NULL;
	double (*modversion)(void);

	handle = dlopen(libpath, RTLD_LAZY);
	if (!handle) {
		return NULL;
	}

	modversion = dlsym(handle, CLU_PLUGIN_VERSION_SYM);
	if (!modversion) {
		fprintf(stderr,"Failed to map %s\n", CLU_PLUGIN_VERSION_SYM);
		dlclose(handle);
		return NULL;
	}

	if (modversion() != CLUSTER_PLUGIN_API_VERSION) {
		fprintf(stderr, "API version mismatch in %s. %f expected; %f"
			" received.\n", libpath, CLUSTER_PLUGIN_API_VERSION,
			modversion());
		dlclose(handle);
		return NULL;
	}

	cpp = malloc(sizeof(*cpp));
	if (!cpp) {
		fprintf(stderr,"Failed to malloc %d bytes\n",(int)sizeof(*cpp));
		return NULL;
	}

	memset(cpp, 0, sizeof(*cpp));

	/* Initially, set everything to _U */
	CP_SET_UNIMP(cpp, null);
	CP_SET_UNIMP(cpp, member_list);
	CP_SET_UNIMP(cpp, login);
	CP_SET_UNIMP(cpp, logout);
	CP_SET_UNIMP(cpp, plugin_version);
	CP_SET_UNIMP(cpp, lock);
	CP_SET_UNIMP(cpp, unlock);
	
	/* Store the handle */
	cpp->cp_private.p_dlhandle = handle;

	/* Set local node ID to none */
	cpp->cp_private.p_localid = NODE_ID_NONE;

	/* Grab the init and deinit functions */
	cpp->cp_private.p_load_func = dlsym(handle, CLU_PLUGIN_LOAD_SYM);
	cpp->cp_private.p_init_func = dlsym(handle, CLU_PLUGIN_INIT_SYM);
	cpp->cp_private.p_unload_func = dlsym(handle, CLU_PLUGIN_UNLOAD_SYM);

	/*
	 * Modules *MUST* have a load function, and it can not fail.
	 */
	if (!cpp->cp_private.p_load_func) {
		fprintf(stderr, "Module load function not found in %s\n",
			libpath);
		free(cpp);
		dlclose(handle);
		return NULL;
	}

	/*
	 * Modules *MUST* have an init function.
	 */
	if (!cpp->cp_private.p_init_func) {
		fprintf(stderr, "Module init function not found in %s\n",
			libpath);
		free(cpp);
		dlclose(handle);
		return NULL;
	}

	if (cpp->cp_private.p_load_func(cpp) < 0) {
		printf("Load function failed\n");
		free(cpp);
		return NULL;
	}

	return cpp;
}


/**
 * Initialize a cluster plugin structure.  This calls the
 * initialization function we loaded in cp_load.
 *
 * @param cpp		Pointer to plugin structure to initialize.
 * @param priv		Optional driver-specific private data to copy in
 *			to cpp.
 * @param privlen	Size of data in priv.
 * @return		-1 on failure; 0 on success.
 * @see	cp_load
 */
int
cp_init(cluster_plugin_t *cpp, const void *priv, size_t privlen)
{
	/*
	 * Modules *MUST* have an initialization function, and it can not
	 * fail.
	 */
	if (!cpp->cp_private.p_init_func) {
		errno = ENOSYS;
		return -ENOSYS;
	}

	if ((cpp->cp_private.p_init_func)(cpp, priv, privlen) < 0) {
		return -EINVAL;
	}

	return 0;
}


int
cp_unload(cluster_plugin_t *cpp)
{
	void *handle;

	if (!cpp)
		return 0;

	/*
	 * Call the deinitialization function, if it exists.
	 */
	if (cpp->cp_private.p_unload_func &&
	    (cpp->cp_private.p_unload_func(cpp) < 0)) {
		return -EINVAL;
	}

	handle = cpp->cp_private.p_dlhandle;
	free(cpp);
	dlclose(handle);

	return 0;
}


/**
  Reentrant version of 
  @ref clu_null
  using specified plugin.
 */
int
cp_null(cluster_plugin_t *cpp)
{
	return cpp->cp_ops.s_null(cpp);
}


/**
  Reentrant version of 
  @ref clu_member_list
  using specified plugin.
 */
cluster_member_list_t *
cp_member_list(cluster_plugin_t *cpp, char *groupname)
{
	return cpp->cp_ops.s_member_list(cpp, groupname);
}


/**
  Reentrant version of 
  @ref clu_quorum_status
  using specified plugin.
 */
int
cp_quorum_status(cluster_plugin_t *cpp, char *groupname)
{
	return cpp->cp_ops.s_quorum_status(cpp, groupname);
}


/**
  Reentrant version of 
  @ref clu_plugin_version
  using specified plugin.
 */
char *
cp_plugin_version(cluster_plugin_t *cpp)
{
	return cpp->cp_ops.s_plugin_version(cpp);
}


/**
  Reentrant version of 
  @ref clu_get_event
  using specified plugin.
 */
int
cp_get_event(cluster_plugin_t *cpp, int fd)
{
	return cpp->cp_ops.s_get_event(cpp, fd);
}


/**
  Obtain a cluster lock using the specified plugin.  The caller must
  take care to ensure mutual exclusion; not all lock managers will
  provide this based solely on the lock information.

  @see clu_lock
 */
int
cp_lock(cluster_plugin_t *cpp, char *resource, int flags, void **lockpp)
{
	return cpp->cp_ops.s_lock(cpp, resource, flags, lockpp);
}


/**
  Release a cluster lock using the specified plugin.  The caller must
  take care to ensure mutual exclusion if the underlying lock manager
  requires it.

  @see clu_unlock
 */
int
cp_unlock(cluster_plugin_t *cpp, char *resource, void *lockp)
{
	return cpp->cp_ops.s_unlock(cpp, resource, lockp);
}


/**
  Reentrant version of 
  @ref clu_login
  using specified plugin.
 */
int
cp_login(cluster_plugin_t *cpp, int fd, char *groupname)
{
	return cpp->cp_ops.s_login(cpp, fd, groupname);
}


/**
  Reentrant version of 
  @ref clu_open
  using specified plugin.
 */
int
cp_open(cluster_plugin_t *cpp)
{
	return cpp->cp_ops.s_open(cpp);
}


/**
  Reentrant version of 
  @ref clu_close
  using specified plugin.
 */
int
cp_close(cluster_plugin_t *cpp, int fd)
{
	return cpp->cp_ops.s_close(cpp, fd);
}


/**
  Reentrant version of 
  @ref clu_fence
  using specified plugin.
 */
int
cp_fence(cluster_plugin_t *cpp, cluster_member_t *node)
{
	return cpp->cp_ops.s_fence(cpp, node);
}


/**
  Reentrant version of 
  @ref clu_logout
  using specified plugin.
 */
int
cp_logout(cluster_plugin_t *cpp, int fd)
{
	return cpp->cp_ops.s_logout(cpp, fd);
}


