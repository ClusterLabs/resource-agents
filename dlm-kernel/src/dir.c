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
#include "nodes.h"
#include "lockspace.h"
#include "lowcomms.h"
#include "reccomms.h"
#include "rsb.h"
#include "config.h"
#include "memory.h"
#include "recover.h"
#include "util.h"

/* 
 * We use the upper 16 bits of the hash value to select the directory node.
 * Low bits are used for distribution of rsb's among hash buckets on each node.
 *
 * From the hash value, we are interested in arriving at a final value between
 * zero and the number of nodes minus one (num_nodes - 1).
 *
 * To accomplish this scaling, we take the nearest power of two larger than
 * num_nodes and subtract one to create a bit mask.  The mask is applied to the
 * hash, reducing the range to nearer the final range.
 *
 * To give the exact range wanted (0 to num_nodes-1), we apply a modulus of
 * num_nodes to the previously masked hash value.
 *
 * This value in the desired range is used as an offset into the sorted list of
 * nodeid's to give the particular nodeid of the directory node.
 */

uint32_t name_to_directory_nodeid(gd_ls_t *ls, char *name, int length)
{
	struct list_head *tmp;
	gd_csb_t *csb = NULL;
	uint32_t hash, node, n = 0, nodeid;

	if (ls->ls_num_nodes == 1) {
		nodeid = our_nodeid();
		goto out;
	}

	hash = gdlm_hash(name, length);
	node = (hash >> 16) & ls->ls_nodes_mask;
	node %= ls->ls_num_nodes;

	list_for_each(tmp, &ls->ls_nodes) {
		if (n++ != node)
			continue;
		csb = list_entry(tmp, gd_csb_t, csb_list);
		break;
	}

	GDLM_ASSERT(csb, printk("num_nodes=%u n=%u node=%u mask=%x\n",
				ls->ls_num_nodes, n, node, ls->ls_nodes_mask););
	nodeid = csb->csb_node->gn_nodeid;

      out:
	return nodeid;
}

uint32_t get_directory_nodeid(gd_res_t *rsb)
{
	return name_to_directory_nodeid(rsb->res_ls, rsb->res_name,
					rsb->res_length);
}

static inline uint32_t rd_hash(gd_ls_t *ls, char *name, int len)
{
	uint32_t val;

	val = gdlm_hash(name, len);
	val &= RESDIRHASH_MASK;

	return val;
}

static void add_resdata_to_hash(gd_ls_t *ls, gd_resdata_t *rd)
{
	gd_resdir_bucket_t *bucket;
	uint32_t hashval;

	hashval = rd_hash(ls, rd->rd_name, rd->rd_length);
	bucket = &ls->ls_resdir_hash[hashval];

	list_add_tail(&rd->rd_list, &bucket->rb_reslist);
}

static gd_resdata_t *search_rdbucket(gd_ls_t *ls, char *name, int namelen,
				     uint32_t bucket)
{
	struct list_head *head;
	gd_resdata_t *rd;

	head = &ls->ls_resdir_hash[bucket].rb_reslist;
	list_for_each_entry(rd, head, rd_list) {
		if (rd->rd_length == namelen &&
		    !memcmp(name, rd->rd_name, namelen))
			goto out;
	}
	rd = NULL;
      out:
	return rd;
}

void remove_resdata(gd_ls_t *ls, uint32_t nodeid, char *name, int namelen,
		    uint8_t sequence)
{
	gd_resdata_t *rd;
	uint32_t bucket;

	bucket = rd_hash(ls, name, namelen);

	write_lock(&ls->ls_resdir_hash[bucket].rb_lock);

	rd = search_rdbucket(ls, name, namelen, bucket);

	if (!rd) {
		log_debug(ls, "remove from %u seq %u none", nodeid, sequence);
		goto out;
	}

	if (rd->rd_master_nodeid != nodeid) {
		log_debug(ls, "remove from %u seq %u ID %u seq %3u",
			  nodeid, sequence, rd->rd_master_nodeid,
			  rd->rd_sequence);
		goto out;
	}

	if (rd->rd_sequence == sequence) {
		log_debug(ls, "remove from %u seq %u", nodeid, sequence);
		list_del(&rd->rd_list);
		free_resdata(rd);
	} else {
		log_debug(ls, "remove from %u seq %u id %u SEQ %3u",
			  nodeid, sequence, rd->rd_master_nodeid,
			  rd->rd_sequence);
	}

      out:
	write_unlock(&ls->ls_resdir_hash[bucket].rb_lock);
}

void resdir_clear(gd_ls_t *ls)
{
	struct list_head *head;
	gd_resdata_t *rd;
	int i;

	for (i = 0; i < RESDIRHASH_SIZE; i++) {
		head = &ls->ls_resdir_hash[i].rb_reslist;
		while (!list_empty(head)) {
			rd = list_entry(head->next, gd_resdata_t, rd_list);
			list_del(&rd->rd_list);
			free_resdata(rd);
		}
	}
}

static void gdlm_resmov_in(gd_resmov_t *rm, char *buf)
{
	gd_resmov_t tmp;

	memcpy(&tmp, buf, sizeof(gd_resmov_t));

	rm->rm_nodeid = be32_to_cpu(tmp.rm_nodeid);
	rm->rm_length = be16_to_cpu(tmp.rm_length);
}

int resdir_rebuild_local(gd_ls_t *ls)
{
	gd_csb_t *csb;
	gd_resdata_t *rd;
	gd_rcom_t *rc;
	gd_resmov_t mov, last_mov;
	char *b, *last_name;
	int error = -ENOMEM, count = 0;

	log_all(ls, "rebuild resource directory");

	resdir_clear(ls);

	rc = allocate_rcom_buffer(ls);
	if (!rc)
		goto out;

	last_name = (char *) kmalloc(DLM_RESNAME_MAXLEN, GFP_KERNEL);
	if (!last_name)
		goto free_rc;

	list_for_each_entry(csb, &ls->ls_nodes, csb_list) {
		last_mov.rm_length = 0;
		for (;;) {
			error = gdlm_recovery_stopped(ls);
			if (error)
				goto free_last;

			memcpy(rc->rc_buf, last_name, last_mov.rm_length);
			rc->rc_datalen = last_mov.rm_length;

			error = rcom_send_message(ls, csb->csb_node->gn_nodeid,
						  RECCOMM_RECOVERNAMES, rc, 1);
			if (error)
				goto free_last;

			schedule();

			/* 
			 * pick each res out of buffer
			 */

			b = rc->rc_buf;

			for (;;) {
				gdlm_resmov_in(&mov, b);
				b += sizeof(gd_resmov_t);

				/* Length of 0 with a non-zero nodeid marks the 
				 * end of the list */
				if (!mov.rm_length && mov.rm_nodeid)
					goto done;

				/* This is just the end of the block */
				if (!mov.rm_length)
					break;

				error = -ENOMEM;
				rd = allocate_resdata(ls, mov.rm_length);
				if (!rd)
					goto free_last;

				rd->rd_master_nodeid = mov.rm_nodeid;
				rd->rd_length = mov.rm_length;
				rd->rd_sequence = 1;

				memcpy(rd->rd_name, b, mov.rm_length);
				b += mov.rm_length;

				add_resdata_to_hash(ls, rd);
				count++;

				last_mov = mov;
				memset(last_name, 0, DLM_RESNAME_MAXLEN);
				memcpy(last_name, rd->rd_name, rd->rd_length);
			}
		}
	      done:
		;
	}

	set_bit(LSFL_RESDIR_VALID, &ls->ls_flags);
	error = 0;

	log_all(ls, "rebuilt %d resources", count);

      free_last:
	kfree(last_name);

      free_rc:
	free_rcom_buffer(rc);

      out:
	return error;
}

/* 
 * The reply end of resdir_rebuild_local/RECOVERNAMES.  Collect and send as
 * many resource names as can fit in the buffer.
 */

int resdir_rebuild_send(gd_ls_t *ls, char *inbuf, int inlen, char *outbuf,
			int outlen, uint32_t nodeid)
{
	struct list_head *list;
	gd_res_t *start_rsb = NULL, *rsb;
	int offset = 0, start_namelen, error;
	char *start_name;
	gd_resmov_t tmp;
	uint32_t dir_nodeid;

	/* 
	 * Find the rsb where we left off (or start again)
	 */

	start_namelen = inlen;
	start_name = inbuf;

	if (start_namelen > 1) {
		error = find_or_create_rsb(ls, NULL, start_name,
				           start_namelen, 0, &start_rsb);
		GDLM_ASSERT(!error && start_rsb, printk("error %d\n", error););
		release_rsb(start_rsb);
	}

	/* 
	 * Send rsb names for rsb's we're master of and whose directory node
	 * matches the requesting node.
	 */

	down_read(&ls->ls_rec_rsblist);
	if (start_rsb)
		list = start_rsb->res_rootlist.next;
	else
		list = ls->ls_rootres.next;

	for (offset = 0; list != &ls->ls_rootres; list = list->next) {
		rsb = list_entry(list, gd_res_t, res_rootlist);
		if (rsb->res_nodeid)
			continue;

		dir_nodeid = get_directory_nodeid(rsb);
		if (dir_nodeid != nodeid)
			continue;

		if (offset + sizeof(gd_resmov_t)*2 + rsb->res_length > outlen) {
			/* Write end-of-block record */
			memset(&tmp, 0, sizeof(gd_resmov_t));
			memcpy(outbuf + offset, &tmp, sizeof(gd_resmov_t));
			offset += sizeof(gd_resmov_t);
			goto out;
		}

		memset(&tmp, 0, sizeof(gd_resmov_t));
		tmp.rm_nodeid = cpu_to_be32(our_nodeid());
		tmp.rm_length = cpu_to_be16(rsb->res_length);

		memcpy(outbuf + offset, &tmp, sizeof(gd_resmov_t));
		offset += sizeof(gd_resmov_t);

		memcpy(outbuf + offset, rsb->res_name, rsb->res_length);
		offset += rsb->res_length;
	}

	/* 
	 * If we've reached the end of the list (and there's room) write a
	 * terminating record.
	 */

	if ((list == &ls->ls_rootres) &&
	    (offset + sizeof(gd_resmov_t) <= outlen)) {

		memset(&tmp, 0, sizeof(gd_resmov_t));
		/* This only needs to be non-zero */
		tmp.rm_nodeid = cpu_to_be32(1);
		/* and this must be zero */
		tmp.rm_length = 0;
		memcpy(outbuf + offset, &tmp, sizeof(gd_resmov_t));
		offset += sizeof(gd_resmov_t);
	}

 out:
	up_read(&ls->ls_rec_rsblist);
	return offset;
}

static void inc_sequence(gd_resdata_t *rd, int recovery)
{
	if (!recovery) {
		if (++rd->rd_sequence == 0)
			rd->rd_sequence++;
	} else
		rd->rd_sequence = 1;
}

static int get_resdata(gd_ls_t *ls, uint32_t nodeid, char *name, int namelen,
		       uint32_t *r_nodeid, uint8_t *r_seq, int recovery)
{
	gd_resdata_t *rd, *tmp;
	uint32_t bucket;
	char strname[namelen+1];

	memset(strname, 0, namelen+1);
	memcpy(strname, name, namelen);

	bucket = rd_hash(ls, name, namelen);

	write_lock(&ls->ls_resdir_hash[bucket].rb_lock);
	rd = search_rdbucket(ls, name, namelen, bucket);
	if (rd) {
		inc_sequence(rd, recovery);
		*r_nodeid = rd->rd_master_nodeid;
		*r_seq = rd->rd_sequence;
		write_unlock(&ls->ls_resdir_hash[bucket].rb_lock);
		goto out;
	}

        write_unlock(&ls->ls_resdir_hash[bucket].rb_lock);

	rd = allocate_resdata(ls, namelen);
	if (!rd)
		return -ENOMEM;

	rd->rd_master_nodeid = nodeid;
	rd->rd_length = namelen;
	rd->rd_sequence = 1;
	memcpy(rd->rd_name, name, namelen);

	write_lock(&ls->ls_resdir_hash[bucket].rb_lock);
	tmp = search_rdbucket(ls, name, namelen, bucket);
	if (tmp) {
		free_resdata(rd);
		rd = tmp;
		inc_sequence(rd, recovery);
	} else
		list_add_tail(&rd->rd_list,
			      &ls->ls_resdir_hash[bucket].rb_reslist);
	*r_nodeid = rd->rd_master_nodeid;
	*r_seq = rd->rd_sequence;
	write_unlock(&ls->ls_resdir_hash[bucket].rb_lock);

 out:
	return 0;
}

int dlm_dir_lookup(gd_ls_t *ls, uint32_t nodeid, char *name, int namelen,
		   uint32_t *r_nodeid, uint8_t *r_seq)
{
	return get_resdata(ls, nodeid, name, namelen, r_nodeid, r_seq, 0);
}

int dlm_dir_lookup_recovery(gd_ls_t *ls, uint32_t nodeid, char *name,
			    int namelen, uint32_t *r_nodeid)
{
	uint8_t seq;
	return get_resdata(ls, nodeid, name, namelen, r_nodeid, &seq, 1);
}

/* 
 * The node with lowest id queries all nodes to determine when all are done.
 * All other nodes query the low nodeid for this.
 */

int resdir_rebuild_wait(gd_ls_t *ls)
{
	int error;

	if (ls->ls_low_nodeid == our_nodeid()) {
		error = gdlm_wait_status_all(ls, RESDIR_VALID);
		if (!error)
			set_bit(LSFL_ALL_RESDIR_VALID, &ls->ls_flags);
	} else
		error = gdlm_wait_status_low(ls, RESDIR_ALL_VALID);

	return error;
}
