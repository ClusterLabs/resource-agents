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

#include "sm.h"
#include "config.h"
#include <linux/seq_file.h>

#define MAX_DEBUG_MSG_LEN	(40)

extern struct list_head sm_members;
static uint32_t		local_ids;
static uint32_t		event_id;
static spinlock_t	event_id_lock;
static char *		debug_buf;
static unsigned int	debug_size;
static unsigned int	debug_point;
static int		debug_wrap;
static spinlock_t	debug_lock;


void init_sm_misc(void)
{
	local_ids = 1;
	event_id = 1;
	spin_lock_init(&event_id_lock);
	debug_buf = NULL;
	debug_size = 0;
	debug_point = 0;
	debug_wrap = 0;
	spin_lock_init(&debug_lock);

	sm_debug_setup(cman_config.sm_debug_size);
}

sm_node_t *sm_new_node(uint32_t nodeid)
{
	struct kcl_cluster_node kclnode;
	sm_node_t *node;
	int error;

	error = kcl_get_node_by_nodeid(nodeid, &kclnode);
	SM_ASSERT(!error,);

	SM_RETRY(node = (sm_node_t *) kmalloc(sizeof(sm_node_t), GFP_KERNEL),
		 node);

	memset(node, 0, sizeof(sm_node_t));
	node->id = nodeid;
	node->incarnation = kclnode.incarnation;
	return node;
}

sm_node_t *sm_find_joiner(sm_group_t *sg, uint32_t nodeid)
{
	sm_node_t *node;

	list_for_each_entry(node, &sg->joining, list) {
		if (node->id == nodeid)
			return node;
	}
	return NULL;
}

sm_node_t *sm_find_member(uint32_t nodeid)
{
	sm_node_t *node;

	list_for_each_entry(node, &sm_members, list) {
		if (node->id == nodeid)
			return node;
	}
	return NULL;
}

uint32_t sm_new_local_id(int level)
{
	uint32_t id = local_ids++;
	uint8_t l = (uint8_t) level;

	if (level > 0xFF)
		return 0;

	if (id > 0x00FFFFFF)
		return 0;

	id |= (l << 24);
	return id;
}

int sm_id_to_level(uint32_t id)
{
	uint8_t l = (id & 0xFF000000) >> 24;

	return (int) l;
}

void sm_set_event_id(int *id)
{
	spin_lock(&event_id_lock);
	*id = event_id++;
	spin_unlock(&event_id_lock);
}

sm_group_t *sm_local_id_to_sg(int id)
{
	sm_group_t *sg;
	int level = sm_id_to_level(id);
	int found = FALSE;

	down(&sm_sglock);

	list_for_each_entry(sg, &sm_sg[level], list) {
		if (sg->local_id == id) {
			found = TRUE;
			break;
		}
	}
	up(&sm_sglock);
	if (!found)
		sg = NULL;
	return sg;
}

sm_group_t *sm_global_id_to_sg(int id)
{
	sm_group_t *sg;
	int level = sm_id_to_level(id);
	int found = FALSE;

	down(&sm_sglock);

	list_for_each_entry(sg, &sm_sg[level], list) {
		if (sg->global_id == id) {
			found = TRUE;
			break;
		}
	}
	up(&sm_sglock);
	if (!found)
		sg = NULL;
	return sg;
}

void sm_debug_log(sm_group_t *sg, const char *fmt, ...)
{
	va_list va;
	int i, n, size, len;
	char buf[MAX_DEBUG_MSG_LEN+1];

	spin_lock(&debug_lock);

	if (!debug_buf)
		goto out;

	size = MAX_DEBUG_MSG_LEN;
	memset(buf, 0, size+1);

	n = snprintf(buf, size, "%08x ", sg->global_id);
	size -= n;

	va_start(va, fmt);
	vsnprintf(buf+n, size, fmt, va);
	va_end(va);

	len = strlen(buf);
	if (len > MAX_DEBUG_MSG_LEN-1)
		len = MAX_DEBUG_MSG_LEN-1;
	buf[len] = '\n';
	buf[len+1] = '\0';

	for (i = 0; i < strlen(buf); i++) {
		debug_buf[debug_point++] = buf[i];

		if (debug_point == debug_size) {
			debug_point = 0;
			debug_wrap = 1;
		}
	}
 out:
	spin_unlock(&debug_lock);
}

void sm_debug_setup(int size)
{
	char *b = kmalloc(size, GFP_KERNEL);

	spin_lock(&debug_lock);
	if (debug_buf)
		kfree(debug_buf);

	if (size > PAGE_SIZE)
		size = PAGE_SIZE;
	debug_size = size;
	debug_point = 0;
	debug_wrap = 0;
	debug_buf = b;
	memset(debug_buf, 0, debug_size);
	spin_unlock(&debug_lock);
}

#ifdef CONFIG_PROC_FS
static struct seq_operations sm_info_op;

struct sm_seq_info
{
    int pos;
    int level;
    sm_group_t *sg;
};

int sm_debug_info(char *b, char **start, off_t offset, int length)
{
	int i, n = 0;

	spin_lock(&debug_lock);

	if (debug_wrap) {
		for (i = debug_point; i < debug_size; i++)
			n += sprintf(b + n, "%c", debug_buf[i]);
	}
	for (i = 0; i < debug_point; i++)
		n += sprintf(b + n, "%c", debug_buf[i]);

	spin_unlock(&debug_lock);

	return n;
}



static sm_group_t *sm_walk(loff_t offset, int *rlevel)
{
	sm_group_t *sg;
	int  level;
	loff_t n = 0;

	down(&sm_sglock);

	for (level = 0; level < SG_LEVELS; level++) {
		list_for_each_entry(sg, &sm_sg[level], list) {
			if (++n == offset)
			        goto walk_finish;
		}
	}
	sg = NULL;

 walk_finish:
	up(&sm_sglock);
	*rlevel = level;

	return sg;
}


static void *sm_seq_start(struct seq_file *m, loff_t * pos)
{
	struct sm_seq_info *ssi =
	        kmalloc(sizeof (struct sm_seq_info), GFP_KERNEL);

	if (!ssi)
		return NULL;

	ssi->pos = *pos;
	ssi->level = 0;
	ssi->sg = NULL;

	/* Print the header */
	if (*pos == 0) {
		seq_printf(m,
			   "Service          Name                              GID LID State     Code\n");
	}
	return ssi;
}

static void *sm_seq_next(struct seq_file *m, void *p, loff_t * pos)
{
	struct sm_seq_info *ssi = p;

	*pos = ++ssi->pos;

	if ( !(ssi->sg = sm_walk(ssi->pos, &ssi->level)) )
		return NULL;

	return ssi;
}

/* Called from /proc when /proc/cluster/services is opened */
int sm_proc_open(struct inode *inode, struct file *file)
{
    	return seq_open(file, &sm_info_op);
}

static int sm_seq_show(struct seq_file *s, void *p)
{
    struct sm_seq_info *ssi = p;
    sm_node_t *node;
    int i;

    if (!ssi || !ssi->sg)
	    return 0;

    /*
     * Cluster Service
     */

    switch (ssi->level) {
    case SERVICE_LEVEL_FENCE:
	seq_printf(s, "Fence Domain:    ");
	break;
    case SERVICE_LEVEL_GDLM:
	seq_printf(s, "DLM Lock Space:  ");
	break;
    case SERVICE_LEVEL_GFS:
	seq_printf(s, "GFS Mount Group: ");
	break;
    case SERVICE_LEVEL_USER:
	seq_printf(s, "User:            ");
	break;
    }

    /*
     * Name
     */

    seq_printf(s, "\"");
    for (i = 0; i < ssi->sg->namelen; i++)
	    seq_printf(s, "%c", ssi->sg->name[i]);
    seq_printf(s, "\"");

    for (; i < MAX_SERVICE_NAME_LEN-1; i++)
	seq_printf(s, " ");

    /*
     * GID LID (sans level from top byte)
     */

    seq_printf(s, "%3u %3u ",
	       (ssi->sg->global_id & 0x00FFFFFF),
	       (ssi->sg->local_id & 0x00FFFFFF));

    /*
     * State
     */

    switch (ssi->sg->state) {
    case SGST_NONE:
	seq_printf(s, "none      ");
	break;
    case SGST_JOIN:
	seq_printf(s, "join      ");
	break;
    case SGST_RUN:
	seq_printf(s, "run       ");
	break;
    case SGST_RECOVER:
	seq_printf(s, "recover %u ",
		   ssi->sg->recover_state);
	break;
    case SGST_UEVENT:
	seq_printf(s, "update    ");
	break;
    }

    /*
     * Code
     */

    if (test_bit(SGFL_SEVENT, &ssi->sg->flags))
	    seq_printf(s, "S");
    if (test_bit(SGFL_UEVENT, &ssi->sg->flags))
	    seq_printf(s, "U");
    if (test_bit(SGFL_NEED_RECOVERY, &ssi->sg->flags))
	    seq_printf(s, "N");

    seq_printf(s, "-");

    if (test_bit(SGFL_SEVENT, &ssi->sg->flags)
	&& ssi->sg->sevent) {
	seq_printf(s, "%u,%lx,%u",
		   ssi->sg->sevent->se_state,
		   ssi->sg->sevent->se_flags,
		   ssi->sg->sevent->se_reply_count);
    }

    if (test_bit(SGFL_UEVENT, &ssi->sg->flags)) {
	seq_printf(s, "%u,%lx,%u",
		   ssi->sg->uevent.ue_state,
		   ssi->sg->uevent.ue_flags,
		   ssi->sg->uevent.ue_nodeid);
    }

    seq_printf(s, "\n");

    /*
     * node list
     */

    i = 0;

    seq_printf(s, "[");

    list_for_each_entry(node, &ssi->sg->memb, list) {
	    if (i && !(i % 24))
	            seq_printf(s, "\n");

	    if (i)
	            seq_printf(s, " ");

	seq_printf(s, "%u", node->id);
	i++;
    }

    seq_printf(s, "]\n\n");

    return 0;
}

static void sm_seq_stop(struct seq_file *m, void *p)
{
	kfree(p);
}


static struct seq_operations sm_info_op = {
	.start = sm_seq_start,
	.next = sm_seq_next,
	.stop = sm_seq_stop,
	.show = sm_seq_show
};


#endif
