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

#include "dlm_internal.h"
#include "lockspace.h"
#include "member.h"
#include "lowcomms.h"
#include "rcom.h"
#include "config.h"
#include "memory.h"
#include "recover.h"
#include "util.h"
#include "lock.h"

struct resmov {
	int rm_nodeid;
	uint16_t rm_length;
	uint16_t rm_pad;
};

static void put_free_de(struct dlm_ls *ls, struct dlm_direntry *de)
{
	spin_lock(&ls->ls_recover_list_lock);
	list_add(&de->list, &ls->ls_recover_list);
	spin_unlock(&ls->ls_recover_list_lock);
}

static struct dlm_direntry *get_free_de(struct dlm_ls *ls, int len)
{
	int found = FALSE;
	struct dlm_direntry *de;

	spin_lock(&ls->ls_recover_list_lock);
	list_for_each_entry(de, &ls->ls_recover_list, list) {
		if (de->length == len) {
			list_del(&de->list);
			de->master_nodeid = 0;
			memset(de->name, 0, len);
			found = TRUE;
			break;
		}
	}
	spin_unlock(&ls->ls_recover_list_lock);

	if (!found)
		de = allocate_direntry(ls, len);
	return de;
}

void dlm_clear_free_entries(struct dlm_ls *ls)
{
	struct dlm_direntry *de;

	spin_lock(&ls->ls_recover_list_lock);
	while (!list_empty(&ls->ls_recover_list)) {
		de = list_entry(ls->ls_recover_list.next, struct dlm_direntry,
				list);
		list_del(&de->list);
		free_direntry(de);
	}
	spin_unlock(&ls->ls_recover_list_lock);
}

/* 
 * We use the upper 16 bits of the hash value to select the directory node.
 * Low bits are used for distribution of rsb's among hash buckets on each node.
 *
 * To give the exact range wanted (0 to num_nodes-1), we apply a modulus of
 * num_nodes to the hash value.  This value in the desired range is used as an
 * offset into the sorted list of nodeid's to give the particular nodeid of the
 * directory node.
 */

int name_to_directory_nodeid(struct dlm_ls *ls, char *name, int length)
{
	struct list_head *tmp;
	struct dlm_member *memb = NULL;
	uint32_t hash, node, n = 0;
	int nodeid;

	if (ls->ls_num_nodes == 1) {
		nodeid = dlm_our_nodeid();
		goto out;
	}

	hash = dlm_hash(name, length);
	node = (hash >> 16) % ls->ls_num_nodes;

	if (ls->ls_node_array) {
		nodeid = ls->ls_node_array[node];
		goto out;
	}

	list_for_each(tmp, &ls->ls_nodes) {
		if (n++ != node)
			continue;
		memb = list_entry(tmp, struct dlm_member, list);
		break;
	}

	DLM_ASSERT(memb , printk("num_nodes=%u n=%u node=%u\n",
				 ls->ls_num_nodes, n, node););
	nodeid = memb->node->nodeid;
 out:
	return nodeid;
}

int dlm_dir_nodeid(struct dlm_rsb *rsb)
{
	return name_to_directory_nodeid(rsb->res_ls, rsb->res_name,
					rsb->res_length);
}

static inline uint32_t dir_hash(struct dlm_ls *ls, char *name, int len)
{
	uint32_t val;

	val = dlm_hash(name, len);
	val &= (ls->ls_dirtbl_size - 1);

	return val;
}

static void add_entry_to_hash(struct dlm_ls *ls, struct dlm_direntry *de)
{
	uint32_t bucket;

	bucket = dir_hash(ls, de->name, de->length);
	list_add_tail(&de->list, &ls->ls_dirtbl[bucket].list);
}

static struct dlm_direntry *search_bucket(struct dlm_ls *ls, char *name,
					  int namelen, uint32_t bucket)
{
	struct dlm_direntry *de;

	list_for_each_entry(de, &ls->ls_dirtbl[bucket].list, list) {
		if (de->length == namelen && !memcmp(name, de->name, namelen))
			goto out;
	}
	de = NULL;
 out:
	return de;
}

void dlm_dir_remove_entry(struct dlm_ls *ls, int nodeid, char *name, int namelen)
{
	struct dlm_direntry *de;
	uint32_t bucket;

	bucket = dir_hash(ls, name, namelen);

	write_lock(&ls->ls_dirtbl[bucket].lock);

	de = search_bucket(ls, name, namelen, bucket);

	if (!de) {
		log_error(ls, "remove fr %u none", nodeid);
		goto out;
	}

	if (de->master_nodeid != nodeid) {
		log_error(ls, "remove fr %u ID %u", nodeid, de->master_nodeid);
		goto out;
	}

	list_del(&de->list);
	free_direntry(de);
 out:
	write_unlock(&ls->ls_dirtbl[bucket].lock);
}

void dlm_dir_clear(struct dlm_ls *ls)
{
	struct list_head *head;
	struct dlm_direntry *de;
	int i;

	DLM_ASSERT(list_empty(&ls->ls_recover_list), );

	for (i = 0; i < ls->ls_dirtbl_size; i++) {
		write_lock(&ls->ls_dirtbl[i].lock);
		head = &ls->ls_dirtbl[i].list;
		while (!list_empty(head)) {
			de = list_entry(head->next, struct dlm_direntry, list);
			list_del(&de->list);
			put_free_de(ls, de);
		}
		write_unlock(&ls->ls_dirtbl[i].lock);
	}
}

static void resmov_in(struct resmov *rm, char *buf)
{
	struct resmov tmp;

	memcpy(&tmp, buf, sizeof(struct resmov));

	rm->rm_nodeid = be32_to_cpu(tmp.rm_nodeid);
	rm->rm_length = be16_to_cpu(tmp.rm_length);
}

int dlm_recover_directory(struct dlm_ls *ls)
{
	struct dlm_member *memb;
	struct dlm_direntry *de;
	struct dlm_rcom *rc;
	struct resmov mov, last_mov;
	char *b, *last_name;
	int error = -ENOMEM, count = 0;

	log_debug(ls, "rebuild resource directory");

	dlm_dir_clear(ls);

	rc = allocate_rcom_buffer(ls);
	if (!rc)
		goto out;

	last_name = kmalloc(DLM_RESNAME_MAXLEN, GFP_KERNEL);
	if (!last_name)
		goto free_rc;

	list_for_each_entry(memb, &ls->ls_nodes, list) {
		last_mov.rm_length = 0;
		for (;;) {
			error = dlm_recovery_stopped(ls);
			if (error)
				goto free_last;

			memcpy(rc->rc_buf, last_name, last_mov.rm_length);
			rc->rc_datalen = last_mov.rm_length;

			error = dlm_send_rcom(ls, memb->node->nodeid,
					      DLM_RCOM_NAMES, rc, 1);
			if (error)
				goto free_last;

			schedule();

			/* 
			 * pick each res out of buffer
			 */

			b = rc->rc_buf;

			for (;;) {
				resmov_in(&mov, b);
				b += sizeof(struct resmov);

				/* Length of 0 with a non-zero nodeid marks the 
				 * end of the list */
				if (!mov.rm_length && mov.rm_nodeid)
					goto done;

				/* This is just the end of the block */
				if (!mov.rm_length)
					break;

				DLM_ASSERT(mov.rm_nodeid == memb->node->nodeid,);

				error = -ENOMEM;
				de = get_free_de(ls, mov.rm_length);
				if (!de)
					goto free_last;

				de->master_nodeid = mov.rm_nodeid;
				de->length = mov.rm_length;
				memcpy(de->name, b, mov.rm_length);
				b += mov.rm_length;

				add_entry_to_hash(ls, de);
				count++;

				last_mov = mov;
				memset(last_name, 0, DLM_RESNAME_MAXLEN);
				memcpy(last_name, de->name, de->length);
			}
		}
	      done:
		;
	}

	set_bit(LSFL_DIR_VALID, &ls->ls_flags);
	error = 0;

	log_debug(ls, "rebuilt %d resources", count);

 free_last:
	kfree(last_name);

 free_rc:
	free_rcom_buffer(rc);

 out:
	dlm_clear_free_entries(ls);
	return error;
}

static int get_entry(struct dlm_ls *ls, int nodeid, char *name,
		     int namelen, int *r_nodeid)
{
	struct dlm_direntry *de, *tmp;
	uint32_t bucket;

	bucket = dir_hash(ls, name, namelen);

	write_lock(&ls->ls_dirtbl[bucket].lock);
	de = search_bucket(ls, name, namelen, bucket);
	if (de) {
		*r_nodeid = de->master_nodeid;
		write_unlock(&ls->ls_dirtbl[bucket].lock);
		if (*r_nodeid == nodeid)
			return -EEXIST;
		return 0;
	}

	write_unlock(&ls->ls_dirtbl[bucket].lock);

	de = allocate_direntry(ls, namelen);
	if (!de)
		return -ENOMEM;

	de->master_nodeid = nodeid;
	de->length = namelen;
	memcpy(de->name, name, namelen);

	write_lock(&ls->ls_dirtbl[bucket].lock);
	tmp = search_bucket(ls, name, namelen, bucket);
	if (tmp) {
		free_direntry(de);
		de = tmp;
	} else {
		list_add_tail(&de->list, &ls->ls_dirtbl[bucket].list);
	}
	*r_nodeid = de->master_nodeid;
	write_unlock(&ls->ls_dirtbl[bucket].lock);
	return 0;
}

int dlm_dir_lookup(struct dlm_ls *ls, int nodeid, char *name, int namelen,
		   int *r_nodeid)
{
	return get_entry(ls, nodeid, name, namelen, r_nodeid);
}

/* 
 * The node with lowest id queries all nodes to determine when all are done.
 * All other nodes query the low nodeid for this.
 */

int dlm_dir_rebuild_wait(struct dlm_ls *ls)
{
	int error;

	if (ls->ls_low_nodeid == dlm_our_nodeid()) {
		error = dlm_wait_status_all(ls, DIR_VALID);
		if (!error)
			set_bit(LSFL_ALL_DIR_VALID, &ls->ls_flags);
	} else
		error = dlm_wait_status_low(ls, DIR_ALL_VALID);

	return error;
}

/* Copy the names of master rsb's into the buffer provided.
   Only select names whose dir node is the given nodeid. */

int dlm_copy_master_names(struct dlm_ls *ls, char *inbuf, int inlen,
			  char *outbuf, int outlen, int nodeid)
{
	struct list_head *list;
	struct dlm_rsb *start_r = NULL, *r = NULL;
	int offset = 0, start_namelen, error;
	char *start_name;
	struct resmov tmp;
	int dir_nodeid;

	/* 
	 * Find the rsb where we left off (or start again)
	 */

	start_namelen = inlen;
	start_name = inbuf;

	if (start_namelen > 1) {
		/*
		 * We could also use a find_rsb_root() function here that
		 * searched the ls_rootres list.
		 */
		error = dlm_find_rsb(ls, start_name, start_namelen, R_MASTER,
				     &start_r);
		DLM_ASSERT(!error && start_r, printk("error %d\n", error););
		DLM_ASSERT(!list_empty(&r->res_rootlist),);
		dlm_put_rsb(start_r);
	}

	/* 
	 * Send rsb names for rsb's we're master of and whose directory node
	 * matches the requesting node.
	 */

	down_read(&ls->ls_root_lock);
	if (start_r)
		list = start_r->res_rootlist.next;
	else
		list = ls->ls_rootres.next;

	for (offset = 0; list != &ls->ls_rootres; list = list->next) {
		r = list_entry(list, struct dlm_rsb, res_rootlist);
		if (r->res_nodeid)
			continue;

		dir_nodeid = dlm_dir_nodeid(r);
		if (dir_nodeid != nodeid)
			continue;

		if (offset + sizeof(struct resmov)*2 + r->res_length > outlen) {
			/* Write end-of-block record */
			memset(&tmp, 0, sizeof(struct resmov));
			memcpy(outbuf + offset, &tmp, sizeof(struct resmov));
			offset += sizeof(struct resmov);
			goto out;
		}

		memset(&tmp, 0, sizeof(struct resmov));
		tmp.rm_nodeid = cpu_to_be32(dlm_our_nodeid());
		tmp.rm_length = cpu_to_be16(r->res_length);

		memcpy(outbuf + offset, &tmp, sizeof(struct resmov));
		offset += sizeof(struct resmov);

		memcpy(outbuf + offset, r->res_name, r->res_length);
		offset += r->res_length;
	}

	/* 
	 * If we've reached the end of the list (and there's room) write a
	 * terminating record.
	 */

	if ((list == &ls->ls_rootres) &&
	    (offset + sizeof(struct resmov) <= outlen)) {

		memset(&tmp, 0, sizeof(struct resmov));
		/* This only needs to be non-zero */
		tmp.rm_nodeid = cpu_to_be32(1);
		/* and this must be zero */
		tmp.rm_length = 0;
		memcpy(outbuf + offset, &tmp, sizeof(struct resmov));
		offset += sizeof(struct resmov);
	}

 out:
	up_read(&ls->ls_root_lock);
	return offset;
}


