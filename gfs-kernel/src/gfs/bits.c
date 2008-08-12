/*
 * These routines are used by the resource group routines (rgrp.c)
 * to keep track of block allocation.  Each block is represented by two
 * bits.  One bit indicates whether or not the block is used.  (1=used,
 * 0=free)  The other bit indicates whether or not the block contains a
 * dinode or not.  (1=dinode, 0=data block) So, each byte represents
 * GFS_NBBY (i.e. 4) blocks.  
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>

#include "gfs.h"
#include "bits.h"

#if BITS_PER_LONG == 32
#define LBITMASK   (0x55555555UL)
#define LBITSKIP55 (0x55555555UL)
#define LBITSKIP00 (0x00000000UL)
#else
#define LBITMASK   (0x5555555555555555UL)
#define LBITSKIP55 (0x5555555555555555UL)
#define LBITSKIP00 (0x0000000000000000UL)
#endif

static const char valid_change[16] = {
	        /* current */
	/* n */ 0, 1, 1, 1,
	/* e */ 1, 0, 0, 0,
	/* w */ 1, 0, 0, 1,
	        0, 0, 1, 0
};

/**
 * gfs_setbit - Set a bit in the bitmaps
 * @buffer: the buffer that holds the bitmaps
 * @buflen: the length (in bytes) of the buffer
 * @block: the block to set
 * @new_state: the new state of the block
 *
 */

void
gfs_setbit(struct gfs_rgrpd *rgd,
	   unsigned char *buffer, unsigned int buflen,
	   uint32_t block, unsigned char new_state)
{
	unsigned char *byte, *end, cur_state;
	unsigned int bit;

	byte = buffer + (block / GFS_NBBY);
	bit = (block % GFS_NBBY) * GFS_BIT_SIZE;
	end = buffer + buflen;

	gfs_assert(rgd->rd_sbd, byte < end,);

	cur_state = (*byte >> bit) & GFS_BIT_MASK;

	if (valid_change[new_state * 4 + cur_state]) {
		*byte ^= cur_state << bit;
		*byte |= new_state << bit;
	} else
		gfs_consist_rgrpd(rgd);
}

/**
 * gfs_testbit - test a bit in the bitmaps
 * @buffer: the buffer that holds the bitmaps
 * @buflen: the length (in bytes) of the buffer
 * @block: the block to read
 *
 */

unsigned char
gfs_testbit(struct gfs_rgrpd *rgd,
	    unsigned char *buffer, unsigned int buflen, uint32_t block)
{
	unsigned char *byte, *end, cur_state;
	unsigned int bit;

	byte = buffer + (block / GFS_NBBY);
	bit = (block % GFS_NBBY) * GFS_BIT_SIZE;
	end = buffer + buflen;

        gfs_assert(rgd->rd_sbd, byte < end,);

	cur_state = (*byte >> bit) & GFS_BIT_MASK;

	return cur_state;
}

/**
 * gfs_bitfit - Search an rgrp's bitmap buffer to find a bit-pair representing
 *       a block in a given allocation state.
 * @buffer: the buffer that holds the bitmaps
 * @buflen: the length (in bytes) of the buffer
 * @goal: start search at this block's bit-pair (within @buffer)
 * @old_state: GFS_BLKST_XXX the state of the block we're looking for;
 *       bit 0 = alloc(1)/free(0), bit 1 = meta(1)/data(0)
 * 
 * Scope of @goal and returned block number is only within this bitmap buffer,
 *   not entire rgrp or filesystem.
 * @buffer will be offset from the actual beginning of a bitmap block buffer,
 *   skipping any header structures.
 *
 * Return: the block number (bitmap buffer scope) that was found
 */

uint32_t
gfs_bitfit(unsigned char *buffer, unsigned int buflen,
	   uint32_t goal, unsigned char old_state)
{
	const u8 *byte, *start, *end;
	int bit, startbit;
	u32 g1, g2, misaligned;
	unsigned long *plong;
	unsigned long lskipval;

	lskipval = (old_state & GFS_BLKST_USED) ? LBITSKIP00 : LBITSKIP55;
	g1 = (goal / GFS_NBBY);
	start = buffer + g1;
	byte = start;
        end = buffer + buflen;
	g2 = ALIGN(g1, sizeof(unsigned long));
	plong = (unsigned long *)(buffer + g2);
	startbit = bit = (goal % GFS_NBBY) * GFS_BIT_SIZE;
	misaligned = g2 - g1;
	if (!misaligned)
		goto ulong_aligned;
/* parse the bitmap a byte at a time */
misaligned:
	while (byte < end) {
		if (((*byte >> bit) & GFS_BIT_MASK) == old_state) {
			return goal +
				(((byte - start) * GFS_NBBY) +
				 ((bit - startbit) >> 1));
		}
		bit += GFS_BIT_SIZE;
		if (bit >= GFS_NBBY * GFS_BIT_SIZE) {
			bit = 0;
			byte++;
			misaligned--;
			if (!misaligned) {
				plong = (unsigned long *)byte;
				goto ulong_aligned;
			}
		}
	}
	return BFITNOENT;

/* parse the bitmap a unsigned long at a time */
ulong_aligned:
	/* Stop at "end - 1" or else prefetch can go past the end and segfault.
	   We could "if" it but we'd lose some of the performance gained.
	   This way will only slow down searching the very last 4/8 bytes
	   depending on architecture.  I've experimented with several ways
	   of writing this section such as using an else before the goto
	   but this one seems to be the fastest. */
	while ((unsigned char *)plong < end - sizeof(unsigned long)) {
		prefetch(plong + 1);
		if (((*plong) & LBITMASK) != lskipval)
			break;
		plong++;
	}
	if ((unsigned char *)plong < end) {
		byte = (const u8 *)plong;
		misaligned += sizeof(unsigned long) - 1;
		goto misaligned;
	}
	return BFITNOENT;
}

/**
 * gfs_bitcount - count the number of bits in a certain state
 * @buffer: the buffer that holds the bitmaps
 * @buflen: the length (in bytes) of the buffer
 * @state: the state of the block we're looking for
 *
 * Returns: The number of bits
 */

uint32_t
gfs_bitcount(struct gfs_rgrpd *rgd,
	     unsigned char *buffer, unsigned int buflen,
	     unsigned char state)
{
	unsigned char *byte = buffer;
	unsigned char *end = buffer + buflen;
	unsigned char state1 = state << 2;
	unsigned char state2 = state << 4;
	unsigned char state3 = state << 6;
	uint32_t count = 0;

	for (; byte < end; byte++) {
		if (((*byte) & 0x03) == state)
			count++;
		if (((*byte) & 0x0C) == state1)
			count++;
		if (((*byte) & 0x30) == state2)
			count++;
		if (((*byte) & 0xC0) == state3)
			count++;
	}

	return count;
}
