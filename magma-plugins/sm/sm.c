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
 * SM test "Driver"
 */
#include <magma.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <cnxman-socket.h>
#include <libdlm.h>
#include "sm-plugin.h"
#include <signal.h>
#include <sys/types.h>
#include <sys/select.h>

#define MODULE_DESCRIPTION "CMAN/SM Plugin v1.1.5"
#define MODULE_AUTHOR      "Lon Hohberger"

#define DLM_LS_NAME	   "Magma"
#define DLM_LSHOLDER_NAME  "__ref_lck"
#define LOCKINFO_MAX	   16

/* From services.c */
cluster_member_list_t *service_group_members(int sockfd, char *groupname);

/* Internal */
static inline int _dlm_release_lockspace(sm_priv_t *p);


/*
 * Grab the version from the header file so we don't cause API problems
 */
IMPORT_PLUGIN_API_VERSION();

static int
sm_null(cluster_plugin_t *self)
{
	printf(MODULE_DESCRIPTION " NULL function called\n");
	return 0;
}


static cluster_member_list_t *
sm_member_list(cluster_plugin_t *self, char *groupname)
{
	cluster_member_list_t *foo = NULL;
	struct cl_cluster_nodelist sm_nl = { 0, NULL };
	int op = SIOCCLUSTER_SERVICE_GETMEMBERS;
	sm_priv_t *p;
	int x;
	size_t sz;

	assert(self);
	p = (sm_priv_t *)self->cp_private.p_data;
	assert(p);
	assert(p->sockfd >= 0);

	if (!p->groupname && !groupname) {
		/*
		 * No group name at all?
		 * Default group if unjoined = all members
		 */
		op = SIOCCLUSTER_GETMEMBERS;
	} else {
		/*
		 * External call; not logged in.  Read from
		 * /proc/cluster/services
		 */
		if ((groupname && !p->groupname) ||
		    (groupname && strcmp(p->groupname, groupname)))
			return service_group_members(p->sockfd, groupname);

		if (p->state != SMS_JOINED)
			return NULL;
	}

	do {
		/* Clean up if necessary */
		if (sm_nl.nodes)
			free(sm_nl.nodes);
		if (foo)
			/* Don't need to cml_free - we know we didn't
			   resolve anything */
			free(foo);

		x = ioctl(p->sockfd, op, NULL);
		if (x <= 0)
			return NULL;

		sm_nl.max_members = x;

		/* BIG malloc here */
		sz = sizeof(struct cl_cluster_node) * sm_nl.max_members;
		sm_nl.nodes = malloc(sz);
		assert(sm_nl.nodes != NULL);

		/* Another biggie */
		foo = cml_alloc(sm_nl.max_members);
		assert(foo != NULL);

	} while (ioctl(p->sockfd, op, &sm_nl) != sm_nl.max_members);

	/* Store count in our internal structure */
	p->memb_count = sm_nl.max_members;

	foo->cml_count = p->memb_count;
	for (x = 0; x < p->memb_count; x++) {
		/* Copy the data to the lower layer */
		foo->cml_members[x].cm_addrs = NULL;
		foo->cml_members[x].cm_id = (uint64_t)sm_nl.nodes[x].node_id;

		switch(sm_nl.nodes[x].state) {
		case NODESTATE_MEMBER:
			foo->cml_members[x].cm_state = STATE_UP;
			break;
		case NODESTATE_JOINING:
		case NODESTATE_DEAD:
			foo->cml_members[x].cm_state = STATE_DOWN;
			break;
		default:
			foo->cml_members[x].cm_state = STATE_INVALID;
			break;
		}
		
		strncpy(foo->cml_members[x].cm_name, sm_nl.nodes[x].name,
			sizeof(foo->cml_members[x].cm_name));
	}

	free(sm_nl.nodes);

	return foo;
}


/**
 * Determine Quorum & Group membership status.
 *
 * @param self		Instance of plugin
 * @param groupname	Node/Service Group name
 */
static int
sm_quorum_state(cluster_plugin_t *self, char *groupname)
{
	int qs;
	sm_priv_t *p;
	cluster_member_list_t *tmp;

	assert(self);
	p = (sm_priv_t *)self->cp_private.p_data;
	assert(p);
	assert(p->sockfd >= 0);

	p->quorum_state = 0;
	qs = ioctl(p->sockfd, SIOCCLUSTER_ISQUORATE, NULL);

	if ((!groupname && !p->groupname) ||
	    (p->groupname && !groupname) ||
	    (p->groupname && !strcmp(p->groupname, groupname) &&
	     p->state == SMS_JOINED)) {
		/*
		 * We're a group member of the given group if we are logged
		 * in to the group from this instance of the SM plugin.
		 * A given SM plugin instance can only be used with a single
		 * group.  If we're given NULL, check our logged-in group,
		 * if one exists.
		 */
		p->quorum_state |= QF_GROUPMEMBER;
	} else {
		/*
		 * Don't know if we're a group member -- we're not logged
		 * in to the group.  So try to find out from the member 
		 * list.  CMAN/SM guarantees that we won't get a membership
		 * list if we're not a member of that group, so we'll
		 * use it to our advantage.
		 */
		if ((tmp = service_group_members(p->sockfd, groupname))) {
			p->quorum_state |= QF_GROUPMEMBER;
			free(tmp);
		}
	}

	switch(qs) {
	case 1:
		p->quorum_state |= QF_QUORATE;
		break;
	case 0:
	default:
		p->quorum_state &= ~QF_QUORATE;
		break;
	}
	
	return p->quorum_state;
}


static char *
sm_version(cluster_plugin_t *self)
{
	return MODULE_DESCRIPTION;
}


static void
sm_wait_join_complete(sm_priv_t *p)
{
	struct cl_service_event ev;
	fd_set rfds;
	struct cl_portclosed_oob msg;

	if (p->state != SMS_JOINING) {
		/* XXX */
	}

	while (p->state != SMS_JOINED) {

		FD_ZERO(&rfds);
		FD_SET(p->sockfd, &rfds);

		select(p->sockfd+1, &rfds, NULL, NULL, NULL);

		/* Snag the OOB message */
		if (recv(p->sockfd, &msg, sizeof(msg), MSG_OOB) < sizeof(msg))
			continue;

		if (ioctl(p->sockfd, SIOCCLUSTER_SERVICE_GETEVENT,
			     &ev) <= 0)
			continue;

		if (ev.type == SERVICE_EVENT_START) {
			ioctl(p->sockfd, SIOCCLUSTER_SERVICE_STARTDONE,
			      ev.event_id);
			/* XXX what if this fails? */
		}

		if (ev.type == SERVICE_EVENT_FINISH) 
			p->state = SMS_JOINED;
	}

}


static void
sm_wait_leave_complete(sm_priv_t *p)
{
	struct cl_service_event ev;
	fd_set rfds;
	struct cl_portclosed_oob msg;

	if (p->state != SMS_LEAVING) {
		/* XXX */
	}

	/* Can't log out if login is not complete... */
	while (p->state != SMS_LEFT) {

		FD_ZERO(&rfds);
		FD_SET(p->sockfd, &rfds);

		select(p->sockfd+1, &rfds, NULL, NULL, NULL);

		/* Snag the OOB message */
		if (recv(p->sockfd, &msg, sizeof(msg), MSG_OOB) < sizeof(msg))
			continue;

		if (ioctl(p->sockfd, SIOCCLUSTER_SERVICE_GETEVENT,
			     &ev) <= 0)
			continue;

		if (ev.type == SERVICE_EVENT_LEAVEDONE)
			p->state = SMS_LEFT;

		/*
		 * Handle member transitions during shutdown.
		 */
		if (ev.type == SERVICE_EVENT_START) {
			ioctl(p->sockfd, SIOCCLUSTER_SERVICE_STARTDONE,
			      ev.event_id);
			/* XXX what if this fails? */
			//printf("SERVICE_EVENT_START during leave\n");
		}
	}
}


/**
 * Log in to CMAN/SM and become a group member of the specified group name.
 *
 * @param self		Plugin instance
 * @param fd		File descriptor to use for login.
 * @param groupname	Name of group to become member of.
 */
static int
sm_login(cluster_plugin_t *self, int fd, char *groupname)
{
	int q;
	int err;
	sm_priv_t *p;

	assert(self);
	p = (sm_priv_t *)self->cp_private.p_data;
	assert(p);
	assert(p->sockfd >= 0);
	assert(p->sockfd == fd);

	if (!groupname) {
		errno = EINVAL;
		return -1;
	}

	if (p->groupname) {
		errno = EBUSY;
		return -1;
	}

	p->groupname = strdup(groupname);

	q = sm_quorum_state(self, NULL);
	while (!is_quorate(q)) {
		q = sm_quorum_state(self, NULL);
		sleep(2);
	}

	if (ioctl(p->sockfd, SIOCCLUSTER_SERVICE_REGISTER, p->groupname) < 0) {
		err = errno;
		free(p->groupname);
		p->groupname = NULL;
		return -err;
	}

	if (ioctl(p->sockfd, SIOCCLUSTER_SERVICE_JOIN, p->groupname) < 0) {
		err = errno;
		free(p->groupname);
		p->groupname = NULL;
		return -err;
	}

	p->state = SMS_JOINING;

	sm_wait_join_complete(p);
	return 0;
}


static int
sm_open(cluster_plugin_t *self)
{
	sm_priv_t *p;

	assert(self);
	p = (sm_priv_t *)self->cp_private.p_data;
	assert(p);

	if (p->sockfd >= 0)
		close(p->sockfd);

	p->sockfd = socket(AF_CLUSTER, SOCK_DGRAM, CLPROTO_CLIENT);
	if (p->sockfd < 0)
		return -errno;

	return p->sockfd;
}


static int
sm_logout(cluster_plugin_t *self, int fd)
{
	sm_priv_t *p;

	assert(self);
	p = (sm_priv_t *)self->cp_private.p_data;
	assert(p);
	assert(fd == p->sockfd);

	if (p->state == SMS_NONE)
		return 0;

	if (p->state == SMS_JOINED) {

		if (ioctl(p->sockfd, SIOCCLUSTER_SERVICE_LEAVE, NULL))
			return -errno;

		p->state = SMS_LEAVING;

		sm_wait_leave_complete(p);
	}

	/* Unregister. */
	ioctl(p->sockfd, SIOCCLUSTER_SERVICE_UNREGISTER, NULL);

	if (p->groupname) {
		free(p->groupname);
		p->groupname = NULL;
	}

	return 0;
}


static int
sm_close(cluster_plugin_t *self, int fd)
{
	int ret;
	sm_priv_t *p;

	assert(self);
	p = (sm_priv_t *)self->cp_private.p_data;
	assert(p);
	assert(fd == p->sockfd);

	if (p->ls) {
		_dlm_release_lockspace(p);
	}
	p->ls = NULL;

	ret = close(fd);
	p->sockfd = -1;

	return ret;
}


static int
sm_fence(cluster_plugin_t *self, cluster_member_t *node)
{
	int nodeid;
	sm_priv_t *p;

	//printf("SM: %s called\n", __FUNCTION__);

	assert(self);
	p = (sm_priv_t *)self->cp_private.p_data;
	assert(p);

	nodeid = (int)node->cm_id;

	return ioctl(p->sockfd, SIOCCLUSTER_KILLNODE, nodeid);
}


static int
sm_get_event(cluster_plugin_t *self, int fd)
{
	sm_priv_t *p;
	struct cl_service_event ev;
	int n, o;
	struct cl_portclosed_oob msg;

	memset(&msg, 0, sizeof(msg));
	n = recv(fd, &msg, sizeof(msg), MSG_OOB);

	/* Socket closed. */
	if (n == 0)
		return CE_SHUTDOWN;

	assert(self);
	p = (sm_priv_t *)self->cp_private.p_data;
	assert(p);
	assert(fd == p->sockfd);

	/*
	 * Check for quorum transition.
 	 */
	if (msg.cmd == CLUSTER_OOB_MSG_STATECHANGE) {
		o = p->quorum_state;
		n = sm_quorum_state(self, NULL);

		if (is_quorate(o) && !is_quorate(n)) {
			return CE_INQUORATE;
		} else if (!is_quorate(o) && is_quorate(n)) {
			return CE_QUORATE;
		}
	}

	/*
	 * Otherwise, only handle it if it's a service event
	 */
	if (msg.cmd != CLUSTER_OOB_MSG_SERVICEEVENT)
		return CE_NULL;

	if (ioctl(p->sockfd, SIOCCLUSTER_SERVICE_GETEVENT, &ev) < 0) {
		/* XXX */
		//printf("ioctl() failed: %s\n", strerror(errno));
		return CE_NULL;
	}

	if (ev.type == SERVICE_EVENT_STOP) {
		/* We don't actually do anything.  See below. */
		return CE_SUSPEND;
	}

	/*
	 * Begin member transition.  USRM recovery happens asynchronously
	 * from the kernel; it's not proper to suspend the kernel waiting
	 * for userland to complete.  Userland uses the DLM; it should use
	 * it for synchronization of recovery within other userland processes
	 */
	if (ev.type == SERVICE_EVENT_START) {
		ioctl(p->sockfd, SIOCCLUSTER_SERVICE_STARTDONE, ev.event_id);
		/* XXX what if this fails? */
		return CE_NULL;
	}

	/*
	 * Group membership transition complete.
	 */
	if (ev.type == SERVICE_EVENT_FINISH)
		return CE_MEMB_CHANGE;

	return CE_NULL;
}


static void
ast_function(void * __attribute__ ((unused)) arg)
{
}


static int
wait_for_dlm_event(dlm_lshandle_t *ls)
{
	fd_set rfds;
	int fd = dlm_ls_get_fd(ls);

	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);

	if (select(fd + 1, &rfds, NULL, NULL, NULL) == 1)
		return dlm_dispatch(fd);

	return -1;
}


static inline int
_dlm_lock(sm_priv_t *p, int mode, struct dlm_lksb *lksb, int options,
          char *resource)
{
        int ret;

        /*
         * per pjc: create/open lockspace when first lock is taken
         */
        ret = dlm_ls_lock(p->ls, mode, lksb, options, resource,
                          strlen(resource), 0, ast_function, lksb,
                          NULL, NULL);

        if (ret < 0) {
#if 0
                if (errno == ENOENT) {
                        /* This should not happen if we have a lock
                           ref open in the LS ! */A
                        assert(0);
                }
#endif

                return -1;
        }

        if ((ret = (wait_for_dlm_event(p->ls) < 0))) {
                fprintf(stderr, "wait_for_dlm_event: %d / %d\n",
                        ret, errno);
                return -1;
        }

        return 0;
}



static inline int
_dlm_acquire_lockspace(sm_priv_t *p, const char *lsname)
{
        dlm_lshandle_t ls = NULL;
        struct dlm_lksb lksb;
        int ret;

retry:
        while (!ls) {
                ls = dlm_open_lockspace(lsname);
                if (ls)
                        break;

                ls = dlm_create_lockspace(lsname, 0644);
                if (ls)
                        break;

                /* Work around race: Someone was closing lockspace as
                   we were trying to open it.  Retry. */
                if (errno == ENOENT)
                        continue;

                fprintf(stderr, "failed acquiring lockspace: %s\n",
                        strerror(errno));

                return -1;
        }

        p->ls = ls;
        memset(&lksb,0,sizeof(lksb));

        ret = _dlm_lock(p, LKM_NLMODE, &lksb, 0, DLM_LSHOLDER_NAME);
        if (ret != 0) {
                dlm_release_lockspace(DLM_LS_NAME, ls, 0);
                p->ls = NULL;
                if (errno == ENOENT)
                        /* Lockspace freed under us? :( */
                        goto retry;
        }

        memcpy(&p->lsholder, &lksb, sizeof(p->lsholder));

        return 0;
}



static inline int
_dlm_unlock(sm_priv_t *p, struct dlm_lksb *lksb)
{
        int ret;

        ret = dlm_ls_unlock(p->ls, lksb->sb_lkid, 0, lksb, NULL);

        if (ret != 0)
                return ret;

        /* lksb->sb_status should be EINPROG at this point */

        if (wait_for_dlm_event(p->ls) < 0) {
                errno = lksb->sb_status;
                return -1;
        }

        return 0;
}


static inline int
_dlm_release_lockspace(sm_priv_t *p)
{
        _dlm_unlock(p, &p->lsholder);
        return dlm_release_lockspace(DLM_LS_NAME, p->ls, 0);
}


/**
 * Ugly routine to figure out what node has a given lock.
 */
static inline int
_get_holder(char *lk, sm_priv_t *p, int mode, uint64_t *holderid)
{
	struct dlm_lksb lksb;
	struct dlm_lockinfo li[LOCKINFO_MAX];
	struct dlm_resinfo ri;
	struct dlm_queryinfo qi;
	int query = 0, x;
	int ret;

	/*
	 * Take a null lock so we can query the DLM and find out who
	 * is holding the lock we're after.
	 */
	query = DLM_QUERY_QUEUE_GRANTED | DLM_QUERY_LOCKS_ALL;

	memset(&lksb, 0, sizeof(lksb));
	ret = _dlm_lock(p, LKM_NLMODE, &lksb, 0, lk);
	if (ret < 0)
		return -1;

	memset(&qi, 0, sizeof(qi));
	memset(&ri, 0, sizeof(ri));

	qi.gqi_resinfo = &ri;
	qi.gqi_lockinfo = &li[0];
	qi.gqi_locksize = LOCKINFO_MAX;
	qi.gqi_lockcount = 0;

	/* Query based on our null lock */
	ret = dlm_ls_query(p->ls, &lksb, query, &qi,
			   ast_function, NULL);

	if (ret != 0)
		return ret;

	while (lksb.sb_status == EINPROG) {
		if (wait_for_dlm_event(p->ls) < 0)
			return -1;
	}

	ret = -1;
	for (x = 0; x < qi.gqi_lockcount; x++) {
		if (li[x].lki_lkid == lksb.sb_lkid)
			continue;

		/* XXX what symbol is state 2?! */
		if (li[x].lki_state != 2)
			continue;

		if (li[x].lki_rqmode != 255)
			continue;

		if (li[x].lki_grmode == 0)
			continue;
	
		*holderid = (uint64_t)(li[x].lki_node);
		ret = 0;
		break;

		usleep(100000);
	}

	/* XXX check return value?! */

	_dlm_unlock(p, &lksb);

	return ret;

}


static int
sm_lock(cluster_plugin_t *self,
	  char *resource,
	  int flags,
	  void **lockpp)
{
	sm_priv_t *p;
	int mode = 0, options = 0, ret = 0;
	struct dlm_lksb *lksb;
	size_t sz;
	uint64_t holder;

	if (!self || !lockpp) {
		errno = EINVAL;
		return -1;
	}

	p = (sm_priv_t *)self->cp_private.p_data;
	assert(p);
	*lockpp = NULL;
	if (flags & CLK_EX) {
		mode = LKM_EXMODE;
	} else if (flags & CLK_READ) {
		mode = LKM_PRMODE;
	} else if (flags & CLK_WRITE) {
		mode = LKM_PWMODE;
	} else {
		errno = EINVAL;
		return -1;
	}

	if (flags & CLK_NOWAIT)
		options = LKF_NOQUEUE;

	/* Allocate our lock structure. */
	sz = (sizeof(*lksb) > sizeof(uint64_t) ? sizeof(*lksb) :
	      sizeof(uint64_t));

	lksb = malloc(sz);
	assert(lksb);
	memset(lksb, 0, sz);

	while(!p->ls) {
		_dlm_acquire_lockspace(p, DLM_LS_NAME);
	}
	assert(p->ls);

	ret = _dlm_lock(p, mode, lksb, options, resource);

	switch(lksb->sb_status) {
	case 0:
		*lockpp = (void *)lksb;
		return 0;
	case EAGAIN:
		if ((flags & CLK_HOLDER) &&
		    (_get_holder(resource, p, mode, &holder) == 0)) {
			memset(lksb, 0, sz);
			*((uint64_t *)lksb) = holder;
			*lockpp = (void *)lksb;
		} else {
			free(lksb);
		}
		errno = EAGAIN;
		return -1;
	default:
		fprintf(stderr, "_dlm_lock: %d / %d\n", ret, lksb->sb_status);
		ret = lksb->sb_status;
		free(lksb);
		errno = ret;
		return -1;
	}

	/* Not reached */
	fprintf(stderr, "code path error @ %s:%d\n", __FILE__, __LINE__);
	return -1;
}


static int
sm_unlock(cluster_plugin_t *self, char *__attribute__((unused)) resource,
	  void *lockp)
{
	sm_priv_t *p;
	dlm_lshandle_t ls;
	struct dlm_lksb *lksb = (struct dlm_lksb *)lockp;
	int ret;

	assert(self);
	p = (sm_priv_t *)self->cp_private.p_data;
	assert(p);
	ls = p->ls;
	assert(ls);

	if (!lockp) {
		errno = EINVAL;
		return -1;
	}

	ret = _dlm_unlock(p, lksb);

	if (ret != 0) 
		return ret;

	free(lksb);

	return ret;
}


int
cluster_plugin_load(cluster_plugin_t *driver)
{
	if (!driver) {
		errno = EINVAL;
		return -1;
	}

	driver->cp_ops.s_null = sm_null;
	driver->cp_ops.s_member_list = sm_member_list;
	driver->cp_ops.s_quorum_status = sm_quorum_state;
	driver->cp_ops.s_plugin_version = sm_version;
	driver->cp_ops.s_get_event = sm_get_event;
	driver->cp_ops.s_open = sm_open;
	driver->cp_ops.s_login = sm_login;
	driver->cp_ops.s_logout = sm_logout;
	driver->cp_ops.s_close = sm_close;
	driver->cp_ops.s_fence = sm_fence;
	driver->cp_ops.s_lock = sm_lock;
	driver->cp_ops.s_unlock = sm_unlock;

	return 0;
}


int
cluster_plugin_init(cluster_plugin_t *driver, void *priv,
		    size_t privlen)
{
	sm_priv_t *p = NULL;

	if (!driver) {
		errno = EINVAL;
		return -1;
	}

	if (!priv) {
		p = malloc(sizeof(*p));
		assert(p);
	} else {
		assert(privlen >= sizeof(*p));

		p = malloc(sizeof(*p));
		assert(p);
		memcpy(p, priv, sizeof(*p));
	}

	p->sockfd = -1;
	p->quorum_state = 0;
	p->memb_count = 0;
	p->state = SMS_NONE;
	p->groupname = NULL;
	p->ls = NULL;

	driver->cp_private.p_data = (void *)p;
	driver->cp_private.p_datalen = sizeof(*p);

	return 0;
}


/*
 * Clear out the private data, if it exists.
 */
int
cluster_plugin_unload(cluster_plugin_t *driver)
{
	sm_priv_t *p = NULL;

	if (!driver) {
		errno = EINVAL;
		return -1;
	}

	assert(driver);
	p = (sm_priv_t *)driver->cp_private.p_data;
	assert(p);

	/* You did log out, right? */
	assert(p->sockfd < 0);
	free(p);
	driver->cp_private.p_data = NULL;
	driver->cp_private.p_datalen = 0;

	return 0;
}
