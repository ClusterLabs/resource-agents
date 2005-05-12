/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __DLM_NODE_DOT_H__
#define __DLM_NODE_DOT_H__

#define DLM_ADDR_LEN			(256)
#define DLM_MAX_ADDR_COUNT		(3)
#define DLM_NODE_MISC_NAME		("dlm-node")

#define DLM_NODE_VERSION_MAJOR		(1)
#define DLM_NODE_VERSION_MINOR		(0)
#define DLM_NODE_VERSION_PATCH		(0)

struct dlm_node_ioctl {
	__u32	version[3];
	int	nodeid;
	int	weight;
	char	addr[DLM_ADDR_LEN];
};

enum {
	DLM_NODE_VERSION_CMD = 0,
	DLM_SET_NODE_CMD,
	DLM_SET_LOCAL_CMD,
};

#define DLM_IOCTL			(0xd1)

#define DLM_NODE_VERSION _IOWR(DLM_IOCTL, DLM_NODE_VERSION_CMD, struct dlm_node_ioctl)
#define DLM_SET_NODE     _IOWR(DLM_IOCTL, DLM_SET_NODE_CMD, struct dlm_node_ioctl)
#define DLM_SET_LOCAL    _IOWR(DLM_IOCTL, DLM_SET_LOCAL_CMD, struct dlm_node_ioctl)

#endif

