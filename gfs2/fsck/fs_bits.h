#ifndef __FS_BITS_H__
#define __FS_BITS_H__

/*#include "global.h" */
#include "libgfs2.h"
#include "fsck.h"

#define BFITNOENT (0xFFFFFFFF)

struct fs_bitmap
{
	uint32_t   bi_offset;	/* The offset in the buffer of the first byte */
	uint32_t   bi_start;    /* The position of the first byte in this block */
	uint32_t   bi_len;      /* The number of bytes in this block */
};
typedef struct fs_bitmap fs_bitmap_t;

/* functions with blk #'s that are buffer relative */
uint32_t gfs2_bitcount(unsigned char *buffer, unsigned int buflen,
		     unsigned char state);
uint32_t gfs2_bitfit(unsigned char *buffer, unsigned int buflen,
		   uint32_t goal, unsigned char old_state);

/* functions with blk #'s that are rgrp relative */
uint32_t gfs2_blkalloc_internal(struct rgrp_list *rgd, uint32_t goal,
								unsigned char old_state,
								unsigned char new_state, int do_it);

/* functions with blk #'s that are file system relative */
int gfs2_get_bitmap(struct gfs2_sbd *sdp, uint64_t blkno,
					struct rgrp_list *rgd);
int gfs2_set_bitmap(struct gfs2_sbd *sdp, uint64_t blkno, int state);

#endif /* __FS_BITS_H__ */
