/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <linux/init.h>
#include <linux/socket.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/list.h>
#include <linux/in.h>
#include <net/sock.h>
#include <cluster/cnxman.h>
#include <cluster/service.h>

#include "cnxman-private.h"
#include "config.h"

extern int cluster_members;
extern struct list_head cluster_members_list;
extern struct semaphore cluster_members_lock;
extern struct cluster_node *quorum_device;
extern int we_are_a_cluster_member;
extern int cluster_is_quorate;
extern unsigned short cluster_id;
extern atomic_t use_count;
extern unsigned int address_length;
extern unsigned int config_version;
extern char cluster_name[];
extern struct cluster_node *us;
static struct seq_operations cluster_info_op;

int sm_procdata(char *b, char **start, off_t offset, int length);
int sm_debug_info(char *b, char **start, off_t offset, int length);

/* /proc interface to the configuration struct */
static struct config_proc_info {
    char *name;
    int  *value;
} config_proc[] = {
    {
	.name = "joinwait_timeout",
	.value = &cman_config.joinwait_timeout,
    },
    {
	.name = "joinconf_timeout",
	.value = &cman_config.joinconf_timeout,
    },
    {
	.name = "join_timeout",
	.value = &cman_config.join_timeout,
    },
    {
	.name = "hello_timer",
	.value = &cman_config.hello_timer,
    },
    {
	.name = "deadnode_timeout",
	.value = &cman_config.deadnode_timeout,
    },
    {
	.name = "transition_timeout",
	.value = &cman_config.transition_timeout,
    },
    {
	.name = "transition_restarts",
	.value = &cman_config.transition_restarts,
    },
    {
	.name = "max_nodes",
	.value = &cman_config.max_nodes,
    },
    {
	.name = "sm_debug_size",
	.value = &cman_config.sm_debug_size,
    },
};


static int proc_cluster_status(char *b, char **start, off_t offset, int length)
{
    struct list_head *nodelist;
    struct cluster_node *node;
    struct cluster_node_addr *node_addr;
    unsigned int total_votes = 0;
    unsigned int max_expected = 0;
    int c = 0;
    char node_buf[MAX_CLUSTER_MEMBER_NAME_LEN];

    if (!we_are_a_cluster_member) {
	c += sprintf(b+c, "Not a cluster member. State: %s\n",
		     membership_state(node_buf,
				      sizeof (node_buf)));
	return c;
    }

    /* Total the votes */
    down(&cluster_members_lock);
    list_for_each(nodelist, &cluster_members_list) {
	node = list_entry(nodelist, struct cluster_node, list);
	if (node->state == NODESTATE_MEMBER) {
	    total_votes += node->votes;
	    max_expected =
		max(max_expected, node->expected_votes);
	}
    }
    up(&cluster_members_lock);

    if (quorum_device && quorum_device->state == NODESTATE_MEMBER)
	total_votes += quorum_device->votes;

    c += sprintf(b+c,
		 "Version: %d.%d.%d\nConfig version: %d\nCluster name: %s\nCluster ID: %d\nMembership state: %s\n",
		 CNXMAN_MAJOR_VERSION, CNXMAN_MINOR_VERSION,
		 CNXMAN_PATCH_VERSION,
		 config_version,
		 cluster_name, cluster_id,
		 membership_state(node_buf, sizeof (node_buf)));
    c += sprintf(b+c,
		 "Nodes: %d\nExpected_votes: %d\nTotal_votes: %d\nQuorum: %d  %s\n",
		 cluster_members, max_expected, total_votes,
		 get_quorum(),
		 cluster_is_quorate ? " " : "Activity blocked");
    c += sprintf(b+c, "Active subsystems: %d\n",
		 atomic_read(&use_count));


    c += sprintf(b+c, "Node addresses: ");
    list_for_each_entry(node_addr, &us->addr_list, list) {
	struct sockaddr_in6 *saddr = (struct sockaddr_in6 *)node_addr->addr;
	if (saddr->sin6_family == AF_INET6) {
		c += sprintf(b+c, "%x:%x:%x:%x:%x:%x:%x:%x  ",
			     be16_to_cpu(saddr->sin6_addr.s6_addr16[0]),
			     be16_to_cpu(saddr->sin6_addr.s6_addr16[1]),
			     be16_to_cpu(saddr->sin6_addr.s6_addr16[2]),
			     be16_to_cpu(saddr->sin6_addr.s6_addr16[3]),
			     be16_to_cpu(saddr->sin6_addr.s6_addr16[4]),
			     be16_to_cpu(saddr->sin6_addr.s6_addr16[5]),
			     be16_to_cpu(saddr->sin6_addr.s6_addr16[6]),
			     be16_to_cpu(saddr->sin6_addr.s6_addr16[7]));
	}
	else {
	    struct sockaddr_in *saddr4 = (struct sockaddr_in *)saddr;
	    uint8_t *addr = (uint8_t *)&saddr4->sin_addr;
	    c+= sprintf(b+c, "%u.%u.%u.%u  ",
			addr[0], addr[1], addr[2], addr[3]);
	}
    }
    c += sprintf(b+c, "\n\n");
    return c;
}


/* Allocate one of these for /proc/cluster/nodes so we can keep a track of where
 * we are */
struct cluster_seq_info {
	int nodeid;
	int highest_nodeid;
};

static int cluster_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &cluster_info_op);
}

static void *cluster_seq_start(struct seq_file *m, loff_t * pos)
{
	struct cluster_seq_info *csi =
	    kmalloc(sizeof (struct cluster_seq_info), GFP_KERNEL);

	if (!csi)
		return NULL;

	/* Keep highest_nodeid here so we don't need to keep traversing the
	 * list to find it */
	csi->nodeid = *pos;
	csi->highest_nodeid = get_highest_nodeid();

	/* Print the header */
	if (*pos == 0) {
		seq_printf(m,
			   "Node  Votes Exp Sts  Name\n");
		return csi;
	}
	return csi;
}

static void *cluster_seq_next(struct seq_file *m, void *p, loff_t * pos)
{
	struct cluster_seq_info *csi = p;

	*pos = ++csi->nodeid;
	if (csi->nodeid > csi->highest_nodeid)
		return NULL;

	return csi;
}

static int cluster_seq_show(struct seq_file *m, void *p)
{
	char state = '?';
	struct cluster_node *node;
	struct cluster_seq_info *csi = p;

	/*
	 * If we have "0" here then display the quorum device if
	 * there is one.
	 */
	if (csi->nodeid == 0)
		node = quorum_device;
	else
		node = find_node_by_nodeid(csi->nodeid);

	if (!node)
		return 0;

	/* Make state printable */
	switch (node->state) {
	case NODESTATE_MEMBER:
		state = 'M';
		break;
	case NODESTATE_JOINING:
		state = 'J';
		break;
	case NODESTATE_REMOTEMEMBER:
		state = 'R';
		break;
	case NODESTATE_DEAD:
		state = 'X';
		break;
	}
	seq_printf(m, " %3d  %3d  %3d   %c   %s\n",
		   node->node_id,
		   node->votes,
		   node->expected_votes,
		   state,
		   node->name);

	return 0;
}

static void cluster_seq_stop(struct seq_file *m, void *p)
{
	kfree(p);
}

static struct seq_operations cluster_info_op = {
	.start = cluster_seq_start,
	.next = cluster_seq_next,
	.stop = cluster_seq_stop,
	.show = cluster_seq_show
};

static struct file_operations cluster_fops = {
	.open = cluster_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int cman_config_read_proc(char *page, char **start, off_t off, int count,
				 int *eof, void *data)
{
    struct config_proc_info *cinfo = data;

    return snprintf(page, count, "%d\n", *cinfo->value);
}

static int cman_config_write_proc(struct file *file, const char *buffer,
				  unsigned long count, void *data)
{
    struct config_proc_info *cinfo = data;
    int value;
    char *end;

    value = simple_strtoul(buffer, &end, 10);
    if (*end) {
	*cinfo->value = value;
    }
    return count;
}

/* Base of the config directory for cman */
static struct proc_dir_entry *proc_cman_config;
void create_proc_entries(void)
{
	struct proc_dir_entry *procentry;
	struct proc_dir_entry *proc_cluster;
	int i;

	proc_cluster = proc_mkdir("cluster", 0);
	if (!proc_cluster)
		return;
	proc_cluster->owner = THIS_MODULE;

	/* Config dir filled in by us and others */
	if (!proc_mkdir("cluster/config", 0))
		return;

	/* Don't much care if this fails, it's hardly vital */
	procentry = create_proc_entry("cluster/nodes", S_IRUGO, NULL);
	if (procentry)
		procentry->proc_fops = &cluster_fops;

	procentry = create_proc_entry("cluster/status", S_IRUGO, NULL);
	if (procentry)
	        procentry->get_info = proc_cluster_status;

	procentry = create_proc_entry("cluster/services", S_IRUGO, NULL);
	if (procentry)
		procentry->get_info = sm_procdata;

	/* Config entries */
	proc_cman_config = proc_mkdir("cluster/config/cman", 0);
	if (!proc_cman_config)
	        return;

	for (i=0; i<sizeof(config_proc)/sizeof(struct config_proc_info); i++) {
	        procentry = create_proc_entry(config_proc[i].name, 0660,
					      proc_cman_config);
		if (procentry) {
		        procentry->data = &config_proc[i];
			procentry->write_proc = cman_config_write_proc;
			procentry->read_proc = cman_config_read_proc;
		}
	}

	procentry = create_proc_entry("cluster/sm_debug", S_IRUGO, NULL);
	if (procentry)
		procentry->get_info = sm_debug_info;
}

void cleanup_proc_entries(void)
{
        int i, config_count;

	remove_proc_entry("cluster/sm_debug", NULL);

	config_count = sizeof(config_proc) / sizeof(struct config_proc_info);

	if (proc_cman_config) {
	        for (i=0; i<config_count; i++)
			remove_proc_entry(config_proc[i].name, proc_cman_config);
	}
	remove_proc_entry("cluster/config/cman", NULL);
	remove_proc_entry("cluster/config", NULL);

	remove_proc_entry("cluster/nodes", NULL);
	remove_proc_entry("cluster/status", NULL);
	remove_proc_entry("cluster/services", NULL);
	remove_proc_entry("cluster/config", NULL);
	remove_proc_entry("cluster", NULL);
}
