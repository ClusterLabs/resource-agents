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


static ssize_t dlm_control_store(struct dlm_ls *ls, const char *buf, size_t len)
{
	ssize_t ret = len;
	int n = simple_strtol(buf, NULL, 0);

	switch (n) {
	case 0:
		dlm_ls_stop(ls);
		break;
	case 1:
		dlm_ls_start(ls);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static ssize_t dlm_event_store(struct dlm_ls *ls, const char *buf, size_t len)
{
	ls->ls_uevent_result = simple_strtol(buf, NULL, 0);
	set_bit(LSFL_UEVENT_WAIT, &ls->ls_flags);
	wake_up(&ls->ls_uevent_wait);
	return len;
}

struct dlm_attr {
	struct attribute attr;
	ssize_t (*show)(struct dlm_ls *, char *);
	ssize_t (*store)(struct dlm_ls *, const char *, size_t);
};

static struct dlm_attr dlm_attr_control = {
	.attr  = {.name = "control", .mode = S_IWUSR},
	.store = dlm_control_store
};

static struct dlm_attr dlm_attr_event = {
	.attr  = {.name = "event_done", .mode = S_IWUSR},
	.store = dlm_event_store
};

static struct attribute *dlm_attrs[] = {
	&dlm_attr_control.attr,
	&dlm_attr_event.attr,
	NULL,
};

static ssize_t dlm_attr_show(struct kobject *kobj, struct attribute *attr,
			     char *buf)
{
	struct dlm_ls *ls  = container_of(kobj, struct dlm_ls, ls_kobj);
	struct dlm_attr *a = container_of(attr, struct dlm_attr, attr);
	return a->show ? a->show(ls, buf) : 0;
}

static ssize_t dlm_attr_store(struct kobject *kobj, struct attribute *attr,
			      const char *buf, size_t len)
{
	struct dlm_ls *ls  = container_of(kobj, struct dlm_ls, ls_kobj);
	struct dlm_attr *a = container_of(attr, struct dlm_attr, attr);
	return a->store ? a->store(ls, buf, len) : len;
}

static struct sysfs_ops dlm_attr_ops = {
	.show  = dlm_attr_show,
	.store = dlm_attr_store,
};

static struct kobj_type dlm_ktype = {
	.default_attrs = dlm_attrs,
	.sysfs_ops     = &dlm_attr_ops,
};

static struct kset dlm_kset = {
	.subsys = &kernel_subsys,
	.kobj   = {.name = "dlm",},
	.ktype  = &dlm_ktype,
};

int dlm_member_sysfs_init(void)
{
	int error;

	error = kset_register(&dlm_kset);
	if (error)
		printk("dlm_lockspace_init: cannot register kset %d\n", error);
	return error;
}

void dlm_member_sysfs_exit(void)
{
	kset_unregister(&dlm_kset);
}

int dlm_kobject_setup(struct dlm_ls *ls)
{
	char lsname[DLM_LOCKSPACE_LEN];
	int error;

	memset(lsname, 0, DLM_LOCKSPACE_LEN);
	snprintf(lsname, DLM_LOCKSPACE_LEN, "%s", ls->ls_name);

	error = kobject_set_name(&ls->ls_kobj, "%s", lsname);
	if (error)
		return error;

	ls->ls_kobj.kset = &dlm_kset;
	ls->ls_kobj.ktype = &dlm_ktype;
	return 0;
}

int dlm_uevent(struct dlm_ls *ls, int in)
{
	int error;

	if (in)
		kobject_uevent(&ls->ls_kobj, KOBJ_ONLINE, NULL);
	else
		kobject_uevent(&ls->ls_kobj, KOBJ_OFFLINE, NULL);

	error = wait_event_interruptible(ls->ls_uevent_wait,
			test_and_clear_bit(LSFL_UEVENT_WAIT, &ls->ls_flags));
	if (error)
		goto out;

	error = ls->ls_uevent_result;
 out:
	return error;
}

