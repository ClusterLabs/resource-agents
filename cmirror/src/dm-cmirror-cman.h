/*
 * Copyright (C) 2005 Red Hat, Inc.
 *
 * This file is released under the LGPL.
 */

#ifndef __DM_CMIRROR_CMAN_H__
#define __DM_CMIRROR_CMAN_H__

extern uint32_t local_id;
extern uint32_t my_id;
extern int global_count;
extern uint32_t *global_nodeids;

extern int restart_event_type;
extern int restart_event_id;

uint32_t nodeid_to_ipaddr(uint32_t nodeid);
uint32_t ipaddr_to_nodeid(struct sockaddr *addr);

#endif /* __DM_CMIRROR_CMAN_H__ */
