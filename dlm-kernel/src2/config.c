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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/configfs.h>
#include <net/sock.h>

#include "config.h"

/* FIXME: remove */
static inline void *kzalloc(size_t size, unsigned int flags)
{
	void *ret = kmalloc(size, flags);
	if (ret)
		memset(ret, 0, size);
	return ret;
}

/*
 * /config/dlm/<cluster>/spaces/<space>/nodes/<node>/nodeid
 * /config/dlm/<cluster>/spaces/<space>/nodes/<node>/weight
 * /config/dlm/<cluster>/comms/<comm>/nodeid
 * /config/dlm/<cluster>/comms/<comm>/local
 * /config/dlm/<cluster>/comms/<comm>/addr
 * The <cluster> level is useless, but I haven't figured out how to avoid it.
 */

static struct config_group *space_list;
static struct config_group *comm_list;
static struct comm *local_comm;

struct clusters;
struct cluster;
struct spaces;
struct space;
struct comms;
struct comm;
struct nodes;
struct node;

static struct config_group *make_cluster(struct config_group *, const char *);
static void drop_cluster(struct config_group *, struct config_item *);
static void release_cluster(struct config_item *);
static struct config_group *make_space(struct config_group *, const char *);
static void drop_space(struct config_group *, struct config_item *);
static void release_space(struct config_item *);
static struct config_item *make_comm(struct config_group *, const char *);
static void drop_comm(struct config_group *, struct config_item *);
static void release_comm(struct config_item *);
static struct config_item *make_node(struct config_group *, const char *);
static void drop_node(struct config_group *, struct config_item *);
static void release_node(struct config_item *);

static ssize_t show_space(struct config_item *i, struct configfs_attribute *a,
			  char *buf);
static ssize_t store_space(struct config_item *i, struct configfs_attribute *a,
			   const char *buf, size_t len);
static ssize_t show_comm(struct config_item *i, struct configfs_attribute *a,
			 char *buf);
static ssize_t store_comm(struct config_item *i, struct configfs_attribute *a,
			  const char *buf, size_t len);
static ssize_t show_node(struct config_item *i, struct configfs_attribute *a,
			 char *buf);
static ssize_t store_node(struct config_item *i, struct configfs_attribute *a,
			  const char *buf, size_t len);

static ssize_t space_id_read(struct space *sp, char *buf);
static ssize_t space_id_write(struct space *sp, const char *buf, size_t len);
static ssize_t comm_nodeid_read(struct comm *cm, char *buf);
static ssize_t comm_nodeid_write(struct comm *cm, const char *buf, size_t len);
static ssize_t comm_local_read(struct comm *cm, char *buf);
static ssize_t comm_local_write(struct comm *cm, const char *buf, size_t len);
static ssize_t comm_addr_write(struct comm *cm, const char *buf, size_t len);
static ssize_t node_nodeid_read(struct node *nd, char *buf);
static ssize_t node_nodeid_write(struct node *nd, const char *buf, size_t len);
static ssize_t node_weight_read(struct node *nd, char *buf);
static ssize_t node_weight_write(struct node *nd, const char *buf, size_t len);

enum {
	SPACE_ATTR_ID = 0,
};

struct space_attribute {
	struct configfs_attribute attr;
	ssize_t (*show)(struct space *, char *);
	ssize_t (*store)(struct space *, const char *, size_t);
};

static struct space_attribute space_attr_id = {
	.attr   = { .ca_owner = THIS_MODULE,
                    .ca_name = "id",
                    .ca_mode = S_IRUGO | S_IWUSR },
	.show   = space_id_read,
	.store  = space_id_write,
};

static struct configfs_attribute *space_attrs[] = {
	[SPACE_ATTR_ID] = &space_attr_id.attr,
	NULL,
};

enum {
	COMM_ATTR_NODEID = 0,
	COMM_ATTR_LOCAL,
	COMM_ATTR_ADDR,
};

struct comm_attribute {
	struct configfs_attribute attr;
	ssize_t (*show)(struct comm *, char *);
	ssize_t (*store)(struct comm *, const char *, size_t);
};

static struct comm_attribute comm_attr_nodeid = {
	.attr   = { .ca_owner = THIS_MODULE,
                    .ca_name = "nodeid",
                    .ca_mode = S_IRUGO | S_IWUSR },
	.show   = comm_nodeid_read,
	.store  = comm_nodeid_write,
};

static struct comm_attribute comm_attr_local = {
	.attr   = { .ca_owner = THIS_MODULE,
                    .ca_name = "local",
                    .ca_mode = S_IRUGO | S_IWUSR },
	.show   = comm_local_read,
	.store  = comm_local_write,
};

static struct comm_attribute comm_attr_addr = {
	.attr   = { .ca_owner = THIS_MODULE,
                    .ca_name = "addr",
                    .ca_mode = S_IRUGO | S_IWUSR },
	.store  = comm_addr_write,
};

static struct configfs_attribute *comm_attrs[] = {
	[COMM_ATTR_NODEID] = &comm_attr_nodeid.attr,
	[COMM_ATTR_LOCAL] = &comm_attr_local.attr,
	[COMM_ATTR_ADDR] = &comm_attr_addr.attr,
	NULL,
};

enum {
	NODE_ATTR_NODEID = 0,
	NODE_ATTR_WEIGHT,
};

struct node_attribute {
	struct configfs_attribute attr;
	ssize_t (*show)(struct node *, char *);
	ssize_t (*store)(struct node *, const char *, size_t);
};

static struct node_attribute node_attr_nodeid = {
	.attr   = { .ca_owner = THIS_MODULE,
                    .ca_name = "nodeid",
                    .ca_mode = S_IRUGO | S_IWUSR },
	.show   = node_nodeid_read,
	.store  = node_nodeid_write,
};

static struct node_attribute node_attr_weight = {
	.attr   = { .ca_owner = THIS_MODULE,
                    .ca_name = "weight",
                    .ca_mode = S_IRUGO | S_IWUSR },
	.show   = node_weight_read,
	.store  = node_weight_write,
};

static struct configfs_attribute *node_attrs[] = {
	[NODE_ATTR_NODEID] = &node_attr_nodeid.attr,
	[NODE_ATTR_WEIGHT] = &node_attr_weight.attr,
	NULL,
};

struct clusters {
	struct configfs_subsystem subsys;
};

struct cluster {
	struct config_group group;
};

struct spaces {
	struct config_group ss_group;
};

struct space {
	struct config_group group;
	struct list_head members;
	struct semaphore members_lock;
	int members_count;
	uint32_t id;
};

struct comms {
	struct config_group cs_group;
};

struct comm {
	struct config_item item;
	int nodeid;
	int local;
	struct sockaddr_storage addr;
};

struct nodes {
	struct config_group ns_group;
};

struct node {
	struct config_item item;
	struct list_head list; /* space->members */
	int nodeid;
	int weight;
};

static struct configfs_group_operations clusters_ops = {
	.make_group = make_cluster,
	.drop_item = drop_cluster,
};

static struct configfs_item_operations cluster_ops = {
	.release = release_cluster,
};

static struct configfs_group_operations spaces_ops = {
	.make_group = make_space,
	.drop_item = drop_space,
};

static struct configfs_item_operations space_ops = {
	.release = release_space,
	.show_attribute = show_space,
	.store_attribute = store_space,
};

static struct configfs_group_operations comms_ops = {
	.make_item = make_comm,
	.drop_item = drop_comm,
};

static struct configfs_item_operations comm_ops = {
	.release = release_comm,
	.show_attribute = show_comm,
	.store_attribute = store_comm,
};

static struct configfs_group_operations nodes_ops = {
	.make_item = make_node,
	.drop_item = drop_node,
};

static struct configfs_item_operations node_ops = {
	.release = release_node,
	.show_attribute = show_node,
	.store_attribute = store_node,
};

static struct config_item_type clusters_type = {
	.ct_group_ops = &clusters_ops,
	.ct_owner = THIS_MODULE,
};

static struct config_item_type cluster_type = {
	.ct_item_ops = &cluster_ops,
	.ct_owner = THIS_MODULE,
};

static struct config_item_type spaces_type = {
	.ct_group_ops = &spaces_ops,
	.ct_owner = THIS_MODULE,
};

static struct config_item_type space_type = {
	.ct_item_ops = &space_ops,
	.ct_attrs = space_attrs,
	.ct_owner = THIS_MODULE,
};

static struct config_item_type comms_type = {
	.ct_group_ops = &comms_ops,
	.ct_owner = THIS_MODULE,
};

static struct config_item_type comm_type = {
	.ct_item_ops = &comm_ops,
	.ct_attrs = comm_attrs,
	.ct_owner = THIS_MODULE,
};

static struct config_item_type nodes_type = {
	.ct_group_ops = &nodes_ops,
	.ct_owner = THIS_MODULE,
};

static struct config_item_type node_type = {
	.ct_item_ops = &node_ops,
	.ct_attrs = node_attrs,
	.ct_owner = THIS_MODULE,
};

static struct cluster *to_cluster(struct config_item *i)
{
	return i ? container_of(to_config_group(i), struct cluster, group):NULL;
}

static struct space *to_space(struct config_item *i)
{
	return i ? container_of(to_config_group(i), struct space, group) : NULL;
}

static struct comm *to_comm(struct config_item *i)
{
	return i ? container_of(i, struct comm, item) : NULL;
}

static struct node *to_node(struct config_item *i)
{
	return i ? container_of(i, struct node, item) : NULL;
}

static struct config_group *make_cluster(struct config_group *g,
					 const char *name)
{
	struct cluster *cl = NULL;
	struct spaces *sps = NULL;
	struct comms *cms = NULL;
	void *gps = NULL;

	cl = kzalloc(sizeof(struct cluster), GFP_KERNEL);
	gps = kcalloc(3, sizeof(struct config_group *), GFP_KERNEL);
	sps = kzalloc(sizeof(struct spaces), GFP_KERNEL);
	cms = kzalloc(sizeof(struct comms), GFP_KERNEL);

	if (!cl || !gps || !sps || !cms)
		goto fail;

	config_group_init_type_name(&cl->group, name, &cluster_type);
	config_group_init_type_name(&sps->ss_group, "spaces", &spaces_type);
	config_group_init_type_name(&cms->cs_group, "comms", &comms_type);

	cl->group.default_groups = gps;
	cl->group.default_groups[0] = &sps->ss_group;
	cl->group.default_groups[1] = &cms->cs_group;
	cl->group.default_groups[2] = NULL;

	space_list = &sps->ss_group;
	comm_list = &cms->cs_group;
	return &cl->group;

 fail:
	kfree(cl);
	kfree(gps);
	kfree(sps);
	kfree(cms);
	return NULL;
}

static void drop_cluster(struct config_group *g, struct config_item *i)
{
	struct cluster *cl = to_cluster(i);
	struct config_item *tmp;
	int j;

	for (j = 0; cl->group.default_groups[j]; j++) {
		tmp = &cl->group.default_groups[j]->cg_item;
		cl->group.default_groups[j] = NULL;
		config_item_put(tmp);
	}

	space_list = NULL;
	comm_list = NULL;

	config_item_put(i);
}

static void release_cluster(struct config_item *i)
{
	struct cluster *cl = to_cluster(i);
	kfree(cl->group.default_groups);
	kfree(cl);
}

static struct config_group *make_space(struct config_group *g, const char *name)
{
	struct space *sp = NULL;
	struct nodes *nds = NULL;
	void *gps = NULL;

	sp = kzalloc(sizeof(struct space), GFP_KERNEL);
	gps = kcalloc(2, sizeof(struct config_group *), GFP_KERNEL);
	nds = kzalloc(sizeof(struct nodes), GFP_KERNEL);

	if (!sp || !gps || !nds)
		goto fail;

	config_group_init_type_name(&sp->group, name, &space_type);
	config_group_init_type_name(&nds->ns_group, "nodes", &nodes_type);

	sp->group.default_groups = gps;
	sp->group.default_groups[0] = &nds->ns_group;
	sp->group.default_groups[1] = NULL;

	INIT_LIST_HEAD(&sp->members);
	init_MUTEX(&sp->members_lock);
	sp->members_count = 0;
	sp->id = -1;
	return &sp->group;

 fail:
	kfree(sp);
	kfree(gps);
	kfree(nds);
	return NULL;
}

static void drop_space(struct config_group *g, struct config_item *i)
{
	struct space *sp = to_space(i);
	struct config_item *tmp;
	int j;

	/* assert list_empty(&sp->members) */

	for (j = 0; sp->group.default_groups[j]; j++) {
		tmp = &sp->group.default_groups[j]->cg_item;
		sp->group.default_groups[j] = NULL;
		config_item_put(tmp);
	}

	config_item_put(i);
}

static void release_space(struct config_item *i)
{
	struct space *sp = to_space(i);
	kfree(sp->group.default_groups);
	kfree(sp);
}

static struct config_item *make_comm(struct config_group *g, const char *name)
{
	struct comm *cm;

	cm = kzalloc(sizeof(struct comm), GFP_KERNEL);
	if (!cm)
		return NULL;

	config_item_init_type_name(&cm->item, name, &comm_type);
	cm->nodeid = -1;
	cm->local = 0;
	return &cm->item;
}

static void drop_comm(struct config_group *g, struct config_item *i)
{
	struct comm *cm = to_comm(i);
	if (local_comm == cm)
		local_comm = NULL;
	config_item_put(i);
}

static void release_comm(struct config_item *i)
{
	struct comm *cm = to_comm(i);
	kfree(cm);
}

static struct config_item *make_node(struct config_group *g, const char *name)
{
	struct space *sp = to_space(g->cg_item.ci_parent);
	struct node *nd;

	nd = kzalloc(sizeof(struct node), GFP_KERNEL);
	if (!nd)
		return NULL;

	config_item_init_type_name(&nd->item, name, &node_type);
	nd->nodeid = -1;
	nd->weight = -1;

	down(&sp->members_lock);
	list_add(&nd->list, &sp->members);
	sp->members_count++;
	up(&sp->members_lock);

	return &nd->item;
}

static void drop_node(struct config_group *g, struct config_item *i)
{
	struct space *sp = to_space(g->cg_item.ci_parent);
	struct node *nd = to_node(i);

	down(&sp->members_lock);
	list_del(&nd->list);
	sp->members_count--;
	up(&sp->members_lock);

	config_item_put(i);
}

static void release_node(struct config_item *i)
{
	struct node *nd = to_node(i);
	kfree(nd);
}

static struct clusters clusters_root = {
	.subsys = {
		.su_group = {
			.cg_item = {
				.ci_namebuf = "dlm",
				.ci_type = &clusters_type,
			},
		},
	},
};

int dlm_config_init(void)
{
	config_group_init(&clusters_root.subsys.su_group);
	init_MUTEX(&clusters_root.subsys.su_sem);
	return configfs_register_subsystem(&clusters_root.subsys);
}

void dlm_config_exit(void)
{
	configfs_unregister_subsystem(&clusters_root.subsys);
}


/*
 * Functions for user space to read/write attributes
 */

static ssize_t show_space(struct config_item *i, struct configfs_attribute *a,
			  char *buf)
{
	struct space *sp = to_space(i);
	struct space_attribute *spa =
			container_of(a, struct space_attribute, attr);
	return spa->show ? spa->show(sp, buf) : 0;
}

static ssize_t store_space(struct config_item *i, struct configfs_attribute *a,
			   const char *buf, size_t len)
{
	struct space *sp = to_space(i);
	struct space_attribute *spa =
		container_of(a, struct space_attribute, attr);
	return spa->store ? spa->store(sp, buf, len) : -EINVAL;
}

static ssize_t space_id_read(struct space *sp, char *buf)
{
	return sprintf(buf, "%d\n", sp->id);
}

static ssize_t space_id_write(struct space *sp, const char *buf, size_t len)
{
	sp->id = simple_strtol(buf, NULL, 0);
	return len;
}

static ssize_t show_comm(struct config_item *i, struct configfs_attribute *a,
			 char *buf)
{
	struct comm *cm = to_comm(i);
	struct comm_attribute *cma =
			container_of(a, struct comm_attribute, attr);
	return cma->show ? cma->show(cm, buf) : 0;
}

static ssize_t store_comm(struct config_item *i, struct configfs_attribute *a,
			  const char *buf, size_t len)
{
	struct comm *cm = to_comm(i);
	struct comm_attribute *cma =
		container_of(a, struct comm_attribute, attr);
	return cma->store ? cma->store(cm, buf, len) : -EINVAL;
}

static ssize_t comm_nodeid_read(struct comm *cm, char *buf)
{
	return sprintf(buf, "%d\n", cm->nodeid);
}

static ssize_t comm_nodeid_write(struct comm *cm, const char *buf, size_t len)
{
	cm->nodeid = simple_strtol(buf, NULL, 0);
	return len;
}

static ssize_t comm_local_read(struct comm *cm, char *buf)
{
	return sprintf(buf, "%d\n", cm->local);
}

static ssize_t comm_local_write(struct comm *cm, const char *buf, size_t len)
{
	cm->local= simple_strtol(buf, NULL, 0);
	if (cm->local && !local_comm)
		local_comm = cm;
	return len;
}

static ssize_t comm_addr_write(struct comm *cm, const char *buf, size_t len)
{
	if (len < sizeof(struct sockaddr_storage))
		return -EINVAL;
	memcpy(&cm->addr, buf, len);
	return len;
}

static ssize_t show_node(struct config_item *i, struct configfs_attribute *a,
			 char *buf)
{
	struct node *nd = to_node(i);
	struct node_attribute *nda =
			container_of(a, struct node_attribute, attr);
	return nda->show ? nda->show(nd, buf) : 0;
}

static ssize_t store_node(struct config_item *i, struct configfs_attribute *a,
			  const char *buf, size_t len)
{
	struct node *nd = to_node(i);
	struct node_attribute *nda =
		container_of(a, struct node_attribute, attr);
	return nda->store ? nda->store(nd, buf, len) : -EINVAL;
}

static ssize_t node_nodeid_read(struct node *nd, char *buf)
{
	return sprintf(buf, "%d\n", nd->nodeid);
}

static ssize_t node_nodeid_write(struct node *nd, const char *buf, size_t len)
{
	nd->nodeid = simple_strtol(buf, NULL, 0);
	return len;
}

static ssize_t node_weight_read(struct node *nd, char *buf)
{
	return sprintf(buf, "%d\n", nd->weight);
}

static ssize_t node_weight_write(struct node *nd, const char *buf, size_t len)
{
	nd->weight = simple_strtol(buf, NULL, 0);
	return len;
}


/*
 * Functions for the dlm to get the info that's been configured
 */

static struct space *get_space(char *name)
{
	if (!space_list)
		return NULL;
	return to_space(config_group_find_obj(space_list, name));
}

static void put_space(struct space *sp)
{
	config_item_put(&sp->group.cg_item);
}

uint32_t dlm_lockspace_id(char *lsname)
{
	struct space *sp;
	uint32_t id = 0;

	sp = get_space(lsname);
	if (sp) {
		id = sp->id;
		put_space(sp);
	}
	return id;
}

/* caller must free mem */
int dlm_nodeid_list(char *lsname, int **ids_out)
{
	struct space *sp;
	struct node *nd;
	int i = 0, rv = 0;
	int *ids;

	sp = get_space(lsname);
	if (!sp)
		return -ENOSPC;

	down(&sp->members_lock);
	if (!sp->members_count) {
		rv = 0;
		goto out;
	}

	ids = kcalloc(sp->members_count, sizeof(int), GFP_KERNEL);
	if (!ids) {
		rv = -ENOMEM;
		goto out;
	}

	rv = sp->members_count;
	list_for_each_entry(nd, &sp->members, list)
		ids[i++] = nd->nodeid;

	if (rv != i)
		printk("bad nodeid count %d %d\n", rv, i);

	*ids_out = ids;
 out:
	up(&sp->members_lock);
	put_space(sp);
	return rv;
}

int dlm_node_weight(char *lsname, int nodeid)
{
	struct space *sp;
	struct node *nd;
	int weight = -1;

	sp = get_space(lsname);
	if (!sp)
		return -ENOSPC;

	down(&sp->members_lock);
	list_for_each_entry(nd, &sp->members, list) {
		if (nd->nodeid != nodeid)
			continue;
		weight = nd->weight;
		break;
	}
	up(&sp->members_lock);
	put_space(sp);
	return weight;
}

#if 0
int dlm_our_nodeid(void)
{
	return local_comm ? local_comm->nodeid : 0;
}

int dlm_nodeid_to_addr(int nodeid, struct sockaddr_storage *addr_ret)
{
	return 0;
}

int dlm_addr_to_nodeid(struct sockaddr_storage *addr, int *nodeid_ret)
{
	return 0;
}
#endif

/* Config file defaults */
#define DEFAULT_TCP_PORT       21064
#define DEFAULT_BUFFER_SIZE     4096
#define DEFAULT_RSBTBL_SIZE      256
#define DEFAULT_LKBTBL_SIZE     1024
#define DEFAULT_DIRTBL_SIZE      512
#define DEFAULT_RECOVER_TIMER      5
#define DEFAULT_TOSS_SECS         10
#define DEFAULT_SCAN_SECS          5

struct dlm_config_info dlm_config = {
	.tcp_port = DEFAULT_TCP_PORT,
	.buffer_size = DEFAULT_BUFFER_SIZE,
	.rsbtbl_size = DEFAULT_RSBTBL_SIZE,
	.lkbtbl_size = DEFAULT_LKBTBL_SIZE,
	.dirtbl_size = DEFAULT_DIRTBL_SIZE,
	.recover_timer = DEFAULT_RECOVER_TIMER,
	.toss_secs = DEFAULT_TOSS_SECS,
	.scan_secs = DEFAULT_SCAN_SECS
};

