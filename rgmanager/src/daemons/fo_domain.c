/** @file
 * Fail-over Domain & Preferred Node Ordering Driver.  Ripped right from
 * the clumanager 1.2 code base.
 *
 * April 2006 - Nofailback option added to restrict failover behavior in ordered
 *		+ restricted failover domains by Josef Whiter
 */
#include <string.h>
#include <list.h>
#include <logging.h>
#include <resgroup.h>
#include <restart_counter.h>
#include <reslist.h>
#include <ccs.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <members.h>
#include <sets.h>


//#define DEBUG

#ifdef NO_CCS
#define ccs_get(fd, query, ret) conf_get(query, ret)
#endif

/*
   <failoverdomains>
     <failoverdomain name="foo">
       <failoverdomainnode name="member" priority="1"/>
       <failoverdomainnode name="member2" priority="1"/>
       <failoverdomainnode name="member3" priority="2"/>
     </failoverdomain>
   </failoverdomains>
 */
int group_property(char *, char *, char *, size_t);

fod_node_t *
#ifndef NO_CCS
fod_get_node(int ccsfd, char *base, int idx, fod_t *domain)
#else
fod_get_node(int __attribute__((unused)) ccsfd, char *base, int idx, fod_t *domain)
#endif
{
	fod_node_t *fodn;
	char xpath[256];
	char *ret;

	snprintf(xpath, sizeof(xpath), "%s/failoverdomainnode[%d]/@name",
		 base, idx);
	if (ccs_get(ccsfd, xpath, &ret) != 0)
		return NULL;

	list_do(&domain->fd_nodes, fodn) {
		if (strcasecmp(ret, fodn->fdn_name))
			continue;

		log_printf(LOG_ERR, "#30: Node %s defined multiple times in "
		       "domain %s\n", ret, domain->fd_name);
		free(ret);
		return NULL;
	} while (!list_done(&domain->fd_nodes, fodn));

	fodn = malloc(sizeof(*fodn));
	if (!fodn)
		return NULL;
	memset(fodn, 0, sizeof(*fodn));

	/* Already malloc'd; simply store */
	fodn->fdn_name = ret;
	fodn->fdn_prio = 0;
	
 	snprintf(xpath, sizeof(xpath),
 		 "/cluster/clusternodes/clusternode[@name=\"%s\"]/@nodeid",
 		 ret);
 	if (ccs_get(ccsfd, xpath, &ret) != 0) {
 		log_printf(LOG_WARNING, "Node %s has no nodeid attribute\n",
 		       fodn->fdn_name);
 		fodn->fdn_nodeid = -1;
 	} else {
 		/* 64-bit-ism on rhel4? */
 		fodn->fdn_nodeid = atoi(ret);
 	}
 
 	/* Don't even bother getting priority if we're not ordered (it's set
 	   to 0 above */
 	if (!(domain->fd_flags & FOD_ORDERED))
 		return fodn;
 
	snprintf(xpath, sizeof(xpath), "%s/failoverdomainnode[%d]/@priority",
		 base, idx);
	if (ccs_get(ccsfd, xpath, &ret) != 0)
		return fodn;

	fodn->fdn_prio = atoi(ret);
	if (fodn->fdn_prio > 100 || fodn->fdn_prio <= 0)
		fodn->fdn_prio = 0;
	free(ret);

	return fodn;
}


fod_t *
fod_get_domain(int ccsfd, char *base, int idx, fod_t **domains)
{
	fod_t *fod;
	fod_node_t *fodn;
	char xpath[256];
	char *ret;
	int x = 1;

	snprintf(xpath, sizeof(xpath), "%s/failoverdomain[%d]/@name",
		 base, idx);
	if (ccs_get(ccsfd, xpath, &ret) != 0)
		return NULL;

	list_do(domains, fod) {
		if (strcasecmp(fod->fd_name, ret))
			continue;
		    
		log_printf(LOG_ERR, "#31: Domain %s defined multiple times\n",
		       ret);
		free(ret);
		return NULL;
	} while (!list_done(domains, fod));

	fod = malloc(sizeof(*fod));
	if (!fod)
		return NULL;
	memset(fod, 0, sizeof(*fod));
	fod->fd_name = ret;
	fod->fd_nodes = 0;
	fod->fd_flags = 0;

	snprintf(xpath, sizeof(xpath), "%s/failoverdomain[%d]/@ordered",
		 base, idx);
	if (ccs_get(ccsfd, xpath, &ret) == 0) {
		if (atoi(ret) != 0)
			fod->fd_flags |= FOD_ORDERED;
		free(ret);
	}

	snprintf(xpath, sizeof(xpath), "%s/failoverdomain[%d]/@restricted",
		 base, idx);
	if (ccs_get(ccsfd, xpath, &ret) == 0) {
		if (atoi(ret) != 0)
			fod->fd_flags |= FOD_RESTRICTED;
		free(ret);
	}

	snprintf(xpath, sizeof(xpath), "%s/failoverdomain[%d]/@nofailback",
		 base, idx);
	if (ccs_get(ccsfd, xpath, &ret) == 0) {
		if (atoi(ret) != 0)
			fod->fd_flags |= FOD_NOFAILBACK;
		free(ret);
	}

	snprintf(xpath, sizeof(xpath), "%s/failoverdomain[%d]",
		 base, idx);

	do {
		fodn = fod_get_node(ccsfd, xpath, x++, fod);
		if (fodn) {
			/*
			list_do(&fod->fd_nodes, curr) {
				// insert sorted 
				if (fodn->fdn_prio < curr->fdn_prio) {
					list_insert(&fod->fd_nodes, fodn);
					if (curr == fod->fd_nodes)
						fod->fd_nodes = fodn;
				}
			} while (!list_done(&fod->fd_nodes, curr));
			*/
			list_insert(&fod->fd_nodes, fodn);
		}
	} while (fodn);
	
	return fod;
}


int
construct_domains(int ccsfd, fod_t **domains)
{
	char xpath[256];
	int x = 1;
	fod_t *fod;

	snprintf(xpath, sizeof(xpath),
		 RESOURCE_TREE_ROOT "/failoverdomains");

	do {
		fod = fod_get_domain(ccsfd, xpath, x++, domains);
		if (fod) {
			list_insert(domains, fod);
		}
	} while (fod);

	return 0;
}


fod_t *
fod_find_domain(fod_t **domains, char *name)
{
	fod_t *dom;
	
	list_do(domains, dom) {
		
		if (!strcasecmp(dom->fd_name, name))
			return dom;
	
	} while (!list_done(domains,dom));
	
	return NULL;
}


void
deconstruct_domains(fod_t **domains)
{
	fod_t *domain = NULL;
	fod_node_t *node;

	while ((domain = *domains)) {
		list_remove(domains, domain);
		while ((node = domain->fd_nodes)) {
			list_remove(&domain->fd_nodes, node);
			if (node->fdn_name)
				free(node->fdn_name);
			free(node);
		}

		if (domain->fd_name)
			free(domain->fd_name);
		free(domain);
	}
}


void
print_domains(fod_t **domains)
{
	fod_t *fod;
	fod_node_t *fodn = NULL;
	/*
	int x;
	int *node_set = NULL;
	int node_set_len = 0;
	 */

	list_do(domains, fod) {
		printf("Failover domain: %s\n", fod->fd_name);
		printf("Flags: ");
		if (!fod->fd_flags) {
			printf("none\n");
		} else {
			if (fod->fd_flags & FOD_ORDERED)
				printf("Ordered ");
			if (fod->fd_flags & FOD_RESTRICTED)
				printf("Restricted ");
			if (fod->fd_flags & FOD_NOFAILBACK)
				printf("No Failback");
			printf("\n");
		}

  		list_do(&fod->fd_nodes, fodn) {
			printf("  Node %s (id %d, priority %d)\n",
			       fodn->fdn_name, fodn->fdn_nodeid,
			       fodn->fdn_prio);
 		} while (!list_done(&fod->fd_nodes, fodn));
 		
	} while (!list_done(domains, fod));
}


/**
 * Check to see if a given node is the current preferred node within a domain
 * on which we should start the service...
 * @param nodename		Node/member name.
 * @param domain		Existing domain.
 * @param membership		Current membership mask.
 * @return			0 for No, All domain members offline.
 *				1 for No, 1+ Domain member(s) online.
 *				2 for Yes, Not lowest-ordered, online member.
 *				3 for Yes, Lowest-ordered, online member.
 */
int
node_in_domain(char *nodename, fod_t *domain,
	       cluster_member_list_t *membership)
{
	int online = 0, member_match = 0, preferred = 100, myprio = -1;
	fod_node_t *fodn;

	list_do(&domain->fd_nodes, fodn) {
		/*
		 * We have to check the membership mask here so that
		 * we can decide whether or not 'nodename' is the lowest
		 * ordered node.
		 */
		if (!memb_online(membership,
				 memb_name_to_id(membership, fodn->fdn_name)))
			continue;

		/*
		 * If we get here, we know:
		 * A member of the domain is online somewhere
		 */
		online = 1;
		if (!strcmp(nodename, fodn->fdn_name)) {
			/*
			 * If we get here, we know:
			 * We are a member of the domain.
			 */
			member_match = 1;
			myprio = fodn->fdn_prio;
		}

		if (fodn->fdn_prio < preferred)
			preferred = fodn->fdn_prio;
	} while (!list_done(&domain->fd_nodes, fodn));

	if (!online)
		return 0;

	if (!member_match)
		return 1;

	/* Figure out if we're the in the most-preferred group */
	preferred = (myprio <= preferred);
	if (!preferred)
		return 2;

	return 3;
}


int
node_domain_set(fod_t **domains, char *name, int **ret, int *retlen, int *flags)
{
	int x, i, j;
	int *tmpset;
	int ts_count;
	fod_node_t *fodn;
	fod_t *domain;
	int found = 0;

	list_for(domains, domain, x) {
		if (!strcasecmp(domain->fd_name, name)) {
			found = 1;
			break;
		}
	} // while (!list_done(&_domains, fod));

	if (!found)
		return -1;

	/* Count domain length */
	list_for(&domain->fd_nodes, fodn, x) { }
	
	*retlen = 0;
	*ret = malloc(sizeof(int) * x);
	if (!(*ret))
		return -1;
	tmpset = malloc(sizeof(int) * x);
	if (!(*tmpset))
		return -1;

	*flags = domain->fd_flags;

	if (domain->fd_flags & FOD_ORDERED) {
		for (i = 1; i <= 100; i++) {
			
			ts_count = 0;
			list_for(&domain->fd_nodes, fodn, x) {
				if (fodn->fdn_prio == i) {
					s_add(tmpset, &ts_count,
					      fodn->fdn_nodeid);
				}
			}

			if (!ts_count)
				continue;

			/* Shuffle stuff at this prio level */
			if (ts_count > 1)
				s_shuffle(tmpset, ts_count);
			for (j = 0; j < ts_count; j++)
				s_add(*ret, retlen, tmpset[j]);
		}
	}

	/* Add unprioritized nodes */
	ts_count = 0;
	list_for(&domain->fd_nodes, fodn, x) {
		if (!fodn->fdn_prio) {
			s_add(tmpset, &ts_count,
			      fodn->fdn_nodeid);
		}
	}

	if (!ts_count)
		return 0;

	/* Shuffle stuff at this prio level */
	if (ts_count > 1)
		s_shuffle(tmpset, ts_count);
	for (j = 0; j < ts_count; j++)
		s_add(*ret, retlen, tmpset[j]);

	return 0;
}


/**
 * See if a given nodeid should start a specified service svcid.
 *
 * @param nodeid	The node ID in question.
 * @param membership	Current membership mask.
 * @param rg_name	The resource group name in question.
 * @param domains	List of failover domains.
 * @return		0 on NO, 1 for YES
 */
int
node_should_start(int nodeid, cluster_member_list_t *membership,
		  char *rg_name, fod_t **domains)
{
	char *nodename = NULL;
	char domainname[128];
	int ordered = 0;
	int restricted = 0;
	int nofailback = 0;
	fod_t *fod = NULL;
	int found = 0;
	int owned_by_node = 0, started = 0, no_owner = 0;
#ifndef NO_CCS
	rg_state_t svc_state;
	struct dlm_lksb lockp;
#endif

	/*
	 * Um, if the node isn't online...
	 */
	if (!memb_online(membership, nodeid)) {
#ifdef DEBUG
		log_printf(LOG_DEBUG,"Member #%d is not online -> NO\n", nodeid);
#endif
		return FOD_ILLEGAL;
	}

	nodename = memb_id_to_name(membership, nodeid);

#ifndef NO_CCS /* XXX Testing only */
	if (group_property(rg_name, "domain",
			    domainname, sizeof(domainname))) {
		/*
		 * If no domain is present, then the node in question should
		 * try to start the service.
		 */
#ifdef DEBUG
		log_printf(LOG_DEBUG,
		       "Fail-over Domain for service %d nonexistent\n");
#endif
		return FOD_BEST;
	}
#endif

	/*
	 * Ok, we've got a failover domain associated with the service.
	 * Let's see if the domain actually exists...
	 */
	list_do(domains, fod) {

		if (!strcasecmp(fod->fd_name, domainname)) {
			found = 1;
			break;
		}
	} while (!list_done(domains, fod));

	if (!found) {
		/*
		 * Domain doesn't exist!  Weird...
		 */
		log_printf(LOG_WARNING, "#66: Domain '%s' specified for resource "
		       "group %s nonexistent!\n", domainname, rg_name);
		return FOD_BEST;
	}

	/*
	 * Determine whtehter this domain has failback turned on or not..
	 */
	nofailback = !!(fod->fd_flags & FOD_NOFAILBACK);

	/*
	 * Determine whether this domain is restricted or not...
	 */
	restricted = !!(fod->fd_flags & FOD_RESTRICTED);

	/*
	 * Determine whether this domain is ordered or not...
	 */
	ordered = !!(fod->fd_flags & FOD_ORDERED);

#ifndef NO_CCS
	if(nofailback) {
		if (rg_lock(rg_name, &lockp) != 0) {
			log_printf(LOG_WARNING, "Error getting a lock\n");
			return FOD_BEST;
		}
                
		if (get_rg_state(rg_name, &svc_state) == RG_EFAIL) {
                	/*
			 * Couldn't get the service state, thats odd
			 */
			log_printf(LOG_WARNING, "Problem getting state information for "
			       "%s\n", rg_name);
			rg_unlock(&lockp);
			return FOD_BEST;
		}
		rg_unlock(&lockp);

		/*
		 * Check to see if the service is started and if we are the owner in case of
		 * restricted+owner+no failback
		 */
		if (svc_state.rs_state == RG_STATE_STARTED)
			started = 1;
		if (svc_state.rs_owner == (uint32_t)nodeid)
			owned_by_node = 1;
		if (!memb_online(membership, svc_state.rs_owner))
			no_owner = 1;
	}
#endif

	switch (node_in_domain(nodename, fod, membership)) {
	case 0:
		/*
		 * Node is not a member of the domain and no members of the
		 * domain are online.
		 */
#ifdef DEBUG
		log_printf(LOG_DEBUG, "Member #%d is not a member and no "
		       "members are online\n", nodeid);
#endif
		if (!restricted) {
#ifdef DEBUG
			log_printf(LOG_DEBUG,"Restricted mode off -> BEST\n");
#endif
			return FOD_BEST;
		}
#ifdef DEBUG
		log_printf(LOG_DEBUG,"Restricted mode -> ILLEGAL\n");
#endif
		return FOD_ILLEGAL;
	case 1:
		/* 
		 * Node is not a member of the domain and at least one member
		 * of the domain is online.
		 */
		/* In this case, we can ignore 'restricted' */
#ifdef DEBUG
		log_printf(LOG_DEBUG, "Member #%d is not a member of domain %s "
		       "and a member is online\n", nodeid, domainname);
#endif
		if (!restricted) {
#ifdef DEBUG
			log_printf(LOG_DEBUG,"Restricted mode off -> GOOD\n");
#endif
			return FOD_GOOD;
		}
#ifdef DEBUG
		log_printf(LOG_DEBUG,"Restricted mode -> ILLEGAL\n");
#endif
		return FOD_ILLEGAL;
	case 2:
		/*
		 * Node is a member of the domain, but is not the
		 * lowest-ordered, online member.
		 */
#ifdef DEBUG
		log_printf(LOG_DEBUG, "Member #%d is a member, but is not the "
		       "lowest-ordered\n", nodeid);
#endif
		if (ordered) {
			/*
			 * If we are ordered we want to see if failback is
			 * turned on
			 */
			if (nofailback && started && owned_by_node && !no_owner) {
#ifdef DEBUG
				log_printf(LOG_DEBUG,"Ordered mode and no "
				       "failback -> BEST\n");
#endif
				return FOD_BEST;
			}
#ifdef DEBUG
			log_printf(LOG_DEBUG,"Ordered mode -> BETTER\n");
#endif
			return FOD_BETTER;
		}

#ifdef DEBUG
		log_printf(LOG_DEBUG,"Not using ordered mode -> BEST\n");
#endif
		return FOD_BEST;
	case 3:
		/*
		 * Node is a member of the domain and is the lowest-ordered,
		 * online member.
		 */

		if(nofailback && started && !owned_by_node && !no_owner) {
#ifdef DEBUG
			log_printf(LOG_DEBUG, "Member #%d is the lowest-ordered "
			       "memeber of the domain, but is not the owner "
			       "-> BETTER\n", nodeid);
#endif
			return FOD_BETTER;
		}
 
		/* In this case, we can ignore 'ordered' */
#ifdef DEBUG
		log_printf(LOG_DEBUG, "Member #%d is the lowest-ordered member "
		       "of the domain -> BEST\n", nodeid);
#endif
		return FOD_BEST;
	default:
		/* Do what? */
		log_printf(LOG_ERR, "#32: Code path error: "
		       "Invalid return from node_in_domain()\n");
		return FOD_ILLEGAL;
	}

	/* not reached */
	return FOD_ILLEGAL;
}
