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
 *
 * Well, it's really not very dumb anymore.  This provides a single
 * machine access to the magma APIs without configuring other cluster
 * infrastructures.
 */
#include <magma.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <netinet/in.h>
#include <assert.h>

#define MODULE_DESCRIPTION "Dumb Plugin v1.1"
#define MODULE_AUTHOR      "Lon Hohberger"
#define DUMB_LOCK_PATH	   "/tmp/magma-dumb"

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

	//printf("DUMB: %s called\n", __FUNCTION__);
	foo = malloc(sizeof(cluster_member_list_t) +
		     sizeof(cluster_member_t));
	memset(foo, 0, sizeof(cluster_member_list_t) + sizeof(cluster_member_t));

	/* Store our info.  We're up, node ID 0. */
	foo->cml_count = 1;

	/* XXX should check for errors. */
	gethostname(foo->cml_members[0].cm_name,
		    sizeof(foo->cml_members[0].cm_name));
	foo->cml_members[0].cm_state = STATE_UP;
	foo->cml_members[0].cm_id = (uint64_t)0;
	return foo;
}


static int
dumb_quorum_status(cluster_plugin_t * __attribute__ ((unused)) self,
		   char __attribute__ ((unused)) *groupname)
{
	//printf("DUMB: %s called\n", __FUNCTION__);

	/* Make believe there's a group and we're a member */
	return QF_QUORATE | QF_GROUPMEMBER;
}


static char *
dumb_version(cluster_plugin_t * __attribute__ ((unused)) self)
{
	//printf("DUMB: %s called\n", __FUNCTION__);
	return MODULE_DESCRIPTION;
}


static int
dumb_open(cluster_plugin_t * __attribute__ ((unused)) self)
{
	//printf("DUMB: %s called\n", __FUNCTION__);

	/* Make believe there's a real need for this fd */
	return socket(AF_INET, SOCK_DGRAM, 0);
}


static int
dumb_login(cluster_plugin_t * __attribute__ ((unused)) self,
	   int __attribute__ ((unused)) fd,
	   char __attribute__ ((unused)) *groupname)
{
	//printf("DUMB: %s called\n", __FUNCTION__);

	/* Make believe there's a group */
	return 0;
}


static int
dumb_logout(cluster_plugin_t * __attribute__ ((unused)) self,
	    int __attribute__((unused)) fd)
{
	//printf("DUMB: %s called\n", __FUNCTION__);

	/* Make believe there's a group */
	return 0;
}


static int
dumb_close(cluster_plugin_t * __attribute__ ((unused)) self, int fd)
{
	//printf("DUMB: %s called\n", __FUNCTION__);
	return close(fd);
}


static int
dumb_fence(cluster_plugin_t __attribute__ ((unused)) *self,
	   cluster_member_t __attribute__((unused)) *node)
{
	//printf("DUMB: %s called\n", __FUNCTION__);
	errno = ENOSYS;
	return -1;
}


static int
dumb_lock(cluster_plugin_t * __attribute__ ((unused)) self,
	  char *resource, int flags, void **lockpp)
{
	struct flock fl;
	int *fdp;
	int esv;
	char pathname[1024];

	//printf("DUMB: %s called\n", __FUNCTION__);

	fdp = malloc(sizeof(int));
	if (!fdp)
		return -1;

	/*
	 * File system based locks using fcntl.
	 */
	snprintf(pathname, sizeof(pathname), "%s/%s",
		 DUMB_LOCK_PATH, resource);

	*fdp = open(pathname, O_RDWR | O_CREAT | O_TRUNC,
		    S_IRUSR|S_IWUSR);
	if (*fdp == -1) {
		esv = errno;
		free(fdp);
		errno = esv;
		return -1;
	}

	memset(&fl, 0, sizeof(fl));
	fl.l_type = (flags & CLK_WRITE) ? F_WRLCK : F_RDLCK;

	if (fcntl(*fdp, (flags & CLK_NOWAIT) ? F_SETLKW : F_SETLK, &fl) == -1){
		esv = errno;
		free(fdp);
		errno = esv;
		return -1;
	}

	*lockpp = (void *)fdp;
	return 0;
}


static int
dumb_unlock(cluster_plugin_t * __attribute__ ((unused)) self,
	    char *resource, void *lockp)
{
	char pathname[1024];
	//printf("DUMB: %s called\n", __FUNCTION__);

	assert(resource);
	assert(lockp);
	assert(*((int *)lockp) >= 0);

	snprintf(pathname, sizeof(pathname), "%s/%s",
		 DUMB_LOCK_PATH, resource);

	close(*((int *)lockp));
	free(lockp);
	unlink(pathname);
	return 0;
}


static int
dumb_get_event(cluster_plugin_t * __attribute__ ((unused)) self,
	       int __attribute__((unused)) fd)
{
	//printf("DUMB: %s called\n", __FUNCTION__);

	/* Never a real event */
	return CE_NULL;
}


int
cluster_plugin_load(cluster_plugin_t *driver)
{
	//printf("DUMB: %s called\n", __FUNCTION__);

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
	//printf("DUMB: %s called\n", __FUNCTION__);
	if (!driver) {
		errno = EINVAL;
		return -1;
	}

	//printf("DUMB: Plugin API version: %f\n", cluster_plugin_version());
	if (mkdir(DUMB_LOCK_PATH, 0700)) {
		if (errno == EEXIST) {
			if (chmod(DUMB_LOCK_PATH, 0700))
				return -1;
			return 0;
		}
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
	//printf("DUMB: %s called\n", __FUNCTION__);
	if (driver->cp_private.p_data)
		free(driver->cp_private.p_data);

	return 0;
}
