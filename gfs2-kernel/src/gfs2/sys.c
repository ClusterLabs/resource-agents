/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <asm/semaphore.h>
#include <asm/uaccess.h>

#include "gfs2.h"
#include "lm.h"
#include "sys.h"
#include "super.h"

char *gfs2_sys_margs;
spinlock_t gfs2_sys_margs_lock;

static ssize_t gfs2_id_show(struct gfs2_sbd *sdp, char *buf)
{
	return sprintf(buf, "%s\n", sdp->sd_vfs->s_id);
}

static ssize_t gfs2_fsname_show(struct gfs2_sbd *sdp, char *buf)
{
	return sprintf(buf, "%s\n", sdp->sd_fsname);
}

static ssize_t gfs2_freeze_show(struct gfs2_sbd *sdp, char *buf)
{
	unsigned int count;

	down(&sdp->sd_freeze_lock);
	count = sdp->sd_freeze_count;
	up(&sdp->sd_freeze_lock);

	return sprintf(buf, "%u\n", count);
}

static ssize_t gfs2_freeze_store(struct gfs2_sbd *sdp, const char *buf,
				 size_t len)
{
	ssize_t ret = len;
	int error = 0;
	int n = simple_strtol(buf, NULL, 0);

	switch (n) {
	case 0:
		gfs2_unfreeze_fs(sdp);
		break;
	case 1:
		error = gfs2_freeze_fs(sdp);
		break;
	default:
		ret = -EINVAL;
	}

	if (error)
		fs_warn(sdp, "freeze %d error %d", n, error);

	return ret;
}

static ssize_t gfs2_withdraw_show(struct gfs2_sbd *sdp, char *buf)
{
	unsigned int b = test_bit(SDF_SHUTDOWN, &sdp->sd_flags);
	return sprintf(buf, "%u\n", b);
}

static ssize_t gfs2_withdraw_store(struct gfs2_sbd *sdp, const char *buf,
				   size_t len)
{
	ssize_t ret = len;
	int n = simple_strtol(buf, NULL, 0);

	if (n != 1) {
		ret = -EINVAL;
		goto out;
	}

	gfs2_lm_withdraw(sdp,
		"GFS2: fsid=%s: withdrawing from cluster at user's request\n",
		sdp->sd_fsname);
 out:
	return ret;
}

struct gfs2_attr {
	struct attribute attr;
	ssize_t (*show)(struct gfs2_sbd*, char *);
	ssize_t (*store)(struct gfs2_sbd*, const char *, size_t);
};

static struct gfs2_attr gfs2_attr_id = {
	.attr = {.name = "id", .mode = S_IRUSR},
	.show = gfs2_id_show
};

static struct gfs2_attr gfs2_attr_fsname = {
	.attr = {.name = "fsname", .mode = S_IRUSR},
	.show = gfs2_fsname_show
};

static struct gfs2_attr gfs2_attr_freeze = {
	.attr  = {.name = "freeze", .mode = S_IRUSR | S_IWUSR},
	.show  = gfs2_freeze_show,
	.store = gfs2_freeze_store
};

static struct gfs2_attr gfs2_attr_withdraw = {
	.attr  = {.name = "withdraw", .mode = S_IRUSR | S_IWUSR},
	.show  = gfs2_withdraw_show,
	.store = gfs2_withdraw_store
};

static struct attribute *gfs2_attrs[] = {
	&gfs2_attr_id.attr,
	&gfs2_attr_fsname.attr,
	&gfs2_attr_freeze.attr,
	&gfs2_attr_withdraw.attr,
	NULL,
};

static ssize_t gfs2_attr_show(struct kobject *kobj, struct attribute *attr,
			      char *buf)
{
	struct gfs2_sbd *sdp = container_of(kobj, struct gfs2_sbd, sd_kobj);
	struct gfs2_attr *a = container_of(attr, struct gfs2_attr, attr);
	return a->show ? a->show(sdp, buf) : 0;
}

static ssize_t gfs2_attr_store(struct kobject *kobj, struct attribute *attr,
			       const char *buf, size_t len)
{
	struct gfs2_sbd *sdp = container_of(kobj, struct gfs2_sbd, sd_kobj);
	struct gfs2_attr *a = container_of(attr, struct gfs2_attr, attr);
	return a->store ? a->store(sdp, buf, len) : len;
}

static struct sysfs_ops gfs2_attr_ops = {
	.show  = gfs2_attr_show,
	.store = gfs2_attr_store,
};

static struct kobj_type gfs2_ktype = {
	.default_attrs = gfs2_attrs,
	.sysfs_ops     = &gfs2_attr_ops,
};

/* FIXME: should this go under /sys/fs/ ? */

static struct kset gfs2_kset = {
	.subsys = &kernel_subsys,
	.kobj   = {.name = "gfs2",},
	.ktype  = &gfs2_ktype,
};

int gfs2_sys_fs_add(struct gfs2_sbd *sdp)
{
	int error;

	error = kobject_set_name(&sdp->sd_kobj, "%s", sdp->sd_fsname);
	if (error)
		goto out;

	sdp->sd_kobj.kset = &gfs2_kset;
	sdp->sd_kobj.ktype = &gfs2_ktype;

	error = kobject_register(&sdp->sd_kobj);
 out:
	return error;
}

void gfs2_sys_fs_del(struct gfs2_sbd *sdp)
{
	kobject_unregister(&sdp->sd_kobj);
}

int gfs2_sys_init(void)
{
	gfs2_sys_margs = NULL;
	spin_lock_init(&gfs2_sys_margs_lock);
	return kset_register(&gfs2_kset);
}

void gfs2_sys_uninit(void)
{
	kfree(gfs2_sys_margs);
	kset_unregister(&gfs2_kset);
}

