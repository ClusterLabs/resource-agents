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

#include "gfs2.h"
#include "glock.h"
#include "lm.h"

uint32_t gfs2_random_number;

unsigned long gfs2_malloc_warning = 0;

kmem_cache_t *gfs2_glock_cachep = NULL;
kmem_cache_t *gfs2_inode_cachep = NULL;
kmem_cache_t *gfs2_bufdata_cachep = NULL;

/**
 * gfs2_random - Generate a random 32-bit number
 *
 * Generate a semi-crappy 32-bit pseudo-random number without using
 * floating point.
 *
 * The PRNG is from "Numerical Recipes in C" (second edition), page 284.
 *
 * Returns: a 32-bit random number
 */

uint32_t
gfs2_random(void)
{
	ENTER(G2FN_RANDOM)
	gfs2_random_number = 0x0019660D * gfs2_random_number + 0x3C6EF35F;
	RETURN(G2FN_RANDOM, gfs2_random_number);
}

/**
 * hash_more_internal - hash an array of data
 * @data: the data to be hashed
 * @len: the length of data to be hashed
 * @hash: the hash from a previous call
 *
 * Take some data and convert it to a 32-bit hash.
 *
 * This is the 32-bit FNV-1a hash from:
 * http://www.isthe.com/chongo/tech/comp/fnv/
 *
 * Hash guts
 *
 * Returns: the hash
 */

static __inline__ uint32_t
hash_more_internal(const void *data, unsigned int len, uint32_t hash)
{
	ENTER(G2FN_HASH_MORE_INTERNAL)
	unsigned char *p = (unsigned char *)data;
	unsigned char *e = p + len;
	uint32_t h = hash;

	while (p < e) {
		h ^= (uint32_t)(*p++);
		h *= 0x01000193;
	}

	RETURN(G2FN_HASH_MORE_INTERNAL, h);
}

/**
 * gfs2_hash - hash an array of data
 * @data: the data to be hashed
 * @len: the length of data to be hashed
 *
 * Take some data and convert it to a 32-bit hash.
 *
 * This is the 32-bit FNV-1a hash from:
 * http://www.isthe.com/chongo/tech/comp/fnv/
 *
 * Returns: the hash
 */

uint32_t
gfs2_hash(const void *data, unsigned int len)
{
	ENTER(G2FN_HASH)
	uint32_t h = 0x811C9DC5;
	h = hash_more_internal(data, len, h);
	RETURN(G2FN_HASH, h);
}

/**
 * gfs2_hash_more - hash an array of data
 * @data: the data to be hashed
 * @len: the length of data to be hashed
 * @hash: the hash from a previous call
 *
 * Take some data and convert it to a 32-bit hash.
 *
 * This is the 32-bit FNV-1a hash from:
 * http://www.isthe.com/chongo/tech/comp/fnv/
 *
 * This version let's you hash together discontinuous regions.
 * For example, to compute the combined hash of the memory in
 * (data1, len1), (data2, len2), and (data3, len3) you:
 *
 *   h = gfs2_hash(data1, len1);
 *   h = gfs2_hash_more(data2, len2, h);
 *   h = gfs2_hash_more(data3, len3, h);
 *
 * Returns: the hash
 */

uint32_t
gfs2_hash_more(const void *data, unsigned int len, uint32_t hash)
{
	ENTER(G2FN_HASH_MORE)
	RETURN(G2FN_HASH_MORE,
	       hash_more_internal(data, len, hash));
}

/* Byte-wise swap two items of size SIZE. */

#define SWAP(a, b, size) \
do { \
	register size_t __size = (size); \
        register char *__a = (a), *__b = (b); \
        do { \
		char __tmp = *__a; \
		*__a++ = *__b; \
		*__b++ = __tmp; \
	} while (__size-- > 1); \
} while (0)

/**
 * gfs2_sort - Sort base array using shell sort algorithm
 * @base: the input array
 * @num_elem: number of elements in array
 * @size: size of each element in array
 * @compar: fxn to compare array elements (returns negative
 *          for lt, 0 for eq, and positive for gt
 *
 * Sorts the array passed in using the compar fxn to compare elements using
 * the shell sort algorithm
 */

void
gfs2_sort(void *base, unsigned int num_elem, unsigned int size,
	 int (*compar) (const void *, const void *))
{
	ENTER(G2FN_SORT)
	register char *pbase = (char *)base;
	int i, j, k, h;
	static int cols[16] = {1391376, 463792, 198768, 86961,
			       33936, 13776, 4592, 1968,
			       861, 336, 112, 48,
			       21, 7, 3, 1};
	
	for (k = 0; k < 16; k++) {
		h = cols[k];
		for (i = h; i < num_elem; i++) {
			j = i;
			while (j >= h &&
			       (*compar)((void *)(pbase + size * (j - h)),
					 (void *)(pbase + size * j)) > 0) {
				SWAP(pbase + size * j,
				     pbase + size * (j - h),
				     size);
				j = j - h;
			}
		}
	}

	RET(G2FN_SORT);
}

/**
 * gfs2_assert_i - Cause the machine to panic if @assertion is false
 * @sdp:
 * @assertion:
 * @function:
 * @file:
 * @line:
 *
 */

void
gfs2_assert_i(struct gfs2_sbd *sdp,
	     char *assertion,
	     const char *function,
	     char *file, unsigned int line)
{
	if (sdp->sd_args.ar_oopses_ok) {
		printk("GFS2: fsid=%s: fatal: assertion \"%s\" failed\n"
		       "GFS2: fsid=%s:   function = %s\n"
		       "GFS2: fsid=%s:   file = %s, line = %u\n"
		       "GFS2: fsid=%s:   time = %lu\n",
		       sdp->sd_fsname, assertion,
		       sdp->sd_fsname, function,
		       sdp->sd_fsname, file, line,
		       sdp->sd_fsname, get_seconds());
		BUG();
	}
	dump_stack();
	panic("GFS2: fsid=%s: fatal: assertion \"%s\" failed\n"
	      "GFS2: fsid=%s:   function = %s\n"
	      "GFS2: fsid=%s:   file = %s, line = %u\n"
	      "GFS2: fsid=%s:   time = %lu\n",
	      sdp->sd_fsname, assertion,
	      sdp->sd_fsname, function,
	      sdp->sd_fsname, file, line,
	      sdp->sd_fsname, get_seconds());
}

/**
 * gfs2_assert_withdraw_i - Cause the machine to withdraw if @assertion is false
 * @sdp:
 * @assertion:
 * @function:
 * @file:
 * @line:
 *
 * Returns: -1 if this call withdrew the machine,
 *          -2 if it was already withdrawn
 */

int
gfs2_assert_withdraw_i(struct gfs2_sbd *sdp,
		      char *assertion,
		      const char *function,
		      char *file, unsigned int line)
{
	ENTER(G2FN_ASSERT_WITHDRAW_I)
	int me;
	me = gfs2_lm_withdraw(sdp,
			     "GFS2: fsid=%s: fatal: assertion \"%s\" failed\n"
			     "GFS2: fsid=%s:   function = %s\n"
			     "GFS2: fsid=%s:   file = %s, line = %u\n"
			     "GFS2: fsid=%s:   time = %lu\n",
			     sdp->sd_fsname, assertion,
			     sdp->sd_fsname, function,
			     sdp->sd_fsname, file, line,
			     sdp->sd_fsname, get_seconds());
	RETURN(G2FN_ASSERT_WITHDRAW_I, (me) ? -1 : -2);
}

/**
 * gfs2_assert_warn_i - Print a message to the console if @assertion is false
 * @sdp:
 * @assertion:
 * @function:
 * @file:
 * @line:
 *
 * Returns: -1 if we printed something
 *          -2 if we didn't
 */

int
gfs2_assert_warn_i(struct gfs2_sbd *sdp,
		  char *assertion,
		  const char *function,
		  char *file, unsigned int line)
{
	ENTER(G2FN_ASSERT_WARN_I)

	if (time_before(jiffies,
			sdp->sd_last_warning +
			gfs2_tune_get(sdp, gt_complain_secs) * HZ))
		RETURN(G2FN_ASSERT_WARN_I, -2);

	printk("GFS2: fsid=%s: warning: assertion \"%s\" failed\n"
	       "GFS2: fsid=%s:   function = %s\n"
	       "GFS2: fsid=%s:   file = %s, line = %u\n"
	       "GFS2: fsid=%s:   time = %lu\n",
	       sdp->sd_fsname, assertion,
	       sdp->sd_fsname, function,
	       sdp->sd_fsname, file, line,
	       sdp->sd_fsname, get_seconds());

	if (sdp->sd_args.ar_debug)
		BUG();

	sdp->sd_last_warning = jiffies;

	RETURN(G2FN_ASSERT_WARN_I, -1);
}

/**
 * gfs2_consist_i - Flag a filesystem consistency error and withdraw
 * @sdp:
 * @cluster_wide:
 * @function:
 * @file:
 * @line:
 *
 * Returns: -1 if this call withdrew the machine,
 *          0 if it was already withdrawn
 */

int
gfs2_consist_i(struct gfs2_sbd *sdp, int cluster_wide,
	      const char *function,
	      char *file, unsigned int line)
{
	ENTER(G2FN_CONSIST_I)
	RETURN(G2FN_CONSIST_I,
	       gfs2_lm_withdraw(sdp,
			       "GFS2: fsid=%s: fatal: filesystem consistency error\n"
			       "GFS2: fsid=%s:   function = %s\n"
			       "GFS2: fsid=%s:   file = %s, line = %u\n"
			       "GFS2: fsid=%s:   time = %lu\n",
			       sdp->sd_fsname,
			       sdp->sd_fsname, function,
			       sdp->sd_fsname, file, line,
			       sdp->sd_fsname, get_seconds()));
}

/**
 * gfs2_consist_inode_i - Flag an inode consistency error and withdraw
 * @ip:
 * @cluster_wide:
 * @function:
 * @file:
 * @line:
 *
 * Returns: -1 if this call withdrew the machine,
 *          0 if it was already withdrawn
 */

int
gfs2_consist_inode_i(struct gfs2_inode *ip, int cluster_wide,
		    const char *function,
		    char *file, unsigned int line)
{
	ENTER(G2FN_CONSIST_INODE_I)
	struct gfs2_sbd *sdp = ip->i_sbd;
        RETURN(G2FN_CONSIST_INODE_I,
	       gfs2_lm_withdraw(sdp,
			       "GFS2: fsid=%s: fatal: filesystem consistency error\n"
			       "GFS2: fsid=%s:   inode = %"PRIu64"/%"PRIu64"\n"
			       "GFS2: fsid=%s:   function = %s\n"
			       "GFS2: fsid=%s:   file = %s, line = %u\n"
			       "GFS2: fsid=%s:   time = %lu\n",
			       sdp->sd_fsname,
			       sdp->sd_fsname, ip->i_num.no_formal_ino, ip->i_num.no_addr,
			       sdp->sd_fsname, function,
			       sdp->sd_fsname, file, line,
			       sdp->sd_fsname, get_seconds()));
}

/**
 * gfs2_consist_rgrpd_i - Flag a RG consistency error and withdraw
 * @rgd:
 * @cluster_wide:
 * @function:
 * @file:
 * @line:
 *
 * Returns: -1 if this call withdrew the machine,
 *          0 if it was already withdrawn
 */

int
gfs2_consist_rgrpd_i(struct gfs2_rgrpd *rgd, int cluster_wide,
		    const char *function,
		    char *file, unsigned int line)
{
	ENTER(G2FN_CONSIST_RGRPD_I)
        struct gfs2_sbd *sdp = rgd->rd_sbd;
        RETURN(G2FN_CONSIST_RGRPD_I,
	       gfs2_lm_withdraw(sdp,
			       "GFS2: fsid=%s: fatal: filesystem consistency error\n"
			       "GFS2: fsid=%s:   RG = %"PRIu64"\n"
			       "GFS2: fsid=%s:   function = %s\n"
			       "GFS2: fsid=%s:   file = %s, line = %u\n"
			       "GFS2: fsid=%s:   time = %lu\n",
			       sdp->sd_fsname,
			       sdp->sd_fsname, rgd->rd_ri.ri_addr,
			       sdp->sd_fsname, function,
			       sdp->sd_fsname, file, line,
			       sdp->sd_fsname, get_seconds()));
}

/**
 * gfs2_meta_check_ii - Flag a magic number consistency error and withdraw
 * @sdp:
 * @bh:
 * @function:
 * @file:
 * @line:
 *
 * Returns: -1 if this call withdrew the machine,
 *          -2 if it was already withdrawn
 */

int
gfs2_meta_check_ii(struct gfs2_sbd *sdp, struct buffer_head *bh,
		  const char *type,
                  const char *function,
                  char *file, unsigned int line)
{
	ENTER(G2FN_META_CHECK_II)
	int me;
        me = gfs2_lm_withdraw(sdp,
			     "GFS2: fsid=%s: fatal: invalid metadata block\n"
			     "GFS2: fsid=%s:   bh = %"PRIu64" (%s)\n"
			     "GFS2: fsid=%s:   function = %s\n"
			     "GFS2: fsid=%s:   file = %s, line = %u\n"
			     "GFS2: fsid=%s:   time = %lu\n",
			     sdp->sd_fsname,
			     sdp->sd_fsname, (uint64_t)bh->b_blocknr, type,
			     sdp->sd_fsname, function,
			     sdp->sd_fsname, file, line,
			     sdp->sd_fsname, get_seconds());
	RETURN(G2FN_META_CHECK_II, (me) ? -1 : -2);
}

/**
 * gfs2_metatype_check_ii - Flag a metadata type consistency error and withdraw
 * @sdp:
 * @bh:
 * @type:
 * @t:
 * @function:
 * @file:
 * @line:
 *
 * Returns: -1 if this call withdrew the machine,
 *          -2 if it was already withdrawn
 */

int
gfs2_metatype_check_ii(struct gfs2_sbd *sdp, struct buffer_head *bh,
		      uint16_t type, uint16_t t,
		      const char *function,
		      char *file, unsigned int line)
{
	ENTER(G2FN_METATYPE_CHECK_II)
	int me;
        me = gfs2_lm_withdraw(sdp,
			     "GFS2: fsid=%s: fatal: invalid metadata block\n"
			     "GFS2: fsid=%s:   bh = %"PRIu64" (type: exp=%u, found=%u)\n"
			     "GFS2: fsid=%s:   function = %s\n"
			     "GFS2: fsid=%s:   file = %s, line = %u\n"
			     "GFS2: fsid=%s:   time = %lu\n",
			     sdp->sd_fsname,
			     sdp->sd_fsname, (uint64_t)bh->b_blocknr, type, t,
			     sdp->sd_fsname, function,
			     sdp->sd_fsname, file, line,
			     sdp->sd_fsname, get_seconds());
	RETURN(G2FN_METATYPE_CHECK_II, (me) ? -1 : -2);
}

/**
 * gfs2_io_error_i - Flag an I/O error and withdraw
 * @sdp:
 * @function:
 * @file:
 * @line:
 *
 * Returns: -1 if this call withdrew the machine,
 *          0 if it was already withdrawn
 */

int
gfs2_io_error_i(struct gfs2_sbd *sdp,
	       const char *function,
	       char *file, unsigned int line)
{
	ENTER(G2FN_IO_ERROR_i)
        RETURN(G2FN_IO_ERROR_i,
	       gfs2_lm_withdraw(sdp,
			       "GFS2: fsid=%s: fatal: I/O error\n"
			       "GFS2: fsid=%s:   function = %s\n"
			       "GFS2: fsid=%s:   file = %s, line = %u\n"
			       "GFS2: fsid=%s:   time = %lu\n",
			       sdp->sd_fsname,
			       sdp->sd_fsname, function,
			       sdp->sd_fsname, file, line,
			       sdp->sd_fsname, get_seconds()));
}

/**
 * gfs2_io_error_inode_i - Flag an inode I/O error and withdraw
 * @ip:
 * @function:
 * @file:
 * @line:
 *
 * Returns: -1 if this call withdrew the machine,
 *          0 if it was already withdrawn
 */

int
gfs2_io_error_inode_i(struct gfs2_inode *ip,
		     const char *function,
		     char *file, unsigned int line)
{
	ENTER(G2FN_IO_ERROR_INODE_I)
	struct gfs2_sbd *sdp = ip->i_sbd;
        RETURN(G2FN_IO_ERROR_INODE_I,
	       gfs2_lm_withdraw(sdp,
			       "GFS2: fsid=%s: fatal: I/O error\n"
			       "GFS2: fsid=%s:   inode = %"PRIu64"/%"PRIu64"\n"
			       "GFS2: fsid=%s:   function = %s\n"
			       "GFS2: fsid=%s:   file = %s, line = %u\n"
			       "GFS2: fsid=%s:   time = %lu\n",
			       sdp->sd_fsname,
			       sdp->sd_fsname, ip->i_num.no_formal_ino, ip->i_num.no_addr,
			       sdp->sd_fsname, function,
			       sdp->sd_fsname, file, line,
			       sdp->sd_fsname, get_seconds()));
}

/**
 * gfs2_io_error_bh_i - Flag a buffer I/O error and withdraw
 * @sdp:
 * @bh:
 * @function:
 * @file:
 * @line:
 *
 * Returns: -1 if this call withdrew the machine,
 *          0 if it was already withdrawn
 */

int
gfs2_io_error_bh_i(struct gfs2_sbd *sdp, struct buffer_head *bh,
		  const char *function,
		  char *file, unsigned int line)
{
	ENTER(G2FN_IO_ERROR_BH_I)
        RETURN(G2FN_IO_ERROR_BH_I,
	       gfs2_lm_withdraw(sdp,
			       "GFS2: fsid=%s: fatal: I/O error\n"
			       "GFS2: fsid=%s:   block = %"PRIu64"\n"
			       "GFS2: fsid=%s:   function = %s\n"
			       "GFS2: fsid=%s:   file = %s, line = %u\n"
			       "GFS2: fsid=%s:   time = %lu\n",
			       sdp->sd_fsname,
			       sdp->sd_fsname, (uint64_t)bh->b_blocknr,
			       sdp->sd_fsname, function,
			       sdp->sd_fsname, file, line,
			       sdp->sd_fsname, get_seconds()));
}

/**
 * gfs2_add_bh_to_ub - copy a buffer up to user space
 * @ub: the structure representing where to copy
 * @bh: the buffer
 *
 * Returns: errno
 */

int
gfs2_add_bh_to_ub(struct gfs2_user_buffer *ub, struct buffer_head *bh)
{
	ENTER(G2FN_ADD_BH_TO_UB)

	uint64_t blkno = bh->b_blocknr;

	if (ub->ub_count + sizeof(uint64_t) + bh->b_size > ub->ub_size)
		RETURN(G2FN_ADD_BH_TO_UB, -ENOMEM);

	if (copy_to_user(ub->ub_data + ub->ub_count,
			  &blkno,
			  sizeof(uint64_t)))
		RETURN(G2FN_ADD_BH_TO_UB, -EFAULT);
	ub->ub_count += sizeof(uint64_t);

	if (copy_to_user(ub->ub_data + ub->ub_count,
			  bh->b_data,
			  bh->b_size))
		RETURN(G2FN_ADD_BH_TO_UB, -EFAULT);
	ub->ub_count += bh->b_size;

	RETURN(G2FN_ADD_BH_TO_UB, 0);
}

/**
 * gfs2_printf_i -
 * @buf:
 * @size:
 * @count:
 * @fmt:
 *
 * Returns: 0 on success, 1 on out of space
 */

int
gfs2_printf_i(char *buf, unsigned int size, unsigned int *count,
	     char *fmt, ...)
{
	ENTER(G2FN_PRINTF_I)
	va_list args;
	int left, out;

	if (!buf) {
		va_start(args, fmt);
		vprintk(fmt, args);
		va_end(args);
		RETURN(G2FN_PRINTF_I, 0);
	}

	left = size - *count;
	if (left <= 0)
		RETURN(G2FN_PRINTF_I, 1);

	va_start(args, fmt);
	out = vsnprintf(buf + *count, left, fmt, args);
	va_end(args);

	if (out < left)
		*count += out;
	else
		RETURN(G2FN_PRINTF_I, 1);

	RETURN(G2FN_PRINTF_I, 0);
}

void
gfs2_icbit_munge(struct gfs2_sbd *sdp,
		unsigned char **bitmap, unsigned int bit,
		int new_value)
{
	ENTER(G2FN_ICBIT_MUNGE)
	unsigned int c, o, b = bit;
	int old_value;

	c = b / (8 * PAGE_SIZE);
	b %= 8 * PAGE_SIZE;
	o = b / 8;
	b %= 8;

	old_value = (bitmap[c][o] & (1 << b));
	gfs2_assert_withdraw(sdp, !old_value != !new_value);

	if (new_value)
		bitmap[c][o] |= 1 << b;
	else
		bitmap[c][o] &= ~(1 << b);

	RET(G2FN_ICBIT_MUNGE);
}

