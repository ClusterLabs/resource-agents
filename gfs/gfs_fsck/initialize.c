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

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "allocation.h"
#include "interactive.h"
#include "util.h"
#include "fs_inode.h"
#include "fs_recovery.h"
#include "fs_super.h"


#include "initialize.h"

/*
 * empty_super_block - free all structures in the super block
 * sdp: the in-core super block
 *
 * This function frees all allocated structures within the
 * super block.  It does not free the super block itself.
 *
 * Returns: Nothing
 */
static void empty_super_block(fs_sbd_t *sdp)
{
  if(sdp->sd_riinode){
    gfsck_free(sdp->sd_riinode);
    sdp->sd_riinode = NULL;
  }
  if(sdp->sd_jiinode){
    gfsck_free(sdp->sd_jiinode);
    sdp->sd_jiinode = NULL;
  } 
  if(sdp->sd_rooti){
    gfsck_free(sdp->sd_rooti);
    sdp->sd_rooti=NULL;
  }

  if(sdp->sd_jindex){
    gfsck_free(sdp->sd_jindex);
    sdp->sd_jindex = NULL;
  }

  while(!osi_list_empty(&sdp->sd_rglist)){
    fs_rgrpd_t *rgd;
    rgd = osi_list_entry(sdp->sd_rglist.next, fs_rgrpd_t, rd_list);
    osi_list_del(&rgd->rd_list);
    gfsck_free(rgd);
  }
  while(!osi_list_empty(&sdp->sd_dirent_list)){
    di_info_t *di;
    di = osi_list_entry(sdp->sd_dirent_list.next, di_info_t, din_list);
    osi_list_del(&di->din_list);
    gfsck_free(di);
  }
  while(!osi_list_empty(&sdp->sd_nlink_list)){
    di_info_t *di;
    di = osi_list_entry(sdp->sd_nlink_list.next, di_info_t, din_list);
    osi_list_del(&di->din_list);
    gfsck_free(di);
  }
  while(!osi_list_empty(&sdp->sd_bitmaps)){
    bitmap_list_t *bl;
    bl = osi_list_entry(sdp->sd_bitmaps.next, bitmap_list_t, list);
    gfsck_free(bl->bm);
    osi_list_del(&bl->list);
    gfsck_free(bl);
  }
}


/**
 * set_block_ranges
 * @sdp: superblock
 *
 * Uses info in rgrps and jindex to determine boundaries of the
 * file system.
 *
 * Returns: 0 on success, -1 on failure
 */
static int set_block_ranges(fs_sbd_t *sdp)
{
  struct gfs_jindex *jdesc;
  fs_rgrpd_t *rgd;
  struct gfs_rindex *ri;
  osi_list_t *tmp;
  char buf[sdp->sd_sb.sb_bsize];
  uint64 rmax = 0;
  uint64 jmax = 0;
  uint64 rmin = 0;
  uint64 i;
  int error;


  for (tmp = sdp->sd_rglist.next; tmp != &sdp->sd_rglist; tmp = tmp->next)
  {
    rgd = osi_list_entry(tmp, fs_rgrpd_t, rd_list);
    ri = &rgd->rd_ri;
    if (ri->ri_data1 + ri->ri_data - 1 > rmax)
      rmax = ri->ri_data1 + ri->ri_data - 1;
    if (!rmin || ri->ri_data1 < rmin)
      rmin = ri->ri_data1;
  }


  for (i = 0; i < sdp->sd_journals; i++)
  {
    jdesc = &sdp->sd_jindex[i];

    if ((jdesc->ji_addr+jdesc->ji_nsegment*sdp->sd_sb.sb_seg_size-1) > jmax)
      jmax = jdesc->ji_addr + jdesc->ji_nsegment * sdp->sd_sb.sb_seg_size - 1;
  }

  sdp->sd_last_fs_block = (jmax > rmax) ? jmax : rmax;

  sdp->sd_last_data_block = rmax;
  sdp->sd_first_data_block = rmin;

  if(do_lseek(sdp->sd_diskfd, (sdp->sd_last_fs_block * sdp->sd_sb.sb_bsize))){
    pp_print(PPVH, "Can't seek to last block in file system: %"PRIu64"\n",
	     sdp->sd_last_fs_block);
    goto fail;
  }
      
  memset(buf, 0, sdp->sd_sb.sb_bsize);
  error = read(sdp->sd_diskfd, buf, sdp->sd_sb.sb_bsize);
  if (error != sdp->sd_sb.sb_bsize){
    pp_print(PPVH, "Can't read last block in file system (%u), "
	     "last_fs_block: %"PRIu64"\n",
	     error, sdp->sd_last_fs_block);
    goto fail;
  }

  return 0;

 fail:
  return -1;
}


/**
 * fill_super_block
 * @sdp:
 *
 * Returns: 0 on success, -1 on failure
 */
static int fill_super_block(fs_sbd_t *sdp)
{
  int error;
  fs_inode_t *ip = NULL;

  sync();

  /********************************************************************
   ***************** First, initialize all lists **********************
   ********************************************************************/
  osi_list_init(&sdp->sd_rglist);
  osi_list_init(&sdp->sd_dirent_list);
  osi_list_init(&sdp->sd_nlink_list);
  osi_list_init(&sdp->sd_bitmaps);


  /********************************************************************
   ************  next, read in on-disk SB and set constants  **********
   ********************************************************************/
  sdp->sd_sb.sb_bsize = 512;
  if (sdp->sd_sb.sb_bsize < GFS_BASIC_BLOCK)
    sdp->sd_sb.sb_bsize = GFS_BASIC_BLOCK;

  if(sizeof(struct gfs_sb) > sdp->sd_sb.sb_bsize){
    pp_print(PPVH, "sizeof(struct gfs_sb) > sdp->sd_sb.sb_bsize\n");
    return -1;
  }

  if(fs_read_sb(sdp) < 0){
    return -1;
  }
  

  /*******************************************************************
   ******************  Initialize important inodes  ******************
   *******************************************************************/

  /* get ri inode */

  sdp->sd_riinode = ip = (fs_inode_t *)gfsck_zalloc(sizeof(fs_inode_t));

  ip->i_num = sdp->sd_sb.sb_rindex_di;
  ip->i_sbd = sdp;

  error = fs_copyin_dinode(ip);
  if (error){
    pp_print(PPVH, "Unable to copy in ri inode.\n");
    return error;
  }

  /* get ji inode */

  sdp->sd_jiinode = ip = (fs_inode_t *)gfsck_zalloc(sizeof(fs_inode_t));

  ip->i_num = sdp->sd_sb.sb_jindex_di;
  ip->i_sbd = sdp;

  error = fs_copyin_dinode(ip);
  if (error){
    pp_print(PPVH, "Unable to copy in ji inode.\n");
    return error;
  }

  /* get root dinode */
  sdp->sd_rooti = ip = (fs_inode_t *)gfsck_zalloc(sizeof(fs_inode_t));

  ip->i_num = sdp->sd_sb.sb_root_di;
  ip->i_sbd = sdp;
 
  error = fs_copyin_dinode(ip);
  if (error){
    pp_print(PPVH, "Unable to copy in root inode.\n");
    return error;
  }

  /*******************************************************************
   *******  Fill in rgrp and journal indexes and related fields  *****
   *******************************************************************/

  /* read in the ji data */
  if (fs_ji_update(sdp)){
    pp_print(PPVH, "Unable to read in ji inode.\n");
    return -1;
  }

  if(fs_ri_update(sdp)){
    pp_print(PPVH, "Unable to fill in resource group information.\n");
    goto fail;
  }
  
  /*******************************************************************
   *******  Now, set boundary fields in the super block  *************
   *******************************************************************/
  if(set_block_ranges(sdp)){
    pp_print(PPVH, "Unable to determine the boundaries of the file system.\n");
    goto fail;
  }

  return 0;

 fail:
  empty_super_block(sdp);
   
  return -1;
}


/*
 * initialize - Fill in all permanent in-core structures
 * sdp: the super block pointer
 * device: the name of the device the file system resides on
 *
 * This function reads important on-disk metadata and
 * initializes the in-core structures.  This makes the
 * file system available for use.
 *
 * Returns: 0 on success, -1 on failure.
 */
int initialize(fs_sbd_t **sdp, char *device)
{
  fs_sbd_t *new_sdp = NULL;

  *sdp = NULL;
  new_sdp = (fs_sbd_t *)gfsck_zalloc(sizeof(fs_sbd_t));

  if ((new_sdp->sd_diskfd = open(device, O_RDWR)) < 0){
    fprintf(stderr, "Unable to open device: %s\n", device);
    gfsck_free(new_sdp);
    return -1;
  }  

  /* First, read in the super block. */
  if(fill_super_block(new_sdp)){
    pp_print(PPVH, "Unable to fill the super block.\n");
    gfsck_free(new_sdp);
    return -1;
  }

  /* Next, Replay the journals */
  if(new_sdp->sd_flags & SBF_RECONSTRUCT_JOURNALS){
    if(reconstruct_journals(new_sdp)){
      pp_print(PPVH, "Unable to reconstruct journals.\n");
      goto fail;
    }
  } else {
    /* ATTENTION -- Journal replay is not supported */
    if(reconstruct_journals(new_sdp)){
      pp_print(PPVH, "Unable to reconstruct journals.\n");
      goto fail;
    }
  }

  if(fs_mkdir(new_sdp->sd_rooti, "l+f", 00700, &(new_sdp->sd_lf_dip))){
    pp_print(PPVH, "Unable to create/locate l+f directory.\n");
    if(!query("Proceed without lost and found? (y/n) ")){
      goto fail;
    }
  }

  if(new_sdp->sd_lf_dip){
    pp_print(PPVL, "Lost and Found directory inode is at "
	     "block #%"PRIu64".\n",
	     new_sdp->sd_lf_dip->i_num.no_addr);
  }

  *sdp = new_sdp;
  return 0;

 fail:
  empty_super_block(new_sdp);
  *sdp = NULL;
  return -1;
}


/* cleanup - free all in-memory structures
 * sdp: dbl ptr to the super block
 *
 * This would not need to take a double pointer, but
 * it may drive home the point that this function frees
 * the memory associated with the super block as well.
 *
 */
void cleanup(fs_sbd_t **sdp)
{
  if(*sdp){
    empty_super_block(*sdp);
    gfsck_free(*sdp); *sdp = NULL;
  }
}
