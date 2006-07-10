/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <asm/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/mm.h>
#include <linux/pagemap.h>

#include "gfs.h"
#include "bmap.h"
#include "glock.h"
#include "inode.h"
#include "ops_vm.h"
#include "page.h"
#include "quota.h"
#include "rgrp.h"
#include "trans.h"

/**
 * pfault_be_greedy -
 * @ip:
 *
 */

static void
pfault_be_greedy(struct gfs_inode *ip)
{
	unsigned int time;

	spin_lock(&ip->i_spin);
	time = ip->i_greedy;
	ip->i_last_pfault = jiffies;
	spin_unlock(&ip->i_spin);

	gfs_inode_hold(ip);
	if (gfs_glock_be_greedy(ip->i_gl, time))
		gfs_inode_put(ip);
}

/**
 * gfs_private_nopage -
 * @area:
 * @address:
 * @type:
 *
 * Returns: the page
 */

static struct page *
gfs_private_nopage(struct vm_area_struct *area,
		   unsigned long address, int *type)
{
	struct gfs_inode *ip = get_v2ip(area->vm_file->f_mapping->host);
	struct gfs_holder i_gh;
	struct page *result;
	int error;

	atomic_inc(&ip->i_sbd->sd_ops_vm);

	error = gfs_glock_nq_init(ip->i_gl, LM_ST_SHARED, 0, &i_gh);
	if (error)
		return NULL;

	set_bit(GIF_PAGED, &ip->i_flags);

	result = filemap_nopage(area, address, type);

	if (result && result != NOPAGE_OOM)
		pfault_be_greedy(ip);

	gfs_glock_dq_uninit(&i_gh);

	return result;
}

/**
 * alloc_page_backing -
 * @ip:
 * @index:
 *
 * Returns: errno
 */

static int
alloc_page_backing(struct gfs_inode *ip, unsigned long index)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	uint64_t lblock = index << (PAGE_CACHE_SHIFT - sdp->sd_sb.sb_bsize_shift);
	unsigned int blocks = PAGE_CACHE_SIZE >> sdp->sd_sb.sb_bsize_shift;
	struct gfs_alloc *al;
	unsigned int x;
	int error;

	al = gfs_alloc_get(ip);

	error = gfs_quota_lock_m(ip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
	if (error)
		goto out;

	error = gfs_quota_check(ip, ip->i_di.di_uid, ip->i_di.di_gid);
	if (error)
		goto out_gunlock_q;

	gfs_write_calc_reserv(ip, PAGE_CACHE_SIZE,
			      &al->al_requested_data, &al->al_requested_meta);

	error = gfs_inplace_reserve(ip);
	if (error)
		goto out_gunlock_q;

	/* Trans may require:
	   a dinode block, RG bitmaps to allocate from,
	   indirect blocks, and a quota block */

	error = gfs_trans_begin(sdp,
				1 + al->al_rgd->rd_ri.ri_length +
				al->al_requested_meta, 1);
	if (error)
		goto out_ipres;

	if (gfs_is_stuffed(ip)) {
		error = gfs_unstuff_dinode(ip, gfs_unstuffer_page, NULL);
		if (error)
			goto out_trans;
	}

	for (x = 0; x < blocks; ) {
		uint64_t dblock;
		unsigned int extlen;
		int new = TRUE;

		error = gfs_block_map(ip, lblock, &new, &dblock, &extlen);
		if (error)
			goto out_trans;

		lblock += extlen;
		x += extlen;
	}

	gfs_assert_warn(sdp, al->al_alloced_meta || al->al_alloced_data);

 out_trans:
	gfs_trans_end(sdp);

 out_ipres:
	gfs_inplace_release(ip);

 out_gunlock_q:
	gfs_quota_unlock_m(ip);

 out:
	gfs_alloc_put(ip);

	return error;
}

/**
 * gfs_sharewrite_nopage -
 * @area:
 * @address:
 * @type:
 *
 * Returns: the page
 */

static struct page *
gfs_sharewrite_nopage(struct vm_area_struct *area,
		      unsigned long address, int *type)
{
	struct gfs_inode *ip = get_v2ip(area->vm_file->f_mapping->host);
	struct gfs_holder i_gh;
	struct page *result = NULL;
	unsigned long index = ((address - area->vm_start) >> PAGE_CACHE_SHIFT) + area->vm_pgoff;
	int alloc_required;
	int error;

	atomic_inc(&ip->i_sbd->sd_ops_vm);

	error = gfs_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &i_gh);
	if (error)
		return NULL;

	if (gfs_is_jdata(ip))
		goto out;

	set_bit(GIF_PAGED, &ip->i_flags);
	set_bit(GIF_SW_PAGED, &ip->i_flags);

	error = gfs_write_alloc_required(ip, (uint64_t)index << PAGE_CACHE_SHIFT,
					 PAGE_CACHE_SIZE, &alloc_required);
	if (error)
		goto out;

	result = filemap_nopage(area, address, type);
	if (!result || result == NOPAGE_OOM)
		goto out;

	if (alloc_required) {
		error = alloc_page_backing(ip, index);
		if (error) {
			page_cache_release(result);
			result = NULL;
			goto out;
		}
		set_page_dirty(result);
	}

	pfault_be_greedy(ip);

 out:
	gfs_glock_dq_uninit(&i_gh);

	return result;
}

struct vm_operations_struct gfs_vm_ops_private = {
	.nopage = gfs_private_nopage,
};

struct vm_operations_struct gfs_vm_ops_sharewrite = {
	.nopage = gfs_sharewrite_nopage,
};

