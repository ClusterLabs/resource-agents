/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Bastian Blank <waldi@debian.org>
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <linux/module.h>
#include <linux/compat.h>
#include <linux/ioctl32.h>
#include <linux/syscalls.h>
#include <net/sock.h>

#include <cluster/cnxman.h>
#include <cluster/service.h>

#include "cnxman-private.h"

static int do_ioctl32_pointer(unsigned int fd, unsigned int cmd, unsigned long arg, struct file *f)
{
	return sys_ioctl(fd, cmd, (unsigned long)compat_ptr(arg));
}

static int do_ioctl32_ulong(unsigned int fd, unsigned int cmd, unsigned long arg, struct file *f)
{
	return sys_ioctl(fd, cmd, arg);
}

struct ioctl32_cl_cluster_nodelist {
	uint32_t max_members;
	uint32_t nodes;
};

#define IOCTL32_SIOCCLUSTER_GETMEMBERS		_IOR('x', 0x03, struct ioctl32_cl_cluster_nodelist)
#define IOCTL32_SIOCCLUSTER_GETALLMEMBERS	_IOR('x', 0x07, struct ioctl32_cl_cluster_nodelist)
#define IOCTL32_SIOCCLUSTER_SERVICE_GETMEMBERS	_IOR('x', 0x60, struct ioctl32_cl_cluster_nodelist)

static int do_ioctl32_cl_cluster_nodelist(unsigned int fd, unsigned int cmd, unsigned long arg, struct file *f)
{
	struct ioctl32_cl_cluster_nodelist i32;
	struct cl_cluster_nodelist i64, __user *p64 = NULL;
	unsigned int ncmd, r;

	if (arg)
	{
		if (copy_from_user(&i32, compat_ptr(arg), sizeof(struct ioctl32_cl_cluster_nodelist)))
			return -EFAULT;
		r = copy_from_user(&i32, compat_ptr(arg), sizeof(struct ioctl32_cl_cluster_nodelist));

		i64.max_members = i32.max_members;
		i64.nodes = compat_ptr(i32.nodes);

		p64 = compat_alloc_user_space(sizeof(struct cl_cluster_nodelist));
		if (copy_to_user(p64, &i64, sizeof(struct cl_cluster_nodelist)))
			return -EFAULT;
	}

	switch(cmd)
	{
		case IOCTL32_SIOCCLUSTER_GETMEMBERS:
			ncmd = SIOCCLUSTER_GETMEMBERS;
			break;
		case IOCTL32_SIOCCLUSTER_GETALLMEMBERS:
			ncmd = SIOCCLUSTER_GETALLMEMBERS;
			break;
		case IOCTL32_SIOCCLUSTER_SERVICE_GETMEMBERS:
			ncmd = SIOCCLUSTER_SERVICE_GETMEMBERS;
			break;
		default:
			return -EINVAL;
	}

	return sys_ioctl(fd, ncmd, (unsigned long)p64);
}

#define IOCTL32_SIOCCLUSTER_SET_NODENAME	_IOW('x', 0xb1, uint32_t)

static int do_ioctl32_char_p(unsigned int fd, unsigned int cmd, unsigned long arg, struct file *f)
{
	unsigned int ncmd;

	switch(cmd)
	{
		case IOCTL32_SIOCCLUSTER_SET_NODENAME:
			ncmd = SIOCCLUSTER_SET_NODENAME;
			break;
		default:
			return -EINVAL;
	}

	return sys_ioctl(fd, ncmd, (unsigned long)compat_ptr(arg));
}

void __init cnxman_ioctl32_init(void)
{
	register_ioctl32_conversion(SIOCCLUSTER_NOTIFY, do_ioctl32_ulong);
	register_ioctl32_conversion(SIOCCLUSTER_REMOVENOTIFY, do_ioctl32_ulong);
	register_ioctl32_conversion(SIOCCLUSTER_GET_VERSION, do_ioctl32_pointer);
	register_ioctl32_conversion(SIOCCLUSTER_SET_VERSION, do_ioctl32_pointer);
	register_ioctl32_conversion(IOCTL32_SIOCCLUSTER_GETMEMBERS, do_ioctl32_cl_cluster_nodelist);
	register_ioctl32_conversion(IOCTL32_SIOCCLUSTER_GETALLMEMBERS, do_ioctl32_cl_cluster_nodelist);
	register_ioctl32_conversion(SIOCCLUSTER_GETNODE, do_ioctl32_pointer);
	register_ioctl32_conversion(SIOCCLUSTER_GETCLUSTER, do_ioctl32_pointer);
	register_ioctl32_conversion(SIOCCLUSTER_ISQUORATE, do_ioctl32_ulong);
	register_ioctl32_conversion(SIOCCLUSTER_ISACTIVE, do_ioctl32_ulong);
	register_ioctl32_conversion(SIOCCLUSTER_SETEXPECTED_VOTES, do_ioctl32_ulong);
	register_ioctl32_conversion(SIOCCLUSTER_SET_VOTES, do_ioctl32_ulong);
	register_ioctl32_conversion(SIOCCLUSTER_ISLISTENING, do_ioctl32_pointer);
	register_ioctl32_conversion(SIOCCLUSTER_KILLNODE, do_ioctl32_ulong);
	register_ioctl32_conversion(SIOCCLUSTER_GET_JOINCOUNT, do_ioctl32_ulong);
//	register_ioctl32_conversion(SIOCCLUSTER_BARRIER
	register_ioctl32_conversion(SIOCCLUSTER_PASS_SOCKET, do_ioctl32_pointer);
	register_ioctl32_conversion(IOCTL32_SIOCCLUSTER_SET_NODENAME, do_ioctl32_char_p);
	register_ioctl32_conversion(SIOCCLUSTER_SET_NODEID, do_ioctl32_ulong);
	register_ioctl32_conversion(SIOCCLUSTER_JOIN_CLUSTER, do_ioctl32_pointer);
	register_ioctl32_conversion(SIOCCLUSTER_LEAVE_CLUSTER, do_ioctl32_ulong);
	register_ioctl32_conversion(SIOCCLUSTER_SERVICE_REGISTER, do_ioctl32_pointer);
	register_ioctl32_conversion(SIOCCLUSTER_SERVICE_UNREGISTER, do_ioctl32_ulong);
	register_ioctl32_conversion(SIOCCLUSTER_SERVICE_JOIN, do_ioctl32_ulong);
	register_ioctl32_conversion(SIOCCLUSTER_SERVICE_LEAVE, do_ioctl32_ulong);
	register_ioctl32_conversion(SIOCCLUSTER_SERVICE_SETSIGNAL, do_ioctl32_ulong);
	register_ioctl32_conversion(SIOCCLUSTER_SERVICE_STARTDONE, do_ioctl32_ulong);
	register_ioctl32_conversion(SIOCCLUSTER_SERVICE_GETEVENT, do_ioctl32_pointer);
	register_ioctl32_conversion(IOCTL32_SIOCCLUSTER_SERVICE_GETMEMBERS, do_ioctl32_cl_cluster_nodelist);
	register_ioctl32_conversion(SIOCCLUSTER_SERVICE_GLOBALID, do_ioctl32_pointer);
	register_ioctl32_conversion(SIOCCLUSTER_SERVICE_SETLEVEL, do_ioctl32_ulong);
}

void __exit cnxman_ioctl32_exit(void)
{
	unregister_ioctl32_conversion(SIOCCLUSTER_NOTIFY);
	unregister_ioctl32_conversion(SIOCCLUSTER_REMOVENOTIFY);
	unregister_ioctl32_conversion(SIOCCLUSTER_GET_VERSION);
	unregister_ioctl32_conversion(SIOCCLUSTER_SET_VERSION);
	unregister_ioctl32_conversion(IOCTL32_SIOCCLUSTER_GETMEMBERS);
	unregister_ioctl32_conversion(IOCTL32_SIOCCLUSTER_GETALLMEMBERS);
	unregister_ioctl32_conversion(SIOCCLUSTER_GETNODE);
	unregister_ioctl32_conversion(SIOCCLUSTER_GETCLUSTER);
	unregister_ioctl32_conversion(SIOCCLUSTER_ISQUORATE);
	unregister_ioctl32_conversion(SIOCCLUSTER_ISACTIVE);
	unregister_ioctl32_conversion(SIOCCLUSTER_SETEXPECTED_VOTES);
	unregister_ioctl32_conversion(SIOCCLUSTER_SET_VOTES);
	unregister_ioctl32_conversion(SIOCCLUSTER_ISLISTENING);
	unregister_ioctl32_conversion(SIOCCLUSTER_KILLNODE);
	unregister_ioctl32_conversion(SIOCCLUSTER_GET_JOINCOUNT);
//	unregister_ioctl32_conversion(SIOCCLUSTER_BARRIER);
	unregister_ioctl32_conversion(SIOCCLUSTER_PASS_SOCKET);
	unregister_ioctl32_conversion(IOCTL32_SIOCCLUSTER_SET_NODENAME);
	unregister_ioctl32_conversion(SIOCCLUSTER_SET_NODEID);
	unregister_ioctl32_conversion(SIOCCLUSTER_JOIN_CLUSTER);
	unregister_ioctl32_conversion(SIOCCLUSTER_LEAVE_CLUSTER);
	unregister_ioctl32_conversion(SIOCCLUSTER_SERVICE_REGISTER);
	unregister_ioctl32_conversion(SIOCCLUSTER_SERVICE_UNREGISTER);
	unregister_ioctl32_conversion(SIOCCLUSTER_SERVICE_JOIN);
	unregister_ioctl32_conversion(SIOCCLUSTER_SERVICE_LEAVE);
	unregister_ioctl32_conversion(SIOCCLUSTER_SERVICE_SETSIGNAL);
	unregister_ioctl32_conversion(SIOCCLUSTER_SERVICE_STARTDONE);
	unregister_ioctl32_conversion(SIOCCLUSTER_SERVICE_GETEVENT);
	unregister_ioctl32_conversion(IOCTL32_SIOCCLUSTER_SERVICE_GETMEMBERS);
	unregister_ioctl32_conversion(SIOCCLUSTER_SERVICE_GLOBALID);
	unregister_ioctl32_conversion(SIOCCLUSTER_SERVICE_SETLEVEL);
}

