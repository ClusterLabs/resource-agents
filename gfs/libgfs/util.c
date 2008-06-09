#include "libgfs.h"

/**
 * compute_height
 * @sdp:
 * @sz:
 *
 */
int compute_height(struct gfs_sbd *sdp, uint64 sz)
{
  unsigned int height;
  uint64 space, old_space;
  unsigned int bsize = sdp->sd_sb.sb_bsize;

  if (sz <= (bsize - sizeof(struct gfs_dinode)))
    return 0;

  height = 1;
  space = sdp->sd_diptrs * bsize;

  while (sz > space)
  {
    old_space = space;

    height++;
    space *= sdp->sd_inptrs;

    if (space / sdp->sd_inptrs != old_space ||
        space % sdp->sd_inptrs != 0)
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
int check_range(struct gfs_sbd *sdp, uint64 blkno){
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
int set_meta(osi_buf_t *bh, int type, int format){
  struct gfs_meta_header header;

  if(!check_meta(bh, 0)){
	  log_debug("Setting metadata\n");
    ((struct gfs_meta_header *)BH_DATA(bh))->mh_type = cpu_to_gfs32(type);
    ((struct gfs_meta_header *)BH_DATA(bh))->mh_format = cpu_to_gfs32(format);
  } else {
    memset(&header, 0, sizeof(struct gfs_meta_header));
    header.mh_magic = GFS_MAGIC;
    header.mh_type = type;
    header.mh_format = format;

    gfs_meta_header_out(&header, BH_DATA(bh));
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
int check_meta(osi_buf_t *bh, int type){
  uint32 check_magic = ((struct gfs_meta_header *)BH_DATA((bh)))->mh_magic;
  uint32 check_type = ((struct gfs_meta_header *)BH_DATA((bh)))->mh_type;
 
  check_magic = gfs32_to_cpu(check_magic);
  check_type = gfs32_to_cpu(check_type);
  if((check_magic != GFS_MAGIC) || (type && (check_type != type))){
	  log_debug("For %"PRIu64" Expected %X:%X - got %X:%X\n", BH_BLKNO(bh), GFS_MAGIC, type,
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
int check_type(osi_buf_t *bh, int type){
  uint32 check_magic = ((struct gfs_meta_header *)BH_DATA((bh)))->mh_magic;
  uint32 check_type = ((struct gfs_meta_header *)BH_DATA((bh)))->mh_type;
 
  check_magic = gfs32_to_cpu(check_magic);
  check_type = gfs32_to_cpu(check_type);
  if((check_magic != GFS_MAGIC) || (check_type != type)){
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
int next_rg_meta(struct gfs_rgrpd *rgd, uint64 *block, int first)
{
  struct gfs_bitmap *bits = NULL;
  uint32 length = rgd->rd_ri.ri_length;
  uint32 blk = (first)? 0: (uint32)((*block+1)-rgd->rd_ri.ri_data1);
  int i;

  if(!first && (*block < rgd->rd_ri.ri_data1)){
    log_err("next_rg_meta:  Start block is outside rgrp bounds.\n");
    exit(1);
  }

  for(i=0; i < length; i++){
    bits = &rgd->rd_bits[i];
    if(blk < bits->bi_len*GFS_NBBY){
      break;
    }
    blk -= bits->bi_len*GFS_NBBY;
  }


  for(; i < length; i++){
    bits = &rgd->rd_bits[i];

    blk = fs_bitfit(BH_DATA(rgd->rd_bh[i]) + bits->bi_offset,
                    bits->bi_len, blk, GFS_BLKST_USEDMETA);

    if(blk != BFITNOENT){
	    *block = blk + (bits->bi_start * GFS_NBBY) + rgd->rd_ri.ri_data1;
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
 * next_rg_meta_free - finds free or used metadata
 * @rgd:
 * @block:
 * @first: if set, start at zero and ignore block
 *
 * The position to start looking from is *block.  When a block
 * is found, it is returned in block.
 *
 * Returns: 0 on success, -1 when finished
 */
int next_rg_meta_free(struct gfs_rgrpd *rgd, uint64 *block, int first, int *mfree)
{
  struct gfs_bitmap *bits = NULL;
  uint32 length = rgd->rd_ri.ri_length;
  uint32 blk = (first)? 0: (uint32)((*block+1)-rgd->rd_ri.ri_data1);
  uint32 ublk, fblk;
  int i;

  if(!first && (*block < rgd->rd_ri.ri_data1)){
    log_err("next_rg_meta:  Start block is outside rgrp bounds.\n");
    exit(1);
  }

  for(i=0; i < length; i++){
    bits = &rgd->rd_bits[i];
    if(blk < bits->bi_len*GFS_NBBY){
      break;
    }
    blk -= bits->bi_len*GFS_NBBY;
  }


  for(; i < length; i++){
    bits = &rgd->rd_bits[i];

    ublk = fs_bitfit(BH_DATA(rgd->rd_bh[i]) + bits->bi_offset,
                    bits->bi_len, blk, GFS_BLKST_USEDMETA);

    fblk = fs_bitfit(BH_DATA(rgd->rd_bh[i]) + bits->bi_offset,
			     bits->bi_len, blk, GFS_BLKST_FREEMETA);
    if(ublk < fblk) {
	    blk = ublk;
	    *mfree = 0;
    } else {
	    blk = fblk;
	    *mfree = 1;
    }
    if(blk != BFITNOENT){
	    *block = blk + (bits->bi_start * GFS_NBBY) + rgd->rd_ri.ri_data1;
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
int next_rg_metatype(int disk_fd, struct gfs_rgrpd *rgd, uint64 *block,
					 uint32 type, int first)
{
  struct gfs_sbd *sdp = rgd->rd_sbd;
  osi_buf_t *bh=NULL;

  do{
    relse_buf(bh);
    if(next_rg_meta(rgd, block, first))
      return -1;

    if (get_and_read_buf(disk_fd, sdp->sd_sb.sb_bsize, *block, &bh, 0)){
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
  relse_buf(bh);

  return 0;
}
