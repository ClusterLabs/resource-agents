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
#include "lost_n_found.h"
#include "util.h"
#include "fs_bio.h"
#include "fs_dir.h"
#include "fs_inode.h"
#include "fs_rgrp.h"

#include "pass4.h"
/**
 * check_dotdot_entry
 * @sdp:
 * @ip:
 *
 */
static int check_dotdot_entry(fs_inode_t *ip)
{
  fs_sbd_t *sdp = ip->i_sbd;
  osi_filename_t filename;
  fs_inode_t *pip;
  int error;
  identifier_t id;

  memset(&id, 0, sizeof(identifier_t));
  memset(&filename, 0, sizeof(osi_filename_t));
  filename.name = "..";
  filename.len = 2;
  id.filename = &filename;
  id.type = ID_FILENAME;

  error = fs_dir_search(ip, &id, NULL);
  if (error){
    if(error == -ENOENT){
      die("check_dotdot_entry: \"..\" entry not found in dir #%"PRIu64".\n",
	  ip->i_num.no_addr);
    }else{
      die("check_dotdot_entry:  A failure occured while searching for \"..\" "
	  "entry in dir #%"PRIu64".\n", ip->i_num.no_addr);
    }
  }
  if (!id.inum->no_addr)
    die("check_dotdot_entry: this shouldn't happen\n");


  /* get the parent dinode */

  pip = (fs_inode_t *)gfsck_zalloc(sizeof(fs_inode_t));
  pip->i_sbd = sdp;  
  pip->i_num = *(id.inum);
  if(fs_copyin_dinode(pip)){
    pp_print(PPVH, "check_dotdot_entry:  Unable to read in parent dinode.\n");
    gfsck_free(id.inum);
    gfsck_free(pip);
    return -1;
  }
  
  id.filename = NULL;
  id.inum->no_addr = id.inum->no_formal_ino = ip->i_num.no_addr;
  id.type = ID_INUM;

  /* search parent for ip's entry */
  error = fs_dir_search(pip, &id, NULL); 

  if (error){
    if(error == -ENOENT) {
      pp_print(PPN, "Bad linkage from parent to child directory.\n");
    } else {
      pp_print(PPN, "Unable to search parent dir for child entry.\n");
    }
    if(query("\tMove child directory #%"PRIu64" to lost and found? (y/n) ",
	     ip->i_num.no_addr)){
      if(add_inode_to_lf(ip)){
	pp_print(PPN, "Failed to move directory to lost and found.\n");
	if(query("\tRemove child directory #%"PRIu64"? (y/n) ",
		 ip->i_num.no_addr)){
	  if(fs_remove(ip)){
	    pp_print(PPVH, "Unable to remove dir...\n");
	  } else {
	    pp_print(PPN, "Child directory removed.\n");
	  }
	} else {
	  pp_print(PPN, "Badly linked directory remains.\n");
	}
      } else {
	pp_print(PPN, "Directory successfully moved to lost and found.\n");
      }
    } else {
      if(query("\tRemove child directory #%"PRIu64"? (y/n) ",
	       ip->i_num.no_addr)){
	if(fs_remove(ip)){
	  pp_print(PPVH, "Unable to remove dir...\n");
	} else {
	  pp_print(PPN, "Child directory removed.\n");
	}
      } else {
	pp_print(PPN, "Badly linked directory remains.\n");
      }
    }
    error = 0;
  }


  gfsck_free(pip);
  gfsck_free(id.inum);
  if(id.filename){
    gfsck_free(id.filename->name);
    gfsck_free(id.filename);
  }

  return error;
}


/**
 * pass_4
 * @sdp:
 *
 * The purpose of this pass is to ensure that a dir's parent
 * and itself are correctly linked.
 *
 * This pass also ensures that the resource group has a correct
 * dinode count.
 */
int pass_4(fs_sbd_t *sdp)
{
  fs_rgrpd_t *rgd;
  osi_list_t *tmp, *d_tmp;
  fs_inode_t *ip;
  di_info_t *dinfo;
  uint64 block; 
  int error;
  int cnt = 0;  /* count how many rgrps we've gone through */
  int count = 0; /* count the number of dinodes encountered */
  int rgrp_count = 0; /* number of dinodes in a rgrp */
  int first, val;  
  int restart=0;
  int prev_prcnt = -1, prcnt = 0;


  ip = (fs_inode_t *)gfsck_zalloc(sizeof(fs_inode_t));
  ip->i_sbd = sdp;

  for (tmp = sdp->sd_rglist.next; tmp != &sdp->sd_rglist; tmp = tmp->next)
  {
    prcnt = (int)(100.0 * ((float)cnt / (float)sdp->sd_rgcount));
    if(prev_prcnt != prcnt){
      pp_print(PPL, "Pass 4:  %d %% \n", prcnt);
      prev_prcnt = prcnt;
    }
    cnt++;

    rgd = osi_list_entry(tmp, fs_rgrpd_t, rd_list);

    error = fs_rgrp_read(rgd);
    if (error)
      return -1;
    
    first = 1;
    rgrp_count = 0;

    while (1){
      error = next_rg_metatype(rgd, &block, GFS_METATYPE_DI, first);
      if (error){
        break;
      }
      first = 0;

      ip->i_num.no_addr = ip->i_num.no_formal_ino = block;

      if(fs_copyin_dinode(ip)){
	pp_print(PPVH, "Failed to retrieve disk inode data.\n");
	exit(EXIT_FAILURE);
      }

      if (ip->i_di.di_flags & GFS_DIF_UNUSED){
	die("found unused dinode!  Should be cleared in pass 2.\n");
      }

      count++;
      rgrp_count++;

      val = get_bitmap(sdp, ip->i_num.no_addr, NULL);

      if (!val){
	if((ip->i_num.no_addr != sdp->sd_sb.sb_jindex_di.no_addr) &&
	   (ip->i_num.no_addr != sdp->sd_sb.sb_rindex_di.no_addr) &&
	   (ip->i_num.no_addr != sdp->sd_sb.sb_root_di.no_addr) &&
	   (ip->i_num.no_addr != sdp->sd_sb.sb_quota_di.no_addr) &&
	   (ip->i_num.no_addr != sdp->sd_sb.sb_license_di.no_addr))
	{
	  pp_print(PPN, "No connection to dinode #%"PRIu64"\n",
		   ip->i_num.no_addr);
	  if(query("\tMove to lost and found? (y/n) ")){
	    if(add_inode_to_lf(ip)){
	      if(query("\tRemove dinode with no connection? (y/n) ")){
		if(fs_remove(ip)){
		  pp_print(PPVH, "Unable to remove dinode.\n");
		} else {
		  restart = 1;
		  pp_print(PPN, "Dinode removed.\n");
		}
	      } else {
		pp_print(PPN, "Unconnected dinode remains.\n");
	      }
	    } else {
	      restart = 1;
	      pp_print(PPH, "Dinode #%"PRIu64" moved to l+f.\n",
		       ip->i_num.no_addr);
	    }
	  } else {
	    pp_print(PPN, "Unconnected dinode remains.\n");
	  }
	  continue;
	}
      }


      if (ip->i_di.di_type == GFS_FILE_DIR){
	check_dotdot_entry(ip);
      }
      else if (ip->i_di.di_nlink != 1)      /* this list used later on */
      {
	int found=0;

	for (d_tmp = sdp->sd_nlink_list.next;
	     d_tmp != &sdp->sd_nlink_list;
	     d_tmp = d_tmp->next){
	  dinfo = osi_list_entry(d_tmp, di_info_t, din_list);
	  if(dinfo->din_addr == ip->i_num.no_addr)
	    found=1;
	}
	if(!found){
	  dinfo = (di_info_t *)gfsck_zalloc(sizeof(di_info_t));
	  osi_list_add(&dinfo->din_list, &sdp->sd_nlink_list);
	  dinfo->din_addr = ip->i_num.no_addr;
	  dinfo->din_nlink = ip->i_di.di_nlink;
	}
      }
    }
    pp_print(PPVL, "Encountered %d dinodes in rgrp %d.\n", rgrp_count, cnt);
    if(rgrp_count != rgd->rd_rg.rg_useddi){
      pp_print(PPN, "Dinodes encountered differs from count in rgrp #%d.\n",
	       cnt);
      pp_print(PPN, "\tDinodes encountered : %d\n", rgrp_count);
      pp_print(PPN, "\trg_free             : %u\n", rgd->rd_rg.rg_free);
      pp_print(PPN, "\trg_useddi           : %u\n", rgd->rd_rg.rg_useddi);
      pp_print(PPN, "\trg_freedi           : %u\n", rgd->rd_rg.rg_freedi);
      pp_print(PPN, "\trg_usedmeta         : %u\n", rgd->rd_rg.rg_usedmeta);
      pp_print(PPN, "\trg_freemeta         : %u\n", rgd->rd_rg.rg_freemeta);
      rgd->rd_rg.rg_useddi = rgrp_count;
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

  gfsck_free(ip);

  pp_print(PPN, "Pass 4:  %d dinodes checked\n", count);
  
  return restart;
}
