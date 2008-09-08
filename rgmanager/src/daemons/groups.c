//#define DEBUG
#include <platform.h>
#include <resgroup.h>
#include <restart_counter.h>
#include <reslist.h>
#include <vf.h>
#include <message.h>
#include <ccs.h>
#include <clulog.h>
#include <members.h>
#include <list.h>
#include <reslist.h>
#include <assert.h>
#include <event.h>

/* Use address field in this because we never use it internally,
   and there is no extra space in the cman_node_t type.
   */

#define cn_svccount cn_address.cna_address[0] /* Theses are uint8_t size */
#define cn_svcexcl  cn_address.cna_address[1]

extern event_table_t *master_event_table;

static int config_version = 0;
static resource_t *_resources = NULL;
static resource_rule_t *_rules = NULL;
static resource_node_t *_tree = NULL;
static fod_t *_domains = NULL;

#ifdef WRAP_LOCKS
pthread_mutex_t config_mutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
pthread_mutex_t status_mutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
#else
pthread_mutex_t config_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t status_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
pthread_rwlock_t resource_lock = PTHREAD_RWLOCK_INITIALIZER;

void res_build_name(char *, size_t, resource_t *);
int get_rg_state_local(char *, rg_state_t *);
int group_migratory(char *groupname, int lock);
int _group_property(char *groupname, char *property, char *ret, size_t len);


struct status_arg {
	msgctx_t *ctx;
	int fast;
};


/**
   See if a given node ID should start a resource, given cluster membership

   @see node_should_start
 */
int
node_should_start_safe(uint32_t nodeid, cluster_member_list_t *membership,
		       char *rg_name)
{
	int ret;

	pthread_rwlock_rdlock(&resource_lock);
	ret = node_should_start(nodeid, membership, rg_name, &_domains);
	pthread_rwlock_unlock(&resource_lock);

	return ret;
}


int
node_domain_set_safe(char *domainname, int **ret, int *retlen, int *flags)
{
	int rv = 0;
	pthread_rwlock_rdlock(&resource_lock);
	rv = node_domain_set(&_domains, domainname, ret, retlen, flags);
	pthread_rwlock_unlock(&resource_lock);

	return rv;
}


int
count_resource_groups(cluster_member_list_t *ml)
{
	resource_t *res;
	resource_node_t *node;
	char rgname[64], *val;
	int x;
	rg_state_t st;
	struct dlm_lksb lockp;
	cman_node_t *mp;

	for (x = 0; x < ml->cml_count; x++) {
		ml->cml_members[x].cn_svccount = 0;
		ml->cml_members[x].cn_svcexcl = 0;
	}

	pthread_rwlock_rdlock(&resource_lock);

	list_do(&_tree, node) {

		res = node->rn_resource;

		res_build_name(rgname, sizeof(rgname), res);

		if (rg_lock(rgname, &lockp) < 0) {
			clulog(LOG_ERR, "#XX: Unable to obtain cluster "
			       "lock @ %s:%d: %s\n", __FILE__, __LINE__,
			       strerror(errno));
			continue;
		}

		if (get_rg_state(rgname, &st) < 0) {
			clulog(LOG_ERR, "#34: Cannot get status "
			       "for service %s\n", rgname);
			rg_unlock(&lockp);
			continue;
		}

		rg_unlock(&lockp);

		if (st.rs_state != RG_STATE_STARTED &&
		     st.rs_state != RG_STATE_STARTING)
			continue;

		mp = memb_id_to_p(ml, st.rs_owner);
		if (!mp)
			continue;

		++mp->cn_svccount;

		val = res_attr_value(res, "exclusive");
		if (val && ((!strcmp(val, "yes") ||
				     (atoi(val)>0))) ) {
			++mp->cn_svcexcl;
		}

	} while (!list_done(&_tree, node));

	pthread_rwlock_unlock(&resource_lock);
	return 0;
}


static inline int 
is_exclusive_res(resource_t *res)
{
	char *val;

	val = res_attr_value(res, "exclusive");
	if (val && ((!strcmp(val, "yes") ||
			(atoi(val)>0))) ) {
		return 1;
	}
	return 0;
}


/* Locked exported function */
int 
is_exclusive(char *svcName)
{
	int ret = 0;
	resource_t *res = NULL;

	pthread_rwlock_rdlock(&resource_lock);
	res = find_root_by_ref(&_resources, svcName);

	if (!res)
		ret = RG_ENOSERVICE;
	else
		ret = is_exclusive_res(res);

	pthread_rwlock_unlock(&resource_lock);
	return ret;
}


resource_node_t *
node_by_ref(resource_node_t **tree, char *name)
{
	resource_t *res;
	resource_node_t *node, *ret = NULL;
	char rgname[64];
	int x;

	list_for(tree, node, x) {

		res = node->rn_resource;
		res_build_name(rgname, sizeof(rgname), res);

		if (!strcasecmp(name, rgname)) {
			ret = node;
			break;
		}
	}

	return ret;
}


int
count_resource_groups_local(cman_node_t *mp)
{
	resource_t *res;
	resource_node_t *node;
	char rgname[64];
	rg_state_t st;

	mp->cn_svccount = 0;
	mp->cn_svcexcl = 0;

	pthread_rwlock_rdlock(&resource_lock);

	list_do(&_tree, node) {

		res = node->rn_resource;

		res_build_name(rgname, sizeof(rgname), res);

		if (get_rg_state_local(rgname, &st) < 0) {
			continue;
		}

		if (st.rs_state != RG_STATE_STARTED &&
		     st.rs_state != RG_STATE_STARTING)
			continue;

		if (mp->cn_nodeid != st.rs_owner)
			continue;

		++mp->cn_svccount;

		if (is_exclusive_res(res))
			++mp->cn_svcexcl;

	} while (!list_done(&_tree, node));

	pthread_rwlock_unlock(&resource_lock);
	return 0;
}


int
have_exclusive_resources(void)
{
	resource_t *res;

	pthread_rwlock_rdlock(&resource_lock);

	list_do(&_resources, res) {
		if (is_exclusive_res(res)) {
			pthread_rwlock_unlock(&resource_lock);
			return 1;
		}

	} while (!list_done(&_resources, res));

	pthread_rwlock_unlock(&resource_lock);

	return 0;
}


int
check_exclusive_resources(cluster_member_list_t *membership, char *svcName)
{
	cman_node_t *mp;
	int exclusive, count, excl; 
	resource_t *res;

	mp = memb_id_to_p(membership, my_id());
	assert(mp);
	count_resource_groups_local(mp);
	exclusive = mp->cn_svcexcl;
	count = mp->cn_svccount;
	pthread_rwlock_rdlock(&resource_lock);
	res = find_root_by_ref(&_resources, svcName);
	if (!res) {
		pthread_rwlock_unlock(&resource_lock);
		return RG_ENOSERVICE;
	}

	excl = is_exclusive_res(res);
	pthread_rwlock_unlock(&resource_lock);
	if (exclusive || (count && excl))
		return RG_YES;

	return 0;
}


/**
   Find the best target node for a service *besides* the current service
   owner.  Takes into account:

   - Failover domain (ordering / restricted policy)
   - Exclusive service policy
 */
uint32_t
best_target_node(cluster_member_list_t *allowed, uint32_t owner,
		 char *rg_name, int lock)
{
	int x;
	int highscore = 1;
	int score;
	uint32_t highnode = owner, nodeid;
	char *val;
	resource_t *res;
	int exclusive;

	if (lock)
		pthread_rwlock_rdlock(&resource_lock);
	count_resource_groups(allowed);
	if (lock)
		pthread_rwlock_unlock(&resource_lock);

	for (x=0; x < allowed->cml_count; x++) {
		if (!allowed->cml_members[x].cn_member)
			continue;

		nodeid = allowed->cml_members[x].cn_nodeid;

		/* Don't allow trying a restart just yet */
		if (owner != 0 && nodeid == owner)
			continue;
		
		if (lock)
			pthread_rwlock_rdlock(&resource_lock);
		score = node_should_start(nodeid, allowed, rg_name, &_domains);
		if (!score) { /* Illegal -- failover domain constraint */
			if (lock)
				pthread_rwlock_unlock(&resource_lock);
			continue;
		}

		/* Add 2 to score if it's an exclusive service and nodeid
		   isn't running any services currently.  Set score to 0 if
		   it's an exclusive service and the target node already
		   is running a service. */
		res = find_root_by_ref(&_resources, rg_name);
		val = res_attr_value(res, "exclusive");
		exclusive = val && ((!strcmp(val, "yes") || (atoi(val)>0)));

		if (lock)
			pthread_rwlock_unlock(&resource_lock);

		if (exclusive) {

			if (allowed->cml_members[x].cn_svccount > 0) {
				/* Definitely not this guy */
				continue;
			} else {
				score += 2;
			}
		} else if (allowed->cml_members[x].cn_svcexcl) {
			/* This guy has an exclusive resource group.
			   Can't relocate / failover to him. */
			continue;
		}

		if (score < highscore)
			continue;

		highnode = nodeid;
		highscore = score;
	}

	return highnode;
}


int
check_depend(resource_t *res)
{
	char *val;
	rg_state_t rs;

	val = res_attr_value(res, "depend");
	if (!val)
		/* No dependency */
		return -1;

	if (get_rg_state_local(val, &rs) == 0)
		return (rs.rs_state == RG_STATE_STARTED);

	return 1;
}


int
check_depend_safe(char *rg_name)
{
	resource_t *res;
	int ret;

	pthread_rwlock_rdlock(&resource_lock);
	res = find_root_by_ref(&_resources, rg_name);
	if (!res)
		return -1;

	ret = check_depend(res);
	pthread_rwlock_unlock(&resource_lock);

	return ret;
}


int
check_rdomain_crash(char *svcName)
{
	int *nodes = NULL, nodecount;
	int *fd_nodes = NULL, fd_nodecount, fl;
	int *isect = NULL, icount;
	char fd_name[256];

	if (_group_property(svcName, "domain", fd_name, sizeof(fd_name)) != 0)
		goto out_free;

	if (node_domain_set(_domains, fd_name, &fd_nodes,
			    &fd_nodecount, &fl) != 0)
		goto out_free;

	if (!(fl & FOD_RESTRICTED))
		goto out_free;
	
	if (s_intersection(fd_nodes, fd_nodecount, nodes, nodecount, 
		    &isect, &icount) < 0)
		goto out_free;

	if (icount == 0) {
		clulog(LOG_NOTICE, "Marking %s as stopped: "
		       "Restricted domain unavailable\n", svcName);
		rt_enqueue_request(svcName, RG_STOP, NULL, 0, 0,
				   0, 0);
	}

out_free:
	if (fd_nodes)
		free(fd_nodes);
	if (nodes)
		free(nodes);
	if (isect)
		free(isect);

	return 0;
}


/**
  Start or failback a resource group: if it's not running, start it.
  If it is running and we're a better member to run it, then ask for
  it.
 */
void
consider_start(resource_node_t *node, char *svcName, rg_state_t *svcStatus,
	       cluster_member_list_t *membership)
{
	char *val;
	cman_node_t *mp;
	int autostart, exclusive;
	struct dlm_lksb lockp;
	int fod_ret;

	mp = memb_id_to_p(membership, my_id());
	assert(mp);

	/* Service cannot be started if Frozen */
	if (svcStatus->rs_flags & RG_FLAG_FROZEN)
		return;
	/*
	 * Service must be not be running elsewhere to consider for a
	 * local start.
	 */
	if (svcStatus->rs_state == RG_STATE_STARTED &&
	    svcStatus->rs_owner == mp->cn_nodeid)
		return;

	if (svcStatus->rs_state == RG_STATE_DISABLED)
		return;

	/* Stopped, and hasn't been started yet.  See if
	   autostart is disabled.  If it is, leave it stopped */
	if (svcStatus->rs_state == RG_STATE_STOPPED &&
		    svcStatus->rs_transition == 0) {
		val = res_attr_value(node->rn_resource, "autostart");
		autostart = !(val && ((!strcmp(val, "no") ||
				     (atoi(val)==0))));
		if (!autostart) {
			/*
			clulog(LOG_DEBUG,
			       "Skipping RG %s: Autostart disabled\n",
			       svcName);
			 */
			/*
			   Mark non-autostart services as disabled to avoid
			   confusion!
			 */
			if (rg_lock(svcName, &lockp) < 0) {
				clulog(LOG_ERR, "#XX: Unable to obtain cluster "
				       "lock @ %s:%d: %s\n", __FILE__, __LINE__,
				       strerror(errno));
				return;
			}

			if (get_rg_state(svcName, svcStatus) != 0) {
				clulog(LOG_ERR, "#34: Cannot get status "
				       "for service %s\n", svcName);
				rg_unlock(&lockp);
				return;
			}

			if (svcStatus->rs_transition == 0 &&
			    svcStatus->rs_state == RG_STATE_STOPPED) {
				svcStatus->rs_state = RG_STATE_DISABLED;
				set_rg_state(svcName, svcStatus);
			}

			rg_unlock(&lockp);

			return;
		}
	}

	/* See if service this one depends on is running.  If not,
           don't start it */
	if (check_depend(node->rn_resource) == 0) {
		clulog(LOG_DEBUG,
		       "Skipping RG %s: Dependency missing\n", svcName);
		return;
	}

	val = res_attr_value(node->rn_resource, "exclusive");
	exclusive = val && ((!strcmp(val, "yes") || (atoi(val)>0)));

	if (exclusive && mp->cn_svccount) {
		clulog(LOG_DEBUG,
		       "Skipping RG %s: Exclusive and I am running services\n",
		       svcName);
		return;
	}

	/*
	   Don't start other services if I'm running an exclusive
	   service.
	 */
	if (mp->cn_svcexcl) {
		clulog(LOG_DEBUG,
		       "Skipping RG %s: I am running an exclusive service\n",
		       svcName);
		return;
	}

	/*
	 * Start any stopped services, or started services
	 * that are owned by a down node.
	 */
	fod_ret = node_should_start(mp->cn_nodeid, membership,
				    svcName, &_domains);
	if (fod_ret == FOD_BEST)
		rt_enqueue_request(svcName, RG_START, NULL, 0, mp->cn_nodeid,
				   0, 0);
	else if (fod_ret == FOD_ILLEGAL)
		check_rdomain_crash(svcName);
}


void
consider_relocate(char *svcName, rg_state_t *svcStatus, uint32_t nodeid,
		  cluster_member_list_t *membership)
{
	int a, b, req = RG_RELOCATE;

	/*
	   Service must be running locally in order to consider for
	   a relocate
	 */
	if ((svcStatus->rs_state != RG_STATE_STARTING &&
	    svcStatus->rs_state != RG_STATE_STARTED) ||
	    svcStatus->rs_owner != my_id())
		return;

	/*
	 * Send the resource group to a node if it's got a higher prio
	 * to run the resource group.
	 */
#if 0
	if (best_target_node(membership, my_id(), svcName, 0) !=
	    nodeid) {
		return;
	}
#endif
	a = node_should_start(nodeid, membership, svcName, &_domains);
	b = node_should_start(my_id(), membership, svcName, &_domains);

	if (a <= b)
		return;

	if (group_migratory(svcName, 1)) {
		req = RG_MIGRATE;
	}

	clulog(LOG_NOTICE, "%s %s to better node %s\n",
	       req==RG_MIGRATE ? "Migrating":"Relocating",
	       svcName,
	       memb_id_to_name(membership, nodeid));

	rt_enqueue_request(svcName, req, NULL, 0, nodeid, 0, 0);
}


char **
get_service_names(int *len)
{
	resource_node_t *node = NULL;
	int nservices, ncopied = 0, x;
	char **ret = NULL;
	char rg_name[64];

	pthread_rwlock_rdlock(&resource_lock);

	nservices = 0;
	list_do(&_tree, node) {
		++nservices;
	} while (!list_done(&_tree, node));
	
	ret = malloc(sizeof(char *) * (nservices + 1));
	if (!ret)
		goto out_fail;

	memset(ret, 0, sizeof(char *) * (nservices + 1));
	nservices = 0;
	list_for(&_tree, node, nservices) {
		res_build_name(rg_name, sizeof(rg_name),
			       node->rn_resource);

		if (!strlen(rg_name))
			continue;

		ret[ncopied] = strdup(rg_name);
		if (ret[ncopied]) {
			ncopied++;
		} else {
			goto out_fail;
		}
	}

	if (len)
		*len = ncopied;
	pthread_rwlock_unlock(&resource_lock);
	return ret;

out_fail:
	pthread_rwlock_unlock(&resource_lock);
	for (x = 0; x < ncopied; x++)
		free(ret[x]);
	if (ret)
		free(ret);
	return NULL;
}





/**
 * Called to decide what services to start locally during a node_event.
 * Originally a part of node_event, it is now its own function to cut down
 * on the length of node_event.
 * 
 * @see			node_event
 */
int
eval_groups(int local, uint32_t nodeid, int nodeStatus)
{
	struct dlm_lksb lockp;
	char svcName[64], *nodeName;
	resource_node_t *node;
	rg_state_t svcStatus;
	cluster_member_list_t *membership;
	int ret;

	if (rg_locked()) {
		clulog(LOG_DEBUG,
			"Resource groups locked; not evaluating\n");
		return -EAGAIN;
	}

	membership = member_list();

	pthread_rwlock_rdlock(&resource_lock);

	/* Requires read lock */
	count_resource_groups(membership);

	list_do(&_tree, node) {

		res_build_name(svcName, sizeof(svcName), node->rn_resource);

		/*
		 * Lock the service information and get the current service
		 * status.
		 */
		if ((ret = rg_lock(svcName, &lockp)) < 0) {
			clulog(LOG_ERR,
			       "#33: Unable to obtain cluster lock: %s\n",
			       strerror(-ret));
			pthread_rwlock_unlock(&resource_lock);
			free_member_list(membership);
			return ret;
		}
		
		if (get_rg_state(svcName, &svcStatus) != 0) {
			clulog(LOG_ERR,
			       "#34: Cannot get status for service %s\n",
			       svcName);
			rg_unlock(&lockp);
			continue;
		}

		rg_unlock(&lockp);

		if (svcStatus.rs_owner == 0)
			nodeName = "none";
		else
			nodeName = memb_id_to_name(membership,
						   svcStatus.rs_owner);

		/* Disabled/failed/in recovery?  Do nothing */
		if ((svcStatus.rs_state == RG_STATE_DISABLED) ||
		    (svcStatus.rs_state == RG_STATE_FAILED) ||
		    (svcStatus.rs_state == RG_STATE_RECOVER)) {
			continue;
		}

		clulog(LOG_DEBUG, "Evaluating RG %s, state %s, owner "
		       "%s\n", svcName,
		       rg_state_str(svcStatus.rs_state),
		       nodeName);

		if ((local && nodeStatus) ||
		    svcStatus.rs_state == RG_STATE_STOPPED) {

			consider_start(node, svcName, &svcStatus, membership);

		} else if (!local && !nodeStatus) {

			/*
			 * Start any stopped services, or started services
			 * that are owned by a down node.
			 */
			consider_start(node, svcName, &svcStatus, membership);

			/*
			 * TODO
			 * Mark a service as 'stopped' if no members in its
			 * restricted fail-over domain are running.
			 */
		} else {
			/* Send to the node if that ndoe is a better
			   owner for this service */
			consider_relocate(svcName, &svcStatus, nodeid,
					  membership);
		}

	} while (!list_done(&_tree, node));

	pthread_rwlock_unlock(&resource_lock);
	free_member_list(membership);

	clulog(LOG_DEBUG, "Event (%d:%d:%d) Processed\n", local,
	       (int)nodeid, nodeStatus);

	return 0;
}


/**
 * Called to decide what services to start locally after a service event.
 * 
 * @see			eval_groups
 */
int
group_event(char *rg_name, uint32_t state, int owner)
{
	char svcName[64], *nodeName;
	resource_node_t *node;
	rg_state_t svcStatus;
	cluster_member_list_t *membership;
	int depend;

	if (rg_locked()) {
		clulog(LOG_DEBUG,
			"Resource groups locked; not evaluating\n");
		return -EAGAIN;
	}

	membership = member_list();
	if (!membership)
		return -1;

	pthread_rwlock_rdlock(&resource_lock);

	/* Requires read lock */
	count_resource_groups(membership);

	list_do(&_tree, node) {

		res_build_name(svcName, sizeof(svcName), node->rn_resource);

		if (get_rg_state_local(svcName, &svcStatus) != 0)
			continue;

		if (svcStatus.rs_owner == 0)
			nodeName = "none";
		else
			nodeName = memb_id_to_name(membership,
						   svcStatus.rs_owner);

		/* Disabled/failed/in recovery?  Do nothing */
		if ((svcStatus.rs_state == RG_STATE_DISABLED) ||
		    (svcStatus.rs_state == RG_STATE_FAILED) ||
		    (svcStatus.rs_state == RG_STATE_RECOVER)) {
			continue;
		}

		depend = check_depend(node->rn_resource);

		/* Skip if we have no dependency */
		if (depend == -1)
			continue;

		/*
		   If we have:
		   (a) a met dependency
		   (b) we're in the STOPPED state, and
		   (c) our new service event is a started service

		   Then see if we should start this other service as well.
		 */
		if (depend == 1 &&
		    svcStatus.rs_state == RG_STATE_STOPPED &&
		    state == RG_STATE_STARTED) {

			clulog(LOG_DEBUG, "Evaluating RG %s, state %s, owner "
			       "%s\n", svcName,
			       rg_state_str(svcStatus.rs_state),
			       nodeName);
			consider_start(node, svcName, &svcStatus, membership);
			continue;
		}
		
		/*
		   If we lost a dependency for this service and it's running
		   locally, stop it.
		 */
		if (depend == 0 &&
		    svcStatus.rs_state == RG_STATE_STARTED &&
		    svcStatus.rs_owner == my_id()) {

			clulog(LOG_WARNING, "Stopping service %s: Dependency missing\n",
			       svcName);
			rt_enqueue_request(svcName, RG_STOP, NULL, 0, my_id(),
					   0, 0);
		}

	} while (!list_done(&_tree, node));

	pthread_rwlock_unlock(&resource_lock);
	free_member_list(membership);

	return 0;
}


/**
  Tells us if a resource group can be migrated.
 */
int
group_migratory(char *groupname, int lock)
{
	resource_node_t *rn;
	resource_t *res;
	int migrate = 0, x, ret = 0;

	if (lock)
		pthread_rwlock_rdlock(&resource_lock);

	res = find_root_by_ref(&_resources, groupname);
	if (!res) {
		/* Nonexistent or non-TL RG cannot be migrated */
		return 0;
	}

	for (x = 0; res->r_rule->rr_actions[x].ra_name; x++) {
		if (!strcmp(res->r_rule->rr_actions[x].ra_name,
		    "migrate")) {
			migrate = 1;
			break;
		}
	}

	if (!migrate)
		goto out_unlock;

	list_do(&_tree, rn) {
		if (rn->rn_resource == res && rn->rn_child) {
			/* TL service w/ children cannot be migrated */
			goto out_unlock;
		}
	} while (!list_done(&_tree, rn));


	/* Ok, we have a migrate option to the resource group,
	   the resource group has no children, and the resource
	   group exists.  We're all good */
	ret = 1;

out_unlock:
	if (lock)
		pthread_rwlock_unlock(&resource_lock);
	return ret;
}



/**
   Perform an operation on a resource group.  That is, walk down the
   tree for that resource group, performing the given operation on
   all children in the necessary order.

   XXX Needs to handle more return codes to be more OCF compliant

   @param groupname	Resource group to operate on
   @param op		Operation to perform
   @return 		0 on success, 1 on failure/error.
 */
int
group_op(char *groupname, int op)
{
	resource_t *res;
	int ret = -1;

	pthread_rwlock_rdlock(&resource_lock);
	/* XXX get group from somewhere else */
	res = find_root_by_ref(&_resources, groupname);
	if (!res) {
		pthread_rwlock_unlock(&resource_lock);
		return -1;
	}

	switch (op) {
	case RG_START:
		ret = res_start(&_tree, res, NULL);
		break;
	case RG_STOP:
		ret = res_stop(&_tree, res, NULL);
		break;
	case RG_STATUS:
		ret = res_status(&_tree, res, NULL);
		break;
	case RG_CONDSTOP:
		ret = res_condstop(&_tree, res, NULL);
		break;
	case RG_CONDSTART:
		ret = res_condstart(&_tree, res, NULL);
		break;
	}
	pthread_rwlock_unlock(&resource_lock);

#if 0
	/*
	   Do NOT return error codes if we failed to stop for one of these
	   reasons.  It didn't start, either, so it's safe to assume that
	   if the program wasn't installed, there's nothing to tear down.
	 */
	if (op == RG_STOP) {
		switch(ret) {
		case OCF_RA_SUCCESS:
		case OCF_RA_NOT_INSTALLED:
		case OCF_RA_NOT_CONFIGURED:
			ret = 0;
			break;
		default:
			ret = 1;
			break;
		}
	}
#endif

	return ret;
}


int
group_migrate(char *groupname, int target)
{
	resource_node_t *rn = NULL, *tmp;
	resource_t *res;
	char *tgt_name;
	int ret = RG_ENOSERVICE;
	cluster_member_list_t *membership;

	if (target <= 0) {
		clulog(LOG_WARNING,
		       "Illegal node ID %d during migrate operation\n",
		       target);
		return RG_EINVAL;
	}

	membership = member_list();
	if (!membership) {
		clulog(LOG_ERR, "Unable to determine membership during "
		       "migrate operation\n");
		return RG_EFAIL;
	}

	pthread_rwlock_rdlock(&resource_lock);
	
	tgt_name = memb_id_to_name(membership, target);
	if (!tgt_name) {
		clulog(LOG_WARNING, "Node ID %d not in membership during "
		       "migrate operation\n", target);
		ret = RG_EINVAL;
		goto out;
	}

	res = find_root_by_ref(&_resources, groupname);
	if (!res) {
		clulog(LOG_WARNING,
		       "Unable to find '%s' in resource list during"
		       "migrate operation\n", groupname);
		goto out;
	}

	list_do(&_tree, tmp) {
		if (tmp->rn_resource == res) {
			rn = tmp;
			break;
		}
	} while (!list_done(&_tree, tmp));

	if (!rn) {
		clulog(LOG_WARNING,
		       "Unable to find '%s' it top level of resource "
		       "tree during migrate operation\n", groupname);
		goto out;
	}
	
	clulog(LOG_NOTICE, "Migrating %s to %s\n", groupname, tgt_name);
	ret = res_exec(rn, RS_MIGRATE, tgt_name, 0);
	if (ret == 0) {
		clulog(LOG_NOTICE,
		       "Migration of %s to %s completed\n",
		       groupname, tgt_name);
	} else {
		clulog(LOG_ERR, 
		       "Migration of %s to %s failed; return code %d\n",
		       groupname, tgt_name, ret);
	}

out:
	pthread_rwlock_unlock(&resource_lock);
	free_member_list(membership);
	return ret;
}


/**
   Gets an attribute of a resource group.

   @param groupname	Name of group
   @param property	Name of property to check for
   @param ret		Preallocated return buffer
   @param len		Length of buffer pointed to by ret
   @return		0 on success, -1 on failure.
 */
int
_group_property(char *groupname, char *property, char *ret, size_t len)
{
	resource_t *res = NULL;
	int x = 0;

	res = find_root_by_ref(&_resources, groupname);
	if (!res) {
		return -1;
	}

	for (; res->r_attrs[x].ra_name; x++) {
		if (strcasecmp(res->r_attrs[x].ra_name, property))
			continue;
		strncpy(ret, res->r_attrs[x].ra_value, len);
		return 0;
	}

	return -1;
}


int
group_property(char *groupname, char *property, char *ret_val, size_t len)
{
	int ret = -1;
	pthread_rwlock_rdlock(&resource_lock);
	ret = _group_property(groupname, property, ret_val, len);
	pthread_rwlock_unlock(&resource_lock);
	return ret;
}
	

/**
  Send the state of a resource group to a given file descriptor.

  @param fd		File descriptor to send state to
  @param rgname		Resource group name whose state we want to send.
  @see send_rg_states
 */
void
send_rg_state(msgctx_t *ctx, char *rgname, int fast)
{
	rg_state_msg_t msg, *msgp = &msg;
	struct dlm_lksb lockp;

	msgp->rsm_hdr.gh_magic = GENERIC_HDR_MAGIC;
	msgp->rsm_hdr.gh_length = sizeof(msg);
	msgp->rsm_hdr.gh_command = RG_STATUS;

	/* try fast read -- only if it fails and fast is not 
	   specified should we do the full locked read */
	if (get_rg_state_local(rgname, &msgp->rsm_state) != 0 &&
	    !fast) {
		if (rg_lock(rgname, &lockp) < 0)
			return;
		if (get_rg_state(rgname, &msgp->rsm_state) < 0) {
			rg_unlock(&lockp);
			return;
		}
		rg_unlock(&lockp);
	}

	swab_rg_state_msg_t(msgp);

	if (msg_send(ctx, msgp, sizeof(msg)) < 0)
		perror("msg_send");
}


/**
  Send status from a thread because we don't want rgmanager's
  main thread to block in the case of DLM issues
 */
static void *
status_check_thread(void *arg)
{
	msgctx_t *ctx = ((struct status_arg *)arg)->ctx;
	int fast = ((struct status_arg *)arg)->fast;
	resource_node_t *node;
	generic_msg_hdr hdr;
	char rg[64];

	free(arg);

	if (central_events_enabled()) {
		/* Never call get_rg_state() (distributed) if 
		   central events are enabled, otherwise we
		   might overwrite the rg state with 'stopped' 
		   when it should be 'disabled' (e.g. autostart="0") */
		fast = 1;
	}

	/* See if we have a slot... */
	if (rg_inc_status() < 0) {
		/* Too many outstanding status checks.  try again later. */
		msg_send_simple(ctx, RG_FAIL, RG_EAGAIN, 0);
		msg_close(ctx);
		msg_free_ctx(ctx);
		pthread_exit(NULL);
	}
	
	/*send_master_state(ctx);*/

	pthread_rwlock_rdlock(&resource_lock);

	list_do(&_tree, node) {

		res_build_name(rg, sizeof(rg), node->rn_resource);
		send_rg_state(ctx, rg, fast);
	} while (!list_done(&_tree, node));

	pthread_rwlock_unlock(&resource_lock);

	msg_send_simple(ctx, RG_SUCCESS, 0, 0);

	/* XXX wait for client to tell us it's done; I don't know why
	   this is needed when doing fast I/O, but it is. */
	msg_receive(ctx, &hdr, sizeof(hdr), 10);
	msg_close(ctx);
	msg_free_ctx(ctx);
	
	rg_dec_status();

	pthread_exit(NULL);
}


/**
  Send all resource group states to a file descriptor

  @param fd		File descriptor to send states to.
  @return		0
 */
int
send_rg_states(msgctx_t *ctx, int fast)
{
	struct status_arg *arg;
	pthread_t newthread;
	pthread_attr_t attrs;

	arg = malloc(sizeof(struct status_arg));
	if (!arg) {
		msg_send_simple(ctx, RG_FAIL, 0, 0);
		return -1;
	}

	arg->ctx = ctx;
	arg->fast = fast;

        pthread_attr_init(&attrs);
        pthread_attr_setinheritsched(&attrs, PTHREAD_INHERIT_SCHED);
        pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);
	pthread_attr_setstacksize(&attrs, 65535);

	pthread_create(&newthread, &attrs, status_check_thread, arg);
        pthread_attr_destroy(&attrs);

	return 0;
}


int
svc_exists(char *svcname)
{
	resource_node_t *node;
	int ret = 0;
	char rg[64];

	pthread_rwlock_rdlock(&resource_lock);

	list_do(&_tree, node) {
		res_build_name(rg, sizeof(rg), node->rn_resource);

		if (strcmp(rg, svcname) == 0) {
			ret = 1;
			break;
		}
	} while (!list_done(&_tree, node));

	pthread_rwlock_unlock(&resource_lock);

	return ret;
}


void
rg_doall(int request, int block, char *debugfmt)
{
	resource_node_t *curr;
	rg_state_t svcblk;
	char rg[64];

	pthread_rwlock_rdlock(&resource_lock);
	list_do(&_tree, curr) {

		/* Group name */
		res_build_name(rg, sizeof(rg), curr->rn_resource);

		if (debugfmt)
			clulog(LOG_DEBUG, debugfmt, rg);

		/* Optimization: Don't bother even queueing the request
		   during the exit case if we don't own it */
		if (request == RG_STOP_EXITING) {
			if (get_rg_state_local(rg, &svcblk) < 0)
				continue;

			/* Always run stop if we're the owner, regardless
			   of state; otherwise, don't run stop */
			if (svcblk.rs_owner != my_id())
				continue;
		}

		rt_enqueue_request(rg, request, NULL, 0,
				   0, 0, 0);
	} while (!list_done(&_tree, curr));

	pthread_rwlock_unlock(&resource_lock);

	/* XXX during shutdown, if we're doing a simultaenous shutdown,
	   this will cause this rgmanager to hang waiting for all the
	   other rgmanagers to complete. */
	if (block) 
		rg_wait_threads();
}


/**
  Stop changed resources.
 */
void *
q_status_checks(void __attribute__ ((unused)) *arg)
{
	resource_node_t *curr;
	rg_state_t svcblk;
	char rg[64];
	struct dlm_lksb lockp;
	
	/* Only one status thread at a time, please! */
	if (pthread_mutex_trylock(&status_mutex) != 0)
		pthread_exit(NULL);

	pthread_rwlock_rdlock(&resource_lock);
	list_do(&_tree, curr) {

		/* Group name */
		res_build_name(rg, sizeof(rg), curr->rn_resource);

		/* Local check - no one will make us take a service */
		if (get_rg_state_local(rg, &svcblk) < 0) {
			if (rg_lock(rg, &lockp) != 0)
				continue;
			if (get_rg_state(rg, &svcblk) < 0) {
				rg_unlock(&lockp);
				continue;
			}
			rg_unlock(&lockp);
		}

		if (svcblk.rs_owner != my_id() ||
		    (svcblk.rs_state != RG_STATE_STARTED &&
		     svcblk.rs_state != RG_STATE_MIGRATE))
			continue;

		rt_enqueue_request(rg, RG_STATUS,
				   NULL, 1, 0, 0, 0);

	} while (!list_done(&_tree, curr));

	pthread_rwlock_unlock(&resource_lock);
	pthread_mutex_unlock(&status_mutex);

	pthread_exit(NULL);
}


void
do_status_checks(void)
{
	pthread_attr_t attrs;
	pthread_t newthread;

        pthread_attr_init(&attrs);
        pthread_attr_setinheritsched(&attrs, PTHREAD_INHERIT_SCHED);
        pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);
	pthread_attr_setstacksize(&attrs, 65535);

	pthread_create(&newthread, &attrs, q_status_checks, NULL);
        pthread_attr_destroy(&attrs);
}


/**
  Stop changed resources.
 */
void
do_condstops(void)
{
	resource_node_t *curr;
	struct dlm_lksb lockp;
	rg_state_t svcblk;
	int need_kill;
	char rg[64];

	clulog(LOG_INFO, "Stopping changed resources.\n");

	pthread_rwlock_rdlock(&resource_lock);
	list_do(&_tree, curr) {

		/* Group name */
		res_build_name(rg, sizeof(rg), curr->rn_resource);

		/* If we're not running it, no need to CONDSTOP */
		if (get_rg_state_local(rg, &svcblk) < 0) {
			continue;
		}

		if (svcblk.rs_owner != my_id())
			continue;

		/* Set state to uninitialized if we're killing a RG */
		need_kill = 0;
		if (curr->rn_resource->r_flags & RF_NEEDSTOP) {
			need_kill = 1;
			clulog(LOG_DEBUG, "Removing %s\n", rg);
		}

		if (!curr->rn_child && ((curr->rn_resource->r_rule->rr_flags &
		    RF_DESTROY) == 0) && group_migratory(rg, 0) &&
		    need_kill == 1) {
			/* Do something smart here: flip state? */
			clulog(LOG_NOTICE,
			       "%s removed from the config, but I am not stopping it.\n",
			       rg);
			if (rg_lock(rg, &lockp) != 0)
				continue;
			if (get_rg_state(rg, &svcblk) < 0)
		       		goto cont;
			svcblk.rs_state = RG_STATE_DISABLED;
			set_rg_state(rg, &svcblk);
cont:
			rg_unlock(&lockp);
			continue;
		}

		rt_enqueue_request(rg, need_kill ? RG_DISABLE : RG_CONDSTOP,
				   NULL, 0, 0, 0, 0);

	} while (!list_done(&_tree, curr));

	pthread_rwlock_unlock(&resource_lock);
	rg_wait_threads();
}


/**
  Start changed resources.
 */
void
do_condstarts(void)
{
	resource_node_t *curr;
	char rg[64], *val;
	rg_state_t svcblk;
	int need_init, new_groups = 0, autostart;
	struct dlm_lksb lockp;

	clulog(LOG_INFO, "Starting changed resources.\n");

	/* Pass 1: Start any normally changed resources */
	pthread_rwlock_rdlock(&resource_lock);
	list_do(&_tree, curr) {

		/* Group name */
		res_build_name(rg, sizeof(rg), curr->rn_resource);

		/* New RG.  We'll need to initialize it. */
		need_init = 0;
		if (!(curr->rn_resource->r_flags & RF_RECONFIG) &&
		    (curr->rn_resource->r_flags & RF_NEEDSTART))
			need_init = 1;

		if (!need_init) {
			if (get_rg_state_local(rg, &svcblk) < 0)
				continue;
		} else {
			if (rg_lock(rg, &lockp) != 0)
				continue;

			if (get_rg_state(rg, &svcblk) < 0) {
				rg_unlock(&lockp);
				continue;
			}

			rg_unlock(&lockp);
		}

		if (!need_init && svcblk.rs_owner != my_id())
			continue;

		if (need_init) {
			++new_groups;
			clulog(LOG_NOTICE, "Initializing %s\n", rg);
		}

		if (!curr->rn_child && ((curr->rn_resource->r_rule->rr_flags &
		    RF_INIT) == 0) && group_migratory(rg, 0) &&
		    need_init == 1) {
			/* Do something smart here? */
			clulog(LOG_NOTICE,
			       "%s was added to the config, but I am not initializing it.\n",
			       rg);
			continue;
		}

		rt_enqueue_request(rg, need_init ? RG_INIT : RG_CONDSTART,
				   NULL, 0, 0, 0, 0);

	} while (!list_done(&_tree, curr));

	pthread_rwlock_unlock(&resource_lock);
	rg_wait_threads();

	if (!new_groups)
		return;

	/* Pass 2: Tag all new resource groups as stopped */
	pthread_rwlock_rdlock(&resource_lock);
	list_do(&_tree, curr) {

		/* Group name */
		res_build_name(rg, sizeof(rg), curr->rn_resource);

		/* New RG.  We'll need to initialize it. */
		if (!(curr->rn_resource->r_flags & RF_NEEDSTART))
			continue;

		if (rg_lock(rg, &lockp) != 0)
			continue;

		if (get_rg_state(rg, &svcblk) < 0) {
			rg_unlock(&lockp);
			continue;
		}

		/* If it is a replacement of an old RG, it will
		   be in the DISABLED state, which will prevent it
		   from restarting.  That's bad.  However, if it's
		   a truly new service, it will be in the UNINITIALIZED
		   state, which will be caught by eval_groups. */
		if (svcblk.rs_state != RG_STATE_DISABLED) {
			rg_unlock(&lockp);
			continue;
		}

		/* Set it up for an auto-start */
		val = res_attr_value(curr->rn_resource, "autostart");
		autostart = !(val && ((!strcmp(val, "no") ||
				     (atoi(val)==0))));
		if (autostart)
			svcblk.rs_state = RG_STATE_STOPPED;
		else
			svcblk.rs_state = RG_STATE_DISABLED;

		set_rg_state(rg, &svcblk);

		rg_unlock(&lockp);

	} while (!list_done(&_tree, curr));
	pthread_rwlock_unlock(&resource_lock);

	/* Pass 3: See if we should start new resource groups */
	eval_groups(1, my_id(), 1);
}


int
check_config_update(int *new, int *old)
{
	int newver = 0, fd, ret = 0;
	char *val = NULL;

       	fd = ccs_lock();
	if (fd == -1) {
		return 0;
	}

	if (ccs_get(fd, "/cluster/@config_version", &val) == 0) {
		newver = atoi(val);
	}

	if (val)
		free(val);

	pthread_mutex_lock(&config_mutex);
	if (newver && newver != config_version)
		ret = 1;
	if (new) *new = newver;
	if (old) *old = config_version;
	pthread_mutex_unlock(&config_mutex);
	ccs_unlock(fd);

	return ret;
}


void
dump_config_version(FILE *fp)
{
	fprintf(fp, "Cluster configuration version %d\n\n", config_version);
}


/**
  Initialize resource groups.  This reads all the resource groups from 
  CCS, builds the tree, etc.  Ideally, we'll have a similar function 
  performing deltas on the two trees so that we can fully support online
  resource group modification.
 */
int
init_resource_groups(int reconfigure)
{
	int fd, x, y, cnt;

	event_table_t *evt = NULL;
	resource_t *reslist = NULL, *res;
	resource_rule_t *rulelist = NULL, *rule;
	resource_node_t *tree = NULL;
	fod_t *domains = NULL, *fod;
	event_t *evp;
	char *val;

	if (reconfigure)
		clulog(LOG_NOTICE, "Reconfiguring\n");
	clulog(LOG_INFO, "Loading Service Data\n");
	clulog(LOG_DEBUG, "Loading Resource Rules\n");
	if (load_resource_rules(RESOURCE_ROOTDIR, &rulelist) != 0) {
		return -1;
	}
	x = 0;
	list_do(&rulelist, rule) { ++x; } while (!list_done(&rulelist, rule));
	clulog(LOG_DEBUG, "%d rules loaded\n", x);

       	fd = ccs_lock();
	if (fd == -1) {
		clulog(LOG_CRIT, "#5: Couldn't connect to ccsd!\n");
		return -1;
	}

	if (ccs_get(fd, "/cluster/@config_version", &val) == 0) {
		pthread_mutex_lock(&config_mutex);
		config_version = atoi(val);
		pthread_mutex_unlock(&config_mutex);
		free(val);
	}
	
	if (ccs_get(fd, "/cluster/rm/@statusmax", &val) == 0) {
		if (strlen(val)) 
			rg_set_statusmax(atoi(val));
		free(val);
	}

	clulog(LOG_DEBUG, "Building Resource Trees\n");
	/* About to update the entire resource tree... */
	if (load_resources(fd, &reslist, &rulelist) != 0) {
		clulog(LOG_CRIT, "#6: Error loading services\n");
		destroy_resources(&reslist);
		destroy_resource_rules(&rulelist);
		ccs_unlock(fd);
		return -1;
	}

	if (build_resource_tree(fd, &tree, &rulelist, &reslist) != 0) {
		clulog(LOG_CRIT, "#7: Error building resource tree\n");
		destroy_resource_tree(&tree);
		destroy_resources(&reslist);
		destroy_resource_rules(&rulelist);
		ccs_unlock(fd);
		return -1;
	}

	x = 0;
	list_do(&reslist, res) { ++x; } while (!list_done(&reslist, res));
	clulog(LOG_DEBUG, "%d resources defined\n", x);

	clulog(LOG_DEBUG, "Loading Failover Domains\n");
	construct_domains(fd, &domains);
	x = 0;
	list_do(&domains, fod) { ++x; } while (!list_done(&domains, fod));
	clulog(LOG_DEBUG, "%d domains defined\n", x);
	construct_events(fd, &evt);
	cnt = 0;
	if (evt) {
		for (x=0; x <= evt->max_prio; x++) {
			if (!evt->entries[x])
				continue;
			
			y = 0;

			list_do(&evt->entries[x], evp) {
				++y;
			} while (!list_done(&evt->entries[x], evp));

			cnt += y;
		}
	}
	clulog(LOG_DEBUG, "%d events defined\n", x);
	

	/* Reconfiguration done */
	ccs_unlock(fd);

	if (reconfigure) {
		/* Calc tree deltas */
		pthread_rwlock_wrlock(&resource_lock);
		resource_delta(&_resources, &reslist);
		resource_tree_delta(&_tree, &tree);
		pthread_rwlock_unlock(&resource_lock);

		do_condstops();
	}

	/* Swap in the new configuration */
	pthread_rwlock_wrlock(&resource_lock);
	if (_tree)
		destroy_resource_tree(&_tree);
	_tree = tree;
	if (_resources)
		destroy_resources(&_resources);
	_resources = reslist;
	if (_rules)
		destroy_resource_rules(&_rules);
	_rules = rulelist;
	if (_domains)
		deconstruct_domains(&_domains);
	_domains = domains;
	if (master_event_table)
		deconstruct_events(&master_event_table);
	master_event_table = evt;
	pthread_rwlock_unlock(&resource_lock);

	if (reconfigure) {
		/* Switch to read lock and do the up-half of the
		   reconfig request */
		clulog(LOG_INFO, "Restarting changed resources.\n");
		do_condstarts();
	} else {
		/* Do initial stop-before-start */
		clulog(LOG_INFO, "Initializing Services\n");
		rg_doall(RG_INIT, 1, "Initializing %s\n");
		clulog(LOG_INFO, "Services Initialized\n");
		rg_set_initialized();
	}

	return 0;
}


void
get_recovery_policy(char *rg_name, char *buf, size_t buflen)
{
	resource_t *res;
	char *val;

	pthread_rwlock_rdlock(&resource_lock);

	strncpy(buf, "restart", buflen);
	res = find_root_by_ref(&_resources, rg_name);
	if (res) {
		val = res_attr_value(res, "recovery");
		if (val) {
			strncpy(buf, val, buflen);
		}
	}

	pthread_rwlock_unlock(&resource_lock);
}


int
get_service_property(char *rg_name, char *prop, char *buf, size_t buflen)
{
	int ret = 0;
	resource_t *res;
	char *val;

	memset(buf, 0, buflen);

#if 0
	if (!strcmp(prop, "domain")) {
		/* not needed */
		strncpy(buf, "", buflen);
	} else if (!strcmp(prop, "autostart")) {
		strncpy(buf, "1", buflen);
	} else if (!strcmp(prop, "hardrecovery")) {
		strncpy(buf, "0", buflen);
	} else if (!strcmp(prop, "exclusive")) {
		strncpy(buf, "0", buflen);
	} else if (!strcmp(prop, "nfslock")) {
		strncpy(buf, "0", buflen);
	} else if (!strcmp(prop, "recovery")) {
		strncpy(buf, "restart", buflen);
	} else if (!strcmp(prop, "depend")) {
		/* not needed */
		strncpy(buf, "", buflen);
	} else {
		/* not found / no defaults */
		ret = -1;
	}
#endif

	pthread_rwlock_rdlock(&resource_lock);
	res = find_root_by_ref(&_resources, rg_name);
	if (res) {
		val = res_attr_value(res, prop);
		if (val) {
			ret = 0;
			strncpy(buf, val, buflen);
		}
	}
	pthread_rwlock_unlock(&resource_lock);

#if 0
	if (ret == 0)
		printf("%s(%s, %s) = %s\n", __FUNCTION__, rg_name, prop, buf);
	else 
		printf("%s(%s, %s) = NOT FOUND\n", __FUNCTION__, rg_name, prop);
#endif

	return ret;
}


int
check_restart(char *rg_name)
{
	resource_node_t *node;
	int ret = 1;

	pthread_rwlock_rdlock(&resource_lock);
	node = node_by_ref(&_tree, rg_name);
	if (node) {
		ret = restart_add(node->rn_restart_counter);
		if (ret) {
			/* Clear it out - caller is about 
			   to relocate the service anyway */
			restart_clear(node->rn_restart_counter);
		}
	}
	pthread_rwlock_unlock(&resource_lock);

	return ret;
}


void
kill_resource_groups(void)
{
	pthread_rwlock_wrlock(&resource_lock);

	destroy_resource_tree(&_tree);
	destroy_resources(&_resources);
	destroy_resource_rules(&_rules);
	deconstruct_domains(&_domains);

	pthread_rwlock_unlock(&resource_lock);
}
