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

#include <linux/miscdevice.h>
#include <linux/fs.h>

#include "dlm_internal.h"
#include "member.h"


static int check_version(unsigned int cmd,
			 struct dlm_node_ioctl __user *u_param)
{
	u32 version[3];
	int error = 0;

	if (copy_from_user(version, u_param->version, sizeof(version)))
		return -EFAULT;

	if ((DLM_NODE_VERSION_MAJOR != version[0]) ||
	    (DLM_NODE_VERSION_MINOR < version[1])) {
		printk("dlm node_ioctl: interface mismatch: "
		       "kernel(%u.%u.%u), user(%u.%u.%u), cmd(%d)\n",
		       DLM_NODE_VERSION_MAJOR,
		       DLM_NODE_VERSION_MINOR,
		       DLM_NODE_VERSION_PATCH,
		       version[0], version[1], version[2], cmd);
		error = -EINVAL;
	}

	version[0] = DLM_NODE_VERSION_MAJOR;
	version[1] = DLM_NODE_VERSION_MINOR;
	version[2] = DLM_NODE_VERSION_PATCH;

	if (copy_to_user(u_param->version, version, sizeof(version)))
		return -EFAULT;
	return error;
}

static int node_ioctl(struct inode *inode, struct file *file,
	              uint command, ulong u)
{
	struct dlm_node_ioctl *k_param;
	struct dlm_node_ioctl __user *u_param;
	unsigned int cmd, type;
	int error;

	u_param = (struct dlm_node_ioctl __user *) u;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	type = _IOC_TYPE(command);
	cmd = _IOC_NR(command);

	if (type != DLM_IOCTL) {
		printk("dlm node_ioctl: bad ioctl 0x%x 0x%x 0x%x\n",
		       command, type, cmd);
		return -ENOTTY;
	}

	error = check_version(cmd, u_param);
	if (error)
		return error;

	if (cmd == DLM_NODE_VERSION_CMD)
		return 0;

	k_param = kmalloc(sizeof(*k_param), GFP_KERNEL);
	if (!k_param)
		return -ENOMEM;

	if (copy_from_user(k_param, u_param, sizeof(*k_param))) {
		kfree(k_param);
		return -EFAULT;
	}

	if (cmd == DLM_SET_NODE_CMD)
		error = dlm_set_node(k_param->nodeid, k_param->weight,
				     k_param->addr);
	else if (cmd == DLM_SET_LOCAL_CMD)
		error = dlm_set_local(k_param->nodeid, k_param->weight,
				      k_param->addr);

	kfree(k_param);
	return error;
}

static struct file_operations node_fops = {
	.ioctl	= node_ioctl,
	.owner	= THIS_MODULE,
};

static struct miscdevice node_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= DLM_NODE_MISC_NAME,
	.fops	= &node_fops
};

int dlm_node_ioctl_init(void)
{
	int error;

	error = misc_register(&node_misc);
	if (error)
		printk("dlm node_ioctl: misc_register failed %d\n", error);
	return error;
}

void dlm_node_ioctl_exit(void)
{
	if (misc_deregister(&node_misc) < 0)
		printk("dlm node_ioctl: misc_deregister failed\n");
}

