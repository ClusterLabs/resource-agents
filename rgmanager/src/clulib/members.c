#include <sys/types.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <malloc.h>
#include <libcman.h>
#include <stdint.h>
#include <stdio.h>
#include <members.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <rg_types.h>
#include <pthread.h>
#include <errno.h>

static int my_node_id = -1;
static pthread_rwlock_t memblock = PTHREAD_RWLOCK_INITIALIZER;
static cluster_member_list_t *membership = NULL;


/**
  Return the stored node ID.  Since this should never
  change during the duration of running rgmanager, it is
  not protected by a lock.
 */
int
my_id(void)
{
	return my_node_id;
}


int
set_my_id(int id)
{
	my_node_id = id;
	return 0;
}


/**
  Determine and store the local node ID.  This should
  only ever be called once during initialization.
 */
int
get_my_nodeid(cman_handle_t h)
{
	cman_node_t node;
	memset(&node,0,sizeof(node));

	if (cman_get_node(h, CMAN_NODEID_US, &node) != 0)
		return -1;

	return node.cn_nodeid;
}



/**
  Generate and return a list of members which are now online in a new
  membership list, given the old membership list.  User must call
  @ref free_member_list
  to free the returned
  @ref cluster_member_list_t
  structure.

  @param old		Old membership list
  @param new		New membership list
  @return		NULL if no members were gained, or a newly 
  			allocated cluster_member_list_t structure.
 */
cluster_member_list_t *
memb_gained(cluster_member_list_t *old, cluster_member_list_t *new)
{
	int count, x, y;
	char in_old = 0;
	cluster_member_list_t *gained = NULL;

	/* No nodes in new?  Then nothing could have been gained */
	if (!new || !new->cml_count)
		return NULL;

	/* Nothing in old?  Duplicate 'new' and return it. */
	if (!old || !old->cml_count)
		return member_list_dup(new);

	/* Use greatest possible count */
	count = (old->cml_count > new->cml_count ?
		 old->cml_count : new->cml_count);
	count *= sizeof(cman_node_t);

	gained = malloc(sizeof(cluster_member_list_t));
	if (!gained)
		return NULL;
	memset(gained, 0, sizeof(*gained));

	gained->cml_members = malloc(count);
	if (!gained->cml_members) {
		free(gained);
		return NULL;
	}
	memset(gained->cml_members, 0, count);

	for (x = 0; x < new->cml_count; x++) {

		/* This one isn't active at the moment; it could not have
		   been gained. */
		if (!new->cml_members[x].cn_member)
			continue;

		in_old = 0;
		for (y = 0; y < old->cml_count; y++) {
			if ((new->cml_members[x].cn_nodeid !=
			     old->cml_members[y].cn_nodeid) ||
			     !old->cml_members[y].cn_member)
				continue;
			in_old = 1;
			break;
		}

		if (in_old)
			continue;
		memcpy(&gained->cml_members[gained->cml_count++],
		       &new->cml_members[x], sizeof(cman_node_t));
	}

	if (gained->cml_count == 0) {
		free(gained->cml_members);
		free(gained);
		gained = NULL;
	}

	return gained;
}


/**
  Generate and return a list of members which are lost or no longer online
  in a new membership list, given the old membership list.  User must call
  @ref free_member_list
  to free the returned
  @ref cluster_member_list_t
  structure.

  @param old		Old membership list
  @param new		New membership list
  @return		NULL if no members were lost, or a newly 
  			allocated cluster_member_list_t structure.
 */
cluster_member_list_t *
memb_lost(cluster_member_list_t *old, cluster_member_list_t *new)
{
	cluster_member_list_t *ret = NULL;
	int x;

	/* Reverse. ;) */
	ret = memb_gained(new, old);
	if (!ret)
		return NULL;

	for (x = 0; x < ret->cml_count; x++) {
		ret->cml_members[x].cn_member = 0;
	}

	return ret;
}



void
member_list_update(cluster_member_list_t *new_ml)
{
	pthread_rwlock_wrlock(&memblock);
	if (membership)
		free_member_list(membership);
	if (new_ml)
		membership = member_list_dup(new_ml);
	else
		membership = NULL;
	pthread_rwlock_unlock(&memblock);
}


cluster_member_list_t *
member_list(void)
{
	cluster_member_list_t *ret = NULL;
	pthread_rwlock_rdlock(&memblock);
	if (membership) 
		ret = member_list_dup(membership);
	pthread_rwlock_unlock(&memblock);
	return ret;
}


void
member_set_state(int nodeid, int state)
{
	int x = 0;

	pthread_rwlock_wrlock(&memblock);
	if (!membership) {
		pthread_rwlock_unlock(&memblock);
		return;
	}

	for (x = 0; x < membership->cml_count; x++) {
		if (membership->cml_members[x].cn_nodeid == nodeid)
			membership->cml_members[x].cn_member = state;
	}
	pthread_rwlock_unlock(&memblock);
}


int
member_low_id(void)
{
	int x = 0, low = -1;

	pthread_rwlock_wrlock(&memblock);
	if (!membership) {
		pthread_rwlock_unlock(&memblock);
		return low;
	}

	for (x = 0; x < membership->cml_count; x++) {
		if ((membership->cml_members[x].cn_member) &&
		    ((membership->cml_members[x].cn_nodeid < low) || (low == -1)))
			low = membership->cml_members[x].cn_nodeid;
	}
	pthread_rwlock_unlock(&memblock);

	return low;
}


int
member_high_id(void)
{
	int x = 0, high = -1;

	pthread_rwlock_wrlock(&memblock);
	if (!membership) {
		pthread_rwlock_unlock(&memblock);
		return high;
	}

	for (x = 0; x < membership->cml_count; x++) {
		if (membership->cml_members[x].cn_member &&
		    (membership->cml_members[x].cn_nodeid > high))
			high = membership->cml_members[x].cn_nodeid;
	}
	pthread_rwlock_unlock(&memblock);

	return high;
}


int
member_online(int nodeid)
{
	int x = 0, ret = 0;

	pthread_rwlock_rdlock(&memblock);
	if (!membership) {
		pthread_rwlock_unlock(&memblock);
		return 0;
	}

	for (x = 0; x < membership->cml_count; x++) {
		if (membership->cml_members[x].cn_nodeid == nodeid) {
			ret = membership->cml_members[x].cn_member;
			break;
		}
	}
	pthread_rwlock_unlock(&memblock);

	return ret;
}



char *
member_name(int id, char *buf, int buflen)
{
	char *n;

	if (!buf || !buflen)
		return NULL;

	pthread_rwlock_rdlock(&memblock);
	n = memb_id_to_name(membership, id);
	if (n) {
		strncpy(buf, n, buflen);
	} else {
		buf[0] = 0;
	}
	pthread_rwlock_unlock(&memblock);
	return buf;
}



cluster_member_list_t *
get_member_list(cman_handle_t h)
{
	int c;
	int tries = 0, local = 0;
	cluster_member_list_t *ml = NULL;
	cman_node_t *nodes = NULL;

	if (h == NULL) {
		local = 1;
		h = cman_init(NULL);
		if (!h)
			return NULL;
	}

	do {	
		++tries;
		if (nodes)
			free(nodes);

		c = cman_get_node_count(h);
		if (c <= 0) {
			if (errno == EINTR)
				continue;
			if (ml)
				free(ml);
			ml = NULL;
			goto out;
		}

		if (!ml)
			ml = malloc(sizeof(*ml));
		if (!ml)
			goto out;

		nodes = malloc(sizeof(*nodes) * c);
		if (!nodes) {
			free(ml);
			ml = NULL;
			goto out;
		}

		memset(ml, 0, sizeof(*ml));
		memset(nodes, 0, sizeof(*nodes)*c);

		cman_get_nodes(h, c, &ml->cml_count, nodes);

	} while (ml->cml_count != c);

	ml->cml_members = nodes;
	ml->cml_count = c;

out:
	if (local)
		cman_finish(h);
	return ml;
}


void
free_member_list(cluster_member_list_t *ml)
{
	if (ml) {
		if (ml->cml_members)
			free(ml->cml_members);
		free(ml);
	}
}


int
memb_online(cluster_member_list_t *ml, int nodeid)
{
	int x = 0;

	if (!ml)
		return 0;

	for (x = 0; x < ml->cml_count; x++) {
		if (ml->cml_members[x].cn_nodeid == nodeid)
			return ml->cml_members[x].cn_member;
	}

	return 0;
}


int
memb_count(cluster_member_list_t *ml)
{
	int x = 0, count = 0;

	if (!ml)
		return 0;

	for (x = 0; x < ml->cml_count; x++) {
		if (ml->cml_members[x].cn_member)
			++count;
	}

	return count;
}


int
memb_mark_down(cluster_member_list_t *ml, int nodeid)
{
	int x = 0;

	if (!ml)
		return -1;

	for (x = 0; x < ml->cml_count; x++) {
		if (ml->cml_members[x].cn_nodeid == nodeid)
			ml->cml_members[x].cn_member = 0;
	}

	return 0;
}



char *
memb_id_to_name(cluster_member_list_t *ml, int nodeid)
{
	int x = 0;
	if (!ml)
		return NULL;

	for (x = 0; x < ml->cml_count; x++) {
		if (ml->cml_members[x].cn_nodeid == nodeid)
			return ml->cml_members[x].cn_name;
	}

	return NULL;
}


cman_node_t *
memb_id_to_p(cluster_member_list_t *ml, int nodeid)
{
	int x = 0;
	if (!ml)
		return NULL;

	for (x = 0; x < ml->cml_count; x++) {
		if (ml->cml_members[x].cn_nodeid == nodeid)
			return &ml->cml_members[x];
	}

	return NULL;
}


int
memb_online_name(cluster_member_list_t *ml, char *name)
{
	int x = 0;
	if (!ml)
		return 0;

	for (x = 0; x < ml->cml_count; x++) {
		if (!strcasecmp(ml->cml_members[x].cn_name, name))
			return ml->cml_members[x].cn_member;
	}

	return 0;
}


int
memb_name_to_id(cluster_member_list_t *ml, char *name)
{
	int x = 0;
	if (!ml)
		return 0;

	for (x = 0; x < ml->cml_count; x++) {
		if (!strcasecmp(ml->cml_members[x].cn_name, name))
			return ml->cml_members[x].cn_nodeid;
	}

	return 0;
}


cman_node_t *
memb_name_to_p(cluster_member_list_t *ml, char *name)
{
	int x = 0;
	if (!ml)
		return NULL;

	for (x = 0; x < ml->cml_count; x++) {
		if (!strcasecmp(ml->cml_members[x].cn_name, name))
			return &ml->cml_members[x];
	}

	return NULL;
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
member_list_dup(cluster_member_list_t *orig)
{
	cluster_member_list_t *ret = NULL;

	if (!orig)
		return NULL;

	ret = malloc(sizeof(cluster_member_list_t));
	if (!ret)
		return NULL;
	memset(ret, 0, sizeof(cluster_member_list_t));
	ret->cml_members = malloc(sizeof(cman_node_t) * orig->cml_count);

	if (!ret->cml_members) {
		free(ret);
		return NULL;
	}
	ret->cml_count = orig->cml_count;
	memcpy(ret->cml_members, orig->cml_members,
	       orig->cml_count * sizeof(cman_node_t));

	return ret;
}

