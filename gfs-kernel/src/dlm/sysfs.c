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

#include <linux/ctype.h>
#include <linux/stat.h>

#include "lock_dlm.h"

static ssize_t lm_dlm_block_show(dlm_t *dlm, char *buf)
{
	ssize_t ret;
	int val = 0;

	if (test_bit(DFL_BLOCK_LOCKS, &dlm->flags))
		val = 1;
	ret = sprintf(buf, "%d\n", val);
	return ret;
}

static ssize_t lm_dlm_block_store(dlm_t *dlm, const char *buf, size_t len)
{
	ssize_t ret = len;
	int val;

	val = simple_strtol(buf, NULL, 0);

	if (val == 1)
		set_bit(DFL_BLOCK_LOCKS, &dlm->flags);
	else if (val == 0) {
		clear_bit(DFL_BLOCK_LOCKS, &dlm->flags);
		lm_dlm_submit_delayed(dlm);
	} else
		ret = -EINVAL;
	return ret;
}

static ssize_t lm_dlm_mounted_show(dlm_t *dlm, char *buf)
{
	ssize_t ret;
	int val = -2;

	if (test_bit(DFL_TERMINATE, &dlm->flags))
		val = -1;
	else if (test_bit(DFL_LEAVE_DONE, &dlm->flags))
		val = 0;
	else if (test_bit(DFL_JOIN_DONE, &dlm->flags))
		val = 1;

	ret = sprintf(buf, "%d\n", val);
	return ret;
}

static ssize_t lm_dlm_mounted_store(dlm_t *dlm, const char *buf, size_t len)
{
	ssize_t ret = len;
	int val;

	val = simple_strtol(buf, NULL, 0);

	if (val == 1)
		set_bit(DFL_JOIN_DONE, &dlm->flags);
	else if (val == 0)
		set_bit(DFL_LEAVE_DONE, &dlm->flags);
	else if (val == -1) {
		set_bit(DFL_TERMINATE, &dlm->flags);
		set_bit(DFL_JOIN_DONE, &dlm->flags);
		set_bit(DFL_LEAVE_DONE, &dlm->flags);
	} else
		ret = -EINVAL;

	wake_up(&dlm->wait_control);

	return ret;
}

static ssize_t lm_dlm_jid_show(dlm_t *dlm, char *buf)
{
	return sprintf(buf, "%u\n", dlm->jid);
}

static ssize_t lm_dlm_jid_store(dlm_t *dlm, const char *buf, size_t len)
{
	dlm->jid = simple_strtol(buf, NULL, 0);
	return len;
}

static ssize_t lm_dlm_first_show(dlm_t *dlm, char *buf)
{
	return sprintf(buf, "%u\n", dlm->first);
}

static ssize_t lm_dlm_first_store(dlm_t *dlm, const char *buf, size_t len)
{
	dlm->first = simple_strtol(buf, NULL, 0);
	return len;
}

static ssize_t lm_dlm_recover_show(dlm_t *dlm, char *buf)
{
	return sprintf(buf, "%u\n", dlm->recover_jid);
}

static ssize_t lm_dlm_recover_store(dlm_t *dlm, const char *buf, size_t len)
{
	dlm->recover_jid = simple_strtol(buf, NULL, 0);
	dlm->fscb(dlm->fsdata, LM_CB_NEED_RECOVERY, &dlm->recover_jid);
	return len;
}

static ssize_t lm_dlm_recover_done_show(dlm_t *dlm, char *buf)
{
	ssize_t ret;
	ret = sprintf(buf, "%d\n", dlm->recover_done);
        return ret;
}

static ssize_t lm_dlm_cluster_show(dlm_t *dlm, char *buf)
{
	ssize_t ret;
	ret = sprintf(buf, "%s\n", dlm->clustername);
        return ret;
}



struct lm_dlm_attr {
	struct attribute attr;
	ssize_t (*show)(dlm_t *dlm, char *);
	ssize_t (*store)(dlm_t *dlm, const char *, size_t);
};

static struct lm_dlm_attr lm_dlm_attr_block = {
	.attr  = {.name = "block", .mode = S_IRUGO | S_IWUSR},
	.show  = lm_dlm_block_show,
	.store = lm_dlm_block_store 
};

static struct lm_dlm_attr lm_dlm_attr_mounted = {
	.attr  = {.name = "mounted", .mode = S_IRUGO | S_IWUSR},
	.show  = lm_dlm_mounted_show,
	.store = lm_dlm_mounted_store 
};

static struct lm_dlm_attr lm_dlm_attr_jid = {
	.attr  = {.name = "jid", .mode = S_IRUGO | S_IWUSR},
	.show  = lm_dlm_jid_show,
	.store = lm_dlm_jid_store 
};

static struct lm_dlm_attr lm_dlm_attr_first = {
	.attr  = {.name = "first", .mode = S_IRUGO | S_IWUSR},
	.show  = lm_dlm_first_show,
	.store = lm_dlm_first_store 
};

static struct lm_dlm_attr lm_dlm_attr_recover = {
	.attr  = {.name = "recover", .mode = S_IRUGO | S_IWUSR},
	.show  = lm_dlm_recover_show,
	.store = lm_dlm_recover_store 
};

static struct lm_dlm_attr lm_dlm_attr_recover_done = {
	.attr  = {.name = "recover_done", .mode = S_IRUGO | S_IWUSR},
	.show  = lm_dlm_recover_done_show,
};

static struct lm_dlm_attr lm_dlm_attr_cluster = {
	.attr  = {.name = "cluster", .mode = S_IRUGO | S_IWUSR},
	.show  = lm_dlm_cluster_show,
};

static struct attribute *lm_dlm_attrs[] = {
	&lm_dlm_attr_block.attr,
	&lm_dlm_attr_mounted.attr,
	&lm_dlm_attr_jid.attr,
	&lm_dlm_attr_first.attr,
	&lm_dlm_attr_recover.attr,
	&lm_dlm_attr_recover_done.attr,
	&lm_dlm_attr_cluster.attr,
	NULL,
};

static ssize_t lm_dlm_attr_show(struct kobject *kobj, struct attribute *attr,
			        char *buf)
{
	dlm_t *dlm = container_of(kobj, dlm_t, kobj);
	struct lm_dlm_attr *a = container_of(attr, struct lm_dlm_attr, attr);
	return a->show ? a->show(dlm, buf) : 0;
}

static ssize_t lm_dlm_attr_store(struct kobject *kobj, struct attribute *attr,
			         const char *buf, size_t len)
{
	dlm_t *dlm = container_of(kobj, dlm_t, kobj);
	struct lm_dlm_attr *a = container_of(attr, struct lm_dlm_attr, attr);
	return a->store ? a->store(dlm, buf, len) : len;
}

static struct sysfs_ops lm_dlm_attr_ops = {
	.show  = lm_dlm_attr_show,
	.store = lm_dlm_attr_store,
};

static struct kobj_type lm_dlm_ktype = {
	.default_attrs = lm_dlm_attrs,
	.sysfs_ops     = &lm_dlm_attr_ops,
};

static struct kset lm_dlm_kset = {
	.subsys = &kernel_subsys,
	.kobj   = {.name = "lock_dlm",},
	.ktype  = &lm_dlm_ktype,
};

int lm_dlm_kobject_setup(dlm_t *dlm)
{
	int error;

	error = kobject_set_name(&dlm->kobj, "%s", dlm->fsname);
	if (error)
		return error;

	dlm->kobj.kset = &lm_dlm_kset;
	dlm->kobj.ktype = &lm_dlm_ktype;

	error = kobject_register(&dlm->kobj);

	error = kobject_uevent(&dlm->kobj, KOBJ_ONLINE, NULL);

	return 0;
}

void lm_dlm_kobject_release(dlm_t *dlm)
{
	kobject_unregister(&dlm->kobj);
}

int lm_dlm_sysfs_init(void)
{
	int error;

	error = kset_register(&lm_dlm_kset);
	if (error)
		printk("lock_dlm: cannot register kset %d\n", error);

	return error;
}

void lm_dlm_sysfs_exit(void)
{
	kset_unregister(&lm_dlm_kset);
}

