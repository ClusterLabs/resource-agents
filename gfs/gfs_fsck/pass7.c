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
#include "bitmap.h"
#include "interactive.h"
#include "util.h"
#include "fs_bio.h"
#include "fs_bits.h"
#include "fs_dir.h"
#include "fs_inode.h"
#include "fs_rgrp.h"

#include "pass7.h"

static int restart;
/**
 * check_block_conflicts
 * @sdp:
 *
 *
 */
static int check_block_conflicts(fs_inode_t *ip)
{
  fs_sbd_t *sdp = ip->i_sbd;

  osi_list_t metalist[GFS_MAX_META_HEIGHT];
  osi_list_t *list, *tmp;
  osi_buf_t *bh;
  uint32 height = ip->i_di.di_height;
  uint64 leaf_no, *ptr;
  int top, bottom, head_size;
  int error, i;


  if (fs_is_stuffed(ip))
    goto leaves;


  for (i = 0; i < GFS_MAX_META_HEIGHT; i++)
    osi_list_init(&metalist[i]);


  /* create metalist for each level */

  error = build_metalist(ip, &metalist[0]);
  if (error){
    pp_print(PPVH, "bad metadata tree for dinode %"PRIu64"\n",
	     ip->i_di.di_num.no_addr);
    return -1;
  }


  /* check indirect blocks */

  top = 1;
  bottom = height - 1;

  while (top <= bottom){
    list = &metalist[top];

    for (tmp = list->next; tmp != list; tmp = tmp->next)
    {
      bh = osi_list_entry(tmp, osi_buf_t, b_list);
      error = set_bitmap(sdp, BH_BLKNO(bh),1);
      if (error){
	pp_print(PPVH, "Indirect block #%"PRIu64" belongs "
		 "to multiple files.\n",
		 BH_BLKNO(bh));
	return -1;
      }
    }
    top++;
  }


  /* check data blocks */

  list = &metalist[height - 1];

  for (tmp = list->next; tmp != list; tmp = tmp->next)
  {
    bh = osi_list_entry(tmp, osi_buf_t, b_list);
    
    head_size = (height != 1 ? sizeof(struct gfs_indirect) : sizeof(struct gfs_dinode));
    ptr = (uint64 *)(bh->b_data + head_size);

    for ( ; (char *)ptr < (bh->b_data + bh->b_size); ptr++)
    {
      if (!*ptr)
        continue;
      
      error = set_bitmap(sdp, gfs64_to_cpu(*ptr),1);
      if (error){
	pp_print(PPVH, "Data block #%"PRIu64" belongs to multiple files.\n",
		 (uint64)gfs64_to_cpu(*ptr));
	return -1;
      }
    }
  }


  /* free metalists */

  for (i = 0; i < GFS_MAX_META_HEIGHT; i++)
  {
    list = &metalist[i];
    while (!osi_list_empty(list))
    {
      bh = osi_list_entry(list->next, osi_buf_t, b_list);
      osi_list_del(&bh->b_list);
      fs_relse_buf(sdp, bh);
    }
  }


 leaves:  

  /* check leaf blocks of directories; leaf block pointers are only accessible
     through the hash table.  this code is basically copied from pass 3 */

  if (ip->i_di.di_type == GFS_FILE_DIR && (ip->i_di.di_flags & GFS_DIF_EXHASH))
  {
    uint32 index;
    uint64 old_leaf = 0;
    uint64 chain_leaf = 0;
    osi_buf_t *leaf_bh;
    struct gfs_leaf *leaf;

    for(index=0; index < (1 << ip->i_di.di_depth); index++){
      if(get_leaf_nr(ip, index, &leaf_no)){
	pp_print(PPVH, "Unable to get leaf block number.\n"
		 "\tDepth = %u\n"
		 "\tindex = %u\n",
		 ip->i_di.di_depth,
		 index);
	return -1;
      }
      if((old_leaf != leaf_no) && (set_bitmap(sdp, leaf_no, 1))){
	pp_print(PPVH, "Leaf block #%"PRIu64" belongs to multiple files.\n",
		 leaf_no);
	return -1;
      }

      chain_leaf = leaf_no;
      do{
	if(fs_get_and_read_buf(sdp, chain_leaf, &leaf_bh, 0)){
	  pp_print(PPVH, "Unable to perform fs_get_and_read_buf().\n");
	  return -1;
	}

	leaf = (struct gfs_leaf *)BH_DATA(leaf_bh);

	chain_leaf = gfs64_to_cpu(leaf->lf_next);

	fs_relse_buf(sdp, leaf_bh);
	if(chain_leaf){
	  if(set_bitmap(sdp, chain_leaf, 1)){
	    pp_print(PPVH, "Chained leaf block #%"PRIu64" belongs to "
		     "multiple files.\n",
		     chain_leaf);
	    return -1;
	  }
	} else {
	  break;
	}
      } while(1);
				    
      old_leaf = leaf_no;
    }
  }

  return 0;
}


/** Global functions **/


/**
 * pass_7
 * @sdp:
 *
 * Reconcile bitmap differences.
 */
int pass_7(fs_sbd_t *sdp)
{
  fs_rgrpd_t *rgd;
  osi_list_t *tmp;
  fs_inode_t *ip;
  uint64 block, x;
  int error, cnt = 0, count = 0, first, val1, val2, diffs = 0;  
  int prev_prcnt = -1, prcnt = 0;

  restart =0;
  ip = (fs_inode_t *)gfsck_zalloc(sizeof(fs_inode_t));
  ip->i_sbd = sdp;

  /* Part a */
  
  for (tmp = sdp->sd_rglist.next; tmp != &sdp->sd_rglist; tmp = tmp->next)
  {
    prcnt = (int)(100.0 * ((float)cnt / (float)sdp->sd_rgcount));
    if(prev_prcnt != prcnt){
      pp_print(PPL, "Pass 7a:  %d %% \n", prcnt);
      prev_prcnt = prcnt;
    }
	       
    cnt++;

    rgd = osi_list_entry(tmp, fs_rgrpd_t, rd_list);

    error = fs_rgrp_read(rgd);
    if (error)
      return -1;
    
    first = 1;

    while (1){
      error = next_rg_metatype(rgd, &block, GFS_METATYPE_DI, first);
      if (error)
        break;

      first = 0;

      ip->i_num.no_addr = ip->i_num.no_formal_ino = block;
      if(fs_copyin_dinode(ip)){
	die("Unable to retrieve inode data from disk.\n");
      }

      if (ip->i_di.di_flags & GFS_DIF_UNUSED){
	die("found unused dinode!  Should be cleared in pass 2.\n");
      }

      count++;

      error = check_block_conflicts(ip);
      if (error){
	pp_print(PPN, "%s (#%"PRIu64") has a block conflict "
		 "with another file/directory\n", 
		 (ip->i_di.di_type == GFS_FILE_DIR) ?
		 "Directory":
		 "File",
		 ip->i_num.no_addr);
	if(query("Remove %s #%"PRIu64"? (y/n)",
		 (ip->i_di.di_type == GFS_FILE_DIR) ?
		 "Directory":
		 "File",
		 ip->i_num.no_addr)){
	  if(fs_remove(ip)){
	    pp_print(PPVH, "Unable to remove %s #%"PRIu64"\n",
		     (ip->i_di.di_type == GFS_FILE_DIR) ?
		     "Directory":
		     "File",
		     ip->i_num.no_addr);
	    return -1;
	  }
	} else {
	  pp_print(PPN, "Block conflict remains.\n");
	}
      }
    }
   
    fs_rgrp_relse(rgd);
  }

  gfsck_free(ip);
  
  /* Part b */
  
  cnt = 0;
  for (tmp = sdp->sd_rglist.next; tmp != &sdp->sd_rglist; tmp = tmp->next)
  {
    prcnt = (int)(100.0 * ((float)cnt / (float)sdp->sd_rgcount));
    if(prev_prcnt != prcnt){
      pp_print(PPL, "Pass 7b:  %d %% \n", prcnt);
      prev_prcnt = prcnt;
    }
    cnt++;

    rgd = osi_list_entry(tmp, fs_rgrpd_t, rd_list);

    error = fs_rgrp_read(rgd);
    if (error)
      return -1;
    
    diffs = 0;
    for (x = rgd->rd_ri.ri_data1;
	 x < rgd->rd_ri.ri_data1 + rgd->rd_ri.ri_data;
	 x++){
      val1 = fs_get_bitmap(sdp, x, rgd);
      val2 = get_bitmap(sdp, x, rgd);

      if (!val1 && !val2)
	continue;

      if (val2 && (val1 == GFS_BLKST_USED || val1 == GFS_BLKST_USEDMETA))
	continue;

      if (!val2 && (val1 == GFS_BLKST_FREEMETA))
	continue;

      if (rgd->rd_ri.ri_data1 == sdp->sd_first_data_block &&
	  x <= sdp->sd_sb.sb_root_di.no_addr)
	continue;

      pp_print(PPN, "On-disk bitmap and calculated bitmap differ."
	       " block %"PRIu64", %u,%u\n",
	       x, val1, val2);
      diffs++;
      if(query("Fix bitmap inconsistency? (y/n) ")){
	if(!val2){
	  if(fs_set_bitmap(sdp, x, GFS_BLKST_FREE)){
	    pp_print(PPVH, "Unable to correct bitmap discrepancy.\n");
	  } else {
	    osi_buf_t *bh;
	    if(!fs_get_buf(sdp, x, &bh)){ /* clear block */
	      fs_write_buf(sdp, bh, 0);
	      fs_relse_buf(sdp, bh);
	    }
	    
	    pp_print(PPN, "Bitmap entry corrected.\n");
	  }
	} else {
	  osi_buf_t *bh;
	  uint32 magic;

	  pp_print(PPN, "Found block #%"PRIu64" in FTW, "
		   "but not in fs bitmap.\n", x);
	  fs_get_and_read_buf(sdp, x, &bh, 0);
	  magic = ((struct gfs_meta_header *)BH_DATA((bh)))->mh_magic;
	  magic = gfs32_to_cpu(magic);
	  if(magic != GFS_MAGIC){
	    pp_print(PPN, "Block #%"PRIu64" is a data block.\n", x);
	    if(((val1 == GFS_BLKST_FREEMETA) &&
		fs_set_bitmap(sdp, x, GFS_BLKST_FREE)) ||
	       fs_set_bitmap(sdp, x, GFS_BLKST_USED)){
	      pp_print(PPVH, "Unable to correct FS bitmap for block "
		       "#%"PRIu64"\n", x);
	      fs_relse_buf(sdp, bh);
	      fs_rgrp_relse(rgd);
	      return -1;
	    } else {
	      restart = 1;
	      pp_print(PPN, "FS bitmap corrected.\n");
	    }
	  } else {
	    pp_print(PPN, "Block #%"PRIu64" is a metadata block.\n", x);
	    if(((val1 == GFS_BLKST_FREE) &&
		fs_set_bitmap(sdp, x, GFS_BLKST_FREEMETA)) ||
	       fs_set_bitmap(sdp, x, GFS_BLKST_USEDMETA)){
	      pp_print(PPVH, "Unable to correct FS bitmap for block "
		       "#%"PRIu64"\n", x);
	      fs_relse_buf(sdp, bh);
	      fs_rgrp_relse(rgd);
	      return -1;
	    } else {
	      restart = 1;
	      pp_print(PPN, "FS bitmap corrected.\n");
	    }
	  }
	  fs_relse_buf(sdp, bh);
	}
      } else {
	pp_print(PPH, "Bitmap discrepancy remains.\n");
      }
    }
    if (diffs){
      pp_print(PPN, "Bitmaps diffs on %u blocks in rg %"PRIu64"\n",
	       diffs, rgd->rd_ri.ri_data1);
    }

    if(fs_rgrp_recount(rgd)){
      die("Unable to recount rgrp blocks.\n");
    }
    /* write out the rgrp */
    gfs_rgrp_out(&rgd->rd_rg, BH_DATA(rgd->rd_bh[0]));
    fs_write_buf(sdp, rgd->rd_bh[0], 0);
    fs_rgrp_verify(rgd);
    fs_rgrp_relse(rgd);
  }

  return restart;
}
