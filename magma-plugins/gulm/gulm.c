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
 * GuLM membership/quorum functions & driver load
 */
#include <magma.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <libgulm.h>
#include <assert.h>
#include <signal.h>
#include "gulm-plugin.h"

#include <sys/types.h>
#include <linux/unistd.h>

#ifdef MDEBUG
#include <mallocdbg.h>
#endif

static _syscall0(pid_t, gettid)
static pid_t gettid(void);

#define MODULE_DESCRIPTION "GuLM Plugin v1.0"
#define MODULE_AUTHOR      "Lon Hohberger"


/*
 * Grab the version from the header file so we don't cause API problems
 */
IMPORT_PLUGIN_API_VERSION();

/**
 * GuLM no-ops protos
 */
static int null_login_reply(void *misc, uint64_t gen, uint32_t error,
			    uint32_t rank, uint8_t corestate);
static int null_logout_reply(void *misc);
static int null_nodelist(void *misc, lglcb_t type, char *name,
			 struct in6_addr *ip, uint8_t state);
static int null_statechange(void *misc, uint8_t corestate, uint8_t quorate,
			    struct in6_addr *masterip, char *mastername);
static int null_nodechange(void *misc, char *nodename,
			   struct in6_addr *nodeip, uint8_t nodestate);
static int null_service_list(void *misc, lglcb_t type, char *service);
static int null_error(void *misc, uint32_t err);

static lg_core_callbacks_t gulm_callbacks_initializer = {
	null_login_reply,
	null_logout_reply,
	null_nodelist,
	null_statechange,
	null_nodechange,
	null_service_list,
	null_error
};


/**
 * GuLM reply-no-ops
 */
static int
null_login_reply(void *misc, uint64_t gen, uint32_t error, uint32_t rank,
		 uint8_t corestate)
{
	printf("GuLM: %s called\n", __FUNCTION__);
	return 0;
}


static int
null_logout_reply(void *misc)
{
	printf("GuLM: %s called\n", __FUNCTION__);
	return 0;
}


static int
null_nodelist(void *misc, lglcb_t type, char *name, struct in6_addr *ip,
	      uint8_t state)
{
	printf("GuLM: %s called\n", __FUNCTION__);
	return 0;
}


static int
null_statechange(void *misc, uint8_t corestate, uint8_t quorate, 
      struct in6_addr *masterip, char *mastername)
{
	printf("GuLM: %s called\n", __FUNCTION__);
	return 0;
}


static int
null_nodechange(void *misc, char *nodename, struct in6_addr *nodeip,
		uint8_t nodestate)
{
	printf("GuLM: %s called\n", __FUNCTION__);
	return 0;
}


static int
null_service_list(void *misc, lglcb_t type, char *service)
{
	printf("GuLM: %s called\n", __FUNCTION__);
	return 0;
}


static int
null_error(void *misc, uint32_t err)
{
	printf("GuLM: %s called\n", __FUNCTION__);
	return 0;
}


/**
 * No-op function.
 */
static int
gulm_null(cluster_plugin_t *self)
{
	printf(MODULE_DESCRIPTION " NULL function called\n");
	return 0;
}



/*
 * Function called back by lg_core_handle_messages
 */
int
gulm_nodelist(void *misc, lglcb_t type, char *name, struct in6_addr *ip,
              uint8_t state)
{
	int idx;
	struct nodelist_misc *nm = (struct nodelist_misc *)misc;
        cluster_member_list_t *mlp;
       
#ifdef DEBUG
	printf("GuLM: %s called\n", __FUNCTION__);
#endif
	assert(misc);
	mlp = nm->members;
	assert(mlp);

	switch(type) {
	case lglcb_start:
		nm->ret = 0;
                return 0;

	case lglcb_item:
		nm->ret = 1;

		idx = mlp->cml_count;
		mlp->cml_count++;
		mlp = realloc(mlp, sizeof(cluster_member_list_t) +
			      sizeof(cluster_member_t) *
				     mlp->cml_count);
		assert(mlp);

		/* Reassign in case pointer changed */
        	((struct nodelist_misc *)misc)->members = mlp;

		strncpy(mlp->cml_members[idx].cm_name, name,
			sizeof(mlp->cml_members[idx].cm_name));

		mlp->cml_members[idx].cm_id =
			((uint64_t)(ip->s6_addr32[2]) << 32) |
			(uint64_t)ip->s6_addr32[3];
		mlp->cml_members[idx].cm_addrs = NULL;

		if (state == lg_core_Logged_in)
			mlp->cml_members[idx].cm_state = STATE_UP;
		else
			mlp->cml_members[idx].cm_state = STATE_DOWN;

        	return 0;

	case lglcb_stop:
		nm->ret = 2;
                return 0;
        }

	return 1;
}


static cluster_member_list_t *
gulm_member_list(cluster_plugin_t *self,
		 char __attribute__ ((unused)) *groupname)
{
	int ret;
	gulm_interface_p pg;
	gulm_priv_t *p;
        lg_core_callbacks_t cb = gulm_callbacks_initializer;
	struct nodelist_misc nm;

#ifdef DEBUG
	printf("GuLM: %s called\n", __FUNCTION__);
#endif
	cb.nodelist = gulm_nodelist;

	assert(self);
	p = (gulm_priv_t *)self->cp_private.p_data;
	assert(p);
	pg = p->interface;
	assert(pg);

	if (lg_core_nodelist(pg) != 0)
		return NULL;

	memset(&nm, 0, sizeof(nm));
	nm.members = malloc(sizeof(cluster_member_list_t));
	if (!nm.members)
		return NULL;

	memset(nm.members,0,sizeof(*nm.members));

	do {
		ret = lg_core_handle_messages(pg, &cb, (void *)&nm);
	} while (nm.ret != 2 && (ret >= 0));

	p->memb_count = nm.members->cml_count;

	/* pointer can change from realloc */
	return nm.members;
}


static int
gulm_statechange(void *misc, uint8_t corestate, uint8_t quorate,
      struct in6_addr *masterip, char *mastername)
{
	int *ret = (int *)misc;

#ifdef DEBUG
	printf("GuLM: %s called\n", __FUNCTION__);
#endif
	*ret = CE_NULL;

   if (quorate)
      *ret = CE_QUORATE;
   else
		*ret = CE_INQUORATE;

	return 0;
}


static int
gulm_quorum_status(cluster_plugin_t *self,
		   char *groupname)
{
        int ret = 0, flag = -1;
	gulm_interface_p pg;
        lg_core_callbacks_t cb = gulm_callbacks_initializer;

#ifdef DEBUG
	printf("GuLM: %s called\n", __FUNCTION__);
#endif
	cb.statechange = gulm_statechange;
	
	assert(self);
	pg = ((gulm_priv_t *)self->cp_private.p_data)->interface;
	assert(pg);

        if (lg_core_corestate(pg) != 0)
		return -1;

        /*
         * gulm_login_reply modifies ret.  WATCH SIZING conflicts for
         * ret here and *ret in gulm_login_reply above!!!
         */
	do {
        	lg_core_handle_messages(pg, &cb, (void *)&flag);
	} while (ret == 0 && flag == -1);

	if (flag == CE_QUORATE)
		return QF_QUORATE | (groupname?QF_GROUPMEMBER:0);
	if (flag == CE_INQUORATE)
		return 0;

        return -1;
}


static char *
gulm_version(cluster_plugin_t *self)
{
	return MODULE_DESCRIPTION;
}


/*
 * Pulled from gulm-stonith bridge.
 */
static int
gulm_login_reply(void *misc, uint64_t gen, uint32_t error, uint32_t rank,
            uint8_t corestate)
{
        int *flag = (int *)misc;

#ifdef DEBUG
	printf("GuLM: %s called\n", __FUNCTION__);
#endif
        *flag = 1;

        switch(error) {
        case lg_err_Ok:
                return 0;

        case lg_err_BadConfig:
#ifdef DEBUG
		printf("GuLM: Bad config\n");
#endif
                return -1;

	case lg_err_BadLogin:
#ifdef DEBUG
		printf("GuLM: Bad login\n");
#endif
		return 1;
        }

#ifdef DEBUG
	printf("GuLM: Error %d\n", error);
#endif
        return -1;
}


static int
gulm_open(cluster_plugin_t *self)
{
        int flag = 0, ret = -1;
	gulm_interface_p pg;
        lg_core_callbacks_t cb = gulm_callbacks_initializer;

#ifdef DEBUG
	printf("GuLM: %s called\n", __FUNCTION__);
#endif
	cb.login_reply = gulm_login_reply;

	assert(self);
	pg = ((gulm_priv_t *)self->cp_private.p_data)->interface;
	assert(pg);

        if (lg_core_login(pg, 0) != 0)
		return -1;

        /*
         * gulm_login_reply modifies ret.  WATCH SIZING conflicts for
         * ret here and *ret in gulm_login_reply above!!!
         */
        ret = lg_core_handle_messages(pg, &cb, (void *)&flag);
        if ((ret != 0) || !flag)
                /* Should NEVER be reached; gulm returns the login reply
                   first - this is guaranteed */
		return -1;

	/* Log in to the lock subsys */
	ret = gulm_lock_login(pg);
	if (ret != 0)
		return ret;

	/* Give back our file descriptor to listen on. */
	ret = lg_core_selector(pg);

        return ret;
}


static int
gulm_logout_reply(void *misc)
{
        int *flag = (int *)misc;

#ifdef DEBUG
	printf("GuLM: %s called\n", __FUNCTION__);
#endif
        /*
         * Ok, we got our logout message.
         */
        *flag = 1;
        return 0;
}


static int
gulm_close(cluster_plugin_t *self, int __attribute__((unused)) fd)
{
	gulm_interface_p pg;
        lg_core_callbacks_t cb = gulm_callbacks_initializer;
	int flag = 0, ret;

#ifdef DEBUG
	printf("GuLM: %s called\n", __FUNCTION__);
#endif
	cb.logout_reply = gulm_logout_reply;

	assert(self);
	pg = ((gulm_priv_t *)self->cp_private.p_data)->interface;
	assert(pg);

	ret = gulm_lock_logout(pg);
	if (ret != 0)
		return ret;

        ret = lg_core_logout(pg);
	if (ret != 0)
                return ret;

        while (flag == 0) {
                /*
                 * Chew up messages.
                 */
                lg_core_handle_messages(pg, &cb, (void *)&flag);
        }

        return 0;
}


static int
gulm_fence(cluster_plugin_t *self, cluster_member_t *node)
{
	gulm_interface_p pg;

#ifdef DEBUG
	printf("GuLM: %s called\n", __FUNCTION__);
#endif
	assert(self);
	pg = ((gulm_priv_t *)self->cp_private.p_data)->interface;
	assert(pg);

	if (lg_core_forceexpire(pg, (char *)node->cm_name) != 0)
		return -1;
	return 0;
}


/**
 * See if we have a membership change event.
 */
static int
gulm_nodechange(void *misc, char *nodename, struct in6_addr *nodeip,
		uint8_t nodestate)
{
	int *event = (int *)misc;

#ifdef DEBUG
	printf("GuLM: %s called\n", __FUNCTION__);
#endif

	/* If we get multiple, ignore the membership changes */
	switch (*event) {
	case CE_QUORATE:
	case CE_INQUORATE:
	case CE_SHUTDOWN:
		return 0;
	}

	switch(nodestate) {
	case lg_core_Logged_in:
	case lg_core_Logged_out:
		*event = CE_MEMB_CHANGE;
		break;
	default:
		*event = CE_NULL;
		break;
	}

	return 0;
}


static int
gulm_get_event(cluster_plugin_t *self, int fd)
{
	gulm_interface_p pg;
	struct timeval tv;
	fd_set rfds;
        lg_core_callbacks_t cb = gulm_callbacks_initializer;
	int ret, event = CE_NULL;

#ifdef DEBUG
	printf("GuLM: %s called\n", __FUNCTION__);
#endif
	cb.nodechange = gulm_nodechange;
	cb.statechange = gulm_statechange;

	assert(self);
	pg = ((gulm_priv_t *)self->cp_private.p_data)->interface;
	assert(pg);

	/*
	   XXX The gulm core statechange is delivered after the
	   membership transitions; but very shortly after.  So, we 
	   allow 1/4 second.  This seems to work ;)
	 */
	tv.tv_sec = 0;
	tv.tv_usec = 250000;
	do {
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
	
		if (select(fd + 1, &rfds, NULL, NULL, &tv) == 0)
			return event;

                /*
                 * Chew up messages.
                 */
                ret = lg_core_handle_messages(pg, &cb, (void *)&event);

        } while (ret >= 0);

	if (ret < 0)
		return CE_SHUTDOWN;

        return event;
}


int
cluster_plugin_load(cluster_plugin_t *driver)
{
	if (!driver) {
		errno = EINVAL;
		return -1;
	}

	driver->cp_ops.s_null = gulm_null;
	driver->cp_ops.s_member_list = gulm_member_list;
	driver->cp_ops.s_quorum_status = gulm_quorum_status;
	driver->cp_ops.s_plugin_version = gulm_version;
	driver->cp_ops.s_get_event = gulm_get_event;
	driver->cp_ops.s_open = gulm_open;
	driver->cp_ops.s_close = gulm_close;
	driver->cp_ops.s_fence = gulm_fence;
	driver->cp_ops.s_lock = gulm_lock;
	driver->cp_ops.s_unlock = gulm_unlock;

	return 0;
}


int
cluster_plugin_init(cluster_plugin_t *driver, void *__attribute__((unused))priv,
		    size_t __attribute__((unused)) privlen)
{
	char myname[256];
	int ret;
	gulm_interface_p pg;
	gulm_priv_t *gp;

	if (!driver) {
		errno = EINVAL;
		return -1;
	}

	snprintf(myname, sizeof(myname), "Magma::%d\n",
		 gettid());

	ret = lg_initialize(&pg, "", myname);
	if (ret != 0)
		return -1;

	assert(pg);
	gp = malloc(sizeof(gulm_priv_t));
	assert(gp);

	gp->interface = pg;
	gp->quorum_state = 0;

	driver->cp_private.p_data = (void *)gp;

	return 0;
}


/*
 * Clear out the private data, if it exists.
 */
int
cluster_plugin_unload(cluster_plugin_t *driver)
{
	gulm_interface_p pg;

	assert(driver);
	pg = ((gulm_priv_t *)driver->cp_private.p_data)->interface;
	assert(pg);
	lg_release(pg);
	free(driver->cp_private.p_data);
	driver->cp_private.p_data = NULL;

	return 0;
}
