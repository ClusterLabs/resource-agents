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

/*
/dlm/lsname/stop       RW  write 1 to suspend operation
/dlm/lsname/start      RW  write event_nr to start recovery
/dlm/lsname/finish     RW  write event_nr to finish recovery
/dlm/lsname/terminate  RW  write event_nr to term recovery
/dlm/lsname/done       RO  event_nr dlm is done processing
/dlm/lsname/id         RW  global id of lockspace
/dlm/lsname/members    RW  read = current members, write = next members
*/


static ssize_t dlm_stop_show(struct dlm_ls *ls, char *buf)
{
	ssize_t ret;
	int val;

	spin_lock(&ls->ls_recover_lock);
	val = ls->ls_last_stop;
	spin_unlock(&ls->ls_recover_lock);
	ret = sprintf(buf, "%d\n", val);
	return ret;
}

static ssize_t dlm_stop_store(struct dlm_ls *ls, const char *buf, size_t len)
{
	ssize_t ret = -EINVAL;

	if (simple_strtol(buf, NULL, 0) == 1) {
		dlm_ls_stop(ls);
		ret = len;
	}
	return ret;
}

static ssize_t dlm_start_show(struct dlm_ls *ls, char *buf)
{
	ssize_t ret;
	int val;

	spin_lock(&ls->ls_recover_lock);
	val = ls->ls_last_start;
	spin_unlock(&ls->ls_recover_lock);
	ret = sprintf(buf, "%d\n", val);
	return ret;
}

static ssize_t dlm_start_store(struct dlm_ls *ls, const char *buf, size_t len)
{
	ssize_t ret;
	ret = dlm_ls_start(ls, simple_strtol(buf, NULL, 0));
	return ret ? ret : len;
}

static ssize_t dlm_finish_show(struct dlm_ls *ls, char *buf)
{
	ssize_t ret;
	int val;

	spin_lock(&ls->ls_recover_lock);
	val = ls->ls_last_finish;
	spin_unlock(&ls->ls_recover_lock);
	ret = sprintf(buf, "%d\n", val);
	return ret;
}

static ssize_t dlm_finish_store(struct dlm_ls *ls, const char *buf, size_t len)
{
	dlm_ls_finish(ls, simple_strtol(buf, NULL, 0));
	return len;
}

static ssize_t dlm_terminate_show(struct dlm_ls *ls, char *buf)
{
	ssize_t ret;
	int val = 0;

	spin_lock(&ls->ls_recover_lock);
	if (test_bit(LSFL_LS_TERMINATE, &ls->ls_flags))
		val = 1;
	spin_unlock(&ls->ls_recover_lock);
	ret = sprintf(buf, "%d\n", val);
	return ret;
}

static ssize_t dlm_terminate_store(struct dlm_ls *ls, const char *buf, size_t len)
{
	ssize_t ret = -EINVAL;

	if (simple_strtol(buf, NULL, 0) == 1) {
		dlm_ls_terminate(ls);
		ret = len;
	}
	return ret;
}

static ssize_t dlm_done_show(struct dlm_ls *ls, char *buf)
{
	ssize_t ret;
	int val;

	spin_lock(&ls->ls_recover_lock);
	val = ls->ls_startdone;
	spin_unlock(&ls->ls_recover_lock);
	ret = sprintf(buf, "%d\n", val);
	return ret;
}

static ssize_t dlm_id_show(struct dlm_ls *ls, char *buf)
{
	return sprintf(buf, "%u\n", ls->ls_global_id);
}

static ssize_t dlm_id_store(struct dlm_ls *ls, const char *buf, size_t len)
{
	ls->ls_global_id = simple_strtol(buf, NULL, 0);
	return len;
}

static ssize_t dlm_members_show(struct dlm_ls *ls, char *buf)
{
	struct dlm_member *memb;
	ssize_t ret = 0;

	if (!down_read_trylock(&ls->ls_in_recovery))
		return -EBUSY;
	list_for_each_entry(memb, &ls->ls_nodes, list)
		ret += sprintf(buf+ret, "%u ", memb->nodeid);
	ret += sprintf(buf+ret, "\n");
	up_read(&ls->ls_in_recovery);
	return ret;
}

static ssize_t dlm_members_store(struct dlm_ls *ls, const char *buf, size_t len)
{
	int *nodeids, id, count = 1, i;
	ssize_t ret = len;
	char *p, *t;

	/* count number of id's in buf, assumes no trailing spaces */
	for (i = 0; i < len; i++)
		if (isspace(buf[i]))
			count++;

	nodeids = kmalloc(sizeof(int) * count, GFP_KERNEL);
	if (!nodeids)
		return -ENOMEM;

	p = kmalloc(len, GFP_KERNEL);
	if (!p) {
		kfree(nodeids);
		return -ENOMEM;
	}
	memcpy(p, buf, len);

	for (i = 0; i < count; i++) {
		if ((t = strsep(&p, " ")) == NULL)
			break;
		if (sscanf(t, "%u", &id) != 1)
			break;
		nodeids[i] = id;
	}

	if (i != count) {
		kfree(nodeids);
		ret = -EINVAL;
		goto out;
	}

	spin_lock(&ls->ls_recover_lock);
	if (ls->ls_nodeids_next) {
		kfree(nodeids);
		ret = -EINVAL;
		goto out_unlock;
	}
	ls->ls_nodeids_next = nodeids;
	ls->ls_nodeids_next_count = count;

 out_unlock:
	spin_unlock(&ls->ls_recover_lock);
 out:
	kfree(p);
	return ret;
}

struct dlm_attr {
	struct attribute attr;
	ssize_t (*show)(struct dlm_ls *, char *);
	ssize_t (*store)(struct dlm_ls *, const char *, size_t);
};

static struct dlm_attr dlm_attr_stop = {
	.attr  = {.name = "stop", .mode = S_IRUGO | S_IWUSR},
	.show  = dlm_stop_show,
	.store = dlm_stop_store
};

static struct dlm_attr dlm_attr_start = {
	.attr  = {.name = "start", .mode = S_IRUGO | S_IWUSR},
	.show  = dlm_start_show,
	.store = dlm_start_store
};

static struct dlm_attr dlm_attr_finish = {
	.attr  = {.name = "finish", .mode = S_IRUGO | S_IWUSR},
	.show  = dlm_finish_show,
	.store = dlm_finish_store
};

static struct dlm_attr dlm_attr_terminate = {
	.attr  = {.name = "terminate", .mode = S_IRUGO | S_IWUSR},
	.show  = dlm_terminate_show,
	.store = dlm_terminate_store
};

static struct dlm_attr dlm_attr_done = {
	.attr  = {.name = "done", .mode = S_IRUGO},
	.show  = dlm_done_show,
};

static struct dlm_attr dlm_attr_id = {
	.attr  = {.name = "id", .mode = S_IRUGO | S_IWUSR},
	.show  = dlm_id_show,
	.store = dlm_id_store
};

static struct dlm_attr dlm_attr_members = {
	.attr  = {.name = "members", .mode = S_IRUGO | S_IWUSR},
	.show  = dlm_members_show,
	.store = dlm_members_store
};

static struct attribute *dlm_attrs[] = {
	&dlm_attr_stop.attr,
	&dlm_attr_start.attr,
	&dlm_attr_finish.attr,
	&dlm_attr_terminate.attr,
	&dlm_attr_done.attr,
	&dlm_attr_id.attr,
	&dlm_attr_members.attr,
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

