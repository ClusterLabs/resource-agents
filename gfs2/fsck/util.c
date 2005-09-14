/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <inttypes.h>
#include <linux_endian.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bio.h"
#include "fs_bits.h"

#include "util.h"
#include "log.h"

/**
 * compute_height
 * @sdp:
 * @sz:
 *
 */
int compute_height(struct fsck_sb *sdp, uint64_t sz)
{
  unsigned int height;
  uint64_t space, old_space;
  unsigned int bsize = sdp->sb.sb_bsize;

  if (sz <= (bsize - sizeof(struct gfs2_dinode)))
    return 0;

  height = 1;
  space = sdp->diptrs * bsize;

  while (sz > space)
  {
    old_space = space;

    height++;
    space *= sdp->inptrs;

    if (space / sdp->inptrs != old_space ||
        space % sdp->inptrs != 0)
      break;
  }

  return height;
}


/*
 * check_range - check if blkno is within FS limits
 * @sdp: super block
 * @blkno: block number
 *
 * Returns: 0 if ok, -1 if out of bounds
 */
int check_range(struct fsck_sb *sdp, uint64_t blkno){
	if((blkno > sdp->last_fs_block) ||
	   (blkno < sdp->first_data_block))
		return -1;
	return 0;
}


/*
 * set_meta - set the meta header of a buffer
 * @bh
 * @type
 *
 * Returns: 0 if ok, -1 on error
 */
int set_meta(struct buffer_head *bh, int type, int format){
  struct gfs2_meta_header header;

  if(!check_meta(bh, 0)){
	  log_debug("Setting metadata\n");
    ((struct gfs2_meta_header *)BH_DATA(bh))->mh_type = cpu_to_gfs2_32(type);
    ((struct gfs2_meta_header *)BH_DATA(bh))->mh_format = cpu_to_gfs2_32(format);
  } else {
    memset(&header, 0, sizeof(struct gfs2_meta_header));
    header.mh_magic = GFS2_MAGIC;
    header.mh_type = type;
    header.mh_format = format;

    gfs2_meta_header_out(&header, BH_DATA(bh));
  }
  return 0;
}




/*
 * check_meta - check the meta header of a buffer
 * @bh: buffer to check
 * @type: meta type (or 0 if don't care)
 *
 * Returns: 0 if ok, -1 on error
 */
int check_meta(struct buffer_head *bh, int type){
  uint32_t check_magic = ((struct gfs2_meta_header *)BH_DATA((bh)))->mh_magic;
  uint32_t check_type = ((struct gfs2_meta_header *)BH_DATA((bh)))->mh_type;
 
  check_magic = gfs2_32_to_cpu(check_magic);
  check_type = gfs2_32_to_cpu(check_type);
  if((check_magic != GFS2_MAGIC) || (type && (check_type != type))){
	  log_debug("For %"PRIu64" Expected %X:%X - got %X:%X\n", BH_BLKNO(bh), GFS2_MAGIC, type,
		    check_magic, check_type);
    return -1;
  }
  return 0;
}

/*
 * check_type - check the meta type of a buffer
 * @bh: buffer to check
 * @type: meta type
 *
 * Returns: 0 if ok, -1 on error
 */
int check_type(struct buffer_head *bh, int type){
  uint32_t check_magic = ((struct gfs2_meta_header *)BH_DATA((bh)))->mh_magic;
  uint32_t check_type = ((struct gfs2_meta_header *)BH_DATA((bh)))->mh_type;
 
  check_magic = gfs2_32_to_cpu(check_magic);
  check_type = gfs2_32_to_cpu(check_type);
  if((check_magic != GFS2_MAGIC) || (check_type != type)){
    return -1;
  }
  return 0;
}

/**
 * next_rg_meta
 * @rgd:
 * @block:
 * @first: if set, start at zero and ignore block
 *
 * The position to start looking from is *block.  When a block
 * is found, it is returned in block.
 *
 * Returns: 0 on success, -1 when finished
 */
int next_rg_meta(struct fsck_rgrp *rgd, uint64_t *block, int first)
{
  fs_bitmap_t *bits = NULL;
  uint32_t length = rgd->rd_ri.ri_length;
  uint32_t blk = (first)? 0: (uint32_t)((*block+1)-rgd->rd_ri.ri_data0);
  int i;

  if(!first && (*block < rgd->rd_ri.ri_data0)){
    log_err("next_rg_meta:  Start block is outside rgrp bounds.\n");
    exit(1);
  }

  for(i=0; i < length; i++){
    bits = &rgd->rd_bits[i];
    if(blk < bits->bi_len*GFS2_NBBY){
      break;
    }
    blk -= bits->bi_len*GFS2_NBBY;
  }


  for(; i < length; i++){
    bits = &rgd->rd_bits[i];

    /* FIXME need to verify DINODE is correct - I think all other
     * metadata will be caught either by walking the dinode structures
     * or in initialization - this has worked fine so far, but it may
     * be an issue that we won't see until some specific corruption
     * point - just keep that in mind*/
    blk = fs_bitfit(BH_DATA(rgd->rd_bh[i]) + bits->bi_offset,
                    bits->bi_len, blk, GFS2_BLKST_DINODE);

    if(blk != BFITNOENT){
	    *block = blk + (bits->bi_start * GFS2_NBBY) + rgd->rd_ri.ri_data0;
	    break;
    }

    blk=0;
  }

  if(i == length){
    return -1;
  }
  return 0;
}


/**
 * next_rg_metatype
 * @rgd:
 * @block:
 * @type: the type of metadata we're looking for
 * @first: if set we should start at block zero and block is ignored
 *
 * Returns: 0 on success, -1 on error or finished
 */
int next_rg_metatype(struct fsck_rgrp *rgd, uint64_t *block, uint32_t type, int first)
{
  struct fsck_sb *sdp = rgd->rd_sbd;
  struct buffer_head *bh=NULL;

  do{
    relse_buf(sdp, bh);
    if(next_rg_meta(rgd, block, first))
      return -1;

    if(get_and_read_buf(sdp, *block, &bh, 0)){
      log_err("next_rg_metatype:  Unable to read meta block "
	      "#%"PRIu64" from disk\n", *block);
      exit(1);
    }

    if(check_meta(bh,0)){
      log_err("next_rg_metatype:  next_rg_meta returned block #%"PRIu64",\n"
	      "                   which is not a valid meta block.\n", *block);
      exit(1);
    }

    first = 0;
  } while(check_meta(bh, type));
  relse_buf(sdp, bh);

  return 0;
}

