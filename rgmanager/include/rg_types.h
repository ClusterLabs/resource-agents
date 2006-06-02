#ifndef _RG_TYPES_H
#define _RG_TYPES_H

#include <stdint.h>
#include <arpa/inet.h>
#include <libcman.h>
#include <libdlm.h>

typedef struct cluster_members {
	int cml_count;
	int pad;
	cman_node_t *cml_members;
} cluster_member_list_t;


typedef struct _cluster_stuff {
	pthread_mutex_t c_lock;
	cman_handle_t c_cluster;
	dlm_lshandle_t c_lockspace;
	int c_nodeid;
	void *local_ctx;
	void *cluster_ctx;
} chandle_t;


#endif
