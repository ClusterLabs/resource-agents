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

#include "lock_dlm.h"

#define MIN(a,b) ((a) <= (b)) ? (a) : (b)
#define MAX(a,b) ((a) >= (b)) ? (a) : (b)

#define CREATE    1
#define NO_CREATE 0

#define WAIT      1
#define NO_WAIT   0
#define X_WAIT   -1

#define EX        1
#define NO_EX     0
#define SH        NO_EX

#define HEAD      1

static int local_conflict(dlm_t *dlm, struct dlm_resource *r,
			  struct lm_lockname *name, unsigned long owner,
			  uint64_t start, uint64_t end, int ex);

static int global_conflict(dlm_t *dlm, struct lm_lockname *name,
			   unsigned long owner, uint64_t start, uint64_t end,
			   int ex);

static int lock_resource(struct dlm_resource *r)
{
	dlm_lock_t *lp;
	struct lm_lockname name;
	int error;

	name.ln_type = LM_TYPE_PLOCK_UPDATE;
	name.ln_number = r->name.ln_number;

	error = create_lp(r->dlm, &name, &lp);
	if (error)
		return error;

	set_bit(LFL_NOBAST, &lp->flags);
	set_bit(LFL_INLOCK, &lp->flags);
	lp->req = DLM_LOCK_EX;
	error = do_dlm_lock_sync(lp, NULL);
	if (error) {
		delete_lp(lp);
		lp = NULL;
	}

	r->update = lp;
	return error;
}

static void unlock_resource(struct dlm_resource *r)
{
	do_dlm_unlock_sync(r->update);
	delete_lp(r->update);
}

static struct dlm_resource *search_resource(dlm_t *dlm, struct lm_lockname *name)
{
	struct dlm_resource *r;

	list_for_each_entry(r, &dlm->resources, list) {
		if (lm_name_equal(&r->name, name))
			return r;
	}
	return NULL;
}

static int get_resource(dlm_t *dlm, struct lm_lockname *name, int create,
			struct dlm_resource **res)
{
	struct dlm_resource *r, *r2;
	int error = -ENOMEM;

	down(&dlm->res_lock);
	r = search_resource(dlm, name);
	if (r)
		r->count++;
	up(&dlm->res_lock);

	if (r)
		goto out;

	if (create == NO_CREATE) {
		error = -ENOENT;
		goto fail;
	}

	r = kmalloc(sizeof(struct dlm_resource), GFP_KERNEL);
	if (!r)
		goto fail;

	memset(r, 0, sizeof(struct dlm_resource));
	r->dlm = dlm;
	r->name = *name;
	r->count = 1;
	INIT_LIST_HEAD(&r->locks);
	INIT_LIST_HEAD(&r->async_locks);
	init_MUTEX(&r->sema);
	spin_lock_init(&r->async_spin);

	down(&dlm->res_lock);
	r2 = search_resource(dlm, name);
	if (r2) {
		r2->count++;
		up(&dlm->res_lock);
		kfree(r);
		r = r2;
		goto out;
	}

	list_add_tail(&r->list, &dlm->resources);
	up(&dlm->res_lock);

 out:
	*res = r;
	return 0;
 fail:
	return error;
}

static void put_resource(struct dlm_resource *r)
{
	dlm_t *dlm = r->dlm;

	down(&dlm->res_lock);
	r->count--;
	if (r->count == 0) {
		DLM_ASSERT(list_empty(&r->locks), );
		DLM_ASSERT(list_empty(&r->async_locks), );
		list_del(&r->list);
		kfree(r);
	}
	up(&dlm->res_lock);
}

static inline void hold_resource(struct dlm_resource *r)
{
	down(&r->dlm->res_lock);
	r->count++;
	up(&r->dlm->res_lock);
}

static inline int ranges_overlap(uint64_t start1, uint64_t end1,
				 uint64_t start2, uint64_t end2)
{
	if (end1 < start2 || start1 > end2)
		return FALSE;
	return TRUE;
}

/**
 * overlap_type - returns a value based on the type of overlap
 * @s1 - start of new lock range
 * @e1 - end of new lock range
 * @s2 - start of existing lock range
 * @e2 - end of existing lock range
 *
 */

static int overlap_type(uint64_t s1, uint64_t e1, uint64_t s2, uint64_t e2)
{
	int ret;

	/*
	 * ---r1---
	 * ---r2---
	 */

	if (s1 == s2 && e1 == e2)
		ret = 0;

	/*
	 * --r1--
	 * ---r2---
	 */

	else if (s1 == s2 && e1 < e2)
		ret = 1;

	/*
	 *   --r1--
	 * ---r2---
	 */

	else if (s1 > s2 && e1 == e2)
		ret = 1;

	/*
	 *  --r1--
	 * ---r2---
	 */

	else if (s1 > s2 && e1 < e2)
		ret = 2;

	/*
	 * ---r1---  or  ---r1---  or  ---r1---
	 * --r2--          --r2--       --r2--
	 */

	else if (s1 <= s2 && e1 >= e2)
		ret = 3;

	/*
	 *   ---r1---
	 * ---r2---
	 */

	else if (s1 > s2 && e1 > e2)
		ret = 4;

	/*
	 * ---r1---
	 *   ---r2---
	 */

	else if (s1 < s2 && e1 < e2)
		ret = 4;

	else
		ret = -1;

	return ret;
}

/* shrink the range start2:end2 by the partially overlapping start:end */

static int shrink_range2(uint64_t *start2, uint64_t *end2,
			 uint64_t start, uint64_t end)
{
	int error = 0;

	if (*start2 < start)
		*end2 = start - 1;
	else if (*end2 > end)
		*start2 =  end + 1;
	else
		error = -1;
	return error;
}

static int shrink_range(struct posix_lock *po, uint64_t start, uint64_t end)
{
	return shrink_range2(&po->start, &po->end, start, end);
}

static void put_lock(dlm_lock_t *lp)
{
	struct posix_lock *po = lp->posix;

	po->count--;
	if (po->count == 0) {
		kfree(po);
		delete_lp(lp);
	}
}

static int create_lock(struct dlm_resource *r, unsigned long owner, int ex,
		       uint64_t start, uint64_t end, dlm_lock_t **lpp)
{
	dlm_lock_t *lp;
	struct posix_lock *po;
	int error;

	error = create_lp(r->dlm, &r->name, &lp);
	if (error)
		return error;

	po = kmalloc(sizeof(struct posix_lock), GFP_KERNEL);
	if (!po) {
		kfree(lp);
		return -ENOMEM;
	}
	memset(po, 0, sizeof(struct posix_lock));

	lp->posix = po;
	po->lp = lp;
	po->resource = r;
	po->count = 1;
	po->start = start;
	po->end = end;
	po->owner = owner;
	po->ex = ex;
	list_add_tail(&po->list, &r->locks);

	*lpp = lp;
	return 0;
}

static unsigned int make_flags_posix(dlm_lock_t *lp, int wait)
{
	unsigned int lkf = DLM_LKF_NOORDER;

	if (test_and_clear_bit(LFL_HEADQUE, &lp->flags))
		lkf |= DLM_LKF_HEADQUE;

	if (wait == NO_WAIT || wait == X_WAIT)
		lkf |= DLM_LKF_NOQUEUE;

	if (lp->lksb.sb_lkid != 0)
		lkf |= DLM_LKF_CONVERT;

	return lkf;
}

static void do_range_lock(dlm_lock_t *lp)
{
	struct dlm_range range = { lp->posix->start, lp->posix->end };
	do_dlm_lock(lp, &range);
}

static void request_lock(dlm_lock_t *lp, int wait)
{
	set_bit(LFL_INLOCK, &lp->flags);
	lp->req = lp->posix->ex ? DLM_LOCK_EX : DLM_LOCK_PR;
	lp->lkf = make_flags_posix(lp, wait);

	log_debug("req %x,%"PRIx64" %s %"PRIx64"-%"PRIx64" %x %u w %u",
		  lp->lockname.ln_type, lp->lockname.ln_number,
		  lp->posix->ex ? "ex" : "sh", lp->posix->start,
		  lp->posix->end, lp->lkf, current->pid, wait);

	do_range_lock(lp);
}

static void add_async(struct posix_lock *po, struct dlm_resource *r)
{
	spin_lock(&r->async_spin);
	list_add_tail(&po->async_list, &r->async_locks);
	spin_unlock(&r->async_spin);
}

static void del_async(struct posix_lock *po, struct dlm_resource *r)
{
	spin_lock(&r->async_spin);
	list_del(&po->async_list);
	spin_unlock(&r->async_spin);
}

static int wait_async(dlm_lock_t *lp)
{
	wait_for_completion(&lp->uast_wait);
	del_async(lp->posix, lp->posix->resource);
	return lp->lksb.sb_status;
}

static void wait_async_list(struct dlm_resource *r, unsigned long owner)
{
	struct posix_lock *po;
	int error, found;

 restart:
	found = FALSE;
	spin_lock(&r->async_spin);
	list_for_each_entry(po, &r->async_locks, async_list) {
		if (po->owner != owner)
			continue;
		found = TRUE;
		break;
	}
	spin_unlock(&r->async_spin);

	if (found) {
		DLM_ASSERT(po->lp, );
		error = wait_async(po->lp);
		DLM_ASSERT(!error, );
		goto restart;
	}
}

static void update_lock(dlm_lock_t *lp, int wait)
{
	request_lock(lp, wait);
	add_async(lp->posix, lp->posix->resource);

	if (wait == NO_WAIT || wait == X_WAIT) {
		int error = wait_async(lp);
		DLM_ASSERT(!error, printk("error=%d\n", error););
	}
}

static void add_lock(struct dlm_resource *r, unsigned long owner, int wait,
		     int ex, uint64_t start, uint64_t end, int head)
{
	dlm_lock_t *lp;
	int error;

	error = create_lock(r, owner, ex, start, end, &lp);
	DLM_ASSERT(!error, );
	if (head == HEAD)
		set_bit(LFL_HEADQUE, &lp->flags);

	hold_resource(r);
	update_lock(lp, wait);
}

static int remove_lock(dlm_lock_t *lp)
{
	struct dlm_resource *r = lp->posix->resource;

	log_debug("remove %x,%"PRIx64" %u",
		  r->name.ln_type, r->name.ln_number, current->pid);

	do_dlm_unlock_sync(lp);
	put_lock(lp);
	put_resource(r);
	return 0;
}

/* RN within RE (and starts or ends on RE boundary)
   1. add new lock for non-overlap area of RE, orig mode
   2. convert RE to RN range and mode */

static int lock_case1(struct posix_lock *po, struct dlm_resource *r,
		      unsigned long owner, int wait, int ex, uint64_t start,
		      uint64_t end)
{
	uint64_t start2, end2;

	/* non-overlapping area start2:end2 */
	start2 = po->start;
	end2 = po->end;
	shrink_range2(&start2, &end2, start, end);

	po->start = start;
	po->end = end;
	po->ex = ex;

	if (ex) {
		add_lock(r, owner, X_WAIT, SH, start2, end2, HEAD);
		update_lock(po->lp, wait);
	} else {
		add_lock(r, owner, WAIT, EX, start2, end2, HEAD);
		update_lock(po->lp, X_WAIT);
	}
	return 0;
}

/* RN within RE (RE overlaps RN on both sides)
   1. add new lock for front fragment, orig mode
   2. add new lock for back fragment, orig mode
   3. convert RE to RN range and mode */
			 
static int lock_case2(struct posix_lock *po, struct dlm_resource *r,
		      unsigned long owner, int wait, int ex, uint64_t start,
		      uint64_t end)
{
	if (ex) {
		add_lock(r, owner, X_WAIT, SH, po->start, start-1, HEAD);
		add_lock(r, owner, X_WAIT, SH, end+1, po->end, HEAD);

		po->start = start;
		po->end = end;
		po->ex = ex;

		update_lock(po->lp, wait);
	} else {
		add_lock(r, owner, WAIT, EX, po->start, start-1, HEAD);
		add_lock(r, owner, WAIT, EX, end+1, po->end, HEAD);

		po->start = start;
		po->end = end;
		po->ex = ex;

		update_lock(po->lp, X_WAIT);
	}
	return 0;
}

/* returns ranges from exist list in order of their start values */

static int next_exist(struct list_head *exist, uint64_t *start, uint64_t *end)
{
	struct posix_lock *po;
	int first = TRUE, first_call = FALSE;

	if (!*start && !*end)
		first_call = TRUE;

	list_for_each_entry(po, exist, list) {
		if (!first_call && (po->start <= *start))
			continue;

		if (first) {
			*start = po->start;
			*end = po->end;
			first = FALSE;
		} else if (po->start < *start) {
			*start = po->start;
			*end = po->end;
		}
	}

	return (first ? -1 : 0);
}

/* adds locks in gaps between existing locks from start to end */

static int fill_gaps(struct list_head *exist, struct dlm_resource *r,
		     unsigned long owner, int wait, int ex, uint64_t start,
		     uint64_t end)
{
	uint64_t exist_start = 0, exist_end = 0;

	/* cover gaps in front of each existing lock */
	for (;;) {
		if (next_exist(exist, &exist_start, &exist_end))
			break;
		if (start < exist_start)
			add_lock(r, owner, wait, ex, start, exist_start-1, 0);
		start = exist_end + 1;
	}

	/* cover gap after last existing lock */
	if (exist_end < end)
		add_lock(r, owner, wait, ex, exist_end+1, end, 0);

	return 0;
}

/* RE within RN (possibly more than one RE lock, all within RN) */

static int lock_case3(struct list_head *exist, struct dlm_resource *r,
		      unsigned long owner, int wait, int ex, uint64_t start,
		      uint64_t end)
{
	struct posix_lock *po, *safe;

	fill_gaps(exist, r, owner, wait, ex, start, end);

	if (!ex)
		wait = X_WAIT;

	/* update existing locks to new mode and put back in locks list */
	list_for_each_entry_safe(po, safe, exist, list) {
		list_move_tail(&po->list, &r->locks);
		if (po->ex == ex)
			continue;
		po->ex = ex;
		update_lock(po->lp, wait);
	}

	return 0;
}

/* RE within RN (possibly more than one RE lock, one RE partially overlaps RN)
   1. add new locks with new mode for RN gaps not covered by RE's
   2. convert RE locks' mode to new mode
   other steps deal with the partial-overlap fragment and depend on whether
   the request is sh->ex or ex->sh */

static int lock_case4(struct posix_lock *opo, struct list_head *exist,
		      struct dlm_resource *r, unsigned long owner, int wait,
		      int ex, uint64_t start, uint64_t end)
{
	struct posix_lock *po, *safe;
	uint64_t over_start = 0, over_end = 0;
	uint64_t frag_start = 0, frag_end = 0;

	/* fragment (non-overlap) range of opo */
	if (opo->start < start) {
		frag_start = opo->start;
		frag_end = start - 1;
	} else {
		frag_start = end + 1;
		frag_end = opo->end;
	}

	/* overlap range of opo */
	if (opo->start < start) {
		over_start = start;
		over_end = opo->end;
	} else {
		over_start = opo->start;
		opo->end = end;
	}

	/* cut off the non-overlap portion of opo so fill_gaps will work */
	opo->start = over_start;
	opo->end = over_end;

	fill_gaps(exist, r, owner, wait, ex, start, end);

	/* update existing locks to new mode and put back in locks list */
	list_for_each_entry_safe(po, safe, exist, list) {
		list_move_tail(&po->list, &r->locks);
		if (po == opo)
			continue;
		if (po->ex == ex)
			continue;
		po->ex = ex;
		update_lock(po->lp, wait);
	}

	/* deal with the RE that partially overlaps the requested range */

	if (ex == opo->ex)
		return 0;

	if (ex) {
		/* 1. add a shared lock in the non-overlap range
		   2. convert RE to overlap range and requested mode */

		add_lock(r, owner, X_WAIT, SH, frag_start, frag_end, HEAD);

		opo->start = over_start;
		opo->end = over_end;
		opo->ex = ex;

		update_lock(opo->lp, wait);
	} else {
		/* 1. request a shared lock in the overlap range
		   2. convert RE to non-overlap range
		   3. wait for shared lock to complete */

		add_lock(r, owner, WAIT, SH, over_start, over_end, HEAD);

		opo->start = frag_start;
		opo->end = frag_end;

		update_lock(opo->lp, X_WAIT);
	}

	return 0;
}

/* go through r->locks to find what needs to be done to extend,
   shrink, shift, split, etc existing locks (this often involves adding new
   locks in addition to modifying existing locks. */

static int plock_internal(struct dlm_resource *r, unsigned long owner,
			  int wait, int ex, uint64_t start, uint64_t end)
{
	LIST_HEAD(exist);
	struct posix_lock *po, *safe, *case4_po = NULL;
	int error = 0;

	list_for_each_entry_safe(po, safe, &r->locks, list) {
		if (po->owner != owner)
			continue;
		if (!ranges_overlap(po->start, po->end, start, end))
			continue;

		/* existing range (RE) overlaps new range (RN) */

		switch(overlap_type(start, end, po->start, po->end)) {

		case 0:
			if (po->ex == ex)
				goto out;

			/* ranges the same - just update the existing lock */
			po->ex = ex;
			update_lock(po->lp, wait);
			goto out;

		case 1:
			if (po->ex == ex)
				goto out;

			error = lock_case1(po, r, owner, wait, ex, start, end);
			goto out;

		case 2:
			if (po->ex == ex)
				goto out;

			error = lock_case2(po, r, owner, wait, ex, start, end);
			goto out;

		case 3:
			list_move_tail(&po->list, &exist);
			break;

		case 4:
			DLM_ASSERT(!case4_po, );
			case4_po = po;
			list_move_tail(&po->list, &exist);
			break;

		default:
			error = -1;
			goto out;
		}
	}

	if (case4_po)
		error = lock_case4(case4_po, &exist, r, owner, wait, ex,
				   start, end);
	else if (!list_empty(&exist))
		error = lock_case3(&exist, r, owner, wait, ex, start, end);
	else
		add_lock(r, owner, wait, ex, start, end, 0);

 out:
	return error;
}

static int punlock_internal(struct dlm_resource *r, unsigned long owner,
	  		    uint64_t start, uint64_t end)
{
	struct posix_lock *po, *safe;
	int error = 0;

	list_for_each_entry_safe(po, safe, &r->locks, list) {
		if (po->owner != owner)
			continue;
		if (!ranges_overlap(po->start, po->end, start, end))
			continue;

		/* existing range (RE) overlaps new range (RN) */

		switch(overlap_type(start, end, po->start, po->end)) {

		case 0:
			/* ranges the same - just remove the existing lock */

			list_del(&po->list);
			remove_lock(po->lp);
			goto out;

		case 1:
			/* RN within RE and starts or ends on RE boundary -
			 * shrink and update RE */

			shrink_range(po, start, end);
			update_lock(po->lp, X_WAIT);
			goto out;

		case 2:
			/* RN within RE - shrink and update RE to be front
			 * fragment, and add a new lock for back fragment */

			add_lock(r, owner, po->ex ? WAIT : X_WAIT, po->ex,
				 end+1, po->end, HEAD);

			po->end = start - 1;
			update_lock(po->lp, X_WAIT);
			goto out;

		case 3:
			/* RE within RN - remove RE, then continue checking
			 * because RN could cover other locks */

			list_del(&po->list);
			remove_lock(po->lp);
			continue;

		case 4:
			/* front of RE in RN, or end of RE in RN - shrink and
			 * update RE, then continue because RN could cover
			 * other locks */

			shrink_range(po, start, end);
			update_lock(po->lp, X_WAIT);
			continue;

		default:
			error = -1;
			goto out;
		}
	}

 out:
	return error;
}

int lm_dlm_plock(lm_lockspace_t *lockspace, struct lm_lockname *name,
                 unsigned long owner, int wait, int ex, uint64_t start,
                 uint64_t end)
{
	dlm_t *dlm = (dlm_t *) lockspace;
	struct dlm_resource *r;
	int error;

	log_debug("en plock %u %x,%"PRIx64"", current->pid,
		  name->ln_type, name->ln_number);

	error = get_resource(dlm, name, CREATE, &r);
	if (error)
		goto out;

	error = down_interruptible(&r->sema);
	if (error)
		goto out_put;

	if (!wait && local_conflict(dlm, r, name, owner, start, end, ex)) {
		error = -1;
		goto out_up;
	}

	error = lock_resource(r);
	if (error)
		goto out_up;

	if (!wait && global_conflict(dlm, name, owner, start, end, ex)) {
		error = -2;
		unlock_resource(r);
		goto out_up;
	}

	/* If NO_WAIT all requests should return immediately.
	   If WAIT all requests go on r->async_locks which we wait on in
	   wait_async_locks().  This means DLM should not return -EAGAIN and we
	   should never block waiting for a plock to be released (by a local or
	   remote process) until we call wait_async_list(). */

	error = plock_internal(r, owner, wait, ex, start, end);
	unlock_resource(r);

	/* wait_async_list() must follow the up() because we must be able
	   to punlock a range on this resource while there's a blocked plock
	   request to prevent deadlock between nodes (and processes). */

 out_up:
	up(&r->sema);
	wait_async_list(r, owner);
 out_put:
	put_resource(r);
 out:
	log_debug("ex plock %u error %d", current->pid, error);
	return error;
}

int lm_dlm_punlock(lm_lockspace_t *lockspace, struct lm_lockname *name,
                   unsigned long owner, uint64_t start, uint64_t end)
{
	dlm_t *dlm = (dlm_t *) lockspace;
	struct dlm_resource *r;
	int error;

	log_debug("en punlock %u %x,%"PRIx64"", current->pid,
		  name->ln_type, name->ln_number);

	error = get_resource(dlm, name, NO_CREATE, &r);
	if (error)
		goto out;

	error = down_interruptible(&r->sema);
	if (error)
		goto out_put;

	error = lock_resource(r);
	if (error)
		goto out_up;

	error = punlock_internal(r, owner, start, end);
	unlock_resource(r);

 out_up:
	up(&r->sema);
	wait_async_list(r, owner);
 out_put:
	put_resource(r);
 out:
	log_debug("ex punlock %u error %d", current->pid, error);
	return error;
}

static void query_ast(void *astargs)
{
	dlm_lock_t *lp = (dlm_lock_t *) astargs;;
	complete(&lp->uast_wait);
}

static int get_conflict_global(dlm_t *dlm, struct lm_lockname *name,
			       unsigned long owner, uint64_t *start,
			       uint64_t *end, int *ex, unsigned long *rowner)
{
	dlm_lock_t *lp;
	struct dlm_queryinfo qinfo;
	struct dlm_lockinfo *lki;
	int query = 0, s, error;

	/* acquire a null lock on which to base the query */

	error = create_lp(dlm, name, &lp);
	if (error)
		goto ret;

	lp->req = DLM_LOCK_NL;
	lp->lkf = DLM_LKF_EXPEDITE;
	set_bit(LFL_INLOCK, &lp->flags);
	do_dlm_lock_sync(lp, NULL);

	/* do query, repeating if insufficient space */

	query = DLM_LOCK_THIS | DLM_QUERY_QUEUE_GRANTED |
		DLM_QUERY_LOCKS_HIGHER;

	for (s = 16; s < dlm->max_nodes + 1; s += 16) {

		lki = kmalloc(s * sizeof(struct dlm_lockinfo), GFP_KERNEL);
		if (!lki) {
			error = -ENOMEM;
			goto out;
		}
		memset(lki, 0, s * sizeof(struct dlm_lockinfo));
		memset(&qinfo, 0, sizeof(qinfo));
		qinfo.gqi_locksize = s;
		qinfo.gqi_lockinfo = lki;

		init_completion(&lp->uast_wait);
		error = dlm_query(dlm->gdlm_lsp, &lp->lksb, query, &qinfo,
			   	   query_ast, (void *) lp);
		if (error) {
			kfree(lki);
			goto out;
		}
		wait_for_completion(&lp->uast_wait);
		error = lp->lksb.sb_status;

		if (!error)
			break;
		kfree(lki);
		if (error != -E2BIG)
			goto out;
	}

	/* check query results for blocking locks */

	error = 0;

	for (s = 0; s < qinfo.gqi_lockcount; s++) {

		lki = &qinfo.gqi_lockinfo[s];

		if (!ranges_overlap(*start, *end, lki->lki_grrange.ra_start,
				    lki->lki_grrange.ra_end))
			continue;

		if (lki->lki_node == dlm->our_nodeid)
			continue;

		if (lki->lki_grmode == DLM_LOCK_EX || *ex) {
			*start = lki->lki_grrange.ra_start;
			*end = lki->lki_grrange.ra_end;
			*ex = (lki->lki_grmode == DLM_LOCK_EX) ? 1 : 0;
			*rowner = lki->lki_node;
			error = -EAGAIN;
			break;
		}
	}

	kfree(qinfo.gqi_lockinfo);

	log_debug("global conflict %d %"PRIx64"-%"PRIx64" ex %d own %lu pid %u",
		  error, *start, *end, *ex, *rowner, current->pid);
 out:
	do_dlm_unlock_sync(lp);
	kfree(lp);
 ret:
	return error;
}

static int get_conflict_local(dlm_t *dlm, struct dlm_resource *r,
			      struct lm_lockname *name, unsigned long owner,
			      uint64_t *start, uint64_t *end, int *ex,
			      unsigned long *rowner)
{
	struct posix_lock *po;
	int found = FALSE;

	list_for_each_entry(po, &r->locks, list) {
		if (po->owner == owner)
			continue;
		if (!ranges_overlap(po->start, po->end, *start, *end))
			continue;

		if (*ex || po->ex) {
			*start = po->start;
			*end = po->end;
			*ex = po->ex;
			*rowner = po->owner;
			found = TRUE;
			break;
		}
	}
	return found;
}

int lm_dlm_plock_get(lm_lockspace_t *lockspace, struct lm_lockname *name,
                     unsigned long owner, uint64_t *start, uint64_t *end,
                     int *ex, unsigned long *rowner)
{
	dlm_t *dlm = (dlm_t *) lockspace;
	struct dlm_resource *r;
	int error, found;

	error = get_resource(dlm, name, NO_CREATE, &r);
	if (!error) {
		error = down_interruptible(&r->sema);
		if (error) {
			put_resource(r);
			goto out;
		}

		found = get_conflict_local(dlm, r, name, owner, start, end, ex,
					   rowner);
		up(&r->sema);
		put_resource(r);
		if (found)
			goto out;
	}

	error = get_conflict_global(dlm, name, owner, start, end, ex, rowner);
	if (error == -EAGAIN) {
		log_debug("pl get global conflict %"PRIx64"-%"PRIx64" %d %lu",
			  *start, *end, *ex, *rowner);
		error = 1;
	}
 out:
	return error;
}

static int local_conflict(dlm_t *dlm, struct dlm_resource *r,
			  struct lm_lockname *name, unsigned long owner,
			  uint64_t start, uint64_t end, int ex)
{
	uint64_t get_start = start, get_end = end;
	unsigned long get_owner = 0;
	int get_ex = ex;

	return get_conflict_local(dlm, r, name, owner,
				  &get_start, &get_end, &get_ex, &get_owner);
}

static int global_conflict(dlm_t *dlm, struct lm_lockname *name,
			   unsigned long owner, uint64_t start, uint64_t end,
			   int ex)
{
	uint64_t get_start = start, get_end = end;
	unsigned long get_owner = 0;
	int get_ex = ex;

	return get_conflict_global(dlm, name, owner,
				    &get_start, &get_end, &get_ex, &get_owner);
}
