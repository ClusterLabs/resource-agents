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
 * lkb.c
 *
 * Allocate and free locks on the lock ID table.
 *
 * This is slightly naff but I don't really like the
 * VMS lockidtbl stuff as it uses a realloced array
 * to hold the locks in. I think this is slightly better
 * in some ways.
 *
 * Any better suggestions gratefully received. Patrick
 *
 */

#include "dlm_internal.h"
#include "lockqueue.h"
#include "lkb.h"
#include "config.h"
#include "rsb.h"
#include "memory.h"
#include "lockspace.h"
#include "util.h"

/* 
 * Internal find lock by ID. Must be called with the lockidtbl spinlock held.
 */

static struct dlm_lkb *__find_lock_by_id(struct dlm_ls *ls, uint32_t lkid)
{
	uint16_t bucket = lkid & 0xFFFF;
	struct dlm_lkb *lkb;

	if (bucket >= ls->ls_lkbtbl_size)
		goto out;

	list_for_each_entry(lkb, &ls->ls_lkbtbl[bucket].list, lkb_idtbl_list){
		if (lkb->lkb_id == lkid)
			return lkb;
	}
 out:
	return NULL;
}

/* 
 * LKB lkid's are 32 bits and have two 16 bit parts.  The bottom 16 bits are a
 * random number between 0 and lockidtbl_size-1.  This random number specifies
 * the "bucket" for the lkb in lockidtbl.  The upper 16 bits are a sequentially
 * assigned per-bucket id.
 *
 * Because the 16 bit id's per bucket can roll over, a new lkid must be checked
 * against the lkid of all lkb's in the bucket to avoid duplication.
 *
 */

struct dlm_lkb *create_lkb(struct dlm_ls *ls)
{
	struct dlm_lkb *lkb;
	uint32_t lkid;
	uint16_t bucket;

	lkb = allocate_lkb(ls);
	if (!lkb)
		goto out;

 retry:
	get_random_bytes(&bucket, sizeof(bucket));
	bucket &= (ls->ls_lkbtbl_size - 1);

	write_lock(&ls->ls_lkbtbl[bucket].lock);

	lkid = bucket | (ls->ls_lkbtbl[bucket].counter++ << 16);

	if (__find_lock_by_id(ls, lkid)) {
		write_unlock(&ls->ls_lkbtbl[bucket].lock);
		goto retry;
	}

	lkb->lkb_id = lkid;
	list_add(&lkb->lkb_idtbl_list, &ls->ls_lkbtbl[bucket].list);
	write_unlock(&ls->ls_lkbtbl[bucket].lock);
 out:
	return lkb;
}

/* 
 * Free LKB and remove it from the lockidtbl.
 * NB - this always frees the lkb whereas release_rsb doesn't free an
 * rsb unless its reference count is zero.
 */

void release_lkb(struct dlm_ls *ls, struct dlm_lkb *lkb)
{
	uint16_t bucket = lkb->lkb_id & 0xFFFF;

	if (lkb->lkb_status) {
		log_error(ls, "release lkb with status %u", lkb->lkb_status);
		print_lkb(lkb);
		return;
	}

	if (lkb->lkb_parent)
		atomic_dec(&lkb->lkb_parent->lkb_childcnt);

	write_lock(&ls->ls_lkbtbl[bucket].lock);
	list_del(&lkb->lkb_idtbl_list);
	write_unlock(&ls->ls_lkbtbl[bucket].lock);

	/* if this is not a master copy then lvbptr points into the user's
	 * lksb, so don't free it */
	if (lkb->lkb_lvbptr && lkb->lkb_flags & GDLM_LKFLG_MSTCPY)
		free_lvb(lkb->lkb_lvbptr);

	if (lkb->lkb_range)
		free_range(lkb->lkb_range);

	free_lkb(lkb);
}

struct dlm_lkb *find_lock_by_id(struct dlm_ls *ls, uint32_t lkid)
{
	struct dlm_lkb *lkb;
	uint16_t bucket = lkid & 0xFFFF;

	read_lock(&ls->ls_lkbtbl[bucket].lock);
	lkb = __find_lock_by_id(ls, lkid);
	read_unlock(&ls->ls_lkbtbl[bucket].lock);

	return lkb;
}

struct dlm_lkb *dlm_get_lkb(void *ls, uint32_t lkid)
{
        struct dlm_ls *lspace = find_lockspace_by_local_id(ls);
	return find_lock_by_id(lspace, lkid);
}

/*
 * Initialise the range parts of an LKB.
 */

int lkb_set_range(struct dlm_ls *lspace, struct dlm_lkb *lkb, uint64_t start, uint64_t end)
{
	int ret = -ENOMEM;

	/*
	 * if this wasn't already a range lock, make it one
	 */
	if (!lkb->lkb_range) {
		lkb->lkb_range = allocate_range(lspace);
		if (!lkb->lkb_range)
			goto out;

		/*
		 * This is needed for conversions that contain ranges where the
		 * original lock didn't but it's harmless for new locks too.
		 */
		lkb->lkb_range[GR_RANGE_START] = 0LL;
		lkb->lkb_range[GR_RANGE_END] = 0xffffffffffffffffULL;
	}

	lkb->lkb_range[RQ_RANGE_START] = start;
	lkb->lkb_range[RQ_RANGE_END] = end;

	ret = 0;

      out:
	return ret;
}
