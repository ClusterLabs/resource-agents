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
#include "fs_dir.h"
#include "fs_inode.h"
#include "fs_rgrp.h"

#include "pass2.h"


/**
 * check_extended_leaf_eattr
 * @ip
 * @el_blk: block number of the extended leaf
 *
 * An EA leaf block can contain EA's with pointers to blocks
 * where the data for that EA is kept.  Those blocks still
 * have the gfs meta header of type GFS_METATYPE_EA
 *
 * Returns: 0 if correct[able], -1 if removal is needed
 */
static int check_extended_leaf_eattr(fs_inode_t *ip, uint64 el_blk){
  osi_buf_t *el_buf;
  fs_sbd_t *sdp = ip->i_sbd;

  pp_print(PPVL, "Checking EA extended leaf block #%"PRIu64".\n", el_blk);

  if(check_range(sdp, el_blk)){
    pp_print(PPN, "EA extended leaf block #%"PRIu64" "
		   "is out of range.\n",
		   el_blk);
    return -1;
  }

  if(fs_get_and_read_buf(sdp, el_blk, &el_buf, 0)){
    pp_print(PPVH, "Unable to extended leaf block.\n");
    return -1;
  }

  if(check_meta(el_buf, GFS_METATYPE_EA)){
    pp_print(PPN, "EA extended leaf block has incorrect type.\n");
    fs_relse_buf(sdp, el_buf);
    return -1;
  }

  fs_relse_buf(sdp, el_buf);
  return 0;
}


static int remove_eattr(fs_sbd_t *sdp, osi_buf_t *leaf_bh,
			struct gfs_ea_header *curr,
			struct gfs_ea_header *prev){
  pp_print(PPN, "Removing EA located in block #%"PRIu64".\n",
	   BH_BLKNO(leaf_bh));
  if(!prev){
    curr->ea_type = GFS_EATYPE_UNUSED;
  } else {
    prev->ea_rec_len = cpu_to_gfs32(gfs32_to_cpu(curr->ea_rec_len) + 
				    gfs32_to_cpu(prev->ea_rec_len));
  }
  if(fs_write_buf(sdp, leaf_bh, 0)){
    pp_print(PPVH, "Unable to perform fs_write_buf().\n");
    pp_print(PPVH, "EA removal failed.\n");
    return -1;
  }
  return 0;
}


/**
 * check_leaf_eattr
 * @ip: the inode the eattr comes from
 * @leaf_blk: block number of the leaf
 *
 * Returns: number of EA blocks if correct[able], -1 if removal is needed
 */
static int check_leaf_eattr(fs_inode_t *ip, uint64 leaf_blk){
  int i, val;
  int error=0;
  int rtn_blks = 1, extended_blks = 0;
  char ea_name[256];
  osi_buf_t *leaf_bh = NULL;
  fs_sbd_t *sdp = ip->i_sbd;
  struct gfs_ea_header *ea_hdr = NULL;
  struct gfs_ea_header *ea_hdr_prev = NULL;

  pp_print(PPVL, "Checking EA leaf block #%"PRIu64".\n", leaf_blk);

  if(check_range(sdp, leaf_blk)){
    pp_print(PPN, "EA leaf block #%"PRIu64" is out of range.\n",
		   leaf_blk);
    return -1;
  }

  if(fs_get_and_read_buf(sdp, leaf_blk, &leaf_bh, 0)){
    pp_print(PPVH, "Unable to read EA leaf block #%"PRIu64".\n", leaf_blk);
    return -1;
  }

  if(check_meta(leaf_bh, GFS_METATYPE_EA)){
    pp_print(PPN, "EA leaf block has incorrect type.\n");
    error = -1;
    goto fail;
  }

  ea_hdr = (struct gfs_ea_header *)(BH_DATA(leaf_bh) + 
				    sizeof(struct gfs_meta_header));
  while(1){
    if(!ea_hdr->ea_name_len){
      pp_print(PPN, "EA has name length == 0\n");
      if(remove_eattr(sdp, leaf_bh, ea_hdr, ea_hdr_prev)){
	error = -1;
	goto fail;
      }
      goto move_on;
    }

    memset(ea_name, 0, sizeof(ea_name));
    strncpy(ea_name, (char *)ea_hdr + sizeof(struct gfs_ea_header), ea_hdr->ea_name_len);

    if(!GFS_EATYPE_VALID(ea_hdr->ea_type) &&
       ((ea_hdr_prev) || (!ea_hdr_prev && ea_hdr->ea_type))){
      pp_print(PPN, "EA (%s) type is invalid (%d > %d).\n",
	       ea_name, ea_hdr->ea_type, GFS_EATYPE_LAST);
      if(remove_eattr(sdp, leaf_bh, ea_hdr, ea_hdr_prev)){
	error = -1;
	goto fail;
      }
      goto move_on;
    }

    extended_blks=0;
    if(ea_hdr->ea_num_ptrs){
      uint32 avail_size;
      int max_ptrs;
      uint64 *ea_data_ptr;

      avail_size = sdp->sd_sb.sb_bsize - sizeof(struct gfs_meta_header);
      max_ptrs = (gfs32_to_cpu(ea_hdr->ea_data_len)+avail_size-1)/avail_size;

      if(max_ptrs > ea_hdr->ea_num_ptrs){
	pp_print(PPN, "EA (%s) has incorrect number of pointers.\n", ea_name);
	pp_print(PPN,
		 "  Required:  %d\n"
		 "  Reported:  %d\n",
		 max_ptrs, ea_hdr->ea_num_ptrs);
	if(remove_eattr(sdp, leaf_bh, ea_hdr, ea_hdr_prev)){
	  error = -1;
	  goto fail;
	}
	goto move_on;
      } else {
	pp_print(PPVL,
		 "  Pointers Required: %d\n"
		 "  Pointers Reported: %d\n",
		 max_ptrs,
		 ea_hdr->ea_num_ptrs);
      }

      ea_data_ptr =
	((uint64 *)((char *)ea_hdr + sizeof(struct gfs_ea_header) +
		    ((ea_hdr->ea_name_len + 7) & ~7)));
	
      extended_blks=0;
      /* It is possible when a EA is shrunk to have ea_num_ptrs be **
      ** greater than the number required for data.  In this case, **
      ** the EA code leaves the blocks there for reuse...........  */
      for(i = 0; i < ea_hdr->ea_num_ptrs; i++){
	if(check_extended_leaf_eattr(ip, gfs64_to_cpu(*ea_data_ptr))){
	  if(remove_eattr(sdp, leaf_bh, ea_hdr, ea_hdr_prev)){
	    error = -1;
	    goto fail;
	  }
	  goto move_on;
	}
	extended_blks++;
	ea_data_ptr++;
      }
    }

    rtn_blks += extended_blks;

  move_on:
    if(ea_hdr->ea_flags & GFS_EAFLAG_LAST){
      /* ATTENTION -- better equal the end of the block */
      break;
    }
    /* ATTENTION -- be sure this doesn't go beyond the end */
    ea_hdr_prev = ea_hdr;
    ea_hdr = (struct gfs_ea_header *)((char *)(ea_hdr) +	
				  gfs32_to_cpu(ea_hdr->ea_rec_len));
  }

  val = fs_get_bitmap(sdp, BH_BLKNO(leaf_bh), NULL);

  if(val != GFS_BLKST_USEDMETA){
    pp_print(PPN, "Bitmap incorrect for EA leaf block #%"PRIu64"\n",
	     BH_BLKNO(leaf_bh));
    pp_print(PPL,
	     "\tExpected: GFS_BLKST_USEDMETA\n"
	     "\tReceived: %s\n",
	     (val == GFS_BLKST_FREEMETA)? "GFS_BLKST_FREEMETA" :
	     (val == GFS_BLKST_FREE)? "GFS_BLKST_FREE" :
	     (val == GFS_BLKST_USED)? "GFS_BLKST_USED" :
	     "UNKNOWN");

    if(query("Fix bitmap? (y/n) ")){
      if(fs_set_bitmap(sdp, BH_BLKNO(leaf_bh), GFS_BLKST_USEDMETA)){
	pp_print(PPVH, "Unable to correct bitmap for EA leaf block #%"PRIu64".\n",
		 BH_BLKNO(leaf_bh));
      } else {
	pp_print(PPN, "Bitmap corrected.\n");
      }
    } else {
      pp_print(PPN, "Incorrect bitmap value remains.\n");
    }
  }

 fail:
  fs_relse_buf(sdp, leaf_bh);
  if(error){
    return error;
  } else {
    return rtn_blks;
  }
}


/**
 * shift_pointers_up
 * @sdp
 * @buf
 * @ptr
 *
 * This function removes a pointer to an EA leaf block by shifting
 * all following pointers up.  It is up to the caller of this function
 * to be sure to write the contents to disk when finished.
 */
static void shift_pointers_up(fs_sbd_t *sdp, osi_buf_t *buf, uint64 *ptr){
  uint64 *init_ptr, *next_ptr;
  uint64 *end;

  init_ptr = (uint64 *)(BH_DATA(buf) + sizeof(struct gfs_indirect));
  end = init_ptr + ((sdp->sd_sb.sb_bsize - sizeof(struct gfs_indirect)) / 8);
  
  for(next_ptr = ptr+1; (next_ptr < end) && (*next_ptr); next_ptr++, ptr++){
    *ptr = *next_ptr;
  }
  *ptr = 0;
}


/**
 * check_indirect_eattr
 * @ip: the inode the eattr comes from
 * @indirect_block
 *
 * Returns: number of EA blocks if correct[able], -1 on error
 */
static int check_indirect_eattr(fs_inode_t *ip, uint64 indirect){
  int val = 0;
  int error = 0;
  int write_buf = 0;
  int rtn_blks = 1, ea_blks = 0;
  uint64 *ea_leaf_ptr, *end;
  osi_buf_t *indirect_buf;
  fs_sbd_t *sdp = ip->i_sbd;
  
  pp_print(PPVL, "Checking EA indirect block #%"PRIu64".\n", indirect);

  if(check_range(sdp, indirect)){
    pp_print(PPN, "EA indirect block #%"PRIu64" is out of range.\n",
		   indirect);
    return -1;
  }

  if(fs_get_and_read_buf(sdp, indirect, &indirect_buf, 0)){
    pp_print(PPVH, "Unable to read EA indirect block #%"PRIu64".\n", indirect);
    return -1;
  }

  if(check_meta(indirect_buf, GFS_METATYPE_IN)){
    pp_print(PPN, "EA indirect block has incorrect type.\n");
    error = -1;
    goto fail;
  }

  ea_leaf_ptr = (uint64 *)(BH_DATA(indirect_buf) + sizeof(struct gfs_indirect));
  end = ea_leaf_ptr + ((sdp->sd_sb.sb_bsize - sizeof(struct gfs_indirect)) / 8);

  while(*ea_leaf_ptr && (ea_leaf_ptr < end)){
    ea_blks=0;
    if((ea_blks = check_leaf_eattr(ip, gfs64_to_cpu(*ea_leaf_ptr))) < 0){
      shift_pointers_up(sdp, indirect_buf, ea_leaf_ptr);
      write_buf = 1;
    } else {
      rtn_blks += ea_blks;
    }
    ea_leaf_ptr++;
  }

  if(write_buf){
    if(fs_write_buf(sdp, indirect_buf, 0) < 0){
      pp_print(PPVH, "Unable to write out altered EA indirect block.\n");
      error = -1;
      goto fail;
    }
  }

  val = fs_get_bitmap(sdp, indirect, NULL);
  if(val != GFS_BLKST_USEDMETA){
    pp_print(PPN, "Bitmap incorrect for EA indirect block #%"PRIu64".\n", indirect);
    if(fs_set_bitmap(sdp, indirect, GFS_BLKST_USEDMETA)){
      pp_print(PPVH, "Unable to set bitmap to correct value.\n");
    } else {
      pp_print(PPN, "Bitmap corrected.\n");
    }
  }

 fail:
  fs_relse_buf(sdp, indirect_buf);
  if(error){
    return error;
  } else {
    return rtn_blks;
  }
}


/**
 * check_inode_eattr - check the EA's for a single inode
 * @ip: the inode whose EA to check
 *
 * Returns: number of EA blocks if correct[able], -1 on error
 */
static int check_inode_eattr(fs_inode_t *ip){
  int error = 0;

  if(!ip->i_di.di_eattr){
    return 0;
  }

  pp_print(PPL, "Extended attributes exist for dinode #%"PRIu64".\n",
	   ip->i_num.no_addr);

  if(ip->i_di.di_flags & GFS_DIF_EA_INDIRECT){
    error = check_indirect_eattr(ip, ip->i_di.di_eattr);
  } else {
    error = check_leaf_eattr(ip, ip->i_di.di_eattr);
  }

  return error;
}


/**
 * check_metatree
 * @ip:
 * @rgd:
 *
 */
static int check_metatree(fs_inode_t *ip)
{
  fs_sbd_t *sdp = ip->i_sbd;
  osi_list_t metalist[GFS_MAX_META_HEIGHT];
  osi_list_t *list, *tmp;
  osi_buf_t *bh;
  uint64 block, reg_blocks = 0, *ptr;
  uint32 height = ip->i_di.di_height, indir_blocks = 0, ea_blks = 0;
  int top, bottom, val, i, head_size;
  int restart = 0;

  for (i = 0; i < GFS_MAX_META_HEIGHT; i++)
    osi_list_init(&metalist[i]);

  /* create metalist for each level */

  if(build_metalist(ip, &metalist[0])){
    pp_print(PPVH, "check_metatree :: Unable to build meta list.\n");
    return -1;
  }

  /* check dinode block */

  val = fs_get_bitmap(sdp, ip->i_di.di_num.no_addr, NULL);

  if (val != GFS_BLKST_USEDMETA){
    pp_print(PPN, "Dinode #%"PRIu64" has bad FS bitmap value.  "
	     "Found %d, Expected %d\n",
	     ip->i_di.di_num.no_addr, val, GFS_BLKST_USEDMETA);
    if(query("\tSet bitmap to correct value? (y/n)")){
      if(!fs_get_bitmap(sdp, ip->i_di.di_num.no_addr, NULL)){
	/* can not handle 0 -> 3 directly */
	if(fs_set_bitmap(sdp, ip->i_di.di_num.no_addr, GFS_BLKST_FREEMETA)){
	  pp_print(PPVH, "Unable to perform fs_set_bitmap().\n");
	  pp_print(PPVH, "Bitmap retains incorrect value\n");
	}
      }
      if(fs_set_bitmap(sdp, ip->i_di.di_num.no_addr, GFS_BLKST_USEDMETA)){
	pp_print(PPVH, "Unable to perform fs_set_bitmap().\n");
	pp_print(PPVH, "Bitmap retains incorrect value\n");
      } else {
	pp_print(PPN, "\tBitmap value corrected\n");
      }
    } else {
      pp_print(PPH, "Bitmap retains incorrect value\n");
    }
  }

  if (!height)
    goto end;


  /* check indirect blocks */

  top = 1;
  bottom = height - 1;

  while (top <= bottom)
  {
    list = &metalist[top];

    for (tmp = list->next; tmp != list; tmp = tmp->next)
    {
      bh = osi_list_entry(tmp, osi_buf_t, b_list);
      val = fs_get_bitmap(sdp, BH_BLKNO(bh), NULL);

      if (val != GFS_BLKST_USEDMETA){
	pp_print(PPN, "Indirect block %"PRIu64" has bad FS bitmap value.  "
		 "Found %d, Expected %u\n",
		 BH_BLKNO(bh), val, GFS_BLKST_USEDMETA);
	if(check_meta(bh, GFS_METATYPE_IN)){
	  pp_print(PPVH, "Indirect block %"PRIu64" has bad meta info.\n",
		   BH_BLKNO(bh));
	  return -1;
	}
	if(query("\tSet bitmap to correct value? (y/n)")){
	  pp_print(PPL, "Changing bitmap value from %d to %d.\n",
		   fs_get_bitmap(sdp, BH_BLKNO(bh), NULL), GFS_BLKST_USEDMETA);
	  if(fs_set_bitmap(sdp, BH_BLKNO(bh), GFS_BLKST_USEDMETA)){
	    pp_print(PPVH, "Unable to perform fs_set_bitmap().\n");
	    pp_print(PPVH, "Bitmap retains incorrect value\n");
	  } else {
	    pp_print(PPN, "\tBitmap value corrected\n");
	  }
	} else {
	  pp_print(PPH, "Bitmap retains incorrect value\n");
	}
      }
      pp_print(PPVL, "\tIndirect block at %15"PRIu64" (%u).\n",
	       BH_BLKNO(bh), indir_blocks+1);      
      indir_blocks++;
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
      
      block =  gfs64_to_cpu(*ptr);
      
      if (check_range(ip->i_sbd, block)){
	pp_print(PPN, "Bad data block pointer (out of range)\n");
	if(query("\tReplace lost data with a hole? (y/n): ")){
	  /* set *ptr to 0 and write the buffer to disk */
	  *ptr = 0;
	  if(fs_write_buf(ip->i_sbd, bh, 0)){
	    pp_print(PPVH, "Unable to perform fs_write_buf().\n");
	    pp_print(PPVH, "Corrupted file metadata tree remains.\n");
	  } else {
	    pp_print(PPN, "\tData replaced.\n");
	  }
	} else {
	  pp_print(PPH, "Corrupted file metadata tree remains.\n");
	}
	continue;
      }

      val = fs_get_bitmap(sdp, block, NULL);
      
      if (ip->i_di.di_flags & GFS_DIF_JDATA){
	/* Attention -- journal block? */
	if (val != GFS_BLKST_USEDMETA){
	  osi_buf_t *data_bh;
	  pp_print(PPN, "Journal block %"PRIu64" has bad FS bitmap value.  "
		   "Found %d, Expected %d\n",
		   block, val, GFS_BLKST_USEDMETA);
	  if(fs_get_and_read_buf(ip->i_sbd, block, &data_bh, 0) || 
	     check_meta(data_bh, GFS_METATYPE_JD)){
	    if(!data_bh){
	      pp_print(PPN, "Unable to read block #%"PRIu64"\n", block);
	    }else{
	      pp_print(PPN, "Block #%"PRIu64" does not have "
		       "correct meta header.\n", block);
	      fs_relse_buf(sdp, data_bh);
	    }
	    if(query("\tRemove Data section? (y/n) ")){
	      pp_print(PPN, "Setting FS bitmap to 0.\n");
	      fs_set_bitmap(sdp, block, GFS_BLKST_FREE);
	      pp_print(PPN, "Replacing data with hole.\n");
	      *ptr = 0;
	      if(fs_write_buf(ip->i_sbd, bh, 0)){
		pp_print(PPVH, "Unable to perform fs_write_buf().\n");
		pp_print(PPVH, "Corrupted file metadata tree remains.\n");
	      } else {
		pp_print(PPN, "Data replaced.\n");
	      }
	    }
	    continue;
	  } else {
	    fs_relse_buf(sdp, data_bh);
	    if(query("\tSet bitmap to correct value? (y/n)")){
	      if(fs_set_bitmap(sdp, block, GFS_BLKST_USEDMETA)){
		pp_print(PPVH, "Unable to perform fs_set_bitmap().\n");
		pp_print(PPVH, "Bitmap retains incorrect value\n");
	      } else {
		pp_print(PPN, "\tBitmap value corrected\n");
	      }
	    } else {
	      pp_print(PPH, "Bitmap retains incorrect value\n");
	    }
	  }
	}
      }
      else
      {
	if (val != GFS_BLKST_USED){
	  pp_print(PPN, "Data block %"PRIu64" has bad bitmap value.  "
		   "Found %d, Expected %d\n",
		   block, val, GFS_BLKST_USED);
	  if(query("\tSet bitmap to correct value? (y/n)")){
	    if(fs_set_bitmap(sdp, block, GFS_BLKST_USED)){
	      pp_print(PPVH, "Unable to perform fs_set_bitmap().\n");
	      pp_print(PPVH, "Bitmap retains incorrect value\n");
	    } else {
	      pp_print(PPN, "\tBitmap value corrected\n");
	    }
	  } else {
	    pp_print(PPH, "Bitmap retains incorrect value\n");
	  }
	}
      }
      pp_print(PPVL, "\t%s block at %15"PRIu64" (%"PRIu64").\n",
	       (ip->i_di.di_flags & GFS_DIF_JDATA)? "Journaled Data":
	       "Data", block, reg_blocks+1);
      reg_blocks++;
    }
  }

 end:
  /* free metalists */

  for (i = 0; i < GFS_MAX_META_HEIGHT; i++)
  {
    list = &metalist[i];
    while (!osi_list_empty(list))
    {
      bh = osi_list_entry(list->next, osi_buf_t, b_list);
      osi_list_del(&bh->b_list);
      fs_relse_buf(ip->i_sbd, bh);
    }
  }
 
  /* check validity of leaf blocks and leaf chains */
  if (ip->i_di.di_type == GFS_FILE_DIR && (ip->i_di.di_flags & GFS_DIF_EXHASH)){
    uint32	index;
    uint64	old_leaf = 0, new_leaf = 0;
    osi_buf_t	*leaf_bh;
    struct gfs_leaf	leaf;
    fs_sbd_t	*sdp = ip->i_sbd;

    for(index=0; index < (1 << ip->i_di.di_depth); index++){
      if(get_leaf_nr(ip, index, &new_leaf)){
	pp_print(PPVH, "Unable to get leaf block number "
		 "for directory #%"PRIu64"\n", ip->i_di.di_num.no_addr);
	return -1;
      }

      if(old_leaf != new_leaf){

	do{
	  if(check_range(sdp, new_leaf)){
	    pp_print(PPN, "Leaf block #%"PRIu64" is out of range for "
		     "directory #%"PRIu64".\n",
		     new_leaf, ip->i_di.di_num.no_addr);
	    break;
	  }
	  if(fs_get_and_read_buf(sdp, new_leaf, &leaf_bh, 0)){
	    pp_print(PPVH, "Unable to read leaf block #%"PRIu64" for "
		     "directory #%"PRIu64".\n", 
		     new_leaf, ip->i_di.di_num.no_addr);
	    return -1;
	  }
	  if(check_meta(bh, GFS_METATYPE_LF)){
	    pp_print(PPN, "Bad meta header for leaf block #%"PRIu64".\n",
		     new_leaf);
	    fs_relse_buf(sdp, leaf_bh);
	    break;
	  }

	  /* Let's consider leaf blocks to fit in the reg_blocks catagory */
	  pp_print(PPVL, "\tLeaf block at %15"PRIu64" (%"PRIu64").\n",
		   new_leaf, reg_blocks+1);
	  reg_blocks++;
	  gfs_leaf_in(&leaf, BH_DATA(leaf_bh));
	  fs_relse_buf(sdp, leaf_bh);
	  if(!leaf.lf_next){
	    break;
	  }	  
	  new_leaf = leaf.lf_next;
	  pp_print(PPVL, "Leaf chain detected.\n");
	} while(1);
      }
      old_leaf = new_leaf;
    }
  }

  if((ea_blks = check_inode_eattr(ip)) < 0){
    osi_buf_t	*di_bh;
    ip->i_di.di_eattr = 0;
    if(fs_get_and_read_buf(sdp, ip->i_di.di_num.no_addr, &di_bh, 0)){
      pp_print(PPVH, "Unable to read dinode block.\n");
      pp_print(PPVH, "Bad EA reference remains.\n");
    } else {
      gfs_dinode_out(&ip->i_di, BH_DATA(di_bh));
      if(fs_write_buf(ip->i_sbd, di_bh, 0) < 0){
	pp_print(PPVH, "Unable to perform fs_write_buf().\n");
	pp_print(PPVH, "Bad EA reference remains.\n");
      } else {
	ea_blks = 0;
	pp_print(PPN, "Bad EA reference cleared.\n");
      }	
      fs_relse_buf(sdp, di_bh);
    }
  }

  /* check block count */
   
  if (ip->i_di.di_blocks != (1 + reg_blocks + indir_blocks + ea_blks)){
    pp_print(PPN, "Dinode #%"PRIu64" has bad block count.\n"
	     "  Found    :: %"PRIu64"\n"
	     "   Dinode   : 1\n"
	     "   Reg blks : %"PRIu64"\n"
	     "   Ind blks : %"PRIu64"\n"
	     "   EA blks  : %"PRIu64"\n"
	     "  Expected :: %"PRIu64"\n",
	     ip->i_num.no_addr,
	     (1 + reg_blocks + indir_blocks + ea_blks),
	     reg_blocks,
	     (uint64)indir_blocks,
	     (uint64)ea_blks,
	     ip->i_di.di_blocks);
	     
    if(query("\tCorrect block count? (y/n): ")){
      ip->i_di.di_blocks = (1 + reg_blocks + indir_blocks + ea_blks);
      fs_get_and_read_buf(ip->i_sbd, ip->i_di.di_num.no_addr, &bh, 0);
      gfs_dinode_out(&ip->i_di, BH_DATA(bh));
      if(fs_write_buf(ip->i_sbd, bh, 0) < 0){
	pp_print(PPVH, "Unable to perform fs_write_buf().\n");
	pp_print(PPVH, "Bad block count remains.\n");
      } else {
	pp_print(PPN, "\tBlock count modified.\n");
      }	
      fs_relse_buf(sdp, bh);
    } else {
      pp_print(PPH, "Bad block count remains.\n");
    }
  }

  return restart;
}  
  

/**
 * pass_2
 * @sdp: superblock
 *
 * This function goes through each dinode.  If it is unused, it is
 * cleared from the bitmap.  Otherwise, the metadata tree of that
 * dinode is checked - including extended attributes.
 *
 * Once this process is complete, all bitmap entries that are
 * GFS_BLKST_FREEMETA are cleared (GFS_BLKST_FREE).
 *
 *  Returns: 0 on success, -1 on error, 1 on restart request
 */
int pass_2(fs_sbd_t *sdp)
{
  fs_rgrpd_t *rgd;
  osi_list_t *tmp;
  fs_inode_t *ip;
  uint64 block; 
  uint64 total_reclaim = 0;
  uint32 reclaim = 0;
  int error, cnt = 0, count = 0, first;  
  int restart = 0;
  int prev_prcnt = -1, prcnt = 0;

  ip = (fs_inode_t *)gfsck_zalloc(sizeof(fs_inode_t));
  ip->i_sbd = sdp;

  
  for (tmp = sdp->sd_rglist.next; tmp != &sdp->sd_rglist; tmp = tmp->next){
    reclaim = 0;
    prcnt = (int)(100.0 * ((float)cnt / (float)sdp->sd_rgcount));
    if(prev_prcnt != prcnt){
      pp_print(PPL, "Pass 2:  %d %% \n", prcnt);
      prev_prcnt = prcnt;
    }
    cnt++;

    rgd = osi_list_entry(tmp, fs_rgrpd_t, rd_list);
    
    if(fs_rgrp_read(rgd))
      return -1;
    
    /* check used dinodes */
    first = 1;

    while (1){
      error = next_rg_metatype(rgd, &block, GFS_METATYPE_DI, first);
      if (error)
        break;

      first = 0;
      ip->i_num.no_addr = ip->i_num.no_formal_ino = block;

      if(fs_copyin_dinode(ip)){
	pp_print(PPVH, "Unable to copy in inode information from disk.\n");
	/* ATTENTION -- need to handle this error */
	continue;
      }

      if (ip->i_di.di_flags & GFS_DIF_UNUSED){
	/* clear bitmap of unused dinodes */
	if(fs_set_bitmap(sdp, ip->i_num.no_addr, GFS_BLKST_FREE)){
	  pp_print(PPVH, "Unable to convert bitmap entry for unused dinode "
		   "#%"PRIu64"\n",
		   ip->i_num.no_addr);
	} else {
	  pp_print(PPVL, "Bitmap entry cleared for unused dinode"
		   "#%"PRIu64"\n",
		   ip->i_num.no_addr);
	}
	continue;
      }

      count++;
      
      error = check_metatree(ip);
      switch(error){
      case 0:
	break;
      case 1:
	restart = 1;
	break;
      default:
	if (error < 0){
	  pp_print(PPN, "Dinode #%"PRIu64" has bad metatree.\n",
		   ip->i_num.no_addr);
	  if(query("\tFix bad metatree? (y/n) ")){
	    pp_print(PPVH, "Function not implemented.\n");
	    pp_print(PPVH, "Bad metatree remains for dinode %"PRIu64"\n",
		     ip->i_num.no_addr);
	  } else {
	    pp_print(PPH, "Bad metatree remains for dinode %"PRIu64"\n",
		     ip->i_num.no_addr);
	    /* attention - exit for now */
	    exit(EXIT_FAILURE);
	  }
	}
      }
    }

    /* clear bitmap of unused meta blocks */
    while(fs_blkalloc_internal(rgd, 0, GFS_BLKST_FREEMETA, 
			       GFS_BLKST_FREE, 1) != BFITNOENT);

    /* clear out all unused dinodes - bitmaps for unused dinodes were **
    ** cleared earlier............................................... */
    rgd->rd_rg.rg_freedi_list.no_formal_ino = 0;
    rgd->rd_rg.rg_freedi_list.no_addr = 0;


    reclaim += rgd->rd_rg.rg_freedi;
    reclaim += rgd->rd_rg.rg_freemeta;
    rgd->rd_rg.rg_free += reclaim;
    rgd->rd_rg.rg_freedi = 0;
    rgd->rd_rg.rg_freemeta = 0;

    if(fs_rgrp_recount(rgd)){
      die("Unable to recount rgrp blocks.\n");
    }

    /* write out the rgrp */
    gfs_rgrp_out(&rgd->rd_rg, BH_DATA(rgd->rd_bh[0]));
    fs_write_buf(sdp, rgd->rd_bh[0], 0);

    fs_rgrp_verify(rgd);

    fs_rgrp_relse(rgd);
    total_reclaim += reclaim;
  }

  gfsck_free(ip);

  pp_print(PPN, "Pass 2:  %d dinodes checked, %"PRIu64" meta blocks reclaimed\n",
	 count, total_reclaim);
  
  return restart;
}




