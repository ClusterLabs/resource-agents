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
#include "fs_bio.h"
#include "fs_file.h"
#include "fs_rgrp.h"

#include "fs_super.h"
/**
 * check_sb - Check superblock
 * @sdp: the filesystem
 * @sb: The superblock
 *
 * Checks the version code of the FS is one that we understand how to
 * read and that the sizes of the various on-disk structures have not
 * changed.
 *
 * Returns: 0 on success, -1 on failure
 */
static int check_sb(fs_sbd_t *sdp, struct gfs_sb *sb)
{
  int error = 0;
	
  if (sb->sb_header.mh_magic != GFS_MAGIC ||
      sb->sb_header.mh_type != GFS_METATYPE_SB){
    pp_print(PPVH, "Either the super block is corrupted, or this "
	     "is not a GFS filesystem\n");
    error = -EINVAL;
    goto out;
  }
	
  /*  If format numbers match exactly, we're done.  */ 
  if (sb->sb_fs_format != GFS_FORMAT_FS ||
      sb->sb_multihost_format != GFS_FORMAT_MULTI){
    pp_print(PPVH, "Old file system detected.\n");
  }
  
 out:
  return error;
}


/*
 * fs_read_sb: read the super block from disk
 * sdp: in-core super block
 *
 * This function reads in the super block from disk and
 * initializes various constants maintained in the super
 * block
 *
 * Returns: 0 on success, -1 on failure.
 */
int fs_read_sb(fs_sbd_t *sdp)
{
  osi_buf_t *bh;
  uint64 space = 0;
  unsigned int x;
  int error;

  error = fs_get_and_read_buf(sdp, GFS_SB_ADDR >> sdp->sd_fsb2bb_shift,
			      &bh, 0);
  if (error){
    pp_print(PPVH, "Unable to read superblock\n");
    goto out;
  }
  
  gfs_sb_in(&sdp->sd_sb, BH_DATA(bh));
	
  fs_relse_buf(sdp, bh);
  
  error = check_sb(sdp, &sdp->sd_sb);
  if (error)
    goto out;
	
  sdp->sd_fsb2bb_shift = sdp->sd_sb.sb_bsize_shift - 9;
  sdp->sd_diptrs =
    (sdp->sd_sb.sb_bsize-sizeof(struct gfs_dinode)) / sizeof(uint64);
  sdp->sd_inptrs =
    (sdp->sd_sb.sb_bsize-sizeof(struct gfs_indirect)) / sizeof(uint64);
  sdp->sd_jbsize = sdp->sd_sb.sb_bsize - sizeof(struct gfs_meta_header);
  sdp->sd_hash_bsize = sdp->sd_sb.sb_bsize / 2;
  sdp->sd_hash_ptrs = sdp->sd_hash_bsize / sizeof(uint64);
  sdp->sd_heightsize[0] = sdp->sd_sb.sb_bsize - sizeof(struct gfs_dinode);
  sdp->sd_heightsize[1] = sdp->sd_sb.sb_bsize * sdp->sd_diptrs;
  for (x = 2; ; x++){
    space = sdp->sd_heightsize[x - 1] * sdp->sd_inptrs;
    if (space / sdp->sd_inptrs != sdp->sd_heightsize[x - 1] ||
	space % sdp->sd_inptrs != 0)
      break;
    sdp->sd_heightsize[x] = space;
  }
  sdp->sd_max_height = x;
  if(sdp->sd_max_height > GFS_MAX_META_HEIGHT){
    printf("Bad sd_max_height.");
    error = -1;
    goto out;
  }
	
  sdp->sd_jheightsize[0] = sdp->sd_sb.sb_bsize - sizeof(struct gfs_dinode);
  sdp->sd_jheightsize[1] = sdp->sd_jbsize * sdp->sd_diptrs;
  for (x = 2; ; x++){
    space = sdp->sd_jheightsize[x - 1] * sdp->sd_inptrs;
    if (space / sdp->sd_inptrs != sdp->sd_jheightsize[x - 1] ||
	space % sdp->sd_inptrs != 0)
      break;
    sdp->sd_jheightsize[x] = space;
  }
  sdp->sd_max_jheight = x;
  if(sdp->sd_max_jheight > GFS_MAX_META_HEIGHT){
    printf("Bad sd_max_jheight.");
    error = -1;
  }
	
 out:
  
  return error;
}


/*
 * fs_ji_update - fill in journal info
 * ip: the journal index inode
 *
 * Given the inode for the journal index, read in all
 * the journal indexes.
 *
 * Returns: 0 on success, -1 on failure
 */
int fs_ji_update(fs_sbd_t *sdp)
{
  fs_inode_t *ip = sdp->sd_jiinode;
  char buf[sizeof(struct gfs_jindex)];
  unsigned int j;
  int error=0;
	
  if(ip->i_di.di_size % sizeof(struct gfs_jindex) != 0){
    pp_print(PPVH, "The size reported in the journal index inode is not a\n"
	     "\tmultiple of the size of a journal index.\n");
    return -1;
  }

  sdp->sd_jindex = (struct gfs_jindex *)gfsck_zalloc(ip->i_di.di_size);

  for (j = 0; ; j++){
    error = fs_readi(ip, buf, j * sizeof(struct gfs_jindex), sizeof(struct gfs_jindex));
    if(!error)
      break;
    if (error != sizeof(struct gfs_jindex)){
      pp_print(PPVH, 
	       "An error occurred while reading the journal index file.\n");
      goto fail;
    }
	
    gfs_jindex_in(sdp->sd_jindex + j, buf);
  }
	
	
  if(j * sizeof(struct gfs_jindex) != ip->i_di.di_size){
    pp_print(PPVH, "j * sizeof(struct gfs_jindex) != ip->i_di.di_size\n");
    goto fail;
  }
  sdp->sd_journals = j;
  pp_print(PPVL, "fs_ji_update:  %d journals found.\n", j);
	
  return 0;

 fail:
  gfsck_free(sdp->sd_jindex);
  return -1;
}


/**
 * fs_ri_update - attach rgrps to the super block
 * @sdp:
 *
 * Given the rgrp index inode, link in all rgrps into the super block
 * and be sure that they can be read.
 *
 * Returns: 0 on success, -1 on failure.
 */
int fs_ri_update(fs_sbd_t *sdp)
{
  fs_rgrpd_t *rgd;
  osi_list_t *tmp;
  struct gfs_rindex buf;
  unsigned int rg;
  int error, count1 = 0, count2 = 0;


  for (rg = 0; ; rg++)
  {
    error = fs_readi(sdp->sd_riinode, (char *)&buf, 
		     rg * sizeof(struct gfs_rindex), sizeof(struct gfs_rindex));
    if (!error)
      break;
    if (error != sizeof(struct gfs_rindex)){
      pp_print(PPVH, "Unable to read resource group index #%d.\n", rg);
      goto fail;
    }

    rgd = (fs_rgrpd_t *)gfsck_zalloc(sizeof(fs_rgrpd_t));

    rgd->rd_sbd = sdp;

    osi_list_add_prev(&rgd->rd_list, &sdp->sd_rglist);

    gfs_rindex_in(&rgd->rd_ri, (char *)&buf);

    if(fs_compute_bitstructs(rgd)){
      goto fail;
    }

    rgd->rd_open_count = 0;

    count1++;
  }

  pp_print(PPL, "fs_ri_update:  %d resource groups found.\n", rg);

  for (tmp = sdp->sd_rglist.next; tmp != &sdp->sd_rglist; tmp = tmp->next)
  {
    rgd = osi_list_entry(tmp, fs_rgrpd_t, rd_list);

    error = fs_rgrp_read(rgd);
    if (error){
      pp_print(PPVH, "Unable to read in rgrp descriptor.\n");
      goto fail;
    }

    fs_rgrp_relse(rgd);
    count2++;
  }

  if (count1 != count2){
    pp_print(PPVH, "Rgrps allocated does not equal rgrps read.\n");
    goto fail;
  }

  sdp->sd_rgcount = count1;
  return 0;

 fail:
  while(!osi_list_empty(&sdp->sd_rglist)){
    rgd = osi_list_entry(sdp->sd_rglist.next, fs_rgrpd_t, rd_list);
    osi_list_del(&rgd->rd_list);
    gfsck_free(rgd);
  }
    
  return -1;
}
