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
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

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
  Free up a null-terminated array of strings
 */
void
free_dirnames(char **dirnames)
{
	int x = 0;

	for (; dirnames[x]; x++)
		free(dirnames[x]);

	free(dirnames);
}


/**
  Read all entries in a directory and return them in a NULL-terminated,
  sorted array.
 */
int
read_dirnames_sorted(char *directory, char ***dirnames)
{
	DIR *dir;
	struct dirent *entry;
	char filename[1024];
	int count = 0, x = 0;

	dir = opendir(directory);
	if (!dir)
		return -1;

	/* Count the number of plugins */
	while ((entry = readdir(dir)) != NULL)
		++count;

	/* Malloc the entries */
	*dirnames = malloc(sizeof(char *) * (count+1));
	if (!*dirnames) {
#ifdef DEBUG
		printf("Magma: %s: Failed to malloc %d bytes",
		       __FUNCTION__, (int)(sizeof(char *) * (count+1)));
#endif
		closedir(dir);
		errno = ENOMEM;
		return -1;
	}
	memset(*dirnames, 0, sizeof(char *) * (count + 1));
	rewinddir(dir);

	/* Store the directory names. */
	while ((entry = readdir(dir)) != NULL) {
		snprintf(filename, sizeof(filename), "%s/%s", directory,
			 entry->d_name);

		(*dirnames)[x] = strdup(filename);
		if (!(*dirnames)[x]) {
#ifdef DEBUG
			printf("Magma: Failed to duplicate %s\n",
			       filename);
#endif
			free_dirnames(*dirnames);
			closedir(dir);
			errno = ENOMEM;
			return -1;
		}
		++x;
	}

	closedir(dir);

	/* Sort the directory names. */
	qsort((*dirnames), count, sizeof(char *), alphasort);

	return 0;
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
	int fd, ret, found = 0;
	cluster_plugin_t *cp;
	char **filenames;
	int fcount = 0;

	if (*cpp) {
		errno = EINVAL;
		return -1;
	}

#ifdef DEBUG
	printf("Magma: Trying plugins in %s\n", PLUGINDIR);
#endif
	if (read_dirnames_sorted(PLUGINDIR, &filenames) != 0) {
		return -1;
	}

	for (fcount = 0; filenames[fcount]; fcount++) {

		cp = cp_load(filenames[fcount]);
		if (cp == NULL)
			continue;
		++found;

#ifdef DEBUG
		printf("Magma: Trying %s: ", filename[fcount]);
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
		free_dirnames(filenames);

#ifdef DEBUG
		printf("Magma: Connected, file descriptor %d\n", fd);
#endif
		return fd;
	}

	free_dirnames(filenames);
	if (!found) {
#ifdef DEBUG
		printf("Magma: No usable plugins found.\n");
#endif
		errno = ELIBACC;
	} else {
#ifdef DEBUG
		printf("Magma: No applicable plugin found or no cluster "
		       "infrastructure running.\n");
#endif
		errno = ESRCH;
	}
	return -1;
}


const char *
cp_load_error(int e)
{
	switch(e) {
	case EINVAL:
		return "NULL plugin filename specified";
	case EPERM:
		return "User-readable bit not set";
	case ELIBBAD:
		return "dlopen() error";
	case EPROTO:
		return "API version incorrect or nonexistent";
	case ENOSYS:
		return "Load/init function nonexistent";
	case EBADE:
		return "Load function failed";
	default:
		return strerror(e);
	}

	return NULL;
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
	struct stat sb;

	errno = 0;

	if (!libpath) {
		errno = EINVAL;
		return NULL;
	}

	if (stat(libpath, &sb) != 0) {
		return NULL;
	}

	/*
	   If it's not owner-readable or it's a directory,
	   ignore/fail.  Thus, a user may change the permission of
	   a plugin "u-r" and this would then prevent magma apps
	   from loading it.
	 */
	if (S_ISDIR(sb.st_mode)) {
		errno = EISDIR;
		return NULL;
	}
	if (!(sb.st_mode & S_IRUSR)) {
#ifdef DEBUG
		printf("Magma: Ignoring %s (User-readable bit not set)\n",
		       libpath);
#endif
		errno = EPERM;
		return NULL;
	}

	handle = dlopen(libpath, RTLD_LAZY);
	if (!handle) {
		errno = ELIBBAD;
		return NULL;
	}

	modversion = dlsym(handle, CLU_PLUGIN_VERSION_SYM);
	if (!modversion) {
#ifdef DEBUG
		printf("Magma: Failed to map %s\n", CLU_PLUGIN_VERSION_SYM);
#endif
		dlclose(handle);
		errno = EPROTO; /* XXX what? */
		return NULL;
	}

	if (modversion() != CLUSTER_PLUGIN_API_VERSION) {
#ifdef DEBUG
		printf("Magma: API version mismatch in %s: \n"
		       "       %f expected; %f received.\n", libpath,
			CLUSTER_PLUGIN_API_VERSION, modversion());
#endif
		dlclose(handle);
		errno = EPROTO; /* XXX What? */
		return NULL;
	}

	cpp = malloc(sizeof(*cpp));
	if (!cpp) {
#ifdef DEBUG
		printf("Magma: Failed to malloc %d bytes\n",
		       (int)sizeof(*cpp));
#endif
		errno = ENOMEM;
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
#ifdef DEBUG
		printf("Magma: Module load function not found in %s\n",
		       libpath);
#endif
		free(cpp);
		dlclose(handle);
		errno = ENOSYS;
		return NULL;
	}

	/*
	 * Modules *MUST* have an init function.
	 */
	if (!cpp->cp_private.p_init_func) {
#ifdef DEBUG
		printf("Magma: Module init function not found in %s\n",
	       	       libpath);
#endif
		free(cpp);
		dlclose(handle);
		errno = ENOSYS;
		return NULL;
	}

	if (cpp->cp_private.p_load_func(cpp) < 0) {
#ifdef DEBUG
		printf("Load function failed\n");
#endif
		free(cpp);
		dlclose(handle);
		errno = EBADE;
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
	if (!cpp) {
		errno = EINVAL;
		return -1;
	}

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
	if (!cpp) {
		errno = EINVAL;
		return -1;
	}

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
	if (!cpp) {
		errno = EINVAL;
		return NULL;
	}

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
	if (!cpp) {
		errno = EINVAL;
		return -1;
	}

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
	if (!cpp) {
		errno = EINVAL;
		return NULL;
	}

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
	if (!cpp) {
		errno = EINVAL;
		return -1;
	}

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
	if (!cpp) {
		errno = EINVAL;
		return -1;
	}

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
	if (!cpp) {
		errno = EINVAL;
		return -1;
	}

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
	if (!cpp) {
		errno = EINVAL;
		return -1;
	}

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
	if (!cpp) {
		errno = EINVAL;
		return -1;
	}

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
	if (!cpp) {
		errno = EINVAL;
		return -1;
	}

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
	if (!cpp) {
		errno = EINVAL;
		return -1;
	}

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
	if (!cpp) {
		errno = EINVAL;
		return -1;
	}

	return cpp->cp_ops.s_logout(cpp, fd);
}


