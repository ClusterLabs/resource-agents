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
  Header for Magma Cluster API Library
 */
#ifndef __MAGMA_H
#define __MAGMA_H

#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#define __MAX_SIZE 256 /** Altering this is hazardous to your health */

typedef struct __cluster_member {
	uint64_t	cm_id;			/** Node ID */
	uint8_t		cm_name[__MAX_SIZE];	/** Node name */
	uint8_t		cm_state; 		/** Node state */
	uint8_t		cm_pad[7];		/** Align this */
	struct addrinfo *cm_addrs;		/** Node IP addresses */
} cluster_member_t;

typedef struct __cluster_member_list {
	char			cml_groupname[__MAX_SIZE]; /** Group name */
	uint32_t		cml_count;	/** Node count */
	uint8_t			cml_pad[4];	/** Align this */
	cluster_member_t	cml_members[0];	/** Node array */
} cluster_member_list_t;

#define cml_size(c) \
	(sizeof(cluster_member_list_t) + \
	 sizeof(cluster_member_t) * c)

#ifdef MDEBUG
#define cml_alloc(size) _dmalloc(cml_size(size), __FILE__, __LINE__)
#else
#define cml_alloc(size) malloc(cml_size(size))
#endif


/* don't call this from within modules. */
void cml_free(cluster_member_list_t *ml);
cluster_member_list_t *cml_dup(cluster_member_list_t *ml);


#ifdef DEBUG
#ifndef dbg_printf
#define dbg_printf(fmt, args...) printf("DEBUG: " fmt, ##args)
#endif
#else
#ifndef dbg_dprintf
#define dbg_dprintf(fmt, args...)
#endif
#endif


#define clu_perror(func, ret) \
	fprintf(stderr, "%s: %s\n", func, strerror(-ret))

/*
 * Member states
 */
#define STATE_DOWN		0
#define STATE_UP		1
#define STATE_INVALID		2


/**
 * Quorum/Group states
 */
#define QF_NONE			(0)	/** Not quorate.  Unsafe to run */
#define QF_QUORATE		(1<<0)	/** Quorate.  Safe to run */
#define QF_GROUPMEMBER		(1<<1)	/** Member of group.  App specific */

#define is_quorate(i)		(i&QF_QUORATE)
#define is_group_member(i)	(i&QF_GROUPMEMBER)
#define is_safe(i)		(isquorate(i) && is_group_member(i))


/*
 * Events we need to handle
 */
#define CE_NULL			0	
#define CE_MEMB_CHANGE		1	/* Membership change / transition */
#define CE_QUORATE		2	/* AKA Gain of quorum */
#define CE_INQUORATE		3	/* AKA loss of quorum.  All locks
					   are destroyed. */
#define CE_SHUTDOWN		4	/* Clean shutdown please */
#define CE_SUSPEND		5	/* Pause; next event unpauses */


/**
 * Lock flags
 */
#define CLK_NONE		(0)	/** ... */
#define CLK_NOWAIT		(1<<0)	/** return EAGAIN if not avail */
#define CLK_WRITE		(1<<1)	/** Write lock */
#define CLK_READ		(1<<2)	/** read lock -- TBD */
#define CLK_EX			(CLK_READ|CLK_WRITE)


#define NODE_ID_NONE		((uint64_t)-1)


/**
 * User programs should not manipulate the ccluster_plugin_t structure,
 * so we have it as a void pointer in user programs.
 */
#ifndef __CLUSTER__

typedef void cluster_plugin_t;

#else

#include <magma-build.h>

#endif


/*
 * plugin.c: Load/unload functions
 */
cluster_plugin_t *cp_load(const char *libpath);
int cp_init(cluster_plugin_t *cpp, const void *priv, size_t privlen);
int cp_unload(cluster_plugin_t *cpp);

/*
 * plugin.c: Wrappers to call member functions of a given driver struct
 */
int cp_null(cluster_plugin_t *);
cluster_member_list_t * cp_member_list(cluster_plugin_t *, char *);
int cp_quorum_status(cluster_plugin_t *, char *);
char *cp_plugin_version(cluster_plugin_t *);

int cp_open(cluster_plugin_t *);
int cp_login(cluster_plugin_t *, int, char *name);
int cp_get_event(cluster_plugin_t *, int);
int cp_logout(cluster_plugin_t *, int);
int cp_close(cluster_plugin_t *, int);
int cp_fence(cluster_plugin_t *, cluster_member_t *);

int cp_lock(cluster_plugin_t *, char *, int, void **);
int cp_unlock(cluster_plugin_t *, char *, void*);


/*
 * global.c: Set/clear default driver structure
 */
void clu_set_default(cluster_plugin_t *driver);
void clu_clear_default(void);


/*
 * global.c: Wrappers around default object, if one exists.  All of these
 * calls are protected by a mutex, so use wisely.
 */
int clu_null(void);
cluster_member_list_t * clu_member_list(char *);
int clu_quorum_status(char *);
char *clu_plugin_version(void);
int clu_get_event(int);

/* People who use the high-level APIs ought to just use clu_connect and
   clu_disconnect; these are provided for completeness. */
int clu_open(void);
int clu_close(int);
int clu_login(int, char *);
int clu_logout(int);

/* These Don't require being logged in. */
int clu_lock(char *resource, int flags, void **lockinfo);
int clu_unlock(char *resource, void *lockinfo);


/*
 * global.c: High-level connect/disconnect.  Sets up default and disconnects
 * from the same.  
 */
int clu_connect(char *, int); /** Search for and log in to whatever we find */
int clu_disconnect(int); /** Log out of whatever module we loaded */
int clu_connected(void); /** are we successfully connected? */


/*
 * localinfo.c: High-level local node info (name, ID).
 */
int clu_local_nodename(char *, char *, size_t);
int clu_local_nodeid(char *, uint64_t *);

/*
 * memberlist.c: Membership deltas.
 */
cluster_member_list_t *clu_members_gained(cluster_member_list_t *old,
				  	  cluster_member_list_t *new);
cluster_member_list_t *clu_members_lost(cluster_member_list_t *old,
				   	cluster_member_list_t *new);

/*
 * memberlist.c: Utilities for finding nodes
 */
int memb_online(cluster_member_list_t *nodes, uint64_t nodeid);
int memb_count(cluster_member_list_t *nodes);
uint64_t memb_name_to_id(cluster_member_list_t *nodes, char *nodename);
cluster_member_t *memb_name_to_p(cluster_member_list_t *nodes, char *nodename);
char *memb_id_to_name(cluster_member_list_t *nodes, uint64_t nodeid);
cluster_member_t *memb_id_to_p(cluster_member_list_t *nodes, uint64_t nodeid);
int memb_mark_down(cluster_member_list_t *nodes, uint64_t nodeid);

/*
 * Functions to resolve hostnames to ipv4/ipv6 addresses
 */
int memb_resolve(cluster_member_t *member);
int memb_resolve_list(cluster_member_list_t *new, cluster_member_list_t *old);

/*
 * address.c: Utilities for dealing with addresses
 */
void print_member_list(cluster_member_list_t *list, int verbose);

#endif /* __MAGMA_H */
