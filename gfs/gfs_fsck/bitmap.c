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
#include "fs_rgrp.h"

#include "bitmap.h"

#define DIV_RU(x, y) (((x) + (y) - 1) / (y))

/**
 * allocate_bitmaps
 * @sdp:
  *
 */
int allocate_bitmaps(fs_sbd_t *sdp)
{
  bitmap_list_t *bl;
  fs_rgrpd_t *rgd;
  osi_list_t *tmp;
  uint32 count = 0;

  for (tmp = sdp->sd_rglist.next; tmp != &sdp->sd_rglist; tmp = tmp->next){
    rgd = osi_list_entry(tmp, fs_rgrpd_t, rd_list);
    count += DIV_RU(rgd->rd_ri.ri_bitbytes, 2);
  }

  pp_print(PPL, "Allocating %.1f MB of memory for bitmaps\n", 
	 (float)count / (1024.0 * 1024.0));

  for (tmp = sdp->sd_rglist.next; tmp != &sdp->sd_rglist; tmp = tmp->next){
    rgd = osi_list_entry(tmp, fs_rgrpd_t, rd_list);
    bl = (bitmap_list_t *)gfsck_zalloc(sizeof(bitmap_list_t));
    bl->bm = gfsck_zalloc(DIV_RU(rgd->rd_ri.ri_bitbytes, 2));
    bl->rgd = rgd;
    osi_list_add(&bl->list, &sdp->sd_bitmaps);
  }
  
  return 0;
}



/**
 * free_bitmaps
 * @sdp:
  *
 */
int free_bitmaps(fs_sbd_t *sdp)
{
  bitmap_list_t *bl;

  while(!osi_list_empty(&sdp->sd_bitmaps)){
    bl = osi_list_entry(sdp->sd_bitmaps.next, bitmap_list_t, list);
    gfsck_free(bl->bm);
    osi_list_del(&bl->list);
    gfsck_free(bl);
  }
  
  return 0;
}


/**
 * get_bitmap - get value from internal bitmap
 * @sdp: super block
 * @blkno: block number relative to file system
 *
 * This function gets the value of a bit of the
 * internal bitmap.  This is NOT the file system bitmap,
 * but rather the bitmap built up by gfsck.
 * State values for a block in the internal bitmap are:
 *  unset (0)
 *  set   (1)
 *
 * Returns: state on success, -1 on error
 */
int get_bitmap(fs_sbd_t *sdp, uint64 blkno,fs_rgrpd_t *rgd ){
  osi_list_t	*tmp;
  bitmap_list_t *bl = NULL;
  uint64	rgrp_blk;
  char		*byte;
  int		bit, found = 0, local_rgd = 0;

  if(rgd == NULL) {
    local_rgd = 1;
    rgd = fs_blk2rgrpd(sdp, blkno);
  }
  if(fs_rgrp_read(rgd)){
    pp_print(PPVH, "Failed to read rgrp.\n");
    return -1;
  }

  /* FIXME: This pushed this fxn to O(n) - which kinda sucks for a large
     fs in pass7 - would be nice to get it to O(1) or at least O(ln n) */
  for (tmp = sdp->sd_bitmaps.next; tmp != &sdp->sd_bitmaps; tmp = tmp->next){
    bl = osi_list_entry(tmp, bitmap_list_t, list);
    if (bl->rgd == rgd){
      found = 1;
      break;
    }
  }

  if (!found)
    goto fail;

  rgrp_blk = blkno - rgd->rd_ri.ri_data1;

  byte = bl->bm + (rgrp_blk / 8);
  bit = rgrp_blk % 8;

  if(local_rgd) {
    fs_rgrp_relse(rgd);
  }
  return ((*byte >> bit) & 0x01);

 fail:
  if(local_rgd) {
    fs_rgrp_relse(rgd);
  }
  die("Unable to get internal bitmap entry for block #%"PRIu64"\n",
      blkno);
}


/**
 * set_bitmap - set value of internal bitmap
 * @sdp: super block
 * @blkno: block number relative to file system
 * @state: 0 to unset, 1 to set
 *
 * This function sets the value of a bit of the
 * internal bitmap.  This is NOT the file system bitmap,
 * but rather the bitmap built up by gfsck.
 *
 * Returns: 0 on success, -1 on error
 */
int set_bitmap(fs_sbd_t *sdp, uint64 blkno, int state){
  fs_rgrpd_t	*rgd = fs_blk2rgrpd(sdp, blkno);
  osi_list_t	*tmp;
  bitmap_list_t *bl = NULL;
  uint64	rgrp_blk;
  char		*byte;
  int		bit, found = 0;

  if(state != 0 && state != 1){
    return -1;
  }

  if(fs_rgrp_read(rgd)){
    pp_print(PPVH, "set_bitmap:  Failed to read rgrp.\n");
    return -1;
  }

  for (tmp = sdp->sd_bitmaps.next; tmp != &sdp->sd_bitmaps; tmp = tmp->next){
    bl = osi_list_entry(tmp, bitmap_list_t, list);
    if (bl->rgd == rgd)
    {
      found = 1;
      break;
    }
  }

  if (!found)
    goto fail;

  rgrp_blk = blkno - rgd->rd_ri.ri_data1;

  byte = bl->bm + (rgrp_blk / 8);
  bit = rgrp_blk % 8;

  if(state){
    if ((*byte >> bit) & 0x01)
      goto fail;

    *byte |= (0x01 << bit);
  } else {
    if(!((*byte >> bit) & 0x01))
      goto fail;

    *byte &= ~(0x01 << bit);
  }
  
  fs_rgrp_relse(rgd);
  return 0;

 fail:
  fs_rgrp_relse(rgd);
  return -1;
}
