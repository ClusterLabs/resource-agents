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
#include "fs_dir.h"
#include "fs_inode.h"
#include "fs_rgrp.h"

#include "pass5.h"
/**
 * pass_5
 * @sdp:
 *
 * The purpose of this pass is to search for directory cycles.
 *
 */
int pass_5(fs_sbd_t *sdp)
{
  fs_rgrpd_t *rgd;
  osi_list_t *tmp;
  fs_inode_t *ip, *pip;
  di_info_t *dinfo;
  uint64 block; 
  int error, cnt = 0, count = 0, first, type = 0;
  osi_filename_t filename;
  osi_list_t visited;
  identifier_t id;
  int prev_prcnt = -1, prcnt = 0;

  ip = (fs_inode_t *)gfsck_zalloc(sizeof(fs_inode_t));
  ip->i_sbd = sdp;

  pip = (fs_inode_t *)gfsck_zalloc(sizeof(fs_inode_t));
  pip->i_sbd = sdp;

  memset(&id, 0, sizeof(identifier_t));
  memset(&filename, 0, sizeof(osi_filename_t));
  filename.name = "..";
  filename.len = 2;
  id.filename = &filename;
  id.type = ID_FILENAME;
  id.inum = NULL;

  for (tmp = sdp->sd_rglist.next; tmp != &sdp->sd_rglist; tmp = tmp->next)
  {
    prcnt = (int)(100.0 * ((float)cnt / (float)sdp->sd_rgcount));
    if(prev_prcnt != prcnt){
      pp_print(PPL, "Pass 5:  %d %% \n", prcnt);
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
	die("pass_5:  Failed to retrieve on-disk inode data.\n");
      }

      if (ip->i_di.di_flags & GFS_DIF_UNUSED){
	die("found unused dinode!  Should be cleared in pass 2.\n");
      }

      if (ip->i_di.di_type != GFS_FILE_DIR)
	continue;
      
      count++;
      
      osi_list_init(&visited);
      dinfo = (di_info_t *)gfsck_zalloc(sizeof(di_info_t));
      osi_list_add(&dinfo->din_list, &visited);
      dinfo->din_addr = ip->i_num.no_addr;

      error = fs_dir_search(ip, &id, &type);
      if (error){
	if(error == -ENOENT){
	  pp_print(PPN, "No \"..\" entry found in dir #%"PRIu64"\n",
		   ip->i_num.no_addr);
	}else{
	  pp_print(PPN, "An error occured while searching dir #%"PRIu64"\n",
		   ip->i_num.no_addr);
	}
	continue;
      }

      while (1)
      {
	if (id.inum->no_addr == sdp->sd_sb.sb_root_di.no_addr)
	  break;

	dinfo = search_list(&visited, id.inum->no_addr);

	if (dinfo){
	  pp_print(PPVH, "Found directory cycle\n");
	  break;
	}

	dinfo = (di_info_t *)gfsck_zalloc(sizeof(di_info_t));

	osi_list_add(&dinfo->din_list, &visited);
	dinfo->din_addr = id.inum->no_addr;

	pip->i_num.no_addr = pip->i_num.no_formal_ino = id.inum->no_addr;
	if(fs_copyin_dinode(pip)){
	  pp_print(PPVH, "error reading dinode\n");
	  break;
	}

	gfsck_free(id.inum);
	id.inum = NULL;
	error = fs_dir_search(pip, &id, &type);
	if (error){
	  if(error == -ENOENT){
	    pp_print(PPN, "No \"..\" entry found in dir #%"PRIu64"\n",
		     ip->i_num.no_addr);
	  } else {
	    pp_print(PPN, "An error occured while searching dir #%"PRIu64"\n",
		     ip->i_num.no_addr);
	  }
	  continue;
	}
      }

      if (dinfo){
	/* fix cycle */
      }

      while (!osi_list_empty(&visited))
      {
	dinfo = osi_list_entry(visited.next, di_info_t, din_list);
	osi_list_del(&dinfo->din_list);
	gfsck_free(dinfo);
      }
      if(id.inum) gfsck_free(id.inum);
      id.inum = NULL;
    }

    fs_rgrp_relse(rgd);
  }

  if(id.inum) gfsck_free(id.inum);
  gfsck_free(ip);
  gfsck_free(pip);

  pp_print(PPN, "Pass 5:  %d directories checked\n", count);

  return 0;
}
