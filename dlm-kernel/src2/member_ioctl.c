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

#include "dlm_internal.h"
#include "member.h"

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>


typedef int (*ioctl_fn)(struct dlm_member_ioctl *param);
static ioctl_fn lookup_fn(char *name);

static void free_params(struct dlm_member_ioctl *param)
{
	vfree(param);
}

static int copy_params(struct dlm_member_ioctl __user *u_param,
		       struct dlm_member_ioctl **param)
{
	struct dlm_member_ioctl *k_param, *v_param;
	int error = 0, size = sizeof(struct dlm_member_ioctl);

	k_param = kmalloc(size, GFP_KERNEL);
	if (!k_param)
		return -ENOMEM;

	if (copy_from_user(k_param, u_param, size)) {
		error = -EFAULT;
		goto out;
	}

	if (k_param->data_size < size) {
		error = -EINVAL;
		goto out;
	}

	v_param = vmalloc(k_param->data_size);
	if (!v_param) {
		error = -ENOMEM;
		goto out;
	}

	if (copy_from_user(v_param, u_param, k_param->data_size)) {
		vfree(v_param);
		error = -EFAULT;
		goto out;
	}

	*param = v_param;
 out:
	kfree(k_param);
	return error;
}

static int validate_params(struct dlm_member_ioctl *param)
{
	param->op[DLM_OP_LEN - 1] = '\0';

	if (!strcmp(param->op, "set_local")) {
		if (param->data_size != sizeof(struct dlm_member_ioctl))
			return -EINVAL;
	} else if (!strcmp(param->op, "set_node")) {
		if (param->data_size != sizeof(struct dlm_member_ioctl))
			return -EINVAL;
	}
	return 0;
}

static int check_version(unsigned int cmd,
			 struct dlm_member_ioctl __user *u_param)
{
	u32 version[3];
	int error = 0;

	if (copy_from_user(version, u_param->version, sizeof(version)))
		return -EFAULT;

	if ((DLM_MEMBER_VERSION_MAJOR != version[0]) ||
	    (DLM_MEMBER_VERSION_MINOR < version[1])) {
		printk("dlm member_ioctl: interface mismatch: "
		       "kernel(%u.%u.%u), user(%u.%u.%u), cmd(%d)\n",
		       DLM_MEMBER_VERSION_MAJOR,
		       DLM_MEMBER_VERSION_MINOR,
		       DLM_MEMBER_VERSION_PATCH,
		       version[0], version[1], version[2], cmd);
		error = -EINVAL;
	}

	version[0] = DLM_MEMBER_VERSION_MAJOR;
	version[1] = DLM_MEMBER_VERSION_MINOR;
	version[2] = DLM_MEMBER_VERSION_PATCH;

	if (copy_to_user(u_param->version, version, sizeof(version)))
		return -EFAULT;
	return error;
}

static struct op_functions {
	char *op;
	ioctl_fn fn;
} opfn[] = {
	{"set_node", dlm_set_node},
	{"set_local", dlm_set_local},
};

static ioctl_fn lookup_fn(char *name)
{
	int i, n = sizeof(opfn) / sizeof(struct op_functions);

	for (i = 0; i < n; i++) {
		if (!strncmp(name, opfn[i].op, strlen(opfn[i].op)))
			return opfn[i].fn;
	}
	return NULL;
}

static int member_ioctl(struct inode *inode, struct file *file,
			uint command, ulong u)
{
	struct dlm_member_ioctl *param;
	struct dlm_member_ioctl __user *u_param;
	unsigned int cmd, type;
	ioctl_fn fn = NULL;
	int error;

	u_param = (struct dlm_member_ioctl __user *) u;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	type = _IOC_TYPE(command);
	cmd = _IOC_NR(command);

	if (type != DLM_IOCTL || cmd > DLM_MEMBER_OP_CMD) {
		printk("dlm member_ioctl: bad command 0x%x 0x%x 0x%x\n",
		       command, type, cmd);
		return -ENOTTY;
	}

	error = check_version(cmd, u_param);
	if (error)
		return error;

	if (cmd == DLM_MEMBER_VERSION_CMD)
		return 0;

	error = copy_params(u_param, &param);
	if (error)
		return error;

	error = validate_params(param);
	if (error)
		goto out;

	fn = lookup_fn(param->op);
	if (!fn) {
		printk("dlm member_ioctl: unknown op \"%s\"\n", param->op);
		return -ENOTTY;
	}

	error = fn(param);
	if (error) {
		printk("dlm member_ioctl: %s error %d\n", param->op, error);
		goto out;
	}

	if (copy_to_user(u_param, param, sizeof(struct dlm_member_ioctl)))
		error = -EFAULT;
 out:
	free_params(param);
	return error;
}

static struct file_operations member_fops = {
	.ioctl	= member_ioctl,
	.owner	= THIS_MODULE,
};

static struct miscdevice member_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= DLM_MEMBER_MISC_NAME,
	.fops	= &member_fops
};

int dlm_member_ioctl_init(void)
{
	int error;

	error = misc_register(&member_misc);
	if (error)
		printk("dlm member_ioctl: misc_register failed %d\n", error);
	return error;
}

void dlm_member_ioctl_exit(void)
{
	if (misc_deregister(&member_misc) < 0)
		printk("dlm member_ioctl: misc_deregister failed\n");
}

