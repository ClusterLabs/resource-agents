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
#include "util.h"
#include "fs_rgrp.h"
#include "fs_inode.h"
#include "fs_bio.h"

#include "fs_bmap.h"

typedef struct metapath
{
  uint64              mp_list[GFS_MAX_META_HEIGHT];
}metapath_t;


/**
 * fs_unstuff_dinode - Unstuff a dinode when the data has grown too big
 * @ip: The GFS inode to unstuff
 *
 * This routine unstuffs a dinode and returns it to a "normal" state such 
 * that the height can be grown in the traditional way.
 *
 * Returns: 0 on success, -EXXXX on failure
 */
int fs_unstuff_dinode(fs_inode_t *ip)
{
  fs_sbd_t *sdp = ip->i_sbd;
  osi_buf_t *bh = NULL;
  osi_buf_t *dibh = NULL;
  int journaled = fs_is_jdata(ip);
  uint64 block = 0;
  int error;
	
  if(!fs_is_stuffed(ip)){
    pp_print(PPVH, "Trying to unstuff a dinode that is already unstuffed.\n");
    return -1;
  }
	
  
  error = fs_get_and_read_buf(sdp, ip->i_num.no_addr, &dibh, 0);
  if (error)
    goto fail;
  
  error = check_meta(dibh, GFS_METATYPE_DI);
  if(error)
    goto fail;
	
  if (ip->i_di.di_size){
    if(journaled){
      error = fs_metaalloc(ip, &block);
      if (error)
	goto fail;
      
      error = fs_get_buf(sdp, block, &bh);
      if (error)
	goto fail;
      
      memcpy(BH_DATA(bh)+sizeof(struct gfs_meta_header),
	     BH_DATA(dibh)+sizeof(struct gfs_dinode),
	     BH_SIZE(dibh)-sizeof(struct gfs_dinode));
      
      error = fs_write_buf(sdp, bh, 0);
      if(error)
	goto fail;
      fs_relse_buf(sdp, bh);
    }
    else{
      error = fs_blkalloc(ip, &block);
      
      if(error)
	goto fail;


      error = fs_get_buf(sdp, block, &bh);
      if (error)
	goto fail;
      
      memcpy(BH_DATA(bh)+sizeof(struct gfs_meta_header),
	     BH_DATA(dibh)+sizeof(struct gfs_dinode),
	     BH_SIZE(dibh)-sizeof(struct gfs_dinode));
      
      error = fs_write_buf(sdp, bh, 0);
      if(error)
	goto fail;
      fs_relse_buf(sdp, bh);
    }
  }
	
  bh = NULL;
  /*  Set up the pointer to the new block  */
	
  memset(BH_DATA(dibh)+sizeof(struct gfs_dinode), 0,
	 BH_SIZE(dibh)-sizeof(struct gfs_dinode));
	
  if (ip->i_di.di_size){
    ((uint64 *)(BH_DATA(dibh) + sizeof(struct gfs_dinode)))[0] = cpu_to_gfs64(block);
    ip->i_di.di_blocks++; 
  }
	
  ip->i_di.di_height = 1;
	
  gfs_dinode_out(&ip->i_di, BH_DATA(dibh));
  if(fs_write_buf(sdp, dibh, 0)){
    pp_print(PPVH, "Dinode unstuffed, but unable to write back dinode.\n");
    goto fail;
  }
  fs_relse_buf(sdp, dibh);
	
  return 0;
	
	
	
 fail:
  if(bh) fs_relse_buf(sdp, bh);
  if(dibh) fs_relse_buf(sdp, dibh);
	
  return error; 
}
	

/**
 * calc_tree_height - Calculate the height of a metadata tree
 * @ip: The GFS inode
 * @size: The proposed size of the file
 *
 * Work out how tall a metadata tree needs to be in order to accommodate a
 * file of a particular size. If size is less than the current size of
 * the inode, then the current size of the inode is used instead of the
 * supplied one.
 *
 * Returns: the height the tree should be
 */
	
static unsigned int calc_tree_height(fs_inode_t *ip, uint64 size)
{
  fs_sbd_t *sdp = ip->i_sbd;
  uint64 *arr;
  unsigned int max, height;
	
  if (ip->i_di.di_size > size)
    size = ip->i_di.di_size;
	
  if (fs_is_jdata(ip)){
    arr = sdp->sd_jheightsize;
    max = sdp->sd_max_jheight;
  }
  else{
    arr = sdp->sd_heightsize;
    max = sdp->sd_max_height;
  }
  for (height = 0; height < max; height++)
    if (arr[height] >= size)
      break;
	
  return height;
}


/**
 * build_height - Build a metadata tree of the requested height
 * @ip: The GFS inode
 * @height: The height to build to
 *
 *
 * Returns: 0 on success, -EXXXX on failure
 */
static int build_height(fs_inode_t *ip, int height)
{
  fs_sbd_t *sdp = ip->i_sbd;
  osi_buf_t *bh, *dibh;
  uint64 block, *bp;
  unsigned int x;
  int new_block;
  int error;
	
  while (ip->i_di.di_height < height){
    error = fs_get_and_read_buf(ip->i_sbd, ip->i_num.no_addr, &dibh, 0);
    if (error)
      goto fail;
	
    new_block = FALSE;
    bp = (uint64 *)(BH_DATA(dibh) + sizeof(struct gfs_dinode));
    for (x = 0; x < sdp->sd_diptrs; x++, bp++)
      if (*bp){
	new_block = TRUE;
	break;
      }
    
    
    if (new_block){
      /*  Get a new block, fill it with the old direct pointers and write it out  */
      error = fs_metaalloc(ip, &block);
      if (error)
	goto fail_drelse;
      
      error = fs_get_and_read_buf(sdp, block, &bh, 0);
      if (error)
	goto fail_drelse;
      
      set_meta(bh, GFS_METATYPE_IN, GFS_FORMAT_IN);
      /*
      gfs_buffer_copy_tail(bh, sizeof(struct gfs_indirect),
			   dibh, sizeof(struct gfs_dinode));
      */
      printf("ATTENTION -- Not doing copy_tail...\n");
      exit(1);
      error = -1;
      goto fail_drelse;
      if((error = fs_write_buf(sdp, bh, 0))){
	pp_print(PPVH, "Unable to write new buffer #%"PRIu64".\n",
		 BH_BLKNO(bh));
	goto fail_drelse;
      }
      fs_relse_buf(sdp, bh);
    }
	
	
    /*  Set up the new direct pointer and write it out to disk  */
    
    memset(BH_DATA(dibh)+sizeof(struct gfs_dinode), 0,
	   BH_SIZE(dibh)-sizeof(struct gfs_dinode));
    
    if (new_block){
      ((uint64 *)(BH_DATA(dibh) + sizeof(struct gfs_dinode)))[0] = cpu_to_gfs64(block);
      ip->i_di.di_blocks++;
    }
    
    ip->i_di.di_height++;
    
    gfs_dinode_out(&ip->i_di, BH_DATA(dibh));
    fs_write_buf(sdp, dibh, 0);
    fs_relse_buf(sdp, dibh);
  }
	
  return 0;
  
  
  
 fail_drelse:
  fs_relse_buf(sdp, dibh);
  
 fail:
  return error; 
}


static void find_metapath(fs_inode_t *ip, metapath_t *mp, uint64 block)
{
  fs_sbd_t *sdp = ip->i_sbd;
  unsigned int i;
	
  for (i = ip->i_di.di_height; i--; ){
    mp->mp_list[i] = block % sdp->sd_inptrs;
    block /= sdp->sd_inptrs;
  }
}
	

/**
 * metapointer - Return pointer to start of metadata in a buffer
 * @bh: The buffer
 * @level: The metadata level (0 = dinode)
 * @mp: The metapath 
 *
 * Return a pointer to the block number of the next level of the metadata
 * tree given a buffer containing the pointer to the current level of the
 * metadata tree.
 */ 
	
static uint64 *metapointer(osi_buf_t *bh, unsigned int level, metapath_t *mp)
{
  int head_size = (level > 0) ? sizeof(struct gfs_indirect) : sizeof(struct gfs_dinode);
  return ((uint64 *)(BH_DATA(bh) + head_size)) + mp->mp_list[level];
}
	
	
/**
 * get_metablock - Get the next metadata block in metadata tree
 * @ip: The GFS inode
 * @bh: Buffer containing the pointers to metadata blocks
 * @level: The level of the tree (0 = dinode)
 * @mp: The metapath
 * @create: Non-zero if we may create a new meatdata block
 * @new: Used to indicate if we did create a new metadata block
 * @block: the returned disk block number
 *
 * Given a metatree, complete to a particular level, checks to see if the next
 * level of the tree exists. If not the next level of the tree is created.
 * The block number of the next level of the metadata tree is returned.
 *
 * Returns: 0 on success, -EXXX on failure
 */

static int get_metablock(fs_inode_t *ip,
			 osi_buf_t *bh, unsigned int level, metapath_t *mp,
			 int create, int *new, uint64 *block)
{
  uint64 *ptr = metapointer(bh, level, mp);
  int error = 0;
	
  *new = 0;
  *block = 0;

  if (*ptr){
    *block = gfs64_to_cpu(*ptr);
    goto out;
  }
	
  if (!create)
    goto out;
 
  error = fs_metaalloc(ip, block);
  if (error)
    goto out;
	
  *ptr = cpu_to_gfs64(*block);
  ip->i_di.di_blocks++;
  fs_write_buf(ip->i_sbd, bh, 0);
	
  *new = 1;
	
 out:
  return error;
}


/**
 * get_datablock - Get datablock number from metadata block
 * @rgd: rgrp to allocate from if necessary
 * @ip: The GFS inode
 * @bh: The buffer containing pointers to datablocks
 * @mp: The metapath
 * @create: Non-zero if we may create a new data block
 * @new: Used to indicate if we created a new data block
 * @block: the returned disk block number
 *
 * Given a fully built metadata tree, checks to see if a particular data
 * block exists. It is created if it does not exist and the block number
 * on disk is returned.
 *
 * Returns: 0 on success, -EXXX on failure
 */

static int get_datablock(fs_inode_t *ip,
			 osi_buf_t *bh, metapath_t *mp,
			 int create, int *new, uint64 *block)
{
  uint64 *ptr = metapointer(bh, ip->i_di.di_height - 1, mp);
  int error = 0;
	
  *new = 0;
  *block = 0;
  
  
  if (*ptr){
    *block = gfs64_to_cpu(*ptr);
    goto out;
  }
  
  if (!create)
    goto out;
  
  if (fs_is_jdata(ip)){
    error = fs_metaalloc(ip, block);
    if (error)
      goto out;
  }
  else {
    error = fs_blkalloc(ip, block);
    if (error)
      goto out;
  }

  *ptr = cpu_to_gfs64(*block);
  ip->i_di.di_blocks++;
  fs_write_buf(ip->i_sbd, bh, 0);
  
  *new = 1;
  
 out:
  return error;
}


/**
 * fs_block_map - Map a block from an inode to a disk block
 * @ip: The GFS inode
 * @lblock: The logical block number
 * @new: Value/Result argument (1 = may create/did create new blocks)
 * @dblock: the disk block number of the start of an extent
 * @extlen: the size of the extent
 *
 * Find the block number on the current device which corresponds to an
 * inode's block. If the block had to be created, "new" will be set.
 *
 * Returns: 0 on success, -EXXX on failure
 */
int fs_block_map(fs_inode_t *ip, uint64 lblock, int *new,
		  uint64 *dblock, uint32 *extlen)
{
  fs_sbd_t *sdp = ip->i_sbd;
  osi_buf_t *bh = NULL;
  metapath_t mp;
  int create = *new;
  unsigned int bsize;
  unsigned int height;
  unsigned int x, end_of_metadata;
  unsigned int nptrs;
  uint64 tmp_dblock;
  int tmp_new;
  int error = 0;
	
  *new = 0;
  *dblock = 0;
  if (extlen)
    *extlen = 0;
	
  if (fs_is_stuffed(ip)){
    *dblock = ip->i_num.no_addr;
    if (extlen)
      *extlen = 1;
    goto out;
  }
  bsize = (fs_is_jdata(ip)) ? sdp->sd_jbsize : sdp->sd_sb.sb_bsize;
  
  height = calc_tree_height(ip, (lblock + 1) * bsize);
  if (ip->i_di.di_height < height){
    if (!create){
      error = 0;
      goto fail;
    }
    
    error = build_height(ip, height);
    if (error)
      goto fail;
  }
  
  
  error = fs_get_and_read_buf(ip->i_sbd, ip->i_num.no_addr, &bh, 0);
  if (error)
    goto fail;
  
  
  find_metapath(ip, &mp, lblock);
  end_of_metadata = ip->i_di.di_height - 1;
  
  for (x = 0; x < end_of_metadata; x++){
    error = get_metablock(ip, bh, x, &mp, create, new, dblock);
    fs_relse_buf(ip->i_sbd, bh); bh = NULL;
    if (error)
      goto fail;
    if (!*dblock)
      goto out;

    printf("ATTENTION -- not doing gfs_get_meta_buffer...\n");
    error = -1;
    exit(1);
    /*
    error = gfs_get_meta_buffer(ip, x + 1, *dblock, *new, &bh);
    */
    if (error)
      goto fail;
  }
  
  
  error = get_datablock(ip, bh, &mp, create, new, dblock);
  if (error)
    goto fail_drelse;
  
  if (extlen && *dblock){
    *extlen = 1;
    
    if (!*new){
      nptrs = (end_of_metadata) ? sdp->sd_inptrs : sdp->sd_diptrs;
      while (++mp.mp_list[end_of_metadata] < nptrs){
	error = get_datablock(ip, bh, &mp, 0, &tmp_new, &tmp_dblock);
	if(error){
	  pp_print(PPVH, "Unable to perform get_datablock.\n");
	  goto fail;
	}
	
	if (*dblock + *extlen != tmp_dblock)
	  break;
	
	(*extlen)++;
      }
    }
  }
  
  
  fs_relse_buf(sdp, bh);
  
  
 out:
  if (*new){
    error = fs_get_and_read_buf(sdp, ip->i_num.no_addr, &bh, 0);
    if (error)
      goto fail;
    gfs_dinode_out(&ip->i_di, BH_DATA(bh));
    fs_write_buf(sdp, bh, 0);
    fs_relse_buf(sdp, bh);
  }
  return 0;
  
  
  
 fail_drelse:
  if(bh)
    fs_relse_buf(sdp, bh);
  
 fail:
  return error;
}
