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

/*
 * Rebuild RSB's on new masters.  Functions for transferring locks and
 * subresources to new RSB masters during recovery.
 */

#include "dlm_internal.h"
#include "reccomms.h"
#include "lkb.h"
#include "rsb.h"
#include "nodes.h"
#include "config.h"
#include "memory.h"
#include "recover.h"


/* Types of entity serialised in remastering messages */
#define REMASTER_ROOTRSB 1
#define REMASTER_RSB     2
#define REMASTER_LKB     3

struct rcom_fill {
	char *			outbuf;		/* Beginning of data */
	int 			offset;		/* Current offset into outbuf */
	int 			maxlen;		/* Max value of offset */
	int 			remasterid;
	int 			count;
	struct dlm_rsb *	rsb;
	struct dlm_rsb *	subrsb;
	struct dlm_lkb *	lkb;
	struct list_head *	lkbqueue;
	char 			more;
};
typedef struct rcom_fill rcom_fill_t;


struct rebuild_node {
	struct list_head	list;
	int 			nodeid;
	struct dlm_rsb *	rootrsb;
};
typedef struct rebuild_node rebuild_node_t;


/*
 * Root rsb passed in for which all lkb's (own and subrsbs) will be sent to new
 * master.  The rsb will be "done" with recovery when the new master has
 * replied with all the new remote lockid's for this rsb's lkb's.
 */

void expect_new_lkids(struct dlm_rsb *rsb)
{
	rsb->res_newlkid_expect = 0;
	recover_list_add(rsb);
}

/*
 * This function is called on root rsb or subrsb when another lkb is being sent
 * to the new master for which we expect to receive a corresponding remote lkid
 */

void need_new_lkid(struct dlm_rsb *rsb)
{
	struct dlm_rsb *root = rsb;

	if (rsb->res_parent)
		root = rsb->res_root;

	if (!root->res_newlkid_expect)
		recover_list_add(root);
	else
		DLM_ASSERT(test_bit(RESFL_RECOVER_LIST, &root->res_flags),);

	root->res_newlkid_expect++;
}

/*
 * This function is called for each lkb for which a new remote lkid is
 * received.  Decrement the expected number of remote lkids expected for the
 * root rsb.
 */

void have_new_lkid(struct dlm_lkb *lkb)
{
	struct dlm_rsb *root = lkb->lkb_resource;

	if (root->res_parent)
		root = root->res_root;

	down_write(&root->res_lock);

	DLM_ASSERT(root->res_newlkid_expect,
		   printk("newlkid_expect=%d\n", root->res_newlkid_expect););

	root->res_newlkid_expect--;

	if (!root->res_newlkid_expect) {
		clear_bit(RESFL_NEW_MASTER, &root->res_flags);
		recover_list_del(root);
	}
	up_write(&root->res_lock);
}

/*
 * Return the rebuild struct for a node - will create an entry on the rootrsb
 * list if necessary.
 *
 * Currently no locking is needed here as it all happens in the dlm_recvd
 * thread
 */

static rebuild_node_t *find_rebuild_root(struct dlm_ls *ls, int nodeid)
{
	rebuild_node_t *node = NULL;

	list_for_each_entry(node, &ls->ls_rebuild_rootrsb_list, list) {
		if (node->nodeid == nodeid)
			return node;
	}

	/* Not found, add one */
	node = kmalloc(sizeof(rebuild_node_t), GFP_KERNEL);
	if (!node)
		return NULL;

	node->nodeid = nodeid;
	node->rootrsb = NULL;
	list_add(&node->list, &ls->ls_rebuild_rootrsb_list);

	return node;
}

/*
 * Tidy up after a rebuild run.  Called when all recovery has finished
 */

void rebuild_freemem(struct dlm_ls *ls)
{
	rebuild_node_t *node = NULL, *s;

	list_for_each_entry_safe(node, s, &ls->ls_rebuild_rootrsb_list, list) {
		list_del(&node->list);
		kfree(node);
	}
}

static void put_int(int x, char *buf, int *offp)
{
	x = cpu_to_le32(x);
	memcpy(buf + *offp, &x, sizeof(int));
	*offp += sizeof(int);
}

static void put_int64(uint64_t x, char *buf, int *offp)
{
	x = cpu_to_le64(x);
	memcpy(buf + *offp, &x, sizeof(uint64_t));
	*offp += sizeof(uint64_t);
}

static void put_bytes(char *x, int len, char *buf, int *offp)
{
	put_int(len, buf, offp);
	memcpy(buf + *offp, x, len);
	*offp += len;
}

static void put_char(char x, char *buf, int *offp)
{
	buf[*offp] = x;
	*offp += 1;
}

static int get_int(char *buf, int *offp)
{
	int value;
	memcpy(&value, buf + *offp, sizeof(int));
	*offp += sizeof(int);
	return le32_to_cpu(value);
}

static uint64_t get_int64(char *buf, int *offp)
{
	uint64_t value;

	memcpy(&value, buf + *offp, sizeof(uint64_t));
	*offp += sizeof(uint64_t);
	return le64_to_cpu(value);
}

static char get_char(char *buf, int *offp)
{
	char x = buf[*offp];

	*offp += 1;
	return x;
}

static void get_bytes(char *bytes, int *len, char *buf, int *offp)
{
	*len = get_int(buf, offp);
	memcpy(bytes, buf + *offp, *len);
	*offp += *len;
}

static int lkb_length(struct dlm_lkb *lkb)
{
	int len = 0;

	len += sizeof(int);	/* lkb_id */
	len += sizeof(int);	/* lkb_resource->res_reamasterid */
	len += sizeof(int);	/* lkb_flags */
	len += sizeof(int);	/* lkb_lockqueue_flags */
	len += sizeof(int);	/* lkb_status */
	len += sizeof(char);	/* lkb_rqmode */
	len += sizeof(char);	/* lkb_grmode */
	len += sizeof(int);	/* lkb_childcnt */
	len += sizeof(int);	/* lkb_parent->lkb_id */
	len += sizeof(int);	/* lkb_bastaddr */
	len += sizeof(int);     /* lkb_ownpid */
	len += sizeof(int);     /* lkb_lvbseq */

	if (lkb->lkb_flags & GDLM_LKFLG_VALBLK) {
		len += sizeof(int);	/* number of lvb bytes */
		len += DLM_LVB_LEN;
	}

	if (lkb->lkb_range) {
		len += sizeof(uint64_t);
		len += sizeof(uint64_t);
		if (lkb->lkb_status == GDLM_LKSTS_CONVERT) {
			len += sizeof(uint64_t);
			len += sizeof(uint64_t);
		}
	}

	return len;
}

/*
 * It's up to the caller to be sure there's enough space in the buffer.
 */

static void serialise_lkb(struct dlm_lkb *lkb, char *buf, int *offp)
{
	int flags;

	/* Need to tell the remote end if we have a range */
	flags = lkb->lkb_flags;
	if (lkb->lkb_range)
		flags |= GDLM_LKFLG_RANGE;

	/*
	 * See lkb_length()
	 * Total: 30 (no lvb) or 66 (with lvb) bytes
	 */

	put_int(lkb->lkb_id, buf, offp);
	put_int(lkb->lkb_resource->res_remasterid, buf, offp);
	put_int(flags, buf, offp);
	put_int(lkb->lkb_lockqueue_flags, buf, offp);
	put_int(lkb->lkb_status, buf, offp);
	put_char(lkb->lkb_rqmode, buf, offp);
	put_char(lkb->lkb_grmode, buf, offp);
	put_int(atomic_read(&lkb->lkb_childcnt), buf, offp);

	if (lkb->lkb_parent)
		put_int(lkb->lkb_parent->lkb_id, buf, offp);
	else
		put_int(0, buf, offp);

	if (lkb->lkb_bastaddr)
		put_int(1, buf, offp);
	else
		put_int(0, buf, offp);
	put_int(lkb->lkb_ownpid, buf, offp);
	put_int(lkb->lkb_lvbseq, buf, offp);

	if (lkb->lkb_flags & GDLM_LKFLG_VALBLK) {
		DLM_ASSERT(lkb->lkb_lvbptr,);
		put_bytes(lkb->lkb_lvbptr, DLM_LVB_LEN, buf, offp);
	}

	/* Only send the range we actually need */
	if (lkb->lkb_range) {
		switch (lkb->lkb_status) {
		case GDLM_LKSTS_CONVERT:
			put_int64(lkb->lkb_range[RQ_RANGE_START], buf, offp);
			put_int64(lkb->lkb_range[RQ_RANGE_END], buf, offp);
			put_int64(lkb->lkb_range[GR_RANGE_START], buf, offp);
			put_int64(lkb->lkb_range[GR_RANGE_END], buf, offp);
			break;
		case GDLM_LKSTS_WAITING:
			put_int64(lkb->lkb_range[RQ_RANGE_START], buf, offp);
			put_int64(lkb->lkb_range[RQ_RANGE_END], buf, offp);
			break;
		case GDLM_LKSTS_GRANTED:
			put_int64(lkb->lkb_range[GR_RANGE_START], buf, offp);
			put_int64(lkb->lkb_range[GR_RANGE_END], buf, offp);
			break;
		default:
			DLM_ASSERT(0,);
		}
	}
}

static int rsb_length(struct dlm_rsb *rsb)
{
	int len = 0;

	len += sizeof(int);	/* number of res_name bytes */
	len += rsb->res_length;	/* res_name */
	len += sizeof(int);	/* res_remasterid */
	len += sizeof(int);	/* res_parent->res_remasterid */

	return len;
}

static inline struct dlm_rsb *next_subrsb(struct dlm_rsb *subrsb)
{
	struct list_head *tmp;
	struct dlm_rsb *r;

	tmp = subrsb->res_subreslist.next;
	r = list_entry(tmp, struct dlm_rsb, res_subreslist);

	return r;
}

static inline int last_in_list(struct dlm_rsb *r, struct list_head *head)
{
	struct dlm_rsb *last;
	last = list_entry(head->prev, struct dlm_rsb, res_subreslist);
	if (last == r)
		return 1;
	return 0;
}

static int lkbs_to_remaster_list(struct list_head *head)
{
	struct dlm_lkb *lkb;

	list_for_each_entry(lkb, head, lkb_statequeue) {
		if (lkb->lkb_flags & GDLM_LKFLG_NOREBUILD)
			continue;
		return TRUE;
	}
	return FALSE;
}

/*
 * Used to decide if an rsb should be rebuilt on a new master.  An rsb only
 * needs to be rebuild if we have lkb's queued on it.  NOREBUILD lkb's are not
 * rebuilt.
 */

static int lkbs_to_remaster(struct dlm_rsb *r)
{
	struct dlm_rsb *sub;

	if (lkbs_to_remaster_list(&r->res_grantqueue))
		return TRUE;
	if (lkbs_to_remaster_list(&r->res_convertqueue))
		return TRUE;
	if (lkbs_to_remaster_list(&r->res_waitqueue))
		return TRUE;

	list_for_each_entry(sub, &r->res_subreslist, res_subreslist) {
		if (lkbs_to_remaster_list(&sub->res_grantqueue))
			return TRUE;
		if (lkbs_to_remaster_list(&sub->res_convertqueue))
			return TRUE;
		if (lkbs_to_remaster_list(&sub->res_waitqueue))
			return TRUE;
	}

	return FALSE;
}

static void serialise_rsb(struct dlm_rsb *rsb, char *buf, int *offp)
{
	/*
	 * See rsb_length()
	 * Total: 36 bytes (4 + 24 + 4 + 4)
	 */

	put_bytes(rsb->res_name, rsb->res_length, buf, offp);
	put_int(rsb->res_remasterid, buf, offp);

	if (rsb->res_parent)
		put_int(rsb->res_parent->res_remasterid, buf, offp);
	else
		put_int(0, buf, offp);

	DLM_ASSERT(!rsb->res_lvbptr,);
}

/*
 * Flatten an LKB into a buffer for sending to the new RSB master.  As a
 * side-effect the nodeid of the lock is set to the nodeid of the new RSB
 * master.
 */

static int pack_one_lkb(struct dlm_rsb *r, struct dlm_lkb *lkb,
			rcom_fill_t *fill)
{
	if (fill->offset + 1 + lkb_length(lkb) > fill->maxlen)
		goto nospace;

	lkb->lkb_nodeid = r->res_nodeid;

	put_char(REMASTER_LKB, fill->outbuf, &fill->offset);
	serialise_lkb(lkb, fill->outbuf, &fill->offset);

	fill->count++;
	need_new_lkid(r);
	return 0;

      nospace:
	return -ENOSPC;
}

/*
 * Pack all LKB's from a given queue, except for those with the NOREBUILD flag.
 */

static int pack_lkb_queue(struct dlm_rsb *r, struct list_head *queue,
			  rcom_fill_t *fill)
{
	struct dlm_lkb *lkb;
	int error;

	list_for_each_entry(lkb, queue, lkb_statequeue) {
		if (lkb->lkb_flags & GDLM_LKFLG_NOREBUILD)
			continue;

		error = pack_one_lkb(r, lkb, fill);
		if (error)
			goto nospace;
	}

	return 0;

      nospace:
	fill->lkb = lkb;
	fill->lkbqueue = queue;

	return error;
}

static int pack_lkb_queues(struct dlm_rsb *r, rcom_fill_t *fill)
{
	int error;

	error = pack_lkb_queue(r, &r->res_grantqueue, fill);
	if (error)
		goto nospace;

	error = pack_lkb_queue(r, &r->res_convertqueue, fill);
	if (error)
		goto nospace;

	error = pack_lkb_queue(r, &r->res_waitqueue, fill);

      nospace:
	return error;
}

/*
 * Pack remaining lkb's for rsb or subrsb.  This may include a partial lkb
 * queue and full lkb queues.
 */

static int pack_lkb_remaining(struct dlm_rsb *r, rcom_fill_t *fill)
{
	struct list_head *tmp, *start, *end;
	struct dlm_lkb *lkb;
	int error;

	/*
	 * Beginning with fill->lkb, pack remaining lkb's on fill->lkbqueue.
	 */

	error = pack_one_lkb(r, fill->lkb, fill);
	if (error)
		goto out;

	start = fill->lkb->lkb_statequeue.next;
	end = fill->lkbqueue;

	for (tmp = start; tmp != end; tmp = tmp->next) {
		lkb = list_entry(tmp, struct dlm_lkb, lkb_statequeue);

		error = pack_one_lkb(r, lkb, fill);
		if (error) {
			fill->lkb = lkb;
			goto out;
		}
	}

	/*
	 * Pack all lkb's on r's queues following fill->lkbqueue.
	 */

	if (fill->lkbqueue == &r->res_waitqueue)
		goto out;
	if (fill->lkbqueue == &r->res_convertqueue)
		goto skip;

	DLM_ASSERT(fill->lkbqueue == &r->res_grantqueue,);

	error = pack_lkb_queue(r, &r->res_convertqueue, fill);
	if (error)
		goto out;
      skip:
	error = pack_lkb_queue(r, &r->res_waitqueue, fill);

      out:
	return error;
}

static int pack_one_subrsb(struct dlm_rsb *rsb, struct dlm_rsb *subrsb,
			   rcom_fill_t *fill)
{
	int error;

	down_write(&subrsb->res_lock);

	if (fill->offset + 1 + rsb_length(subrsb) > fill->maxlen)
		goto nospace;

	subrsb->res_nodeid = rsb->res_nodeid;
	subrsb->res_remasterid = ++fill->remasterid;

	put_char(REMASTER_RSB, fill->outbuf, &fill->offset);
	serialise_rsb(subrsb, fill->outbuf, &fill->offset);

	error = pack_lkb_queues(subrsb, fill);
	if (error)
		goto nospace;

	up_write(&subrsb->res_lock);

	return 0;

      nospace:
	up_write(&subrsb->res_lock);
	fill->subrsb = subrsb;

	return -ENOSPC;
}

static int pack_subrsbs(struct dlm_rsb *rsb, struct dlm_rsb *in_subrsb,
			rcom_fill_t *fill)
{
	struct dlm_rsb *subrsb;
	int error = 0;

	/*
	 * When an initial subrsb is given, we know it needs to be packed.
	 * When no initial subrsb is given, begin with the first (if any exist).
	 */

	if (!in_subrsb) {
		if (list_empty(&rsb->res_subreslist))
			goto out;

		subrsb = list_entry(rsb->res_subreslist.next, struct dlm_rsb,
			       	    res_subreslist);
	} else
		subrsb = in_subrsb;

	for (;;) {
		error = pack_one_subrsb(rsb, subrsb, fill);
		if (error)
			goto out;

		if (last_in_list(subrsb, &rsb->res_subreslist))
			break;

		subrsb = next_subrsb(subrsb);
	}

      out:
	return error;
}

/*
 * Finish packing whatever is left in an rsb tree.  If space runs out while
 * finishing, save subrsb/lkb and this will be called again for the same rsb.
 *
 * !subrsb &&  lkb, we left off part way through root rsb's lkbs.
 *  subrsb && !lkb, we left off just before starting a new subrsb.
 *  subrsb &&  lkb, we left off part way through a subrsb's lkbs.
 * !subrsb && !lkb, we shouldn't be in this function, but starting
 *                  a new rsb in pack_rsb_tree().
 */

static int pack_rsb_tree_remaining(struct dlm_ls *ls, struct dlm_rsb *rsb,
				   rcom_fill_t *fill)
{
	struct dlm_rsb *subrsb = NULL;
	int error = 0;

	if (!fill->subrsb && fill->lkb) {
		error = pack_lkb_remaining(rsb, fill);
		if (error)
			goto out;

		error = pack_subrsbs(rsb, NULL, fill);
		if (error)
			goto out;
	}

	else if (fill->subrsb && !fill->lkb) {
		error = pack_subrsbs(rsb, fill->subrsb, fill);
		if (error)
			goto out;
	}

	else if (fill->subrsb && fill->lkb) {
		error = pack_lkb_remaining(fill->subrsb, fill);
		if (error)
			goto out;

		if (last_in_list(fill->subrsb, &fill->rsb->res_subreslist))
			goto out;

		subrsb = next_subrsb(fill->subrsb);

		error = pack_subrsbs(rsb, subrsb, fill);
		if (error)
			goto out;
	}

	fill->subrsb = NULL;
	fill->lkb = NULL;

      out:
	return error;
}

/*
 * Pack an RSB, all its LKB's, all its subrsb's and all their LKB's into a
 * buffer.  When the buffer runs out of space, save the place to restart (the
 * queue+lkb, subrsb, or subrsb+queue+lkb which wouldn't fit).
 */

static int pack_rsb_tree(struct dlm_ls *ls, struct dlm_rsb *rsb,
			 rcom_fill_t *fill)
{
	int error = -ENOSPC;

	fill->remasterid = 0;

	/*
	 * Pack the root rsb itself.  A 1 byte type precedes the serialised
	 * rsb.  Then pack the lkb's for the root rsb.
	 */

	down_write(&rsb->res_lock);

	if (fill->offset + 1 + rsb_length(rsb) > fill->maxlen)
		goto out;

	rsb->res_remasterid = ++fill->remasterid;
	put_char(REMASTER_ROOTRSB, fill->outbuf, &fill->offset);
	serialise_rsb(rsb, fill->outbuf, &fill->offset);

	error = pack_lkb_queues(rsb, fill);
	if (error)
		goto out;

	up_write(&rsb->res_lock);

	/*
	 * Pack subrsb/lkb's under the root rsb.
	 */

	error = pack_subrsbs(rsb, NULL, fill);

	return error;

      out:
	up_write(&rsb->res_lock);
	return error;
}

/*
 * Given an RSB, return the next RSB that should be sent to a new master.
 */

static struct dlm_rsb *next_remastered_rsb(struct dlm_ls *ls,
					   struct dlm_rsb *rsb)
{
	struct list_head *tmp, *start, *end;
	struct dlm_rsb *r;

	if (!rsb)
		start = ls->ls_rootres.next;
	else
		start = rsb->res_rootlist.next;

	end = &ls->ls_rootres;

	for (tmp = start; tmp != end; tmp = tmp->next) {
		r = list_entry(tmp, struct dlm_rsb, res_rootlist);

		if (test_bit(RESFL_NEW_MASTER, &r->res_flags)) {
			if (r->res_nodeid && lkbs_to_remaster(r)) {
				expect_new_lkids(r);
				return r;
			} else
				clear_bit(RESFL_NEW_MASTER, &r->res_flags);
		}
	}

	return NULL;
}

/*
 * Given an rcom buffer, fill it with RSB's that need to be sent to a single
 * new master node.  In the case where all the data to send to one node
 * requires multiple messages, this function needs to resume filling each
 * successive buffer from the point where it left off when the previous buffer
 * filled up.
 */

static void fill_rcom_buffer(struct dlm_ls *ls, rcom_fill_t *fill,
			     uint32_t *nodeid)
{
	struct dlm_rsb *rsb, *prev_rsb = fill->rsb;
	int error;

	fill->offset = 0;

	if (!prev_rsb) {

		/*
		 * The first time this function is called.
		 */

		rsb = next_remastered_rsb(ls, NULL);
		if (!rsb)
			goto no_more;

	} else if (fill->subrsb || fill->lkb) {

		/*
		 * Continue packing an rsb tree that was partially packed last
		 * time (fill->subrsb/lkb indicates where packing of last block
		 * left off)
		 */

		rsb = prev_rsb;
		*nodeid = rsb->res_nodeid;

		error = pack_rsb_tree_remaining(ls, rsb, fill);
		if (error == -ENOSPC)
			goto more;

		rsb = next_remastered_rsb(ls, prev_rsb);
		if (!rsb)
			goto no_more;

		if (rsb->res_nodeid != prev_rsb->res_nodeid)
			goto more;
	} else {
		rsb = prev_rsb;
	}

	/*
	 * Pack rsb trees into the buffer until we run out of space, run out of
	 * new rsb's or hit a new nodeid.
	 */

	*nodeid = rsb->res_nodeid;

	for (;;) {
		error = pack_rsb_tree(ls, rsb, fill);
		if (error == -ENOSPC)
			goto more;

		prev_rsb = rsb;

		rsb = next_remastered_rsb(ls, prev_rsb);
		if (!rsb)
			goto no_more;

		if (rsb->res_nodeid != prev_rsb->res_nodeid)
			goto more;
	}

      more:
	fill->more = 1;
	fill->rsb = rsb;
	return;

      no_more:
	fill->more = 0;
}

/*
 * Send lkb's (and subrsb/lkbs) for remastered root rsbs to new masters.
 */

int rebuild_rsbs_send(struct dlm_ls *ls)
{
	struct dlm_rcom *rc;
	rcom_fill_t fill;
	uint32_t nodeid;
	int error;

	DLM_ASSERT(recover_list_empty(ls),);

	log_debug(ls, "rebuild locks");

	error = -ENOMEM;
	rc = allocate_rcom_buffer(ls);
	if (!rc)
		goto ret;

	down_read(&ls->ls_root_lock);

	error = 0;
	memset(&fill, 0, sizeof(rcom_fill_t));
	fill.outbuf = rc->rc_buf;
	fill.maxlen = dlm_config.buffer_size - sizeof(struct dlm_rcom);

	do {
		fill_rcom_buffer(ls, &fill, &nodeid);
		if (!fill.offset)
			break;

		rc->rc_datalen = fill.offset;
		error = rcom_send_message(ls, nodeid, RECCOMM_NEWLOCKS, rc, 0);
		if (error) {
			up_read(&ls->ls_root_lock);
			goto out;
		}

		schedule();
		error = dlm_recovery_stopped(ls);
		if (error) {
			up_read(&ls->ls_root_lock);
			goto out;
		}
	}
	while (fill.more);

	up_read(&ls->ls_root_lock);

	error = dlm_wait_function(ls, &recover_list_empty);

	log_debug(ls, "rebuilt %d locks", fill.count);

      out:
	free_rcom_buffer(rc);

      ret:
	if (error)
		recover_list_clear(ls);
	return error;
}

static struct dlm_rsb *find_by_remasterid(struct dlm_ls *ls, int remasterid,
				    	  struct dlm_rsb *rootrsb)
{
	struct dlm_rsb *rsb;

	DLM_ASSERT(rootrsb,);

	if (rootrsb->res_remasterid == remasterid) {
		rsb = rootrsb;
		goto out;
	}

	list_for_each_entry(rsb, &rootrsb->res_subreslist, res_subreslist) {
		if (rsb->res_remasterid == remasterid)
			goto out;
	}
	rsb = NULL;

      out:
	return rsb;
}

/*
 * Search a queue for the given remote lock id (remlkid).
 */

static struct dlm_lkb *search_remlkid(struct list_head *statequeue, int nodeid,
				      int remid)
{
	struct dlm_lkb *lkb;

	list_for_each_entry(lkb, statequeue, lkb_statequeue) {
		if (lkb->lkb_nodeid == nodeid && lkb->lkb_remid == remid) {
			return lkb;
		}
	}

	return NULL;
}

/*
 * Given a remote lock ID (and a parent resource), return the local LKB for it
 * Hopefully we dont need to do this too often on deep lock trees.  This is
 * VERY suboptimal for anything but the smallest lock trees. It searches the
 * lock tree for an LKB with the remote id "remid" and the node "nodeid" and
 * returns the LKB address.  OPTIMISATION: we should keep a list of these while
 * we are building up the remastered LKBs
 */

static struct dlm_lkb *find_by_remlkid(struct dlm_rsb *rootrsb, int nodeid,
				       int remid)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *rsb;

	lkb = search_remlkid(&rootrsb->res_grantqueue, nodeid, remid);
	if (lkb)
		goto out;

	lkb = search_remlkid(&rootrsb->res_convertqueue, nodeid, remid);
	if (lkb)
		goto out;

	lkb = search_remlkid(&rootrsb->res_waitqueue, nodeid, remid);
	if (lkb)
		goto out;

	list_for_each_entry(rsb, &rootrsb->res_subreslist, res_subreslist) {
		lkb = search_remlkid(&rsb->res_grantqueue, nodeid, remid);
		if (lkb)
			goto out;

		lkb = search_remlkid(&rsb->res_convertqueue, nodeid, remid);
		if (lkb)
			goto out;

		lkb = search_remlkid(&rsb->res_waitqueue, nodeid, remid);
		if (lkb)
			goto out;
	}
	lkb = NULL;

      out:
	return lkb;
}

/*
 * Unpack an LKB from a remaster operation
 */

static int deserialise_lkb(struct dlm_ls *ls, int rem_nodeid,
			   struct dlm_rsb *rootrsb, char *buf, int *ptr,
			   char *outbuf, int *outoffp)
{
	struct dlm_lkb *lkb, *exist_lkb = NULL;
	struct dlm_rsb *rsb;
	int error = -ENOMEM, parentid, rsb_rmid, remote_lkid, status, temp;

	remote_lkid = get_int(buf, ptr);

	rsb_rmid = get_int(buf, ptr);
	rsb = find_by_remasterid(ls, rsb_rmid, rootrsb);
	DLM_ASSERT(rsb, printk("no RSB for remasterid %d\n", rsb_rmid););

	/*
	 * We could have received this lkb already from a previous recovery
	 * that was interrupted.  We still need to advance ptr so read in
	 * lkb and then release it.  FIXME: verify this is valid.
	 */
	lkb = find_by_remlkid(rsb, rem_nodeid, remote_lkid);
	if (lkb) {
		log_error(ls, "lkb %x exists %s", remote_lkid, rsb->res_name);
		exist_lkb = lkb;
	}

	lkb = create_lkb(ls);
	if (!lkb)
		goto out;

	lkb->lkb_remid = remote_lkid;
	lkb->lkb_flags = get_int(buf, ptr);
	lkb->lkb_lockqueue_flags = get_int(buf, ptr);
	status = get_int(buf, ptr);
	lkb->lkb_rqmode = get_char(buf, ptr);
	lkb->lkb_grmode = get_char(buf, ptr);
	atomic_set(&lkb->lkb_childcnt, get_int(buf, ptr));

	parentid = get_int(buf, ptr);
	lkb->lkb_bastaddr = (void *) (long) get_int(buf, ptr);
	lkb->lkb_ownpid = get_int(buf, ptr);
	lkb->lkb_lvbseq = get_int(buf, ptr);

	if (lkb->lkb_flags & GDLM_LKFLG_VALBLK) {
		lkb->lkb_lvbptr = allocate_lvb(ls);
		if (!lkb->lkb_lvbptr)
			goto out;
		get_bytes(lkb->lkb_lvbptr, &temp, buf, ptr);
	}

	if (lkb->lkb_flags & GDLM_LKFLG_RANGE) {
		uint64_t start, end;

		/* Don't need to keep the range flag, for comms use only */
		lkb->lkb_flags &= ~GDLM_LKFLG_RANGE;
		start = get_int64(buf, ptr);
		end = get_int64(buf, ptr);

		lkb->lkb_range = allocate_range(ls);
		if (!lkb->lkb_range)
			goto out;

		switch (status) {
		case GDLM_LKSTS_CONVERT:
			lkb->lkb_range[RQ_RANGE_START] = start;
			lkb->lkb_range[RQ_RANGE_END] = end;
			start = get_int64(buf, ptr);
			end = get_int64(buf, ptr);
			lkb->lkb_range[GR_RANGE_START] = start;
			lkb->lkb_range[GR_RANGE_END] = end;

		case GDLM_LKSTS_WAITING:
			lkb->lkb_range[RQ_RANGE_START] = start;
			lkb->lkb_range[RQ_RANGE_END] = end;
			break;

		case GDLM_LKSTS_GRANTED:
			lkb->lkb_range[GR_RANGE_START] = start;
			lkb->lkb_range[GR_RANGE_END] = end;
			break;
		default:
			DLM_ASSERT(0,);
		}
	}

	if (exist_lkb) {
		/* verify lkb and exist_lkb values match? */
		release_lkb(ls, lkb);
		lkb = exist_lkb;
		goto put_lkid;
	}

	/* Resolve local lock LKB address from parent ID */
	if (parentid)
		lkb->lkb_parent = find_by_remlkid(rootrsb, rem_nodeid,
				                  parentid);

	atomic_inc(&rsb->res_ref);
	lkb->lkb_resource = rsb;

	lkb->lkb_flags |= GDLM_LKFLG_MSTCPY;
	lkb->lkb_nodeid = rem_nodeid;

	/*
	 * Put the lkb on an RSB queue.  An lkb that's in the midst of a
	 * conversion request (on the requesting node's lockqueue and has
	 * LQCONVERT set) should be put on the granted queue.  The convert
	 * request will be resent by the requesting node.
	 */

	if (lkb->lkb_flags & GDLM_LKFLG_LQCONVERT) {
		lkb->lkb_flags &= ~GDLM_LKFLG_LQCONVERT;
		DLM_ASSERT(status == GDLM_LKSTS_CONVERT,
			    printk("status=%d\n", status););
		lkb->lkb_rqmode = DLM_LOCK_IV;
		status = GDLM_LKSTS_GRANTED;
	}

	lkb_enqueue(rsb, lkb, status);

	/*
	 * Clear flags that may have been sent over that are only relevant in
	 * the context of the sender.
	 */

	lkb->lkb_flags &= ~(GDLM_LKFLG_DELETED | GDLM_LKFLG_LQRESEND |
			    GDLM_LKFLG_NOREBUILD | GDLM_LKFLG_DEMOTED);

	if (lkb->lkb_flags & GDLM_LKFLG_VALBLK)
		rsb_lvb_recovery(rsb);

      put_lkid:
	/* Return the new LKID to the caller's buffer */
	put_int(lkb->lkb_id, outbuf, outoffp);
	put_int(lkb->lkb_remid, outbuf, outoffp);
	error = 0;

      out:
	return error;
}

static struct dlm_rsb *deserialise_rsb(struct dlm_ls *ls, int nodeid,
				       struct dlm_rsb *rootrsb, char *buf,
				       int *ptr)
{
	int length;
	int remasterid;
	int parent_remasterid;
	char name[DLM_RESNAME_MAXLEN];
	int error;
	struct dlm_rsb *parent = NULL;
	struct dlm_rsb *rsb;

	get_bytes(name, &length, buf, ptr);
	remasterid = get_int(buf, ptr);
	parent_remasterid = get_int(buf, ptr);

	if (parent_remasterid)
		parent = find_by_remasterid(ls, parent_remasterid, rootrsb);

	/*
	 * The rsb reference from this find_or_create_rsb() will keep the rsb
	 * around while we add new lkb's to it from deserialise_lkb.  Each of
	 * the lkb's will add an rsb reference.  The reference added here is
	 * removed by release_rsb() after all lkb's are added.
	 */

	error = find_rsb(ls, parent, name, length, CREATE, &rsb);
	DLM_ASSERT(!error,);

	set_bit(RESFL_MASTER, &rsb->res_flags);

	/* There is a case where the above needs to create the RSB. */
	if (rsb->res_nodeid == -1)
		rsb->res_nodeid = our_nodeid();

	rsb->res_remasterid = remasterid;
	rsb->res_parent = parent;

	return rsb;
}

/*
 * Processing at the receiving end of a NEWLOCKS message from a node in
 * rebuild_rsbs_send().  Rebuild a remastered lock tree.  Nodeid is the remote
 * node whose locks we are now mastering.  For a reply we need to send back the
 * new lockids of the remastered locks so that remote ops can find them.
 */

int rebuild_rsbs_recv(struct dlm_ls *ls, int nodeid, char *buf, int len)
{
	struct dlm_rcom *rc;
	struct dlm_rsb *rsb = NULL;
	rebuild_node_t *rnode;
	char *outbuf;
	int outptr, ptr = 0, error = -ENOMEM;

	rnode = find_rebuild_root(ls, nodeid);
	if (!rnode)
		goto out;

	/*
	 * Allocate a buffer for the reply message which is a list of remote
	 * lock IDs and their (new) local lock ids.  It will always be big
	 * enough to fit <n> ID pairs if it already fit <n> LKBs.
	 */

	rc = allocate_rcom_buffer(ls);
	if (!rc)
		goto out;
	outbuf = rc->rc_buf;
	outptr = 0;

	/*
	 * Unpack RSBs and LKBs, saving new LKB id's in outbuf as they're
	 * created.  Each deserialise_rsb adds an rsb reference that must be
	 * removed with release_rsb once all new lkb's for an rsb have been
	 * added.
	 */

	while (ptr < len) {
		int type;

		type = get_char(buf, &ptr);

		switch (type) {
		case REMASTER_ROOTRSB:
			if (rsb)
				release_rsb(rsb);
			rsb = deserialise_rsb(ls, nodeid, rnode->rootrsb, buf,
					      &ptr);
			rnode->rootrsb = rsb;
			break;

		case REMASTER_RSB:
			if (rsb)
				release_rsb(rsb);
			rsb = deserialise_rsb(ls, nodeid, rnode->rootrsb, buf,
					      &ptr);
			break;

		case REMASTER_LKB:
			deserialise_lkb(ls, nodeid, rnode->rootrsb, buf, &ptr,
					outbuf, &outptr);
			break;

		default:
			DLM_ASSERT(0, printk("type=%d nodeid=%u ptr=%d "
					      "len=%d\n", type, nodeid, ptr,
					      len););
		}
	}

	if (rsb)
		release_rsb(rsb);

	/*
	 * Reply with the new lock IDs.
	 */

	rc->rc_datalen = outptr;
	error = rcom_send_message(ls, nodeid, RECCOMM_NEWLOCKIDS, rc, 0);

	free_rcom_buffer(rc);

      out:
	return error;
}

/*
 * Processing for a NEWLOCKIDS message.  Called when we get the reply from the
 * new master telling us what the new remote lock IDs are for the remastered
 * locks
 */

int rebuild_rsbs_lkids_recv(struct dlm_ls *ls, int nodeid, char *buf, int len)
{
	int offset = 0;

	if (len == 1)
		len = 0;

	while (offset < len) {
		int remote_id;
		int local_id;
		struct dlm_lkb *lkb;

		if (offset + 8 > len) {
			log_error(ls, "rebuild_rsbs_lkids_recv: bad data "
				  "length nodeid=%d offset=%d len=%d",
				  nodeid, offset, len);
			break;
		}

		remote_id = get_int(buf, &offset);
		local_id = get_int(buf, &offset);

		lkb = find_lock_by_id(ls, local_id);
		if (lkb) {
			lkb->lkb_remid = remote_id;
			have_new_lkid(lkb);
		} else {
			log_error(ls, "rebuild_rsbs_lkids_recv: unknown lkid "
				  "nodeid=%d id=%x remid=%x offset=%d len=%d",
				  nodeid, local_id, remote_id, offset, len);
		}
	}

	if (recover_list_empty(ls))
		wake_up(&ls->ls_wait_general);

	return 0;
}
