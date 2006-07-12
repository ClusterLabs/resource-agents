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

#define RG_PORT 177
#define RGMGR_SOCK "/var/run/cluster/rgmanager.sk"

#endif
