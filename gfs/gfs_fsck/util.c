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
int compute_height(struct fsck_sb *sdp, uint64 sz)
{
  unsigned int height;
  uint64 space, old_space;
  unsigned int bsize = sdp->sb.sb_bsize;

  if (sz <= (bsize - sizeof(struct gfs_dinode)))
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
int check_range(struct fsck_sb *sdp, uint64 blkno){
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
int next_rg_meta(struct fsck_rgrp *rgd, uint64 *block, int first)
{
  fs_bitmap_t *bits = NULL;
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

    blk = fs_bitfit((unsigned char *)BH_DATA(rgd->rd_bh[i]) + bits->bi_offset,
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
int next_rg_meta_free(struct fsck_rgrp *rgd, uint64 *block, int first, int *mfree)
{
  fs_bitmap_t *bits = NULL;
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

    ublk = fs_bitfit((unsigned char *)BH_DATA(rgd->rd_bh[i]) + bits->bi_offset,
                    bits->bi_len, blk, GFS_BLKST_USEDMETA);

    fblk = fs_bitfit((unsigned char *)BH_DATA(rgd->rd_bh[i]) + bits->bi_offset,
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
int next_rg_metatype(struct fsck_rgrp *rgd, uint64 *block, uint32 type, int first)
{
  struct fsck_sb *sdp = rgd->rd_sbd;
  osi_buf_t *bh=NULL;

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



#if 0
/**
 * search_list
 * @list
 * @addr
 *
 * Returns: di_info_t ptr if found, NULL otherwise
 */
struct di_info *search_list(osi_list_t *list, uint64 addr)
{
  osi_list_t *tmp;
  struct di_info *dinfo;

  for (tmp = list->next; tmp != list; tmp = tmp->next)
  {
    dinfo = osi_list_entry(tmp, struct di_info, din_list);

    if (dinfo->din_addr == addr)
      return(dinfo);
  }

  return NULL;
}
#endif

/* Put out a warm, fuzzy message every second so the user     */
/* doesn't think we hung.  (This may take a long time).       */
/* We only check whether to report every one percent because  */
/* checking every block kills performance.  We only report    */
/* every second because we don't need 100 extra messages in   */
/* logs made from verbose mode.                               */
void warm_fuzzy_stuff(uint64_t block)
{
	static uint64_t one_percent = 0;
	static struct timeval tv;
	static uint32_t seconds = 0;
	
	if (!one_percent)
		one_percent = last_fs_block / 100;
	if (block - last_reported_block >= one_percent) {
		last_reported_block = block;
		gettimeofday(&tv, NULL);
		if (!seconds)
			seconds = tv.tv_sec;
		if (tv.tv_sec - seconds) {
			static uint64_t percent;

			seconds = tv.tv_sec;
			if (last_fs_block) {
				percent = (block * 100) / last_fs_block;
				log_notice("\r%" PRIu64 " percent complete.\r", percent);
			}
		}
	}
}
