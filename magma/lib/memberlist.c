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
  Magma membership list handling routines.
 */
#include <magma.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>

#ifdef MDEBUG
#include <mallocdbg.h>
#endif


/**
  Generate and return a list of members which are now online in a new
  membership list, given the old membership list.  User must call
  @ref cml_free
  to free the returned
  @ref cluster_member_list_t
  structure.

  @param old		Old membership list
  @param new		New membership list
  @return		NULL if no members were gained, or a newly 
  			allocated cluster_member_list_t structure.
 */
cluster_member_list_t *
clu_members_gained(cluster_member_list_t *old, cluster_member_list_t *new)
{
	int count, x, y;
	char in_old = 0;
	cluster_member_list_t *gained;

	/* No nodes in new?  Then nothing could have been gained */
	if (!new || !new->cml_count)
		return NULL;

	/* Nothing in old?  Duplicate 'new' and return it. */
	if (!old || !old->cml_count) {
		gained = cml_alloc(new->cml_count);
		if (!gained)
			return NULL;
		memcpy(gained, new, cml_size(new->cml_count));
		return gained;
	}

	/* Use greatest possible count */
	count = (old->cml_count > new->cml_count ?
		 cml_size(old->cml_count) : cml_size(new->cml_count));

	gained = malloc(count);
	if (!gained)
		return NULL;
	memset(gained, 0, count);

	for (x = 0; x < new->cml_count; x++) {

		/* This one isn't active at the moment; it could not have
		   been gained. */
		if (new->cml_members[x].cm_state != STATE_UP)
			continue;

		in_old = 0;
		for (y = 0; y < old->cml_count; y++) {
			if ((new->cml_members[x].cm_id !=
			     old->cml_members[y].cm_id) ||
			    (old->cml_members[y].cm_state != STATE_UP))
				continue;
			in_old = 1;
			break;
		}

		if (in_old)
			continue;
		memcpy(&gained->cml_members[gained->cml_count],
		       &new->cml_members[x], sizeof(cluster_member_t));

		/* Don't copy this part over */ 
		gained->cml_members[gained->cml_count++].cm_addrs = NULL;
	}

	if (gained->cml_count == 0) {
		free(gained);
		gained = NULL;
	}

	return gained;
}


/**
  Generate and return a list of members which are lost or no longer online
  in a new membership list, given the old membership list.  User must call
  @ref cml_free
  to free the returned
  @ref cluster_member_list_t
  structure.

  @param old		Old membership list
  @param new		New membership list
  @return		NULL if no members were lost, or a newly 
  			allocated cluster_member_list_t structure.
 */
cluster_member_list_t *
clu_members_lost(cluster_member_list_t *old, cluster_member_list_t *new)
{
	cluster_member_list_t *ret;
	int x;

	/* Reverse. ;) */
	ret = clu_members_gained(new, old);
	if (!ret)
		return NULL;

	for (x = 0; x < ret->cml_count; x++) {
		ret->cml_members[x].cm_state = STATE_DOWN;
	}

	return ret;
}


/**
  Find out if a given node ID is online in a membership list.  In order for 
  this check to return '1' (true), the node must (a) exist in the membership
  list and (b) must be in STATE_UP state.

  @param list		List to search
  @param nodeid		Node ID to look for
  @return		0 if node does not exist or is not STATE_UP, or 1
  			if node is STATE_UP
 */
int
memb_online(cluster_member_list_t *list, uint64_t nodeid)
{
	int x;

	if (!list)
		return 0;

	for (x = 0; x < list->cml_count; x++) {
		if (list->cml_members[x].cm_id == nodeid &&
		    list->cml_members[x].cm_state == STATE_UP)
			return 1;
	}

	return 0;
}


/**
  Find a node's name given it's node ID in a membership list.

  @param list		Member list to search
  @param nodeid		Node ID to look for
  @return		NULL if not found, "none" if the we were asked for
  			@ref NODE_ID_NONE
			or the character pointer of the node name in list.
 */
char *
memb_id_to_name(cluster_member_list_t *list, uint64_t nodeid)
{
	int x;

	if (!list)
		return NULL;

	if (nodeid == (uint64_t)-1)
		return "none";

	for (x = 0; x < list->cml_count; x++) {
		if (list->cml_members[x].cm_id == nodeid &&
		    list->cml_members[x].cm_state == STATE_UP)
			return list->cml_members[x].cm_name;
	}

	return NULL;
}


/**
  Find a node's ID given it's node name in a membership list.

  @param list		Member list to search
  @param nodename	Node name to look for
  @return		NODE_ID_NONE if not found, or the node ID 
  			corresponding to nodename in the list.
 */
uint64_t
memb_name_to_id(cluster_member_list_t *list, char *nodename)
{
	int x;

	if (!list)
		return (uint64_t)-1;

	for (x = 0; x < list->cml_count; x++) {
		if (!strcmp(list->cml_members[x].cm_name, nodename) &&
		    list->cml_members[x].cm_state == STATE_UP)
			return list->cml_members[x].cm_id;
	}

	return (uint64_t)-1;
}


/**
  Find a node's structure pointer given it's node ID.

  @param list		Member list to search
  @param nodeid		Node ID to look for
  @return		NULL if not found, or the address of the
  			cluster_member_t structure corresponding to nodeid.
 */
cluster_member_t *
memb_id_to_p(cluster_member_list_t *list, uint64_t nodeid)
{
	int x;

	if (!list)
		return NULL;

	for (x = 0; x < list->cml_count; x++) {
		if (list->cml_members[x].cm_id == nodeid &&
		    list->cml_members[x].cm_state == STATE_UP)
			return &list->cml_members[x];
	}

	return NULL;
}


/**
  Find a node's structure pointer given it's name.

  @param list		Member list to search
  @param nodename	Node name to look for
  @return		NULL if not found, or the address of the
  			cluster_member_t structure corresponding to nodename.
 */
cluster_member_t *
memb_name_to_p(cluster_member_list_t *list, char *nodename)
{
	int x;

	if (!list)
		return NULL;

	for (x = 0; x < list->cml_count; x++) {
		if (!strcmp(list->cml_members[x].cm_name, nodename) &&
		    list->cml_members[x].cm_state == STATE_UP)
			return &list->cml_members[x];
	}

	return NULL;
}


int
memb_mark_down(cluster_member_list_t *list, uint64_t nodeid)
{
	int x;

	if (!list)
		return 0;

	for (x = 0; x < list->cml_count; x++) {
		if (list->cml_members[x].cm_id == nodeid) {
			list->cml_members[x].cm_state = STATE_DOWN;
			return 0;
		}
	}

	return -1;
}


/**
  Resolve a cluster member's address(es) using getaddrinfo.

  @param member		Member to resolve.
  @return		-1 on getaddrinfo failure, or 0 on success
 */
int
memb_resolve(cluster_member_t *member)
{
	struct addrinfo ai;

	if (!member)
		return -1;

	if (member->cm_addrs)
		freeaddrinfo(member->cm_addrs);
	member->cm_addrs = NULL;

	memset(&ai, 0, sizeof(ai));
	ai.ai_family = AF_UNSPEC;
	ai.ai_flags = AI_CANONNAME;
	/*
	 * Cluster-aware apps using magma must be stream-reachable.
	 */
	ai.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(member->cm_name, NULL, &ai, &member->cm_addrs) != 0) {
		member->cm_addrs = NULL;
		return -1;
	}

	return 0;
}


/**
  Duplicate and return a cluster member list structure, sans the DNS resolution
  information.

  @param orig		List to duplicate.
  @return		NULL if there is nothing to duplicate or duplication
  			fails, or a newly allocated cluster_member_list_t
			structure.
 */
cluster_member_list_t *
cml_dup(cluster_member_list_t *orig)
{
	int x;
	cluster_member_list_t *ret = NULL;

	if (!orig)
		return NULL;

	ret = malloc(cml_size(orig->cml_count));
	memset(ret, 0, cml_size(orig->cml_count));
	memcpy(ret, orig, cml_size(orig->cml_count));

	/* Zero out the addrs */
	for (x = 0; x < ret->cml_count; x++)
		ret->cml_members[x].cm_addrs = NULL;

	return ret;
}


/**
  Find the number of nodes marked as STATE_UP in a membership list

  @param list		List to search
  @return		Number of nodes marked as STATE_UP
 */
int
memb_count(cluster_member_list_t *list)
{
	int x, count = 0;

	for (x = 0; x < list->cml_count; x++) {
		if (list->cml_members[x].cm_state == STATE_UP)
			count++;
	}

	return count;
}


/**
  Resolve cluster members' hostnames->IP addresses.  This gets a list of
  all IP addresses and stores them in the cm_addr part of the
  cluster_member_t structure; so you must free cluster_member_list_t *
  with cml_free.  This function uses "old" for hints about addresses:
  if called with 'old' and 'old' has any addresses for members contained
  within, they are *moved* to 'new'.  This modifies both arguments,
  beware.

  @param new		New membership list
  @param old		Old membership list (or NULL)
  @return		0
 */
int
memb_resolve_list(cluster_member_list_t *new, cluster_member_list_t *old)
{
	cluster_member_t *nm, *om;
	int x, y;
	char found = 0;

	if (!new)
		return -1;

	for (x = 0; x < new->cml_count; x++) {

		nm = &new->cml_members[x];

		if (nm->cm_addrs)
			continue;

		if (!old) {
			memb_resolve(nm);
			continue;
		}

		/* Ok, look in the old list */
		found = 0;
		for (y = 0; y < old->cml_count; y++) {
			om = &old->cml_members[y];

			if (om->cm_id != nm->cm_id)
				continue;

			if (strcmp(om->cm_name, nm->cm_name))
				continue;

			/* We have a match, but no old-address */
			if (!om->cm_addrs)
				break;

			/* Pointer movage. */
			nm->cm_addrs = om->cm_addrs;
			om->cm_addrs = NULL;
			found = 1;
			break;
		}

		if (found)
			continue;

		/* Ok, not found in old list or no addresses present.
		   Do the dirty work. */
		memb_resolve(nm);
	}

	return 0;
}


/**
  Print a membership list structure to stdout.  Primarily for debugging.

  @param list		cluster_member_list_t structure to print
  @param verbose	Prints extra messages if set to nonzero
 */
void
print_member_list(cluster_member_list_t *list, int verbose)
{
	int x;
	struct addrinfo *ai;
	char ipaddr[256];
	void *p;

	if (!list || !list->cml_count) {
		return;
	}

	memb_resolve_list(list, NULL);

	if (verbose)
		printf("+++ Dump of %p (%d nodes)\n", list, list->cml_count);

	for (x=0; x<list->cml_count; x++) {

		printf("    %s (id 0x%016llx) state ",
		       list->cml_members[x].cm_name,
		       (unsigned long long)list->cml_members[x].cm_id);

		if (list->cml_members[x].cm_state == STATE_UP)
			printf("Up\n");
		else
			printf("Down\n");

		if (!list->cml_members[x].cm_addrs)
			continue;

		for (ai = list->cml_members[x].cm_addrs; ai; ai = ai->ai_next){
			if (ai->ai_family == AF_INET)
				p = &(((struct sockaddr_in *)ai->ai_addr)->sin_addr);
			else if (ai->ai_family == AF_INET6)
				p = &(((struct sockaddr_in6 *)ai->ai_addr)->sin6_addr);
			else
				continue;

			if (inet_ntop(ai->ai_family, p, ipaddr,
				      sizeof(ipaddr)) == NULL)
				continue;

			printf("     - %s %s\n", ai->ai_canonname, ipaddr);
		}
	}

	if (verbose)
		printf("--- Done\n");
}


/**
  Frees a cluster member list structure, including DNS/host resolution
  information.

  @param ml		Member list to free.
 */
void
cml_free(cluster_member_list_t *ml)
{
	int x;

	if (!ml)
		return;

	for (x = 0; x < ml->cml_count; x++) {
		if (ml->cml_members[x].cm_addrs)
			freeaddrinfo(ml->cml_members[x].cm_addrs);
	}

	free(ml);
}
