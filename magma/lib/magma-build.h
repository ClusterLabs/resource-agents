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
 * Definitions for implementing cluster infrastructure plugins; should not
 * be included by user programs directly.
 */
#ifndef _MAGMA_BUILD_H
#define _MAGMA_BUILD_H

#ifndef _CLUSTER_
#error "Never include this file from user programs."
#endif

#define CLUSTER_PLUGIN_API_VERSION (double)0.00009

#define IMPORT_PLUGIN_API_VERSION() \
double cluster_plugin_version(void) \
{\
	return CLUSTER_PLUGIN_API_VERSION;\
}


/**
 * Module symbols & definitions
 */
#define CLU_PLUGIN_LOAD		cluster_plugin_load
#define CLU_PLUGIN_INIT		cluster_plugin_init
#define CLU_PLUGIN_UNLOAD	cluster_plugin_unload
#define CLU_PLUGIN_VERSION	cluster_plugin_version

#define CLU_PLUGIN_LOAD_SYM	"cluster_plugin_load"
#define CLU_PLUGIN_INIT_SYM	"cluster_plugin_init"
#define CLU_PLUGIN_UNLOAD_SYM	"cluster_plugin_unload"
#define CLU_PLUGIN_VERSION_SYM	"cluster_plugin_version"



/**
 * Handy macros
 */
#define CP_IMP(ptr, func)	(ptr->cp_ops.s_##func != _U_clu_##func)
#define CP_UNIMP(ptr, func)	(ptr->cp_ops.s_##func != _U_clu_##func)
#define CP_SET_UNIMP(ptr, func) (ptr->cp_ops.s_##func = _U_clu_##func)


/**
 * Cluster plugin object.  This is returned by cp_load(char *filename)
 * It is up to the user to call cp_init(cluster_plugin_t *)
 */
typedef struct _cluster_plugin {
	/**
	 * Plugin functions.
	 */
	struct {
		/**
		  No op cluster function.  Optional; maps to clu_null 
		  in the default cluster plugin.

		  @see clu_null
		 */
		int (*s_null)(struct _cluster_plugin *);

		/**
		  Cluster member list function.  Required; maps to
		  clu_member_list in the default cluster plugin.

		  @see clu_member_list
		 */
		cluster_member_list_t *
			(*s_member_list)(struct _cluster_plugin *, char *);

		/**
		  Cluster quorum status function.  Required; maps to
		  clu_quorum_status in the default cluster plugin.
		 
		  @see clu_quorum_status
		 */
		int (*s_quorum_status)(struct _cluster_plugin *, char *);

		/**
		  Cluster event reading function.  Required; maps to
		  clu_get_event in the default cluster plugin.
		
		  @see clu_get_event
		 */
		int (*s_get_event)(struct _cluster_plugin *,int);

		/**
		  Cluster plugin version function.  Optional; maps to 
		  clu_plugin_version in the default cluster plugin.
		
		  @see clu_plugin_version
		 */
		char *(*s_plugin_version)(struct _cluster_plugin *);

		/**
		  Cluster plugin open function.  Required; maps to clu_open
		  when a default cluster plugin is used.  Though required,
		  this is not generally called directly, as clu_connect handles
		  this..
		 
		  @see clu_open, clu_connect
		 */
		int (*s_open)(struct _cluster_plugin *);

		/**
		  I/O Fencing function.  Optional; maps to clu_fence.
		 
		  @see clu_fence
		 */
		int (*s_fence)(struct _cluster_plugin *, cluster_member_t *);

		/**
		  Group Login function.  Optional; maps to clu_login when
		  a default plugin is set.  Not generally called by users;
		  clu_connect takes care of this for us.
		
		  @see clu_login, clu_connect
		 */
		int (*s_login)(struct _cluster_plugin *, int, char *);

		/**
		  Group Logout function.  Optional; maps to clu_logout when
		  a default plugin is set.  Not generally called by users;
		  clu_disconnect takes care of this for us.
		
		  @see clu_logout, clu_disconnect
		 */
		int (*s_logout)(struct _cluster_plugin *,int);

		/**
		  Cluster plugin open function.  Required; maps to clu_close_
		  when a default cluster plugin is used.  Though required,
		  this is not generally called directly, as clu_connect handles
		  this.
		 
		  @see clu_close, clu_disconnect
		 */
		int (*s_close)(struct _cluster_plugin *,int);

		/**
		  Cluster lock function.  Optional; maps to clu_lock when
		  a default cluster plugin is used.
		 
		  @see clu_lock
		 */
		int (*s_lock)(struct _cluster_plugin *, char *, int, void **);

		/**
		  Cluster unlock function.  Optional; maps to clu_unlock when
		  a default cluster plugin is used.
		 
		  @see clu_unlock
		 */
		int (*s_unlock)(struct _cluster_plugin *, char *, void *);
	} cp_ops;

	/**
	 * Private data.
	 */
	struct {
		/**
		 * Handle we obtained from dlopen()
		 */
		void *p_dlhandle;

		/**
		 * Plugin load function.  Generally, just sets up function
		 * pointers.
		 */
		int (*p_load_func)(struct _cluster_plugin *);

		/**
		 * Initialization function.  Generally, the last two
		 * arguments are left as NULL, 0; they are there primarily
		 * for testing.
		 */
		int (*p_init_func)(struct _cluster_plugin *, const void *,
				   size_t);

		/**
		 * De-initialization+unload function.
		 */
		int (*p_unload_func)(struct _cluster_plugin *);

		/**
		 * Plugin-specific private data.  Can be anything;
		 * implementation specific.
		 */
		void *p_data;

		/**
		 * Plugin-specific private data length.
		 */
		size_t p_datalen;
	} cp_private;
} cluster_plugin_t;


/**
 * plugin.c: All objects start off with these functions assigned to their
 * member functions.  These should not be called directly.
 */
int _U_clu_null(cluster_plugin_t *);
cluster_member_list_t *_U_clu_member_list(cluster_plugin_t *, char *); /* FREE ME! */
int _U_clu_quorum_status(cluster_plugin_t *, char *);
char *_U_clu_plugin_version(cluster_plugin_t *);
int _U_clu_get_event(cluster_plugin_t *,int);
int _U_clu_open(cluster_plugin_t *);
int _U_clu_login(cluster_plugin_t *, int, char *);
int _U_clu_logout(cluster_plugin_t *, int);
int _U_clu_fence(cluster_plugin_t *, cluster_member_t *);
int _U_clu_close(cluster_plugin_t *, int);
int _U_clu_lock(cluster_plugin_t *, char *, int, void **);
int _U_clu_unlock(cluster_plugin_t *, char *, void *);

#endif /* _MAGMA_BUILD_H */
