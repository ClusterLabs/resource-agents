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

#include "interactive.h"
#include "fs_bio.h"
#include "fs_bits.h"

#include "util.h"

/**
 * compute_height
 * @sdp:
 * @sz:
 *
 */
int compute_height(fs_sbd_t *sdp, uint64 sz)
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
int check_range(fs_sbd_t *sdp, uint64 blkno){
  if((blkno > sdp->sd_last_fs_block) ||
     (blkno < sdp->sd_first_data_block))
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
int next_rg_meta(fs_rgrpd_t *rgd, uint64 *block, int first)
{
  fs_bitmap_t *bits = NULL;
  uint32 length = rgd->rd_ri.ri_length;
  uint32 blk = (first)? 0: (uint32)((*block+1)-rgd->rd_ri.ri_data1);
  int i;

  if(!first && (*block < rgd->rd_ri.ri_data1)){
    die("next_rg_meta:  Start block is outside rgrp bounds.\n");
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
 * next_rg_metatype
 * @rgd:
 * @block:
 * @type: the type of metadata we're looking for
 * @first: if set we should start at block zero and block is ignored
 *
 * Returns: 0 on success, -1 on error or finished
 */
int next_rg_metatype(fs_rgrpd_t *rgd, uint64 *block, uint32 type, int first)
{
  fs_sbd_t *sdp = rgd->rd_sbd;
  osi_buf_t *bh=NULL;

  do{
    fs_relse_buf(sdp, bh);
    if(next_rg_meta(rgd, block, first))
      return -1;

    if(fs_get_and_read_buf(sdp, *block, &bh, 0)){
      die("next_rg_metatype:  Unable to read meta block "
          "#%"PRIu64" from disk\n", *block);
    }
  
    if(check_meta(bh,0)){
      die("next_rg_metatype:  next_rg_meta returned block #%"PRIu64",\n"
	  "                   which is not a valid meta block.\n", *block);
    }
  
    first = 0;
  } while(check_meta(bh, type));
  fs_relse_buf(sdp, bh);

  return 0;
}


/**
 * build_metalist
 * @ip:
 * @mlp:
 *
 */

int build_metalist(fs_inode_t *ip, osi_list_t *mlp)
{
  uint32 height = ip->i_di.di_height;
  osi_buf_t *bh, *nbh;
  osi_list_t *prev_list, *cur_list, *tmp;
  int i, head_size;
  uint64 *ptr, block;

  fs_get_and_read_buf(ip->i_sbd, ip->i_di.di_num.no_addr, &bh, 0);

  osi_list_add(&bh->b_list, &mlp[0]);

  /* if(<there are no indirect blocks to check>) */
  if (height < 2)
    return 0;

  for (i = 1; i < height; i++){
    prev_list = &mlp[i - 1];
    cur_list = &mlp[i];

    for (tmp = prev_list->next; tmp != prev_list; tmp = tmp->next){
      bh = osi_list_entry(tmp, osi_buf_t, b_list);

      head_size = (i > 1 ? sizeof(struct gfs_indirect) : sizeof(struct gfs_dinode));
      ptr = (uint64 *)(bh->b_data + head_size);

      for ( ; (char *)ptr < (bh->b_data + bh->b_size); ptr++){
        if (!*ptr)
          continue;

        block = gfs64_to_cpu(*ptr);

        if (check_range(ip->i_sbd, block)){ /* blk outside of FS */
          pp_print(PPN, "Bad indirect block pointer (out of range).\n");
          if(query("\tReplace lost data with a hole? (y/n): ")){
            /* set *ptr to 0 and write the buffer to disk */
            *ptr = 0;
            if(fs_write_buf(ip->i_sbd, bh, 0) < 0){
              pp_print(PPVH, "Unable to perform fs_write_buf().\n");
              pp_print(PPVH, "Corrupted file metadata tree remains.\n");
              goto fail;
            }else{
              pp_print(PPN, "\tData replaced.\n");
            }
          } else {
            pp_print(PPN, "Corrupted file metadata tree remains.\n");
          }
          continue;
        }
        fs_get_and_read_buf(ip->i_sbd, block, &nbh, 0);

        /** Attention -- experimental code **/
        if (check_meta(nbh, GFS_METATYPE_IN)){
          pp_print(PPN, "Bad indirect block pointer "
                   "(points to something that is not an indirect block).\n");
          if(query("\tReplace lost data with a hole? (y/n): "))
          {
            /* set *ptr to 0 and write the buffer to disk */
            *ptr = 0;
            if(fs_write_buf(ip->i_sbd, bh, 0) < 0){
              pp_print(PPVH, "Unable to perform fs_write_buf().\n");
              pp_print(PPVH, "Corrupted file metadata tree remains.\n");
              goto fail;
            }else{
              pp_print(PPN, "\tData replaced.\n");
            }
          } else {
            pp_print(PPN, "Corrupted file metadata tree remains.\n");
          }
          fs_relse_buf(ip->i_sbd, nbh);
        }else{  /* blk check ok */
          osi_list_add(&nbh->b_list, cur_list);
        }
        /** Attention -- experimental code end **/
      }
    }
  }
  return 0;

 fail:
  for (i = 0; i < GFS_MAX_META_HEIGHT; i++)
  {
    osi_list_t *list;
    list = &mlp[i];
    while (!osi_list_empty(list))
    {
      bh = osi_list_entry(list->next, osi_buf_t, b_list);
      osi_list_del(&bh->b_list);
      fs_relse_buf(ip->i_sbd, bh);
    }
  }
  return -1;
}


/**
 * search_list
 * @list
 * @addr
 *
 * Returns: di_info_t ptr if found, NULL otherwise
 */
di_info_t *search_list(osi_list_t *list, uint64 addr)
{
  osi_list_t *tmp;
  di_info_t *dinfo;


  for (tmp = list->next; tmp != list; tmp = tmp->next)
  {
    dinfo = osi_list_entry(tmp, di_info_t, din_list);

    if (dinfo->din_addr == addr)
      return(dinfo);
  }

  return NULL;
}
