/*
  Copyright Red Hat, Inc. 2002-2003
  Copyright Mission Critical Linux, Inc. 2000

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
#include <platform.h>
#include <magma.h>
#include <magmamsg.h>
#include <resgroup.h>
#include <vf.h>
#include <magma.h>
#include <ccs.h>
#include <clulog.h>
#include <list.h>
#include <reslist.h>


static resource_t *_resources = NULL;
static resource_rule_t *_rules = NULL;
static resource_node_t *_tree = NULL;
static fod_t *_domains = NULL;

pthread_rwlock_t resource_lock = PTHREAD_RWLOCK_INITIALIZER;


/**
   See if a given node ID should start a resource, given cluster membership

   @see node_should_start
 */
int
node_should_start_safe(uint64_t nodeid, cluster_member_list_t *membership,
		       char *rg_name)
{
	int ret;

	pthread_rwlock_rdlock(&resource_lock);
	ret = node_should_start(nodeid, membership, rg_name, &_domains);
	pthread_rwlock_unlock(&resource_lock);

	return ret;
}


/**
 * Called to decide what services to start locally during a node_event.
 * Originally a part of node_event, it is now its own function to cut down
 * on the length of node_event.
 * 
 * @see			node_event
 */
int
eval_groups(int local, uint64_t nodeid, int nodeStatus)
{
	void *lockp = NULL;
	char *svcName, *nodeName;
	resource_node_t *node;
	rg_state_t svcStatus;
	cluster_member_list_t *membership;
	int ret;

	if (rg_locked()) {
		clulog(LOG_NOTICE, "Resource groups locked\n");
		return -EAGAIN;
	}

 	membership = member_list();
	pthread_rwlock_rdlock(&resource_lock);
	list_do(&_tree, node) {

		if (strcmp(node->rn_resource->r_rule->rr_type, "resourcegroup"))
			continue;

		svcName = node->rn_resource->r_attrs->ra_value;

		/*
		 * Lock the service information and get the current service
		 * status.
		 */
		if ((ret = rg_lock(svcName, &lockp)) < 0) {
			clulog(LOG_ERR, "Unable to obtain cluster lock: %s\n",
			       strerror(-ret));
			pthread_rwlock_unlock(&resource_lock);
			cml_free(membership);
			return ret;
		}
		
		if (get_rg_state(svcName, &svcStatus) != 0) {
			clulog(LOG_ERR, "Cannot get status for service %s\n",
			       svcName);
			rg_unlock(svcName, lockp);
			continue;
		}

		rg_unlock(svcName, lockp);

		if (svcStatus.rs_owner == NODE_ID_NONE)
			nodeName = "none";
		else
			nodeName = memb_id_to_name(membership,
						   svcStatus.rs_owner);

		if ((svcStatus.rs_state == RG_STATE_DISABLED) ||
		    (svcStatus.rs_state == RG_STATE_FAILED) ||
		    (svcStatus.rs_state == RG_STATE_RECOVER)) {
			continue;
		}

		if (svcStatus.rs_state == RG_STATE_STARTED &&
		    svcStatus.rs_owner == my_id())
			continue;

		clulog(LOG_DEBUG, "Evaluating RG %s, state %s, owner "
		       "%s\n", svcName,
		       rg_state_str(svcStatus.rs_state),
		       nodeName);

		if (local && (nodeStatus == STATE_UP)) {

			/*
			 * Start any stopped services, or started services
			 * that are owned by a down node.
			 */
			if (node_should_start(my_id(), membership,
					      svcName, &_domains) ==
			    FOD_BEST)
				rt_enqueue_request(svcName, RG_START, -1, 0,
						   my_id(), 0, 0);

		} else if (!local && (nodeStatus == STATE_DOWN)) {

			/*
			 * Start any stopped services, or started services
			 * that are owned by a down node.
			 */
			if (node_should_start(my_id(), membership,
					      svcName, &_domains) ==
			    FOD_BEST)
				rt_enqueue_request(svcName, RG_START, -1, 0,
				  		   my_id(), 0, 0);
#if 0
			else
				check_rdomain_crash(svcID, &svcStatus);
#endif
			/*
			 * TODO
			 * Mark a service as 'stopped' if no members in its
			 * restricted
			 * fail-over domain are running.
			 */
		} else {
			//printf("Do nothing: Non-local node-up\n");
		}

	} while (!list_done(&_tree, node));

	pthread_rwlock_unlock(&resource_lock);
	cml_free(membership);

	return 0;
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
	res = find_resource_by_ref(&_resources, "resourcegroup", groupname);
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
	}
	pthread_rwlock_unlock(&resource_lock);

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
group_property(char *groupname, char *property, char *ret, size_t len)
{
	resource_t *res = NULL;
	int x = 0;

	pthread_rwlock_rdlock(&resource_lock);
	res = find_resource_by_ref(&_resources, "resourcegroup", groupname);
	if (!res) {
		pthread_rwlock_unlock(&resource_lock);
		return -1;
	}

	for (; res->r_attrs[x].ra_name; x++) {
		if (strcasecmp(res->r_attrs[x].ra_name, property))
			continue;
		strncpy(ret, res->r_attrs[x].ra_value, len);
		pthread_rwlock_unlock(&resource_lock);
		return 0;
	}
	pthread_rwlock_unlock(&resource_lock);

	return -1;
}


/**
  Send the state of a resource group to a given file descriptor.

  @param fd		File descriptor to send state to
  @param rgname		Resource group name whose state we want to send.
  @see send_rg_states
 */
void
send_rg_state(int fd, char *rgname)
{
	rg_state_msg_t msg, *msgp = &msg;
	void *lockp;

	msgp->rsm_hdr.gh_magic = GENERIC_HDR_MAGIC;
	msgp->rsm_hdr.gh_length = sizeof(msg);
	msgp->rsm_hdr.gh_command = RG_STATUS;

	if (rg_lock(rgname, &lockp) < 0)
		return;

	if (get_rg_state(rgname, &msgp->rsm_state) < 0) {
		rg_unlock(rgname, lockp);
		return;
	}

	rg_unlock(rgname, lockp);
	swab_rg_state_msg_t(msgp);

	msg_send(fd, msgp, sizeof(msg));
}


/**
  Send all resource group states to a file descriptor

  @param fd		File descriptor to send states to.
  @return		0
 */
int
send_rg_states(int fd)
{
	resource_t *res;

	pthread_rwlock_rdlock(&resource_lock);

	list_do(&_resources, res) {
		if (strcmp(res->r_rule->rr_type, "resourcegroup"))
			continue;

		send_rg_state(fd, res->r_attrs[0].ra_value);
	} while (!list_done(&_resources, res));

	pthread_rwlock_unlock(&resource_lock);

	msg_send_simple(fd, RG_SUCCESS, 0, 0);
	msg_close(fd);

	return 0;
}


/**
  Initialize resource groups.  This reads all the resource groups from 
  CCS, builds the tree, etc.  Ideally, we'll have a similar function 
  performing deltas on the two trees so that we can fully support online
  resource group modification.
 */
int
init_resource_groups(void)
{
	rg_state_t rg;
	int fd, x;

	resource_t *reslist = NULL, *res;
	resource_rule_t *rulelist = NULL, *rule;
	resource_node_t *tree = NULL, *curr;
	fod_t *domains = NULL, *fod;
	char *name;

	clulog(LOG_INFO, "Loading Resource Groups\n");
	clulog(LOG_DEBUG, "Loading Resource Rules\n");
	if (load_resource_rules(&rulelist) != 0) {
		return -1;
	}
	x = 0;
	list_do(&rulelist, rule) { ++x; } while (!list_done(&rulelist, rule));
	clulog(LOG_DEBUG, "%d rules loaded\n", x);

       	fd = ccs_lock();
	if (fd == -1) {
		clulog(LOG_CRIT, "Couldn't connect to ccsd!\n");
		return -1;
	}

	clulog(LOG_DEBUG, "Building Resource Trees\n");
	/* About to update the entire resource tree... */
	if (load_resources(fd, &reslist, &rulelist) != 0) {
		clulog(LOG_CRIT, "Error loading resource groups\n");
		destroy_resources(&reslist);
		destroy_resource_rules(&rulelist);
		ccs_unlock(fd);
		return -1;
	}

	if (build_resource_tree(fd, &tree, &rulelist, &reslist) != 0) {
		clulog(LOG_CRIT, "Error loading resource groups\n");
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


	/* XXX refresh resource groups here? */
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

	pthread_rwlock_unlock(&resource_lock);

	/* Ok, read lock on our trees.  Let's do some funky stuff */
	pthread_rwlock_rdlock(&resource_lock);

	clulog(LOG_INFO, "Initializing Resource Groups\n");
	/* do this for each rg XXX don't do this on a reconfigure though*/
	list_do(&_tree, curr) {

		if (strcmp(curr->rn_resource->r_rule->rr_type, "resourcegroup"))
			continue;

		/* Group name */
		name = curr->rn_resource->r_attrs->ra_value;

		clulog(LOG_DEBUG, "Init. Resource Group \"%s\"\n", name);

		rt_enqueue_request(rg.rs_name, RG_INIT, -1, 0, NODE_ID_NONE,
				   0, 0);
	} while (!list_done(&_tree, curr));

	pthread_rwlock_unlock(&resource_lock);
	ccs_unlock(fd);

	rg_wait_threads();
	clulog(LOG_INFO, "Resource Groups Initialized\n");
	rg_set_initialized();
	return 0;
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
