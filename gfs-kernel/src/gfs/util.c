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

#include "gfs.h"
#include "glock.h"

uint32_t gfs_random_number;

kmem_cache_t *gfs_glock_cachep = NULL;
kmem_cache_t *gfs_inode_cachep = NULL;
kmem_cache_t *gfs_bufdata_cachep = NULL;
kmem_cache_t *gfs_mhc_cachep = NULL;

/**
 * gfs_random - Generate a random 32-bit number
 *
 * Generate a semi-crappy 32-bit pseudo-random number without using
 * floating point.
 *
 * The PRNG is from "Numerical Recipes in C" (second edition), page 284.
 *
 * Returns: a 32-bit random number
 */

uint32_t
gfs_random(void)
{
	gfs_random_number = 0x0019660D * gfs_random_number + 0x3C6EF35F;
	return gfs_random_number;
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
	unsigned char *p = (unsigned char *)data;
	unsigned char *e = p + len;
	uint32_t h = hash;

	while (p < e) {
		h ^= (uint32_t)(*p++);
		h *= 0x01000193;
	}

	return h;
}

/**
 * gfs_hash - hash an array of data
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
gfs_hash(const void *data, unsigned int len)
{
	uint32_t h = 0x811C9DC5;
	h = hash_more_internal(data, len, h);
	return h;
}

/**
 * gfs_hash_more - hash an array of data
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
 *   h = gfs_hash(data1, len1);
 *   h = gfs_hash_more(data2, len2, h);
 *   h = gfs_hash_more(data3, len3, h);
 *
 * Returns: the hash
 */

uint32_t
gfs_hash_more(const void *data, unsigned int len, uint32_t hash)
{
	uint32_t h;
	h = hash_more_internal(data, len, hash);
	return h;
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
 * gfs_sort - Sort base array using shell sort algorithm
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
gfs_sort(void *base, unsigned int num_elem, unsigned int size,
	 int (*compar) (const void *, const void *))
{
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
}

/**
 * gfs_assert_i -
 * @sdp:
 * @assertion:
 * @function:
 * @file:
 * @line:
 *
 */

void
gfs_assert_i(struct gfs_sbd *sdp,
	     char *assertion,
	     const char *function,
	     char *file, unsigned int line)
{
	printk("GFS: fsid=%s: assertion \"%s\" failed\n"
	       "GFS: fsid=%s:   function = %s\n"
	       "GFS: fsid=%s:   file = %s, line = %u\n"
	       "GFS: fsid=%s:   time = %lu\n",
	       sdp->sd_fsname, assertion,
	       sdp->sd_fsname, function,
	       sdp->sd_fsname, file, line,
	       sdp->sd_fsname, get_seconds());
	if (!sdp->sd_args.ar_oopses_ok)
		panic_on_oops = 1;
	BUG();
	panic("BUG()\n");
}

/**
 * gfs_warn -
 * @sdp:
 * @fmt:
 *
 */

void
gfs_warn(struct gfs_sbd *sdp, char *fmt, ...)
{
        va_list args;

	if (time_before(jiffies,
			sdp->sd_last_warning +
			sdp->sd_tune.gt_complain_secs * HZ))
		return;

	va_start(args, fmt);
	printk("GFS: fsid=%s: warning: ",
	       sdp->sd_fsname);
	vprintk(fmt, args);
	printk("\n");
        va_end(args);

	sdp->sd_last_warning = jiffies;
}

/**
 * gfs_consist_i -
 * @sdp:
 * @cluster_wide:
 * @function:
 * @file:
 * @line:
 *
 */

void
gfs_consist_i(struct gfs_sbd *sdp, int cluster_wide,
	      const char *function,
	      char *file, unsigned int line)
{
	printk("GFS: fsid=%s: filesystem consistency error\n"
	       "GFS: fsid=%s:   function = %s\n"
	       "GFS: fsid=%s:   file = %s, line = %u\n"
	       "GFS: fsid=%s:   time = %lu\n",
	       sdp->sd_fsname,
	       sdp->sd_fsname, function,
	       sdp->sd_fsname, file, line,
	       sdp->sd_fsname, get_seconds());
	gfs_assert(sdp, FALSE,); /* FixMe!!! */
}

/**
 * gfs_consist_inode_i -
 * @ip:
 * @cluster_wide:
 * @function:
 * @file:
 * @line:
 *
 */

void
gfs_consist_inode_i(struct gfs_inode *ip, int cluster_wide,
		    const char *function,
		    char *file, unsigned int line)
{
	struct gfs_sbd *sdp = ip->i_sbd;
        printk("GFS: fsid=%s: filesystem consistency error\n"
	       "GFS: fsid=%s:   inode = %"PRIu64"/%"PRIu64"\n"
               "GFS: fsid=%s:   function = %s\n"
               "GFS: fsid=%s:   file = %s, line = %u\n"
               "GFS: fsid=%s:   time = %lu\n",
	       sdp->sd_fsname,
	       sdp->sd_fsname, ip->i_num.no_formal_ino, ip->i_num.no_addr,
               sdp->sd_fsname, function,
               sdp->sd_fsname, file, line,
               sdp->sd_fsname, get_seconds());
        gfs_assert(sdp, FALSE,); /* FixMe!!! */
}

/**
 * gfs_consist_rgrpd_i -
 * @rgd:
 * @cluster_wide:
 * @function:
 * @file:
 * @line:
 *
 */

void
gfs_consist_rgrpd_i(struct gfs_rgrpd *rgd, int cluster_wide,
		    const char *function,
		    char *file, unsigned int line)
{
        struct gfs_sbd *sdp = rgd->rd_sbd;
        printk("GFS: fsid=%s: filesystem consistency error\n"
               "GFS: fsid=%s:   RG = %"PRIu64"\n"
               "GFS: fsid=%s:   function = %s\n"
               "GFS: fsid=%s:   file = %s, line = %u\n"
               "GFS: fsid=%s:   time = %lu\n",
	       sdp->sd_fsname,
	       sdp->sd_fsname, rgd->rd_ri.ri_addr,
               sdp->sd_fsname, function,
               sdp->sd_fsname, file, line,
               sdp->sd_fsname, get_seconds());
        gfs_assert(sdp, FALSE,); /* FixMe!!! */
}

/**
 * gfs_io_error_i -
 * @sdp:
 * @function:
 * @file:
 * @line:
 *
 */

void
gfs_io_error_i(struct gfs_sbd *sdp,
	       const char *function,
	       char *file, unsigned int line)
{
        printk("GFS: fsid=%s: I/O error\n"
               "GFS: fsid=%s:   function = %s\n"
               "GFS: fsid=%s:   file = %s, line = %u\n"
               "GFS: fsid=%s:   time = %lu\n",
	       sdp->sd_fsname,
               sdp->sd_fsname, function,
               sdp->sd_fsname, file, line,
               sdp->sd_fsname, get_seconds());
        gfs_assert(sdp, FALSE,); /* FixMe!!! */
}

/**
 * gfs_io_error_inode_i -
 * @ip:
 * @function:
 * @file:
 * @line:
 *
 */

void
gfs_io_error_inode_i(struct gfs_inode *ip,
		     const char *function,
		     char *file, unsigned int line)
{
	struct gfs_sbd *sdp = ip->i_sbd;
        printk("GFS: fsid=%s: I/O error\n"
               "GFS: fsid=%s:   inode = %"PRIu64"/%"PRIu64"\n"
               "GFS: fsid=%s:   function = %s\n"
               "GFS: fsid=%s:   file = %s, line = %u\n"
               "GFS: fsid=%s:   time = %lu\n",
	       sdp->sd_fsname,
	       sdp->sd_fsname, ip->i_num.no_formal_ino, ip->i_num.no_addr,
               sdp->sd_fsname, function,
               sdp->sd_fsname, file, line,
               sdp->sd_fsname, get_seconds());
        gfs_assert(sdp, FALSE,); /* FixMe!!! */
}

/**
 * gfs_io_error_bh_i -
 * @sdp:
 * @bh:
 * @function:
 * @file:
 * @line:
 *
 */

void
gfs_io_error_bh_i(struct gfs_sbd *sdp, struct buffer_head *bh,
		  const char *function,
		  char *file, unsigned int line)
{
        printk("GFS: fsid=%s: I/O error\n"
	       "GFS: fsid=%s:   block = %"PRIu64"\n"
               "GFS: fsid=%s:   function = %s\n"
               "GFS: fsid=%s:   file = %s, line = %u\n"
               "GFS: fsid=%s:   time = %lu\n",
	       sdp->sd_fsname,
	       sdp->sd_fsname, (uint64_t)bh->b_blocknr,
               sdp->sd_fsname, function,
               sdp->sd_fsname, file, line,
               sdp->sd_fsname, get_seconds());
        gfs_assert(sdp, FALSE,); /* FixMe!!! */
}

/**
 * gmalloc - malloc a small amount of memory
 * @size: the number of bytes to malloc
 *
 * Returns: the memory
 */

void *
gmalloc(unsigned int size)
{
	void *p;
	RETRY_MALLOC(p = kmalloc(size, GFP_KERNEL), p);
	return p;
}

