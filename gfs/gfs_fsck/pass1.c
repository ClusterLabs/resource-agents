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


#include "pass1.h"
/**
 * pass_1
 * @sdp: superblock
 *
 * This pass is designed to look over the file system bitmaps for any
 * blocks marked as meta-data.  The block is then read and checked for
 * a meta header.  If not present, the bitmap for the block is cleared.
 *
 * The function then checks if the block is a dinode.  If so, basic
 * checks and corrections are made.
 *
 * Returns: 0 on success, -1 on error, 1 on restart request
 */
int pass_1(fs_sbd_t *sdp)
{
  osi_buf_t *bh;
  osi_list_t *tmp;
  fs_rgrpd_t *rgd;
  uint64 block;
  int cnt = 0;  
  int count = 0, first;
  fs_inode_t *ip;
  int prev_prcnt = -1, prcnt = 0;

  ip = (fs_inode_t *)gfsck_zalloc(sizeof(fs_inode_t));
  ip->i_sbd = sdp;
  
  for (tmp = sdp->sd_rglist.next; tmp != &sdp->sd_rglist; tmp = tmp->next){
    prcnt = (int)(100.0 * ((float)cnt / (float)sdp->sd_rgcount));
    if(prev_prcnt != prcnt){
      pp_print(PPL, "Pass 1:  %d %% \n", prcnt);
      prev_prcnt = prcnt;
    }
    cnt++;

    rgd = osi_list_entry(tmp, fs_rgrpd_t, rd_list);

    
    if(fs_rgrp_read(rgd)){
      gfsck_free(ip);
      return -1;
    }

    first = 1;

    while (1) {
      /* "block" is relative to the entire file system */      
      if(next_rg_meta(rgd, &block, first))
	break;

      first = 0;

      if(fs_get_and_read_buf(sdp, block, &bh, 0)){
	pp_print(PPVH, "Unable to retrieve block #%"PRIu64"\n", block);
	fs_rgrp_relse(rgd);
	gfsck_free(ip);
	return -1;
      }

      if (check_meta(bh, 0)){
	pp_print(PPN, "Bad meta magic for block #%"PRIu64"\n", block);
	if(query("\tClear the bitmap entry for block %"PRIu64"? (y/n): ", block)){
	  if(fs_set_bitmap(sdp, block, GFS_BLKST_FREE)){
	    pp_print(PPVH, "Bitmap entry for block #%"PRIu64" remains.\n",
		     block);
	  } else {
	    pp_print(PPN, "\tBitmap entry for block #%"PRIu64" cleared.\n",
		     block);
	  }
	} else {
	  pp_print(PPH, "\tBitmap entry for block #%"PRIu64" remains.\n",
		   block);
	}
	fs_relse_buf(sdp, bh);
	continue;
      }

      if(check_meta(bh, GFS_METATYPE_DI)){
	fs_relse_buf(sdp, bh);
	continue;
      }

      /* dinode checks */
      fs_relse_buf(sdp, bh);

      ip->i_num.no_addr = ip->i_num.no_formal_ino = block;
      memset(&ip->i_di, 0, sizeof(struct gfs_dinode));
      if(fs_copyin_dinode(ip)){
	pp_print(PPVH, "Unable to retrieve on-disk inode data.\n");
	continue;
      }

      if (ip->i_di.di_flags & GFS_DIF_UNUSED){
	/* Clear these in pass 2 */
	continue;
      }
      count++;

      if (ip->i_di.di_num.no_addr != block){
	pp_print(PPN, "Bad dinode Address.  Found %"PRIu64", "
		 "Expected %"PRIu64"\n",
		 ip->i_di.di_num.no_addr, block);
	if(query("\tSet dinode's address to it's block number? (y/n): ")){
	  ip->i_di.di_num.no_addr = ip->i_di.di_num.no_formal_ino = block;
	  if(fs_copyout_dinode(ip)){
	    pp_print(PPVH, "Bad dinode address can not be reset.\n");
	    fs_rgrp_relse(rgd);
	    return -1;
	  } else {
	    pp_print(PPVH, "Bad dinode address reset.\n");
	  }
	} else {
	  pp_print(PPH, "Address not set.\n");
	  continue;
	}
      }

      if (ip->i_di.di_type != GFS_FILE_REG && ip->i_di.di_type != GFS_FILE_DIR &&
	  ip->i_di.di_type != GFS_FILE_LNK && ip->i_di.di_type != GFS_FILE_BLK &&
	  ip->i_di.di_type != GFS_FILE_CHR && ip->i_di.di_type != GFS_FILE_FIFO &&
	  ip->i_di.di_type != GFS_FILE_SOCK)
      {
	pp_print(PPN, "Dinode #%"PRIu64" has unknown type (%u)\n",
		 ip->i_di.di_num.no_addr, ip->i_di.di_type);
	if(query("\tFix bad dinode type? (y/n): ")){
	  pp_print(PPVH,"Function not implemented.\n");
	  /* once implemented, remove continue statement */
	  continue;
	} else {
	  pp_print(PPH, "Bad dinode type remains.\n");
	  continue;
	}
      }

      if (ip->i_di.di_height < compute_height(sdp, ip->i_di.di_size)){
	pp_print(PPN, "Dinode #%"PRIu64" has bad height  "
		 "Found %u, Expected >= %u\n",
		 ip->i_di.di_num.no_addr, ip->i_di.di_height,
		 compute_height(sdp, ip->i_di.di_size));
	if(query("\tFix bad dinode height? (y/n): ")){
	  pp_print(PPVH, "Function not implemented.\n");
	  pp_print(PPVH, "Height not set.\n");
	  /* once implemented, remove continue statement */
	  continue;
	} else {
	  pp_print(PPH, "Height not set.\n");
	  continue;
	}
      }

      if (ip->i_di.di_type == GFS_FILE_DIR && (ip->i_di.di_flags & GFS_DIF_EXHASH))
      {
	if ((1 << ip->i_di.di_depth) * sizeof(uint64) != ip->i_di.di_size)
	{
	  pp_print(PPN, "Directory dinode #%"PRIu64" has bad depth.  "
		   "Found %u, Expected %u\n",
		   ip->i_di.di_num.no_addr, ip->i_di.di_depth,
		   (1 >> (ip->i_di.di_size/sizeof(uint64))));
	  if(query("\tFix bad dir dinode depth? (y/n): ")){
	    pp_print(PPVH, "\tFunction not implemented.\n");
	    pp_print(PPVH, "\tDepth not set.\n");
	    /* once implemented, remove continue statement */
	    continue;
	  } else {
	    pp_print(PPH, "\tDepth not set.\n");
	    continue;
	  }
	}
      }
    }
       
    fs_rgrp_relse(rgd);
  }
  gfsck_free(ip);

  pp_print(PPN, "Pass 1:  %d dinodes found\n", count);
  return 0;
}





