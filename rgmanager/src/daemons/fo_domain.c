/*
  Copyright Red Hat, Inc. 2002-2003

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
/** @file
 * Fail-over Domain & Preferred Node Ordering Driver.  Ripped right from
 * the clumanager 1.2 code base.
 */
#include <string.h>
#include <list.h>
#include <clulog.h>
#include <resgroup.h>
#include <reslist.h>
#include <ccs.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>


//#define DEBUG

#ifdef DEBUG
#define ENTER() clulog(LOG_DEBUG, "ENTER: %s\n", __FUNCTION__)
#define RETURN(val) {\
	clulog(LOG_DEBUG, "RETURN: %s line=%d value=%d\n", __FUNCTION__, \
	       __LINE__, (val));\
	return(val);\
}
#else
#define ENTER()
#define RETURN(val) return(val)
#endif

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
get_node(int ccsfd, char *base, int idx, fod_t *domain)
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

		clulog(LOG_ERR, "#30: Node %s defined multiple times in "
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
get_domain(int ccsfd, char *base, int idx, fod_t **domains)
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
		    
		clulog(LOG_ERR, "#31: Domain %s defined multiple times\n",
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


	snprintf(xpath, sizeof(xpath), "%s/failoverdomain[%d]",
		 base, idx);

	do {
		fodn = get_node(ccsfd, xpath, x++, fod);
		if (fodn) {
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
		fod = get_domain(ccsfd, xpath, x++, domains);
		if (fod) {
			list_insert(domains, fod);
		}
	} while (fod);

	return 0;
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

	list_do(domains, fod) {
		printf("Failover domain: %s\n", fod->fd_name);
		printf("Flags: ");
		if (!fod->fd_flags) {
			printf("none\n");
		} else {
			if (fod->fd_flags & FOD_ORDERED)
				printf("Ordered ");
			if (fod->fd_flags & FOD_RESTRICTED)
				printf("Restricted");
			printf("\n");
		}

		list_do(&fod->fd_nodes, fodn) {
			printf("  Node %s (priority %d)\n",
			       fodn->fdn_name, fodn->fdn_prio);
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
	int member_online = 0, member_match = 0, preferred = 100, myprio = -1;
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
		member_online = 1;
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

	if (!member_online)
		return 0;

	if (!member_match)
		return 1;

	/* Figure out if we're the in the most-preferred group */
	preferred = (myprio <= preferred);
	if (!preferred)
		return 2;

	return 3;
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
node_should_start(uint64_t nodeid, cluster_member_list_t *membership,
		  char *rg_name, fod_t **domains)
{
	char *nodename = NULL;
	char domainname[128];
	int ordered = 0;
	int restricted = 0;
	fod_t *fod = NULL;
	int found = 0;

	ENTER();

	/*
	 * Um, if the node isn't online...
	 */
	if (!memb_online(membership, nodeid)) {
#ifdef DEBUG
		clulog(LOG_DEBUG,"Member #%d is not online -> NO\n", nodeid);
#endif
		RETURN(FOD_ILLEGAL);
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
		clulog(LOG_DEBUG,
		       "Fail-over Domain for service %d nonexistent\n");
#endif
		RETURN(FOD_BEST);
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
		clulog(LOG_WARNING, "#66: Domain '%s' specified for resource "
		       "group %s nonexistent!\n", domainname, rg_name);
		RETURN(FOD_BEST);
	}

	/*
	 * Determine whether this domain is restricted or not...
	 */
	restricted = !!(fod->fd_flags & FOD_RESTRICTED);

	/*
	 * Determine whether this domain is ordered or not...
	 */
	ordered = !!(fod->fd_flags & FOD_ORDERED);

	switch (node_in_domain(nodename, fod, membership)) {
	case 0:
		/*
		 * Node is not a member of the domain and no members of the
		 * domain are online.
		 */
#ifdef DEBUG
		clulog(LOG_DEBUG, "Member #%d is not a member and no "
		       "members are online\n", nodeid);
#endif
		if (!restricted) {
#ifdef DEBUG
			clulog(LOG_DEBUG,"Restricted mode off -> BEST\n");
#endif
			RETURN(FOD_BEST);
		}
#ifdef DEBUG
		clulog(LOG_DEBUG,"Restricted mode -> ILLEGAL\n");
#endif
		RETURN(FOD_ILLEGAL);
	case 1:
		/* 
		 * Node is not a member of the domain and at least one member
		 * of the domain is online.
		 */
		/* In this case, we can ignore 'restricted' */
#ifdef DEBUG
		clulog(LOG_DEBUG, "Member #%d is not a member of domain %s "
		       "and a member is online\n", nodeid, domainname);
#endif
		if (!restricted) {
#ifdef DEBUG
			clulog(LOG_DEBUG,"Restricted mode off -> GOOD\n");
#endif
			RETURN(FOD_GOOD);
		}
#ifdef DEBUG
		clulog(LOG_DEBUG,"Restricted mode -> ILLEGAL\n");
#endif
		RETURN(FOD_ILLEGAL);
	case 2:
		/*
		 * Node is a member of the domain, but is not the
		 * lowest-ordered, online member.
		 */
#ifdef DEBUG
		clulog(LOG_DEBUG, "Member #%d is a member, but is not the "
		       "lowest-ordered\n", nodeid);
#endif
		if (ordered) {
#ifdef DEBUG
			clulog(LOG_DEBUG,"Ordered mode -> BETTER\n");
#endif
			RETURN(FOD_BETTER);
		}

#ifdef DEBUG
		clulog(LOG_DEBUG,"Not using ordered mode -> BEST\n");
#endif
		RETURN(FOD_BEST);
	case 3:
		/*
		 * Node is a member of the domain and is the lowest-ordered,
		 * online member.
		 */
		/* In this case, we can ignore 'ordered' */
#ifdef DEBUG
		clulog(LOG_DEBUG, "Member #%d is the lowest-ordered member "
		       "of the domain -> BEST\n", nodeid);
#endif
		RETURN(FOD_BEST);
	default:
		/* Do what? */
		clulog(LOG_ERR, "#32: Code path error: "
		       "Invalid return from node_in_domain()\n");
		RETURN(FOD_ILLEGAL);
	}

	/* not reached */
	RETURN(FOD_ILLEGAL);
}
