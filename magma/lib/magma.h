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
#ifndef _MAGMA_H
#define _MAGMA_H

#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#define _MAX_SIZE 256 /** Altering this is hazardous to your health */

typedef struct _cluster_member {
	uint64_t	cm_id;			/** Node ID */
	char		cm_name[_MAX_SIZE];	/** Node name */
	uint8_t		cm_state; 		/** Node state */
	uint8_t		cm_pad[7];		/** Align this */
	struct addrinfo *cm_addrs;		/** Node IP addresses */
} cluster_member_t;

typedef struct _cluster_member_list {
	char			cml_groupname[_MAX_SIZE]; /** Group name */
	uint32_t		cml_count;	/** Node count */
	uint8_t			cml_pad[4];	/** Align this */
	cluster_member_t	cml_members[0];	/** Node array */
} cluster_member_list_t;

#define cml_size(c) \
	(sizeof(cluster_member_list_t) + \
	 sizeof(cluster_member_t) * c)

#define cml_alloc(size) malloc(cml_size(size))


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
#define STATE_DOWN		0	/** Node is not online or is not in
					  the given group. */
#define STATE_UP		1	/** Node is online and/or is in the
					  given group */
#define STATE_INVALID		2	/** Unknown state */


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
#define CE_NULL		0	/** No-op/spurious wakeup */
#define CE_MEMB_CHANGE	1	/** Membership change/transition */
#define CE_QUORATE	2	/** AKA Quorum formed */
#define CE_INQUORATE	3	/** AKA Quorum dissolved */
#define CE_SHUTDOWN	4	/** Clean shutdown please */
#define CE_SUSPEND	5	/** Pause; next event unpauses */


/**
 * Lock flags
 */
#define CLK_NONE	(0)	/** No flags */
#define CLK_NOWAIT	(1<<0)	/** Do not block if not immediately available */
#define CLK_WRITE	(1<<1)	/** Write lock */
#define CLK_READ	(1<<2)	/** Read lock */
#define CLK_EX		(CLK_READ|CLK_WRITE)


#define NODE_ID_NONE	((uint64_t)-1) /** The nonexistent cluster member */


/**
 * User programs should not manipulate the ccluster_plugin_t structure,
 * so we have it as a void pointer in user programs.
 */
#ifndef _CLUSTER_

typedef void cluster_plugin_t;

#else

#include <magma-build.h>

#endif


/*
 * plugin.c: Load/unload functions
 */
int cp_connect(cluster_plugin_t **cpp, char *groupname, int login);
cluster_plugin_t *cp_load(const char *libpath);
const char *cp_load_error(int e);
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
int cp_local_nodename(cluster_plugin_t *, char *, char *, size_t);
int cp_local_nodeid(cluster_plugin_t *, char *, uint64_t *);


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
int clu_fence(cluster_member_t *); /** Fence a member.  Don't do this unless
				      you know what you're doing */


/*
 * localinfo.c: High-level local node info (name, ID).
 */
int clu_local_nodename(char *, char *, size_t);
int clu_local_nodeid(char *, uint64_t *);

/*
 * memberlist.c: Membership deltas.
 */
cluster_member_list_t *memb_gained(cluster_member_list_t *old,
		 		   cluster_member_list_t *new);
cluster_member_list_t *memb_lost(cluster_member_list_t *old,
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

#endif /* _MAGMA_H */

/** \mainpage

\section wtf What is Magma?
\par
	Magma is an API library for communicating with different cluster
	infrastructures.  Originally, it was written to port Red Hat
	Cluster Manager to the newer, more modern cluster infrastructures
	which Sistina Software developed, namely, CMAN and GuLM.  For now,
	it is immensely important to operate on both infrastructures, given
	that they are different architectures which solve slightly different
	problems.
\par
	Magma is designed as a self-configuring cluster switch, with the
	goal ultimately being the ability for applications to run unmodified
	on CMAN+DLM and GuLM.  The API itself is primitive at best, and
	should be thought of as a minimal set of functions necessary to
	create a cluster-aware application.  The API resembles the
	API from Red Hat Cluster Manager.
\par
	Magma is by no means restricted to the API(s) it currently has. 
	Indeed, it is a proof of concept that a self configuring cluster
	switch can be created; it is not meant to obsolete other API(s). 
	It would be more interesting for magma to _provide_ other API(s).

\section design Plugin Design considerations.

\subsection All plugins must provide the following dl-mappable functions
(these are checked by cp_load using dlsym):
\par
\p cluster_plugin_load - Maps functions to pointers in new structure
\par
\p cluster_plugin_init - Sets up private data
\par
\p cluster_plugin_unload - Clears out private data and unmaps functions
\par
\p cluster_plugin_version - Returns the version of the implemented magma 
	plugin API. This can be inherited by placing:
     	"IMPORT_PLUGIN_API_VERSION()" near the top of your source code.

\subsection missing Missing Functions in Plugins
\par
	Plugins which do not implement a given function should leave
	them alone; cp_load() maps all functions in a given
	cluster_plugin_t object to be "unimplemented" versions, so
	there is no danger of dereferencing NULL function pointers
	when cp_load is used.

\subsection unload Unloading Plugins
\par
	When the caller calls cp_unload on a cluster plugin, it must be
	fully cleaned up, logged out, and have all resources cleaned
	up without further intervention of the caller.

\subsection cs Coding Style
\par
	Please use Linux-Kernel Coding Style.

\subsection plugins Plugin Design Considerations
\par
	Plugins should be designed as re-entrant entities with no global
	variables defined.  Any specific data should be stored in
	plugin-specific private data structures and not accessed by the core
	library.

\section provide What Magma Provides for You
\subsection api High-level basic cluster functions
These functions are high-level, non-reentrant functions which may be used
to implement clustered applications.  They are self-configuring, and operate
on a "default" cluster plugin which is loaded when the
\ref clu_connect
function is called for the first time.  It is not necessary to log in to
a node or service group in order to obtain the member list of that group;
group login is primarily required for retrieving cluster events.  Not all
infrastructures implement group membership; for these, all node/service groups
are the set of all nodes in the cluster.

\par
\ref clu_connect
\par
\ref clu_disconnect
\par
\ref clu_member_list
\par
\ref clu_quorum_status
\par
\ref clu_fence
\par
\ref clu_get_event
\par
\ref cp_load

\subsection locking Simple Cluster Locking API
These functions may be used after
\ref clu_connect
is called to create and release cluster locks on arbitrary, user-defined
resources.  Logging in to a group is not necessary to take or release cluster
locks.
\par
\ref clu_lock
\par
\ref clu_unlock

\subsection apire High-level basic cluster functions - Reentrant
These functions are lower level than the non-reentrant versions, and provide
a means of communicating with multiple different cluster infrastructures
running on one node.  However, these functions are not self configuring,
and require a more effort to provide the same functionality as the
non-reentrant versions.  Note that
\ref cp_load
is not reentrant, as it loads a plugin from the host file system.
\par
\ref cp_init
\par
\ref cp_member_list
\par
\ref cp_quorum_status
\par
\ref cp_fence
\par
\ref cp_get_event
\par
\ref cp_open
\par
\ref cp_close
\par
\ref cp_login
\par
\ref cp_logout

\subsection lockingre Simple Cluster Locking API - Reentrant
These functions may be used after
\ref cp_load
,
\ref cp_init
,
and
\ref cp_open
are called to create and release cluster locks on arbitrary, user-defined 
resources.  Logging in to a group is not necessary to take or release
cluster locks.
\par
\ref cp_lock
\par
\ref cp_unlock

\subsection local Local Node Identification Information
These functions allow applications to find out a node's identity according
to the cluster infrastructure.
\par
\ref clu_local_nodename
\par
\ref clu_local_nodeid

\subsection localre Local Node Identification Information - Reentrant
These functions allow applications to find out a node's identity according
to the cluster infrastructure.
\par
\ref cp_local_nodename
\par
\ref cp_local_nodeid


\subsection membership Membership List Manipulation and Querying
These allow applications to find out information within membership lists,
alter lists, create new ones, find differences between two lists, and resolve
hostnames or addresses in a given list.
\par
\ref memb_gained
\par
\ref memb_lost
\par
\ref memb_online
\par
\ref memb_name_to_id
\par
\ref memb_name_to_p
\par
\ref memb_id_to_name
\par
\ref memb_id_to_p
\par
\ref memb_resolve
\par
\ref memb_resolve_list
\par
\ref cml_free
\par
\ref cml_dup
\par
\ref print_member_list

\subsection messaging IPv4 / IPv6 abstracted TCP messaging functions
These APIs may be used to address nodes by node ID instead of names, and has
a high dependency on being up to date with 
\ref msg_update
.  The APIs themselves are part of libmagmamsg, which is licensed under 
the GPL, as it is a derivative work of Kimberlite's messaging layer.  Thus,
it will eventually need to either be rewritten or replaced by LGPL code.
\par
\ref msg_update
\par
\ref msg_listen
\par
\ref msg_open
\par
\ref msg_send
\par
\ref msg_receive
\par
\ref msg_receive_timeout
\par
\ref msg_accept
\par
\ref msg_close

\section plugins Notes on Current Plugins

\subsection gulm GuLM - The Grand Unified Lock Manager (gulm.so)
\par
	This plugin does not have a notion of node groups.
\par
	The locking system does not support multiple process-locks
	on the same node.  To get around this, we take a POSIX lock
	on a file prior to asking for a lock from GuLM.

\subsection cman CMAN - Using DLM to sync after transition (cman.so)
\par
	This plugin does not have a notion of node groups.

\subsection sm CMAN - Using the Service Manager (sm.so)
\par
	This plugin uses two different methods to query node group
	information.
\par
	This is the only plugin which returns the CE_SUSPEND event.
	Applications intending to use it should also be able to
	operate without it for now.  The CE_SUSPEND event is more
	or less a barrier.
*/
