/*
 * Copyright (C) 2005 Red Hat, Inc.
 *
 * This file is released under the LGPL.
 */

#ifndef __DM_CMIRROR_XFR_H__
#define __DM_CMIRROR_XFR_H__

#define MAX_NAME_LEN 128

#define LRT_IS_CLEAN			0x1
#define LRT_IN_SYNC             	0x2
#define LRT_MARK_REGION         	0x4
#define LRT_GET_RESYNC_WORK     	0x8
#define LRT_GET_SYNC_COUNT      	0x10
#define LRT_CLEAR_REGION        	0x20
#define LRT_COMPLETE_RESYNC_WORK        0x40

#define LRT_ELECTION			0x80
#define LRT_SELECTION			0x100
#define LRT_MASTER_ASSIGN		0x200
#define LRT_MASTER_LEAVING		0x400

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
