/*
 * Copyright (C) 2005 Red Hat, Inc.
 *
 * This file is released under the LGPL.
 */
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/list.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/socket.h>
#include <linux/signal.h>
#include <cluster/service.h>
#include <cluster/cnxman.h>
#include <cluster/cnxman-socket.h>

#include "dm-log.h"
#include "dm-io.h"

uint32_t local_id;
uint32_t my_id=0;
int global_count=0;
uint32_t *global_nodeids=NULL;

int restart_event_type=0;
int restart_event_id=0;

uint32_t nodeid_to_ipaddr(uint32_t nodeid){
	struct cluster_node_addr *cna;
	struct sockaddr_in *saddr;
	struct list_head *list = kcl_get_node_addresses(nodeid);

	if(!list){
		printk("No address list for nodeid %u\n", nodeid);
		return 0;
	}
		

	list_for_each_entry(cna, list, list){
		saddr = (struct sockaddr_in *)(&cna->addr);
		return (uint32_t)(saddr->sin_addr.s_addr);
	}
	return 0;
}

uint32_t ipaddr_to_nodeid(struct sockaddr *addr){
	struct list_head *addr_list;
	struct kcl_cluster_node node;
	struct cluster_node_addr *tmp;

	if(!(addr_list = kcl_get_node_addresses(my_id))){
		DMWARN("No address list available for %u\n", my_id);
		goto fail;
	}

	if(addr->sa_family == AF_INET){
		struct sockaddr_in a4;
		struct sockaddr_in *tmp_addr;
		list_for_each_entry(tmp, addr_list, list){
			tmp_addr = (struct sockaddr_in *)tmp->addr;
			if(tmp_addr->sin_family == AF_INET){
				memcpy(&a4, tmp_addr, sizeof(a4));
				memcpy(&a4.sin_addr,
				       &((struct sockaddr_in *)addr)->sin_addr,
				       sizeof(a4.sin_addr));
				if(!kcl_get_node_by_addr((char *)&a4,
							 sizeof(a4),
							 &node)){
					return node.node_id;
				}
			}
		}
	} else if(addr->sa_family == AF_INET6){
		struct sockaddr_in6 a6;
		struct sockaddr_in6 *tmp_addr;
		list_for_each_entry(tmp, addr_list, list){
			tmp_addr = (struct sockaddr_in6 *)tmp->addr;
			if(tmp_addr->sin6_family == AF_INET6){
				memcpy(&a6, tmp_addr, sizeof(a6));
				memcpy(&a6.sin6_addr,
				       &((struct sockaddr_in6 *)addr)->sin6_addr,
				       sizeof(a6.sin6_addr));
				if(!kcl_get_node_by_addr((char *)&a6,
							 sizeof(a6),
							 &node)){
					return node.node_id;
				}
			}
		}
	}

 fail:
	DMWARN("Failed to convert IP address to nodeid.");
	return 0;
}
