//#define DEBUG
#include <assert.h>
#include <platform.h>
#include <message.h>
#include <members.h>
#ifdef OPENAIS
#include <ds.h>
#else
#include <vf.h>
#endif
#include <stdio.h>
#include <string.h>
#include <resgroup.h>
#include <clulog.h>
#include <lock.h>
#include <rg_locks.h>
#include <ccs.h>
#include <rg_queue.h>
#include <msgsimple.h>
#include <res-ocf.h>
#include <event.h>

/* XXX - copied :( */
#define cn_svccount cn_address.cna_address[0] /* Theses are uint8_t size */
#define cn_svcexcl  cn_address.cna_address[1]

int node_should_start_safe(uint32_t, cluster_member_list_t *, char *);

int next_node_id(cluster_member_list_t *membership, int me);

int rg_exec_script(char *rgname, char *script, char *action);
static int _svc_stop_finish(char *svcName, int failed, uint32_t newstate);

int set_rg_state(char *servicename, rg_state_t *svcblk);
int get_rg_state(char *servicename, rg_state_t *svcblk);
void get_recovery_policy(char *rg_name, char *buf, size_t buflen);
int check_depend_safe(char *servicename);
int group_migratory(char *servicename, int lock);
int have_exclusive_resources(void);
int check_exclusive_resources(cluster_member_list_t *membership, char *svcName);
static int msvc_check_cluster(char *svcName);
static inline int handle_started_status(char *svcName, int ret, rg_state_t *svcStatus);
static inline int handle_migrate_status(char *svcName, int ret, rg_state_t *svcStatus);
int count_resource_groups_local(cman_node_t *mp);
int is_exclusive(char *svcName);


int 
next_node_id(cluster_member_list_t *membership, int me)
{
	int low = (int)(-1);
	int next = me, curr;
	int x;

	for (x = 0; x < membership->cml_count; x++) {
		curr = membership->cml_members[x].cn_nodeid;
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


char *
c_name(char *svcName)
{
	char *ptr, *ret = svcName;

	ptr = strchr(svcName,':');
	if (!ptr)
		return ret;
	if ((int)(ptr - svcName) == 7 &&
	    !memcmp(svcName, "service", 7)) /* strlen("service") */
		ret = ptr + 1;

	return ret;
}


void
broadcast_event(char *svcName, uint32_t state, int owner, int last)
{
	SmMessageSt msgp;
	msgctx_t everyone;

	msgp.sm_hdr.gh_magic = GENERIC_HDR_MAGIC;
	msgp.sm_hdr.gh_command = RG_EVENT;
	msgp.sm_hdr.gh_length = sizeof(msgp);
	msgp.sm_hdr.gh_arg1 = owner; 
	msgp.sm_hdr.gh_arg2 = last; 
	msgp.sm_data.d_action = state;
	strncpy(msgp.sm_data.d_svcName, svcName,
		sizeof(msgp.sm_data.d_svcName));
	msgp.sm_data.d_svcOwner = owner;
	msgp.sm_data.d_ret = 0;

	swab_SmMessageSt(&msgp);

	if (msg_open(MSG_CLUSTER, 0, RG_PORT, &everyone, 0) < 0)
		return;

	msg_send(&everyone, &msgp, sizeof(msgp));
	msg_close(&everyone);
}


int
svc_report_failure(char *svcName)
{
	struct dlm_lksb lockp;
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
		rg_unlock(&lockp);
		return -1;
	}
	rg_unlock(&lockp);

	membership = member_list();
	nodeName = memb_id_to_name(membership, svcStatus.rs_last_owner);
	if (nodeName) {
		clulog(LOG_ALERT, "#2: Service %s returned failure "
		       "code.  Last Owner: %s\n", svcName, nodeName);
	} else {
		clulog(LOG_ALERT, "#3: Service %s returned failure "
		       "code.  Last Owner: %d\n",
		       svcName, (int)svcStatus.rs_last_owner);
	}

	free_member_list(membership);

	clulog(LOG_ALERT,
	       "#4: Administrator intervention required.\n",
	       svcName, nodeName);

	return 0;
}


int
#ifdef DEBUG
_rg_lock(char *name, struct dlm_lksb *p)
#else
rg_lock(char *name, struct dlm_lksb *p)
#endif
{
	char res[256];

	snprintf(res, sizeof(res), "rg=\"%s\"", name);
	return clu_lock(LKM_EXMODE, p, 0, res);
}


#ifdef DEBUG
int
_rg_lock_dbg(char *name, struct dlm_lksb *p, char *file, int line)
{
	dprintf("rg_lock(%s) @ %s:%d\n", name, file, line);
	return _rg_lock(name, p);
}
#endif


int
#ifdef DEBUG
_rg_unlock(struct dlm_lksb *p)
#else
rg_unlock(struct dlm_lksb *p)
#endif
{
	return clu_unlock(p);
}


#ifdef DEBUG
int
_rg_unlock_dbg(struct dlm_lksb *p, char *file, int line)
{
	dprintf("rg_unlock() @ %s:%d\n", file, line);
	return _rg_unlock(p);
}
#endif


void
send_ret(msgctx_t *ctx, char *name, int ret, int orig_request, int new_owner)
{
	SmMessageSt msg, *msgp = &msg;
	if (!ctx)
		return;

	msgp->sm_hdr.gh_magic = GENERIC_HDR_MAGIC;
	msgp->sm_hdr.gh_command = RG_ACTION_REQUEST;
	msgp->sm_hdr.gh_length = sizeof(*msgp);
	msgp->sm_data.d_action = orig_request;
	strncpy(msgp->sm_data.d_svcName, name,
		sizeof(msgp->sm_data.d_svcName));
	if (!new_owner)
		new_owner = my_id();
	msgp->sm_data.d_svcOwner = new_owner; /* XXX Broken */
	msgp->sm_data.d_ret = ret;

	swab_SmMessageSt(msgp);
	msg_send(ctx, msgp, sizeof(*msgp));
}

	
void
send_response(int ret, int nodeid, request_t *req)
{
	SmMessageSt msg, *msgp = &msg;

	if (req->rr_resp_ctx == NULL)
		return;

	msgp->sm_hdr.gh_magic = GENERIC_HDR_MAGIC;
	msgp->sm_hdr.gh_command = RG_ACTION_REQUEST;
	msgp->sm_hdr.gh_length = sizeof(*msgp);
	msgp->sm_data.d_action = req->rr_orig_request;
	strncpy(msgp->sm_data.d_svcName, req->rr_group,
		sizeof(msgp->sm_data.d_svcName));
	if (!nodeid)
		msgp->sm_data.d_svcOwner = my_id();
	else 
		msgp->sm_data.d_svcOwner = nodeid;
	msgp->sm_data.d_ret = ret;

	swab_SmMessageSt(msgp);
	msg_send(req->rr_resp_ctx, msgp, sizeof(*msgp));
}


int
set_rg_state(char *name, rg_state_t *svcblk)
{
	char res[256];
	rg_state_t svcblk_store;
#ifndef OPENAIS
	cluster_member_list_t *membership;
	int ret, tries = 0;
#endif

	if (name)
		strncpy(svcblk->rs_name, name, sizeof(svcblk->rs_name));

	snprintf(res, sizeof(res), "rg=\"%s\"", name);

	memcpy(&svcblk_store, svcblk, sizeof(svcblk_store));
	swab_rg_state_t(&svcblk_store);

#ifdef OPENAIS
	if (ds_write(res, &svcblk_store, sizeof(svcblk_store)) < 0)
		return -1;
	return 0;
#else
	do {
		/* Retry up to 3 times just in case members transition
		   while we're trying to commit something */
		membership = member_list();
		ret = vf_write(membership, VFF_IGN_CONN_ERRORS, res,
			       &svcblk_store,
       		       	       sizeof(svcblk_store));
		free_member_list(membership);
	} while (ret == VFR_TIMEOUT && ++tries < 3);

	return (ret==VFR_OK?0:-1);
#endif
}


static int
init_rg(char *name, rg_state_t *svcblk)
{
	svcblk->rs_owner = 0;
	svcblk->rs_last_owner = 0;
	svcblk->rs_state = RG_STATE_STOPPED;
       	svcblk->rs_flags = 0;
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
#ifdef OPENAIS
	char data[DS_MIN_SIZE];
	int datalen;
#else
	uint64_t viewno;
	void *data = NULL;
	cluster_member_list_t *membership;
	uint32_t datalen = 0;
#endif

	/* ... */
	if (name)
		strncpy(svcblk->rs_name, name, sizeof(svcblk->rs_name));

	snprintf(res, sizeof(res),"rg=\"%s\"", svcblk->rs_name);

#ifdef OPENAIS
	while((datalen = ds_read(res, data, sizeof(data))) < 0) {
		if (errno == ENOENT) {
			ds_key_init(res, DS_MIN_SIZE, 10);
		} else {
			perror("ds_read");
			return -1;
		}
	}

	if (datalen <= 0) {

		ret = init_rg(name, svcblk);
		if (ret < 0) {
			printf("Couldn't initialize rg %s!\n", name);
			return RG_EFAIL;
		}

		datalen = ds_read(res, &data, sizeof(data));
		if (ret < 0) {
			printf("Couldn't reread rg %s! (%d)\n", name, ret);
			return RG_EFAIL;
		}
	}

	memcpy(svcblk, data, sizeof(*svcblk));
	swab_rg_state_t(svcblk);

	return 0;
#else
	membership = member_list();

	ret = vf_read(membership, res, &viewno, &data, &datalen);

	if (ret != VFR_OK || datalen == 0) {
		if (data)
			free(data);

		ret = init_rg(name, svcblk);
		if (ret != VFR_OK) {
			free_member_list(membership);
			printf("Couldn't initialize rg %s!\n", name);
			return RG_EFAIL;
		}

		ret = vf_read(membership, res, &viewno, &data, &datalen);
		if (ret != VFR_OK) {
			if (data)
				free(data);
			free_member_list(membership);
			printf("Couldn't reread rg %s! (%d)\n", name, ret);
			return RG_EFAIL;
		}
	}

	if (datalen < sizeof(*svcblk)) {
		printf("Size mismatch; expected %d got %d\n",
		       (int)sizeof(*svcblk), datalen);
		if (data)
			free(data);
		free_member_list(membership);
		return RG_EFAIL;
	}

	/* Copy out the data. */
	memcpy(svcblk, data, sizeof(*svcblk));
	swab_rg_state_t(svcblk);
	free(data);
	free_member_list(membership);

	return 0;
#endif
}


int vf_read_local(char *, uint64_t *, void *, uint32_t *);
int
get_rg_state_local(char *name, rg_state_t *svcblk)
{
	char res[256];
	int ret;
#ifdef OPENAIS
	char data[1024];
	int datalen;
#else
	void *data = NULL;
	uint64_t viewno;
	uint32_t datalen;
#endif

	/* ... */
	if (name)
		strncpy(svcblk->rs_name, name, sizeof(svcblk->rs_name));

	snprintf(res, sizeof(res),"rg=\"%s\"", svcblk->rs_name);

#ifdef OPENAIS
	ret = ds_read(res, data, sizeof(data));
	if (ret <= 0) {
#else
	ret = vf_read_local(res, &viewno, &data, &datalen);

	if (ret != VFR_OK || datalen == 0 ||
	    datalen != sizeof(*svcblk)) {
		if (data)
			free(data);
#endif
		svcblk->rs_owner = 0;
		svcblk->rs_last_owner = 0;
		svcblk->rs_state = RG_STATE_UNINITIALIZED;
       		svcblk->rs_flags = 0;
       		svcblk->rs_restarts = 0;
		svcblk->rs_transition = 0;	
		strncpy(svcblk->rs_name, name, sizeof(svcblk->rs_name));

		return RG_EFAIL;
	}

	/* Copy out the data. */
	memcpy(svcblk, data, sizeof(*svcblk));
	swab_rg_state_t(svcblk);
#ifndef OPENAIS
	free(data);
#endif
	return 0;
}


/**
 * Advise service manager as to whether or not to stop a service, given
 * that we already know it's legal to run the service.
 *
 * @param svcStatus	Current service status.
 * @param svcName	Service name
 * @param req		Specify request to perform
 * @return		0 = DO NOT stop service, return RG_EFAIL
 *			1 = STOP service - return whatever it returns.
 *			2 = DO NOT stop service, return 0 (success)
 *                      3 = DO NOT stop service, return RG_EFORWARD
 *			4 = DO NOT stop service, return RG_EAGAIN
 *			5 = DO NOT stop service, return RG_EFROZEN
 *			6 = DO NOT stop service, mark stopped and return
 *			    RG_SUCCESS (0)
 */
int
svc_advise_stop(rg_state_t *svcStatus, char *svcName, int req)
{
	cluster_member_list_t *membership = member_list();
	int ret = 0;
	
	if (svcStatus->rs_flags & RG_FLAG_FROZEN) {
		clulog(LOG_DEBUG, "Service %s frozen.\n", svcName);
		return 5;
	}

	switch(svcStatus->rs_state) {
	case RG_STATE_FAILED:
		if (req == RG_DISABLE)
			ret = 1; /* Failed services can be disabled */
		else
			ret = 0; /* Failed services may not be stopped */
		break;
		
	case RG_STATE_STOPPING:
	case RG_STATE_STARTED:
	case RG_STATE_CHECK:
	case RG_STATE_STARTING:
	case RG_STATE_RECOVER:
	case RG_STATE_MIGRATE:
		if ((svcStatus->rs_owner != (uint32_t)my_id()) &&
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

		if (svcStatus->rs_owner == 0 ||
		    (svcStatus->rs_owner == (uint32_t)my_id())) {
			/*
			   Service is marked as running locally or on
			   0 (e.g. no member).  Safe
			   to do a full stop.
			 */
			ret = 1;
			break;
		}

		/*
		   Service is marked as running but node is down.
		   Doesn't make much sense to stop it - but we need
		   to mark it stopped
		 */
		ret = 6;
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
			       "Disabling disabled service %s\n",
			       svcName);
			ret = 1;
			break;
		}

		ret = 2;
		clulog(LOG_DEBUG, "Not stopping disabled service %s\n",
		       svcName);
		break;

	default:
		clulog(LOG_ERR,
		       "#42: Cannot stop RG %s: Invalid State %d\n",
		       svcName, svcStatus->rs_state);
		break;
	}

	free_member_list(membership);
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
 * @return		0 = DO NOT start service, return RG_EFAIL
 *			1 = START service - return whatever it returns.
 *			2 = DO NOT start service, return 0
 *			3 = DO NOT start service, return RG_EAGAIN
 *			4 = DO NOT start service, return RG_ERUN
 *			5 = DO NOT start service, return RG_EFROZEN
 */
int
svc_advise_start(rg_state_t *svcStatus, char *svcName, int req)
{
	cluster_member_list_t *membership = member_list();
	int ret = 0;
	
	if (svcStatus->rs_flags & RG_FLAG_FROZEN) {
		clulog(LOG_DEBUG, "Service %s frozen.\n", svcName);
		return 5;
	}

	switch(svcStatus->rs_state) {
	case RG_STATE_FAILED:
		clulog(LOG_ERR,
		       "#43: Service %s has failed; can not start.\n",
		       svcName);
		break;

	case RG_STATE_MIGRATE:
		ret = 4;
		break;
		
	case RG_STATE_STOPPING:
	case RG_STATE_STARTED:
	case RG_STATE_CHECK:
	case RG_STATE_STARTING:
		if (svcStatus->rs_owner == (uint32_t)my_id()) {
		    	/*
			 * Service is already running locally
			clulog(LOG_DEBUG,
			       "RG %s is already running locally\n", svcName);
			 */
			ret = 4;
			break;
		}

		if (svcStatus->rs_owner != (uint32_t)my_id() &&
		    memb_online(membership, svcStatus->rs_owner)) {
			/*
			 * Service is running and the owner is online!
			clulog(LOG_DEBUG, "RG %s is running on member %s.\n",
			       svcName,
			       memb_id_to_name(membership,svcStatus->rs_owner));
			 */
			ret = 4;
			break;
		}

		/* We are allowed to do something with the service.  Make
		   sure we're not locked */
		if (svcStatus->rs_owner == 0) {
			if (rg_locked()) {
				ret = 3;
				break;
			}

			clulog(LOG_NOTICE,
			       "Starting stopped service%s\n",
			       svcName);
			ret = 1;
			break;
		}

		if (rg_locked()) {
			clulog(LOG_WARNING, "Not initiating failover of %s: "
			       "Resource groups locked!\n", svcName);
			ret = 3;
			break;
		}

		/*
		 * Service is running but owner is down -> RG_EFAILOVER
		 */
		clulog(LOG_NOTICE,
		       "Taking over service %s from down member %s\n",
		       svcName, memb_id_to_name(membership,
						svcStatus->rs_owner));
		ret = 1;
		break;

	case RG_STATE_RECOVER:
		/*
		 * Starting failed service...
		 */
		if (req == RG_START_RECOVER || central_events_enabled()) {
			clulog(LOG_NOTICE,
			       "Recovering failed service %s\n",
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
	case RG_STATE_ERROR:
		/* Don't actually enable if the RG is locked! */
		if (rg_locked()) {
			ret = 3;
			break;
		}

		clulog(LOG_NOTICE, "Starting stopped service %s\n",
		       svcName);
		ret = 1;
		break;
	
	case RG_STATE_DISABLED:
	case RG_STATE_UNINITIALIZED:
		if (req == RG_ENABLE || req == RG_START_REMOTE) {
			/* Don't actually enable if the RG is locked! */
			if (rg_locked()) {
				ret = 3;
				break;
			}

			clulog(LOG_NOTICE,
			       "Starting disabled service %s\n",
			       svcName);
			ret = 1;
			break;
		}
		if (req == RG_START_RECOVER) {
			ret = 1;
			break;
		}

		clulog(LOG_DEBUG, "Not starting disabled RG %s\n",
		       svcName);
		break;

	default:
		clulog(LOG_ERR,
		       "#44: Cannot start RG %s: Invalid State %d\n",
		       svcName, svcStatus->rs_state);
		break;
	}

	free_member_list(membership);
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
	struct dlm_lksb lockp;
	int ret;
	rg_state_t svcStatus;

	if (rg_lock(svcName, &lockp) < 0) {
		clulog(LOG_ERR, "#45: Unable to obtain cluster lock: %s\n",
		       strerror(errno));
		return RG_EFAIL;
	}

	if (get_rg_state(svcName, &svcStatus) != 0) {
		rg_unlock(&lockp);
		clulog(LOG_ERR, "#46: Failed getting status for RG %s\n",
		       svcName);
		return RG_EFAIL;
	}

	/* LOCK HELD */
	switch (svc_advise_start(&svcStatus, svcName, req)) {
	case 0: /* Don't start service, return RG_EFAIL */
		rg_unlock(&lockp);
		return RG_EFAIL;
	case 2: /* Don't start service, return 0 */
		rg_unlock(&lockp);
		return 0;
	case 3:
		rg_unlock(&lockp);
		return RG_EAGAIN;
	case 4:
		rg_unlock(&lockp);
		return RG_ERUN;
	case 5:
		rg_unlock(&lockp);
		return RG_EFROZEN;
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

	if (set_rg_state(svcName, &svcStatus) < 0) {
		clulog(LOG_ERR,
		       "#47: Failed changing service status\n");
		rg_unlock(&lockp);
		return RG_EFAIL;
	}
	
	rg_unlock(&lockp);

	ret = group_op(svcName, RG_START);
	ret = !!ret; /* Either it worked or it didn't.  Ignore all the
			cute values scripts might return */

	if (rg_lock(svcName, &lockp) < 0) {
		clulog(LOG_ERR, "#74: Unable to obtain cluster lock: %s\n",
		       strerror(errno));
		return RG_EFAIL;
	}

	svcStatus.rs_state = RG_STATE_STARTED;
	if (set_rg_state(svcName, &svcStatus) != 0) {
		clulog(LOG_ERR,
		       "#75: Failed changing service status\n");
		rg_unlock(&lockp);
		return RG_EFAIL;
	}
	rg_unlock(&lockp);
       
	if (ret == 0) {
		clulog(LOG_NOTICE,
		       "Service %s started\n",
		       svcName);

		broadcast_event(svcName, RG_STATE_STARTED, svcStatus.rs_owner,
				svcStatus.rs_last_owner);
	} else {
		clulog(LOG_WARNING,
		       "#68: Failed to start %s; return value: %d\n",
		       svcName, ret);
	}

	return ret;
}


/**
 * Migrate a service to another node.
 */
int
svc_migrate(char *svcName, int target)
{
	struct dlm_lksb lockp;
	rg_state_t svcStatus;
	int ret;
	cluster_member_list_t *membership;
	cman_node_t *m;

	if (!group_migratory(svcName, 1))
		return RG_EINVAL;

	membership = member_list();
	m = memb_id_to_p(membership, target);
	if (!m) {
		free_member_list(membership);
		return RG_EINVAL;
	}

	count_resource_groups_local(m);
	if (m->cn_svcexcl ||
	    (m->cn_svccount && is_exclusive(svcName))) {
		free_member_list(membership);
		return RG_EDEPEND;
	}
	free_member_list(membership);

	if (rg_lock(svcName, &lockp) < 0) {
		clulog(LOG_ERR, "#45: Unable to obtain cluster lock: %s\n",
		       strerror(errno));
		return RG_EFAIL;
	}

	if (get_rg_state(svcName, &svcStatus) != 0) {
		rg_unlock(&lockp);
		clulog(LOG_ERR, "#46: Failed getting status for RG %s\n",
		       svcName);
		return RG_EFAIL;
	}

	if (svcStatus.rs_owner == (uint32_t)target) {
		rg_unlock(&lockp);
		/* Do not allow migration to its current owner! */
		return 0;
	}

	if (svcStatus.rs_owner != (uint32_t)my_id()) {
		rg_unlock(&lockp);
		return RG_EFORWARD;
	}

	switch(svcStatus.rs_state) {
	case RG_STATE_STARTED:
		break;
	case RG_STATE_STARTING:
	case RG_STATE_STOPPING:
	case RG_STATE_RECOVER:
	case RG_STATE_MIGRATE:
	case RG_STATE_ERROR:
		rg_unlock(&lockp);
		return RG_EAGAIN;
	default:
		rg_unlock(&lockp);
		return RG_EFAIL;
	}

	/* LOCK HELD */
	svcStatus.rs_owner = target;
	svcStatus.rs_last_owner = my_id();
	svcStatus.rs_state = RG_STATE_MIGRATE;
	svcStatus.rs_transition = (uint64_t)time(NULL);

	if (set_rg_state(svcName, &svcStatus) != 0) {
		clulog(LOG_ERR,
		       "#75: Failed changing service status\n");
		rg_unlock(&lockp);
		return RG_EFAIL;
	}
	rg_unlock(&lockp);
       
	ret = group_migrate(svcName, target);

	switch(ret) {
	default:
	case -1:
	case OCF_RA_ERROR:
		svc_fail(svcName);
		/* XXX run svc_status again here to see if it's still
		   healthy; if it is, don't FAIL it; it could be that
		   the target node simply died; in this case, set status
		   back to started */
		return RG_EFAIL;
		break;
	case OCF_RA_NOT_RUNNING:
		/* For these two, the VM was either not running or 
		   migration is simply impossible. */
		/* Don't mark the service as failed; since it's either
		   recoverable or still running. */
		ret = RG_EFAIL;
		break;
	case OCF_RA_NOT_CONFIGURED:
		ret = RG_EINVAL;
		break;
	case 0:
		return 0;
	}

	/* Ok, we've hit a recoverable condition.  Since VMs and migratory
	   services are ... well, migratable, we can just flip the state
	   back to 'started' and error checking will fix it later. */
	if (rg_lock(svcName, &lockp) < 0) {
		clulog(LOG_ERR, "#45: Unable to obtain cluster lock: %s\n",
		       strerror(errno));
		return ret;
	}

	if (get_rg_state(svcName, &svcStatus) != 0) {
		rg_unlock(&lockp);
		clulog(LOG_ERR, "#46: Failed getting status for RG %s\n",
		       svcName);
		return ret;
	}

	if (svcStatus.rs_last_owner != (uint32_t)my_id() ||
	    svcStatus.rs_owner != (uint32_t)target ||
	    svcStatus.rs_state != RG_STATE_MIGRATE) {
		rg_unlock(&lockp);
		return ret;
	}

	svcStatus.rs_owner = my_id();
	svcStatus.rs_state = RG_STATE_STARTED;

	set_rg_state(svcName, &svcStatus);
	rg_unlock(&lockp);

	return ret;
}


/**
 * Ask the other nodes if they've seen this service.  This can be used
 * to allow users the ability to use non-rgmanager tools to migrate
 * a virtual machine to another node in the cluster.
 * 
 * Returns the node ID of the new owner, if any.  -1 if no one in the
 * cluster has seen the service.
 */
int
get_new_owner(char *svcName)
{
	SmMessageSt msgp, response;
	msgctx_t ctx;
	cluster_member_list_t *membership;
	int x, ret = -1, me = my_id();

	/* Build message */
	msgp.sm_hdr.gh_magic = GENERIC_HDR_MAGIC;
	msgp.sm_hdr.gh_command = RG_ACTION_REQUEST;
	msgp.sm_hdr.gh_arg1 = RG_STATUS_INQUIRY;
	msgp.sm_hdr.gh_length = sizeof(msgp);
	msgp.sm_data.d_action = RG_STATUS_INQUIRY;
	strncpy(msgp.sm_data.d_svcName, svcName,
		sizeof(msgp.sm_data.d_svcName));
	msgp.sm_data.d_svcOwner = 0;
	msgp.sm_data.d_ret = 0;

	swab_SmMessageSt(&msgp);

	membership = member_list();
	for (x = 0; x < membership->cml_count && ret < 0; x++) {

		/* don't query down members */
		if (!membership->cml_members[x].cn_member)
			continue;
		/* don't query self */
		if (membership->cml_members[x].cn_nodeid == me)
			continue;

		if (msg_open(MSG_CLUSTER, membership->cml_members[x].cn_nodeid,
			     RG_PORT, &ctx, 2) < 0) {
			/* failed to open: better to claim false successful
			   status rather than claim a failure and possibly
			   end up with a service on >1 node */
			goto out;
		}

		msg_send(&ctx, &msgp, sizeof(msgp));
		if (msg_receive(&ctx, &response, sizeof (response), 5) != sizeof(response))
			goto cont;;

		swab_SmMessageSt(&response);
		if (response.sm_data.d_ret == RG_SUCCESS)
			ret = response.sm_data.d_svcOwner;
		else
			ret = -1;

cont:
		msg_close(&ctx);
	}

out:
	free_member_list(membership);
	
	return ret;
}


/**
   If a service is 'migratory' - that is, it has the 'migratory' attribute
   and has no children, this will query other nodes in the cluster, checking
   to see if the service has migrated to that node using a status inquiry
   message.  Note that this is a very inefficient thing to do; it would be
   much, much better to simply use the cluster tools to migrate rather than
   using the standard management tools for the service/virtual machine.
 */
static int
msvc_check_cluster(char *svcName)
{
	struct dlm_lksb lockp;
	int newowner;
	rg_state_t svcStatus;

	if (!group_migratory(svcName, 1))
		return -1;

	newowner = get_new_owner(svcName);
	if (newowner < 0) {
		clulog(LOG_DEBUG, "No other nodes have seen %s\n", svcName);
		return -1;
	}

	/* New owner found */
	clulog(LOG_NOTICE, "Migration: %s is running on %d\n", svcName, newowner);

	/* If the check succeeds (returns 0), then flip the state back to
	   'started' - with a new owner */
	if (rg_lock(svcName, &lockp) < 0) {
		clulog(LOG_ERR, "#451: Unable to obtain cluster lock: %s\n",
			strerror(errno));
		return -1;
	}

	if (get_rg_state(svcName, &svcStatus) != 0) {
		rg_unlock(&lockp);
		clulog(LOG_ERR, "#452: Failed getting status for RG %s\n",
		       svcName);
		return -1;
	}

	svcStatus.rs_state = RG_STATE_STARTED;
	svcStatus.rs_owner = newowner;

	if (set_rg_state(svcName, &svcStatus) != 0) {
		rg_unlock(&lockp);
		clulog(LOG_ERR, "#453: Failed setting status for RG %s\n",
		       svcName);
		return -1;
	}
	rg_unlock(&lockp);

	return newowner;
}


/**
 * Check status of a cluster service 
 *
 * @param svcName	Service name to check.
 * @return		RG_EFORWARD, RG_EFAIL, 0
 */
int
svc_status(char *svcName)
{
	struct dlm_lksb lockp;
	rg_state_t svcStatus;
	int ret;

	if (rg_lock(svcName, &lockp) < 0) {
		clulog(LOG_ERR, "#48: Unable to obtain cluster lock: %s\n",
		       strerror(errno));
		return RG_EFAIL;
	}

	if (get_rg_state(svcName, &svcStatus) != 0) {
		rg_unlock(&lockp);
		clulog(LOG_ERR, "#49: Failed getting status for RG %s\n",
		       svcName);
		return RG_EFAIL;
	}
	rg_unlock(&lockp);

	if (svcStatus.rs_flags & RG_FLAG_FROZEN)
		/* Don't check status if the service is frozen */
		return 0;

	if (svcStatus.rs_owner != (uint32_t)my_id())
		/* Don't check status for anything not owned */
		return 0;

	if (svcStatus.rs_state != RG_STATE_STARTED &&
	    svcStatus.rs_state != RG_STATE_MIGRATE)
		/* Not-running RGs should not be checked either. */
		return 0;

	ret = group_op(svcName, RG_STATUS);

	/* For running services, if the return code is 0, we're done*/
	if (svcStatus.rs_state == RG_STATE_STARTED)
		return handle_started_status(svcName, ret, &svcStatus);
	
	return handle_migrate_status(svcName, ret, &svcStatus);
}


static inline int
handle_started_status(char *svcName, int ret,
		      rg_state_t __attribute__((unused)) *svcStatus)
{
	int newowner;

	if (ret & SFL_FAILURE) {
		newowner = msvc_check_cluster(svcName);
		if (newowner >= 0)
			return 0; /* running but not here */
		return ret;	  /* not running anymore */
	}

	/* Ok, we have a recoverable service.  Try to perform
	   inline recovery */
	if (ret & SFL_RECOVERABLE) {

		clulog(LOG_WARNING, "Some independent resources in %s failed; "
		       "Attempting inline recovery\n", svcName);

		ret = group_op(svcName, RG_CONDSTOP);
		if (!(ret & SFL_FAILURE)) {
			ret = group_op(svcName, RG_CONDSTART);
		}

		if (ret) {
			clulog(LOG_WARNING, "Inline recovery of %s failed\n",
			       svcName);
		} else {
			clulog(LOG_NOTICE,
			       "Inline recovery of %s succeeded\n",
			       svcName);
			return 0;
		}
	}

	return ret;
}


static inline int
handle_migrate_status(char *svcName, int ret, rg_state_t *svcStatus)
{
	struct dlm_lksb lockp;
	/* For service(s) migrating to the local node, ignore invalid
	   return codes.
	   XXX Should put a timeout on migrating services */
	if (ret != 0)
		return 0;

	/* If the check succeeds (returns 0), then flip the state back to
	   'started' - we now own the service */
	if (rg_lock(svcName, &lockp) < 0) {
		clulog(LOG_ERR, "#45: Unable to obtain cluster lock: %s\n",
			strerror(errno));
		return RG_EFAIL;
	}

	svcStatus->rs_state = RG_STATE_STARTED;
	if (set_rg_state(svcName, svcStatus) != 0) {
		rg_unlock(&lockp);
		clulog(LOG_ERR, "#46: Failed getting status for RG %s\n",
		       svcName);
		return RG_EFAIL;
	}
	rg_unlock(&lockp);

	clulog(LOG_NOTICE, "%s is now running locally\n", svcName);

	return 0;
}


int
svc_status_inquiry(char *svcName)
{
	rg_state_t svcStatus;

	if (get_rg_state_local(svcName, &svcStatus) != 0) {
		clulog(LOG_ERR, "Failed getting local status for RG %s\n",
		       svcName);
		return RG_EFAIL;
	}

	if (svcStatus.rs_flags & RG_FLAG_FROZEN)
		return 0;
	
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
	struct dlm_lksb lockp;
	rg_state_t svcStatus;
	int ret;
	int old_state;

	if (!rg_quorate()) {
		clulog(LOG_WARNING, "#69: Unclean %s of %s\n", 
		       rg_req_str(req), svcName);
		return group_op(svcName, RG_STOP);
	}

	if (rg_lock(svcName, &lockp) == RG_EFAIL) {
		clulog(LOG_ERR, "#50: Unable to obtain cluster lock: %s\n",
		       strerror(errno));
		return RG_EFAIL;
	}

	if (get_rg_state(svcName, &svcStatus) != 0) {
		rg_unlock(&lockp);
		clulog(LOG_ERR, "#51: Failed getting status for RG %s\n",
		       svcName);
		return RG_EFAIL;
	}

	switch (svc_advise_stop(&svcStatus, svcName, req)) {
	case 0:
		rg_unlock(&lockp);
		clulog(LOG_DEBUG, "Unable to stop RG %s in %s state\n",
		       svcName, rg_state_str(svcStatus.rs_state));
		return RG_EFAIL;
	case 6:
		/* Mark stopped, but do not do anything */
		svcStatus.rs_last_owner = svcStatus.rs_owner;
		svcStatus.rs_owner = 0;
		svcStatus.rs_state = RG_STATE_STOPPED;
		if (set_rg_state(svcName, &svcStatus) != 0) {
			rg_unlock(&lockp);
			return RG_EFAIL;
		}
		/* FALLTHROUGH */
	case 2:
		rg_unlock(&lockp);
		return RG_ESUCCESS;
	case 3:
		rg_unlock(&lockp);
		return RG_EFORWARD;
	case 4:
		rg_unlock(&lockp);
		return RG_EAGAIN;
	case 5:
		rg_unlock(&lockp);
		return RG_EFROZEN;
	default:
		break;
	}

	old_state = svcStatus.rs_state;

	clulog(LOG_NOTICE, "Stopping service %s\n", svcName);

	if (recover) 
	       	svcStatus.rs_state = RG_STATE_ERROR;
	else
		svcStatus.rs_state = RG_STATE_STOPPING;
	svcStatus.rs_transition = (uint64_t)time(NULL);

	//printf("rg state = %s\n", rg_state_str(svcStatus.rs_state));

	if (set_rg_state(svcName, &svcStatus) != 0) {
		rg_unlock(&lockp);
		clulog(LOG_ERR, "#52: Failed changing RG status\n");
		return RG_EFAIL;
	}
	rg_unlock(&lockp);

	ret = group_op(svcName, RG_STOP);

	if (old_state == RG_STATE_FAILED && newstate == RG_STATE_DISABLED) {
		if (ret)
			clulog(LOG_ALERT, "Marking %s as 'disabled', "
			       "but some resources may still be allocated!\n",
			       svcName);
		_svc_stop_finish(svcName, 0, newstate);
	} else {
		_svc_stop_finish(svcName, ret, newstate);
	}

	return ret;
}


static int
_svc_stop_finish(char *svcName, int failed, uint32_t newstate)
{
	rg_state_t svcStatus;
	struct dlm_lksb lockp;

	if (rg_lock(svcName, &lockp) == RG_EFAIL) {
		clulog(LOG_ERR, "#53: Unable to obtain cluster lock: %s\n",
		       strerror(errno));
		return RG_EFAIL;
	}

	if (get_rg_state(svcName, &svcStatus) != 0) {
		rg_unlock(&lockp);
		clulog(LOG_ERR, "#54: Failed getting status for RG %s\n",
		       svcName);
		return RG_EFAIL;
	}

	if ((svcStatus.rs_state != RG_STATE_STOPPING) &&
	    (svcStatus.rs_state != RG_STATE_ERROR) &&
	    (svcStatus.rs_state != RG_STATE_RECOVER)) {
		rg_unlock(&lockp);
		return 0;
	}

	svcStatus.rs_last_owner = svcStatus.rs_owner;
	svcStatus.rs_owner = 0;

	if (failed) {
		clulog(LOG_CRIT, "#12: RG %s failed to stop; intervention "
		       "required\n", svcName);
		newstate = RG_STATE_FAILED;
	} else if (svcStatus.rs_state == RG_STATE_ERROR) {
		svcStatus.rs_state = RG_STATE_RECOVER;
		newstate = RG_STATE_RECOVER;
	}

	svcStatus.rs_state = newstate;

	clulog(LOG_NOTICE, "Service %s is %s\n", svcName,
	       rg_state_str(svcStatus.rs_state));
	//printf("rg state = %s\n", rg_state_str(svcStatus.rs_state));

	svcStatus.rs_transition = (uint64_t)time(NULL);
	if (set_rg_state(svcName, &svcStatus) != 0) {
		rg_unlock(&lockp);
		clulog(LOG_ERR, "#55: Failed changing RG status\n");
		return RG_EFAIL;
	}
	rg_unlock(&lockp);

	broadcast_event(svcName, newstate, -1, svcStatus.rs_last_owner);

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
svc_stop(char *svcName, int req)
{
	return _svc_stop(svcName, req, (req == RG_STOP_RECOVER),
			 RG_STATE_STOPPED);
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
	struct dlm_lksb lockp;
	rg_state_t svcStatus;

	if (rg_lock(svcName, &lockp) == RG_EFAIL) {
		clulog(LOG_ERR, "#55: Unable to obtain cluster lock: %s\n",
		       strerror(errno));
		return RG_EFAIL;
	}

	clulog(LOG_DEBUG, "Handling failure request for RG %s\n", svcName);

	if (get_rg_state(svcName, &svcStatus) != 0) {
		rg_unlock(&lockp);
		clulog(LOG_ERR, "#56: Failed getting status for RG %s\n",
		       svcName);
		return RG_EFAIL;
	}

	if ((svcStatus.rs_state == RG_STATE_STARTED) &&
	    (svcStatus.rs_owner != (uint32_t)my_id())) {
		rg_unlock(&lockp);
		clulog(LOG_DEBUG, "Unable to disable RG %s in %s state\n",
		       svcName, rg_state_str(svcStatus.rs_state));
		return RG_EFAIL;
	}

	/*
	 * Leave a bread crumb so we can debug the problem with the service!
	 */
	if (svcStatus.rs_owner != 0) {
		svcStatus.rs_last_owner = svcStatus.rs_owner;
		svcStatus.rs_owner = 0;
	}
	svcStatus.rs_state = RG_STATE_FAILED;
	svcStatus.rs_transition = (uint64_t)time(NULL);
	svcStatus.rs_restarts = 0;
	if (set_rg_state(svcName, &svcStatus) != 0) {
		rg_unlock(&lockp);
		clulog(LOG_ERR, "#57: Failed changing RG status\n");
		return RG_EFAIL;
	}
	rg_unlock(&lockp);

	broadcast_event(svcName, RG_STATE_FAILED, -1,
			svcStatus.rs_last_owner);

	return 0;
}

/**
 * Flag/Unflag a cluster service as frozen.
 *
 * @param svcName	Service ID to flag/unflag as frozen.
 * @return		FAIL, 0
 */
int
_svc_freeze(char *svcName, int enabled)
{
	struct dlm_lksb lockp;
	rg_state_t svcStatus;

	if (rg_lock(svcName, &lockp) == RG_EFAIL) {
		clulog(LOG_ERR, "#55: Unable to obtain cluster lock: %s\n",
		       strerror(errno));
		return RG_EFAIL;
	}

	clulog(LOG_DEBUG, "Handling %s request for RG %s\n", enabled?"freeze":"unfreeze",
	       svcName);

	if (get_rg_state(svcName, &svcStatus) != 0) {
		rg_unlock(&lockp);
		clulog(LOG_ERR, "#56: Failed getting status for RG %s\n",
		       svcName);
		return RG_EFAIL;
	}

	switch(svcStatus.rs_state) {
	case RG_STATE_STOPPED:
	case RG_STATE_STARTED:
	case RG_STATE_DISABLED:

		if (enabled == 1) {
			clulog(LOG_DEBUG, "Freezing RG %s\n", svcName);
			svcStatus.rs_flags |= RG_FLAG_FROZEN;
		} else {
			clulog(LOG_DEBUG, "Unfreezing RG %s\n", svcName);
			svcStatus.rs_flags &= ~RG_FLAG_FROZEN;
		}

		if (set_rg_state(svcName, &svcStatus) != 0) {
			rg_unlock(&lockp);
			clulog(LOG_ERR, "#57: Failed changing RG status\n");
			return RG_EFAIL;
		}
		break;

	default:
		rg_unlock(&lockp);
		return RG_EAGAIN;
		break;
	}

	rg_unlock(&lockp);

	return 0;
}

int
svc_freeze(char *svcName)
{
	return _svc_freeze(svcName, 1);
}

int
svc_unfreeze(char *svcName)
{
	return _svc_freeze(svcName, 0);
}


/*
 * Send a message to the target node to start the service.
 */
int
svc_start_remote(char *svcName, int request, uint32_t target)
{
	SmMessageSt msg_relo;
	int msg_ret;
	cluster_member_list_t *ml;
	msgctx_t ctx;

	/* Build the message header */
	msg_relo.sm_hdr.gh_magic = GENERIC_HDR_MAGIC;
	msg_relo.sm_hdr.gh_command = RG_ACTION_REQUEST;
	/* XXX XXX */
	msg_relo.sm_hdr.gh_arg1 = RG_ACTION_MASTER;
	msg_relo.sm_hdr.gh_length = sizeof (SmMessageSt);
	msg_relo.sm_data.d_action = request;
	strncpy(msg_relo.sm_data.d_svcName, svcName,
		sizeof(msg_relo.sm_data.d_svcName));
	msg_relo.sm_data.d_ret = 0;
	msg_relo.sm_data.d_svcOwner = target;
	/* Open a connection to the other node */

	if (msg_open(MSG_CLUSTER, target, RG_PORT, &ctx, 2)< 0) {
		clulog(LOG_ERR,
		       "#58: Failed opening connection to member #%d\n",
		       target);
		return -1;
	}

	/* Encode */
	swab_SmMessageSt(&msg_relo);

	/* Send relocate message to the other node */
	if (msg_send(&ctx, &msg_relo, sizeof (SmMessageSt)) < 
	    (int)sizeof (SmMessageSt)) {
		clulog(LOG_ERR,
		       "#59: Error sending remote-start request to member #%d\n",
		       target);
		msg_close(&ctx);
		return -1;
	}

	clulog(LOG_DEBUG, "Sent remote-start request to %d\n", (int)target);

	/* Check the response */
	do {
		msg_ret = msg_receive(&ctx, &msg_relo,
					      sizeof (SmMessageSt), 10);

		if ((msg_ret == -1 && errno != ETIMEDOUT) ||
		    (msg_ret > 0)) {
			break;
		}

		/* Check to see if resource groups are locked for local
		   shutdown */
		if (rg_locked()) {
			clulog(LOG_WARNING,
			       "#XX: Cancelling relocation: Shutting down\n");
			msg_close(&ctx);
			return RG_NO;
		}

		/* Check for node transition in the middle of a relocate */
		ml = member_list();
		if (memb_online(ml, target)) {
			free_member_list(ml);
			continue;
		}
		clulog(LOG_WARNING,
		       "#XX: Cancelling relocation: Target node down\n");
		free_member_list(ml);
		msg_close(&ctx);
		return RG_EFAIL;
	} while (1);

	if (msg_ret != sizeof (SmMessageSt)) {
		/* 
		 * In this case, we don't restart the service, because the 
		 * service state is actually unknown to us at this time.
		 */
		clulog(LOG_ERR, "#60: Mangled reply from member #%d during RG "
		       "relocate\n", target);
		msg_close(&ctx);
		return 0;	/* XXX really UNKNOWN */
	}

	/* Got a valid response from other node. */
	msg_close(&ctx);

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
handle_relocate_req(char *svcName, int request, int preferred_target,
		    int *new_owner)
{
	cluster_member_list_t *allowed_nodes = NULL, *backup = NULL;
	cman_node_t *m;
	int target = preferred_target, me = my_id();
	int ret, x;
	rg_state_t svcStatus;
	
	get_rg_state_local(svcName, &svcStatus);
	if (svcStatus.rs_state == RG_STATE_DISABLED ||
	    svcStatus.rs_state == RG_STATE_UNINITIALIZED)
		return RG_EINVAL;

	if (preferred_target > 0) {
		/* TODO: simplify this and don't keep alloc/freeing 
		   member lists */
		allowed_nodes = member_list();
		/* Avoid even bothering the other node if we can */
		m = memb_id_to_p(allowed_nodes, preferred_target);
		if (!m) {
			free_member_list(allowed_nodes);
			return RG_EINVAL;
		}

		count_resource_groups_local(m);
		if (m->cn_svcexcl ||
	    	    (m->cn_svccount && is_exclusive(svcName))) {
			free_member_list(allowed_nodes);
			return RG_EDEPEND;
		}
		free_member_list(allowed_nodes);
	}

	/*
	 * Stop the service - if we haven't already done so.
	 */
	if (request != RG_START_RECOVER) {
		ret = _svc_stop(svcName, request, 0, RG_STATE_STOPPED);
		if (ret == RG_EFAIL) {
			svc_fail(svcName);
			return RG_EFAIL;
		}
		if (ret == RG_EFROZEN) {
			return RG_EFROZEN;
		}
		if (ret == RG_EFORWARD)
			return RG_EFORWARD;
	}

	if (preferred_target > 0) {

		allowed_nodes = member_list();
		/*
	   	   Mark everyone except me and the preferred target DOWN for now
		   If we can't start it on the preferred target, then we'll try
	 	   other nodes.
		 */
		//count_resource_groups(allowed_nodes);
		backup = member_list_dup(allowed_nodes);

		for (x = 0; x < allowed_nodes->cml_count; x++) {
			if (allowed_nodes->cml_members[x].cn_nodeid == me ||
		    	    allowed_nodes->cml_members[x].cn_nodeid ==
			    		preferred_target)
				continue;
			allowed_nodes->cml_members[x].cn_member = 0;
		}

		/*
		 * First, see if it's legal to relocate to the target node.
		 * Legal means: the node is online and is in the
		 * [restricted] failover domain of the service, or the
		 * service has no failover domain.
		 */
		target = best_target_node(allowed_nodes, me, svcName, 1);

		free_member_list(allowed_nodes);

		/*
		 * I am the ONLY one capable of running this service,
		 * PERIOD...
		 */
		if (target == me && me != preferred_target) {
			free_member_list(backup);
			goto exhausted;
		}

		if (target == me) {
			/*
			   Relocate to self.  Don't send a network request
			   to do it; it would block.
			 */
			if (svc_start(svcName, RG_START) == 0) {
				*new_owner = me;
				return 0;
			}
		} else if (target == preferred_target) {
			/*
		 	 * It's legal to start the service on the given
		 	 * node.  Try to do so.
		 	 */
			if (svc_start_remote(svcName, request, target) == 0) {
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
	if (backup) {
		allowed_nodes = backup;
	} else {
		allowed_nodes = member_list();
		//count_resource_groups(allowed_nodes);
	}

	if (preferred_target > 0)
		memb_mark_down(allowed_nodes, preferred_target);
	memb_mark_down(allowed_nodes, me);

	while (memb_count(allowed_nodes)) {
		target = best_target_node(allowed_nodes, me, svcName, 1);
		if (target == me)
			goto exhausted;

		ret = svc_start_remote(svcName, request, target);
		switch (ret) {
		case RG_ERUN:
			/* Someone stole the service while we were 
			   trying to relo it */
			get_rg_state_local(svcName, &svcStatus);
			*new_owner = svcStatus.rs_owner;
			free_member_list(allowed_nodes);
			return 0;
		case RG_EDEPEND:
		case RG_EFAIL:
			memb_mark_down(allowed_nodes, target);
			continue;
		case RG_EABORT:
			svc_report_failure(svcName);
			free_member_list(allowed_nodes);
			return RG_EFAIL;
		default:
			/* deliberate fallthrough */
			clulog(LOG_ERR,
			       "#61: Invalid reply from member %d during"
			       " relocate operation!\n", target);
		case RG_NO:
			/* state uncertain */
			free_member_list(allowed_nodes);
			clulog(LOG_CRIT, "State Uncertain: svc:%s "
			       "nid:%d req:%s ret:%d\n", svcName,
			       target, rg_req_str(request), ret);
			return 0;
		case 0:
			*new_owner = target;
			clulog(LOG_NOTICE, "Service %s is now running "
			       "on member %d\n", svcName, (int)target);
			free_member_list(allowed_nodes);
			return 0;
		}
	}
	free_member_list(allowed_nodes);

	/*
	 * We got sent here from handle_start_req.
	 * We're DONE.
	 */
	if (request == RG_START_RECOVER) {
		_svc_stop_finish(svcName, 0, RG_STATE_STOPPED);
		return RG_EFAIL;
	}

	/*
	 * All potential places for the service to start have been exhausted.
	 * We're done.
	 */
exhausted:
	if (!rg_locked()) {
		clulog(LOG_WARNING,
		       "#70: Failed to relocate %s; restarting locally\n",
		       svcName);
		if (svc_start(svcName, RG_START_RECOVER) == 0) {
			*new_owner = me;
			return RG_EFAIL;
		}
	}

	if (svc_stop(svcName, RG_STOP) != 0) {
		svc_fail(svcName);
		svc_report_failure(svcName);
	}

	return RG_EFAIL;
}


pthread_mutex_t exclusive_mutex = PTHREAD_MUTEX_INITIALIZER;
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
handle_start_req(char *svcName, int req, int *new_owner)
{
	int ret, tolerance = FOD_BEST;
	cluster_member_list_t *membership;
	int need_check, actual_failure = 0;
  
 	/* When we get an enable req. for a migratory service, 
 	   check other nodes to see if they are already running
 	   said service - and ignore failover domain constraints 
 	 */
	if ((ret = msvc_check_cluster(svcName)) >= 0) {
		*new_owner = ret;
		return RG_SUCCESS;
	}
 
	need_check = have_exclusive_resources();
	membership = member_list();

	/*
	 * When a service request is from a user application (eg, clusvcadm),
	 * accept FOD_GOOD instead of FOD_BEST
	 */
	if (req == RG_ENABLE)
		tolerance = FOD_GOOD;
	if (req != RG_RESTART &&
	    req != RG_START_RECOVER &&
	    (node_should_start_safe(my_id(), membership, svcName) <
	     tolerance)) {
		free_member_list(membership);
		return RG_EFAIL;
	}
	if (need_check) {
		pthread_mutex_lock(&exclusive_mutex);
		ret = check_exclusive_resources(membership, svcName);
		if (ret != 0) {
			free_member_list(membership);
			pthread_mutex_unlock(&exclusive_mutex);
			if (ret > 0)
				goto relocate;
			else
				return RG_EFAIL;
		}
	}
	free_member_list(membership);

	/* Check for dependency.  We cannot start unless our
	   dependency is met */
	if (check_depend_safe(svcName) == 0)
		return RG_EDEPEND;
	
	/*
	 * This is a 'root' start request.  We need to clear out our failure
	 * mask here - so that we can try all nodes if necessary.
	 */
	ret = svc_start(svcName, req);
	if (need_check)
		pthread_mutex_unlock(&exclusive_mutex);

	/* 
	   If services are locked, return the error 
	  */
	if (ret == RG_EAGAIN || ret == RG_ERUN || ret == RG_EFROZEN)
		return ret;

	/*
	 * If we succeeded, then we're done.
	 */
	if (ret == RG_ESUCCESS) {
		*new_owner = my_id();
		return RG_ESUCCESS;
	}

	/* Already running? */
	if (ret == RG_NO) {
		return RG_ESUCCESS;
	}
	
	/* 
	 * Keep the state open so the other nodes don't try to start
	 * it.  This allows us to be the 'root' of a given service.
	 */
	clulog(LOG_DEBUG, "Stopping failed service %s\n", svcName);
	if (svc_stop(svcName, RG_STOP_RECOVER) != 0) {
		clulog(LOG_CRIT,
		       "#13: Service %s failed to stop cleanly\n",
		       svcName);
		(void) svc_fail(svcName);

		/*
		 * If we failed to stop the service, we're done.  At this
		 * point, we can't determine the service's status - so
		 * trying to start it on other nodes is right out.
		 */
		return RG_EABORT;
	}
	actual_failure = 1;
	
relocate:
	/*
	 * OK, it failed to start - but succeeded to stop.  Now,
	 * we should relocate the service.
	 */
	if (actual_failure)
		clulog(LOG_WARNING, "#71: Relocating failed service %s\n",
	       	       svcName);
	ret = handle_relocate_req(svcName, RG_START_RECOVER, -1, new_owner);

	/* If we leave the service stopped, instead of disabled, someone
	   will try to start it after the next node transition */
	if (ret == RG_EFAIL) {
		if (svc_stop(svcName, RG_STOP) != 0) {
			svc_fail(svcName);
			svc_report_failure(svcName);
		}
	}

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
	uint32_t me = my_id();
	cluster_member_list_t *membership;
	int need_check;

	if (rg_locked()) {
		/* don't even calc if rg's locked */
		return RG_EFAIL;
	}

	need_check = have_exclusive_resources();
	membership = member_list();

	/* XXX ok, so we need to say "should I start this if I was the
	   only cluster member online */
	for (x = 0; x < (int)membership->cml_count; x++) {
		if (membership->cml_members[x].cn_nodeid == (int)me)
			continue;

		membership->cml_members[x].cn_member = 0;
	}

	if (req == RG_ENABLE)
		tolerance = FOD_GOOD;

	/*
	 * See if we agree with our ability to start the given service.
	 */
	if (node_should_start_safe(me, membership, svcName) < tolerance){
		free_member_list(membership);
		return RG_EFAIL;
	}
	if (need_check) {
		pthread_mutex_lock(&exclusive_mutex);
		if (check_exclusive_resources(membership, svcName) != 0) {
			free_member_list(membership);
			pthread_mutex_unlock(&exclusive_mutex);
			return RG_EEXCL;
		}
	}
	free_member_list(membership);

	x = svc_start(svcName, req);

	if ((x == 0) || (x == RG_ERUN)) {
		if (need_check)
			pthread_mutex_unlock(&exclusive_mutex);
		return x;
	}
	if (need_check)
		pthread_mutex_unlock(&exclusive_mutex);

	if (svc_stop(svcName, RG_STOP_RECOVER) == 0)
		return RG_EFAIL;

	svc_fail(svcName);
	return RG_EABORT;
}


/**
  handle_recover_req
 */
int
handle_recover_req(char *svcName, int *new_owner)
{
	char policy[20];

	get_recovery_policy(svcName, policy, sizeof(policy));

	if (!strcasecmp(policy, "disable")) {
		return svc_disable(svcName);
	} else if (!strcasecmp(policy, "relocate")) {
		return handle_relocate_req(svcName, RG_START_RECOVER, -1,
					   new_owner);
	}

	/* Check restart counter/timer for this resource */
	if (check_restart(svcName) > 0) {
		clulog(LOG_NOTICE, "Restart threshold for %s exceeded; "
		       "attempting to relocate\n", svcName);
		return handle_relocate_req(svcName, RG_START_RECOVER, -1,
					   new_owner);
	}

	return handle_start_req(svcName, RG_START_RECOVER, new_owner);
}


int
handle_fd_start_req(char *svcName, int request, int *new_owner)
{
	cluster_member_list_t *allowed_nodes;
	int target, me = my_id();
	int ret = RG_EFAIL;

	/* When we get an enable req. for a migratory service, 
	   check other nodes to see if they are already running
	   said service - and ignore failover domain constraints 
	 */
	if ((ret = msvc_check_cluster(svcName)) >= 0) {
		*new_owner = ret;
		return RG_SUCCESS;
	}

	allowed_nodes = member_list();

	while (memb_count(allowed_nodes)) {
		target = best_target_node(allowed_nodes, 0,
	 				  svcName, 1);
	  	if (target == me) {
	   		ret = handle_start_remote_req(svcName, request);
			if (ret == RG_EAGAIN)
				goto out;
	    	} else if (target < 0) {
			goto out;
	       	} else {
			ret = svc_start_remote(svcName, RG_START_REMOTE,
					       target);
		}

		switch(ret) {
		case RG_ESUCCESS:
		    	ret = RG_ESUCCESS;
			goto out;
		case RG_ERUN:
		      	ret = RG_ERUN;
			goto out;
		case RG_EFAIL:
			memb_mark_down(allowed_nodes, target);
			continue;
		case RG_EABORT:
			svc_report_failure(svcName);
			ret = RG_EFAIL;
			goto out;
      		default:
			clulog(LOG_ERR,
	 		       "#6X: Invalid reply [%d] from member %d during"
	  		       " relocate operation!\n", ret, target);
	   	}
	}

out:
	free_member_list(allowed_nodes);
	return ret;
}
