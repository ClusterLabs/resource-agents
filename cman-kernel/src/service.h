/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**  
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __SERVICE_DOT_H__
#define __SERVICE_DOT_H__

/* 
 * Interface between service manager and services
 */

/* 
 * Service levels are started in order from lowest, so level 0 is started on
 * all nodes before level 1 is started.
 */

#define SERVICE_LEVEL_FENCE      (0)
#define SERVICE_LEVEL_GDLM       (1)
#define SERVICE_LEVEL_GFS        (2)
#define SERVICE_LEVEL_USER	 (3)

#define MAX_SERVICE_NAME_LEN     (33)

/* 
 * The type of start a service receives.  The start (and preceding stop) may be
 * due to a node joining or leaving the SG or due to a node having failed.
 */

#define SERVICE_NODE_FAILED      (1)
#define SERVICE_NODE_JOIN        (2)
#define SERVICE_NODE_LEAVE       (3)


struct kcl_service {
	struct list_head list;
	uint16_t level;
	uint32_t local_id;
	uint32_t global_id;
	int node_count;
	char name[MAX_SERVICE_NAME_LEN];
};

int kcl_get_services(struct list_head *list, int level);


/* 
 * These routines which run in CMAN context must return quickly and cannot
 * block.
 */

struct kcl_service_ops {
	int (*stop) (void *servicedata);
	int (*start) (void *servicedata, uint32_t *nodeids, int count,
		      int event_id, int type);
	void (*finish) (void *servicedata, int event_id);
};

/* 
 * Register will cause CMAN to create a Service Group (SG) for the named
 * instance of the service.  A local ID is returned which is used to join,
 * leave and unregister the service.
 */

int kcl_register_service(char *name, int namelen, int level,
			 struct kcl_service_ops *ops, int unique,
			 void *servicedata, uint32_t *local_id);

void kcl_unregister_service(uint32_t local_id);

/* 
 * Once a service is joined it will be managed by CMAN and receive start, stop,
 * and finish calls.  After leave is called the service is no longer managed by
 * CMAN.  The first start for a service may arrive before kcl_join_service()
 * returns.
 */

int kcl_join_service(uint32_t local_id);
int kcl_leave_service(uint32_t local_id);

/* 
 * After a service is started, it can ask for its cluster-wide unique ID.
 */

void kcl_global_service_id(uint32_t local_id, uint32_t * global_id);

/* 
 * Called by a service when it's done with a start().  Cannot be called from
 * the start function.
 */

void kcl_start_done(uint32_t local_id, int event_id);

#endif
