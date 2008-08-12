#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
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
 * gfs_private_fault -
 * @area:
 * @address:
 * @type:
 *
 * Returns: the page
 */

static int gfs_private_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct gfs_inode *ip = get_v2ip(vma->vm_file->f_mapping->host);
	struct gfs_holder i_gh;
	int error;
	int ret = 0;

	atomic_inc(&ip->i_sbd->sd_ops_vm);

	error = gfs_glock_nq_init(ip->i_gl, LM_ST_SHARED, 0, &i_gh);
	if (error)
		goto out;

	set_bit(GIF_PAGED, &ip->i_flags);

	ret = filemap_fault(vma, vmf);

	if (ret && ret != VM_FAULT_OOM)
		pfault_be_greedy(ip);

	gfs_glock_dq_uninit(&i_gh);
 out:
	return ret;
}

/**
 * alloc_page_backing -
 * @ip:
 * @index:
 *
 * Returns: errno
 */

static int
alloc_page_backing(struct gfs_inode *ip, struct page *page)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	unsigned long index = page->index;
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
 * gfs_sharewrite_fault -
 * @area:
 * @address:
 * @type:
 *
 * Returns: the page
 */

static int gfs_sharewrite_fault(struct vm_area_struct *vma,
				struct vm_fault *vmf)
{
	struct file *file = vma->vm_file;
	struct gfs_inode *ip = get_v2ip(file->f_mapping->host);
	struct gfs_holder i_gh;
	int alloc_required;
	int error;
	int ret = 0;

	atomic_inc(&ip->i_sbd->sd_ops_vm);

	error = gfs_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &i_gh);
	if (error)
		goto out;

	if (gfs_is_jdata(ip))
		goto out_unlock;

	set_bit(GIF_PAGED, &ip->i_flags);
	set_bit(GIF_SW_PAGED, &ip->i_flags);

	error = gfs_write_alloc_required(ip,
					 (u64)vmf->pgoff << PAGE_CACHE_SHIFT,
					 PAGE_CACHE_SIZE, &alloc_required);
	if (error) {
		ret = VM_FAULT_OOM; /* XXX: are these right? */
		goto out_unlock;
	}

	ret = filemap_fault(vma, vmf);
	if (ret & VM_FAULT_ERROR)
		goto out_unlock;

	if (alloc_required) {
		/* XXX: do we need to drop page lock around alloc_page_backing?*/
		error = alloc_page_backing(ip, vmf->page);
		if (error) {
                        /*
                         * VM_FAULT_LOCKED should always be the case for
                         * filemap_fault, but it may not be in a future
                         * implementation.
                         */
			if (ret & VM_FAULT_LOCKED)
				unlock_page(vmf->page);
			page_cache_release(vmf->page);
			ret = VM_FAULT_OOM;
			goto out_unlock;
		}
		set_page_dirty(vmf->page);
	}

	pfault_be_greedy(ip);

 out_unlock:
	gfs_glock_dq_uninit(&i_gh);
 out:
	return ret;
}

struct vm_operations_struct gfs_vm_ops_private = {
	.fault = gfs_private_fault,
};

struct vm_operations_struct gfs_vm_ops_sharewrite = {
	.fault = gfs_sharewrite_fault,
};

