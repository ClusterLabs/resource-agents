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
 * Dumb test "Driver"
 */
#include <magma.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#define MODULE_DESCRIPTION "Dumb Plugin v1.0"
#define MODULE_AUTHOR      "Lon Hohberger"

/*
 * Grab the version from the header file so we don't cause API problems
 */
IMPORT_PLUGIN_API_VERSION();


static int
dumb_null(cluster_plugin_t * __attribute__ ((unused)) self)
{
	printf("DUMB: %s called\n", __FUNCTION__);
	printf(MODULE_DESCRIPTION " NULL function called\n");
	return 0;
}


static cluster_member_list_t *
dumb_member_list(cluster_plugin_t * __attribute__ ((unused)) self,
		 char __attribute__ ((unused)) *groupname)
{
	cluster_member_list_t *foo;

	printf("DUMB: %s called\n", __FUNCTION__);

	foo = malloc(sizeof(cluster_member_list_t) +
		     sizeof(cluster_member_t));
	foo->cml_count = 1;
	gethostname(foo->cml_members[0].cm_name,
		    sizeof(foo->cml_members[0].cm_name));
	return foo;
}


static int
dumb_quorum_status(cluster_plugin_t * __attribute__ ((unused)) self,
		   char __attribute__ ((unused)) *groupname)
{
	printf("DUMB: %s called\n", __FUNCTION__);
	return 0;
}


static char *
dumb_version(cluster_plugin_t * __attribute__ ((unused)) self)
{
	printf("DUMB: %s called\n", __FUNCTION__);
	return MODULE_DESCRIPTION;
}


static int
dumb_open(cluster_plugin_t * __attribute__ ((unused)) self)
{
	printf("DUMB: %s called\n", __FUNCTION__);
	errno = ENOSYS;
	return -1;
}


static int
dumb_login(cluster_plugin_t * __attribute__ ((unused)) self,
	   int __attribute__ ((unused)) fd,
	   char __attribute__ ((unused)) *groupname)
{
	printf("DUMB: %s called\n", __FUNCTION__);
	errno = ENOSYS;
	return -1;
}


static int
dumb_logout(cluster_plugin_t * __attribute__ ((unused)) self,
	    int __attribute__((unused)) fd)
{
	printf("DUMB: %s called\n", __FUNCTION__);
	errno = ENOSYS;
	return -1;
}


static int
dumb_close(cluster_plugin_t * __attribute__ ((unused)) self,
	   int __attribute__((unused)) fd)
{
	printf("DUMB: %s called\n", __FUNCTION__);
	errno = ENOSYS;
	return -1;
}


static int
dumb_fence(cluster_plugin_t __attribute__ ((unused)) *self,
	   cluster_member_t __attribute__((unused)) *node)
{
	printf("DUMB: %s called\n", __FUNCTION__);
	errno = ENOSYS;
	return -1;
}


static int
dumb_lock(cluster_plugin_t * __attribute__ ((unused)) self,
	  char *__attribute__((unused)) resource,
	  int __attribute__((unused)) flags,
	  void **__attribute__((unused)) lockpp)
{
	printf("DUMB: %s called\n", __FUNCTION__);
	errno = ENOSYS;
	return -1;
}


static int
dumb_unlock(cluster_plugin_t * __attribute__ ((unused)) self,
	    char *__attribute__((unused)) resource,
	    void *__attribute__((unused)) lockp)
{
	printf("DUMB: %s called\n", __FUNCTION__);
	errno = ENOSYS;
	return -1;
}


static int
dumb_get_event(cluster_plugin_t * __attribute__ ((unused)) self,
	       int __attribute__((unused)) fd)
{
	printf("DUMB: %s called\n", __FUNCTION__);
	errno = ENOSYS;
	return -1;
}


int
cluster_plugin_load(cluster_plugin_t *driver)
{
	printf("DUMB: %s called\n", __FUNCTION__);

	if (!driver) {
		errno = EINVAL;
		return -1;
	}

	driver->cp_ops.s_null = dumb_null;
	driver->cp_ops.s_member_list = dumb_member_list;
	driver->cp_ops.s_quorum_status = dumb_quorum_status;
	driver->cp_ops.s_plugin_version = dumb_version;
	driver->cp_ops.s_get_event = dumb_get_event;
	driver->cp_ops.s_open = dumb_open;
	driver->cp_ops.s_close = dumb_close;
	driver->cp_ops.s_fence = dumb_fence;
	driver->cp_ops.s_login = dumb_login;
	driver->cp_ops.s_logout = dumb_logout;
	driver->cp_ops.s_lock = dumb_lock;
	driver->cp_ops.s_unlock = dumb_unlock;

	return 0;
}


int
cluster_plugin_init(cluster_plugin_t *driver,
		    void *__attribute__((unused)) priv,
		    size_t __attribute__((unused)) privlen)
{
	printf("DUMB: %s called\n", __FUNCTION__);

	printf("DUMB: Plugin API version: %f\n", cluster_plugin_version());

	if (!driver) {
		errno = EINVAL;
		return -1;
	}

	return 0;
}


/*
 * Clear out the private data, if it exists.
 */
int
cluster_plugin_unload(cluster_plugin_t *driver)
{
	printf("DUMB: %s called\n", __FUNCTION__);

	if (driver->cp_private.p_data)
		free(driver->cp_private.p_data);

	return 0;
}
