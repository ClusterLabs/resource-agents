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

struct list_head gfs_fs_list;
struct semaphore gfs_fs_lock;
char *gfs_sys_margs;
spinlock_t gfs_sys_margs_lock;
spinlock_t req_lock;

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

struct lockstruct_attr {
	struct attribute attr;
	ssize_t (*show)(struct gfs_sbd *, char *);
};

#define LOCKSTRUCT_ATTR(name, fmt)                                          \
static ssize_t name##_show(struct gfs_sbd *sdp, char *buf)                 \
{                                                                           \
	return sprintf(buf, fmt, sdp->sd_lockstruct.ls_##name);             \
}                                                                           \
static struct lockstruct_attr lockstruct_attr_##name = __ATTR_RO(name)

LOCKSTRUCT_ATTR(jid,      "%u\n");
LOCKSTRUCT_ATTR(first,    "%u\n");
LOCKSTRUCT_ATTR(lvb_size, "%u\n");
LOCKSTRUCT_ATTR(flags,    "%d\n");

static struct attribute *lockstruct_attrs[] = {
	&lockstruct_attr_jid.attr,
	&lockstruct_attr_first.attr,
	&lockstruct_attr_lvb_size.attr,
	&lockstruct_attr_flags.attr,
	NULL
};

/*
 * display struct gfs_args fields
 */

struct args_attr {
	struct attribute attr;
	ssize_t (*show)(struct gfs_sbd *, char *);
};

#define ARGS_ATTR(name, fmt)                                                \
static ssize_t name##_show(struct gfs_sbd *sdp, char *buf)                 \
{                                                                           \
	return sprintf(buf, fmt, sdp->sd_args.ar_##name);                   \
}                                                                           \
static struct args_attr args_attr_##name = __ATTR_RO(name)

ARGS_ATTR(lockproto,       "%s\n");
ARGS_ATTR(locktable,       "%s\n");
ARGS_ATTR(hostdata,        "%s\n");
ARGS_ATTR(spectator,       "%d\n");
ARGS_ATTR(ignore_local_fs, "%d\n");
ARGS_ATTR(localcaching,    "%d\n");
ARGS_ATTR(localflocks,     "%d\n");
ARGS_ATTR(debug,           "%d\n");
ARGS_ATTR(upgrade,         "%d\n");
ARGS_ATTR(num_glockd,      "%u\n");
ARGS_ATTR(suiddir,         "%d\n");

static struct attribute *args_attrs[] = {
	&args_attr_lockproto.attr,
	&args_attr_locktable.attr,
	&args_attr_hostdata.attr,
	&args_attr_spectator.attr,
	&args_attr_ignore_local_fs.attr,
	&args_attr_localcaching.attr,
	&args_attr_localflocks.attr,
	&args_attr_debug.attr,
	&args_attr_upgrade.attr,
	&args_attr_num_glockd.attr,
	&args_attr_suiddir.attr,
	NULL
};

/*
 * display counters from superblock
 */

struct counters_attr {
	struct attribute attr;
	ssize_t (*show)(struct gfs_sbd *, char *);
};

#define COUNTERS_ATTR(name, fmt)                                            \
static ssize_t name##_show(struct gfs_sbd *sdp, char *buf)                 \
{                                                                           \
	return sprintf(buf, fmt, (unsigned int)atomic_read(&sdp->sd_##name)); \
}                                                                           \
static struct counters_attr counters_attr_##name = __ATTR_RO(name)

COUNTERS_ATTR(glock_count,      "%u\n");
COUNTERS_ATTR(glock_held_count, "%u\n");
COUNTERS_ATTR(inode_count,      "%u\n");
COUNTERS_ATTR(reclaimed,        "%u\n");

static struct attribute *counters_attrs[] = {
	&counters_attr_glock_count.attr,
	&counters_attr_glock_held_count.attr,
	&counters_attr_inode_count.attr,
	&counters_attr_reclaimed.attr,
	NULL
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
	.subsys = &fs_subsys,
	.kobj   = {.name = "gfs",},
	.ktype  = &gfs_ktype,
};

static ssize_t tune_set(struct gfs_sbd *sdp, unsigned int *field,
			int check_zero, const char *buf, size_t len)
{
	struct gfs_tune *gt = &sdp->sd_tune;
	unsigned int x;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	x = simple_strtoul(buf, NULL, 0);

	if (check_zero && !x)
		return -EINVAL;

	spin_lock(&gt->gt_spin);
	*field = x;
	spin_unlock(&gt->gt_spin);
	return len;
}

struct tune_attr {
	struct attribute attr;
	ssize_t (*show)(struct gfs_sbd *, char *);
	ssize_t (*store)(struct gfs_sbd *, const char *, size_t);
};

#define TUNE_ATTR_3(name, show, store)                                        \
static struct tune_attr tune_attr_##name = __ATTR(name, 0644, show, store)

#define TUNE_ATTR_2(name, store)                                              \
static ssize_t name##_show(struct gfs_sbd *sdp, char *buf)                   \
{                                                                             \
	return sprintf(buf, "%u\n", sdp->sd_tune.gt_##name);                  \
}                                                                             \
TUNE_ATTR_3(name, name##_show, store)

#define TUNE_ATTR(name, check_zero)                                           \
static ssize_t name##_store(struct gfs_sbd *sdp, const char *buf, size_t len)\
{                                                                             \
	return tune_set(sdp, &sdp->sd_tune.gt_##name, check_zero, buf, len);  \
}                                                                             \
TUNE_ATTR_2(name, name##_store)

#define TUNE_ATTR_DAEMON(name, process)                                       \
static ssize_t name##_store(struct gfs_sbd *sdp, const char *buf, size_t len)\
{                                                                             \
	ssize_t r = tune_set(sdp, &sdp->sd_tune.gt_##name, 1, buf, len);      \
	wake_up_process(sdp->sd_##process);                                   \
	return r;                                                             \
}                                                                             \
TUNE_ATTR_2(name, name##_store)

TUNE_ATTR(ilimit1, 0);
TUNE_ATTR(ilimit1_tries, 0);
TUNE_ATTR(ilimit1_min, 0);
TUNE_ATTR(ilimit2, 0);
TUNE_ATTR(ilimit2_tries, 0);
TUNE_ATTR(ilimit2_min, 0);
TUNE_ATTR(demote_secs, 0);
TUNE_ATTR(incore_log_blocks, 0);
TUNE_ATTR(jindex_refresh_secs, 0);
TUNE_ATTR(quota_warn_period, 0);
TUNE_ATTR(quota_quantum, 0);
TUNE_ATTR(atime_quantum, 0);
TUNE_ATTR(max_readahead, 0);
TUNE_ATTR(complain_secs, 0);
TUNE_ATTR(reclaim_limit, 0);
TUNE_ATTR(prefetch_secs, 0);
TUNE_ATTR(statfs_slots, 0);
TUNE_ATTR(new_files_jdata, 0);
TUNE_ATTR(new_files_directio, 0);
TUNE_ATTR(quota_simul_sync, 1);
TUNE_ATTR(max_atomic_write, 1);
TUNE_ATTR(stall_secs, 1);
TUNE_ATTR(entries_per_readdir, 1);
TUNE_ATTR(greedy_default, 1);
TUNE_ATTR(greedy_quantum, 1);
TUNE_ATTR(greedy_max, 1);
TUNE_ATTR_DAEMON(scand_secs, scand_process);
TUNE_ATTR_DAEMON(recoverd_secs, recoverd_process);
TUNE_ATTR_DAEMON(logd_secs, logd_process);
TUNE_ATTR_DAEMON(quotad_secs, quotad_process);

static struct attribute *tune_attrs[] = {
	&tune_attr_ilimit1.attr,
	&tune_attr_ilimit1_tries.attr,
	&tune_attr_ilimit1_min.attr,
	&tune_attr_ilimit2.attr,
	&tune_attr_ilimit2_tries.attr,
	&tune_attr_ilimit2_min.attr,
	&tune_attr_demote_secs.attr,
	&tune_attr_incore_log_blocks.attr,
	&tune_attr_jindex_refresh_secs.attr,
	&tune_attr_quota_warn_period.attr,
	&tune_attr_quota_quantum.attr,
	&tune_attr_atime_quantum.attr,
	&tune_attr_max_readahead.attr,
	&tune_attr_complain_secs.attr,
	&tune_attr_reclaim_limit.attr,
	&tune_attr_prefetch_secs.attr,
	&tune_attr_statfs_slots.attr,
	&tune_attr_quota_simul_sync.attr,
	&tune_attr_max_atomic_write.attr,
	&tune_attr_stall_secs.attr,
	&tune_attr_entries_per_readdir.attr,
	&tune_attr_greedy_default.attr,
	&tune_attr_greedy_quantum.attr,
	&tune_attr_greedy_max.attr,
	&tune_attr_scand_secs.attr,
	&tune_attr_recoverd_secs.attr,
	&tune_attr_logd_secs.attr,
	&tune_attr_quotad_secs.attr,
	&tune_attr_new_files_jdata.attr,
	&tune_attr_new_files_directio.attr,
	NULL
};

static struct attribute_group lockstruct_group = {
	.name = "lockstruct",
	.attrs = lockstruct_attrs
};

static struct attribute_group counters_group = {
	.name = "counters",
	.attrs = counters_attrs
};

static struct attribute_group args_group = {
	.name = "args",
	.attrs = args_attrs
};

static struct attribute_group tune_group = {
	.name = "tune",
	.attrs = tune_attrs
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

	error = sysfs_create_group(&sdp->sd_kobj, &lockstruct_group);
	if (error)
		goto fail_reg;

	error = sysfs_create_group(&sdp->sd_kobj, &counters_group);
	if (error)
		goto fail_lockstruct;

	error = sysfs_create_group(&sdp->sd_kobj, &args_group);
	if (error)
		goto fail_counters;
	
	error = sysfs_create_group(&sdp->sd_kobj, &tune_group);
	if (error)
		goto fail_args;
	return 0;

 fail_args:
	sysfs_remove_group(&sdp->sd_kobj, &args_group);
 fail_counters:
	sysfs_remove_group(&sdp->sd_kobj, &counters_group);
 fail_lockstruct:
	sysfs_remove_group(&sdp->sd_kobj, &lockstruct_group);
 fail_reg:
	kobject_unregister(&sdp->sd_kobj);
 fail:
	return error;
}

void gfs_sys_fs_del(struct gfs_sbd *sdp)
{
	sysfs_remove_group(&sdp->sd_kobj, &tune_group);
	sysfs_remove_group(&sdp->sd_kobj, &args_group);
	sysfs_remove_group(&sdp->sd_kobj, &counters_group);
	sysfs_remove_group(&sdp->sd_kobj, &lockstruct_group);
	kobject_unregister(&sdp->sd_kobj);
}

int gfs_sys_init(void)
{
	gfs_sys_margs = NULL;
	spin_lock_init(&gfs_sys_margs_lock);
	return kset_register(&gfs_kset);
}

void gfs_sys_uninit(void)
{
	kfree(gfs_sys_margs);
	kset_unregister(&gfs_kset);
}
