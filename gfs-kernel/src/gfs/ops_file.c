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

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <asm/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <asm/uaccess.h>
#include <linux/pagemap.h>
#include <linux/uio.h>
#include <linux/blkdev.h>
#include <linux/mm.h>

#include "gfs.h"
#include "bmap.h"
#include "dio.h"
#include "dir.h"
#include "file.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "ioctl.h"
#include "log.h"
#include "ops_file.h"
#include "ops_vm.h"
#include "quota.h"
#include "rgrp.h"
#include "trans.h"

struct filldir_bad_entry {
	char *fbe_name;
	unsigned int fbe_length;
	uint64_t fbe_offset;
	struct gfs_inum fbe_inum;
	unsigned int fbe_type;
};

struct filldir_bad {
	struct gfs_sbd *fdb_sbd;

	struct filldir_bad_entry *fdb_entry;
	unsigned int fdb_entry_num;
	unsigned int fdb_entry_off;

	char *fdb_name;
	unsigned int fdb_name_size;
	unsigned int fdb_name_off;
};

struct filldir_reg {
	struct gfs_sbd *fdr_sbd;
	int fdr_prefetch;

	filldir_t fdr_filldir;
	void *fdr_opaque;
};

typedef ssize_t(*do_rw_t) (struct file * file,
			   char *buf,
			   size_t size, loff_t * offset,
			   unsigned int num_gh, struct gfs_holder * ghs);

/**
 * gfs_llseek - seek to a location in a file
 * @file: the file
 * @offset: the offset
 * @origin: Where to seek from (SEEK_SET, SEEK_CUR, or SEEK_END)
 *
 * SEEK_END requires the glock for the file because it references the
 * file's size.
 *
 * Returns: The new offset, or -EXXX on error
 */

static loff_t
gfs_llseek(struct file *file, loff_t offset, int origin)
{
	struct gfs_inode *ip = vn2ip(file->f_mapping->host);
	struct gfs_holder i_gh;
	loff_t error;

	atomic_inc(&ip->i_sbd->sd_ops_file);

	if (origin == 2) {
		error = gfs_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY, &i_gh);
		if (!error) {
			error = remote_llseek(file, offset, origin);
			gfs_glock_dq_uninit(&i_gh);
		}
	} else
		error = remote_llseek(file, offset, origin);

	return error;
}

#define vma2state(vma) \
((((vma)->vm_flags & (VM_MAYWRITE | VM_MAYSHARE)) == \
 (VM_MAYWRITE | VM_MAYSHARE)) ? \
 LM_ST_EXCLUSIVE : LM_ST_SHARED) \

/**
 * functionname - summary
 * @param1: description
 * @param2: description
 * @param3: description
 *
 * Function description
 *
 * Returns: what is returned
 */

static ssize_t
walk_vm_hard(struct file *file, char *buf, size_t size, loff_t *offset,
	     do_rw_t operation)
{
	struct gfs_holder *ghs;
	unsigned int num_gh = 0;
	ssize_t count;

	{
		struct super_block *sb = file->f_dentry->d_inode->i_sb;
		struct mm_struct *mm = current->mm;
		struct vm_area_struct *vma;
		unsigned long start = (unsigned long)buf;
		unsigned long end = start + size;
		int dumping = (current->flags & PF_DUMPCORE);
		unsigned int x = 0;

		for (vma = find_vma(mm, start); vma; vma = vma->vm_next) {
			if (end <= vma->vm_start)
				break;
			if (vma->vm_file &&
			    vma->vm_file->f_dentry->d_inode->i_sb == sb) {
				num_gh++;
			}
		}

		ghs = kmalloc((num_gh + 1) * sizeof(struct gfs_holder), GFP_KERNEL);
		if (!ghs) {
			if (!dumping)
				up_read(&mm->mmap_sem);
			return -ENOMEM;
		}

		for (vma = find_vma(mm, start); vma; vma = vma->vm_next) {
			if (end <= vma->vm_start)
				break;
			if (vma->vm_file) {
				struct inode *inode = vma->vm_file->f_dentry->d_inode;
				if (inode->i_sb == sb)
					gfs_holder_init(vn2ip(inode)->i_gl,
							vma2state(vma),
							0, &ghs[x++]);
			}
		}

		if (!dumping)
			up_read(&mm->mmap_sem);

		GFS_ASSERT_SBD(x == num_gh, vfs2sdp(sb),);
	}

	count = operation(file, buf, size, offset, num_gh, ghs);

	while (num_gh--)
		gfs_holder_uninit(&ghs[num_gh]);
	kfree(ghs);

	return count;
}

/**
 * walk_vma - Walk the vmas associated with a buffer for read or write.
 *    If any of them are gfs, pass the gfs inode down to the read/write
 *    worker function so that locks can be acquired in the correct order.
 * @file: The file to read/write from/to
 * @buf: The buffer to copy to/from
 * @size: The amount of data requested
 * @offset: The current file offset
 * @operation: The read or write worker function
 *
 * Outputs: Offset - updated according to number of bytes written
 *
 * Returns: The number of bytes written, -errno on failure
 */

static ssize_t
walk_vm(struct file *file, char *buf, size_t size, loff_t *offset,
	do_rw_t operation)
{
	if (current->mm) {
		struct super_block *sb = file->f_dentry->d_inode->i_sb;
		struct mm_struct *mm = current->mm;
		struct vm_area_struct *vma;
		unsigned long start = (unsigned long)buf;
		unsigned long end = start + size;
		int dumping = (current->flags & PF_DUMPCORE);

		if (!dumping)
			down_read(&mm->mmap_sem);

		for (vma = find_vma(mm, start); vma; vma = vma->vm_next) {
			if (end <= vma->vm_start)
				break;
			if (vma->vm_file &&
			    vma->vm_file->f_dentry->d_inode->i_sb == sb)
				goto do_locks;
		}

		if (!dumping)
			up_read(&mm->mmap_sem);
	}

	{
		struct gfs_holder gh;
		return operation(file, buf, size, offset, 0, &gh);
	}

 do_locks:
	return walk_vm_hard(file, buf, size, offset, operation);
}

/**
 * functionname - summary
 * @param1: description
 * @param2: description
 * @param3: description
 *
 * Function description
 *
 * Returns: what is returned
 */

static ssize_t
do_read_readi(struct file *file, char *buf, size_t size, loff_t *offset)
{
	struct gfs_inode *ip = vn2ip(file->f_mapping->host);
	ssize_t count = 0;

	if (*offset < 0)
		return -EINVAL;
	if (!access_ok(VERIFY_WRITE, buf, size))
		return -EFAULT;

	if (!(file->f_flags & O_LARGEFILE)) {
		if (*offset >= 0x7FFFFFFFull)
			return -EFBIG;
		if (*offset + size > 0x7FFFFFFFull)
			size = 0x7FFFFFFFull - *offset;
	}

	count = gfs_readi(ip, buf, *offset, size, gfs_copy2user);

	if (count > 0)
		*offset += count;

	return count;
}

/**
 * do_read_direct - Read bytes from a file
 * @file: The file to read from
 * @buf: The buffer to copy into
 * @size: The amount of data requested
 * @offset: The current file offset
 * @num_gh: The number of other locks we need to do the read
 * @ghs: the locks we need plus one for our lock
 *
 * Outputs: Offset - updated according to number of bytes read
 *
 * Returns: The number of bytes read, -EXXX on failure
 */

static ssize_t
do_read_direct(struct file *file, char *buf, size_t size, loff_t *offset,
	       unsigned int num_gh, struct gfs_holder *ghs)
{
	struct inode *inode = file->f_mapping->host;
	struct gfs_inode *ip = vn2ip(inode);
	unsigned int state = LM_ST_DEFERRED;
	int flags = 0;
	unsigned int x;
	ssize_t count = 0;
	int error;

	for (x = 0; x < num_gh; x++)
		if (ghs[x].gh_gl == ip->i_gl) {
			state = LM_ST_SHARED;
			flags |= GL_LOCAL_EXCL;
			break;
		}

	gfs_holder_init(ip->i_gl, state, flags, &ghs[num_gh]);

	error = gfs_glock_nq_m(num_gh + 1, ghs);
	if (error)
		goto out;

	error = -EINVAL;
	if (gfs_is_jdata(ip))
		goto out_gunlock;

	if (gfs_is_stuffed(ip)) {
		size_t mask = bdev_hardsect_size(inode->i_sb->s_bdev) - 1;

		if (((*offset) & mask) || (((unsigned long)buf) & mask))
			goto out_gunlock;

		count = do_read_readi(file, buf, size & ~mask, offset);
	}
	else
		count = generic_file_read(file, buf, size, offset);

	error = 0;

 out_gunlock:
	gfs_glock_dq_m(num_gh + 1, ghs);

 out:
	gfs_holder_uninit(&ghs[num_gh]);

	return (count) ? count : error;
}

/**
 * do_read_buf - Read bytes from a file
 * @file: The file to read from
 * @buf: The buffer to copy into
 * @size: The amount of data requested
 * @offset: The current file offset
 * @num_gh: The number of other locks we need to do the read
 * @ghs: the locks we need plus one for our lock
 *
 * Outputs: Offset - updated according to number of bytes read
 *
 * Returns: The number of bytes read, -EXXX on failure
 */

static ssize_t
do_read_buf(struct file *file, char *buf, size_t size, loff_t *offset,
	    unsigned int num_gh, struct gfs_holder *ghs)
{
	struct gfs_inode *ip = vn2ip(file->f_mapping->host);
	ssize_t count = 0;
	int error;

	gfs_holder_init(ip->i_gl, LM_ST_SHARED, GL_ATIME, &ghs[num_gh]);

	error = gfs_glock_nq_m_atime(num_gh + 1, ghs);
	if (error)
		goto out;

	if (gfs_is_jdata(ip) ||
	    (gfs_is_stuffed(ip) && !test_bit(GIF_PAGED, &ip->i_flags)))
		count = do_read_readi(file, buf, size, offset);
	else
		count = generic_file_read(file, buf, size, offset);

	gfs_glock_dq_m(num_gh + 1, ghs);

 out:
	gfs_holder_uninit(&ghs[num_gh]);

	return (count) ? count : error;
}

/**
 * gfs_read - Read bytes from a file
 * @file: The file to read from
 * @buf: The buffer to copy into
 * @size: The amount of data requested
 * @offset: The current file offset
 *
 * Outputs: Offset - updated according to number of bytes read
 *
 * Returns: The number of bytes read, -EXXX on failure
 */

static ssize_t
gfs_read(struct file *file, char *buf, size_t size, loff_t *offset)
{
	atomic_inc(&vfs2sdp(file->f_mapping->host->i_sb)->sd_ops_file);

	if (file->f_flags & O_DIRECT)
		return walk_vm(file, buf, size, offset, do_read_direct);
	else
		return walk_vm(file, buf, size, offset, do_read_buf);
}

/**
 * grope_mapping - feel up a mapping that needs to be written
 * @buf: the start of the memory to be written
 * @size: the size of the memory to be written
 *
 * We do this after acquiring the locks on the mapping,
 * but before starting the write transaction.  We need to make
 * sure that we don't cause recursive transactions if blocks
 * need to be allocated to the file backing the mapping.
 *
 * Returns:  0 on success, -EXXX on failure
 */

static int
grope_mapping(char *buf, size_t size)
{
	unsigned long start = (unsigned long)buf;
	unsigned long stop = start + size;
	char c;

	while (start < stop) {
		if (copy_from_user(&c, (char *)start, 1))
			return -EFAULT;

		start += PAGE_CACHE_SIZE;
		start &= PAGE_CACHE_MASK;
	}

	return 0;
}

/**
 * do_write_direct_alloc - Write bytes to a file
 * @file: The file to write to
 * @buf: The buffer to copy from
 * @size: The amount of data requested
 * @offset: The current file offset
 *
 * Outputs: Offset - updated according to number of bytes written
 *
 * Returns: The number of bytes written, -EXXX on failure
 */

static ssize_t
do_write_direct_alloc(struct file *file, char *buf, size_t size, loff_t *offset)
{
	struct inode *inode = file->f_mapping->host;
	struct gfs_inode *ip = vn2ip(inode);
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_alloc *al = NULL;
	struct iovec local_iov = { .iov_base = buf, .iov_len = size };
	struct buffer_head *dibh;
	unsigned int data_blocks, ind_blocks;
	ssize_t count;
	int error;

	gfs_write_calc_reserv(ip, size, &data_blocks, &ind_blocks);

	al = gfs_alloc_get(ip);

	error = gfs_quota_lock_m(ip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
	if (error)
		goto fail;

	error = gfs_quota_check(ip, ip->i_di.di_uid, ip->i_di.di_gid);
	if (error)
		goto fail_gunlock_q;

	al->al_requested_meta = ind_blocks;
	al->al_requested_data = data_blocks;

	error = gfs_inplace_reserve(ip);
	if (error)
		goto fail_gunlock_q;

	/* Trans may require:
	   All blocks for a RG bitmap, whatever indirect blocks we
	   need, a modified dinode, and a quota change. */

	error = gfs_trans_begin(sdp,
				1 + al->al_rgd->rd_ri.ri_length + ind_blocks,
				1);
	if (error)
		goto fail_ipres;

	if ((ip->i_di.di_mode & (S_ISUID | S_ISGID)) && !capable(CAP_FSETID)) {
		error = gfs_get_inode_buffer(ip, &dibh);
		if (error)
			goto fail_end_trans;

		ip->i_di.di_mode &= (ip->i_di.di_mode & S_IXGRP) ? (~(S_ISUID | S_ISGID)) : (~S_ISUID);

		gfs_trans_add_bh(ip->i_gl, dibh);
		gfs_dinode_out(&ip->i_di, dibh->b_data);
		brelse(dibh);
	}

	if (gfs_is_stuffed(ip)) {
		error = gfs_unstuff_dinode(ip, gfs_unstuffer_sync, NULL);
		if (error)
			goto fail_end_trans;
	}

	count = generic_file_write_nolock(file, &local_iov, 1, offset);
	if (count < 0) {
		error = count;
		goto fail_end_trans;
	}

	error = gfs_get_inode_buffer(ip, &dibh);
	if (error)
		goto fail_end_trans;

	if (ip->i_di.di_size < inode->i_size)
		ip->i_di.di_size = inode->i_size;
	ip->i_di.di_mtime = ip->i_di.di_ctime = get_seconds();

	gfs_trans_add_bh(ip->i_gl, dibh);
	gfs_dinode_out(&ip->i_di, dibh->b_data);
	brelse(dibh);

	gfs_trans_end(sdp);

	if (file->f_flags & O_SYNC)
		gfs_log_flush_glock(ip->i_gl);

	gfs_inplace_release(ip);
	gfs_quota_unlock_m(ip);
	gfs_alloc_put(ip);

	return count;

 fail_end_trans:
	gfs_trans_end(sdp);

 fail_ipres:
	gfs_inplace_release(ip);

 fail_gunlock_q:
	gfs_quota_unlock_m(ip);

 fail:
	gfs_alloc_put(ip);

	return error;
}

/**
 * do_write_direct - Write bytes to a file
 * @file: The file to write to
 * @buf: The buffer to copy from
 * @size: The amount of data requested
 * @offset: The current file offset
 * @num_gh: The number of other locks we need to do the read
 * @gh: the locks we need plus one for our lock
 *
 * Outputs: Offset - updated according to number of bytes written
 *
 * Returns: The number of bytes written, -EXXX on failure
 */

static ssize_t
do_write_direct(struct file *file, char *buf, size_t size, loff_t *offset,
		unsigned int num_gh, struct gfs_holder *ghs)
{
	struct gfs_inode *ip = vn2ip(file->f_mapping->host);
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_file *fp = vf2fp(file);
	unsigned int state = LM_ST_DEFERRED;
	int alloc_required;
	unsigned int x;
	size_t s;
	ssize_t count = 0;
	int error;

	if (test_bit(GFF_DID_DIRECT_ALLOC, &fp->f_flags))
		state = LM_ST_EXCLUSIVE;
	else
		for (x = 0; x < num_gh; x++)
			if (ghs[x].gh_gl == ip->i_gl) {
				state = LM_ST_EXCLUSIVE;
				break;
			}

 restart:
	gfs_holder_init(ip->i_gl, state, 0, &ghs[num_gh]);

	error = gfs_glock_nq_m(num_gh + 1, ghs);
	if (error)
		goto out;

	error = -EINVAL;
	if (gfs_is_jdata(ip))
		goto out_gunlock;

	if (num_gh) {
		error = grope_mapping(buf, size);
		if (error)
			goto out_gunlock;
	}

	if (file->f_flags & O_APPEND)
		*offset = ip->i_di.di_size;

	if (!(file->f_flags & O_LARGEFILE)) {
		error = -EFBIG;
		if (*offset >= 0x7FFFFFFFull)
			goto out_gunlock;
		if (*offset + size > 0x7FFFFFFFull)
			size = 0x7FFFFFFFull - *offset;
	}

	if (gfs_is_stuffed(ip) ||
	    *offset + size > ip->i_di.di_size ||
	    ((ip->i_di.di_mode & (S_ISUID | S_ISGID)) && !capable(CAP_FSETID)))
		alloc_required = TRUE;
	else {
		error = gfs_write_alloc_required(ip, *offset, size,
						 &alloc_required);
		if (error)
			goto out_gunlock;
	}

	if (alloc_required && state != LM_ST_EXCLUSIVE) {
		gfs_glock_dq_m(num_gh + 1, ghs);
		gfs_holder_uninit(&ghs[num_gh]);
		state = LM_ST_EXCLUSIVE;
		goto restart;
	}

	if (alloc_required) {
		set_bit(GFF_DID_DIRECT_ALLOC, &fp->f_flags);

		while (size) {
			s = sdp->sd_tune.gt_max_atomic_write;
			if (s > size)
				s = size;

			error = do_write_direct_alloc(file, buf, s, offset);
			if (error < 0)
				goto out_gunlock;

			buf += error;
			size -= error;
			count += error;
		}
	} else {
		struct iovec local_iov = { .iov_base = buf, .iov_len = size };
		struct gfs_holder t_gh;

		clear_bit(GFF_DID_DIRECT_ALLOC, &fp->f_flags);

		error = gfs_glock_nq_init(sdp->sd_trans_gl, LM_ST_SHARED, 0, &t_gh);
		if (error)
			goto out_gunlock;

		count = generic_file_write_nolock(file, &local_iov, 1, offset);

		gfs_glock_dq_uninit(&t_gh);
	}

	error = 0;

      out_gunlock:
	gfs_glock_dq_m(num_gh + 1, ghs);

      out:
	gfs_holder_uninit(&ghs[num_gh]);

	return (count) ? count : error;
}

/**
 * do_do_write_buf - Write bytes to a file
 * @file: The file to write to
 * @buf: The buffer to copy from
 * @size: The amount of data requested
 * @offset: The current file offset
 *
 * Outputs: Offset - updated according to number of bytes written
 *
 * Returns: The number of bytes written, -EXXX on failure
 */

static ssize_t
do_do_write_buf(struct file *file, char *buf, size_t size, loff_t *offset)
{
	struct inode *inode = file->f_mapping->host;
	struct gfs_inode *ip = vn2ip(inode);
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_alloc *al = NULL;
	struct buffer_head *dibh;
	unsigned int data_blocks, ind_blocks;
	int alloc_required, journaled;
	ssize_t count;
	int error;

	journaled = gfs_is_jdata(ip);

	gfs_write_calc_reserv(ip, size, &data_blocks, &ind_blocks);

	error = gfs_write_alloc_required(ip, *offset, size, &alloc_required);
	if (error)
		return error;

	if (alloc_required) {
		al = gfs_alloc_get(ip);

		error = gfs_quota_lock_m(ip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
		if (error)
			goto fail;

		error = gfs_quota_check(ip, ip->i_di.di_uid, ip->i_di.di_gid);
		if (error)
			goto fail_gunlock_q;

		if (journaled)
			al->al_requested_meta = ind_blocks + data_blocks;
		else {
			al->al_requested_meta = ind_blocks;
			al->al_requested_data = data_blocks;
		}

		error = gfs_inplace_reserve(ip);
		if (error)
			goto fail_gunlock_q;

		/* Trans may require:
		   All blocks for a RG bitmap, whatever indirect blocks we
		   need, a modified dinode, and a quota change. */

		error = gfs_trans_begin(sdp,
					1 + al->al_rgd->rd_ri.ri_length +
					ind_blocks +
					((journaled) ? data_blocks : 0), 1);
		if (error)
			goto fail_ipres;
	} else {
		/* Trans may require:
		   A modified dinode. */

		error = gfs_trans_begin(sdp,
					1 + ((journaled) ? data_blocks : 0), 0);
		if (error)
			goto fail_ipres;
	}

	if ((ip->i_di.di_mode & (S_ISUID | S_ISGID)) && !capable(CAP_FSETID)) {
		error = gfs_get_inode_buffer(ip, &dibh);
		if (error)
			goto fail_end_trans;

		ip->i_di.di_mode &= (ip->i_di.di_mode & S_IXGRP) ? (~(S_ISUID | S_ISGID)) : (~S_ISUID);

		gfs_trans_add_bh(ip->i_gl, dibh);
		gfs_dinode_out(&ip->i_di, dibh->b_data);
		brelse(dibh);
	}

	if (journaled ||
	    (gfs_is_stuffed(ip) && !test_bit(GIF_PAGED, &ip->i_flags) &&
	     *offset + size <= sdp->sd_sb.sb_bsize - sizeof(struct gfs_dinode))) {

		count = gfs_writei(ip, buf, *offset, size, gfs_copy_from_user);
		if (count < 0) {
			error = count;
			goto fail_end_trans;
		}

		*offset += count;
	} else {
		struct iovec local_iov = { .iov_base = buf, .iov_len = size };

		count = generic_file_write_nolock(file, &local_iov, 1, offset);
		if (count < 0) {
			error = count;
			goto fail_end_trans;
		}

		error = gfs_get_inode_buffer(ip, &dibh);
		if (error)
			goto fail_end_trans;

		if (ip->i_di.di_size < inode->i_size)
			ip->i_di.di_size = inode->i_size;
		ip->i_di.di_mtime = ip->i_di.di_ctime = get_seconds();

		gfs_trans_add_bh(ip->i_gl, dibh);
		gfs_dinode_out(&ip->i_di, dibh->b_data);
		brelse(dibh);
	}

	gfs_trans_end(sdp);

	if (file->f_flags & O_SYNC)
		gfs_log_flush_glock(ip->i_gl);

	if (alloc_required) {
		GFS_ASSERT_INODE(count != size ||
				 al->al_alloced_meta ||
				 al->al_alloced_data, ip,);
		gfs_inplace_release(ip);
		gfs_quota_unlock_m(ip);
		gfs_alloc_put(ip);
	}

	return count;

 fail_end_trans:
	gfs_trans_end(sdp);

 fail_ipres:
	if (alloc_required)
		gfs_inplace_release(ip);

 fail_gunlock_q:
	if (alloc_required)
		gfs_quota_unlock_m(ip);

 fail:
	if (alloc_required)
		gfs_alloc_put(ip);

	return error;
}

/**
 * do_write_buf - Write bytes to a file
 * @file: The file to write to
 * @buf: The buffer to copy from
 * @size: The amount of data requested
 * @offset: The current file offset
 * @num_gh: The number of other locks we need to do the read
 * @gh: the locks we need plus one for our lock
 *
 * Outputs: Offset - updated according to number of bytes written
 *
 * Returns: The number of bytes written, -EXXX on failure
 */

static ssize_t
do_write_buf(struct file *file,
	     char *buf, size_t size, loff_t *offset,
	     unsigned int num_gh, struct gfs_holder *ghs)
{
	struct gfs_inode *ip = vn2ip(file->f_mapping->host);
	struct gfs_sbd *sdp = ip->i_sbd;
	size_t s;
	ssize_t count = 0;
	int error;

	gfs_holder_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &ghs[num_gh]);

	error = gfs_glock_nq_m(num_gh + 1, ghs);
	if (error)
		goto out;

	if (num_gh) {
		error = grope_mapping(buf, size);
		if (error)
			goto out_gunlock;
	}

	if (file->f_flags & O_APPEND)
		*offset = ip->i_di.di_size;

	if (!(file->f_flags & O_LARGEFILE)) {
		error = -EFBIG;
		if (*offset >= 0x7FFFFFFFull)
			goto out_gunlock;
		if (*offset + size > 0x7FFFFFFFull)
			size = 0x7FFFFFFFull - *offset;
	}

	while (size) {
		s = sdp->sd_tune.gt_max_atomic_write;
		if (s > size)
			s = size;

		error = do_do_write_buf(file, buf, s, offset);
		if (error < 0)
			goto out_gunlock;

		buf += error;
		size -= error;
		count += error;
	}

	error = 0;

 out_gunlock:
	gfs_glock_dq_m(num_gh + 1, ghs);

 out:
	gfs_holder_uninit(&ghs[num_gh]);

	return (count) ? count : error;
}

/**
 * gfs_write - Write bytes to a file
 * @file: The file to write to
 * @buf: The buffer to copy from
 * @size: The amount of data requested
 * @offset: The current file offset
 *
 * Outputs: Offset - updated according to number of bytes written
 *
 * Returns: The number of bytes written, -EXXX on failure
 */

static ssize_t
gfs_write(struct file *file, const char *buf, size_t size, loff_t *offset)
{
	struct inode *inode = file->f_mapping->host;
	ssize_t count;

	atomic_inc(&vfs2sdp(inode->i_sb)->sd_ops_file);

	if (*offset < 0)
		return -EINVAL;
	if (!access_ok(VERIFY_READ, buf, size))
		return -EFAULT;

	down(&inode->i_sem);
	if (file->f_flags & O_DIRECT)
		count = walk_vm(file, (char *)buf, size, offset, do_write_direct);
	else
		count = walk_vm(file, (char *)buf, size, offset, do_write_buf);
	up(&inode->i_sem);

	return count;
}

/**
 * filldir_reg_func - Report a directory entry to the caller of gfs_dir_read()
 * @opaque: opaque data used by the function
 * @name: the name of the directory entry
 * @length: the length of the name
 * @offset: the entry's offset in the directory
 * @inum: the inode number the entry points to
 * @type: the type of inode the entry points to
 *
 * Returns: 0 on success, 1 if buffer full
 */

static int
filldir_reg_func(void *opaque,
		 const char *name, unsigned int length,
		 uint64_t offset,
		 struct gfs_inum *inum, unsigned int type)
{
	struct filldir_reg *fdr = (struct filldir_reg *)opaque;
	struct gfs_sbd *sdp = fdr->fdr_sbd;
	unsigned int vfs_type;
	int error;

	switch (type) {
	case GFS_FILE_NON:
		vfs_type = DT_UNKNOWN;
		break;
	case GFS_FILE_REG:
		vfs_type = DT_REG;
		break;
	case GFS_FILE_DIR:
		vfs_type = DT_DIR;
		break;
	case GFS_FILE_LNK:
		vfs_type = DT_LNK;
		break;
	case GFS_FILE_BLK:
		vfs_type = DT_BLK;
		break;
	case GFS_FILE_CHR:
		vfs_type = DT_CHR;
		break;
	case GFS_FILE_FIFO:
		vfs_type = DT_FIFO;
		break;
	case GFS_FILE_SOCK:
		vfs_type = DT_SOCK;
		break;
	default:
		GFS_ASSERT_SBD(FALSE, sdp,
			       printk("type = %u\n", type););
	}

	error = fdr->fdr_filldir(fdr->fdr_opaque, name, length, offset,
				 inum->no_formal_ino, vfs_type);
	if (error)
		return 1;

	if (fdr->fdr_prefetch && !(length == 1 && *name == '.')) {
		gfs_glock_prefetch_num(sdp,
				       inum->no_formal_ino, &gfs_inode_glops,
				       LM_ST_SHARED, LM_FLAG_TRY | LM_FLAG_ANY);
		gfs_glock_prefetch_num(sdp,
				       inum->no_addr, &gfs_iopen_glops,
				       LM_ST_SHARED, LM_FLAG_TRY);
	}

	return 0;
}

/**
 * readdir_reg - Read directory entries from a directory
 * @file: The directory to read from
 * @dirent: Buffer for dirents
 * @filldir: Function used to do the copying
 *
 * Returns: 0 on success, -EXXXX on failure
 */

static int
readdir_reg(struct file *file, void *dirent, filldir_t filldir)
{
	struct gfs_inode *dip = vn2ip(file->f_mapping->host);
	struct filldir_reg fdr;
	struct gfs_holder d_gh;
	uint64_t offset = file->f_pos;
	int error;

	fdr.fdr_sbd = dip->i_sbd;
	fdr.fdr_prefetch = TRUE;
	fdr.fdr_filldir = filldir;
	fdr.fdr_opaque = dirent;

	gfs_holder_init(dip->i_gl, LM_ST_SHARED, GL_ATIME, &d_gh);
	error = gfs_glock_nq_atime(&d_gh);
	if (error) {
		gfs_holder_uninit(&d_gh);
		return error;
	}

	error = gfs_dir_read(dip, &offset, &fdr, filldir_reg_func);

	gfs_glock_dq_uninit(&d_gh);

	file->f_pos = offset;

	return error;
}

/**
 * filldir_bad_func - Report a directory entry to the caller of gfs_dir_read()
 * @opaque: opaque data used by the function
 * @name: the name of the directory entry
 * @length: the length of the name
 * @offset: the entry's offset in the directory
 * @inum: the inode number the entry points to
 * @type: the type of inode the entry points to
 *
 * Returns: 0 on success, 1 if buffer full
 */

static int
filldir_bad_func(void *opaque,
		 const char *name, unsigned int length,
		 uint64_t offset,
		 struct gfs_inum *inum, unsigned int type)
{
	struct filldir_bad *fdb = (struct filldir_bad *)opaque;
	struct gfs_sbd *sdp = fdb->fdb_sbd;
	struct filldir_bad_entry *fbe;

	if (fdb->fdb_entry_off == fdb->fdb_entry_num ||
	    fdb->fdb_name_off + length > fdb->fdb_name_size)
		return 1;

	fbe = &fdb->fdb_entry[fdb->fdb_entry_off];
	fbe->fbe_name = fdb->fdb_name + fdb->fdb_name_off;
	memcpy(fbe->fbe_name, name, length);
	fbe->fbe_length = length;
	fbe->fbe_offset = offset;
	fbe->fbe_inum = *inum;
	fbe->fbe_type = type;

	fdb->fdb_entry_off++;
	fdb->fdb_name_off += length;

	if (!(length == 1 && *name == '.')) {
		gfs_glock_prefetch_num(sdp,
				       inum->no_formal_ino, &gfs_inode_glops,
				       LM_ST_SHARED, LM_FLAG_TRY | LM_FLAG_ANY);
		gfs_glock_prefetch_num(sdp,
				       inum->no_addr, &gfs_iopen_glops,
				       LM_ST_SHARED, LM_FLAG_TRY);
	}

	return 0;
}

/**
 * readdir_bad - Read directory entries from a directory
 * @file: The directory to read from
 * @dirent: Buffer for dirents
 * @filldir: Function used to do the copying
 *
 * Returns: 0 on success, -EXXXX on failure
 */

static int
readdir_bad(struct file *file, void *dirent, filldir_t filldir)
{
	struct gfs_inode *dip = vn2ip(file->f_mapping->host);
	struct gfs_sbd *sdp = dip->i_sbd;
	struct filldir_reg fdr;
	unsigned int entries, size;
	struct filldir_bad *fdb;
	struct gfs_holder d_gh;
	uint64_t offset = file->f_pos;
	unsigned int x;
	struct filldir_bad_entry *fbe;
	int error;

	entries = sdp->sd_tune.gt_entries_per_readdir;
	size = sizeof(struct filldir_bad) +
	    entries * (sizeof(struct filldir_bad_entry) + GFS_FAST_NAME_SIZE);

	fdb = gmalloc(size);
	memset(fdb, 0, size);

	fdb->fdb_sbd = sdp;
	fdb->fdb_entry = (struct filldir_bad_entry *)(fdb + 1);
	fdb->fdb_entry_num = entries;
	fdb->fdb_name = ((char *)fdb) + sizeof(struct filldir_bad) +
		entries * sizeof(struct filldir_bad_entry);
	fdb->fdb_name_size = entries * GFS_FAST_NAME_SIZE;

	gfs_holder_init(dip->i_gl, LM_ST_SHARED, GL_ATIME, &d_gh);
	error = gfs_glock_nq_atime(&d_gh);
	if (error) {
		gfs_holder_uninit(&d_gh);
		goto out;
	}

	error = gfs_dir_read(dip, &offset, fdb, filldir_bad_func);

	gfs_glock_dq_uninit(&d_gh);

	fdr.fdr_sbd = sdp;
	fdr.fdr_prefetch = FALSE;
	fdr.fdr_filldir = filldir;
	fdr.fdr_opaque = dirent;

	for (x = 0; x < fdb->fdb_entry_off; x++) {
		fbe = &fdb->fdb_entry[x];

		error = filldir_reg_func(&fdr,
					 fbe->fbe_name, fbe->fbe_length,
					 fbe->fbe_offset,
					 &fbe->fbe_inum, fbe->fbe_type);
		if (error) {
			file->f_pos = fbe->fbe_offset;
			error = 0;
			goto out;
		}
	}

	file->f_pos = offset;

 out:
	kfree(fdb);

	return error;
}

/**
 * gfs_readdir - Read directory entries from a directory
 * @file: The directory to read from
 * @dirent: Buffer for dirents
 * @filldir: Function used to do the copying
 *
 * Returns: 0 on success, -EXXXX on failure
 */

static int
gfs_readdir(struct file *file, void *dirent, filldir_t filldir)
{
	int error;

	atomic_inc(&vfs2sdp(file->f_mapping->host->i_sb)->sd_ops_file);

	if (strcmp(current->comm, "nfsd") != 0)
		error = readdir_reg(file, dirent, filldir);
	else
		error = readdir_bad(file, dirent, filldir);

	return error;
}

/**
 * gfs_ioctl - do an ioctl on a file
 * @inode: the inode
 * @file: the file pointer
 * @cmd: the ioctl command
 * @arg: the argument
 *
 * Returns: 0 on success, -EXXXX on failure
 */

static int
gfs_ioctl(struct inode *inode, struct file *file,
	  unsigned int cmd, unsigned long arg)
{
	struct gfs_inode *ip = vn2ip(inode);
	atomic_inc(&ip->i_sbd->sd_ops_file);
	return gfs_ioctli(ip, cmd, (void *)arg);
}

/**
 * gfs_mmap - We don't support shared writable mappings right now
 * @file: The file to map
 * @vma: The VMA which described the mapping
 *
 * Returns: 0 or error code
 */

static int
gfs_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct gfs_inode *ip = vn2ip(file->f_mapping->host);
	struct gfs_holder i_gh;
	int error;

	atomic_inc(&ip->i_sbd->sd_ops_file);

	gfs_holder_init(ip->i_gl, LM_ST_SHARED, GL_ATIME, &i_gh);
	error = gfs_glock_nq_atime(&i_gh);
	if (error) {
		gfs_holder_uninit(&i_gh);
		return error;
	}

	if (gfs_is_jdata(ip)) {
		if (vma->vm_flags & VM_MAYSHARE)
			error = -ENOSYS;
		else
			vma->vm_ops = &gfs_vm_ops_private;
	} else {
		/* This is VM_MAYWRITE instead of VM_WRITE because a call
		   to mprotect() can turn on VM_WRITE later. */

		if ((vma->vm_flags & (VM_MAYSHARE | VM_MAYWRITE)) == (VM_MAYSHARE | VM_MAYWRITE))
			vma->vm_ops = &gfs_vm_ops_sharewrite;
		else
			vma->vm_ops = &gfs_vm_ops_private;
	}

	gfs_glock_dq_uninit(&i_gh);

	return error;
}

/**
 * gfs_open - open a file
 * @inode: the inode to open
 * @file: the struct file for this opening
 *
 * Returns: 0 on success, -EXXX on failure
 */

static int
gfs_open(struct inode *inode, struct file *file)
{
	struct gfs_inode *ip = vn2ip(inode);
	struct gfs_holder i_gh;
	struct gfs_file *fp;
	int error;

	atomic_inc(&ip->i_sbd->sd_ops_file);

	fp = gmalloc(sizeof(struct gfs_file));
	memset(fp, 0, sizeof(struct gfs_file));

	init_MUTEX(&fp->f_fl_lock);

	fp->f_inode = ip;
	fp->f_vfile = file;

	GFS_ASSERT_INODE(!vf2fp(file), ip,);
	vf2fp(file) = fp;

	if (ip->i_di.di_type == GFS_FILE_REG) {
		error = gfs_glock_nq_init(ip->i_gl,
					  LM_ST_SHARED, LM_FLAG_ANY,
					  &i_gh);
		if (error)
			goto fail;

		if (!(file->f_flags & O_LARGEFILE) &&
		    ip->i_di.di_size > 0x7FFFFFFFull) {
			error = -EFBIG;
			goto fail_gunlock;
		}

		/* Listen to the Direct I/O flag */

		if (ip->i_di.di_flags & GFS_DIF_DIRECTIO)
			file->f_flags |= O_DIRECT;

		/* Don't let the user open O_DIRECT on a jdata file */

		if ((file->f_flags & O_DIRECT) && gfs_is_jdata(ip)) {
			error = -EINVAL;
			goto fail_gunlock;
		}

		gfs_glock_dq_uninit(&i_gh);
	}

	return 0;

 fail_gunlock:
	gfs_glock_dq_uninit(&i_gh);

 fail:
	vf2fp(file) = NULL;
	kfree(fp);

	return error;
}

/**
 * gfs_close - called to close a struct file
 * @inode: the inode the struct file belongs to
 * @file: the struct file being closed
 *
 * Returns: 0 on success, -EXXX on failure
 */

static int
gfs_close(struct inode *inode, struct file *file)
{
	struct gfs_file *fp;

	atomic_inc(&vfs2sdp(inode->i_sb)->sd_ops_file);

	fp = vf2fp(file);
	vf2fp(file) = NULL;

	GFS_ASSERT(fp,);

	kfree(fp);

	return 0;
}

/**
 * gfs_fsync - sync the dirty data for a file (across the cluster)
 * @file: the file that points to the dentry (Huh?)
 * @dentry: the dentry that points to the inode to sync
 *
 * Returns: 0 on success, -EXXX on failure
 */

static int
gfs_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	struct gfs_inode *ip = vn2ip(dentry->d_inode);
	struct gfs_holder i_gh;
	int error;

	atomic_inc(&ip->i_sbd->sd_ops_file);

	error = gfs_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY, &i_gh);
	if (error)
		return error;

	if (gfs_is_jdata(ip))
		gfs_log_flush_glock(ip->i_gl);
	else
		i_gh.gh_flags |= GL_SYNC;

	gfs_glock_dq_uninit(&i_gh);

	return error;
}

/**
 * do_plock - acquire/release a posix lock on a file
 * @file: the file pointer
 * @cmd: either modify or retrieve lock state, possibly wait
 * @fl: type and range of lock
 *
 * Returns: errno
 */

static int
do_plock(struct file *file, int cmd, struct file_lock *fl)
{
	struct gfs_inode *ip = vn2ip(file->f_mapping->host);
	struct gfs_sbd *sdp = ip->i_sbd;
	struct lm_lockname name =
		{ .ln_number = ip->i_num.no_formal_ino,
		  .ln_type = LM_TYPE_PLOCK };

	if (sdp->sd_args.ar_localflocks) {
		if (IS_GETLK(cmd))
			return LOCK_USE_CLNT;
		return posix_lock_file_wait(file, fl);
	}

	if (IS_GETLK(cmd))
		return sdp->sd_lockstruct.ls_ops->lm_plock_get(
			sdp->sd_lockstruct.ls_lockspace,
			&name, file, fl);

	else if (fl->fl_type == F_UNLCK)
		return sdp->sd_lockstruct.ls_ops->lm_punlock(
			sdp->sd_lockstruct.ls_lockspace,
			&name, file, fl);

	else
		return sdp->sd_lockstruct.ls_ops->lm_plock(
			sdp->sd_lockstruct.ls_lockspace,
			&name, file, cmd, fl);
}

/**
 * gfs_lock - acquire/release a posix lock on a file
 * @file: the file pointer
 * @cmd: either modify or retrieve lock state, possibly wait
 * @fl: type and range of lock
 *
 * Returns: errno
 */

static int
gfs_lock(struct file *file, int cmd, struct file_lock *fl)
{
	struct gfs_inode *ip = vn2ip(file->f_mapping->host);
	int error = 0;

	atomic_inc(&ip->i_sbd->sd_ops_file);

	if ((ip->i_di.di_mode & (S_ISGID | S_IXGRP)) == S_ISGID)
		return -ENOLCK;

	if (fl->fl_flags & FL_POSIX)
		error = do_plock(file, cmd, fl);

	else
		error = -ENOLCK;

	return error;
}

/**
 * gfs_sendfile - Send bytes to a file or socket
 * @in_file: The file to read from
 * @out_file: The file to write to
 * @count: The amount of data
 * @offset: The beginning file offset
 *
 * Outputs: offset - updated according to number of bytes read
 *
 * Returns: The number of bytes sent, -EXXX on failure
 */

static ssize_t
gfs_sendfile(struct file *in_file, loff_t *offset, size_t count, read_actor_t actor, void __user *target)
{
	struct gfs_inode *ip = vn2ip(in_file->f_mapping->host);
	struct gfs_holder gh;
	ssize_t retval;

	atomic_inc(&ip->i_sbd->sd_ops_file);

	gfs_holder_init(ip->i_gl, LM_ST_SHARED, GL_ATIME, &gh);

	retval = gfs_glock_nq_atime(&gh);
	if (retval)
		goto out;

	if (gfs_is_jdata(ip))
		retval = -ENOSYS;
	else 
		retval = generic_file_sendfile(in_file, offset, count, actor, target);

	gfs_glock_dq(&gh);

 out:
	gfs_holder_uninit(&gh);

	return retval;
}

/**
 * do_flock - Acquire a flock on a file
 * @file:
 * @cmd:
 * @fl:
 *
 * Returns: errno
 */

static int
do_flock(struct file *file, int cmd, struct file_lock *fl)
{
	struct gfs_file *fp = vf2fp(file);
	struct gfs_holder *fl_gh = &fp->f_fl_gh;
	struct gfs_inode *ip = fp->f_inode;
	struct gfs_glock *gl;
	unsigned int state;
	int flags;
	int error = 0;

	state = (fl->fl_type == F_WRLCK) ? LM_ST_EXCLUSIVE : LM_ST_SHARED;
	flags = ((IS_SETLKW(cmd)) ? 0 : LM_FLAG_TRY) | GL_EXACT | GL_NOCACHE;

	down(&fp->f_fl_lock);

	gl = fl_gh->gh_gl;
	if (gl) {
		if (fl_gh->gh_state == state)
			goto out;
		gfs_glock_hold(gl);
		flock_lock_file_wait(file,
				     &(struct file_lock){.fl_type = F_UNLCK});		
		gfs_glock_dq_uninit(fl_gh);
	} else {
		error = gfs_glock_get(ip->i_sbd,
				      ip->i_num.no_formal_ino, &gfs_flock_glops,
				      CREATE, &gl);
		if (error)
			goto out;
	}

	gfs_holder_init(gl, state, flags, fl_gh);
	gfs_glock_put(gl);

	error = gfs_glock_nq(fl_gh);
	if (error) {
		gfs_holder_uninit(fl_gh);
		if (error == GLR_TRYFAILED) {
			GFS_ASSERT_INODE(flags & LM_FLAG_TRY, ip,);
			error = -EAGAIN;
		}
	} else {
		error = flock_lock_file_wait(file, fl);
		if (error)
			printk("%s: local flock dropped\n",
			       ip->i_sbd->sd_fsname);
	}

 out:
	up(&fp->f_fl_lock);

	return error;
}

/**
 * do_unflock - Release a flock on a file
 * @file: the file
 * @fl:
 *
 */

static void
do_unflock(struct file *file, struct file_lock *fl)
{
	struct gfs_file *fp = vf2fp(file);
	struct gfs_holder *fl_gh = &fp->f_fl_gh;

	down(&fp->f_fl_lock);
	flock_lock_file_wait(file, fl);
	if (fl_gh->gh_gl)
		gfs_glock_dq_uninit(fl_gh);
	up(&fp->f_fl_lock);
}

/**
 * gfs_flock - acquire/release a flock lock on a file
 * @file: the file pointer
 * @cmd: either modify or retrieve lock state, possibly wait
 * @fl: type and range of lock
 *
 * Returns: errno
 */

static int
gfs_flock(struct file *file, int cmd, struct file_lock *fl)
{
	struct gfs_inode *ip = vn2ip(file->f_mapping->host);
	int error = 0;

	atomic_inc(&ip->i_sbd->sd_ops_file);

	if ((ip->i_di.di_mode & (S_ISGID | S_IXGRP)) == S_ISGID)
		return -ENOLCK;

	if (fl->fl_flags & FL_FLOCK) {
		if (fl->fl_type == F_UNLCK)
			do_unflock(file, fl);
		else
			error = do_flock(file, cmd, fl);

	} else
		error = -ENOLCK;

	return error;
}

struct file_operations gfs_file_fops = {
	.llseek = gfs_llseek,
	.read = gfs_read,
	.write = gfs_write,
	.ioctl = gfs_ioctl,
	.mmap = gfs_mmap,
	.open = gfs_open,
	.release = gfs_close,
	.fsync = gfs_fsync,
	.lock = gfs_lock,
	.sendfile = gfs_sendfile,
	.flock = gfs_flock,
};

struct file_operations gfs_dir_fops = {
	.readdir = gfs_readdir,
	.ioctl = gfs_ioctl,
	.open = gfs_open,
	.release = gfs_close,
	.fsync = gfs_fsync,
	.lock = gfs_lock,
	.flock = gfs_flock,
};
