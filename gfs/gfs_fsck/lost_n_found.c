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
#include "fs_dir.h"

#include "lost_n_found.h"

/* add_inode_to_lf - Add dir entry to lost+found for the inode
 * @ip: inode to add to lost + found
 *
 * This function adds an entry into the lost and found dir
 * for the given inode.  The name of the entry will be
 * "lost_<ip->i_num.no_addr>".
 *
 * Returns: 0 on success, -1 on failure.
 */
int add_inode_to_lf(fs_inode_t *ip){
  char tmp_name[256];
  fs_inode_t *lf_ip = ip->i_sbd->sd_lf_dip;
  osi_filename_t filename;

  if(!lf_ip){
    pp_print(PPL, "No l+f directory, can not add inode.\n");
    return -1;
  }

  if(!ip->i_di.di_size){
    pp_print(PPN, "File has zero size, skipping l+f addition.\n");
    return -1;
  }

  switch(ip->i_di.di_type){
  case GFS_FILE_DIR:
    sprintf(tmp_name, "..");
    filename.len = strlen(tmp_name);  /* no trailing NULL */
    filename.name = gfsck_zalloc(filename.len);
    memcpy(filename.name, tmp_name, filename.len);

    if(fs_dirent_del(ip, &filename)){
      pp_print(PPVH, "add_inode_to_lf:  "
	       "Unable to remove \"..\" directory entry.\n");
      gfsck_free(filename.name);
      return -1;
    }

    if(fs_dir_add(ip, &filename, &(lf_ip->i_num), lf_ip->i_di.di_type)){
      pp_print(PPH, "Failed to link .. entry to l+f directory.\n");
      return -1;
    }
    
    gfsck_free(filename.name);
    sprintf(tmp_name, "lost_dir_%"PRIu64, ip->i_num.no_addr);
    break;
  case GFS_FILE_REG:
    sprintf(tmp_name, "lost_file_%"PRIu64, ip->i_num.no_addr);
    break;
  default:
    sprintf(tmp_name, "lost_%"PRIu64, ip->i_num.no_addr);
    break;
  }
  filename.len = strlen(tmp_name);  /* no trailing NULL */
  filename.name = gfsck_zalloc(filename.len);
  memcpy(filename.name, tmp_name, filename.len);

  if(fs_dir_add(lf_ip, &filename, &(ip->i_num), ip->i_di.di_type)){
    pp_print(PPH, "Failed to add inode #%"PRIu64" to l+f dir.\n",
	     ip->i_num.no_addr);
    gfsck_free(filename.name);
    return -1;
  }

  gfsck_free(filename.name);
  return 0;
}
