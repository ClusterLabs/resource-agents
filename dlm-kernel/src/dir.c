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

static LIST_HEAD(expired_resdata_list);
static spinlock_t expired_resdata_lock = SPIN_LOCK_UNLOCKED;

struct resmov {
	uint32_t rm_nodeid;
	uint16_t rm_length;
	uint16_t rm_pad;
};


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

uint32_t name_to_directory_nodeid(struct dlm_ls *ls, char *name, int length)
{
	struct list_head *tmp;
	struct dlm_csb *csb = NULL;
	uint32_t hash, node, n = 0, nodeid;

	if (ls->ls_num_nodes == 1) {
		nodeid = our_nodeid();
		goto out;
	}

	hash = dlm_hash(name, length);
	node = (hash >> 16) & ls->ls_nodes_mask;
	node %= ls->ls_num_nodes;

	list_for_each(tmp, &ls->ls_nodes) {
		if (n++ != node)
			continue;
		csb = list_entry(tmp, struct dlm_csb, list);
		break;
	}

	DLM_ASSERT(csb, printk("num_nodes=%u n=%u node=%u mask=%x\n",
				ls->ls_num_nodes, n, node, ls->ls_nodes_mask););
	nodeid = csb->node->nodeid;

      out:
	return nodeid;
}

uint32_t get_directory_nodeid(struct dlm_rsb *rsb)
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

static void add_resdata_to_hash(struct dlm_ls *ls, struct dlm_direntry *de)
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

void remove_resdata(struct dlm_ls *ls, uint32_t nodeid, char *name, int namelen,
		    uint8_t sequence)
{
	struct dlm_direntry *de;
	uint32_t bucket;

	bucket = dir_hash(ls, name, namelen);

	write_lock(&ls->ls_dirtbl[bucket].lock);

	de = search_bucket(ls, name, namelen, bucket);

	if (!de) {
		log_debug(ls, "remove from %u seq %u none", nodeid, sequence);
		goto out;
	}

	if (de->master_nodeid != nodeid) {
		log_debug(ls, "remove from %u seq %u ID %u seq %3u",
			  nodeid, sequence, de->master_nodeid,
			  de->sequence);
		goto out;
	}

	if (de->sequence == sequence) {
		/* Expire immediately if timeout is 0 */
		if (!dlm_config.resdir_expiretime) {
			list_del(&de->list);
			free_resdata(de);
			goto out;
		}
		/* If it's already on the timeout list, then just adjust the
		 * expiry due time.
		 */
	        if (!de->duetime) {
			spin_lock(&expired_resdata_lock);
			list_add_tail(&de->expirelist, &expired_resdata_list);
			spin_unlock(&expired_resdata_lock);
	        }
	    	de->duetime = jiffies + dlm_config.resdir_expiretime*HZ; 
	} else {
		log_debug(ls, "remove from %u seq %u id %u SEQ %3u",
			  nodeid, sequence, de->master_nodeid,
			  de->sequence);
	}

 out:
	write_unlock(&ls->ls_dirtbl[bucket].lock);
}

/* Called now and then from astd */
void process_expired_resdata(void)
{
	struct dlm_direntry *de;
	struct list_head *pos, *tmp;
	
	spin_lock(&expired_resdata_lock);
	list_for_each_safe(pos, tmp, &expired_resdata_list) {
		de = list_entry(pos, struct dlm_direntry, expirelist);

		if (time_after(jiffies, de->duetime)) {
			list_del(&de->list);
			list_del(&de->expirelist);
			free_resdata(de);
		}
	}
	spin_unlock(&expired_resdata_lock);
}

void dlm_dir_clear(struct dlm_ls *ls)
{
	struct list_head *head;
	struct dlm_direntry *de;
	int i;

	for (i = 0; i < ls->ls_dirtbl_size; i++) {
		head = &ls->ls_dirtbl[i].list;
		while (!list_empty(head)) {
			de = list_entry(head->next, struct dlm_direntry, list);
			list_del(&de->list);
			if (de->duetime) {
				spin_lock(&expired_resdata_lock);
				list_del(&de->expirelist);
				spin_unlock(&expired_resdata_lock);
			}
			free_resdata(de);
		}
	}
}

static void resmov_in(struct resmov *rm, char *buf)
{
	struct resmov tmp;

	memcpy(&tmp, buf, sizeof(struct resmov));

	rm->rm_nodeid = be32_to_cpu(tmp.rm_nodeid);
	rm->rm_length = be16_to_cpu(tmp.rm_length);
}

int dlm_dir_rebuild_local(struct dlm_ls *ls)
{
	struct dlm_csb *csb;
	struct dlm_direntry *de;
	struct dlm_rcom *rc;
	struct resmov mov, last_mov;
	char *b, *last_name;
	int error = -ENOMEM, count = 0;

	log_all(ls, "rebuild resource directory");

	dlm_dir_clear(ls);

	rc = allocate_rcom_buffer(ls);
	if (!rc)
		goto out;

	last_name = (char *) kmalloc(DLM_RESNAME_MAXLEN, GFP_KERNEL);
	if (!last_name)
		goto free_rc;

	list_for_each_entry(csb, &ls->ls_nodes, list) {
		last_mov.rm_length = 0;
		for (;;) {
			error = dlm_recovery_stopped(ls);
			if (error)
				goto free_last;

			memcpy(rc->rc_buf, last_name, last_mov.rm_length);
			rc->rc_datalen = last_mov.rm_length;

			error = rcom_send_message(ls, csb->node->nodeid,
						  RECCOMM_RECOVERNAMES, rc, 1);
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

				error = -ENOMEM;
				de = allocate_resdata(ls, mov.rm_length);
				if (!de)
					goto free_last;

				de->master_nodeid = mov.rm_nodeid;
				de->length = mov.rm_length;
				de->sequence = 1;

				memcpy(de->name, b, mov.rm_length);
				b += mov.rm_length;

				add_resdata_to_hash(ls, de);
				count++;

				last_mov = mov;
				memset(last_name, 0, DLM_RESNAME_MAXLEN);
				memcpy(last_name, de->name, de->length);
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
 * The reply end of dlm_dir_rebuild_local/RECOVERNAMES.  Collect and send as
 * many resource names as can fit in the buffer.
 */

int dlm_dir_rebuild_send(struct dlm_ls *ls, char *inbuf, int inlen,
			 char *outbuf, int outlen, uint32_t nodeid)
{
	struct list_head *list;
	struct dlm_rsb *start_rsb = NULL, *rsb;
	int offset = 0, start_namelen, error;
	char *start_name;
	struct resmov tmp;
	uint32_t dir_nodeid;

	/* 
	 * Find the rsb where we left off (or start again)
	 */

	start_namelen = inlen;
	start_name = inbuf;

	if (start_namelen > 1) {
		error = find_or_create_rsb(ls, NULL, start_name,
				           start_namelen, 0, &start_rsb);
		DLM_ASSERT(!error && start_rsb, printk("error %d\n", error););
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
		rsb = list_entry(list, struct dlm_rsb, res_rootlist);
		if (rsb->res_nodeid)
			continue;

		dir_nodeid = get_directory_nodeid(rsb);
		if (dir_nodeid != nodeid)
			continue;

		if (offset + sizeof(struct resmov)*2 + rsb->res_length > outlen) {
			/* Write end-of-block record */
			memset(&tmp, 0, sizeof(struct resmov));
			memcpy(outbuf + offset, &tmp, sizeof(struct resmov));
			offset += sizeof(struct resmov);
			goto out;
		}

		memset(&tmp, 0, sizeof(struct resmov));
		tmp.rm_nodeid = cpu_to_be32(our_nodeid());
		tmp.rm_length = cpu_to_be16(rsb->res_length);

		memcpy(outbuf + offset, &tmp, sizeof(struct resmov));
		offset += sizeof(struct resmov);

		memcpy(outbuf + offset, rsb->res_name, rsb->res_length);
		offset += rsb->res_length;
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
	up_read(&ls->ls_rec_rsblist);
	return offset;
}

static void inc_sequence(struct dlm_direntry *de, int recovery)
{
	if (!recovery) {
		if (++de->sequence == 0)
			de->sequence++;
	} else
		de->sequence = 1;
}

static int get_resdata(struct dlm_ls *ls, uint32_t nodeid, char *name,
		       int namelen, uint32_t *r_nodeid, uint8_t *r_seq,
		       int recovery)
{
	struct dlm_direntry *de, *tmp;
	uint32_t bucket;

	bucket = dir_hash(ls, name, namelen);

	write_lock(&ls->ls_dirtbl[bucket].lock);
	de = search_bucket(ls, name, namelen, bucket);
	if (de) {
		inc_sequence(de, recovery);
		*r_nodeid = de->master_nodeid;
		*r_seq = de->sequence;
		write_unlock(&ls->ls_dirtbl[bucket].lock);
		goto out;
	}

        write_unlock(&ls->ls_dirtbl[bucket].lock);

	de = allocate_resdata(ls, namelen);
	if (!de)
		return -ENOMEM;

	de->master_nodeid = nodeid;
	de->length = namelen;
	de->sequence = 1;
	memcpy(de->name, name, namelen);

	write_lock(&ls->ls_dirtbl[bucket].lock);
	tmp = search_bucket(ls, name, namelen, bucket);
	if (tmp) {
		free_resdata(de);
		de = tmp;
		inc_sequence(de, recovery);
	} else
		list_add_tail(&de->list, &ls->ls_dirtbl[bucket].list);

	if (de->duetime) {
		spin_lock(&expired_resdata_lock);
		list_del(&de->expirelist);
		spin_unlock(&expired_resdata_lock);
	    	de->duetime = 0L;
	}
	*r_nodeid = de->master_nodeid;
	*r_seq = de->sequence;
	write_unlock(&ls->ls_dirtbl[bucket].lock);

 out:
	return 0;
}

int dlm_dir_lookup(struct dlm_ls *ls, uint32_t nodeid, char *name, int namelen,
		   uint32_t *r_nodeid, uint8_t *r_seq)
{
	return get_resdata(ls, nodeid, name, namelen, r_nodeid, r_seq, 0);
}

int dlm_dir_lookup_recovery(struct dlm_ls *ls, uint32_t nodeid, char *name,
			    int namelen, uint32_t *r_nodeid)
{
	uint8_t seq;
	return get_resdata(ls, nodeid, name, namelen, r_nodeid, &seq, 1);
}

/* 
 * The node with lowest id queries all nodes to determine when all are done.
 * All other nodes query the low nodeid for this.
 */

int dlm_dir_rebuild_wait(struct dlm_ls *ls)
{
	int error;

	if (ls->ls_low_nodeid == our_nodeid()) {
		error = dlm_wait_status_all(ls, RESDIR_VALID);
		if (!error)
			set_bit(LSFL_ALL_RESDIR_VALID, &ls->ls_flags);
	} else
		error = dlm_wait_status_low(ls, RESDIR_ALL_VALID);

	return error;
}
