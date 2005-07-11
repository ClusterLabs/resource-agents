/*
 * Copyright (C) 2005 Red Hat, Inc.
 *
 * This file is released under the LGPL.
 */

#ifndef __DM_CMIRROR_XFR_H__
#define __DM_CMIRROR_XFR_H__

#define MAX_NAME_LEN 128

#define LRT_IS_CLEAN			1
#define LRT_IS_REMOTE_RECOVERING	2
#define LRT_IN_SYNC             	3
#define LRT_MARK_REGION         	4
#define LRT_CLEAR_REGION        	5
#define LRT_GET_RESYNC_WORK     	6
#define LRT_COMPLETE_RESYNC_WORK        7
#define LRT_GET_SYNC_COUNT      	8

#define LRT_ELECTION			9
#define LRT_SELECTION			10
#define LRT_MASTER_ASSIGN		11
#define LRT_MASTER_LEAVING		12

#define CLUSTER_LOG_PORT 51005

struct log_request {
	int lr_type;
	union {
		struct {
			uint32_t lr_starter;
			int lr_starter_port;
			uint32_t lr_node_count;
			uint32_t lr_coordinator;
		};
		struct {
			int lr_int_rtn;          /* use this if int return */
			region_t lr_region_rtn;  /* use this if region_t return */
			sector_t lr_region;
		};
	} u;
	char lr_uuid[MAX_NAME_LEN];
};

int my_recvmsg(struct socket *sock, struct msghdr *msg,
	       size_t size, int flags, int time_out);

#endif /* __DM_CMIRROR_XFR_H__ */
