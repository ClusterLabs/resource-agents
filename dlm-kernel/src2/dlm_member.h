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

#ifndef __DLM_MEMBER_DOT_H__
#define __DLM_MEMBER_DOT_H__

#define DLM_OP_LEN			(16)
#define DLM_LOCKSPACE_LEN		(64)
#define DLM_ADDR_LEN			(64)
#define DLM_MEMBER_MISC_NAME		("dlm_member")

#define DLM_MEMBER_VERSION_MAJOR	(1)
#define DLM_MEMBER_VERSION_MINOR	(0)
#define DLM_MEMBER_VERSION_PATCH	(0)

/*
 * All ioctl arguments consist of a single chunk of memory, with
 * this structure at the start.
 */

struct dlm_member_ioctl {
	__u32	version[3];
	__u32	data_size;	/* total size of data passed in including
				   this struct */
	__u32	data_start;	/* offset to start of data relative to
				   start of this struct */
	__u32	start_event;
	__u32	stop_event;
	__u32	finish_event;
	__u32	startdone_event;
	__u32	node_count;
	__u32	global_id;
	int	nodeid;
	int	weight;

	char	addr[DLM_ADDR_LEN];
	char	op[DLM_OP_LEN];
	char	name[DLM_LOCKSPACE_LEN];
	char	pad[316];	/* make this struct 512 bytes */
};

enum {
	DLM_MEMBER_VERSION_CMD = 0,
	DLM_MEMBER_OP_CMD,
};

#define DLM_IOCTL			(0xd1)

#define DLM_MEMBER_VERSION _IOWR(DLM_IOCTL, DLM_MEMBER_VERSION_CMD, struct dlm_member_ioctl)

#define DLM_MEMBER_OP _IOWR(DLM_IOCTL, DLM_MEMBER_OP_CMD, struct dlm_member_ioctl)

#endif
