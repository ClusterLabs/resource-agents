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

#include "allocation.h"
#include "interactive.h"
#include "util.h"
#include "fs_bio.h"
#include "fs_bits.h"
#include "fs_inode.h"

#include "fs_rgrp.h"

/**
 * fs_compute_bitstructs - Compute the bitmap sizes
 * @rgd: The resource group descriptor
 *
 * Returns: 0 on success, -1 on error
 */
int fs_compute_bitstructs(fs_rgrpd_t *rgd)
{
  fs_sbd_t *sdp = rgd->rd_sbd;
  fs_bitmap_t *bits;
  uint32 length = rgd->rd_ri.ri_length;
  uint32 bytes_left, bytes;
  int x;

  rgd->rd_bits = (fs_bitmap_t *)gfsck_zalloc(length * sizeof(fs_bitmap_t));
	
  bytes_left = rgd->rd_ri.ri_bitbytes;
	
  for (x = 0; x < length; x++){
    bits = &rgd->rd_bits[x];
    
    if (length == 1){
      bytes = bytes_left;
      bits->bi_offset = sizeof(struct gfs_rgrp);
      bits->bi_start = 0;
      bits->bi_len = bytes;
    }
    else if (x == 0){
      bytes = sdp->sd_sb.sb_bsize - sizeof(struct gfs_rgrp);
      bits->bi_offset = sizeof(struct gfs_rgrp);
      bits->bi_start = 0;
      bits->bi_len = bytes;
    }
    else if (x + 1 == length){
      bytes = bytes_left;
      bits->bi_offset = sizeof(struct gfs_meta_header);
      bits->bi_start = rgd->rd_ri.ri_bitbytes - bytes_left;
      bits->bi_len = bytes;
    }
    else{
      bytes = sdp->sd_sb.sb_bsize - sizeof(struct gfs_meta_header);
      bits->bi_offset = sizeof(struct gfs_meta_header);
      bits->bi_start = rgd->rd_ri.ri_bitbytes - bytes_left;
      bits->bi_len = bytes;
    }
	
    bytes_left -= bytes;
  }
	
  if(bytes_left){
    pp_print(PPVH, "fs_compute_bitstructs:  Too many blocks in rgrp to "
	     "fit into available bitmap.\n");
    return -1;
  }

  if((rgd->rd_bits[length - 1].bi_start +
      rgd->rd_bits[length - 1].bi_len) * GFS_NBBY != rgd->rd_ri.ri_data){
    pp_print(PPVH, "fs_compute_bitstructs:  # of blks in rgrp do not equal "
	     "# of blks represented in bitmap.\n"
	     "\tbi_start = %u\n"
	     "\tbi_len   = %u\n"
	     "\tGFS_NBBY = %u\n"
	     "\tri_data  = %u\n",
	     rgd->rd_bits[length - 1].bi_start,
	     rgd->rd_bits[length - 1].bi_len,
	     GFS_NBBY,
	     rgd->rd_ri.ri_data);
    return -1;
  }
  
	
  rgd->rd_bh = (osi_buf_t **)gfsck_zalloc(length * sizeof(osi_buf_t *));
  return 0;
}


/**
 * blk2rgrpd - Find resource group for a given data block number
 * @sdp: The GFS superblock
 * @n: The data block number
 *
 * Returns: Ths resource group, or NULL if not found
 */
fs_rgrpd_t *fs_blk2rgrpd(fs_sbd_t *sdp, uint64 blk)
{
  osi_list_t *tmp;
  fs_rgrpd_t *rgd = NULL;
  struct gfs_rindex *ri;
	
  for(tmp = sdp->sd_rglist.next; tmp != &sdp->sd_rglist; tmp = tmp->next){
    rgd = osi_list_entry(tmp, fs_rgrpd_t, rd_list);
    ri = &rgd->rd_ri;
	    
    if (ri->ri_data1 <= blk && blk < ri->ri_data1 + ri->ri_data){
      break;
    } else
      rgd = NULL;
  }
  return rgd;
}
	

int fs_rgrp_read(fs_rgrpd_t *rgd)
{
  fs_sbd_t *sdp = rgd->rd_sbd;
  unsigned int x, length = rgd->rd_ri.ri_length;
  int error;

  if(rgd->rd_open_count){
    pp_print(PPD, "fs_rgrp_read:  rgrp already read...\n");
    rgd->rd_open_count++;
    return 0;
  }

  for (x = 0; x < length; x++){
    if(rgd->rd_bh[x]){
      die("Programmer error!  Bitmaps are already present in rgrp.\n");
    }
    error = fs_get_and_read_buf(sdp, rgd->rd_ri.ri_addr + x,
				&(rgd->rd_bh[x]), 0);
    if(check_meta(rgd->rd_bh[x], (x) ? GFS_METATYPE_RB : GFS_METATYPE_RG)){
      pp_print(PPVH, "Buffer #%"PRIu64" (%d of %d) is neither GFS_METATYPE_RB"
	       "nor GFS_METATYPE_RG.\n",
	       BH_BLKNO(rgd->rd_bh[x]),
	       (int)x+1,
	       (int)length);
      error = -1;
      goto fail;
    }
  }
	
  gfs_rgrp_in(&rgd->rd_rg, BH_DATA(rgd->rd_bh[0]));
  rgd->rd_open_count = 1;
	
  return 0;
	
 fail:
  for (x = 0; x < length; x++){
    fs_relse_buf(sdp, rgd->rd_bh[x]);
    rgd->rd_bh[x] = NULL;
  }

  pp_print(PPVH, "Resource group is corrupted.\n");
  return error;
}
	
void fs_rgrp_relse(fs_rgrpd_t *rgd)
{
  int x, length = rgd->rd_ri.ri_length;

  rgd->rd_open_count--;
  if(rgd->rd_open_count){
    pp_print(PPD, "fs_rgrp_relse:  rgrp still held...\n");
  } else {
    for (x = 0; x < length; x++){
      fs_relse_buf(rgd->rd_sbd, rgd->rd_bh[x]);
      rgd->rd_bh[x] = NULL;
    }	
  }
}


/**
 * rgrp_verify - Verify that a resource group is consistent
 * @sdp: the filesystem
 * @rgd: the rgrp
 *
 * Returns: 0 if ok, -1 on error
 */
int fs_rgrp_verify(fs_rgrpd_t *rgd)
{
  fs_bitmap_t *bits = NULL;
  uint32 length = rgd->rd_ri.ri_length;
  uint32 count[4], tmp;
  int buf, x;
	
  for (x = 0; x < 4; x++){
    count[x] = 0;
	
    for (buf = 0; buf < length; buf++){
      bits = &rgd->rd_bits[buf];
      count[x] += fs_bitcount(BH_DATA(rgd->rd_bh[buf]) + bits->bi_offset,
			      bits->bi_len, x);
    }
  }
	
  if(count[0] != rgd->rd_rg.rg_free){
    pp_print(PPVH, "free data mismatch:  %u != %u\n",
	   count[0], rgd->rd_rg.rg_free);
    return -1;
  }
	
  tmp = rgd->rd_ri.ri_data -
    (rgd->rd_rg.rg_usedmeta + rgd->rd_rg.rg_freemeta) -
    (rgd->rd_rg.rg_useddi + rgd->rd_rg.rg_freedi) -
    rgd->rd_rg.rg_free;

  if(count[1] != tmp){
    pp_print(PPVH, "used data mismatch:  %u != %u\n",
	   count[1], tmp);
    return -1;
  }
  if(count[2] != rgd->rd_rg.rg_freemeta){
    pp_print(PPVH, "free metadata mismatch:  %u != %u\n",
	   count[2], rgd->rd_rg.rg_freemeta);
    return -1;
  }

  tmp = rgd->rd_rg.rg_usedmeta + 
    (rgd->rd_rg.rg_useddi + rgd->rd_rg.rg_freedi);

  if(count[3] != tmp){
    pp_print(PPVH, "used metadata mismatch:  %u != %u\n",
	   count[3], tmp);
    return -1;
  }
  return 0;
}


/**
 * fs_rgrp_recount - adjust block tracking numbers
 * rgd: resource group
 *
 * The resource groups keep track of how many free blocks, used blocks,
 * etc there are.  This function readjusts those numbers based on the
 * current state of the bitmap.
 *
 * Returns: 0 on success, -1 on failure
 */
int fs_rgrp_recount(fs_rgrpd_t *rgd){
  int i,j;
  fs_bitmap_t *bits = NULL;
  uint32 length = rgd->rd_ri.ri_length;
  uint32 count[4], tmp;

  for(i=0; i < 4; i++){
    count[i] = 0;
    for(j = 0; j < length; j++){
      bits = &rgd->rd_bits[j];
      count[i] += fs_bitcount(BH_DATA(rgd->rd_bh[j]) + bits->bi_offset,
                              bits->bi_len, i);
    }
  }
  if(count[0] != rgd->rd_rg.rg_free){
    pp_print(PPL, "\tAdjusting free block count (%u -> %u).\n",
           rgd->rd_rg.rg_free, count[0]);
    rgd->rd_rg.rg_free = count[0];
  }
  if(count[2] != rgd->rd_rg.rg_freemeta){
    pp_print(PPL, "\tAdjusting freemeta block count (%u -> %u).\n",
           rgd->rd_rg.rg_freemeta, count[2]);
    rgd->rd_rg.rg_freemeta = count[2];
  }
  tmp = rgd->rd_rg.rg_usedmeta + 
    (rgd->rd_rg.rg_useddi + rgd->rd_rg.rg_freedi);
  
  if(count[3] != tmp){
    int first = 1;
    fs_sbd_t *sdp = rgd->rd_sbd;
    uint32 useddi = 0;
    uint32 freedi = 0;
    uint64 block; 
    fs_inode_t *ip;

    ip = (fs_inode_t *)gfsck_zalloc(sizeof(fs_inode_t));
    ip->i_sbd = sdp;

    while (1){  /* count the used dinodes */
      if(next_rg_metatype(rgd, &block, GFS_METATYPE_DI, first)){
        break;
      }
      first = 0;

      ip->i_num.no_addr = ip->i_num.no_formal_ino = block;
      if(fs_copyin_dinode(ip)){
	pp_print(PPN, "Unable to retrieve disk inode data.\n");
	continue;
      }

      if (ip->i_di.di_flags & GFS_DIF_UNUSED){
        freedi++;
        continue;
      }
      useddi++;
    }

    gfsck_free(ip);
    
    if(useddi != rgd->rd_rg.rg_useddi){
      pp_print(PPL, "\tAdjusting used dinode block count (%u -> %u).\n",
             rgd->rd_rg.rg_useddi, useddi);
      rgd->rd_rg.rg_useddi = useddi;
    }
    if(freedi != rgd->rd_rg.rg_freedi){
      pp_print(PPL, "\tAdjusting free dinode block count (%u -> %u).\n",
             rgd->rd_rg.rg_freedi, freedi);
      rgd->rd_rg.rg_freedi = freedi;
    }
    if(rgd->rd_rg.rg_usedmeta != count[3] - (freedi + useddi)){
      pp_print(PPL, "\tAdjusting used meta block count (%u -> %u).\n",
             rgd->rd_rg.rg_usedmeta, (count[3] - (freedi + useddi)));
      rgd->rd_rg.rg_usedmeta = count[3] - (freedi + useddi);
    }
  }

  tmp = rgd->rd_ri.ri_data -
    (rgd->rd_rg.rg_usedmeta + rgd->rd_rg.rg_freemeta) -
    (rgd->rd_rg.rg_useddi + rgd->rd_rg.rg_freedi) -
    rgd->rd_rg.rg_free; 

  if(count[1] != tmp){
    pp_print(PPVH, "Could not reconcile rgrp block counts.\n");
    return -1;
  }
  return 0;
}



/**
 * clump_alloc - Allocate a clump of metadata
 * @rgd: the resource group descriptor
 * @goal: the goal block in the RG
 *
 * Returns: 0 on success, -1 on failure
 */
int clump_alloc(fs_rgrpd_t *rgd, uint32 goal)
{
  fs_sbd_t *sdp = rgd->rd_sbd;
  struct gfs_meta_header mh;
  osi_buf_t *bh[GFS_META_CLUMP];
  uint32 block;
  unsigned int i,j;
  int error = 0;
	
  memset(&mh, 0, sizeof(struct gfs_meta_header));
  mh.mh_magic = GFS_MAGIC;
  mh.mh_type = GFS_METATYPE_NONE;

  if(rgd->rd_rg.rg_free < GFS_META_CLUMP){
    pp_print(PPVL, "clump_alloc:  Not enough free blocks in rgrp.\n");
    return -1;
  }

  for (i = 0; i < GFS_META_CLUMP; i++){
    block = fs_blkalloc_internal(rgd, goal,
			      GFS_BLKST_FREE, GFS_BLKST_FREEMETA, TRUE);

    if(fs_get_buf(sdp, rgd->rd_ri.ri_data1 + block, &(bh[i]))){
      pp_print(PPVH, "Unable to allocate new buffer.\n");
      goto fail;
    }
    gfs_meta_header_out(&mh, BH_DATA(bh[i]));

    goal = block;
  }
	
  pp_print(PPVL, "64 Meta blocks (%"PRIu64" - %"PRIu64"), allocated in rgrp 0x%lx\n",
	   (rgd->rd_ri.ri_data1 + block)-63,
	   (rgd->rd_ri.ri_data1 + block),
	   (unsigned long)rgd);
  for (j = 0; j < GFS_META_CLUMP; j++){
	
    error = fs_write_buf(sdp, bh[j], BW_WAIT);
    if (error){
      pp_print(PPVH, "Unable to write allocated metablock to disk.\n");
      goto fail;
    }
  }
	
  if(rgd->rd_rg.rg_free < GFS_META_CLUMP){
    pp_print(PPVH, "More blocks were allocated from rgrp "
	     "than are available.\n");
    goto fail;
  }
  rgd->rd_rg.rg_free -= GFS_META_CLUMP;
  rgd->rd_rg.rg_freemeta += GFS_META_CLUMP;
	
  for (i = 0; i < GFS_META_CLUMP; i++)
    fs_relse_buf(sdp, bh[i]);
  
  return 0;

 fail:
  pp_print(PPL, "clump_alloc failing...\n");
  for(--i; i >=0; i--){
    fs_set_bitmap(sdp, BH_BLKNO(bh[i]), GFS_BLKST_FREE);
    fs_relse_buf(sdp, bh[i]);
  }
  return -1;
}
	
	
/**
 * fs_blkalloc - Allocate a data block
 * @ip: the inode to allocate the data block for
 * @block: the block allocated
 *
 * Returns: 0 on success, -1 on failure
 */
int fs_blkalloc(fs_inode_t *ip, uint64 *block)
{
  osi_list_t *tmp;
  fs_sbd_t *sdp = ip->i_sbd;
  fs_rgrpd_t *rgd;
  uint32 goal;
  int same;
	
  for(tmp = sdp->sd_rglist.next; tmp != &sdp->sd_rglist; tmp = tmp->next){
    rgd = osi_list_entry(tmp, fs_rgrpd_t, rd_list);

    if(!rgd){
      pp_print(PPVH, "fs_blkalloc:  Bad rgrp list!\n");
      return -1;
    }

    if(fs_rgrp_read(rgd)){
      pp_print(PPVH, "fs_blkalloc:  Unable to read rgrp.\n");
      return -1;
    }

    if(!rgd->rd_rg.rg_free){
      fs_rgrp_relse(rgd);
      continue;
    }

    same = (rgd->rd_ri.ri_addr == ip->i_di.di_goal_rgrp);
    goal = (same) ? ip->i_di.di_goal_dblk : 0;
	
    *block = fs_blkalloc_internal(rgd, goal,
			       GFS_BLKST_FREE, GFS_BLKST_USED, TRUE);
	
    if (!same){
      ip->i_di.di_goal_rgrp = rgd->rd_ri.ri_addr;
      ip->i_di.di_goal_mblk = 0;
    }
    ip->i_di.di_goal_dblk = *block;
	
    *block += rgd->rd_ri.ri_data1;

    rgd->rd_rg.rg_free--;
	
    gfs_rgrp_out(&rgd->rd_rg, BH_DATA(rgd->rd_bh[0]));
    if(fs_write_buf(sdp, rgd->rd_bh[0], 0)){
      pp_print(PPVH, "Unable to write out rgrp block #%"PRIu64".\n",
	       BH_BLKNO(rgd->rd_bh[0]));
      fs_rgrp_relse(rgd);
      return -1;
    }
    fs_rgrp_relse(rgd);
    return 0;
  }

  return 1;
}
	
	
/**
 * fs_metaalloc - Allocate a metadata block to a file
 * @ip:  the file
 * @block: the block allocated
 *
 * Returns: 0 on success, -1 on failure
 */
int fs_metaalloc(fs_inode_t *ip, uint64 *block)
{
  osi_list_t *tmp;
  fs_sbd_t *sdp = ip->i_sbd;
  fs_rgrpd_t *rgd;
  uint32 goal;
  int same;
  int error = 0;

  /* ATTENTION -- maybe we should try to allocate from goal rgrp first */
  for(tmp = sdp->sd_rglist.next; tmp != &sdp->sd_rglist; tmp = tmp->next){
    rgd = osi_list_entry(tmp, fs_rgrpd_t, rd_list);

    if(!rgd){
      pp_print(PPH, "fs_metaalloc:  Bad rgrp list!\n");
      return -1;
    }

    if(fs_rgrp_read(rgd)){
      pp_print(PPVH, "fs_metaalloc:  Unable to read rgrp.\n");
      return -1;
    }

    same = (rgd->rd_ri.ri_addr == ip->i_di.di_goal_rgrp);
    goal = (same) ? ip->i_di.di_goal_mblk : 0;
	
    if (!rgd->rd_rg.rg_freemeta){
      error = clump_alloc(rgd, goal);
      if (error){
	fs_rgrp_relse(rgd);
	continue;
      }
    }


    if(!rgd->rd_rg.rg_freemeta){
      fs_rgrp_relse(rgd);
      continue;
    }
    *block = fs_blkalloc_internal(rgd, goal,
			       GFS_BLKST_FREEMETA, GFS_BLKST_USEDMETA, TRUE);
	
    if (!same){
      ip->i_di.di_goal_rgrp = rgd->rd_ri.ri_addr;
      ip->i_di.di_goal_dblk = 0;
    }
    ip->i_di.di_goal_mblk = *block;
	
    *block += rgd->rd_ri.ri_data1;
	
    rgd->rd_rg.rg_freemeta--;
    rgd->rd_rg.rg_usedmeta++;
  
    gfs_rgrp_out(&rgd->rd_rg, BH_DATA(rgd->rd_bh[0]));
    fs_write_buf(sdp, rgd->rd_bh[0], 0);
    fs_rgrp_relse(rgd);
    /* if we made it this far, then we are ok */
    return 0;
  }

  return -1;
}
