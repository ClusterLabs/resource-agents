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

#include <linux/pagemap.h>
#include <linux/seq_file.h>
#include <linux/module.h>
#include <linux/ctype.h>

#include "dlm_internal.h"

enum {
	DLM_MAGIC = 0x20444C4D /* " DLM" */
};

static DECLARE_MUTEX(dlm_fs_mutex);
static struct dentry *dlm_root;
static struct super_block *dlm_sb;
static LIST_HEAD(dlm_debug_list);

struct rsb_iter {
	int entry;
	struct dlm_ls *ls;
	struct list_head *next;
	struct dlm_rsb *rsb;
};

static char *print_lockmode(int mode)
{
	switch (mode) {
	case DLM_LOCK_IV:
		return "--";
	case DLM_LOCK_NL:
		return "NL";
	case DLM_LOCK_CR:
		return "CR";
	case DLM_LOCK_CW:
		return "CW";
	case DLM_LOCK_PR:
		return "PR";
	case DLM_LOCK_PW:
		return "PW";
	case DLM_LOCK_EX:
		return "EX";
	default:
		return "??";
	}
}

static void print_lock(struct seq_file *s, struct dlm_lkb *lkb,
		       struct dlm_rsb *res)
{
	seq_printf(s, "%08x %s", lkb->lkb_id, print_lockmode(lkb->lkb_grmode));

	if (lkb->lkb_status == GDLM_LKSTS_CONVERT
	    || lkb->lkb_status == GDLM_LKSTS_WAITING)
		seq_printf(s, " (%s)", print_lockmode(lkb->lkb_rqmode));

	if (lkb->lkb_range) {
		/* This warns on Alpha. Tough. Only I see it */
		if (lkb->lkb_status == GDLM_LKSTS_CONVERT
		    || lkb->lkb_status == GDLM_LKSTS_GRANTED)
			seq_printf(s, " %" PRIx64 "-%" PRIx64,
				   lkb->lkb_range[GR_RANGE_START],
				   lkb->lkb_range[GR_RANGE_END]);
		if (lkb->lkb_status == GDLM_LKSTS_CONVERT
		    || lkb->lkb_status == GDLM_LKSTS_WAITING)
			seq_printf(s, " (%" PRIx64 "-%" PRIx64 ")",
				   lkb->lkb_range[RQ_RANGE_START],
				   lkb->lkb_range[RQ_RANGE_END]);
	}

	if (lkb->lkb_nodeid) {
		if (lkb->lkb_nodeid != res->res_nodeid)
			seq_printf(s, " Remote: %3d %08x", lkb->lkb_nodeid,
				   lkb->lkb_remid);
		else
			seq_printf(s, " Master:     %08x", lkb->lkb_remid);
	}

	if (lkb->lkb_status != GDLM_LKSTS_GRANTED)
		seq_printf(s, "  LQ: %d,0x%x", lkb->lkb_lockqueue_state,
			   lkb->lkb_lockqueue_flags);

	seq_printf(s, "\n");
}

static int print_resource(struct dlm_rsb *res, struct seq_file *s)
{
	struct dlm_lkb *lkb;
	int i;

	seq_printf(s, "\nResource %p (parent %p). Name (len=%d) \"", res,
		   res->res_parent, res->res_length);
	for (i = 0; i < res->res_length; i++) {
		if (isprint(res->res_name[i]))
			seq_printf(s, "%c", res->res_name[i]);
		else
			seq_printf(s, "%c", '.');
	}
	if (res->res_nodeid)
		seq_printf(s, "\"  \nLocal Copy, Master is node %d\n",
			   res->res_nodeid);
	else
		seq_printf(s, "\"  \nMaster Copy\n");

	/* Print the LVB: */
	if (res->res_lvbptr) {
		seq_printf(s, "LVB: ");
		for (i = 0; i < DLM_LVB_LEN; i++) {
			if (i == DLM_LVB_LEN / 2)
				seq_printf(s, "\n     ");
			seq_printf(s, "%02x ",
				   (unsigned char) res->res_lvbptr[i]);
		}
		if (test_bit(RESFL_VALNOTVALID, &res->res_flags))
			seq_printf(s, " (INVALID)");
		seq_printf(s, "\n");
	}

	/* Print the locks attached to this resource */
	seq_printf(s, "Granted Queue\n");
	list_for_each_entry(lkb, &res->res_grantqueue, lkb_statequeue)
		print_lock(s, lkb, res);

	seq_printf(s, "Conversion Queue\n");
	list_for_each_entry(lkb, &res->res_convertqueue, lkb_statequeue)
		print_lock(s, lkb, res);

	seq_printf(s, "Waiting Queue\n");
	list_for_each_entry(lkb, &res->res_waitqueue, lkb_statequeue)
		print_lock(s, lkb, res);

	return 0;
}

static int rsb_iter_next(struct rsb_iter *ri)
{
	struct dlm_ls *ls = ri->ls;
	int i;

 top:
	if (!ri->next) {
		/* Find the next non-empty hash bucket */
		for (i = ri->entry; i < ls->ls_rsbtbl_size; i++) {
			read_lock(&ls->ls_rsbtbl[i].lock);
			if (!list_empty(&ls->ls_rsbtbl[i].list)) {
				ri->next = ls->ls_rsbtbl[i].list.next;
				read_unlock(&ls->ls_rsbtbl[i].lock);
				break;
			}
			read_unlock(&ls->ls_rsbtbl[i].lock);
                }
		ri->entry = i;

		if (ri->entry >= ls->ls_rsbtbl_size)
			return 1;
	} else {
		i = ri->entry;
		read_lock(&ls->ls_rsbtbl[i].lock);
		ri->next = ri->next->next;
		if (ri->next->next == ls->ls_rsbtbl[i].list.next) {
			/* End of list - move to next bucket */
			ri->next = NULL;
			ri->entry++;
			read_unlock(&ls->ls_rsbtbl[i].lock);
			goto top;
                }
		read_unlock(&ls->ls_rsbtbl[i].lock);
	}
	ri->rsb = list_entry(ri->next, struct dlm_rsb, res_hashchain);

	return 0;
}

static void rsb_iter_free(struct rsb_iter *ri)
{
	kfree(ri);
}

static struct rsb_iter *rsb_iter_init(struct dlm_ls *ls)
{
	struct rsb_iter *ri;

	ri = kmalloc(sizeof *ri, GFP_KERNEL);
	if (!ri)
		return NULL;

	ri->ls = ls;
	ri->entry = 0;
	ri->next = NULL;

	if (rsb_iter_next(ri)) {
		rsb_iter_free(ri);
		return NULL;
	}

	return ri;
}

static void *seq_start(struct seq_file *file, loff_t *pos)
{
	struct rsb_iter *ri;
	loff_t n = *pos;

	ri = rsb_iter_init(file->private);
	if (!ri)
		return NULL;

	while (n--) {
		if (rsb_iter_next(ri)) {
			rsb_iter_free(ri);
			return NULL;
		}
	}

	return ri;
}

static void *seq_next(struct seq_file *file, void *iter_ptr, loff_t *pos)
{
	struct rsb_iter *ri = iter_ptr;

	(*pos)++;

	if (rsb_iter_next(ri)) {
		rsb_iter_free(ri);
		return NULL;
	}

	return ri;
}

static void seq_stop(struct seq_file *file, void *iter_ptr)
{
	/* nothing for now */
}

static int seq_show(struct seq_file *file, void *iter_ptr)
{
	struct rsb_iter *ri = iter_ptr;

	print_resource(ri->rsb, file);

	return 0;
}

static struct seq_operations dlm_seq_ops = {
	.start = seq_start,
	.next  = seq_next,
	.stop  = seq_stop,
	.show  = seq_show,
};

static int do_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	int ret;

	ret = seq_open(file, &dlm_seq_ops);
	if (ret)
		return ret;

	seq = file->private_data;
	seq->private = inode->u.generic_ip;

	return 0;
}

static struct file_operations dlm_fops = {
	.owner   = THIS_MODULE,
	.open    = do_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

static struct inode *dlm_get_inode(void)
{
	struct inode *inode = new_inode(dlm_sb);

	if (inode) {
		inode->i_mode 	 = S_IFREG | S_IRUGO;
		inode->i_uid 	 = 0;
		inode->i_gid 	 = 0;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks  = 0;
		inode->i_atime 	 = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		inode->i_fop     = &dlm_fops;
	}

	return inode;
}

static int __dlm_create_debug_file(struct dlm_ls *ls)
{
	struct dentry *dentry;
	struct inode *inode;

	dentry = d_alloc_name(dlm_root, ls->ls_name);
	if (!dentry)
		return -ENOMEM;

	inode = dlm_get_inode();
	if (!inode) {
		dput(dentry);
		return -ENOMEM;
	}

	inode->u.generic_ip = ls;
	ls->ls_debug_dentry = dentry;

	d_add(dentry, inode);

	return 0;
}

int dlm_create_debug_file(struct dlm_ls *ls)
{
	down(&dlm_fs_mutex);
	list_add_tail(&ls->ls_debug_list, &dlm_debug_list);
	if (!dlm_sb) {
		up(&dlm_fs_mutex);
		return 0;
	}
	up(&dlm_fs_mutex);

	return __dlm_create_debug_file(ls);
}

void dlm_delete_debug_file(struct dlm_ls *ls)
{
	down(&dlm_fs_mutex);
	list_del(&ls->ls_debug_list);
	if (!dlm_sb) {
		up(&dlm_fs_mutex);
		return;
	}
	up(&dlm_fs_mutex);

	if (ls->ls_debug_dentry) {
		d_drop(ls->ls_debug_dentry);
		simple_unlink(dlm_root->d_inode, ls->ls_debug_dentry);
	}
}

static int dlm_fill_super(struct super_block *sb, void *data, int silent)
{
	static struct tree_descr dlm_files[] = {
		{ "" }
	};
	struct dlm_ls *ls;
	int ret;

	ret = simple_fill_super(sb, DLM_MAGIC, dlm_files);
	if (ret)
		return ret;

	dlm_root = sb->s_root;

	down(&dlm_fs_mutex);

	dlm_sb = sb;

	list_for_each_entry(ls, &dlm_debug_list, ls_debug_list) {
		ret = __dlm_create_debug_file(ls);
		if (ret)
			break;
	}

	up(&dlm_fs_mutex);

	return ret;
}

static struct super_block *dlm_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return get_sb_single(fs_type, flags, data, dlm_fill_super);
}

static void dlm_kill_sb(struct super_block *sb)
{
	down(&dlm_fs_mutex);
	dlm_sb = NULL;
	up(&dlm_fs_mutex);

	kill_litter_super(sb);
}

static struct file_system_type dlm_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "dlm_debugfs",
	.get_sb		= dlm_get_sb,
	.kill_sb	= dlm_kill_sb,
};

int dlm_register_debugfs(void)
{
	return register_filesystem(&dlm_fs_type);
}

void dlm_unregister_debugfs(void)
{
	unregister_filesystem(&dlm_fs_type);
}
