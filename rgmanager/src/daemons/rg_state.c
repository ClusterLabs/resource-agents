/*
  Copyright Red Hat, Inc. 2002-2004

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
//#define DEBUG
#include <assert.h>
#include <platform.h>
#include <magma.h>
#include <magmamsg.h>
#include <vf.h>
#include <stdio.h>
#include <string.h>
#include <resgroup.h>
#include <clulog.h>
#include <rg_locks.h>
#include <ccs.h>
#include <rg_queue.h>
#include <msgsimple.h>

int node_should_start_safe(uint64_t, cluster_member_list_t *, char *);

uint64_t next_node_id(cluster_member_list_t *membership, uint64_t me);

int rg_exec_script(char *rgname, char *script, char *action);
static int _svc_stop_finish(char *svcName, int failed, uint32_t newstate);

int set_rg_state(char *servicename, rg_state_t *svcblk);
int get_rg_state(char *servicename, rg_state_t *svcblk);


uint64_t
next_node_id(cluster_member_list_t *membership, uint64_t me)
{
	uint64_t low = (uint64_t)(-1);
	uint64_t next = me, curr;
	int x;

	for (x = 0; x < membership->cml_count; x++) {
		curr = membership->cml_members[x].cm_id;
		if (curr < low)
			low = curr;

		if ((curr > me) && ((next == me) || (curr < next)))
			next = curr;
	}

	/* I am highest ID; go to lowest */
	if (next == me)
		next = low;

	return next;
}


int
svc_report_failure(char *svcName)
{
	void *lockp = NULL;
	rg_state_t svcStatus;
	char *nodeName;
	cluster_member_list_t *membership;

	if (rg_lock(svcName, &lockp) == -1) {
		clulog(LOG_ERR, "#41: Couldn't obtain lock for RG %s: %s\n",
		       svcName, strerror(errno));
		return -1;
	}

	if (get_rg_state(svcName, &svcStatus) != 0) {
		clulog(LOG_ERR, "#42: Couldn't obtain status for RG %s\n",
		       svcName);
		clu_unlock(svcName, lockp);
		return -1;
	}
	rg_unlock(svcName, lockp);

	membership = member_list();
	nodeName = memb_id_to_name(membership, svcStatus.rs_last_owner);
	if (nodeName) {
		clulog(LOG_ALERT, "#2: Resource group %s returned failure "
		       "code.  Last Owner: %s\n", svcName, nodeName);
	} else {
		clulog(LOG_ALERT, "#3: Resource group %s returned failure "
		       "code.  Last Owner: %d\n",
		       svcName, (int)svcStatus.rs_last_owner);
	}

	cml_free(membership);

	clulog(LOG_ALERT,
	       "#4: Administrator intervention required.\n",
	       svcName, nodeName);

	return 0;
}


int
#ifdef DEBUG
_rg_lock(char *name, void **p)
#else
rg_lock(char *name, void **p)
#endif
{
#if 0
	int ret;
#endif
	char res[256];

	snprintf(res, sizeof(res), "usrm::rg=\"%s\"", name);
	*p = NULL;

#if 0
	do {
		ret = clu_lock(res, CLK_EX | CLK_NOWAIT, p);
		if ((ret == -1) && (errno == EAGAIN)) {
			usleep(50000);
			continue;
		}
		
	} while (1);

	return ret;
#else
	return clu_lock(res, CLK_EX, p);
#endif
	
}


#ifdef DEBUG
int
_rg_lock_dbg(char *name, void **p, char *file, int line)
{
	dprintf("rg_lock(%s) @ %s:%d\n", name, file, line);
	return _rg_lock(name, p);
}
#endif
	


int
#ifdef DEBUG
_rg_unlock(char *name, void *p)
#else
rg_unlock(char *name, void *p)
#endif
{
	char res[256];

	snprintf(res, sizeof(res), "usrm::rg=\"%s\"", name);
	return clu_unlock(res, p);
}


#ifdef DEBUG
int
_rg_unlock_dbg(char *name, void *p, char *file, int line)
{
	dprintf("rg_unlock(%s) @ %s:%d\n", name, file, line);
	return _rg_unlock(name, p);
}
#endif

	
void
send_response(int ret, request_t *req)
{
	SmMessageSt msg, *msgp = &msg;

	if (req->rr_resp_fd < 0)
		return;

	msgp->sm_hdr.gh_magic = GENERIC_HDR_MAGIC;
	msgp->sm_hdr.gh_command = RG_ACTION_REQUEST;
	msgp->sm_hdr.gh_length = sizeof(*msgp);
	msgp->sm_data.d_action = req->rr_orig_request;
	strncpy(msgp->sm_data.d_svcName, req->rr_group,
		sizeof(msgp->sm_data.d_svcName));
	msgp->sm_data.d_svcOwner = my_id(); /* XXX Broken */
	msgp->sm_data.d_ret = ret;

	swab_SmMessageSt(msgp);
	msg_send(req->rr_resp_fd, msgp, sizeof(*msgp));

	/* :) */
	msg_close(req->rr_resp_fd);
	req->rr_resp_fd = -1;
}


int
set_rg_state(char *name, rg_state_t *svcblk)
{
	cluster_member_list_t *membership;
	char res[256];
	int ret;

	if (name)
		strncpy(svcblk->rs_name, name, sizeof(svcblk->rs_name));

	membership = member_list();
	snprintf(res, sizeof(res), "usrm::rg=\"%s\"", name);
	ret = vf_write(membership, VFF_IGN_CONN_ERRORS, res, svcblk,
       		       sizeof(*svcblk));
	cml_free(membership);
	return ret;
}


static int
init_rg(char *name, rg_state_t *svcblk)
{
	svcblk->rs_owner = NODE_ID_NONE;
	svcblk->rs_last_owner = NODE_ID_NONE;
	svcblk->rs_state = RG_STATE_STOPPED;
       	svcblk->rs_restarts = 0;
	svcblk->rs_transition = 0;	
	strncpy(svcblk->rs_name, name, sizeof(svcblk->rs_name));

	return set_rg_state(name, svcblk);
}


int
get_rg_state(char *name, rg_state_t *svcblk)
{
	char res[256];
	int ret;
	void *data = NULL;
	uint32_t datalen = 0;
	uint64_t viewno;
	cluster_member_list_t *membership;

	/* ... */
	if (name)
		strncpy(svcblk->rs_name, name, sizeof(svcblk->rs_name));

	membership = member_list();

	snprintf(res, sizeof(res),"usrm::rg=\"%s\"", svcblk->rs_name);
	ret = vf_read(membership, res, &viewno, &data, &datalen);

	if (ret != VFR_OK || datalen == 0) {
		if (data)
			free(data);

		ret = init_rg(name, svcblk);
		if (ret != VFR_OK) {
			cml_free(membership);
			printf("Couldn't initialize rg %s!\n", name);
			return FAIL;
		}

		ret = vf_read(membership, res, &viewno, &data, &datalen);
		if (ret != VFR_OK) {
			if (data)
				free(data);
			cml_free(membership);
			printf("Couldn't reread rg %s! (%d)\n", name, ret);
			return FAIL;
		}
	}

	if (datalen != sizeof(*svcblk)) {
		printf("Size mismatch; expected %d got %d\n",
		       (int)sizeof(*svcblk), datalen);
		if (data)
			free(data);
		cml_free(membership);
		return FAIL;
	}

	/* Copy out the data. */
	memcpy(svcblk, data, sizeof(*svcblk));
	free(data);
	cml_free(membership);

	return 0;
}


/**
 * Advise service manager as to whether or not to stop a service, given
 * that we already know it's legal to run the service.
 *
 * @param svcStatus	Current service status.
 * @param svcName	Service name
 * @param req		Specify request to perform
 * @return		0 = DO NOT stop service, return FAIL
 *			1 = STOP service - return whatever it returns.
 *			2 = DO NOT stop service, return 0 (success)
 *                      3 = DO NOT stop service, return FORWARD
 */
int
svc_advise_stop(rg_state_t *svcStatus, char *svcName, int req)
{
	cluster_member_list_t *membership = member_list();
	int ret = 0;
	
	switch(svcStatus->rs_state) {
	case RG_STATE_FAILED:
		if (req == RG_DISABLE)
			ret = 1; /* Failed services can be disabled */
		else
			ret = 0; /* Failed services may not be stopped */
		break;
		
	case RG_STATE_STOPPING:
		printf("Service %s is stopping right now\n", svcName);
		ret = 2;
		break;

	case RG_STATE_STARTED:
	case RG_STATE_CHECK:
	case RG_STATE_STARTING:
	case RG_STATE_RECOVER:
		if ((svcStatus->rs_owner != my_id()) &&
		    memb_online(membership, svcStatus->rs_owner)) {
			/*
			   Service is running and the owner is online.
			   Forward the request if it's a user request
			   (e.g. disable).
			 */
			if (req == RG_STOP) {
				/*
				   It's running somewhere, but not here,
				   and it's not a user request.  Toss
				   it out the window.
				 */
				ret = 2;
				break;
			}

			/* Disable or relocate request here. */
			clulog(LOG_DEBUG, "Forwarding req. to %s.\n",
			       memb_id_to_name(membership,
					       svcStatus->rs_owner));
			ret = 3;
			break;
		}

		if (svcStatus->rs_owner == NODE_ID_NONE ||
		    (svcStatus->rs_owner == my_id())) {
			/*
			   Service is marked as running locally or on
			   NODE_ID_NONE (e.g. no member).  Safe
			   to do a full stop.
			 */
			ret = 1;
			break;
		}

		/*
		   Service is marked as running but node is down.
		   Doesn't make much sense to stop it.
		 */
		ret = 2;
		break;

	case RG_STATE_ERROR:
		/* Don't stop; return failure. */
		if (req == RG_DISABLE) {
			ret = 1;
			break;
		}
		clulog(LOG_DEBUG,
		       "Not stopping %s: service is failed\n",
		       svcName);
		ret = 0;
		break;

	case RG_STATE_STOPPED:
		/* Allow disabling of stopped services */
		if (req == RG_DISABLE)
			ret = 1;
		else
			ret = 2; /* if it's already stopped, do nothing */
		break;
	
	case RG_STATE_DISABLED:
	case RG_STATE_UNINITIALIZED:
		if (req == RG_DISABLE) {
			clulog(LOG_NOTICE,
			       "Disabling disabled resource group %s\n",
			       svcName);
			ret = 1;
			break;
		}

		clulog(LOG_DEBUG, "Not stopping disabled RG %s\n",
		       svcName);
		break;

	default:
		clulog(LOG_ERR,
		       "#42: Cannot stop RG %s: Invalid State %d\n",
		       svcName, svcStatus->rs_state);
		break;
	}

	cml_free(membership);
	return ret;
}


/**
 * Advise service manager as to whether or not to start a service, given
 * that we already know it's legal to run the service.
 *
 * @param svcStatus	Current service status.
 * @param svcName	Service name
 * @param flags		Specify whether or not it's legal to start a 
 *			disabled service, etc.
 * @return		0 = DO NOT start service, return FAIL
 *			1 = START service - return whatever it returns.
 *			2 = DO NOT start service, return 0
 */
int
svc_advise_start(rg_state_t *svcStatus, char *svcName, int req)
{
	cluster_member_list_t *membership = member_list();
	int ret = 0;
	
	switch(svcStatus->rs_state) {
	case RG_STATE_FAILED:
		clulog(LOG_ERR,
		       "#43: Resource group %s has failed; can not start.\n",
		       svcName);
		break;
		
	case RG_STATE_STOPPING:
		clulog(LOG_DEBUG, "RG %s is stopping\n", svcName);
		ret = 2;
		break;

	case RG_STATE_STARTED:
	case RG_STATE_CHECK:
	case RG_STATE_STARTING:
		if (svcStatus->rs_owner == my_id()) {
		    	/*
			 * Service is already running locally
			clulog(LOG_DEBUG,
			       "RG %s is already running locally\n", svcName);
			 */
			ret = 2;
			break;
		}

		if (svcStatus->rs_owner != my_id() &&
		    memb_online(membership, svcStatus->rs_owner)) {
			/*
			 * Service is running and the owner is online!
			clulog(LOG_DEBUG, "RG %s is running on member %s.\n",
			       svcName,
			       memb_id_to_name(membership,svcStatus->rs_owner));
			 */
			ret = 2;
			break;
		}

		if (svcStatus->rs_owner == NODE_ID_NONE) {
			clulog(LOG_NOTICE,
			       "Starting stopped resource group %s\n",
			       svcName);
			ret = 1;
			break;
		}

		/*
		 * Service is running but owner is down -> FAILOVER
		 */
		clulog(LOG_NOTICE,
		       "Taking over resource group %s from down member %s\n",
		       svcName, memb_id_to_name(membership,
						svcStatus->rs_owner));
		ret = 1;
		break;

	case RG_STATE_RECOVER:
		/*
		 * Starting failed service...
		 */
		if (req == RG_START_RECOVER) {
			clulog(LOG_NOTICE,
			       "Recovering failed resource group %s\n",
			       svcName);
			svcStatus->rs_state = RG_STATE_STOPPED;
			/* Start! */
			ret = 1;
			break;
		}

		/* Don't start, but return success. */
		clulog(LOG_DEBUG,
		       "Not starting %s: recovery state\n",
		       svcName);
		ret = 2;
		break;

	case RG_STATE_STOPPED:
		clulog(LOG_NOTICE, "Starting stopped resource group %s\n",
		       svcName);
		ret = 1;
		break;
	
	case RG_STATE_DISABLED:
	case RG_STATE_UNINITIALIZED:
		if (req == RG_ENABLE) {
			clulog(LOG_NOTICE,
			       "Starting disabled resource group %s\n",
			       svcName);
			ret = 1;
			break;
		}

		clulog(LOG_DEBUG, "Not starting disabled RG %s\n",
		       svcName);
		break;

	case RG_STATE_ERROR:
	default:
		clulog(LOG_ERR,
		       "#44: Cannot start RG %s: Invalid State %d\n",
		       svcName, svcStatus->rs_state);
		break;
	}

	cml_free(membership);
	return ret;
}


/**
 * Start a cluster service.
 *
 * @param svcName	Service ID to start.
 * @param flags		Service-operation specific flags to take into account.
 * @see svc_advise_start
 * @return		FAIL, 0
 */
int
svc_start(char *svcName, int req)
{
	void *lockp = NULL;
	int ret;
	rg_state_t svcStatus;

	if (rg_lock(svcName, &lockp) < 0) {
		clulog(LOG_ERR, "#45: Unable to obtain cluster lock: %s\n",
		       strerror(errno));
		return FAIL;
	}

	if (get_rg_state(svcName, &svcStatus) != 0) {
		rg_unlock(svcName, lockp);
		clulog(LOG_ERR, "#46: Failed getting status for RG %s\n",
		       svcName);
		return FAIL;
	}

	/* LOCK HELD */
	switch (svc_advise_start(&svcStatus, svcName, req)) {
	case 0: /* Don't start service, return FAIL */
		rg_unlock(svcName, lockp);
		return FAIL;
	case 2: /* Don't start service, return 0 */
		rg_unlock(svcName, lockp);
		return 0;
	default:
		break;
	}

	/* LOCK HELD if we get here */

	svcStatus.rs_owner = my_id();
	svcStatus.rs_state = RG_STATE_STARTING;
	svcStatus.rs_transition = (uint64_t)time(NULL);

	if (req == RG_START_RECOVER)
		svcStatus.rs_restarts++;
	else
		svcStatus.rs_restarts = 0;

	if (set_rg_state(svcName, &svcStatus) != 0) {
		clulog(LOG_ERR,
		       "#47: Failed changing resource group status\n");
		rg_unlock(svcName, lockp);
		return FAIL;
	}
	
	rg_unlock(svcName, lockp);

	ret = group_op(svcName, RG_START);
	ret = !!ret; /* Either it worked or it didn't.  Ignore all the
			cute values scripts might return */

	if (rg_lock(svcName, &lockp) < 0) {
		clulog(LOG_ERR, "#74: Unable to obtain cluster lock: %s\n",
		       strerror(errno));
		return FAIL;
	}

	svcStatus.rs_state = RG_STATE_STARTED;
	if (set_rg_state(svcName, &svcStatus) != 0) {
		clulog(LOG_ERR,
		       "#75: Failed changing resource group status\n");
		rg_unlock(svcName, lockp);
		return FAIL;
	}
	rg_unlock(svcName, lockp);
       
	if (ret == 0)
		clulog(LOG_NOTICE,
		       "Resource group %s started\n",
		       svcName);
	else
		clulog(LOG_WARNING,
		       "#68: Failed to start %s; return value: %d\n",
		       svcName, ret);

	return ret;
}


/**
 * Check status of a cluster service 
 *
 * @param svcName	Service name to check.
 * @return		FORWARD, FAIL, 0
 */
int
svc_status(char *svcName)
{
	void *lockp = NULL;
	rg_state_t svcStatus;

	if (rg_lock(svcName, &lockp) < 0) {
		clulog(LOG_ERR, "#48: Unable to obtain cluster lock: %s\n",
		       strerror(errno));
		return FAIL;
	}

	if (get_rg_state(svcName, &svcStatus) != 0) {
		rg_unlock(svcName, lockp);
		clulog(LOG_ERR, "#49: Failed getting status for RG %s\n",
		       svcName);
		return FAIL;
	}
	rg_unlock(svcName, lockp);

	if (svcStatus.rs_state == RG_STATE_STARTED &&
	    svcStatus.rs_owner != my_id())
		/* Don't check status for other resource groups */
		return SUCCESS;

	if (svcStatus.rs_state != RG_STATE_STARTED &&
	    svcStatus.rs_owner == my_id())
		/* Not-running RGs should not be checked yet. */
		return SUCCESS;

	return group_op(svcName, RG_STATUS);
}


/**
 * Stop a cluster service.
 *
 * @param svcName	Service ID to stop.
 * @param flags		Service-operation specific flags to take into account.
 * @see svc_advise_stop
 * @return		FAIL, 0
 */
static int
_svc_stop(char *svcName, int req, int recover, uint32_t newstate)
{
	void *lockp = NULL;
	rg_state_t svcStatus;
	int ret;

	if (!rg_quorate()) {
		clulog(LOG_WARNING, "#69: Unclean %s of %s\n", 
		       rg_req_str(req), svcName);
		return group_op(svcName, RG_STOP);
	}

	if (rg_lock(svcName, &lockp) == FAIL) {
		clulog(LOG_ERR, "#50: Unable to obtain cluster lock: %s\n",
		       strerror(errno));
		return FAIL;
	}

	if (get_rg_state(svcName, &svcStatus) != 0) {
		rg_unlock(svcName, lockp);
		clulog(LOG_ERR, "#51: Failed getting status for RG %s\n",
		       svcName);
		return FAIL;
	}

	switch (svc_advise_stop(&svcStatus, svcName, req)) {
	case 0:
		rg_unlock(svcName, lockp);
		clulog(LOG_DEBUG, "Unable to stop RG %s in %s state\n",
		       svcName, rg_state_str(svcStatus.rs_state));
		return FAIL;
	case 2:
		rg_unlock(svcName, lockp);
		return SUCCESS;
	case 3:
		rg_unlock(svcName, lockp);
		return FORWARD;
	default:
		break;
	}

	clulog(LOG_NOTICE, "Stopping resource group %s\n", svcName);

	if (recover)
		svcStatus.rs_state = RG_STATE_ERROR;
	else
		svcStatus.rs_state = RG_STATE_STOPPING;
	svcStatus.rs_transition = (uint64_t)time(NULL);

	//printf("rg state = %s\n", rg_state_str(svcStatus.rs_state));

	if (set_rg_state(svcName, &svcStatus) != 0) {
		rg_unlock(svcName, lockp);
		clulog(LOG_ERR, "#52: Failed changing RG status\n");
		return FAIL;
	}
	rg_unlock(svcName, lockp);

	ret = group_op(svcName, RG_STOP);

	_svc_stop_finish(svcName, ret, newstate);

	return ret;
}


static int
_svc_stop_finish(char *svcName, int failed, uint32_t newstate)
{
	rg_state_t svcStatus;
	void *lockp;

	if (rg_lock(svcName, &lockp) == FAIL) {
		clulog(LOG_ERR, "#53: Unable to obtain cluster lock: %s\n",
		       strerror(errno));
		return FAIL;
	}

	if (get_rg_state(svcName, &svcStatus) != 0) {
		rg_unlock(svcName, lockp);
		clulog(LOG_ERR, "#54: Failed getting status for RG %s\n",
		       svcName);
		return FAIL;
	}

	if ((svcStatus.rs_state != RG_STATE_STOPPING) &&
	     (svcStatus.rs_state != RG_STATE_ERROR)) {
		rg_unlock(svcName, lockp);
		return 0;
	}

	svcStatus.rs_last_owner = svcStatus.rs_owner;
	svcStatus.rs_owner = NODE_ID_NONE;

	if (failed) {
		clulog(LOG_CRIT, "#12: RG %s failed to stop; intervention "
		       "required\n", svcName);
		svcStatus.rs_state = RG_STATE_FAILED;
	} else if (svcStatus.rs_state == RG_STATE_ERROR)
		svcStatus.rs_state = RG_STATE_RECOVER;
	else
		svcStatus.rs_state = newstate;

	clulog(LOG_NOTICE, "Resource group %s is %s\n", svcName,
	       rg_state_str(svcStatus.rs_state));
	//printf("rg state = %s\n", rg_state_str(svcStatus.rs_state));

	svcStatus.rs_transition = (uint64_t)time(NULL);
	if (set_rg_state(svcName, &svcStatus) != 0) {
		rg_unlock(svcName, lockp);
		clulog(LOG_ERR, "#55: Failed changing RG status\n");
		return FAIL;
	}
	rg_unlock(svcName, lockp);

	return 0;
}


/**
 * Disable a cluster service.  Services in the disabled state are never
 * automatically started by the service manager - one must send a SVC_START
 * message.
 *
 * @param svcName	Service ID to stop.
 * @return		FAIL, 0
 */
int
svc_disable(char *svcName)
{
	return _svc_stop(svcName, RG_DISABLE, 0, RG_STATE_DISABLED);
}


int
svc_stop(char *svcName, int recover)
{
	return _svc_stop(svcName, recover?RG_STOP_RECOVER : RG_STOP,
			  recover, RG_STATE_STOPPED);
}


/**
 * Mark a cluster service as failed.  User intervention required.
 *
 * @param svcName	Service ID to stop.
 * @return		FAIL, 0
 */
int
svc_fail(char *svcName)
{
	void *lockp = NULL;
	rg_state_t svcStatus;

	if (rg_lock(svcName, &lockp) == FAIL) {
		clulog(LOG_ERR, "#55: Unable to obtain cluster lock: %s\n",
		       strerror(errno));
		return FAIL;
	}

	clulog(LOG_DEBUG, "Handling failure request for RG %s\n", svcName);

	if (get_rg_state(svcName, &svcStatus) != 0) {
		rg_unlock(svcName, lockp);
		clulog(LOG_ERR, "#56: Failed getting status for RG %s\n",
		       svcName);
		return FAIL;
	}

	if ((svcStatus.rs_state == RG_STATE_STARTED) &&
	    (svcStatus.rs_owner != my_id())) {
		rg_unlock(svcName, lockp);
		clulog(LOG_DEBUG, "Unable to disable RG %s in %s state\n",
		       svcName, rg_state_str(svcStatus.rs_state));
		return FAIL;
	}

	/*
	 * Leave a bread crumb so we can debug the problem with the service!
	 */
	if (svcStatus.rs_owner != NODE_ID_NONE) {
		svcStatus.rs_last_owner = svcStatus.rs_owner;
		svcStatus.rs_owner = NODE_ID_NONE;
	}
	svcStatus.rs_state = RG_STATE_FAILED;
	svcStatus.rs_transition = (uint64_t)time(NULL);
	svcStatus.rs_restarts = 0;
	if (set_rg_state(svcName, &svcStatus) != 0) {
		rg_unlock(svcName, lockp);
		clulog(LOG_ERR, "#57: Failed changing RG status\n");
		return FAIL;
	}
	rg_unlock(svcName, lockp);

	return 0;
}





/*
 * Send a message to the target node to start the service.
 */
static int
relocate_service(char *svcName, int request, uint64_t target)
{
	SmMessageSt msg_relo;
	int fd_relo, msg_ret;

	/* Build the message header */
	msg_relo.sm_hdr.gh_magic = GENERIC_HDR_MAGIC;
	msg_relo.sm_hdr.gh_command = RG_ACTION_REQUEST;
	msg_relo.sm_hdr.gh_length = sizeof (SmMessageSt);
	msg_relo.sm_data.d_action = request;
	strncpy(msg_relo.sm_data.d_svcName, svcName,
		sizeof(msg_relo.sm_data.d_svcName));
	msg_relo.sm_data.d_ret = 0;

	/* Open a connection to the other node */

	if ((fd_relo = msg_open(target, RG_PORT, RG_PURPOSE, 2)) < 0) {
		clulog(LOG_ERR,
		       "#58: Failed opening connection to member #%d\n",
		       target);
		return -1;
	}

	/* Encode */
	swab_SmMessageSt(&msg_relo);

	/* Send relocate message to the other node */
	if (msg_send(fd_relo, &msg_relo, sizeof (SmMessageSt)) !=
	    sizeof (SmMessageSt)) {
		clulog(LOG_ERR,
		       "#59: Error sending relocate request to member #%d\n",
		       target);
		msg_close(fd_relo);
		return -1;
	}

	clulog(LOG_DEBUG, "Sent relocate request to %d\n", (int)target);

	/* Check the response */
	msg_ret = msg_receive(fd_relo, &msg_relo, sizeof (SmMessageSt));

	if (msg_ret != sizeof (SmMessageSt)) {
		/* 
		 * In this case, we don't restart the service, because the 
		 * service state is actually unknown to us at this time.
		 */
		clulog(LOG_ERR, "#60: Mangled reply from member #%d during RG "
		       "relocate\n", target);
		msg_close(fd_relo);
		return 0;	/* XXX really UNKNOWN */
	}

	/* Got a valid response from other node. */
	msg_close(fd_relo);

	/* Decode */
	swab_SmMessageSt(&msg_relo);

	return msg_relo.sm_data.d_ret;
}


/**
 * handle_relocate_req - Relocate a service.  This seems like a huge
 * deal, except it really isn't.
 *
 * @param svcID		Service ID in question.
 * @param flags		If (flags & SVCF_PENDING), we were called from
 *			handle_start_req - and so we should ignore all local
 *			restarts/stops - since handle_start_req does this
 *			for us.
 * @param preferred_target	When sent a relocate message from the
 *				management software, a destination node
 *				is sent as well.  This causes us to try
 *				starting the service on that node *first*,
 *				but does NOT GUARANTEE that the service
 *				will end up on that node.  It will end up
 *				on whatever node actually successfully
 *				starts it.
 * @param new_owner	Member who actually ends up owning the service.
 */
int
handle_relocate_req(char *svcName, int request, uint64_t preferred_target,
		    uint64_t *new_owner)
{
	cluster_member_list_t *allowed_nodes;
	uint64_t target = preferred_target, me = my_id();
	int ret, x;
	
	/*
	 * Stop the service - if we haven't already done so.
	 */
	if (request != RG_START_RECOVER) {
		ret = _svc_stop(svcName, request, 0, RG_STATE_STOPPED);
		if (ret == FAIL) {
			svc_fail(svcName);
			return FAIL;
		}
		if (ret == FORWARD)
			return FORWARD;
	}

	allowed_nodes = member_list();
	/*
	   Mark everyone except me and the preferred target DOWN for now
	   If we can't start it on the preferred target, then we'll try
 	   other nodes.
	 */
	for (x = 0; x < allowed_nodes->cml_count; x++) {
		if (allowed_nodes->cml_members[x].cm_id == me ||
		    allowed_nodes->cml_members[x].cm_id == preferred_target)
			continue;

		allowed_nodes->cml_members[x].cm_state = STATE_DOWN;
	}

	/*
	 * First, see if it's legal to relocate to the target node.  Legal
	 * means: the node is online and is in the [restricted] failover
	 * domain of the service, or the service has no failover domain.
	 */
	if (preferred_target != (uint64_t)FAIL) {

		target = best_target_node(allowed_nodes, me, svcName, 1);

		/*
		 * I am the ONLY one capable of running this service,
		 * PERIOD...
		 */
		if (target == me)
			goto exhausted;

		if (target == preferred_target) {
			/*
		 	 * It's legal to start the service on the given
		 	 * node.  Try to do so.
		 	 */
			if (relocate_service(svcName, request, target) == 0) {
				*new_owner = target;
				/*
				 * Great! We're done...
				 */
				return 0;
			}
		}
	}

	/*
	 * Ok, so, we failed to send it to the preferred target node.
	 * Try to start it on all other nodes.
	 */
	for (x = 0; x < allowed_nodes->cml_count; x++) {
		if (allowed_nodes->cml_members[x].cm_id == me ||
		    allowed_nodes->cml_members[x].cm_id == preferred_target) {
			allowed_nodes->cml_members[x].cm_state = STATE_DOWN;
			continue;
		}
		allowed_nodes->cml_members[x].cm_state = STATE_UP;
	}
	memb_mark_down(allowed_nodes, me);

	while (memb_count(allowed_nodes)) {
		target = best_target_node(allowed_nodes, me, svcName, 1);
		if (target == me)
			goto exhausted;

		switch (relocate_service(svcName, request, target)) {
		case FAIL:
			memb_mark_down(allowed_nodes, target);
			continue;
		case ABORT:
			svc_report_failure(svcName);
			cml_free(allowed_nodes);
			return FAIL;
		case 0:
			*new_owner = target;
			clulog(LOG_NOTICE, "Resource group %s is now running "
			       "on member %d\n", svcName, (int)target);
			cml_free(allowed_nodes);
			return 0;
		default:
			clulog(LOG_ERR,
			       "#61: Invalid reply from member %d during"
			       " relocate operation!\n", target);
		}
	}
	cml_free(allowed_nodes);

	/*
	 * We got sent here from handle_start_req.
	 * We're DONE.
	 */
	if (request == RG_START_RECOVER)
		return FAIL;

	/*
	 * All potential places for the service to start have been exhausted.
	 * We're done.
	 */
exhausted:
	clulog(LOG_WARNING,
	       "#70: Attempting to restart resource group %s locally.\n",
	       svcName);
	if (svc_start(svcName, RG_START_RECOVER) == 0) {
		*new_owner = me;
		return FAIL;
	}
		
	if (svc_stop(svcName, 0) != 0) {
		svc_fail(svcName);
		svc_report_failure(svcName);
	}

	return FAIL;
}


/**
 * handle_start_req - Handle a generic start request from a user or during
 * service manager boot.
 *
 * @param svcID		Service ID to start.
 * @param flags
 * @param new_owner	Owner which actually started the service.
 * @return		FAIL - Failure.
 *			0 - The service is running.
 */
int
handle_start_req(char *svcName, int req, uint64_t *new_owner)
{
	int ret, tolerance = FOD_BEST;
	cluster_member_list_t *membership = member_list();

	/*
	 * When a service request is from a user application (eg, clusvcadm),
	 * accept FOD_GOOD instead of FOD_BEST
	 */
	if (req == RG_ENABLE)
		tolerance = FOD_GOOD;
	
	if (req != RG_RESTART &&
	    (node_should_start_safe(my_id(), membership, svcName) <
	     tolerance)) {
		cml_free(membership);
		return FAIL;
	}
	cml_free(membership);
	
	/*
	 * This is a 'root' start request.  We need to clear out our failure
	 * mask here - so that we can try all nodes if necessary.
	 */
	ret = svc_start(svcName, req);

	/*
	 * If we succeeded, then we're done.
	 */
	if (ret == SUCCESS) {
		*new_owner = my_id();
		return SUCCESS;
	}

	/* Already running? */
	if (ret == NO) {
		return SUCCESS;
	}
	
	/* 
	 * Keep the state open so the other nodes don't try to start
	 * it.  This allows us to be the 'root' of a given service.
	 */
	clulog(LOG_DEBUG, "Stopping failed resource group %s\n", svcName);
	if (svc_stop(svcName, 1) != 0) {
		clulog(LOG_CRIT,
		       "#13: Resource group %s failed to stop cleanly",
		       svcName);
		(void) svc_fail(svcName);

		/*
		 * If we failed to stop the service, we're done.  At this
		 * point, we can't determine the service's status - so
		 * trying to start it on other nodes is right out.
		 */
		return ABORT;
	}
	
	/*
	 * OK, it failed to start - but succeeded to stop.  Now,
	 * we should relocate the service.
	 */
	clulog(LOG_WARNING, "#71: Relocating failed resource group %s\n",
	       svcName);
	ret = handle_relocate_req(svcName, RG_START_RECOVER, -1, new_owner);

	/* If we leave the service stopped, instead of disabled, someone
	   will try to start it after the next node transition */
	//if (ret == FAIL)
		//svc_disable(svcName);

	return ret;
}


/**
 * handle_start_remote_req - Handle a remote start request.
 *
 * @param svcID		Service ID to start.
 * @param flags		Flags to use to determine start behavior.
 * @return		FAIL - Local failure.  ABORT - Unrecoverable error:
 *			the service didn't start, nor stop cleanly. 0
 *			- We started the service.
 */
int
handle_start_remote_req(char *svcName, int req)
{
	int tolerance = FOD_BEST;
	int x;
	uint64_t me = my_id();
	cluster_member_list_t *membership = member_list();

	/* XXX ok, so we need to say "should I start this if I was the
	   only cluster member online */
	for (x = 0; x < membership->cml_count; x++) {
		if (membership->cml_members[x].cm_id == me)
			continue;

		membership->cml_members[x].cm_state = STATE_DOWN;
	}

	if (req == RG_ENABLE)
		tolerance = FOD_GOOD;

	/*
	 * See if we agree with our ability to start the given service.
	 */
	if (node_should_start_safe(me, membership, svcName) < tolerance){
		cml_free(membership);
		return FAIL;
	}
	cml_free(membership);

	if (svc_start(svcName, req) == 0)
		return 0;

	if (svc_stop(svcName, 1) == 0)
		return FAIL;

	svc_fail(svcName);
	return ABORT;
}

