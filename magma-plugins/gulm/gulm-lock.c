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
 * GuLM lock/unlock functions
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
#include <fcntl.h>
#include "gulm-plugin.h"

#ifdef MDEBUG
#include <mallocdbg.h>
#endif

#define GULM_USRM_LVB		"usrm::gulm"
#define GULM_USRM_LVB_LEN	10


/**
 * GuLM lock no-ops protos
 */
static int null_lk_login_reply(void *misc, uint32_t error, uint8_t which);
static int null_lk_logout_reply(void *misc);
static int null_lk_lock_state(void *misc, uint8_t *key, uint16_t keylen,
			      uint64_t subid, uint64_t start, uint64_t stop,
			      uint8_t state, uint32_t flags, uint32_t error,
			      uint8_t *LVB, uint16_t LVBlen);
static int null_lk_lock_action(void *misc, uint8_t *key, uint16_t keylen,
			       uint64_t subid, uint8_t action, uint32_t error);
static int null_lk_drop_lock_req(void *misc, uint8_t *key, uint16_t keylen,
				 uint64_t subid, uint8_t state);
static int null_lk_drop_all(void *misc);
static int null_lk_error(void *misc, uint32_t err);


static lg_lockspace_callbacks_t lock_callbacks_initializer = {
	null_lk_login_reply,
	null_lk_logout_reply,
	null_lk_lock_state,
	null_lk_lock_action,
	null_lk_drop_lock_req,
	null_lk_drop_all,
	null_lk_error
};


/**
 * GuLM lock reply-no-ops
 */
static int
null_lk_login_reply(void *misc, uint32_t error, uint8_t which)
{
	printf("GuLM Lock: %s called\n", __FUNCTION__);
	return 0;
}


static int
null_lk_logout_reply(void *misc)
{
	printf("GuLM Lock: %s called\n", __FUNCTION__);
	return 0;
}


static int
null_lk_lock_state(void *misc, uint8_t *key, uint16_t keylen,
		   uint64_t subid, uint64_t start, uint64_t stop, uint8_t state,
		   uint32_t flags, uint32_t error, uint8_t *LVB,
		   uint16_t LVBlen)
{
	printf("GuLM Lock: %s called\n", __FUNCTION__);
	return 0;
}


static int
null_lk_lock_action(void *misc, uint8_t *key, uint16_t keylen,
		    uint64_t subid, uint8_t action, uint32_t error)
{
	printf("GuLM Lock: %s called\n", __FUNCTION__);
	return 0;
}


static int
null_lk_drop_lock_req(void *misc, uint8_t *key, uint16_t keylen,
		      uint64_t subid, uint8_t state)
{
	printf("GuLM Lock: %s called\n", __FUNCTION__);
	return 0;
}


static int
null_lk_drop_all(void *misc)
{
	printf("GuLM Lock: %s called\n", __FUNCTION__);
	return 0;
}


static int
null_lk_error(void *misc, uint32_t err)
{
	printf("GuLM Lock: %s called\n", __FUNCTION__);
	return 0;
}


/**
 * GuLM lock Login/logout functions
 */
static int
gulm_lk_login_reply(void *misc, uint32_t error, uint8_t which)
{
	int *flagp = (int *)misc;

	if (flagp)
		*flagp = 1;

	return error;
}


static int
gulm_lk_logout_reply(void *misc)
{
	int *flagp = (int *)misc;

	if (flagp)
		*flagp = 1;

	return 0;
}


/**
 *  Login
 */
int
gulm_lock_login(gulm_interface_p pg)
{
	int flag = 0, ret;
	lg_lockspace_callbacks_t cb = lock_callbacks_initializer;

	cb.login_reply = gulm_lk_login_reply;

	if (lg_lock_login(pg, "usrm") != 0)
		return -ENOLCK;

	do {
		ret = lg_lock_handle_messages(pg, &cb, &flag);
	} while (!flag);

	return ret;
}


/**
 * Logout
 */
int
gulm_lock_logout(gulm_interface_p pg)
{
	int flag = 0, ret;
	lg_lockspace_callbacks_t cb = lock_callbacks_initializer;

	cb.logout_reply = gulm_lk_logout_reply;

	/* Ruh roh */
	ret = lg_lock_logout(pg);
	if (ret != 0)
		return ret;

	do {
		ret = lg_lock_handle_messages(pg, &cb, &flag);
	} while (!flag);

	return ret;
}


/**
 *
 */
int
gulm_lk_lock_state(void *misc, uint8_t *key, uint16_t keylen,
		   uint64_t subid, uint64_t start, uint64_t stop,
		   uint8_t state,
		   uint32_t flags, uint32_t error, uint8_t *LVB,
		   uint16_t LVBlen)
{
	int *flagp = (int *)misc;

	*flagp = 1;

	if (!LVB) {
		printf("No LVB in lock reply.  Oddness.\n");
	}

	switch(error) {
	case lg_err_Ok:
		return 0;
	case lg_err_NotAllowed:
		return -EPERM;
	case lg_err_Unknown_Cs:
		return -EFAULT;
	case lg_err_TryFailed:
	case lg_err_Canceled:
		return -EAGAIN;
	case lg_err_AlreadyPend:
		return -EINPROGRESS;
	}

	printf("Unhandled lock error code: %d\n", error);
	return -1;
}



/**
 *
 */
int
gulm_lock(cluster_plugin_t *self,
	  char *resource,
	  int flags,
	  void **lockpp)
{
	lg_lockspace_callbacks_t cb = lock_callbacks_initializer;
	uint16_t reslen;
	uint8_t state;
	uint32_t lkflags = 0;
	int ret, flag = 0;
	uint64_t pid;
	gulm_interface_p pg;
	gulm_priv_t *priv;

	assert(self);
	priv = (gulm_priv_t *)self->cp_private.p_data;
	assert(priv);
	pg = priv->interface;
	assert(resource);
	reslen = (uint16_t)strlen(resource);
	assert(reslen);

	if (flags & CLK_EX) {
		state = lg_lock_state_Exclusive;
	} else if (flags & CLK_READ) {
		state = lg_lock_state_Shared;
	} else if (flags & CLK_WRITE) {
		state = lg_lock_state_Exclusive;
	} else {
		return -EINVAL;
	}

	pid = getpid();

	if (flags & CLK_NOWAIT)
		lkflags |= lg_lock_flag_Try;

	do {
		/*
		 * Send lock request to GuLM.
		 */
		ret = lg_lock_state_req(pg, (uint8_t *)resource, reslen,
					pid, 0, ~0ULL,
					state,
					lkflags, GULM_USRM_LVB, GULM_USRM_LVB_LEN);

		if (ret != 0)
			return ret;

		cb.lock_state = gulm_lk_lock_state;

		/* Wait for response from GuLM */
		do {
			ret = lg_lock_handle_messages(pg, &cb, &flag);
		} while (!flag);

		switch(ret) {
		case -EINPROGRESS:
			lg_lock_cancel_req(pg, resource, reslen, pid);
			break;
		case -EAGAIN:
			if (!(lkflags & lg_lock_flag_Try))
				break;
			return ret;
		case 0:
		default:
			return ret;
		}

		usleep(250000);
	} while (1);

	return ret;
}


/**
 *
 */
int
gulm_unlock(cluster_plugin_t *self,
	  char *resource,
	  void *lockp)
{
	lg_lockspace_callbacks_t cb = lock_callbacks_initializer;
	uint16_t reslen;
	int ret, flag = 0;
	uint64_t pid;
	gulm_interface_p pg;
	gulm_priv_t *priv;

	assert(self);
	priv = (gulm_priv_t *)self->cp_private.p_data;
	assert(priv);
	pg = priv->interface;
	assert(resource);
	reslen = (uint16_t)strlen(resource);
	assert(reslen);
	pid = getpid();

	/*
	 * Send lock request to GuLM.
	 */
	ret = lg_lock_state_req(pg, (uint8_t *)resource, reslen, 
				pid, 0, ~0ULL,
				lg_lock_state_Unlock,
				0, GULM_USRM_LVB, GULM_USRM_LVB_LEN);

	if (ret != 0)
		return ret;

	cb.lock_state = gulm_lk_lock_state;

	/* Wait for response from GuLM */
	do {
		ret = lg_lock_handle_messages(pg, &cb, &flag);
	} while (!flag);

	return ret;
}
