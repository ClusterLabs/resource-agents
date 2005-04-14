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
#include "memory.h"
#include "lowcomms.h"
#include "requestqueue.h"
#include "util.h"
#include "dir.h"
#include "member.h"
#include "lockspace.h"
#include "ast.h"
#include "lock.h"
#include "rcom.h"
#include "recover.h"

/* Central locking logic has four stages:

   dlm_lock()
   dlm_unlock()

   request_lock(ls, lkb)
   convert_lock(ls, lkb)
   unlock_lock(ls, lkb)
   cancel_lock(ls, lkb)

   _request_lock(r, lkb)
   _convert_lock(r, lkb)
   _unlock_lock(r, lkb)
   _cancel_lock(r, lkb)

   do_request(r, lkb)
   do_convert(r, lkb)
   do_unlock(r, lkb)
   do_cancel(r, lkb)


   Stage 1 (lock, unlock) is mainly about checking input args and
   splitting into one of the four main operations:

       dlm_lock          = request_lock
       dlm_lock+CONVERT  = convert_lock
       dlm_unlock        = unlock_lock
       dlm_unlock+CANCEL = cancel_lock

   Stage 2, xxxx_lock(), just finds and locks the relevant rsb which is
   provided to the next stage.

   Stage 3, _xxxx_lock(), determines if the operation is local or remote.
   When remote, it calls send_xxxx(), when local it calls do_xxxx().

   Stage 4, do_xxxx(), is the guts of the operation.  It manipulates the
   given rsb and lkb and queues callbacks.


   For remote operations, the send_xxxx() results in the corresponding
   do_xxxx() function being executed on the remote node.  The connecting
   send/receive calls on local (L) and remote (R) nodes:

   L: send_xxxx()              ->  R: receive_xxxx()
                                   R: do_xxxx()
   L: receive_xxxx_reply()     <-  R: send_xxxx_reply()
*/

/*
 * Lock compatibilty matrix - thanks Steve
 * UN = Unlocked state. Not really a state, used as a flag
 * PD = Padding. Used to make the matrix a nice power of two in size
 * Other states are the same as the VMS DLM.
 * Usage: matrix[grmode+1][rqmode+1]  (although m[rq+1][gr+1] is the same)
 */

const int __dlm_compat_matrix[8][8] = {
      /* UN NL CR CW PR PW EX PD */
        {1, 1, 1, 1, 1, 1, 1, 0},       /* UN */
        {1, 1, 1, 1, 1, 1, 1, 0},       /* NL */
        {1, 1, 1, 1, 1, 1, 0, 0},       /* CR */
        {1, 1, 1, 1, 0, 0, 0, 0},       /* CW */
        {1, 1, 1, 0, 1, 0, 0, 0},       /* PR */
        {1, 1, 1, 0, 0, 0, 0, 0},       /* PW */
        {1, 1, 0, 0, 0, 0, 0, 0},       /* EX */
        {0, 0, 0, 0, 0, 0, 0, 0}        /* PD */
};

#define modes_compat(gr, rq) \
	__dlm_compat_matrix[(gr)->lkb_grmode + 1][(rq)->lkb_rqmode + 1]

int dlm_modes_compat(int mode1, int mode2)
{
	return __dlm_compat_matrix[mode1 + 1][mode2 + 1];
}

/*
 * Compatibility matrix for conversions with QUECVT set.
 * Granted mode is the row; requested mode is the column.
 * Usage: matrix[grmode+1][rqmode+1]
 */

const int __quecvt_compat_matrix[8][8] = {
      /* UN NL CR CW PR PW EX PD */
        {0, 0, 0, 0, 0, 0, 0, 0},       /* UN */
        {0, 0, 1, 1, 1, 1, 1, 0},       /* NL */
        {0, 0, 0, 1, 1, 1, 1, 0},       /* CR */
        {0, 0, 0, 0, 1, 1, 1, 0},       /* CW */
        {0, 0, 0, 1, 0, 1, 1, 0},       /* PR */
        {0, 0, 0, 0, 0, 0, 1, 0},       /* PW */
        {0, 0, 0, 0, 0, 0, 0, 0},       /* EX */
        {0, 0, 0, 0, 0, 0, 0, 0}        /* PD */
};

/*
 * This defines the direction of transfer of LVB data.
 * Granted mode is the row; requested mode is the column.
 * Usage: matrix[grmode+1][rqmode+1]
 * 1 = LVB is returned to the caller
 * 0 = LVB is written to the resource
 * -1 = nothing happens to the LVB
 */

const int __lvb_operations[8][8] = {
        /* UN   NL  CR  CW  PR  PW  EX  PD*/
        {  -1,  1,  1,  1,  1,  1,  1, -1 }, /* UN */
        {  -1,  1,  1,  1,  1,  1,  1,  0 }, /* NL */
        {  -1, -1,  1,  1,  1,  1,  1,  0 }, /* CR */
        {  -1, -1, -1,  1,  1,  1,  1,  0 }, /* CW */
        {  -1, -1, -1, -1,  1,  1,  1,  0 }, /* PR */
        {  -1,  0,  0,  0,  0,  0,  1,  0 }, /* PW */
        {  -1,  0,  0,  0,  0,  0,  0,  0 }, /* EX */
        {  -1,  0,  0,  0,  0,  0,  0,  0 }  /* PD */
};

void dlm_print_lkb(struct dlm_lkb *lkb)
{
	printk("lkb: nodeid %d id %x remid %x exflags %x flags %x\n"
	       "     status %d rqmode %d grmode %d wait_type %d ast_type %d\n",
	       lkb->lkb_nodeid, lkb->lkb_id, lkb->lkb_remid, lkb->lkb_exflags,
	       lkb->lkb_flags, lkb->lkb_status, lkb->lkb_rqmode,
	       lkb->lkb_grmode, lkb->lkb_wait_type, lkb->lkb_ast_type);
}

void dlm_print_rsb(struct dlm_rsb *r)
{
	printk("rsb: nodeid %d flags %lx trial %x name %s\n",
	       r->res_nodeid, r->res_flags, r->res_trial_lkid, r->res_name);
}


/* Threads cannot use the lockspace while it's being recovered */

void lock_recovery(struct dlm_ls *ls)
{
	down_read(&ls->ls_in_recovery);
}

void unlock_recovery(struct dlm_ls *ls)
{
	up_read(&ls->ls_in_recovery);
}

int lock_recovery_try(struct dlm_ls *ls)
{
	return down_read_trylock(&ls->ls_in_recovery);
}

int can_be_queued(struct dlm_lkb *lkb)
{
	return (!(lkb->lkb_exflags & DLM_LKF_NOQUEUE));
}

int force_blocking_asts(struct dlm_lkb *lkb)
{
	return (lkb->lkb_exflags & DLM_LKF_NOQUEUEBAST);
}

int is_demoted(struct dlm_lkb *lkb)
{
	return (lkb->lkb_sbflags & DLM_SBF_DEMOTED);
}

int is_remote(struct dlm_rsb *r)
{
	DLM_ASSERT(r->res_nodeid >= 0, dlm_print_rsb(r););
	return r->res_nodeid ? TRUE : FALSE;
}

int is_master(struct dlm_rsb *r)
{
	return r->res_nodeid ? FALSE : TRUE;
}

int is_local_copy(struct dlm_lkb *lkb)
{
	return (!lkb->lkb_nodeid && !(lkb->lkb_flags & DLM_IFL_MSTCPY));
}
 
int is_process_copy(struct dlm_lkb *lkb)
{
	return (lkb->lkb_nodeid && !(lkb->lkb_flags & DLM_IFL_MSTCPY));
}

int is_master_copy(struct dlm_lkb *lkb)
{
	if (lkb->lkb_flags & DLM_IFL_MSTCPY)
		DLM_ASSERT(lkb->lkb_nodeid, dlm_print_lkb(lkb););
	return (lkb->lkb_flags & DLM_IFL_MSTCPY) ? TRUE : FALSE;
}

void queue_cast(struct dlm_rsb *r, struct dlm_lkb *lkb, int rv)
{
	if (is_master_copy(lkb))
		return;

	DLM_ASSERT(lkb->lkb_lksb, dlm_print_lkb(lkb););

	lkb->lkb_lksb->sb_status = rv;
	lkb->lkb_lksb->sb_flags = lkb->lkb_sbflags;

	dlm_add_ast(lkb, AST_COMP);
}

void queue_bast(struct dlm_rsb *r, struct dlm_lkb *lkb, int rqmode)
{
	if (is_master_copy(lkb))
		send_bast(r, lkb, rqmode);
	else {
		lkb->lkb_bastmode = rqmode;
		dlm_add_ast(lkb, AST_BAST);
	}
}

int dir_remove(struct dlm_rsb *r)
{
	int to_nodeid = dlm_dir_nodeid(r);

	if (to_nodeid != dlm_our_nodeid())
		send_remove(r);
	else
		dlm_dir_remove_entry(r->res_ls, to_nodeid,
				     r->res_name, r->res_length);
	return 0;
}


/*
 * Basic operations on rsb's and lkb's
 */

struct dlm_rsb *create_rsb(struct dlm_ls *ls, char *name, int len)
{
	struct dlm_rsb *r;

	r = allocate_rsb(ls, len);
	if (!r)
		return NULL;

	r->res_ls = ls;
	r->res_length = len;
	memcpy(r->res_name, name, len);
	init_MUTEX(&r->res_sem);

	INIT_LIST_HEAD(&r->res_lookup);
	INIT_LIST_HEAD(&r->res_grantqueue);
	INIT_LIST_HEAD(&r->res_convertqueue);
	INIT_LIST_HEAD(&r->res_waitqueue);
	INIT_LIST_HEAD(&r->res_root_list);
	INIT_LIST_HEAD(&r->res_recover_list);

	return r;
}

int search_rsb_list(struct list_head *head, char *name, int len,
		    unsigned int flags, struct dlm_rsb **r_ret)
{
	struct dlm_rsb *r;
	int error = 0;

	list_for_each_entry(r, head, res_hashchain) {
		if (len == r->res_length && !memcmp(name, r->res_name, len))
			goto found;
	}
	return -ENOENT;

 found:
	if (r->res_nodeid && (flags & R_MASTER))
		error = -ENOTBLK;
	*r_ret = r;
	return error;
}

int _search_rsb(struct dlm_ls *ls, char *name, int len, int b,
		unsigned int flags, struct dlm_rsb **r_ret)
{
	struct dlm_rsb *r;
	int error;

	error = search_rsb_list(&ls->ls_rsbtbl[b].list, name, len, flags, &r);
	if (!error) {
		kref_get(&r->res_ref);
		goto out;
	}
	error = search_rsb_list(&ls->ls_rsbtbl[b].toss, name, len, flags, &r);
	if (!error) {
		list_move(&r->res_hashchain, &ls->ls_rsbtbl[b].list);

		if (r->res_nodeid == -1) {
			clear_bit(RESFL_MASTER_WAIT, &r->res_flags);
			clear_bit(RESFL_MASTER_UNCERTAIN, &r->res_flags);
			r->res_trial_lkid = 0;
		} else if (r->res_nodeid > 0) {
			clear_bit(RESFL_MASTER_WAIT, &r->res_flags);
			set_bit(RESFL_MASTER_UNCERTAIN, &r->res_flags);
			r->res_trial_lkid = 0;
		} else {
			DLM_ASSERT(r->res_nodeid == 0,
				   dlm_print_rsb(r););
			DLM_ASSERT(!test_bit(RESFL_MASTER_WAIT, &r->res_flags),
				   dlm_print_rsb(r););
			DLM_ASSERT(!test_bit(RESFL_MASTER_UNCERTAIN,
					     &r->res_flags),);
		}
	}
 out:
	*r_ret = r;
	return error;
}

int search_rsb(struct dlm_ls *ls, char *name, int len, int b,
	       unsigned int flags, struct dlm_rsb **r_ret)
{
	int error;
	write_lock(&ls->ls_rsbtbl[b].lock);
	error = _search_rsb(ls, name, len, b, flags, r_ret);
	write_unlock(&ls->ls_rsbtbl[b].lock);
	return error;
}

/*
 * Find rsb in rsbtbl and potentially create/add one
 *
 * Delaying the release of rsb's has a similar benefit to applications keeping
 * NL locks on an rsb without the guarantee that the cached master value will
 * still be valid when the rsb is reused.  Apps aren't always smart enough to
 * keep NL locks on an rsb that they may lock again shortly; this can lead to
 * excessive master lookups and removals if we don't delay the release.
 *
 * Searching for an rsb means looking through both the normal list and toss
 * list.  When found on the toss list the rsb is moved to the normal list with
 * ref count of 1; when found on normal list the ref count is incremented.
 */

int find_rsb(struct dlm_ls *ls, char *name, int namelen, unsigned int flags,
	     struct dlm_rsb **r_ret)
{
	struct dlm_rsb *r, *tmp;
	uint32_t bucket;
	int error = 0;

	bucket = dlm_hash(name, namelen);
	bucket &= (ls->ls_rsbtbl_size - 1);

	error = search_rsb(ls, name, namelen, bucket, flags, &r);
	if (!error)
		goto out;

	if (error == -ENOENT && !(flags & R_CREATE))
		goto out;

	/* the rsb was found but wasn't a master copy */
	if (error == -ENOTBLK)
		goto out;

	error = -ENOMEM;
	r = create_rsb(ls, name, namelen);
	if (!r)
		goto out;

	r->res_bucket = bucket;
	r->res_nodeid = -1;
	kref_init(&r->res_ref);

	write_lock(&ls->ls_rsbtbl[bucket].lock);
	error = _search_rsb(ls, name, namelen, bucket, 0, &tmp);
	if (!error) {
		write_unlock(&ls->ls_rsbtbl[bucket].lock);
		free_rsb(r);
		r = tmp;
		goto out;
	}
	list_add(&r->res_hashchain, &ls->ls_rsbtbl[bucket].list);
	write_unlock(&ls->ls_rsbtbl[bucket].lock);
	error = 0;
 out:
	*r_ret = r;
	return error;
}

int dlm_find_rsb(struct dlm_ls *ls, char *name, int namelen,
		 unsigned int flags, struct dlm_rsb **r_ret)
{
	return find_rsb(ls, name, namelen, flags, r_ret);
}

/* This is only called to add a reference when the code already holds
   a valid reference to the rsb, so there's no need for locking. */
   
void hold_rsb(struct dlm_rsb *r)
{
	kref_get(&r->res_ref);
}

void dlm_hold_rsb(struct dlm_rsb *r)
{
	hold_rsb(r);
}

void toss_rsb(struct kref *kref)
{
	struct dlm_rsb *r = container_of(kref, struct dlm_rsb, res_ref);
	struct dlm_ls *ls = r->res_ls;

	DLM_ASSERT(list_empty(&r->res_root_list), dlm_print_rsb(r););
	kref_init(&r->res_ref);
	list_move(&r->res_hashchain, &ls->ls_rsbtbl[r->res_bucket].toss);
	r->res_toss_time = jiffies;
}

/* When all references to the rsb are gone it's transfered to
   the tossed list for later disposal. */

void put_rsb(struct dlm_rsb *r)
{
	struct dlm_ls *ls = r->res_ls;
	uint32_t bucket = r->res_bucket;

	write_lock(&ls->ls_rsbtbl[bucket].lock);
	kref_put(&r->res_ref, toss_rsb);
	write_unlock(&ls->ls_rsbtbl[bucket].lock);
}

void dlm_put_rsb(struct dlm_rsb *r)
{
	put_rsb(r);
}

/* See comment for unhold_lkb */

void unhold_rsb(struct dlm_rsb *r)
{
	int rv;
	rv = kref_put(&r->res_ref, toss_rsb);
	DLM_ASSERT(!rv, dlm_print_rsb(r););
}

void kill_rsb(struct kref *kref)
{
	struct dlm_rsb *r = container_of(kref, struct dlm_rsb, res_ref);

	/* All work is done after the return from kref_put() so we
	   can release the write_lock before the remove and free. */

	DLM_ASSERT(list_empty(&r->res_lookup),);
	DLM_ASSERT(list_empty(&r->res_grantqueue),);
	DLM_ASSERT(list_empty(&r->res_convertqueue),);
	DLM_ASSERT(list_empty(&r->res_waitqueue),);
	DLM_ASSERT(list_empty(&r->res_root_list),);
	DLM_ASSERT(list_empty(&r->res_recover_list),);
}

/* FIXME: shouldn't this be able to exit as soon as one non-due rsb is
   found since they are in order of newest to oldest? */

int shrink_bucket(struct dlm_ls *ls, int b)
{
	struct dlm_rsb *r;
	int count = 0, found;

	for (;;) {
		found = FALSE;
		write_lock(&ls->ls_rsbtbl[b].lock);
		list_for_each_entry_reverse(r, &ls->ls_rsbtbl[b].toss,
					    res_hashchain) {
			if (!time_after_eq(jiffies, r->res_toss_time +
					            DLM_TOSS_SECS * HZ))
				continue;
			found = TRUE;
			break;
		}

		if (!found) {
			write_unlock(&ls->ls_rsbtbl[b].lock);
			break;
		}

		if (kref_put(&r->res_ref, kill_rsb)) {
			list_del(&r->res_hashchain);
			write_unlock(&ls->ls_rsbtbl[b].lock);

			if (is_master(r))
				dir_remove(r);
			free_rsb(r);
			count++;
		} else {
			write_unlock(&ls->ls_rsbtbl[b].lock);
			log_error(ls, "tossed rsb in use %s", r->res_name);
		}
	}

	return count;
}

void dlm_scan_rsbs(struct dlm_ls *ls)
{
	int i, count = 0;

	for (i = 0; i < ls->ls_rsbtbl_size; i++) {
		count += shrink_bucket(ls, i);
		cond_resched();
	}
}

/* exclusive access to rsb and all its locks */

void lock_rsb(struct dlm_rsb *r)
{
	down(&r->res_sem);
}

void unlock_rsb(struct dlm_rsb *r)
{
	up(&r->res_sem);
}

void dlm_lock_rsb(struct dlm_rsb *r)
{
	lock_rsb(r);
}

void dlm_unlock_rsb(struct dlm_rsb *r)
{
	unlock_rsb(r);
}

/* Attaching/detaching lkb's from rsb's is for rsb reference counting.
   The rsb must exist as long as any lkb's for it do. */

void attach_lkb(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	hold_rsb(r);
	lkb->lkb_resource = r;
}

void detach_lkb(struct dlm_lkb *lkb)
{
	if (lkb->lkb_resource) {
		put_rsb(lkb->lkb_resource);
		lkb->lkb_resource = NULL;
	}
}

int create_lkb(struct dlm_ls *ls, struct dlm_lkb **lkb_ret)
{
	struct dlm_lkb *lkb;
	uint32_t lkid;
	uint16_t bucket;

	lkb = allocate_lkb(ls);
	if (!lkb)
		return -ENOMEM;

	lkb->lkb_nodeid = -1;
	lkb->lkb_grmode = DLM_LOCK_IV;
	kref_init(&lkb->lkb_ref);

	get_random_bytes(&bucket, sizeof(bucket));
	bucket &= (ls->ls_lkbtbl_size - 1);

	write_lock(&ls->ls_lkbtbl[bucket].lock);
	lkid = bucket | (ls->ls_lkbtbl[bucket].counter++ << 16);
	/* FIXME: do a find to verify lkid not in use */

	DLM_ASSERT(lkid, );

	lkb->lkb_id = lkid;
	list_add(&lkb->lkb_idtbl_list, &ls->ls_lkbtbl[bucket].list);
	write_unlock(&ls->ls_lkbtbl[bucket].lock);

	*lkb_ret = lkb;
	return 0;
}

struct dlm_lkb *__find_lkb(struct dlm_ls *ls, uint32_t lkid)
{
	uint16_t bucket = lkid & 0xFFFF;
	struct dlm_lkb *lkb;

	list_for_each_entry(lkb, &ls->ls_lkbtbl[bucket].list, lkb_idtbl_list) {
		if (lkb->lkb_id == lkid)
			return lkb;
	}
	return NULL;
}

int find_lkb(struct dlm_ls *ls, uint32_t lkid, struct dlm_lkb **lkb_ret)
{
	struct dlm_lkb *lkb;
	uint16_t bucket = lkid & 0xFFFF;

	if (bucket >= ls->ls_lkbtbl_size)
		return -EBADSLT;

	read_lock(&ls->ls_lkbtbl[bucket].lock);
	lkb = __find_lkb(ls, lkid);
	if (lkb)
		kref_get(&lkb->lkb_ref);
	read_unlock(&ls->ls_lkbtbl[bucket].lock);

	*lkb_ret = lkb;
	return lkb ? 0 : -ENOENT;
}

int dlm_find_lkb(struct dlm_ls *ls, uint32_t lkid, struct dlm_lkb **lkb_ret)
{
	return find_lkb(ls, lkid, lkb_ret);
}

void kill_lkb(struct kref *kref)
{
	struct dlm_lkb *lkb = container_of(kref, struct dlm_lkb, lkb_ref);

	/* All work is done after the return from kref_put() so we
	   can release the write_lock before the detach_lkb */

	DLM_ASSERT(!lkb->lkb_status, dlm_print_lkb(lkb););
}

int put_lkb(struct dlm_lkb *lkb)
{
	struct dlm_ls *ls = lkb->lkb_resource->res_ls;
	uint16_t bucket = lkb->lkb_id & 0xFFFF;

	write_lock(&ls->ls_lkbtbl[bucket].lock);
	if (kref_put(&lkb->lkb_ref, kill_lkb)) {
		list_del(&lkb->lkb_idtbl_list);
		write_unlock(&ls->ls_lkbtbl[bucket].lock);

		detach_lkb(lkb);

		/* for local/process lkbs, lvbptr points to caller's lksb */
		if (lkb->lkb_lvbptr && is_master_copy(lkb))
			free_lvb(lkb->lkb_lvbptr);
		if (lkb->lkb_range)
			free_range(lkb->lkb_range);
		free_lkb(lkb);
		return 1;
	} else {
		write_unlock(&ls->ls_lkbtbl[bucket].lock);
		return 0;
	}
}

int dlm_put_lkb(struct dlm_lkb *lkb)
{
	return put_lkb(lkb);
}

/* This is only called to add a reference when the code already holds
   a valid reference to the lkb, so there's no need for locking. */

void hold_lkb(struct dlm_lkb *lkb)
{
	kref_get(&lkb->lkb_ref);
}

/* This is called when we need to remove a reference and are certain
   it's not the last ref.  e.g. del_lkb is always called between a
   find_lkb/put_lkb and is always the inverse of a previous add_lkb.
   put_lkb would work fine, but would involve unnecessary locking */

void unhold_lkb(struct dlm_lkb *lkb)
{
	int rv;
	rv = kref_put(&lkb->lkb_ref, kill_lkb);
	DLM_ASSERT(!rv, dlm_print_lkb(lkb););
}

void lkb_add_ordered(struct list_head *new, struct list_head *head, int mode)
{
	struct dlm_lkb *lkb = NULL;

	list_for_each_entry(lkb, head, lkb_statequeue)
		if (lkb->lkb_rqmode < mode)
			break;

	if (!lkb)
		list_add_tail(new, head);
	else
		__list_add(new, lkb->lkb_statequeue.prev, &lkb->lkb_statequeue);
}

/* add/remove lkb to rsb's grant/convert/wait queue */

void add_lkb(struct dlm_rsb *r, struct dlm_lkb *lkb, int sts)
{
	kref_get(&lkb->lkb_ref);

	DLM_ASSERT(!lkb->lkb_status, dlm_print_lkb(lkb););

	lkb->lkb_status = sts;

	switch(sts) {
	case DLM_LKSTS_WAITING:
		if (lkb->lkb_exflags & DLM_LKF_HEADQUE)
			list_add(&lkb->lkb_statequeue, &r->res_waitqueue);
		else
			list_add_tail(&lkb->lkb_statequeue, &r->res_waitqueue);
		break;
	case DLM_LKSTS_GRANTED:
		/* convention says granted locks kept in order of grmode */
		lkb_add_ordered(&lkb->lkb_statequeue, &r->res_grantqueue,
				lkb->lkb_grmode);
		break;
	case DLM_LKSTS_CONVERT:
		if (lkb->lkb_exflags & DLM_LKF_HEADQUE)
			list_add(&lkb->lkb_statequeue, &r->res_convertqueue);
		else
			list_add_tail(&lkb->lkb_statequeue,
				      &r->res_convertqueue);
		break;
	default:
		DLM_ASSERT(0,);
	}
}

void del_lkb(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	lkb->lkb_status = 0;
	list_del(&lkb->lkb_statequeue);
	unhold_lkb(lkb);
}

void move_lkb(struct dlm_rsb *r, struct dlm_lkb *lkb, int sts)
{
	hold_lkb(lkb);
	del_lkb(r, lkb);
	add_lkb(r, lkb, sts);
	unhold_lkb(lkb);
}

/* add/remove lkb from global waiters list of lkb's waiting for
   a reply from a remote node */

void add_to_waiters(struct dlm_lkb *lkb, int mstype)
{
	struct dlm_ls *ls = lkb->lkb_resource->res_ls;

	down(&ls->ls_waiters_sem);
	if (lkb->lkb_wait_type) {
		printk("add_to_waiters error %d", lkb->lkb_wait_type);
		goto out;
	}
	lkb->lkb_wait_type = mstype;
	kref_get(&lkb->lkb_ref);
	list_add(&lkb->lkb_wait_reply, &ls->ls_waiters);
 out:
	up(&ls->ls_waiters_sem);
}

int _remove_from_waiters(struct dlm_lkb *lkb)
{
	int error = 0;

	if (!lkb->lkb_wait_type) {
		printk("remove_from_waiters error");
		error = -EINVAL;
		goto out;
	}
	lkb->lkb_wait_type = 0;
	list_del(&lkb->lkb_wait_reply);
	unhold_lkb(lkb);
 out:
	return error;
}

int remove_from_waiters(struct dlm_lkb *lkb)
{
	struct dlm_ls *ls = lkb->lkb_resource->res_ls;
	int error;

	down(&ls->ls_waiters_sem);
	error = _remove_from_waiters(lkb);
	up(&ls->ls_waiters_sem);
	return error;
}

int dlm_remove_from_waiters(struct dlm_lkb *lkb)
{
	return remove_from_waiters(lkb);
}

/*
 * check and set input args
 * FIXME: is it safe to check lkb fields without a lock_rsb() ?
 */

int set_lock_args(struct dlm_ls *ls, struct dlm_lkb *lkb, int mode,
		  struct dlm_lksb *lksb, uint32_t flags, int namelen,
		  uint32_t parent_lkid, void *ast, void *astarg, void *bast,
		  struct dlm_range *range)
{
	int rv = -EINVAL;

	/* check for invalid arg usage */

	if (mode < 0 || mode > DLM_LOCK_EX)
		goto out;

	if (!(flags & DLM_LKF_CONVERT) && (namelen > DLM_RESNAME_MAXLEN))
		goto out;

	if (flags & DLM_LKF_CANCEL)
		goto out;

	if (flags & DLM_LKF_QUECVT && !(flags & DLM_LKF_CONVERT))
		goto out;

	if (flags & DLM_LKF_CONVDEADLK && !(flags & DLM_LKF_CONVERT))
		goto out;

	if (flags & DLM_LKF_CONVDEADLK && flags & DLM_LKF_NOQUEUE)
		goto out;

	if (flags & DLM_LKF_EXPEDITE && flags & DLM_LKF_CONVERT)
		goto out;

	if (flags & DLM_LKF_EXPEDITE && flags & DLM_LKF_QUECVT)
		goto out;

	if (flags & DLM_LKF_EXPEDITE && flags & DLM_LKF_NOQUEUE)
		goto out;

	if (flags & DLM_LKF_EXPEDITE && mode != DLM_LOCK_NL)
		goto out;

	if (!ast || !lksb)
		goto out;

	if (flags & DLM_LKF_VALBLK && !lksb->sb_lvbptr)
		goto out;

	/* FIXME: parent/child locks not yet supported */
	if (parent_lkid)
		goto out;

	/* checks specific to conversions */

	if (flags & DLM_LKF_CONVERT) {

		if (!lksb->sb_lkid)
			goto out;

		if (lkb->lkb_flags & DLM_IFL_MSTCPY)
			goto out;

		if (flags & DLM_LKF_QUECVT &&
		    !__quecvt_compat_matrix[lkb->lkb_grmode + 1][mode + 1])
			goto out;

		rv = -EBUSY;
		if (lkb->lkb_status != DLM_LKSTS_GRANTED)
			goto out;
	}

	/* copy input values into lkb */

	lkb->lkb_exflags = flags;
	lkb->lkb_sbflags = 0;
	lkb->lkb_astaddr = ast;
	lkb->lkb_astparam = (long) astarg;
	lkb->lkb_bastaddr = bast;
	lkb->lkb_rqmode = mode;
	lkb->lkb_lksb = lksb;
	lkb->lkb_lvbptr = lksb->sb_lvbptr;
	lkb->lkb_ownpid = (int) current->pid;

	lkb->lkb_flags &= ~DLM_IFL_RETURNLVB;

	if (range) {
		if (!lkb->lkb_range) {
			rv = -ENOMEM;
			lkb->lkb_range = allocate_range(ls);
			if (!lkb->lkb_range)
				goto out;

			/* This is needed for conversions that contain ranges
			   where the original lock didn't but it's harmless for
			   new locks too. */

			lkb->lkb_range[GR_RANGE_START] = 0LL;
			lkb->lkb_range[GR_RANGE_END] = 0xffffffffffffffffULL;
		}

		lkb->lkb_range[RQ_RANGE_START] = range->ra_start;
		lkb->lkb_range[RQ_RANGE_END] = range->ra_end;
		lkb->lkb_flags |= DLM_IFL_RANGE;
	}

	rv = 0;
 out:
	return rv;
}

int set_unlock_args(struct dlm_ls *ls, struct dlm_lkb *lkb, uint32_t flags,
		    struct dlm_lksb *lksb, void *astarg)	
{
	int rv = -EINVAL;

	if (lkb->lkb_flags & DLM_IFL_MSTCPY) {
		log_error(ls, "can't unlock MSTCPY %x", lkb->lkb_id);
		goto out;
	}

	if (flags & DLM_LKF_CANCEL && lkb->lkb_status == DLM_LKSTS_GRANTED) {
		log_error(ls, "can't cancel granted %x %d", lkb->lkb_id,
			  lkb->lkb_status);
		goto out;
	}

	if (!(flags & DLM_LKF_CANCEL) && lkb->lkb_status != DLM_LKSTS_GRANTED) {
		log_error(ls, "can't unlock ungranted %x %d", lkb->lkb_id,
			  lkb->lkb_status);
		goto out;
	}

	rv = -EBUSY;
	if (lkb->lkb_wait_type)
		goto out;

	lkb->lkb_exflags = flags;
	lkb->lkb_sbflags = 0;
	lkb->lkb_astparam = (long)astarg;

	rv = 0;
 out:
	return rv;
}


/*
 * Two stage 1 varieties:  dlm_lock() and dlm_unlock()
 */

int dlm_lock(dlm_lockspace_t *lockspace,
	     int mode,
	     struct dlm_lksb *lksb,
	     uint32_t flags,
	     void *name,
	     unsigned int namelen,
	     uint32_t parent_lkid,
	     void (*ast) (void *astarg),
	     void *astarg,
	     void (*bast) (void *astarg, int mode),
	     struct dlm_range *range)
{
	struct dlm_ls *ls;
	struct dlm_lkb *lkb;
	int error, convert = flags & DLM_LKF_CONVERT;

	ls = find_lockspace_local(lockspace);
	if (!ls)
		return -EINVAL;

	lock_recovery(ls);

	if (convert)
		error = find_lkb(ls, lksb->sb_lkid, &lkb);
	else
		error = create_lkb(ls, &lkb);

	if (error)
		goto out;

	error = set_lock_args(ls, lkb, mode, lksb, flags, namelen, parent_lkid,
			      ast, astarg, bast, range);
	if (error)
		goto out_put;

	if (convert)
		error = convert_lock(ls, lkb);
	else
		error = request_lock(ls, lkb, name, namelen);

	if (error == -EINPROGRESS)
		error = 0;
 out_put:
	if (convert || error)
		put_lkb(lkb);
	if (error == -EAGAIN)
		error = 0;
 out:
	unlock_recovery(ls);
	put_lockspace(ls);
	return error;
}

int dlm_unlock(dlm_lockspace_t *lockspace,
	       uint32_t lkid,
	       uint32_t flags,
	       struct dlm_lksb *lksb,
	       void *astarg)
{
	struct dlm_ls *ls;
	struct dlm_lkb *lkb;
	int error;

	ls = find_lockspace_local(lockspace);
	if (!ls)
		return -EINVAL;

	lock_recovery(ls);

	error = find_lkb(ls, lkid, &lkb);
	if (error)
		goto out;

	error = set_unlock_args(ls, lkb, flags, lksb, astarg);
	if (error)
		goto out_put;

	if (flags & DLM_LKF_CANCEL)
		error = cancel_lock(ls, lkb);
	else
		error = unlock_lock(ls, lkb);

	if (error == -DLM_EUNLOCK || error == -DLM_ECANCEL)
		error = 0;
 out_put:
	put_lkb(lkb);
 out:
	unlock_recovery(ls);
	put_lockspace(ls);
	return error;
}


/* set_master(r, lkb) -- set the master nodeid of a resource

   The purpose of this function is to set the nodeid field in the given
   lkb using the nodeid field in the given rsb.  If the rsb's nodeid is
   known, it can just be copied to the lkb and the function will return
   0.  If the rsb's nodeid is _not_ known, it needs to be looked up
   before it can be copied to the lkb.
   
   When the rsb nodeid is being looked up remotely, the initial lkb
   causing the lookup is kept on the ls_waiters list waiting for the
   lookup reply.  Other lkb's waiting for the same rsb lookup are kept
   on the rsb's res_lookup list until the master is verified.

   After a remote lookup or when a tossed rsb is retrived that specifies
   a remote master, that master value is uncertain -- it may have changed
   by the time we send it a request.  While it's uncertain, only one lkb
   is allowed to go ahead and use the master value; that lkb is specified
   by res_trial_lkid.  Once the trial lkb is queued on the master node
   we know the rsb master is correct and any other lkbs on res_lookup
   can get the rsb nodeid and go ahead with their request.

   a. res_nodeid == 0   we are master
   b. res_nodeid > 0    remote node is master
   c. res_nodeid == -1  no idea who master is
   d. lkb_nodeid == 0   we are master, res_nodeid is certain
   e. lkb_nodeid != 0
   f. res_flags MASTER_UNCERTAIN  (b. should be true in this case)
   g. res_flags MASTER_WAIT
   h. res_trial_lkid != 0

   Cases:
   1. we've no idea who master is [c]
   2. we know who master /was/ in the past (r was tossed and remote) [b,f]
   3. we're certain of who master is because we hold granted locks
      or are the master ourself [b or a]
   4. we're in the process of looking up master from dir [c,g]
   5. we've been told who the master is (from dir) but it could
      change by the time we send it a lock request (only if we
      were told the master is remote) [b,g]
   6. 5 and we have a request outstanding to this uncertain master [b,g,h]

   Return values:
   0: nodeid is set in rsb/lkb and the caller should go ahead and use it
   1: the rsb master is not available and the lkb has been placed on
      a wait queue
   -EXXX: there was some error in processing
*/
 
int set_master(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	struct dlm_ls *ls = r->res_ls;
	int error, dir_nodeid, ret_nodeid, our_nodeid = dlm_our_nodeid();

	if (test_and_clear_bit(RESFL_MASTER_UNCERTAIN, &r->res_flags)) {
		set_bit(RESFL_MASTER_WAIT, &r->res_flags);
		r->res_trial_lkid = lkb->lkb_id;
		lkb->lkb_nodeid = r->res_nodeid;
		return 0;
	}

	if (r->res_nodeid == 0) {
		lkb->lkb_nodeid = 0;
		return 0;
	}

	if (r->res_trial_lkid == lkb->lkb_id) {
		DLM_ASSERT(lkb->lkb_id, dlm_print_lkb(lkb););
		lkb->lkb_nodeid = r->res_nodeid;
		return 0;
	}

	if (test_bit(RESFL_MASTER_WAIT, &r->res_flags)) {
		list_add_tail(&lkb->lkb_rsb_lookup, &r->res_lookup);
		return 1;
	}

	if (r->res_nodeid > 0) {
		lkb->lkb_nodeid = r->res_nodeid;
		return 0;
	}

	/* This is the first lkb requested on this rsb since the rsb
	   was created.  We need to figure out who the rsb master is. */

	DLM_ASSERT(r->res_nodeid == -1, );

	dir_nodeid = dlm_dir_nodeid(r);

	if (dir_nodeid != our_nodeid) {
		set_bit(RESFL_MASTER_WAIT, &r->res_flags);
		send_lookup(r, lkb);
		return 1;
	}

	for (;;) {
		/* It's possible for dlm_scand to remove an old rsb for
		   this same resource from the toss list, us to create
		   a new one, look up the master locally, and find it
		   already exists just before dlm_scand does the
		   dir_remove() on the previous rsb. */

		error = dlm_dir_lookup(ls, our_nodeid, r->res_name,
				       r->res_length, &ret_nodeid);
		if (!error)
			break;
		log_debug(ls, "dir_lookup error %d %s", error, r->res_name);
		schedule();
	}

	if (ret_nodeid == our_nodeid) {
		r->res_nodeid = 0;
		lkb->lkb_nodeid = 0;
		return 0;
	}

	set_bit(RESFL_MASTER_WAIT, &r->res_flags);
	r->res_trial_lkid = lkb->lkb_id;
	r->res_nodeid = ret_nodeid;
	lkb->lkb_nodeid = ret_nodeid;
	return 0;
}

/* confirm_master -- confirm (or deny) an rsb's master nodeid

   This is called when we get a request reply from a remote node
   who we believe is the master.  The return value (error) we got
   back indicates whether it's really the master or not.  If it
   wasn't we need to start over and do another master lookup.  If
   it was and our lock was queued we know the master won't change.
   If it was and our lock wasn't queued, we need to do another
   trial with the next lkb.
*/

void confirm_master(struct dlm_rsb *r, int error)
{
	struct dlm_lkb *lkb, *safe;

	if (!test_bit(RESFL_MASTER_WAIT, &r->res_flags))
		return;

	switch (error) {
	case 0:
	case -EINPROGRESS:
		/* the remote master queued our request, or
		   the remote dir node told us we're the master */

		clear_bit(RESFL_MASTER_WAIT, &r->res_flags);
		r->res_trial_lkid = 0;

		list_for_each_entry_safe(lkb, safe, &r->res_lookup,
					 lkb_rsb_lookup) {
			list_del(&lkb->lkb_rsb_lookup);
			_request_lock(r, lkb);
			schedule();
		}
		break;
	
	case -EAGAIN:
		/* the remote master didn't queue our NOQUEUE request;
		   do another trial with the next waiting lkb */

		if (!list_empty(&r->res_lookup)) {
			lkb = list_entry(r->res_lookup.next, struct dlm_lkb,
					 lkb_rsb_lookup);
			list_del(&lkb->lkb_rsb_lookup);
			r->res_trial_lkid = lkb->lkb_id;
			_request_lock(r, lkb);
			break;
		}

		/* fall through so the rsb looks new */

	case -ENOENT:
	case -ENOTBLK:
		/* the remote master wasn't really the master, i.e.  our
		   trial failed; so we start over with another lookup */

		r->res_nodeid = -1;
		r->res_trial_lkid = 0;
		clear_bit(RESFL_MASTER_WAIT, &r->res_flags);
		break;

	default:
		log_error(r->res_ls, "confirm_master unknown error %d", error);
	}
}

/*
 * Four stage 2 varieties:
 * request_lock(), convert_lock(), unlock_lock(), cancel_lock()
 * These are only called on the node making the request.
 */

int request_lock(struct dlm_ls *ls, struct dlm_lkb *lkb, char *name, int len)
{
	struct dlm_rsb *r;
	int error;

	error = find_rsb(ls, name, len, R_CREATE, &r);
	if (error)
		return error;

	lock_rsb(r);

	attach_lkb(r, lkb);
	error = _request_lock(r, lkb);

	unlock_rsb(r);
	put_rsb(r);

	lkb->lkb_lksb->sb_lkid = lkb->lkb_id;
	return error;
}

int convert_lock(struct dlm_ls *ls, struct dlm_lkb *lkb)
{
	struct dlm_rsb *r;
	int error;

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	error = _convert_lock(r, lkb);

	unlock_rsb(r);
	put_rsb(r);
	return error;
}

int unlock_lock(struct dlm_ls *ls, struct dlm_lkb *lkb)
{
	struct dlm_rsb *r;
	int error;

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	error = _unlock_lock(r, lkb);

	unlock_rsb(r);
	put_rsb(r);
	return error;
}

int cancel_lock(struct dlm_ls *ls, struct dlm_lkb *lkb)
{
	struct dlm_rsb *r;
	int error;

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	error = _cancel_lock(r, lkb);

	unlock_rsb(r);
	put_rsb(r);
	return error;
}

/*
 * Four stage 3 varieties:
 * _request_lock(), _convert_lock(), _unlock_lock(), _cancel_lock()
 * These are called on the node making the request.
 */

/* add a new lkb to a possibly new rsb, called by requesting process */

int _request_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error;

	/* set_master: sets lkb nodeid from r */
	   
	error = set_master(r, lkb);
	if (error < 0) 
		goto out;
	if (error) {
		error = 0;
		goto out;
	}

	if (is_remote(r))
		/* receive_request() calls do_request() on remote node */
		error = send_request(r, lkb);
	else
		error = do_request(r, lkb);
 out:
	return error;
}

/* change some property of an existing lkb, e.g. mode, range */

int _convert_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error;

	if (is_remote(r))
		/* receive_convert() calls do_convert() on remote node */
		error = send_convert(r, lkb);
	else
		error = do_convert(r, lkb);

	return error;
}

/* remove an existing lkb from the granted queue */

int _unlock_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error;

	if (is_remote(r))
		/* receive_unlock() calls call do_unlock() on remote node */
		error = send_unlock(r, lkb);
	else
		error = do_unlock(r, lkb);

	return error;
}

/* remove an existing lkb from the convert or wait queue */

int _cancel_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error;

	if (is_remote(r))
		/* receive_cancel() calls do_cancel() on remote node */
		error = send_cancel(r, lkb);
	else
		error = do_cancel(r, lkb);

	return error;
}

/* lkb is master or local copy */

void set_lvb_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int b;

	/* b=1 lvb returned to caller
	   b=0 lvb written to rsb or invalidated
	   b=-1 do nothing */

	b =  __lvb_operations[lkb->lkb_grmode + 1][lkb->lkb_rqmode + 1];

	if (b == 1) {
		if (!lkb->lkb_lvbptr)
			return;

		if (!(lkb->lkb_exflags & DLM_LKF_VALBLK))
			return;

		if (!r->res_lvbptr)
			return;

		memcpy(lkb->lkb_lvbptr, r->res_lvbptr, DLM_LVB_LEN);
		lkb->lkb_lvbseq = r->res_lvbseq;
		lkb->lkb_flags |= DLM_IFL_RETURNLVB;

	} else if (b == 0) {
		if (lkb->lkb_exflags & DLM_LKF_IVVALBLK) {
			set_bit(RESFL_VALNOTVALID, &r->res_flags);
			return;
		}

		if (!lkb->lkb_lvbptr)
			return;

		if (!(lkb->lkb_exflags & DLM_LKF_VALBLK))
			return;

		if (!r->res_lvbptr)
			r->res_lvbptr = allocate_lvb(r->res_ls);

		if (!r->res_lvbptr)
			return;

		memcpy(r->res_lvbptr, lkb->lkb_lvbptr, DLM_LVB_LEN);
		r->res_lvbseq++;
		lkb->lkb_lvbseq = r->res_lvbseq;
		clear_bit(RESFL_VALNOTVALID, &r->res_flags);
	}

	if (test_bit(RESFL_VALNOTVALID, &r->res_flags))
		lkb->lkb_sbflags |= DLM_SBF_VALNOTVALID;
}

void set_lvb_unlock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	if (lkb->lkb_grmode < DLM_LOCK_PW)
		return;

	if (lkb->lkb_exflags & DLM_LKF_IVVALBLK) {
		set_bit(RESFL_VALNOTVALID, &r->res_flags);
		return;
	}

	if (!lkb->lkb_lvbptr)
		return;

	if (!(lkb->lkb_exflags & DLM_LKF_VALBLK))
		return;

	if (!r->res_lvbptr)
		r->res_lvbptr = allocate_lvb(r->res_ls);

	memcpy(r->res_lvbptr, lkb->lkb_lvbptr, DLM_LVB_LEN);
	r->res_lvbseq++;
	clear_bit(RESFL_VALNOTVALID, &r->res_flags);
}

/* lkb is process copy (pc) */

void set_lvb_lock_pc(struct dlm_rsb *r, struct dlm_lkb *lkb,
		     struct dlm_message *ms)
{
	if (!lkb->lkb_lvbptr)
		return;

	if (!(lkb->lkb_exflags & DLM_LKF_VALBLK))
		return;

	if (!(lkb->lkb_flags & DLM_IFL_RETURNLVB))
		return;

	memcpy(lkb->lkb_lvbptr, ms->m_lvb, DLM_LVB_LEN);
	lkb->lkb_lvbseq = ms->m_lvbseq;
}

/* Manipulate lkb's on rsb's convert/granted/waiting queues
   remove_lock -- used for unlock, removes lkb from granted
   revert_lock -- used for cancel, moves lkb from convert to granted
   grant_lock  -- used for request and convert, adds lkb to granted or
                  moves lkb from convert or waiting to granted
 
   Each of these is used for master or local copy lkb's.  There is
   also a _pc() variation used to make the corresponding change on
   a process copy (pc) lkb. */

void _remove_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	del_lkb(r, lkb);
	lkb->lkb_grmode = DLM_LOCK_IV;
}

void remove_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	set_lvb_unlock(r, lkb);
	_remove_lock(r, lkb);
}

void remove_lock_pc(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	_remove_lock(r, lkb);
}

void revert_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	lkb->lkb_rqmode = DLM_LOCK_IV;
	move_lkb(r, lkb, DLM_LKSTS_GRANTED);
}

void revert_lock_pc(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	revert_lock(r, lkb);
}

void _grant_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	if (lkb->lkb_grmode != lkb->lkb_rqmode) {
		lkb->lkb_grmode = lkb->lkb_rqmode;
		if (lkb->lkb_status)
			move_lkb(r, lkb, DLM_LKSTS_GRANTED);
		else
			add_lkb(r, lkb, DLM_LKSTS_GRANTED);
	}

	lkb->lkb_rqmode = DLM_LOCK_IV;

	if (lkb->lkb_range) {
		lkb->lkb_range[GR_RANGE_START] = lkb->lkb_range[RQ_RANGE_START];
		lkb->lkb_range[GR_RANGE_END] = lkb->lkb_range[RQ_RANGE_END];
	}
}

void grant_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	set_lvb_lock(r, lkb);
	_grant_lock(r, lkb);
	lkb->lkb_highbast = 0;
}

void grant_lock_pc(struct dlm_rsb *r, struct dlm_lkb *lkb,
		   struct dlm_message *ms)
{
	set_lvb_lock_pc(r, lkb, ms);
	_grant_lock(r, lkb);
}

/* called by grant_pending_locks() which means an async grant message must
   be sent to the requesting node in addition to granting the lock if the
   lkb belongs to a remote node. */

void grant_lock_pending(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	grant_lock(r, lkb);
	if (is_master_copy(lkb))
		send_grant(r, lkb);
	else
		queue_cast(r, lkb, 0);
}

static inline int first_in_list(struct dlm_lkb *lkb, struct list_head *head)
{
	struct dlm_lkb *first = list_entry(head->next, struct dlm_lkb,
					   lkb_statequeue);
	if (lkb->lkb_id == first->lkb_id)
		return TRUE;

	return FALSE;
}

/*
 * Return 1 if the locks' ranges overlap
 * If the lkb has no range then it is assumed to cover 0-ffffffff.ffffffff
 */

static inline int ranges_overlap(struct dlm_lkb *lkb1, struct dlm_lkb *lkb2)
{
	if (!lkb1->lkb_range || !lkb2->lkb_range)
		return TRUE;

	if (lkb1->lkb_range[RQ_RANGE_END] < lkb2->lkb_range[GR_RANGE_START] ||
	    lkb1->lkb_range[RQ_RANGE_START] > lkb2->lkb_range[GR_RANGE_END])
		return FALSE;

	return TRUE;
}

/*
 * Check if the given lkb conflicts with another lkb on the queue.
 */

static int queue_conflict(struct list_head *head, struct dlm_lkb *lkb)
{
	struct dlm_lkb *this;

	list_for_each_entry(this, head, lkb_statequeue) {
		if (this == lkb)
			continue;
		if (ranges_overlap(lkb, this) && !modes_compat(this, lkb))
			return TRUE;
	}
	return FALSE;
}

/*
 * "A conversion deadlock arises with a pair of lock requests in the converting
 * queue for one resource.  The granted mode of each lock blocks the requested
 * mode of the other lock."
 *
 * Part 2: if the granted mode of lkb is preventing the first lkb in the
 * convert queue from being granted, then demote lkb (set grmode to NL).
 * This second form requires that we check for conv-deadlk even when
 * now == 0 in _can_be_granted().
 *
 * Example:
 * Granted Queue: empty
 * Convert Queue: NL->EX (first lock)
 *                PR->EX (second lock)
 *
 * The first lock can't be granted because of the granted mode of the second
 * lock and the second lock can't be granted because it's not first in the
 * list.  We demote the granted mode of the second lock (the lkb passed to this
 * function).
 *
 * After the resolution, the "grant pending" function needs to go back and try
 * to grant locks on the convert queue again since the first lock can now be
 * granted.
 */

static int conversion_deadlock_detect(struct dlm_rsb *rsb, struct dlm_lkb *lkb)
{
	struct dlm_lkb *this, *first = NULL, *self = NULL;

	list_for_each_entry(this, &rsb->res_convertqueue, lkb_statequeue) {
		if (!first)
			first = this;
		if (this == lkb) {
			self = lkb;
			continue;
		}

		if (!ranges_overlap(lkb, this))
			continue;

		if (!modes_compat(this, lkb) && !modes_compat(lkb, this))
			return TRUE;
	}

	/* if lkb is on the convert queue and is preventing the first
	   from being granted, then there's deadlock and we demote lkb.
	   multiple converting locks may need to do this before the first
	   converting lock can be granted. */

	if (self && self != first) {
		if (!modes_compat(lkb, first) &&
		    !queue_conflict(&rsb->res_grantqueue, first))
			return TRUE;
	}

	return FALSE;
}

/*
 * Return 1 if the lock can be granted, 0 otherwise.
 * Also detect and resolve conversion deadlocks.
 *
 * lkb is the lock to be granted
 *
 * now is 1 if the function is being called in the context of the
 * immediate request, it is 0 if called later, after the lock has been
 * queued.
 *
 * References are from chapter 6 of "VAXcluster Principles" by Roy Davis
 */

static int _can_be_granted(struct dlm_rsb *r, struct dlm_lkb *lkb, int now)
{
	int8_t conv = (lkb->lkb_grmode != DLM_LOCK_IV);

	/*
	 * 6-10: Version 5.4 introduced an option to address the phenomenon of
	 * a new request for a NL mode lock being blocked.
	 *
	 * 6-11: If the optional EXPEDITE flag is used with the new NL mode
	 * request, then it would be granted.  In essence, the use of this flag
	 * tells the Lock Manager to expedite theis request by not considering
	 * what may be in the CONVERTING or WAITING queues...  As of this
	 * writing, the EXPEDITE flag can be used only with new requests for NL
	 * mode locks.  This flag is not valid for conversion requests.
	 *
	 * A shortcut.  Earlier checks return an error if EXPEDITE is used in a
	 * conversion or used with a non-NL requested mode.  We also know an
	 * EXPEDITE request is always granted immediately, so now must always
	 * be 1.  The full condition to grant an expedite request: (now &&
	 * !conv && lkb->rqmode == DLM_LOCK_NL && (flags & EXPEDITE)) can
	 * therefore be shortened to just checking the flag.
	 */

	if (lkb->lkb_exflags & DLM_LKF_EXPEDITE)
		return TRUE;

	/*
	 * A shortcut. Without this, !queue_conflict(grantqueue, lkb) would be
	 * added to the remaining conditions.
	 */

	if (queue_conflict(&r->res_grantqueue, lkb))
		goto out;

	/*
	 * 6-3: By default, a conversion request is immediately granted if the
	 * requested mode is compatible with the modes of all other granted
	 * locks
	 */

	if (queue_conflict(&r->res_convertqueue, lkb))
		goto out;

	/*
	 * 6-5: But the default algorithm for deciding whether to grant or
	 * queue conversion requests does not by itself guarantee that such
	 * requests are serviced on a "first come first serve" basis.  This, in
	 * turn, can lead to a phenomenon known as "indefinate postponement".
	 *
	 * 6-7: This issue is dealt with by using the optional QUECVT flag with
	 * the system service employed to request a lock conversion.  This flag
	 * forces certain conversion requests to be queued, even if they are
	 * compatible with the granted modes of other locks on the same
	 * resource.  Thus, the use of this flag results in conversion requests
	 * being ordered on a "first come first servce" basis.
	 *
	 * DCT: This condition is all about new conversions being able to occur
	 * "in place" while the lock remains on the granted queue (assuming
	 * nothing else conflicts.)  IOW if QUECVT isn't set, a conversion
	 * doesn't _have_ to go onto the convert queue where it's processed in
	 * order.  The "now" variable is necessary to distinguish converts
	 * being received and processed for the first time now, because once a
	 * convert is moved to the conversion queue the condition below applies
	 * requiring fifo granting.
	 */

	if (now && conv && !(lkb->lkb_exflags & DLM_LKF_QUECVT))
		return TRUE;

	/*
	 * When using range locks the NOORDER flag is set to avoid the standard
	 * vms rules on grant order.
	 */

	if (lkb->lkb_exflags & DLM_LKF_NOORDER)
		return TRUE;

	/*
	 * 6-3: Once in that queue [CONVERTING], a conversion request cannot be
	 * granted until all other conversion requests ahead of it are granted
	 * and/or canceled.
	 */

	if (!now && conv && first_in_list(lkb, &r->res_convertqueue))
		return TRUE;

	/*
	 * 6-4: By default, a new request is immediately granted only if all
	 * three of the following conditions are satisfied when the request is
	 * issued:
	 * - The queue of ungranted conversion requests for the resource is
	 *   empty.
	 * - The queue of ungranted new requests for the resource is empty.
	 * - The mode of the new request is compatible with the most
	 *   restrictive mode of all granted locks on the resource.
	 */

	if (now && !conv && list_empty(&r->res_convertqueue) &&
	    list_empty(&r->res_waitqueue))
		return TRUE;

	/*
	 * 6-4: Once a lock request is in the queue of ungranted new requests,
	 * it cannot be granted until the queue of ungranted conversion
	 * requests is empty, all ungranted new requests ahead of it are
	 * granted and/or canceled, and it is compatible with the granted mode
	 * of the most restrictive lock granted on the resource.
	 */

	if (!now && !conv && list_empty(&r->res_convertqueue) &&
	    first_in_list(lkb, &r->res_waitqueue))
		return TRUE;

 out:
	/*
	 * The following, enabled by CONVDEADLK, departs from VMS.
	 */

	if (conv && (lkb->lkb_exflags & DLM_LKF_CONVDEADLK) &&
	    conversion_deadlock_detect(r, lkb)) {
		lkb->lkb_grmode = DLM_LOCK_NL;
		lkb->lkb_sbflags |= DLM_SBF_DEMOTED;
	}

	return FALSE;
}

/*
 * The ALTPR and ALTCW flags aren't traditional lock manager flags, but are a
 * simple way to provide a big optimization to applications that can use them.
 */

static int can_be_granted(struct dlm_rsb *r, struct dlm_lkb *lkb, int now)
{
	uint32_t flags = lkb->lkb_exflags;
	int rv;
	int8_t alt = 0, rqmode = lkb->lkb_rqmode;

	rv = _can_be_granted(r, lkb, now);
	if (rv)
		goto out;

	if (lkb->lkb_sbflags & DLM_SBF_DEMOTED)
		goto out;

	if (rqmode != DLM_LOCK_PR && flags & DLM_LKF_ALTPR)
		alt = DLM_LOCK_PR;
	else if (rqmode != DLM_LOCK_CW && flags & DLM_LKF_ALTCW)
		alt = DLM_LOCK_CW;

	if (alt) {
		lkb->lkb_rqmode = alt;
		rv = _can_be_granted(r, lkb, now);
		if (rv)
			lkb->lkb_sbflags |= DLM_SBF_ALTMODE;
		else
			lkb->lkb_rqmode = rqmode;
	}
 out:
	return rv;
}

int grant_pending_convert(struct dlm_rsb *r, int high)
{
	struct dlm_lkb *lkb, *s;
	int hi, demoted, quit, grant_restart, demote_restart;

	quit = 0;
 restart:
	grant_restart = 0;
	demote_restart = 0;
	hi = DLM_LOCK_IV;

	list_for_each_entry_safe(lkb, s, &r->res_convertqueue, lkb_statequeue) {
		demoted = is_demoted(lkb);
		if (can_be_granted(r, lkb, FALSE)) {
			grant_lock_pending(r, lkb);
			grant_restart = 1;
		} else {
			hi = MAX(lkb->lkb_rqmode, hi);
			if (!demoted && is_demoted(lkb))
				demote_restart = 1;
		}
	}

	if (grant_restart)
		goto restart;
	if (demote_restart && !quit) {
		quit = 1;
		goto restart;
	}

	return MAX(high, hi);
}

int grant_pending_wait(struct dlm_rsb *r, int high)
{
	struct dlm_lkb *lkb, *s;

	list_for_each_entry_safe(lkb, s, &r->res_waitqueue, lkb_statequeue) {
		if (can_be_granted(r, lkb, FALSE))
			grant_lock_pending(r, lkb);
                else
			high = MAX(lkb->lkb_rqmode, high);
	}

	return high;
}

int grant_pending_locks(struct dlm_rsb *r)
{
	struct dlm_lkb *lkb, *s;
	int high = DLM_LOCK_IV;

	high = grant_pending_convert(r, high);
	high = grant_pending_wait(r, high);

	if (high == DLM_LOCK_IV)
		return 0;

	/*
	 * If there are locks left on the wait/convert queue then send blocking
	 * ASTs to granted locks that are blocking
	 * FIXME: This might generate spurious blocking ASTs for range locks.
	 * FIXME: the highbast < high comparison is not always valid.
	 */

	list_for_each_entry_safe(lkb, s, &r->res_grantqueue, lkb_statequeue) {
		if (lkb->lkb_bastaddr && (lkb->lkb_highbast < high) &&
		    !__dlm_compat_matrix[lkb->lkb_grmode+1][high+1]) {
			queue_bast(r, lkb, high);
			lkb->lkb_highbast = high;
		}
	}

	return 0;
}

static void send_bast_queue(struct dlm_rsb *r, struct list_head *head,
			    struct dlm_lkb *lkb)
{
	struct dlm_lkb *gr;

	list_for_each_entry(gr, head, lkb_statequeue) {
		if (gr->lkb_bastaddr &&
		    gr->lkb_highbast < lkb->lkb_rqmode &&
		    ranges_overlap(lkb, gr) && !modes_compat(gr, lkb)) {
			queue_bast(r, gr, lkb->lkb_rqmode);
			gr->lkb_highbast = lkb->lkb_rqmode;
		}
	}
}

static void send_blocking_asts(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	send_bast_queue(r, &r->res_grantqueue, lkb);
}

static void send_blocking_asts_all(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	send_bast_queue(r, &r->res_grantqueue, lkb);
	send_bast_queue(r, &r->res_convertqueue, lkb);
}

/*
 * Four stage 4 varieties:
 * do_request(), do_convert(), do_unlock(), do_cancel()
 * These are called on the master node for the given lock and
 * from the central locking logic.
 */

int do_request(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error = 0;

	if (can_be_granted(r, lkb, TRUE)) {
		grant_lock(r, lkb);
		queue_cast(r, lkb, 0);
		goto out;
	}

	if (can_be_queued(lkb)) {
		error = -EINPROGRESS;
		add_lkb(r, lkb, DLM_LKSTS_WAITING);
		send_blocking_asts(r, lkb);
		goto out;
	}

	error = -EAGAIN;
	if (force_blocking_asts(lkb))
		send_blocking_asts_all(r, lkb);
	queue_cast(r, lkb, -EAGAIN);

 out:
	return error;
}

int do_convert(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error = 0;

	/* changing an existing lock may allow others to be granted */

	if (can_be_granted(r, lkb, TRUE)) {
		grant_lock(r, lkb);
		queue_cast(r, lkb, 0);
		grant_pending_locks(r);
		goto out;
	}

	if (can_be_queued(lkb)) {
		if (is_demoted(lkb))
			grant_pending_locks(r);
		error = -EINPROGRESS;
		del_lkb(r, lkb);
		add_lkb(r, lkb, DLM_LKSTS_CONVERT);
		send_blocking_asts(r, lkb);
		goto out;
	}

	error = -EAGAIN;
	if (force_blocking_asts(lkb))
		send_blocking_asts_all(r, lkb);
	queue_cast(r, lkb, -EAGAIN);

 out:
	return error;
}

int do_unlock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	remove_lock(r, lkb);
	queue_cast(r, lkb, -DLM_EUNLOCK);
	/* this unhold undoes the original ref from create_lkb()
	   so this leads to the lkb being freed */
	unhold_lkb(lkb);
	grant_pending_locks(r);
	return -DLM_EUNLOCK;
}

/* FIXME: cancel can also remove an lkb from the waitqueue which
   leads to the lkb being freed */

int do_cancel(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	revert_lock(r, lkb);
	queue_cast(r, lkb, -DLM_ECANCEL);
	grant_pending_locks(r);
	return -DLM_ECANCEL;
}


/*
 * send/receive routines for remote operations and replies
 *
 * send_args
 * send_common
 * send_request			receive_request
 * send_convert			receive_convert
 * send_unlock			receive_unlock
 * send_cancel			receive_cancel
 * send_grant			receive_grant
 * send_bast			receive_bast
 * send_lookup			receive_lookup
 * send_remove			receive_remove
 *
 * 				send_common_reply
 * receive_request_reply	send_request_reply
 * receive_convert_reply	send_convert_reply
 * receive_unlock_reply		send_unlock_reply
 * receive_cancel_reply		send_cancel_reply
 * receive_lookup_reply		send_lookup_reply
 */

int create_message(struct dlm_rsb *r, int to_nodeid, int mstype,
		   struct dlm_message **ms_ret, struct dlm_mhandle **mh_ret)
{
	struct dlm_message *ms;
	struct dlm_mhandle *mh;
	char *mb;
	int mb_len = sizeof(struct dlm_message);

	if (mstype == DLM_MSG_REQUEST ||
	    mstype == DLM_MSG_LOOKUP ||
	    mstype == DLM_MSG_REMOVE)
		mb_len += r->res_length;

	/* get_buffer gives us a message handle (mh) that we need to
	   pass into lowcomms_commit and a message buffer (mb) that we
	   write our data into */

	mh = lowcomms_get_buffer(to_nodeid, mb_len, GFP_KERNEL, &mb);
	if (!mh)
		return -ENOBUFS;

	memset(mb, 0, mb_len);

	ms = (struct dlm_message *) mb;

	ms->m_header.h_version = (DLM_HEADER_MAJOR | DLM_HEADER_MINOR);
	ms->m_header.h_lockspace = r->res_ls->ls_global_id;
	ms->m_header.h_nodeid = dlm_our_nodeid();
	ms->m_header.h_length = mb_len;
	ms->m_header.h_cmd = DLM_MSG;

	ms->m_type = mstype;

	*mh_ret = mh;
	*ms_ret = ms;
	return 0;
}

int send_message(struct dlm_mhandle *mh, struct dlm_message *ms)
{
	struct dlm_header *hd = (struct dlm_header *) ms;

	/* log_print("send %d lkid %x remlkid %x", ms->m_type,
		  ms->m_lkid, ms->m_remlkid); */

	/* FIXME: do byte swapping here */
	hd->h_length = cpu_to_le16(hd->h_length);

	lowcomms_commit_buffer(mh);
	return 0;
}

void send_args(struct dlm_rsb *r, struct dlm_lkb *lkb, struct dlm_message *ms)
{
	ms->m_nodeid   = lkb->lkb_nodeid;
	ms->m_pid      = lkb->lkb_ownpid;
	ms->m_lkid     = lkb->lkb_id;
	ms->m_remlkid  = lkb->lkb_remid;
	ms->m_exflags  = lkb->lkb_exflags;
	ms->m_sbflags  = lkb->lkb_sbflags;
	ms->m_flags    = lkb->lkb_flags;
	ms->m_lvbseq   = lkb->lkb_lvbseq;
	ms->m_status   = lkb->lkb_status;
	ms->m_grmode   = lkb->lkb_grmode;
	ms->m_rqmode   = lkb->lkb_rqmode;

	/* m_result and m_bastmode are set from function args,
	   not from lkb fields */

	if (lkb->lkb_bastaddr)
		ms->m_asts |= AST_BAST;
	if (lkb->lkb_astaddr)
		ms->m_asts |= AST_COMP;

	if (lkb->lkb_range) {
		ms->m_range[0] = lkb->lkb_range[RQ_RANGE_START];
		ms->m_range[1] = lkb->lkb_range[RQ_RANGE_END];
	}

	if (lkb->lkb_lvbptr)
		memcpy(ms->m_lvb, lkb->lkb_lvbptr, DLM_LVB_LEN);
	
	if (ms->m_type == DLM_MSG_REQUEST || ms->m_type == DLM_MSG_LOOKUP)
		memcpy(ms->m_name, r->res_name, r->res_length);
}

int send_common(struct dlm_rsb *r, struct dlm_lkb *lkb, int mstype)
{
	struct dlm_message *ms;
	struct dlm_mhandle *mh;
	int to_nodeid, error;

	add_to_waiters(lkb, mstype);

	to_nodeid = r->res_nodeid;

	error = create_message(r, to_nodeid, mstype, &ms, &mh);
	if (error)
		goto fail;

	send_args(r, lkb, ms);

	error = send_message(mh, ms);
	if (error)
		goto fail;
	return 0;

 fail:
	remove_from_waiters(lkb);
	return error;
}

int send_request(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	return send_common(r, lkb, DLM_MSG_REQUEST);
}

int send_convert(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	return send_common(r, lkb, DLM_MSG_CONVERT);
}

/* FIXME: if this lkb is the only lock we hold on the rsb, then set
   MASTER_UNCERTAIN to force the next request on the rsb to confirm
   that the master is still correct. */
   
int send_unlock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	return send_common(r, lkb, DLM_MSG_UNLOCK);
}

int send_cancel(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	return send_common(r, lkb, DLM_MSG_CANCEL);
}

int send_grant(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	struct dlm_message *ms;
	struct dlm_mhandle *mh;
	int to_nodeid, error;

	to_nodeid = lkb->lkb_nodeid;

	error = create_message(r, to_nodeid, DLM_MSG_GRANT, &ms, &mh);
	if (error)
		goto out;

	send_args(r, lkb, ms);

	ms->m_result = 0;

	error = send_message(mh, ms);
 out:
	return error;
}

int send_bast(struct dlm_rsb *r, struct dlm_lkb *lkb, int mode)
{
	struct dlm_message *ms;
	struct dlm_mhandle *mh;
	int to_nodeid, error;

	to_nodeid = lkb->lkb_nodeid;

	error = create_message(r, to_nodeid, DLM_MSG_BAST, &ms, &mh);
	if (error)
		goto out;

	send_args(r, lkb, ms);

	ms->m_bastmode = mode;

	error = send_message(mh, ms);
 out:
	return error;
}

int send_lookup(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	struct dlm_message *ms;
	struct dlm_mhandle *mh;
	int to_nodeid, error;

	add_to_waiters(lkb, DLM_MSG_LOOKUP);

	to_nodeid = dlm_dir_nodeid(r);

	error = create_message(r, to_nodeid, DLM_MSG_LOOKUP, &ms, &mh);
	if (error)
		goto fail;

	send_args(r, lkb, ms);

	error = send_message(mh, ms);
	if (error)
		goto fail;
	return 0;

 fail:
	remove_from_waiters(lkb);
	return error;
}

int send_remove(struct dlm_rsb *r)
{
	struct dlm_message *ms;
	struct dlm_mhandle *mh;
	int to_nodeid, error;

	to_nodeid = dlm_dir_nodeid(r);

	error = create_message(r, to_nodeid, DLM_MSG_REMOVE, &ms, &mh);
	if (error)
		goto out;

	memcpy(ms->m_name, r->res_name, r->res_length);

	error = send_message(mh, ms);
 out:
	return error;
}

int send_common_reply(struct dlm_rsb *r, struct dlm_lkb *lkb, int mstype, int rv)
{
	struct dlm_message *ms;
	struct dlm_mhandle *mh;
	int to_nodeid, error;

	to_nodeid = lkb->lkb_nodeid;

	error = create_message(r, to_nodeid, mstype, &ms, &mh);
	if (error)
		goto out;

	send_args(r, lkb, ms);

	ms->m_result = rv;

	error = send_message(mh, ms);
 out:
	return error;
}

int send_request_reply(struct dlm_rsb *r, struct dlm_lkb *lkb, int rv)
{
	return send_common_reply(r, lkb, DLM_MSG_REQUEST_REPLY, rv);
}

int send_convert_reply(struct dlm_rsb *r, struct dlm_lkb *lkb, int rv)
{
	return send_common_reply(r, lkb, DLM_MSG_CONVERT_REPLY, rv);
}

int send_unlock_reply(struct dlm_rsb *r, struct dlm_lkb *lkb, int rv)
{
	return send_common_reply(r, lkb, DLM_MSG_UNLOCK_REPLY, rv);
}

int send_cancel_reply(struct dlm_rsb *r, struct dlm_lkb *lkb, int rv)
{
	return send_common_reply(r, lkb, DLM_MSG_CANCEL_REPLY, rv);
}

int send_lookup_reply(struct dlm_ls *ls, struct dlm_message *ms_in,
		      int ret_nodeid, int rv)
{
	struct dlm_rsb *r = &ls->ls_stub_rsb;
	struct dlm_message *ms;
	struct dlm_mhandle *mh;
	int error, to_nodeid = ms_in->m_header.h_nodeid;

	error = create_message(r, to_nodeid, DLM_MSG_LOOKUP_REPLY, &ms, &mh);
	if (error)
		goto out;

	ms->m_lkid = ms_in->m_lkid;
	ms->m_result = rv;
	ms->m_nodeid = ret_nodeid;

	error = send_message(mh, ms);
 out:
	return error;
}

/* which args we save from a received message depends heavily on the type
   of message, unlike the send side where we can safely send everything about
   the lkb for any type of message */

void receive_flags(struct dlm_lkb *lkb, struct dlm_message *ms)
{
	lkb->lkb_exflags = ms->m_exflags;
	lkb->lkb_flags = (lkb->lkb_flags & 0xFFFF0000) |
		         (ms->m_flags & 0x0000FFFF);
}

void receive_flags_reply(struct dlm_lkb *lkb, struct dlm_message *ms)
{
	lkb->lkb_sbflags = ms->m_sbflags;
	lkb->lkb_flags = (lkb->lkb_flags & 0xFFFF0000) |
		         (ms->m_flags & 0x0000FFFF);
}

int receive_namelen(struct dlm_message *ms)
{
	return (ms->m_header.h_length - sizeof(struct dlm_message));
}

int receive_range(struct dlm_ls *ls, struct dlm_lkb *lkb,
		  struct dlm_message *ms)
{
	if (lkb->lkb_flags & DLM_IFL_RANGE) {
		lkb->lkb_range = allocate_range(ls);
		if (!lkb->lkb_range)
			return -ENOMEM;
		lkb->lkb_range[RQ_RANGE_START] = ms->m_range[0];
		lkb->lkb_range[RQ_RANGE_END] = ms->m_range[1];
	}
	return 0;
}

int receive_lvb(struct dlm_ls *ls, struct dlm_lkb *lkb, struct dlm_message *ms)
{
	if (lkb->lkb_exflags & DLM_LKF_VALBLK) {
		if (!lkb->lkb_lvbptr) {
			lkb->lkb_lvbptr = allocate_lvb(ls);
			if (!lkb->lkb_lvbptr)
				return -ENOMEM;
		}
		memcpy(lkb->lkb_lvbptr, ms->m_lvb, DLM_LVB_LEN);
	}
	return 0;
}

int receive_request_args(struct dlm_ls *ls, struct dlm_lkb *lkb,
			 struct dlm_message *ms)
{
	lkb->lkb_nodeid = ms->m_header.h_nodeid;
	lkb->lkb_ownpid = ms->m_pid;
	lkb->lkb_remid = ms->m_lkid;
	lkb->lkb_grmode = DLM_LOCK_IV;
	lkb->lkb_rqmode = ms->m_rqmode;
	lkb->lkb_bastaddr = (void *) (long) (ms->m_asts & AST_BAST);
	lkb->lkb_astaddr = (void *) (long) (ms->m_asts & AST_COMP);

	DLM_ASSERT(is_master_copy(lkb), dlm_print_lkb(lkb););

	if (receive_range(ls, lkb, ms))
		return -ENOMEM;

	if (receive_lvb(ls, lkb, ms))
		return -ENOMEM;

	return 0;
}

int receive_convert_args(struct dlm_ls *ls, struct dlm_lkb *lkb,
			 struct dlm_message *ms)
{
	if (lkb->lkb_nodeid != ms->m_header.h_nodeid) {
		log_error(ls, "convert_args nodeid %d %d lkid %x %x",
			  lkb->lkb_nodeid, ms->m_header.h_nodeid,
			  lkb->lkb_id, lkb->lkb_remid);
		return -EINVAL;
	}

	DLM_ASSERT(is_master_copy(lkb), dlm_print_lkb(lkb););

	lkb->lkb_rqmode = ms->m_rqmode;
	lkb->lkb_lvbseq = ms->m_lvbseq;

	if (receive_range(ls, lkb, ms))
		return -ENOMEM;

	if (lkb->lkb_range) {
		lkb->lkb_range[GR_RANGE_START] = 0LL;
		lkb->lkb_range[GR_RANGE_END] = 0xffffffffffffffffULL;
	}

	if (receive_lvb(ls, lkb, ms))
		return -ENOMEM;

	return 0;
}

int receive_unlock_args(struct dlm_ls *ls, struct dlm_lkb *lkb,
			struct dlm_message *ms)
{
	DLM_ASSERT(is_master_copy(lkb), dlm_print_lkb(lkb););
	if (receive_lvb(ls, lkb, ms))
		return -ENOMEM;
	return 0;
}

/* We fill in the stub-lkb fields with the info that send_xxxx_reply()
   uses to send a reply and that the remote end uses to process the reply. */

void setup_stub_lkb(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb = &ls->ls_stub_lkb;
	lkb->lkb_nodeid = ms->m_header.h_nodeid;
	lkb->lkb_remid = ms->m_lkid;
}

void receive_request(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error, namelen;

	error = create_lkb(ls, &lkb);
	if (error)
		goto fail;

	receive_flags(lkb, ms);
	lkb->lkb_flags |= DLM_IFL_MSTCPY;
	error = receive_request_args(ls, lkb, ms);
	if (error) {
		put_lkb(lkb);
		goto fail;
	}

	namelen = receive_namelen(ms);

	error = find_rsb(ls, ms->m_name, namelen, R_MASTER, &r);
	if (error) {
		put_lkb(lkb);
		goto fail;
	}

	lock_rsb(r);

	attach_lkb(r, lkb);
	error = do_request(r, lkb);
	send_request_reply(r, lkb, error);

	unlock_rsb(r);
	put_rsb(r);

	if (error == -EINPROGRESS)
		error = 0;
	if (error)
		put_lkb(lkb);
	return;

 fail:
	setup_stub_lkb(ls, ms);
	send_request_reply(&ls->ls_stub_rsb, &ls->ls_stub_lkb, error);
}

void receive_convert(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remlkid, &lkb);
	if (error)
		goto fail;

	receive_flags(lkb, ms);
	error = receive_convert_args(ls, lkb, ms);
	if (error) {
		put_lkb(lkb);
		goto fail;
	}

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	error = do_convert(r, lkb);
	send_convert_reply(r, lkb, error);

	unlock_rsb(r);
	put_rsb(r);
	put_lkb(lkb);
	return;

 fail:
	setup_stub_lkb(ls, ms);
	send_convert_reply(&ls->ls_stub_rsb, &ls->ls_stub_lkb, error);
}

void receive_unlock(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remlkid, &lkb);
	if (error)
		goto fail;

	receive_flags(lkb, ms);
	error = receive_unlock_args(ls, lkb, ms);
	if (error) {
		put_lkb(lkb);
		goto fail;
	}

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	error = do_unlock(r, lkb);
	send_unlock_reply(r, lkb, error);

	unlock_rsb(r);
	put_rsb(r);
	put_lkb(lkb);
	return;

 fail:
	setup_stub_lkb(ls, ms);
	send_unlock_reply(&ls->ls_stub_rsb, &ls->ls_stub_lkb, error);
}

void receive_cancel(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remlkid, &lkb);
	if (error)
		goto fail;

	receive_flags(lkb, ms);

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	error = do_cancel(r, lkb);
	send_cancel_reply(r, lkb, error);

	unlock_rsb(r);
	put_rsb(r);
	put_lkb(lkb);
	return;

 fail:
	setup_stub_lkb(ls, ms);
	send_cancel_reply(&ls->ls_stub_rsb, &ls->ls_stub_lkb, error);
}

void receive_grant(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remlkid, &lkb);
	if (error) {
		log_error(ls, "receive_grant no lkb");
		return;
	}
	DLM_ASSERT(is_process_copy(lkb), dlm_print_lkb(lkb););

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	receive_flags_reply(lkb, ms);
	grant_lock_pc(r, lkb, ms);
	queue_cast(r, lkb, 0);

	unlock_rsb(r);
	put_rsb(r);
	put_lkb(lkb);
}

void receive_bast(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remlkid, &lkb);
	if (error) {
		log_error(ls, "receive_bast no lkb");
		return;
	}
	DLM_ASSERT(is_process_copy(lkb), dlm_print_lkb(lkb););

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	queue_bast(r, lkb, ms->m_bastmode);

	unlock_rsb(r);
	put_rsb(r);
	put_lkb(lkb);
}

void receive_lookup(struct dlm_ls *ls, struct dlm_message *ms)
{
	int len, error, ret_nodeid, dir_nodeid, from_nodeid;

	from_nodeid = ms->m_header.h_nodeid;

	len = receive_namelen(ms);

	dir_nodeid = dlm_dir_name2nodeid(ls, ms->m_name, len);
	if (dir_nodeid != dlm_our_nodeid()) {
		log_error(ls, "lookup dir_nodeid %d from %d",
			  dir_nodeid, from_nodeid);
		error = -EINVAL;
		ret_nodeid = -1;
		goto out;
	}

	error = dlm_dir_lookup(ls, from_nodeid, ms->m_name, len, &ret_nodeid);
 out:
	send_lookup_reply(ls, ms, ret_nodeid, error);
}

void receive_remove(struct dlm_ls *ls, struct dlm_message *ms)
{
	int len, dir_nodeid, from_nodeid;

	from_nodeid = ms->m_header.h_nodeid;

	len = receive_namelen(ms);

	dir_nodeid = dlm_dir_name2nodeid(ls, ms->m_name, len);
	if (dir_nodeid != dlm_our_nodeid()) {
		log_error(ls, "remove dir entry dir_nodeid %d from %d",
			  dir_nodeid, from_nodeid);
		return;
	}

	dlm_dir_remove_entry(ls, from_nodeid, ms->m_name, len);
}

void receive_request_reply(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remlkid, &lkb);
	if (error) {
		log_error(ls, "receive_request_reply no lkb");
		return;
	}
	DLM_ASSERT(is_process_copy(lkb), dlm_print_lkb(lkb););

	error = remove_from_waiters(lkb);
	if (error) {
		log_error(ls, "receive_request_reply not on waiters");
		goto out;
	}

	/* this is the value returned from do_request() on the master */
	error = ms->m_result;

	r = lkb->lkb_resource;
	hold_rsb(r);
	lock_rsb(r);

	switch (error) {
	case -EAGAIN:
		/* request would block (be queued) on remote master;
		   the unhold undoes the original ref from create_lkb()
		   so it leads to the lkb being freed */
		queue_cast(r, lkb, -EAGAIN);
		confirm_master(r, -EAGAIN);
		unhold_lkb(lkb);
		break;

	case -EINPROGRESS:
	case 0:
		/* request was queued or granted on remote master */
		receive_flags_reply(lkb, ms);
		lkb->lkb_remid = ms->m_lkid;
		if (error)
			add_lkb(r, lkb, DLM_LKSTS_WAITING);
		else {
			grant_lock_pc(r, lkb, ms);
			queue_cast(r, lkb, 0);
		}
		confirm_master(r, error);
		break;

	case -ENOENT:
	case -ENOTBLK:
		/* find_rsb failed to find rsb or rsb wasn't master */

		DLM_ASSERT(test_bit(RESFL_MASTER_WAIT, &r->res_flags),
		           log_print("receive_request_reply error %d", error);
		           dlm_print_lkb(lkb);
		           dlm_print_rsb(r););

		confirm_master(r, error);
		lkb->lkb_nodeid = -1;
		_request_lock(r, lkb);
		break;

	default:
		log_error(ls, "receive_request_reply unknown error %d", error);
	}

	unlock_rsb(r);
	put_rsb(r);
 out:
	put_lkb(lkb);
}

void receive_convert_reply(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remlkid, &lkb);
	if (error) {
		log_error(ls, "receive_convert_reply no lkb");
		return;
	}
	DLM_ASSERT(is_process_copy(lkb), dlm_print_lkb(lkb););

	error = remove_from_waiters(lkb);
	if (error) {
		log_error(ls, "receive_convert_reply not on waiters");
		goto out;
	}

	/* this is the value returned from do_convert() on the master */
	error = ms->m_result;

	r = lkb->lkb_resource;
	hold_rsb(r);
	lock_rsb(r);

	switch (error) {
	case -EAGAIN:
		/* convert would block (be queued) on remote master */
		queue_cast(r, lkb, -EAGAIN);
		break;

	case -EINPROGRESS:
		/* convert was queued on remote master */
		del_lkb(r, lkb);
		add_lkb(r, lkb, DLM_LKSTS_CONVERT);
		break;

	case 0:
		/* convert was granted on remote master */
		receive_flags_reply(lkb, ms);
		grant_lock_pc(r, lkb, ms);
		queue_cast(r, lkb, 0);
		break;

	default:
		log_error(ls, "receive_convert_reply unknown error %d", error);
	}

	unlock_rsb(r);
	put_rsb(r);
 out:
	put_lkb(lkb);
}

void _receive_unlock_reply(struct dlm_ls *ls, struct dlm_lkb *lkb,
			   struct dlm_message *ms)
{
	struct dlm_rsb *r = lkb->lkb_resource;
	int error = ms->m_result;

	hold_rsb(r);
	lock_rsb(r);

	/* this is the value returned from do_unlock() on the master */

	switch (error) {
	case -DLM_EUNLOCK:
		receive_flags_reply(lkb, ms);
		remove_lock_pc(r, lkb);
		queue_cast(r, lkb, -DLM_EUNLOCK);
		/* this unhold undoes the original ref from create_lkb()
	   	   so this leads to the lkb being freed */
		unhold_lkb(lkb);
		break;
	default:
		log_error(ls, "receive_unlock_reply unknown error %d", error);
	}

	unlock_rsb(r);
	put_rsb(r);
}

void receive_unlock_reply(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	int error;

	error = find_lkb(ls, ms->m_remlkid, &lkb);
	if (error) {
		log_error(ls, "receive_unlock_reply no lkb");
		return;
	}
	DLM_ASSERT(is_process_copy(lkb), dlm_print_lkb(lkb););

	error = remove_from_waiters(lkb);
	if (error) {
		log_error(ls, "receive_unlock_reply not on waiters");
		goto out;
	}

	_receive_unlock_reply(ls, lkb, ms);
 out:
	put_lkb(lkb);
}

void _receive_cancel_reply(struct dlm_ls *ls, struct dlm_lkb *lkb,
			   struct dlm_message *ms)
{
	struct dlm_rsb *r = lkb->lkb_resource;
	int error = ms->m_result;

	hold_rsb(r);
	lock_rsb(r);

	/* this is the value returned from do_cancel() on the master */

	switch (error) {
	case -DLM_ECANCEL:
		receive_flags_reply(lkb, ms);
		revert_lock_pc(r, lkb);
		queue_cast(r, lkb, -DLM_ECANCEL);
		break;
	default:
		log_error(ls, "receive_cancel_reply unknown error %d", error);
	}

	unlock_rsb(r);
	put_rsb(r);
}

void receive_cancel_reply(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	int error;

	error = find_lkb(ls, ms->m_remlkid, &lkb);
	if (error) {
		log_error(ls, "receive_cancel_reply no lkb");
		return;
	}
	DLM_ASSERT(is_process_copy(lkb), dlm_print_lkb(lkb););

	error = remove_from_waiters(lkb);
	if (error) {
		log_error(ls, "receive_cancel_reply not on waiters");
		goto out;
	}

	_receive_cancel_reply(ls, lkb, ms);
 out:
	put_lkb(lkb);
}

void receive_lookup_reply(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error, ret_nodeid;

	error = find_lkb(ls, ms->m_lkid, &lkb);
	if (error) {
		log_error(ls, "receive_lookup_reply no lkb");
		return;
	}

	error = remove_from_waiters(lkb);
	if (error) {
		log_error(ls, "receive_lookup_reply not on waiters");
		goto out;
	}

	/* this is the value returned by dlm_dir_lookup on dir node
	   FIXME: will a non-zero error ever be returned? */
	error = ms->m_result;

	r = lkb->lkb_resource;
	hold_rsb(r);
	lock_rsb(r);

	ret_nodeid = ms->m_nodeid;
	if (ret_nodeid == dlm_our_nodeid())
		r->res_nodeid = ret_nodeid = 0;
	else {
		r->res_nodeid = ret_nodeid;
		r->res_trial_lkid = lkb->lkb_id;
	}

	_request_lock(r, lkb);

	if (!ret_nodeid)
		confirm_master(r, 0);

	unlock_rsb(r);
	put_rsb(r);
 out:
	put_lkb(lkb);
}

int dlm_receive_message(struct dlm_header *hd, int nodeid, int recovery)
{
	struct dlm_message *ms = (struct dlm_message *) hd;
	struct dlm_ls *ls;
	int error;

	/* FIXME: do byte swapping here */
	hd->h_length = le16_to_cpu(hd->h_length);
	DLM_ASSERT(hd->h_nodeid,);

	ls = find_lockspace_global(hd->h_lockspace);
	if (!ls) {
		log_print("drop message %d from %d for unknown lockspace %d",
			  ms->m_type, nodeid, hd->h_lockspace);
		return -EINVAL;
	}

	/* recovery may have just ended leaving a bunch of backed-up requests
	   in the requestqueue; wait while dlm_recoverd clears them */

	if (!recovery)
		dlm_wait_requestqueue(ls);

	/* recovery may have just started while there were a bunch of
	   in-flight requests -- save them in requestqueue to be processed
	   after recovery.  we can't let dlm_recvd block on the recovery
	   lock.  if dlm_recoverd is calling this function to clear the
	   requestqueue, it needs to be interrupted (-EINTR) if another
	   recovery operation is starting. */

	while (1) {
		if (!test_bit(LSFL_LS_RUN, &ls->ls_flags)) {
			if (!recovery)
				dlm_add_requestqueue(ls, nodeid, hd);
			error = -EINTR;
			goto out;
		}

		if (lock_recovery_try(ls))
			break;
		schedule();
	}

	/* log_print("recv %d lkid %x remlkid %x result %d", ms->m_type,
		  ms->m_lkid, ms->m_remlkid, ms->m_result); */

	switch (ms->m_type) {

	/* messages sent to a master node */

	case DLM_MSG_REQUEST:
		receive_request(ls, ms);
		break;

	case DLM_MSG_CONVERT:
		receive_convert(ls, ms);
		break;

	case DLM_MSG_UNLOCK:
		receive_unlock(ls, ms);
		break;

	case DLM_MSG_CANCEL:
		receive_cancel(ls, ms);
		break;

	/* messages sent from a master node (replies to above) */

	case DLM_MSG_REQUEST_REPLY:
		receive_request_reply(ls, ms);
		break;

	case DLM_MSG_CONVERT_REPLY:
		receive_convert_reply(ls, ms);
		break;

	case DLM_MSG_UNLOCK_REPLY:
		receive_unlock_reply(ls, ms);
		break;

	case DLM_MSG_CANCEL_REPLY:
		receive_cancel_reply(ls, ms);
		break;

	/* messages sent from a master node (only two types of async msg) */

	case DLM_MSG_GRANT:
		receive_grant(ls, ms);
		break;

	case DLM_MSG_BAST:
		receive_bast(ls, ms);
		break;

	/* messages sent to a dir node */

	case DLM_MSG_LOOKUP:
		receive_lookup(ls, ms);
		break;

	case DLM_MSG_REMOVE:
		receive_remove(ls, ms);
		break;

	/* messages sent from a dir node (remove has no reply) */

	case DLM_MSG_LOOKUP_REPLY:
		receive_lookup_reply(ls, ms);
		break;

	default:
		log_error(ls, "unknown message type %d", ms->m_type);
	}

	unlock_recovery(ls);
 out:
	put_lockspace(ls);
	dlm_astd_wake();
	return 0;
}


/*
 * Recovery related
 */

/* Create a single list of all root rsb's to be used during recovery */

int dlm_create_root_list(struct dlm_ls *ls)
{
	struct dlm_rsb *r;
	int i, error = 0;

	down_write(&ls->ls_root_sem);
	if (!list_empty(&ls->ls_root_list)) {
		log_error(ls, "root list not empty");
		error = -EINVAL;
		goto out;
	}

	for (i = 0; i < ls->ls_rsbtbl_size; i++) {
		write_lock(&ls->ls_rsbtbl[i].lock);
		list_for_each_entry(r, &ls->ls_rsbtbl[i].list, res_hashchain) {
			list_add(&r->res_root_list, &ls->ls_root_list);
			hold_rsb(r);
		}
		write_unlock(&ls->ls_rsbtbl[i].lock);
	}
 out:
	up_write(&ls->ls_root_sem);
	return error;
}

void dlm_release_root_list(struct dlm_ls *ls)
{
	struct dlm_rsb *r, *safe;

	down_write(&ls->ls_root_sem);
	list_for_each_entry_safe(r, safe, &ls->ls_root_list, res_root_list) {
		list_del_init(&r->res_root_list);
		put_rsb(r);
	}
	up_write(&ls->ls_root_sem);
}

/* We only need to do recovery for lkb's waiting for replies from nodes
   who have been removed. */

void dlm_recover_waiters_pre(struct dlm_ls *ls)
{
	struct dlm_lkb *lkb, *safe;

	down(&ls->ls_waiters_sem);

	list_for_each_entry_safe(lkb, safe, &ls->ls_waiters, lkb_wait_reply) {
		if (!dlm_is_removed(ls, lkb->lkb_nodeid))
			continue;

		log_debug(ls, "pre recover waiter lkid %x type %d flags %x",
			  lkb->lkb_id, lkb->lkb_wait_type, lkb->lkb_flags);

		switch (lkb->lkb_wait_type) {

		case DLM_MSG_REQUEST:
		case DLM_MSG_CONVERT:
			lkb->lkb_flags |= DLM_IFL_RESEND;
			break;

		case DLM_MSG_UNLOCK:
			ls->ls_stub_ms.m_result = -DLM_EUNLOCK;
			_remove_from_waiters(lkb);
			_receive_unlock_reply(ls, lkb, &ls->ls_stub_ms);
			break;

		case DLM_MSG_CANCEL:
			ls->ls_stub_ms.m_result = -DLM_ECANCEL;
			_remove_from_waiters(lkb);
			_receive_cancel_reply(ls, lkb, &ls->ls_stub_ms);
			break;

		case DLM_MSG_LOOKUP:
			/* all outstanding lookups, regardless of dest.
			   will be resent after recovery is done */
			break;

		default:
			log_error(ls, "invalid lkb wait_type %d",
				  lkb->lkb_wait_type);
		}
	}
	up(&ls->ls_waiters_sem);
}

int remove_resend_waiter(struct dlm_ls *ls, struct dlm_lkb **lkb_ret)
{
	struct dlm_lkb *lkb;
	int rv = 0;

	down(&ls->ls_waiters_sem);
	list_for_each_entry(lkb, &ls->ls_waiters, lkb_wait_reply) {
		if (lkb->lkb_flags & DLM_IFL_RESEND) {
			rv = lkb->lkb_wait_type;
			_remove_from_waiters(lkb);
			lkb->lkb_flags &= ~DLM_IFL_RESEND;
			break;
		}
	}
	up(&ls->ls_waiters_sem);

	if (!rv)
		lkb = NULL;
	*lkb_ret = lkb;
	return rv;
}

/* Deal with lookups and lkb's marked RESEND from _pre.  We may now be the
   master or dir-node for r.  Processing the lkb may result in it being placed
   back on waiters. */

int dlm_recover_waiters_post(struct dlm_ls *ls)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error = 0, mstype;

	while (1) {
		if (!test_bit(LSFL_LS_RUN, &ls->ls_flags)) {
			log_debug(ls, "recover_waiters_post aborted");
			error = -EINTR;
			break;
		}

		mstype = remove_resend_waiter(ls, &lkb);
		if (!mstype)
			break;

		r = lkb->lkb_resource;

		log_debug(ls, "recover_waiters_post %x type %d flags %x %s",
			  lkb->lkb_id, mstype, lkb->lkb_flags, r->res_name);

		switch (mstype) {

		case DLM_MSG_LOOKUP:
		case DLM_MSG_REQUEST:
			hold_rsb(r);
			lock_rsb(r);
			_request_lock(r, lkb);
			unlock_rsb(r);
			put_rsb(r);
			break;

		case DLM_MSG_CONVERT:
			hold_rsb(r);
			lock_rsb(r);
			_convert_lock(r, lkb);
			unlock_rsb(r);
			put_rsb(r);
			break;

		default:
			log_error(ls, "recover_waiters_post type %d", mstype);
		}
	}

	return error;
}

static int purge_queue(struct dlm_rsb *r, struct list_head *queue)
{
	struct dlm_ls *ls = r->res_ls;
	struct dlm_lkb *lkb, *safe;

	list_for_each_entry_safe(lkb, safe, queue, lkb_statequeue) {
		if (!is_master_copy(lkb))
			continue;

		if (dlm_is_removed(ls, lkb->lkb_nodeid)) {
			del_lkb(r, lkb);
			/* this put should free the lkb */
			if (!put_lkb(lkb))
				log_error(ls, "purged lkb not released");
		}
	}
	return 0;
}

/*
 * Get rid of locks held by nodes that are gone.
 */

int dlm_purge_locks(struct dlm_ls *ls)
{
	struct dlm_rsb *r;

	log_debug(ls, "dlm_purge_locks");

	down_write(&ls->ls_root_sem);
	list_for_each_entry(r, &ls->ls_root_list, res_root_list) {
		hold_rsb(r);
		lock_rsb(r);

		purge_queue(r, &r->res_grantqueue);
		purge_queue(r, &r->res_convertqueue);
		purge_queue(r, &r->res_waitqueue);

		unlock_rsb(r);
		unhold_rsb(r);

		schedule();
	}
	up_write(&ls->ls_root_sem);

	return 0;
}

int dlm_grant_after_purge(struct dlm_ls *ls)
{
	struct dlm_rsb *r;
	int i;

	for (i = 0; i < ls->ls_rsbtbl_size; i++) {
		read_lock(&ls->ls_rsbtbl[i].lock);
		list_for_each_entry(r, &ls->ls_rsbtbl[i].list, res_hashchain) {
			hold_rsb(r);
			lock_rsb(r);
			grant_pending_locks(r);
			unlock_rsb(r);
			put_rsb(r);
		}
		read_unlock(&ls->ls_rsbtbl[i].lock);
	}

	return 0;
}

struct dlm_lkb *search_remid_list(struct list_head *head, uint32_t remid)
{
	struct dlm_lkb *lkb;

	list_for_each_entry(lkb, head, lkb_statequeue) {
		if (lkb->lkb_remid == remid)
			return lkb;
	}
	return lkb;
}

struct dlm_lkb *search_remid(struct dlm_rsb *r, uint32_t remid)
{
	struct dlm_lkb *lkb;

	lkb = search_remid_list(&r->res_grantqueue, remid);
	if (lkb)
		return lkb;
	lkb = search_remid_list(&r->res_convertqueue, remid);
	if (lkb)
		return lkb;
	lkb = search_remid_list(&r->res_waitqueue, remid);
	if (lkb)
		return lkb;
	return NULL;
}

int receive_rcom_lock_args(struct dlm_ls *ls, struct dlm_lkb *lkb,
			   struct dlm_rsb *r, struct dlm_rcom *rc)
{
	struct rcom_lock *rl = (struct rcom_lock *) rc->rc_buf;

	lkb->lkb_nodeid = rc->rc_header.h_nodeid;
	lkb->lkb_ownpid = rl->rl_ownpid;
	lkb->lkb_remid = rl->rl_id;
	lkb->lkb_grmode = rl->rl_grmode;
	lkb->lkb_rqmode = rl->rl_rqmode;
	lkb->lkb_bastaddr = (void *) (long) (rl->rl_asts & AST_BAST);
	lkb->lkb_astaddr = (void *) (long) (rl->rl_asts & AST_COMP);

	lkb->lkb_exflags = rl->rl_exflags;
	lkb->lkb_flags = rl->rl_flags & 0x0000FFFF;
	lkb->lkb_flags |= DLM_IFL_MSTCPY;

	if (lkb->lkb_flags & DLM_IFL_RANGE) {
		lkb->lkb_range = allocate_range(ls);
		if (!lkb->lkb_range)
			return -ENOMEM;
		memcpy(lkb->lkb_range, rl->rl_range, 4*sizeof(uint64_t));
	}

	if (lkb->lkb_exflags & DLM_LKF_VALBLK) {
		lkb->lkb_lvbptr = allocate_lvb(ls);
		if (!lkb->lkb_lvbptr)
			return -ENOMEM;
		memcpy(lkb->lkb_lvbptr, rl->rl_lvb, DLM_LVB_LEN);
	}

	return 0;
}

/* This lkb may have been recovered in a previous aborted recovery so we need
   to check if the rsb already has an lkb with the given remote nodeid/lkid.
   If so we just send back a standard reply.  If not, we create a new lkb with
   the given values and send back our lkid.  We send back our lkid by sending
   back the rcom_lock struct we got but with the remid field filled in. */

int dlm_recover_master_copy(struct dlm_ls *ls, struct dlm_rcom *rc)
{
	struct rcom_lock *rl = (struct rcom_lock *) rc->rc_buf;
	struct dlm_rsb *r;
	struct dlm_lkb *lkb;
	int error;

	if (rl->rl_parent_lkid) {
		error = -EOPNOTSUPP;
		goto out;
	}

	error = find_rsb(ls, rl->rl_name, rl->rl_namelen, R_MASTER, &r);
	if (error)
		goto out;

	lock_rsb(r);

	lkb = search_remid(r, rl->rl_id);
	if (lkb) {
		error = -EEXIST;
		goto out_unlock;
	}

	error = create_lkb(ls, &lkb);
	if (error)
		goto out_unlock;

	error = receive_rcom_lock_args(ls, lkb, r, rc);
	if (error) {
		put_lkb(lkb);
		goto out_unlock;
	}

	attach_lkb(r, lkb);
	add_lkb(r, lkb, rl->rl_status);

	/* this is the new value returned to the lock holder for
	   saving in its process-copy lkb */
	rl->rl_remid = lkb->lkb_id;
	error = 0;

 out_unlock:
	unlock_rsb(r);
	put_rsb(r);
 out:
	rl->rl_result = error;
	return error;
}

int dlm_recover_process_copy(struct dlm_ls *ls, struct dlm_rcom *rc)
{
	struct rcom_lock *rl = (struct rcom_lock *) rc->rc_buf;
	struct dlm_rsb *r;
	struct dlm_lkb *lkb;
	int error;

	error = find_lkb(ls, rl->rl_id, &lkb);
	if (error) {
		log_error(ls, "recover_process_copy no lkid %x", rl->rl_id);
		return error;
	}

	DLM_ASSERT(is_process_copy(lkb), dlm_print_lkb(lkb););

	error = rl->rl_result;

	r = lkb->lkb_resource;
	hold_rsb(r);
	lock_rsb(r);

	switch (error) {
	case 0:
		lkb->lkb_remid = rl->rl_remid;
		break;
	case -EEXIST:
		log_debug(ls, "lkb %x previously recovered", lkb->lkb_id);
		break;
	default:
		log_error(ls, "dlm_recover_process_copy unknown error %d %x",
			  error, lkb->lkb_id);
	}

	/* an ack for dlm_recover_locks() which waits for replies from
	   all the locks it sends to new masters */
	dlm_recovered_lock(r);

	unlock_rsb(r);
	put_rsb(r);
	put_lkb(lkb);

	return 0;
}

