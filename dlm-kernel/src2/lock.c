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
#include "astd.h"
#include "lock.h"
#include "lock_remote.h"

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

static struct list_head dlm_waiters;
static struct semaphore dlm_waiters_sem;

void dlm_init_lock(void)
{
	INIT_LIST_HEAD(&dlm_waiters);
	init_MUTEX(&dlm_waiters_sem);
}

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


/*
 * Threads cannot use the lockspace while it's being recovered
 */

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
	DLM_ASSERT(r->res_nodeid >= 0, );
	return r->res_nodeid;
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
	return (lkb->lkb_nodeid && (lkb->lkb_flags & DLM_IFL_MSTCPY));
}

void queue_cast(struct dlm_rsb *r, struct dlm_lkb *lkb, int rv)
{
	if (is_master_copy(lkb))
		return;

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
		dlm_dir_remove_entry(r->res_ls, 0, r->res_name, r->res_length);
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
	INIT_LIST_HEAD(&r->res_rootlist);

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
	if (!test_bit(RESFL_MASTER, &r->res_flags) && (flags & R_MASTER))
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
			set_bit(RESFL_MASTER_WAIT, &r->res_flags);
			set_bit(RESFL_MASTER_UNCERTAIN, &r->res_flags);
			r->res_trial_lkid = 0;
		} else {
			DLM_ASSERT(r->res_nodeid == 0, );
			DLM_ASSERT(test_bit(RESFL_MASTER, &r->res_flags),);
			DLM_ASSERT(!test_bit(RESFL_MASTER_WAIT,
					     &r->res_flags),);
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

void toss_rsb(struct kref *kref)
{
	struct dlm_rsb *r = container_of(kref, struct dlm_rsb, res_ref);
	struct dlm_ls *ls = r->res_ls;

	DLM_ASSERT(list_empty(&r->res_rootlist),);
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
	/* DLM_ASSERT(!kref_put(&r->res_ref, toss_rsb),); */
	kref_put(&r->res_ref, toss_rsb);
}

void kill_rsb(struct kref *kref)
{
	struct dlm_rsb *r = container_of(kref, struct dlm_rsb, res_ref);

	list_del(&r->res_hashchain);
	dir_remove(r);
	free_rsb(r);
}

/* FIXME: create a global dlm thread that periodically calls this
   for each lockspace */

void shrink_rsb_list(struct dlm_ls *ls, struct list_head *head)
{
#if 0
	struct dlm_rsb *r, *safe;

	list_for_each_entry_safe(r, safe, head, res_hashchain) {
		if (toss_time_due(ls, r))
			kref_put(&r->res_ref, kill_rsb);
	}
#endif
}

/* FIXME: these rsbtbl locks are held for far too long */

void shrink_tossed_rsbs(struct dlm_ls *ls)
{
	int i;

	for (i = 0; i < ls->ls_rsbtbl_size; i++) {
		write_lock(&ls->ls_rsbtbl[i].lock);
		shrink_rsb_list(ls, &ls->ls_rsbtbl[i].toss);
		write_unlock(&ls->ls_rsbtbl[i].lock);
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

void kill_lkb(struct kref *kref)
{
	struct dlm_lkb *lkb = container_of(kref, struct dlm_lkb, lkb_ref);

	DLM_ASSERT(!lkb->lkb_status,);

	list_del(&lkb->lkb_idtbl_list);

	detach_lkb(lkb);

	/* for local/process lkbs, lvbptr points to the caller's lksb */
	if (lkb->lkb_lvbptr && is_master_copy(lkb))
		free_lvb(lkb->lkb_lvbptr);
	if (lkb->lkb_range)
		free_range(lkb->lkb_range);
	free_lkb(lkb);
}

int create_lkb(struct dlm_ls *ls, struct dlm_lkb **lkb_ret)
{
	struct dlm_lkb *lkb;
	uint32_t lkid;
	uint16_t bucket;

	lkb = allocate_lkb(ls);
	if (!lkb)
		return -ENOMEM;

	kref_init(&lkb->lkb_ref);

	get_random_bytes(&bucket, sizeof(bucket));
	bucket &= (ls->ls_lkbtbl_size - 1);

	write_lock(&ls->ls_lkbtbl[bucket].lock);
	lkid = bucket | (ls->ls_lkbtbl[bucket].counter++ << 16);
	/* FIXME: do a find to verify lkid not in use */

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
		return -EINVAL;

	read_lock(&ls->ls_lkbtbl[bucket].lock);
	lkb = __find_lkb(ls, lkid);
	if (lkb)
		kref_get(&lkb->lkb_ref);
	read_unlock(&ls->ls_lkbtbl[bucket].lock);

	*lkb_ret = lkb;
	return lkb ? 0 : -ENOENT;
}

int put_lkb(struct dlm_lkb *lkb)
{
	struct dlm_ls *ls = lkb->lkb_resource->res_ls;
	uint16_t bucket = lkb->lkb_id & 0xFFFF;
	int rv = 0;

	write_lock(&ls->ls_lkbtbl[bucket].lock);
	/* rv = kref_put(&lkb->lkb_ref, kill_lkb); */
	kref_put(&lkb->lkb_ref, kill_lkb);
	write_unlock(&ls->ls_lkbtbl[bucket].lock);
	return rv;
}

int dlm_put_lkb(struct dlm_lkb *lkb)
{
	return put_lkb(lkb);
}

/* This is called when we need to remove a reference and are certain
   it's not the last ref.  e.g. del_lkb is always called between a
   find_lkb/put_lkb and is always the inverse of a previous add_lkb.
   put_lkb would work fine, but would involve unnecessary locking */

void unhold_lkb(struct dlm_lkb *lkb)
{
	/* DLM_ASSERT(!kref_put(&lkb->lkb_ref, kill_lkb),); */
	kref_put(&lkb->lkb_ref, kill_lkb);
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

	DLM_ASSERT(!lkb->lkb_status,);

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

/* add/remove lkb from global waiters list of lkb's waiting for
   a reply from a remote node */

void add_to_waiters(struct dlm_lkb *lkb, int mstype)
{
	down(&dlm_waiters_sem);
	if (lkb->lkb_wait_type) {
		printk("add_to_waiters error %d", lkb->lkb_wait_type);
		goto out;
	}
	lkb->lkb_wait_type = mstype;
	kref_get(&lkb->lkb_ref);
	list_add(&lkb->lkb_wait_reply, &dlm_waiters);
 out:
	up(&dlm_waiters_sem);
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
	int error;
	down(&dlm_waiters_sem);
	error = _remove_from_waiters(lkb);
	up(&dlm_waiters_sem);
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

	/* return lkid in lksb for new locks */

	if (!lksb->sb_lkid)
		lksb->sb_lkid = lkb->lkb_id;
	rv = 0;
 out:
	return rv;
}

int set_unlock_args(struct dlm_ls *ls, struct dlm_lkb *lkb, uint32_t flags,
		    struct dlm_lksb *lksb, void *astarg)	
{
	int rv = -EINVAL;

	if (lkb->lkb_flags & DLM_IFL_MSTCPY)
		goto out;

	if (flags & DLM_LKF_CANCEL && lkb->lkb_status == DLM_LKSTS_GRANTED)
		goto out;

	if (!(flags & DLM_LKF_CANCEL) && lkb->lkb_status != DLM_LKSTS_GRANTED)
		goto out;

	rv = -EBUSY;
	if (lkb->lkb_wait_type)
		goto out;

	lkb->lkb_exflags = flags;
	lkb->lkb_sbflags = 0;

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
	int error;

	ls = find_lockspace_local(lockspace);
	if (!ls)
		return -EINVAL;

	lock_recovery(ls);

	if (flags & DLM_LKF_CONVERT)
		error = find_lkb(ls, lksb->sb_lkid, &lkb);
	else
		error = create_lkb(ls, &lkb);

	if (error)
		goto out;

	error = set_lock_args(ls, lkb, mode, lksb, flags, namelen, parent_lkid,
			      ast, astarg, bast, range);
	if (error)
		goto out_put;

	if (flags & DLM_LKF_CONVERT)
		error = convert_lock(ls, lkb);
	else
		error = request_lock(ls, lkb, name, namelen);

	/* the lock request is queued */
	if (error == -EBUSY)
		error = 0;
 out_put:
	if (error)
		put_lkb(lkb);
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

 out_put:
	put_lkb(lkb);
 out:
	unlock_recovery(ls);
	put_lockspace(ls);
	return error;
}


/* set_master(r, lkb) -- set the master nodeid of a resource

   _request_lock calls this:
   o in the context of an original lock request, or
   o in the context of dlm_recvd after receiving a master lookup reply
     (which may be positive or negative), or
   o in the context of dlm_recoverd if the lkb was on the waiters
     list when recovery occured

   The purpose of this function is to set the nodeid field in the given
   lkb using the nodeid field in the given rsb.  If the rsb's nodeid is
   known, it can just be copied to the lkb and the function will return
   0.  If the rsb's nodeid is _not_ known, it needs to be looked up
   before it can be copied to the lkb.  If the rsb's nodeid is known,
   but is uncertain, then we may wait to copy it to the lkb until we
   know for certain.  [An rsb's nodeid may change (and is uncertain) after
   we look it up but before we are granted a lock on it.]
   
   When the rsb nodeid is being looked up, the initial lkb causing
   the lookup is kept on the dlm_waiters list waiting for the lookup
   reply.  Other lkb's waiting for the same rsb lookup are kept on
   the rsb's res_lookup list until the master is certain.

   After the initial lookup reply, the initial lkb gets the resulting
   nodeid copied from the rsb.  This lkb then "tries out" the nodeid
   since it's still uncertain.  This lkb is designed the "trial lkb"
   and is pointed to by res_trial_lkb until it gets a reply from the
   master indicating the lkb request was queued.  This confirms that
   the master nodeid is correct and allows any other lkb's on the
   res_lookup list to have their nodeid copied from the rsb.

   a. res_nodeid == 0   we are master
   b. res_nodeid > 0    remote node is master
   c. res_nodeid == -1  no idea who master is
   d. lkb_nodeid == 0   we are master, res_nodeid is certain
   e. lkb_nodeid != 0
   f. res_flags MASTER            we are master (a. should be true)
   g. res_flags MASTER_UNCERTAIN  (b. should be true in this case)
   h. res_flags MASTER_WAIT

   Cases:
   1. we've no idea who master is [c]
   2. we know who master /was/ in the past (r was tossed and remote) [b,g]
   3. we're certain of who master is (we hold granted locks) [b or a]
   4. we're in the process of looking up master from dir [c,h]
   5. we've been told who the master is (from dir) but it could
      change by the time we send it a lock request (only if we
      were told the master is remote) [b,g]
   6. 5 and we have a request outstanding to this uncertain master [b,g,h]

   When set_master is called again on a trial lkb it means the trial failed and
   the uncertain rsb master was wrong.  The rsb nodeid goes from being
   uncertain to unknown, and the lkb goes from being a trial to just waiting
   for the lookup reply.  The lkb will become a trial again when the lookup
   reply is received and the rsb nodeid is tried again.

   Return values:
   0: nodeid is set in rsb/lkb and the caller should go ahead and use it
   1: the rsb master is not available and the lkb has been placed on
      a wait queue
   -EXXX: there was some error in processing
*/
 
int set_master(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error;

	if (r->res_trial_lkid == lkb->lkb_id) {
		r->res_nodeid = -1;
		lkb->lkb_nodeid = -1;
		r->res_trial_lkid = 0;
		clear_bit(RESFL_MASTER_WAIT, &r->res_flags);
		clear_bit(RESFL_MASTER_UNCERTAIN, &r->res_flags);
	}

	if (r->res_nodeid == -1) {
		if (!test_bit(RESFL_MASTER_WAIT, &r->res_flags)) {
			error = send_lookup(r, lkb);
			if (error)
				return error;
			set_bit(RESFL_MASTER_WAIT, &r->res_flags);
		} else
			list_add_tail(&lkb->lkb_rsb_lookup, &r->res_lookup);
		return 1;
	}

	if (r->res_nodeid == 0) {
		lkb->lkb_nodeid = 0;
		return 0;
	}

	if (r->res_nodeid > 0) {
		if (!test_bit(RESFL_MASTER_UNCERTAIN, &r->res_flags)) {
			lkb->lkb_nodeid = r->res_nodeid;
			return 0;
		}

		/* When the rsb nodeid is uncertain we let the first
		   lkb try it out while any others wait.  If the trial
		   fails we'll end up back in this function in the first
		   condition. */

		if (!r->res_trial_lkid) {
			lkb->lkb_nodeid = r->res_nodeid;
			r->res_trial_lkid = lkb->lkb_id;
			return 0;
		} else {
			list_add_tail(&lkb->lkb_rsb_lookup, &r->res_lookup);
			return 1;
		}
	}

	return -1;
}

/* confirm_master -- confirm an rsb's master nodeid

   This is called when a master node sends a non-EINVAL reply to a
   request.  It means that we have the rsb nodeid correct, which is
   interesting if we're waiting to confirm an uncertain rsb nodeid.
 
   The "rv" return value is the result the master sent back for the
   request.  It could be -EAGAIN (the lkb would block), -EBUSY (the
   lkb was put on the wait queue), or 0 (the lkb was granted.)
 
   If the rsb nodeid is uncertain and the rv is -EAGAIN, we still
   can't be certain so we need to do another trial lkb.  If the
   rsb is uncertain and the rv is -EBUSY or 0, we are now certain
   and can send any lkb requests waiting on res_lookup.
*/

void confirm_master(struct dlm_rsb *r, int rv)
{
	struct dlm_lkb *lkb, *safe;

	if (!test_bit(RESFL_MASTER_UNCERTAIN, &r->res_flags))
		return;

	if (rv == 0 || rv == -EBUSY) {
		clear_bit(RESFL_MASTER_UNCERTAIN, &r->res_flags);
		clear_bit(RESFL_MASTER_WAIT, &r->res_flags);
		r->res_trial_lkid = 0;

		list_for_each_entry_safe(lkb, safe, &r->res_lookup,
					 lkb_rsb_lookup) {
			list_del(&lkb->lkb_rsb_lookup);
			_request_lock(r, lkb);
			schedule();
		}
		return;
	}

	if (rv == -EAGAIN) {
		/* unsure about this case where lookup list is empty */
		if (list_empty(&r->res_lookup)) {
			r->res_trial_lkid = 0;
			printk("confirm_master: lookup list empty\n");
			return;
		}

		/* setting trial_lkid to 0 allows this lkb to become the
		   trial when _request_lock calls set_master */

		lkb = list_entry(r->res_lookup.next, struct dlm_lkb,
				 lkb_rsb_lookup);
		list_del(&lkb->lkb_rsb_lookup);
		r->res_trial_lkid = 0;
		_request_lock(r, lkb);
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


/*
 * Special-purpose routines called by the core locking functions.
 * Deal with "details" whereas primary routines deal with locking "logic".
 * (Many routines from locking.c can be used here with minor change.)
 */

/* lkb is master or local copy */

void set_lvb_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int b;

	if (!r->res_lvbptr)
		r->res_lvbptr = allocate_lvb(r->res_ls);

	/* b=1 lvb returned to caller
	   b=0 lvb written to rsb or invalidated
	   b=-1 do nothing */

	b =  __lvb_operations[lkb->lkb_grmode + 1][lkb->lkb_rqmode + 1];

	if (b == 1) {
		if (!lkb->lkb_lvbptr)
			return;

		if (!(lkb->lkb_exflags & DLM_LKF_VALBLK))
			return;

		memcpy(lkb->lkb_lvbptr, r->res_lvbptr, DLM_LVB_LEN);
		lkb->lkb_lvbseq = r->res_lvbseq;

	} else if (b == 0) {
		if (lkb->lkb_exflags & DLM_LKF_IVVALBLK) {
			set_bit(RESFL_VALNOTVALID, &r->res_flags);
			return;
		}

		if (!lkb->lkb_lvbptr)
			return;

		if (!(lkb->lkb_exflags & DLM_LKF_VALBLK))
			return;

		memcpy(r->res_lvbptr, lkb->lkb_lvbptr, DLM_LVB_LEN);
		r->res_lvbseq++;
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

/* pc: lkb is process copy */

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

/* "remove_lock" varieties used for unlock */

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

/* "revert_lock" varieties used for cancel */

void revert_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	del_lkb(r, lkb);
	lkb->lkb_rqmode = DLM_LOCK_IV;
	add_lkb(r, lkb, DLM_LKSTS_GRANTED);
}

void revert_lock_pc(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	revert_lock(r, lkb);
}

/* "grant_lock" varieties used for request and convert */

void _grant_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	if (lkb->lkb_grmode != lkb->lkb_rqmode) {
		lkb->lkb_grmode = lkb->lkb_rqmode;
		del_lkb(r, lkb);
		add_lkb(r, lkb, DLM_LKSTS_GRANTED);
	}

	lkb->lkb_rqmode = DLM_LOCK_IV;

	if (lkb->lkb_range) {
		lkb->lkb_range[GR_RANGE_START] = lkb->lkb_range[RQ_RANGE_START];
		lkb->lkb_range[GR_RANGE_END] = lkb->lkb_range[RQ_RANGE_END];
	}
}

/* lkb is master or local copy */

void grant_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	set_lvb_lock(r, lkb);
	_grant_lock(r, lkb);
	lkb->lkb_highbast = 0;
}

/* pc: lkb is process copy */

void grant_lock_pc(struct dlm_rsb *r, struct dlm_lkb *lkb,
		   struct dlm_message *ms)
{
	set_lvb_lock_pc(r, lkb, ms);
	_grant_lock(r, lkb);
}

/* called by grant_pending_locks() which means an async grant message must
   be sent to the requesting node in addition to granting the lock.
   i.e. it's not granted in the context of the initial request/convert */

void grant_lock_pending(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	grant_lock(r, lkb);
	if (is_master_copy(lkb))
		send_grant(r, lkb);
}

static inline int first_in_list(struct dlm_lkb *lkb, struct list_head *head)
{
	struct dlm_lkb *first = list_entry(head->next, struct dlm_lkb, lkb_statequeue);

	if (lkb->lkb_id == first->lkb_id)
		return 1;

	return 0;
}

/*
 * Return 1 if the locks' ranges overlap
 * If the lkb has no range then it is assumed to cover 0-ffffffff.ffffffff
 */

static inline int ranges_overlap(struct dlm_lkb *lkb1, struct dlm_lkb *lkb2)
{
	if (!lkb1->lkb_range || !lkb2->lkb_range)
		return 1;

	if (lkb1->lkb_range[RQ_RANGE_END] < lkb2->lkb_range[GR_RANGE_START] ||
	    lkb1->lkb_range[RQ_RANGE_START] > lkb2->lkb_range[GR_RANGE_END])
		return 0;

	return 1;
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
	 *
	 * FIXME: This might generate some spurious blocking ASTs for range
	 * locks.
	 *
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
		error = -EBUSY;
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
		error = -EBUSY;
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
	grant_pending_locks(r);
	queue_cast(r, lkb, -DLM_EUNLOCK);
	/* this unhold undoes the original ref from create_lkb()
	   so this leads to the lkb being freed */
	unhold_lkb(lkb);
	return -DLM_EUNLOCK;
}

int do_cancel(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	revert_lock(r, lkb);
	grant_pending_locks(r);
	queue_cast(r, lkb, -DLM_ECANCEL);
	return -DLM_ECANCEL;
}


/*
 * send/receive routines for remote operations and replies
 *
 * send_args     (all sends use this for setting args)
 * send_common
 * send_request
 * send_convert
 * send_unlock
 * send_cancel
 * send_grant
 * send_bast
 * send_lookup
 *
 * send_common_reply
 * send_request_reply
 * send_convert_reply
 * send_unlock_reply
 * send_cancel_reply
 * send_lookup_reply
 *
 * receive_request
 * receive_convert
 * receive_unlock
 * receive_cancel
 * receive_grant
 * receive_bast
 * receive_lookup
 *
 * receive_request_reply
 * receive_convert_reply
 * receive_unlock_reply
 * receive_cancel_reply
 * receive_lookup_reply
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

int send_message(struct dlm_mhandle *mh)
{
	/* FIXME: add byte order munging */
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

	error = send_message(mh);
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

	error = send_message(mh);
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

	error = send_message(mh);
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

	error = send_message(mh);
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

	error = send_message(mh);
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

	error = send_message(mh);
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

int send_lookup_reply(struct dlm_ls *ls, int to_nodeid, int ret_nodeid, int rv)
{
	struct dlm_rsb r = { .res_ls = ls };
	struct dlm_message *ms;
	struct dlm_mhandle *mh;
	int error;

	error = create_message(&r, to_nodeid, DLM_MSG_LOOKUP_REPLY, &ms, &mh);
	if (error)
		goto out;

	ms->m_result = rv;
	ms->m_nodeid = ret_nodeid;

	error = send_message(mh);
 out:
	return error;
}

/* When we aren't able to use the standard send_xxx_reply to return an error,
   then we use this. */

int send_fail_reply(struct dlm_ls *ls, struct dlm_message *ms_in, int rv)
{
	struct dlm_rsb r = { .res_ls = ls };
	struct dlm_message *ms;
	struct dlm_mhandle *mh;
	int error, nodeid = ms_in->m_header.h_nodeid;

	error = create_message(&r, nodeid, DLM_MSG_FAIL_REPLY, &ms, &mh);
	if (error)
		goto out;

	ms->m_lkid = ms_in->m_lkid;
	ms->m_result = rv;

	error = send_message(mh);
 out:
	return error;
}


/* which args we save from a received message depends heavily on the type
   of message, unlike the send side where we can safely send everything
   about the lkb for any type of message */

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

int receive_range(struct dlm_ls *ls, struct dlm_lkb *lkb, struct dlm_message *ms)
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

int receive_request_args(struct dlm_ls *ls, struct dlm_lkb *lkb, struct dlm_message *ms)
{
	lkb->lkb_nodeid = ms->m_header.h_nodeid;
	lkb->lkb_ownpid = ms->m_pid;
	lkb->lkb_remid = ms->m_lkid;
	lkb->lkb_grmode = DLM_LOCK_IV;
	lkb->lkb_rqmode = ms->m_rqmode;
	lkb->lkb_bastaddr = (void *) (long) (ms->m_asts & AST_BAST);
	lkb->lkb_astaddr = (void *) (long) (ms->m_asts & AST_COMP);

	if (receive_range(ls, lkb, ms))
		return -ENOMEM;

	if (receive_lvb(ls, lkb, ms))
		return -ENOMEM;

	return 0;
}

int receive_convert_args(struct dlm_ls *ls, struct dlm_lkb *lkb, struct dlm_message *ms)
{
	lkb->lkb_rqmode = ms->m_rqmode;
	lkb->lkb_lvbseq = ms->m_lvbseq;

	if (receive_range(ls, lkb, ms))
		return -ENOMEM;

	lkb->lkb_range[GR_RANGE_START] = 0LL;
	lkb->lkb_range[GR_RANGE_END] = 0xffffffffffffffffULL;

	if (receive_lvb(ls, lkb, ms))
		return -ENOMEM;

	return 0;
}

int receive_unlock_args(struct dlm_ls *ls, struct dlm_lkb *lkb, struct dlm_message *ms)
{
	if (receive_lvb(ls, lkb, ms))
		return -ENOMEM;
	return 0;
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

	namelen = receive_namelen(ms);

	error = receive_request_args(ls, lkb, ms);
	if (error) {
		put_lkb(lkb);
		goto fail;
	}

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
	put_lkb(lkb);
	return;
 fail:
	send_fail_reply(ls, ms, error);
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
	send_fail_reply(ls, ms, error);
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
	send_fail_reply(ls, ms, error);
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
	send_fail_reply(ls, ms, error);
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
	DLM_ASSERT(is_process_copy(lkb),);

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
	DLM_ASSERT(is_process_copy(lkb),);

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

	dir_nodeid = name_to_directory_nodeid(ls, ms->m_name, len);
	if (dir_nodeid != dlm_our_nodeid()) {
		error = -EINVAL;
		ret_nodeid = -1;
		goto out;
	}

	error = dlm_dir_lookup(ls, from_nodeid, ms->m_name, len, &ret_nodeid);
 out:
	send_lookup_reply(ls, from_nodeid, ret_nodeid, error);
}

void receive_remove(struct dlm_ls *ls, struct dlm_message *ms)
{
	int len, dir_nodeid, from_nodeid;

	from_nodeid = ms->m_header.h_nodeid;

	len = receive_namelen(ms);

	dir_nodeid = name_to_directory_nodeid(ls, ms->m_name, len);
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
	DLM_ASSERT(is_process_copy(lkb),);

	error = remove_from_waiters(lkb);
	if (error) {
		log_error(ls, "receive_request_reply not on lockqueue");
		goto out;
	}

	/* this is the value returned from do_request() on the master */
	error = ms->m_result;

	/* now follow pattern of xxx_lock() functions */

	r = lkb->lkb_resource;
	hold_rsb(r);
	lock_rsb(r);

	switch (error) {
	case -EINVAL:
		/* the remote node wasn't the master as we thought */
		_request_lock(r, lkb);
		break;

	case -EAGAIN:
		/* request would block (be queued) on remote master */
		queue_cast(r, lkb, -EAGAIN);
		confirm_master(r, -EAGAIN);
		break;

	case -EBUSY:
		/* request was queued on remote master */
		receive_flags_reply(lkb, ms);
		lkb->lkb_remid = ms->m_lkid;
		add_lkb(r, lkb, DLM_LKSTS_WAITING);
		confirm_master(r, -EBUSY);
		break;

	case 0:
		/* request was granted on remote master */
		receive_flags_reply(lkb, ms);
		lkb->lkb_remid = ms->m_lkid;
		grant_lock_pc(r, lkb, ms);
		queue_cast(r, lkb, 0);
		confirm_master(r, 0);
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
	DLM_ASSERT(is_process_copy(lkb),);

	error = remove_from_waiters(lkb);
	if (error) {
		log_error(ls, "receive_convert_reply not on lockqueue");
		goto out;
	}

	/* this is the value returned from do_convert() on the master */
	error = ms->m_result;

	/* now follow pattern of xxx_lock() functions */

	r = lkb->lkb_resource;
	hold_rsb(r);
	lock_rsb(r);

	switch (error) {
	case -EAGAIN:
		/* convert would block (be queued) on remote master */
		queue_cast(r, lkb, -EAGAIN);
		break;

	case -EBUSY:
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

void receive_unlock_reply(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remlkid, &lkb);
	if (error) {
		log_error(ls, "receive_unlock_reply no lkb");
		return;
	}
	DLM_ASSERT(is_process_copy(lkb),);

	error = remove_from_waiters(lkb);
	if (error) {
		log_error(ls, "receive_unlock_reply not on lockqueue");
		goto out;
	}

	/* this is the value returned from do_unlock() on the master */
	error = ms->m_result;

	/* now follow pattern of xxx_lock() functions */

	r = lkb->lkb_resource;
	hold_rsb(r);
	lock_rsb(r);

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
 out:
	put_lkb(lkb);
}

void receive_cancel_reply(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remlkid, &lkb);
	if (error) {
		log_error(ls, "receive_cancel_reply no lkb");
		return;
	}
	DLM_ASSERT(is_process_copy(lkb),);

	error = remove_from_waiters(lkb);
	if (error) {
		log_error(ls, "receive_cancel_reply not on lockqueue");
		goto out;
	}

	/* this is the value returned from do_cancel() on the master */
	error = ms->m_result;

	/* now follow pattern of xxx_lock() functions */

	r = lkb->lkb_resource;
	hold_rsb(r);
	lock_rsb(r);

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
 out:
	put_lkb(lkb);
}

void receive_lookup_reply(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remlkid, &lkb);
	if (error) {
		log_error(ls, "receive_lookup_reply no lkb");
		return;
	}

	error = remove_from_waiters(lkb);
	if (error) {
		log_error(ls, "receive_lookup_reply not on lockqueue");
		goto out;
	}

	/* this is the value returned by dlm_dir_lookup on dir node */
	error = ms->m_result;

	/* now follow pattern of xxx_lock() functions.
	   this is basically request_lock() again */

	r = lkb->lkb_resource;
	hold_rsb(r);
	lock_rsb(r);

	/* set nodeid as master in r; _request_lock calls
	   set_master(r, lkb) to propagate the master to lkb */

	r->res_nodeid = ms->m_nodeid;
	if (r->res_nodeid != 0)
		set_bit(RESFL_MASTER_UNCERTAIN, &r->res_flags);

	/* We ignore the return value of _request_lock because there's
	   no user context to return it to.  The user is left to getting the
	   result from the ast. */

	_request_lock(r, lkb);

	unlock_rsb(r);
	put_rsb(r);
 out:
	put_lkb(lkb);
}

void receive_fail_reply(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	int error;

	error = find_lkb(ls, ms->m_lkid, &lkb);
	if (error) {
		log_error(ls, "receive_fail_reply no lkb");
		return;
	}

	log_error(ls, "remote operation %d failed %d for lkid %d",
		  lkb->lkb_wait_type, ms->m_result, lkb->lkb_id);

	error = remove_from_waiters(lkb);
	if (error) {
		log_error(ls, "receive_fail_reply not on waiters");
		goto out;
	}

	/* not clear what to do here, a completion ast with m_result? */
 out:
	put_lkb(lkb);
}

int dlm_receive_message(struct dlm_header *hd, int nodeid, int recovery)
{
	struct dlm_message *ms = (struct dlm_message *) hd;
	struct dlm_ls *ls;
	int error;

	/* FIXME: do byte swapping here */

	ls = find_lockspace_global(hd->h_lockspace);
	if (!ls)
		return -EINVAL;

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

		error = lock_recovery_try(ls);
		if (!error)
			break;
		schedule();
	}

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

	case DLM_MSG_FAIL_REPLY:
		receive_fail_reply(ls, ms);
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

	/* messages sent from a dir node */

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

/* Create a single list of all root rsb's that's used during recovery */

int dlm_create_root_list(struct dlm_ls *ls)
{
	struct dlm_rsb *r;
	int i, error = 0;

	down_write(&ls->ls_root_lock);
	if (!list_empty(&ls->ls_rootres)) {
		log_error(ls, "rootres list not empty");
		error = -EINVAL;
		goto out;
	}

	for (i = 0; i < ls->ls_rsbtbl_size; i++) {
		write_lock(&ls->ls_rsbtbl[i].lock);
		list_for_each_entry(r, &ls->ls_rsbtbl[i].list, res_hashchain) {
			list_add(&r->res_rootlist, &ls->ls_rootres);
			hold_rsb(r);
		}
		write_unlock(&ls->ls_rsbtbl[i].lock);
	}
 out:
	up_write(&ls->ls_root_lock);
	return error;
}

void dlm_release_root_list(struct dlm_ls *ls)
{
	struct dlm_rsb *r, *safe;

	down_write(&ls->ls_root_lock);
	list_for_each_entry_safe(r, safe, &ls->ls_rootres, res_rootlist) {
		list_del_init(&r->res_rootlist);
		put_rsb(r);
	}
	up_write(&ls->ls_root_lock);
}

void dlm_recover_waiters_pre(struct dlm_ls *ls)
{
	struct dlm_lkb *lkb, *safe;
	struct dlm_rsb *r;

	down(&dlm_waiters_sem);

	list_for_each_entry_safe(lkb, safe, &dlm_waiters, lkb_wait_reply) {
		if (lkb->lkb_resource->res_ls != ls)
			continue;

		r = lkb->lkb_resource;

		/* we only need to do pre-recovery if the node the lkb
		   is waiting on is gone */

		if (!dlm_is_removed(ls, r->res_nodeid))
			continue;

		log_debug(ls, "pre recover waiter lkid %x type %d flags %x %s",
			  lkb->lkb_id, lkb->lkb_wait_type, lkb->lkb_flags,
			  r->res_name);

		switch (lkb->lkb_wait_type) {

		case DLM_MSG_REQUEST:
			lkb->lkb_flags |= DLM_IFL_RESEND;
			break;

		case DLM_MSG_CONVERT:
			lkb->lkb_flags |= DLM_IFL_RESEND;

			/* FIXME: don't think this flag is needed any more
			   since the process copy lkb isn't ever placed on
			   the convert queue. */
			lkb->lkb_flags |= DLM_IFL_CONVERTING;
			break;

		case DLM_MSG_UNLOCK:
			/* follow receive_unlock_reply() and pretend we
			   received an unlock reply from the former master */

			log_debug(ls, "fake unlock reply lkid %x %s",
				  lkb->lkb_id, r->res_name);

			_remove_from_waiters(lkb);

			/* FIXME: fake an ms struct and call
			   receive_unlock_reply() ? */

			hold_rsb(r);
			lock_rsb(r);

			remove_lock_pc(r, lkb);
			queue_cast(r, lkb, -DLM_EUNLOCK);

			/* this unhold undoes the original ref from
			   create_lkb() so this leads to the lkb being freed */
			unhold_lkb(lkb);

			unlock_rsb(r);
			put_rsb(r);
			put_lkb(lkb);
			break;

		case DLM_MSG_CANCEL:
			/* follow receive_cancel_reply() and pretend we
			   received a cancel reply from the former master */

			log_debug(ls, "fake cancel reply lkid %x %s",
				  lkb->lkb_id, r->res_name);

			_remove_from_waiters(lkb);

			/* FIXME: fake an ms struct and call
			   receive_cancel_reply() ? */

			hold_rsb(r);
			lock_rsb(r);
			revert_lock_pc(r, lkb);
			queue_cast(r, lkb, -DLM_ECANCEL);

			unlock_rsb(r);
			put_rsb(r);
			put_lkb(lkb);
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
	up(&dlm_waiters_sem);
}

void recover_request_lock(struct dlm_lkb *lkb)
{
	struct dlm_rsb *r = lkb->lkb_resource;

	/* FIXME: probably don't need hold_rsb/put_rsb here */

	hold_rsb(r);
	lock_rsb(r);
	_request_lock(r, lkb);
	unlock_rsb(r);
	put_rsb(r);
}

void recover_convert_lock(struct dlm_lkb *lkb)
{
	struct dlm_rsb *r = lkb->lkb_resource;

	/* FIXME: probably don't need hold_rsb/put_rsb here */

	hold_rsb(r);
	lock_rsb(r);
	_convert_lock(r, lkb);
	unlock_rsb(r);
	put_rsb(r);
}

struct dlm_lkb *remove_resend_waiter(struct dlm_ls *ls)
{
	struct dlm_lkb *lkb;
	int found = 0;

	down(&dlm_waiters_sem);
	list_for_each_entry(lkb, &dlm_waiters, lkb_wait_reply) {
		if (lkb->lkb_resource->res_ls != ls)
			continue;

		if (lkb->lkb_flags & DLM_IFL_RESEND) {
			_remove_from_waiters(lkb);
			found = 1;
			break;
		}
	}
	up(&dlm_waiters_sem);

	if (!found)
		lkb = NULL;
	return lkb;
}

int dlm_recover_waiters_post(struct dlm_ls *ls)
{
	struct dlm_lkb *lkb;
	int error = 0;

	/* Deal with lookups and lkb's marked RESEND from _pre.
	   We may now be the master or dir-node for r.  Processing
	   the lkb may result in it being placed back on waiters. */

	while (1) {
		if (!test_bit(LSFL_LS_RUN, &ls->ls_flags)) {
			log_debug(ls, "recover_waiters_post aborted");
			error = -EINTR;
			break;
		}

		lkb = remove_resend_waiter(ls);
		if (!lkb)
			break;

		log_debug(ls, "recover_waiters_post %x type %d flags %x %s",
			  lkb->lkb_id, lkb->lkb_wait_type, lkb->lkb_flags,
			  lkb->lkb_resource->res_name);

		lkb->lkb_flags &= ~DLM_IFL_RESEND;
		lkb->lkb_flags &= ~DLM_IFL_CONVERTING;

		switch (lkb->lkb_wait_type) {

		case DLM_MSG_LOOKUP:
		case DLM_MSG_REQUEST:
			recover_request_lock(lkb);
			break;
		case DLM_MSG_CONVERT:
			recover_convert_lock(lkb);
			break;
		default:
			log_error(ls, "recover_waiters_post wait_type %d",
				  lkb->lkb_wait_type);
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

	log_debug(ls, "purge locks of departed nodes");

	down_write(&ls->ls_root_lock);
	list_for_each_entry(r, &ls->ls_rootres, res_rootlist) {
		DLM_ASSERT(!is_remote(r),);

		hold_rsb(r);
		lock_rsb(r);

		purge_queue(r, &r->res_grantqueue);
		purge_queue(r, &r->res_convertqueue);
		purge_queue(r, &r->res_waitqueue);

		unlock_rsb(r);
		unhold_rsb(r);

		schedule();
	}
	up_write(&ls->ls_root_lock);

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

