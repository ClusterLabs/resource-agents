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

volatile int gfs_in_panic = FALSE;

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
	int cols[16] = {1391376, 463792, 198768, 86961, 33936, 13776, 4592,
			1968, 861, 336, 112, 48, 21, 7, 3, 1};
	
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
 * bitch_about - 
 * @sdp: the filesystem
 * @last: the last time we bitched
 * @about:
 *
 */

void
bitch_about(struct gfs_sbd *sdp, unsigned long *last, char *about)
{
	if (time_after_eq(jiffies, *last + sdp->sd_tune.gt_complain_secs * HZ)) {
		printk("GFS: fsid=%s: %s by program \"%s\"\n",
		       sdp->sd_fsname, about, current->comm);
		*last = jiffies;
	}
}

/**
 * gfs_assert_i - Stop the machine
 * @assertion: the assertion that failed
 * @file: the file that called us
 * @line: the line number of the file that called us
 *
 * Don't do ENTER() and EXIT() here.
 *
 */

void
gfs_assert_i(char *assertion,
	     unsigned int type, void *ptr,
	     char *file, unsigned int line)
{
	gfs_in_panic = TRUE;

	switch (type) {
	case GFS_ASSERT_TYPE_SBD:
	{
		struct gfs_sbd *sdp = (struct gfs_sbd *)ptr;
		panic("GFS: Assertion failed on line %d of file %s\n"
		      "GFS: assertion: \"%s\"\n"
		      "GFS: time = %lu\n"
		      "GFS: fsid=%s\n",
		      line, file, assertion, get_seconds(),
		      sdp->sd_fsname);
	}
	break;

	case GFS_ASSERT_TYPE_GLOCK:
	{
		struct gfs_glock *gl = (struct gfs_glock *)ptr;
		struct gfs_sbd *sdp = gl->gl_sbd;
		panic("GFS: Assertion failed on line %d of file %s\n"
		      "GFS: assertion: \"%s\"\n"
		      "GFS: time = %lu\n"
		      "GFS: fsid=%s: glock = (%u, %"PRIu64")\n",
		      line, file, assertion, get_seconds(),
		      sdp->sd_fsname,
		      gl->gl_name.ln_type,
		      gl->gl_name.ln_number);
	}
	break;

	case GFS_ASSERT_TYPE_INODE:
	{
		struct gfs_inode *ip = (struct gfs_inode *)ptr;
		struct gfs_sbd *sdp = ip->i_sbd;
		panic("GFS: Assertion failed on line %d of file %s\n"
		      "GFS: assertion: \"%s\"\n"
		      "GFS: time = %lu\n"
		      "GFS: fsid=%s: inode = %"PRIu64"/%"PRIu64"\n",
		      line, file, assertion, get_seconds(),
		      sdp->sd_fsname,
		      ip->i_num.no_formal_ino, ip->i_num.no_addr);
	}
	break;

	case GFS_ASSERT_TYPE_RGRPD:
	{
		struct gfs_rgrpd *rgd = (struct gfs_rgrpd *)ptr;
		struct gfs_sbd *sdp = rgd->rd_sbd;
		panic("GFS: Assertion failed on line %d of file %s\n"
		      "GFS: assertion: \"%s\"\n"
		      "GFS: time = %lu\n"
		      "GFS: fsid=%s: rgroup = %"PRIu64"\n",
		      line, file, assertion, get_seconds(),
		      sdp->sd_fsname,
		      rgd->rd_ri.ri_addr);
	}
	break;

	default:
		panic("GFS: Assertion failed on line %d of file %s\n"
		      "GFS: assertion: \"%s\"\n"
		      "GFS: time = %lu\n",
		      line, file, assertion, get_seconds());
	}
}

/**
 * gfs_io_errori - handle an I/O error
 * @sdp: the filesystem
 * @bh: the buffer the error happened on (can be NULL)
 *
 * This will do something other than panic, eventually.
 *
 */

void gfs_io_error_i(struct gfs_sbd *sdp,
		    unsigned int type, void *ptr,
		    char *file, unsigned int line)
{
	switch (type) {
	case GFS_IO_ERROR_TYPE_BH:
	{
		struct buffer_head *bh = (struct buffer_head *)ptr;
		printk("GFS: fsid=%s: I/O error on block %"PRIu64"\n",
		       sdp->sd_fsname, (uint64_t)bh->b_blocknr);
	}
	break;

	case GFS_IO_ERROR_TYPE_INODE:
	{
		struct gfs_inode *ip = (struct gfs_inode *)ptr;
		printk("GFS: fsid=%s: I/O error in inode %"PRIu64"/%"PRIu64"\n",
		       sdp->sd_fsname,
		       ip->i_num.no_formal_ino, ip->i_num.no_addr);
	}
	break;

	default:
	printk("GFS: fsid=%s: I/O error\n", sdp->sd_fsname);
	break;
	}

	GFS_ASSERT_SBD(FALSE, sdp,);
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

