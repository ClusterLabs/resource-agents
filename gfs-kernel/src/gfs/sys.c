/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <asm/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <asm/uaccess.h>

#include "gfs.h"
#include "glock.h"
#include "lm.h"
#include "sys.h"
#include "super.h"

char *gfs_sys_margs;
spinlock_t gfs_sys_margs_lock;

static ssize_t id_show(struct gfs_sbd *sdp, char *buf)
{
	return sprintf(buf, "%s\n", sdp->sd_vfs->s_id);
}

static ssize_t fsname_show(struct gfs_sbd *sdp, char *buf)
{
	return sprintf(buf, "%s\n", sdp->sd_fsname);
}

struct gfs_attr {
	struct attribute attr;
	ssize_t (*show)(struct gfs_sbd *, char *);
	ssize_t (*store)(struct gfs_sbd *, const char *, size_t);
};

#define GFS_ATTR(name, mode, show, store) \
static struct gfs_attr gfs_attr_##name = __ATTR(name, mode, show, store)

GFS_ATTR(id,                  0444, id_show,       NULL);
GFS_ATTR(fsname,              0444, fsname_show,   NULL);

static struct attribute *gfs_attrs[] = {
	&gfs_attr_id.attr,
	&gfs_attr_fsname.attr,
	NULL,
};

static ssize_t gfs_attr_show(struct kobject *kobj, struct attribute *attr,
			      char *buf)
{
	struct gfs_sbd *sdp = container_of(kobj, struct gfs_sbd, sd_kobj);
	struct gfs_attr *a = container_of(attr, struct gfs_attr, attr);
	return a->show ? a->show(sdp, buf) : 0;
}

static ssize_t gfs_attr_store(struct kobject *kobj, struct attribute *attr,
			       const char *buf, size_t len)
{
	struct gfs_sbd *sdp = container_of(kobj, struct gfs_sbd, sd_kobj);
	struct gfs_attr *a = container_of(attr, struct gfs_attr, attr);
	return a->store ? a->store(sdp, buf, len) : len;
}

static struct sysfs_ops gfs_attr_ops = {
	.show  = gfs_attr_show,
	.store = gfs_attr_store,
};

static struct kobj_type gfs_ktype = {
	.default_attrs = gfs_attrs,
	.sysfs_ops     = &gfs_attr_ops,
};

static struct kset gfs_kset = {
	.ktype  = &gfs_ktype,
};

int gfs_sys_fs_add(struct gfs_sbd *sdp)
{
	int error;

	sdp->sd_kobj.kset = &gfs_kset;
	sdp->sd_kobj.ktype = &gfs_ktype;

	error = kobject_set_name(&sdp->sd_kobj, "%s", sdp->sd_table_name);
	if (error)
		goto fail;

	error = kobject_register(&sdp->sd_kobj);
	if (error)
		goto fail;

	return 0;

 fail:
	return error;
}

void gfs_sys_fs_del(struct gfs_sbd *sdp)
{
	kobject_unregister(&sdp->sd_kobj);
}

int gfs_sys_init(void)
{
	gfs_sys_margs = NULL;
	spin_lock_init(&gfs_sys_margs_lock);
	kobject_set_name(&gfs_kset.kobj, "gfs");
	kobj_set_kset_s(&gfs_kset, fs_subsys);
	return kset_register(&gfs_kset);
}

void gfs_sys_uninit(void)
{
	kfree(gfs_sys_margs);
	kset_unregister(&gfs_kset);
}
