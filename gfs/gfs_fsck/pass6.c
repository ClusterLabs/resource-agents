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
#include "fs_bio.h"
#include "fs_bits.h"
#include "fs_inode.h"

#include "pass6.h"
/**
 * pass_6
 * @sdp:
 *
 * This function corrects dinodes with bad link counts.
 */
int pass_6(fs_sbd_t *sdp)
{
  int restart=0;
  osi_list_t *tmp1, *tmp2;
  fs_inode_t ip;
  di_info_t *dinfo_nlink = NULL, *dinfo_dirent = NULL;

  memset(&ip, 0, sizeof(fs_inode_t));
  ip.i_sbd = sdp;

  /* check to be sure that the number of times we encountered a file **
  ** is reflected in it's link count................................ */
  pp_print(PPL, "Dinodes with more than one dirent:\n");
  for(tmp2 = sdp->sd_dirent_list.next;
      tmp2 != &sdp->sd_dirent_list;
      tmp2 = tmp2->next){
    dinfo_dirent = osi_list_entry(tmp2, di_info_t, din_list);
    pp_print(PPL, "\tinode = %"PRIu64", dirents = %d\n",
	     dinfo_dirent->din_addr, dinfo_dirent->din_dirents);
  }

  pp_print(PPL, "Dinodes with link count > 1:\n");
  for (tmp1 = sdp->sd_nlink_list.next;
       tmp1 != &sdp->sd_nlink_list;
       tmp1 = tmp1->next){
    dinfo_nlink = osi_list_entry(tmp1, di_info_t, din_list);
    pp_print(PPL, "\tinode = %"PRIu64", nlink = %d\n",
	     dinfo_nlink->din_addr, dinfo_nlink->din_nlink);
  }
  for(tmp2 = sdp->sd_dirent_list.next;
      tmp2 != &sdp->sd_dirent_list;
      tmp2 = tmp2->next){
    dinfo_dirent = osi_list_entry(tmp2, di_info_t, din_list);

    for (tmp1 = sdp->sd_nlink_list.next;
	 tmp1 != &sdp->sd_nlink_list;
	 tmp1 = tmp1->next){
      dinfo_nlink = osi_list_entry(tmp1, di_info_t, din_list);

      if (dinfo_nlink->din_addr == dinfo_dirent->din_addr){
	if (dinfo_nlink->din_nlink == dinfo_dirent->din_dirents){
	  dinfo_nlink->din_addr = 0;
	  dinfo_dirent->din_addr = 0;
	  break;
	}else{
	  pp_print(PPVH, "Bad link count on dinode #%"PRIu64"\n",
		   dinfo_nlink->din_addr);
	  pp_print(PPVH, "  nlink: %u, directory entries: %u\n",
		   dinfo_nlink->din_nlink, 
		   dinfo_dirent->din_dirents);
	  /* change di_nlink to dinfo_dirent->din_dirents */
	  if(fs_get_bitmap(sdp, dinfo_nlink->din_addr, NULL) 
             == GFS_BLKST_USEDMETA){
	    if(query("Attempt to reconcile link count? (y/n) ")){
	      ip.i_num.no_addr = 
		ip.i_num.no_formal_ino = dinfo_nlink->din_addr;
	      if(fs_copyin_dinode(&ip)){
		pp_print(PPN, "Unable to read dinode with bad link count.\n");
		if(query("Remove corrupted dinode? (y/n) ")){
		  if(fs_set_bitmap(sdp, dinfo_nlink->din_addr, GFS_BLKST_FREE)
		     || set_bitmap(sdp, dinfo_nlink->din_addr, 0)){
		    pp_print(PPVH, "Removal failed.\n");
		    return -1;
		  } else {
		    restart = 1;
		    pp_print(PPN, "Dinode removed.\n");
		    continue;
		  }
		} else {
		  pp_print(PPH, "Dinode with bad link count remains.\n");
		}
	      }
	      ip.i_di.di_nlink = dinfo_dirent->din_dirents;
	      if(fs_copyout_dinode(&ip))
		return -1;
	    }
	  } else {
	    osi_buf_t *bh;
	    pp_print(PPN, "Dinode not marked in FS bitmaps.\n");
	    if(query("Remove corrupted/unmarked dinode? (y/n) ")){
	      fs_get_and_read_buf(sdp, dinfo_nlink->din_addr, &bh, 0);
	      memset(BH_DATA(bh), 0, BH_SIZE(bh));
	      fs_write_buf(sdp, bh, 0);
	      fs_relse_buf(sdp, bh);
	      pp_print(PPN, "Dinode removed.\n");
	    } else {
	      pp_print(PPH, "Unmarked Dinode with bad link count remains.\n");
	    }
	  }
	}
      }
    }
  }

  /* files with link count not equal to one */

  for (tmp1 = sdp->sd_nlink_list.next;
       tmp1 != &sdp->sd_nlink_list;
       tmp1 = tmp1->next){
    dinfo_nlink = osi_list_entry(tmp1, di_info_t, din_list);
    
    if (dinfo_nlink->din_addr == 0)
      continue;

    if(get_bitmap(sdp, dinfo_nlink->din_addr, NULL)){
      pp_print(PPN, "Bad link count on dinode #%"PRIu64"\n",
	       dinfo_nlink->din_addr);
      pp_print(PPN, " nlink = %u, but dirents = 1\n", dinfo_nlink->din_nlink);
      if(fs_get_bitmap(sdp, dinfo_nlink->din_addr, NULL)
         == GFS_BLKST_USEDMETA){
	if(query("Reconcile link count (%u -> 1)? (y/n) ",
		 dinfo_nlink->din_nlink)){
	  ip.i_num.no_addr = 
	    ip.i_num.no_formal_ino = dinfo_nlink->din_addr;
	  if(fs_copyin_dinode(&ip)){
	    pp_print(PPN, "Unable to read dinode with bad link count.\n");
	    if(query("Remove corrupted dinode? (y/n) ")){
	      if(fs_set_bitmap(sdp, dinfo_nlink->din_addr, GFS_BLKST_FREE) ||
		 set_bitmap(sdp, dinfo_nlink->din_addr, 0)){
		pp_print(PPVH, "Removal failed.\n");
		return -1;
	      } else {
		pp_print(PPN, "Dinode removed.\n");
		continue;
	      }
	    } else {
	      pp_print(PPH, "Dinode with bad link count remains.\n");
	    }
	  }
	  /* because its listed in bitmap but  **
	  ** not found in dirent list set to 1 */
	  ip.i_di.di_nlink = 1;
	  if(fs_copyout_dinode(&ip))
	    return -1;
	}
      } else {
	osi_buf_t *bh;
	pp_print(PPN, "File not marked in FS bitmaps.\n");
	if(query("Remove corrupted/unmarked file? (y/n) ")){
	  fs_get_and_read_buf(sdp, dinfo_nlink->din_addr, &bh, 0);
	  memset(BH_DATA(bh), 0, BH_SIZE(bh));
	  fs_write_buf(sdp, bh, 0);
	  fs_relse_buf(sdp, bh);
	  restart = 1;
	  pp_print(PPN, "File removed.\n");
	} else {
	  pp_print(PPH, "Unmarked file with bad link count remains.\n");
	}
      }
    } else{
      pp_print(PPVH, "It is impossible for this to be printed :)\n");
      /* this should not happen.  if the file is not in a directory,
	 it should have been removed in pass 4 */
    }
  }

  for (tmp2 = sdp->sd_dirent_list.next;
       tmp2 != &sdp->sd_dirent_list;
       tmp2 = tmp2->next){
    dinfo_dirent = osi_list_entry(tmp2, di_info_t, din_list);
  
    /* If we have ok'ed this above by setting din_addr to 0, continue */
    if (dinfo_dirent->din_addr == 0)
      continue;

    ip.i_num.no_addr = 
      ip.i_num.no_formal_ino = dinfo_dirent->din_addr;

    if(fs_copyin_dinode(&ip)){
      pp_print(PPVH, "Unable to read dinode with bad link count. *");
      if(query("Remove corrupted dinode? (y/n) ")){
	if(fs_get_bitmap(sdp, dinfo_dirent->din_addr, NULL)){
	  fs_set_bitmap(sdp, dinfo_dirent->din_addr, GFS_BLKST_FREE);
	  restart = 1;
	}
	if(get_bitmap(sdp, dinfo_dirent->din_addr, NULL)){
	  set_bitmap(sdp, dinfo_dirent->din_addr, 0);
	}
	pp_print(PPN, "Dinode removed.\n");
      } else {
	pp_print(PPH, "Dinode with bad link count remains.\n");
      }
      
    } else {
      if(ip.i_di.di_type == GFS_FILE_DIR){
	pp_print(PPN, "There are multiple directory entries for the "
		 "same directory (%"PRIu64").\n", ip.i_num.no_addr);
	
	pp_print(PPN, "Forced to remove directory.\n");
	if(fs_remove(&ip)){
	  die("Unable to remove directory.  Cannot proceed.\n"
	      "Removal by hand may be the only option.\n"
	      "Contact support.\n"
	      "IMPORTANT INFO:  Blk #%"PRIu64"\n",
	      ip.i_num.no_addr);
	} else {
	  pp_print(PPL, "Directory removed successfully.\n");
	  restart = 1;
	}
      } else {
	pp_print(PPN, "Bad link count on dinode #%"PRIu64"\n",
		 dinfo_dirent->din_addr);
	pp_print(PPN, " nlink = %u, dir entries = %u\n",
		 ip.i_di.di_nlink, dinfo_dirent->din_dirents);

	if(query("Fix bad link count (%u -> %u)? (y/n) ",
		 ip.i_di.di_nlink, dinfo_dirent->din_dirents)){
	  ip.i_di.di_nlink = dinfo_dirent->din_dirents;
	  if(fs_copyout_dinode(&ip)){
	    pp_print(PPVH, "Unable to write dinode information.");
	    return -1;
	  } else {
	    pp_print(PPN, "Link count reconciled.\n");
	  }
	} else {
	  pp_print(PPH, "Dinode with bad link count remains.\n");
	}
      }
    }
  }


  while(!osi_list_empty(&sdp->sd_nlink_list)){
    dinfo_nlink = osi_list_entry(sdp->sd_nlink_list.next,
				  di_info_t, din_list);
    osi_list_del(&dinfo_nlink->din_list);
    gfsck_free(dinfo_nlink);
  }

  while(!osi_list_empty(&sdp->sd_dirent_list))
  {
    dinfo_dirent = osi_list_entry(sdp->sd_dirent_list.next,
				  di_info_t, din_list);
    osi_list_del(&dinfo_dirent->din_list);
    gfsck_free(dinfo_dirent);
  }

  return restart;
}







