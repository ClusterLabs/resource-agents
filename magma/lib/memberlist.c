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
  Generate a list of nodes which are now present in 'new' but weren't in 'old'
  User must free.
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
  Generate a list of nodes which are now present in 'new' but weren't in 'old'
  User must free.
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
  Returns 1 if nodeid exists in list and nodeid's state is UP
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
  Return a nodename in a list given a nodeid
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
  Return a nodename in a list given a nodeid
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
  Return a nodename in a list given a nodeid
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
  Return a nodename in a list given a nodeid
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

		printf("    %s (id %ld) state ",
		       list->cml_members[x].cm_name,
		       (long)list->cml_members[x].cm_id);

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
