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
#ifndef __MAGMA_BUILD_H
#define __MAGMA_BUILD_H

#ifndef __CLUSTER__
#error "Never include this file from user programs."
#endif

#define CLUSTER_PLUGIN_API_VERSION (double)0.00007

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
#define CP_IMP(ptr, func)	(ptr->cp_ops.s_##func != __U_clu_##func)
#define CP_UNIMP(ptr, func)	(ptr->cp_ops.s_##func != __U_clu_##func)
#define CP_SET_UNIMP(ptr, func) (ptr->cp_ops.s_##func = __U_clu_##func)


/**
 * Cluster plugin object.  This is returned by cp_load(char *filename)
 * It is up to the user to call cp_init(cluster_plugin_t *)
 */
typedef struct __cluster_plugin {
	/**
	 * Plugin functions.
	 */
	struct {
		int (*s_null)(struct __cluster_plugin *);
		cluster_member_list_t *
			(*s_member_list)(struct __cluster_plugin *, char *);

		int (*s_quorum_status)(struct __cluster_plugin *, char *);
		int (*s_get_event)(struct __cluster_plugin *,int);
		char *(*s_plugin_version)(struct __cluster_plugin *);
		int (*s_open)(struct __cluster_plugin *);
		int (*s_fence)(struct __cluster_plugin *, cluster_member_t *);
		int (*s_login)(struct __cluster_plugin *, int, char *);
		int (*s_logout)(struct __cluster_plugin *,int);
		int (*s_close)(struct __cluster_plugin *,int);
		int (*s_lock)(struct __cluster_plugin *, char *, int, void **);
		int (*s_unlock)(struct __cluster_plugin *, char *, void *);
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
		int (*p_load_func)(struct __cluster_plugin *);

		/**
		 * Initialization function.  Generally, the last two
		 * arguments are left as NULL, 0; they are there primarily
		 * for testing.
		 */
		int (*p_init_func)(struct __cluster_plugin *, const void *,
				   size_t);

		/**
		 * De-initialization+unload function.
		 */
		int (*p_unload_func)(struct __cluster_plugin *);

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
int __U_clu_null(cluster_plugin_t *);
cluster_member_list_t *__U_clu_member_list(cluster_plugin_t *, char *); /* FREE ME! */
int __U_clu_quorum_status(cluster_plugin_t *, char *);
char *__U_clu_plugin_version(cluster_plugin_t *);
int __U_clu_get_event(cluster_plugin_t *,int);
int __U_clu_open(cluster_plugin_t *);
int __U_clu_login(cluster_plugin_t *, int, char *);
int __U_clu_logout(cluster_plugin_t *, int);
int __U_clu_fence(cluster_plugin_t *, cluster_member_t *);
int __U_clu_close(cluster_plugin_t *, int);
int __U_clu_lock(cluster_plugin_t *, char *, int, void **);
int __U_clu_unlock(cluster_plugin_t *, char *, void *);

#endif /* __MAGMA_BUILD_H */
