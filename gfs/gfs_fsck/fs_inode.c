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
#include "fs_rgrp.h"

#include "fs_inode.h"

#define ST_CREATE 1

/**
 * fs_get_istruct - Get an inode given its number
 * @sdp: The GFS superblock
 * @inum: The inode number
 * @create: Flag to say if we are allowed to create a new fs_inode_t
 * @ipp: pointer to put the returned inode in
 *
 * Returns: 0 on success, -1 on error
 */
static int fs_get_istruct(fs_sbd_t *sdp, struct gfs_inum *inum,
			  int create, fs_inode_t **ipp)
{
  fs_inode_t *ip = NULL;
  int error = 0;

  if (!create){
    /* we are not currently tracking which inodes we already have */
    error = -1;
    goto out;
  }

  ip = (fs_inode_t *)gfsck_zalloc(sizeof(fs_inode_t));
  ip->i_num = *inum;

  ip->i_sbd = sdp;

  error = fs_copyin_dinode(ip);
  if (error){
    gfsck_free(ip);
    ip = NULL;
    goto out;
  }

 out:
  *ipp = ip;

  return error;
}


/*
 * fs_copyin_dinode - read dinode from disk and store in inode
 * @ip: inode, sdp and inum must be set
 *
 * Returns: 0 on success, -1 on error
 */
int fs_copyin_dinode(fs_inode_t *ip)
{
  osi_buf_t *dibh;
  int error;

  error = fs_get_and_read_buf(ip->i_sbd, ip->i_num.no_addr, &dibh, 0);
  if (error)
    goto out;

  if(check_meta(dibh, GFS_METATYPE_DI)){
    pp_print(PPVH, "fs_copyin_dinode:  Block #%"PRIu64" is not a dinode.\n",
	     ip->i_num.no_addr);
    fs_relse_buf(ip->i_sbd, dibh);
    return -1;
  }

  gfs_dinode_in(&ip->i_di, BH_DATA(dibh));
	
  fs_relse_buf(ip->i_sbd, dibh);

  if(ip->i_num.no_formal_ino != ip->i_di.di_num.no_formal_ino){
    pp_print(PPVH, 
	     "fs_copyin_dinode:  In-core and on-disk formal inode numbers do "
	     "not match.\n");
    error = -1;
    goto out;
  }	

  /*  Handle a moved inode  */
  
  if (ip->i_num.no_addr != ip->i_di.di_num.no_addr){
    pp_print(PPVH, "fs_copyin_dinode:\n"
	   "\tBlock # used to read disk inode: %"PRIu64"\n"
	   "\tBlock # recorded in disk inode : %"PRIu64"\n",
	   ip->i_num.no_addr, ip->i_di.di_num.no_addr);
    error = -1;
  }

 out:
  return error;
}


/*
 * fs_copyout_dinode - given an inode, copy its dinode data to disk
 * @ip: the inode
 *
 * Returns: 0 on success, -1 on error
 */
int fs_copyout_dinode(fs_inode_t *ip){
  osi_buf_t *dibh;
  int error;

  error = fs_get_and_read_buf(ip->i_sbd, ip->i_num.no_addr, &dibh, 0);
  if(error){
    pp_print(PPVH, "Unable to get a buffer to write dinode to disk.\n");
    return -1;
  }

  gfs_dinode_out(&ip->i_di, BH_DATA(dibh));

  if(fs_write_buf(ip->i_sbd, dibh, 0)){
    pp_print(PPVH, "Unable to commit dinode buffer to disk.\n");
    fs_relse_buf(ip->i_sbd, dibh);
    return -1;
  }

  fs_relse_buf(ip->i_sbd, dibh);
  return 0;
}

/**
 * make_dinode - Fill in a new dinode structure
 * @dip: the directory this inode is being created in
 * @inum: the inode number
 * @type: the file type
 * @mode: the file permissions
 * @cred: a credentials structure
 *
 */

static int make_dinode(fs_inode_t *dip, struct gfs_inum *inum,
                       unsigned int type, unsigned int mode, osi_cred_t *cred)
{
  fs_sbd_t *sdp = dip->i_sbd;
  struct gfs_dinode di;
  osi_buf_t *dibh;
  fs_rgrpd_t *rgd;
  int error;

  error = fs_get_and_read_buf(sdp, inum->no_addr, &dibh, 0);
  if (error)
    goto out;

  if(check_meta(dibh, 0)){
    pp_print(PPN, "make_dinode:  Buffer #%"PRIu64" has no meta header.\n",
	     BH_BLKNO(dibh));
    if(query("Add header? (y/n) ")){
      struct gfs_meta_header mh;
      memset(&mh, 0, sizeof(struct gfs_meta_header));
      mh.mh_magic = GFS_MAGIC;
      mh.mh_type = GFS_METATYPE_NONE;
      gfs_meta_header_out(&mh, BH_DATA(dibh));
      pp_print(PPN, "meta header added.\n");
    } else {
      pp_print(PPH, "meta header not added.  Failing make_dinode.\n");
      fs_relse_buf(sdp, dibh);
      return -1;
    }
  }

  ((struct gfs_meta_header *)BH_DATA(dibh))->mh_type =
    cpu_to_gfs32(GFS_METATYPE_DI);
  ((struct gfs_meta_header *)BH_DATA(dibh))->mh_format = 
    cpu_to_gfs32(GFS_FORMAT_DI);

  memset(BH_DATA(dibh) + sizeof(struct gfs_dinode), 0,
	 BH_SIZE(dibh) - sizeof(struct gfs_dinode));

  memset(&di, 0, sizeof(struct gfs_dinode));

  gfs_meta_header_in(&di.di_header, BH_DATA(dibh));

  di.di_num = *inum;

  if (dip->i_di.di_mode & 02000)
  {
    di.di_mode = mode | ((type == GFS_FILE_DIR) ? 02000 : 0);
    di.di_gid = dip->i_di.di_gid;
  }
  else
  {
    di.di_mode = mode;
    di.di_gid = osi_cred_to_gid(cred);
  }

  di.di_uid = osi_cred_to_uid(cred);
  di.di_nlink = 1;
  di.di_blocks = 1;
  di.di_atime = di.di_mtime = di.di_ctime = osi_current_time();

  rgd = fs_blk2rgrpd(sdp, inum->no_addr);
  if(!rgd){
    pp_print(PPVH, "Unable to map block #%"PRIu64" to rgrp\n", inum->no_addr);
    exit(1);
  }

  di.di_rgrp = rgd->rd_ri.ri_addr;
  di.di_goal_rgrp = di.di_rgrp;
  di.di_goal_dblk = di.di_goal_mblk = inum->no_addr - rgd->rd_ri.ri_data1;

  di.di_type = type;

  gfs_dinode_out(&di, BH_DATA(dibh));
  if(fs_write_buf(dip->i_sbd, dibh, 0)){
    pp_print(PPVH, "make_dinode:  bad fs_write_buf()\n");
    error = -EIO;
  }

  fs_relse_buf(dip->i_sbd, dibh);


 out:

  return error;
}


/**
 * fs_change_nlink - Change nlink count on inode
 * @ip: The GFS inode
 * @diff: The change in the nlink count required
 *
 * Returns: 0 on success, -EXXXX on failure.
 */
static int fs_change_nlink(fs_inode_t *ip, int diff)
{
  osi_buf_t *dibh;
  uint32 nlink;
  int error=0;
	
  nlink = ip->i_di.di_nlink + diff;
  
  if (diff < 0)
    if(nlink >= ip->i_di.di_nlink)
      pp_print(PPVH, "fs_change_nlink:  Bad link count detected in dinode.\n");
  
  error = fs_get_and_read_buf(ip->i_sbd, ip->i_num.no_addr, &dibh, 0);
  if (error)
    goto out;
	
  ip->i_di.di_nlink = nlink;
  ip->i_di.di_ctime = osi_current_time();
	
  gfs_dinode_out(&ip->i_di, BH_DATA(dibh));
  fs_write_buf(ip->i_sbd, dibh, 0);
  fs_relse_buf(ip->i_sbd,dibh);
	
 out:
  return error;
}

/**
 * fs_lookupi - Look up a filename in a directory and return its inode
 * @dip: The directory to search
 * @name: The name of the inode to look for
 * @cred: The caller's credentials
 * @ipp: Used to return the found inode if any
 *
 * Returns: 0 on success, -EXXXX on failure
 */
static int fs_lookupi(fs_inode_t *dip, osi_filename_t *name,
	       osi_cred_t *cred, fs_inode_t **ipp)
{
  fs_sbd_t *sdp = dip->i_sbd;
  int error = 0;
  identifier_t id;

  memset(&id, 0, sizeof(identifier_t));
  id.filename = name;
  id.type = ID_FILENAME;

  *ipp = NULL;

  if (!name->len || name->len > GFS_FNAMESIZE)
  {
    error = -ENAMETOOLONG;
    goto out;
  }

  if (fs_filecmp(name, (char *)".", 1))
  {
    *ipp = dip;
    goto out;
  }

  error = fs_dir_search(dip, &id, NULL);
  if (error){
    if (error == -ENOENT)
      error = 0;
    goto out;
  }

  error = fs_get_istruct(sdp, id.inum, ST_CREATE, ipp);

 out:
 
  if(id.inum) gfsck_free(id.inum);
  return error;
}

int fs_createi(fs_inode_t *dip, osi_filename_t *name,
	       unsigned int type, unsigned int mode, osi_cred_t *cred,
	       int *new, fs_inode_t **ipp)
{
  osi_list_t *tmp=NULL;
  fs_sbd_t *sdp = dip->i_sbd;
  struct gfs_inum inum;
  int error;
  int allocate=0;
  identifier_t id;

  memset(&id, 0, sizeof(identifier_t));

  if (!name->len || name->len > GFS_FNAMESIZE){
    error = -ENAMETOOLONG;
    goto fail;
  }  

 restart:

  /*  Don't create entries in an unlinked directory  */
  if (!dip->i_di.di_nlink){
    error = -EPERM;
    goto fail;
  }

  id.filename = name;
  id.type = ID_FILENAME;

  error = fs_dir_search(dip, &id, NULL);
  if(id.inum) gfsck_free(id.inum);
  switch (error)
  {
  case -ENOENT:
    break;

  case 0:
    if (!new){
      error = -EEXIST;
      goto fail;
    } else {
      error = fs_lookupi(dip, name, cred, ipp);
      if (error)
        goto fail;

      if (*ipp){
        *new = FALSE;
        return 0;
      } else
        goto restart;
    }
    break;

  default:
    goto fail;
  }

  if (dip->i_di.di_entries == (uint32)-1){
    error = -EFBIG;
    goto fail;
  }
  if (type == GFS_FILE_DIR && dip->i_di.di_nlink == (uint32)-1){
    error = -EMLINK;
    goto fail;
  }

 retry:
  inum.no_addr = inum.no_formal_ino = 0;
  for (tmp = sdp->sd_rglist.next; tmp != &sdp->sd_rglist; tmp = tmp->next){
    uint64 block;
    fs_rgrpd_t *rgd;
    
    rgd = osi_list_entry(tmp, fs_rgrpd_t, rd_list);
    if(fs_rgrp_read(rgd))
      return -1;
    if(rgd->rd_rg.rg_freemeta){
      block = fs_blkalloc_internal(rgd, dip->i_num.no_addr,
				   GFS_BLKST_FREEMETA, GFS_BLKST_USEDMETA, 1);
      block += rgd->rd_ri.ri_data1;
  
      inum.no_addr = inum.no_formal_ino = block;
      rgd->rd_rg.rg_freemeta--;
      rgd->rd_rg.rg_useddi++;

      if(fs_rgrp_recount(rgd)){
	pp_print(PPVH,  "fs_createi:  Unable to recount rgrp blocks.\n");
	fs_rgrp_relse(rgd);
	error = -EIO;
	goto fail;
      }

      /* write out the rgrp */
      gfs_rgrp_out(&rgd->rd_rg, BH_DATA(rgd->rd_bh[0]));
      fs_write_buf(sdp, rgd->rd_bh[0], 0);
      fs_rgrp_relse(rgd);
      break;
    } else {
      if(allocate){
	if(!clump_alloc(rgd, 0)){
	  block = fs_blkalloc_internal(rgd, dip->i_num.no_addr,
				       GFS_BLKST_FREEMETA,
				       GFS_BLKST_USEDMETA, 1);
	  block += rgd->rd_ri.ri_data1;
  
	  inum.no_addr = inum.no_formal_ino = block;
	  rgd->rd_rg.rg_freemeta--;
	  rgd->rd_rg.rg_useddi++;

	  if(fs_rgrp_recount(rgd)){
	    pp_print(PPVH, "fs_createi:  Unable to recount rgrp blocks.\n");
	    fs_rgrp_relse(rgd);
	    error = -EIO;
	    goto fail;
	  }

	  /* write out the rgrp */
	  gfs_rgrp_out(&rgd->rd_rg, BH_DATA(rgd->rd_bh[0]));
	  fs_write_buf(sdp, rgd->rd_bh[0], 0);
	  fs_rgrp_relse(rgd);
	  break;
	}
      }
      fs_rgrp_relse(rgd);
    }      
  }

  if(!inum.no_addr){
    if(allocate){
      pp_print(PPVH, "No space available for new file or directory.\n");
      return -1;
    } else {
      allocate = 1;
      goto retry;
    }
  }

  error = fs_dir_add(dip, name, &inum, type);
  if (error)
    goto fail;

  error = make_dinode(dip, &inum, type, mode, cred);
  if (error)
    goto fail;


  error = fs_get_istruct(sdp, &inum, ST_CREATE, ipp);
  if (error)
    goto fail;

  if (new)
    *new = TRUE;

  return 0;

 fail:
  return error;
}


/*
 * fs_mkdir - make a directory
 * @dip - dir inode that is the parent of the new dir
 * @new_dir - name of the new dir
 * @mode - mode of new dir
 * @nip - returned inode ptr to the new directory
 *
 * This function has one main difference from the way a normal mkdir
 * works.  It will not return an error if the directory already
 * exists.  Instead it will return success and nip will point to the
 * inode that exists with the same name as new_dir.
 *
 * Returns: 0 on success, -1 on failure.
 */
int fs_mkdir(fs_inode_t *dip, char *new_dir, int mode, fs_inode_t **nip){
  int error;
  osi_cred_t creds;
  osi_buf_t *dibh;
  struct gfs_dinode *di;
  struct gfs_dirent *dent;
  fs_inode_t *ip= NULL;
  fs_sbd_t *sdp = dip->i_sbd;
  osi_filename_t name;
  int new;

  name.name = new_dir;
  name.len = strlen(new_dir);
  creds.cr_uid = getuid();
  creds.cr_gid = getgid();

  error = fs_createi(dip, &name, GFS_FILE_DIR, mode, &creds, &new, &ip);

  if (error)
    goto fail;

  if(!new){
    goto out;
  }

  if(!ip){
    pp_print(PPVH,  "fs_mkdir:  fs_createi() failed.\n");
    error = -1;
    goto fail;
  }

  ip->i_di.di_nlink = 2;
  ip->i_di.di_size = sdp->sd_sb.sb_bsize - sizeof(struct gfs_dinode);
  ip->i_di.di_flags |= GFS_DIF_JDATA;
  ip->i_di.di_payload_format = GFS_FORMAT_DE;
  ip->i_di.di_entries = 2;

  error = fs_get_and_read_buf(ip->i_sbd, ip->i_num.no_addr, &dibh, 0);
  if(error){
    pp_print(PPVH, "fs_mkdir:  Unable to aquire directory buffer.\n");
    goto fail;
  }

  di = (struct gfs_dinode *)BH_DATA(dibh);

  error = fs_dirent_alloc(ip, dibh, 1, &dent);
  if(error){  /*  This should never fail  */
    pp_print(PPVH, "fs_mkdir:  fs_dirent_alloc() failed for \".\" entry.\n");
    goto fail;
  }

  dent->de_inum = di->di_num;  /*  already GFS endian  */
  dent->de_hash = gfs_dir_hash(".", 1);
  dent->de_hash = cpu_to_gfs32(dent->de_hash);
  dent->de_type = cpu_to_gfs16(GFS_FILE_DIR);
  memcpy((char *)(dent + 1), ".", 1);
  di->di_entries = cpu_to_gfs32(1);

  error = fs_dirent_alloc(ip, dibh, 2, &dent);
  if(error){  /*  This should never fail  */
    pp_print(PPVH, "fs_mkdir:  fs_dirent_alloc() failed for \"..\" entry.\n");
    goto fail;
  }
  gfs_inum_out(&dip->i_num, (char *)&dent->de_inum);
  dent->de_hash = gfs_dir_hash("..", 2);
  dent->de_hash = cpu_to_gfs32(dent->de_hash);
  dent->de_type = cpu_to_gfs16(GFS_FILE_DIR);
  memcpy((char *)(dent + 1), "..", 2);

  gfs_dinode_out(&ip->i_di, (char *)di);
  if(fs_write_buf(ip->i_sbd, dibh, 0)){
    pp_print(PPVH, "fs_mkdir:  Bad fs_write_buf()\n");
    error = -EIO;
    goto fail;
  }

  fs_relse_buf(ip->i_sbd, dibh);
  

  error = fs_change_nlink(dip, +1);
  if(error){
    pp_print(PPVH, "fs_mkdir:  fs_change_nlink() failed.\n");
    goto fail;
  }

 out:
  error=0;
  if(nip)
    *nip = ip;
 fail:

  return error;
}


/*
 * remove_leaf_chain
 * @leaf_no
 *
 * Given the starting leaf block, this function removes
 * leaf chains.
 *
 * Returns: 0 on success, -1 on error
 */
static int remove_leaf_chain(fs_sbd_t *sdp, uint64 leaf_no){
  struct gfs_leaf   *leaf;
  osi_buf_t    *bh;
  int error = 0;

  while(leaf_no){
    error = fs_get_and_read_buf(sdp, leaf_no, &bh, 0);
    if(error){
      pp_print(PPN, "Unable to get leaf block (%"PRIu64") buffer\n",
	       leaf_no);
      pp_print(PPN, "Skipping...\n");
      break;
    }
    if(check_meta(bh, GFS_METATYPE_LF)){
      pp_print(PPN, "Leaf block #%"PRIu64" is corrupted or unfilled\n",
	       leaf_no);
      pp_print(PPN, "Skipping...\n");
      break;
    } 

    fs_set_bitmap(sdp, BH_BLKNO(bh), GFS_BLKST_FREE);
    set_bitmap(sdp, BH_BLKNO(bh), GFS_BLKST_FREE);

    leaf = (struct gfs_leaf *)BH_DATA(bh);
    if(leaf->lf_next){
      leaf_no = gfs64_to_cpu(leaf->lf_next);
    } else {
      leaf_no = 0;
    }
    memset(BH_DATA(bh), 0, BH_SIZE(bh));
    fs_write_buf(sdp, bh, 0);  /* don't really care if this fails */
    fs_relse_buf(sdp, bh);
  }
  return error;
}


/*
 * fs_remove - remove a file or directory from disk
 * @ip: inode of file to remove
 *
 * This function removes as traces of a file or of
 * a directory file.  In the case of a directory, the
 * files it contains and subdirectories are *not*
 * removed.  This will leave all of its contents
 * unlinked, if not removed ahead of time.
 *
 * Returns: 0 on success, -1 on error
 */
int fs_remove(fs_inode_t *ip)
{
  fs_sbd_t      *sdp = ip->i_sbd;
  fs_rgrpd_t    *rgd = fs_blk2rgrpd(sdp, ip->i_num.no_addr);
  osi_list_t    metalist[GFS_MAX_META_HEIGHT];
  osi_list_t    *list, *tmp;
  osi_buf_t     *bh;
  uint32        height = ip->i_di.di_height;
  uint64        *ptr;
  int           top, bottom, head_size;
  int           i, error=0;

  pp_print(PPVL, "Removing dinode #%"PRIu64", a %s\n",
           ip->i_num.no_addr,
           (ip->i_di.di_type == GFS_FILE_REG) ? 
           "File":
           (ip->i_di.di_type == GFS_FILE_DIR) ? 
           "Directory":
           (ip->i_di.di_type == GFS_FILE_LNK) ? 
           "Link File":
           (ip->i_di.di_type == GFS_FILE_BLK) ? 
           "Block Device":
           (ip->i_di.di_type == GFS_FILE_CHR) ? 
           "Character Device":
           (ip->i_di.di_type == GFS_FILE_FIFO) ? 
           "FIFO":
           (ip->i_di.di_type == GFS_FILE_SOCK) ? 
           "Socket":
           "Unknown type");

  if(ip->i_sbd->sd_rooti->i_num.no_addr == ip->i_num.no_addr){
    pp_print(PPVH,"HEAH HO!  Can not remove root dinode!\n");
    return -1;
  }
             
  if(ip->i_di.di_type != GFS_FILE_REG && 
     ip->i_di.di_type != GFS_FILE_LNK &&
     ip->i_di.di_type != GFS_FILE_DIR){
    pp_print(PPVH, "Unable to handle removal of file of this type.\n");
    return -1;
  }

  if((ip->i_di.di_type == GFS_FILE_REG) && (ip->i_di.di_nlink > 1)){
    pp_print(PPVH, "Multi-linked file, can not remove.\n");
    return -1;
  }

  for (i = 0; i < GFS_MAX_META_HEIGHT; i++)
    osi_list_init(&metalist[i]);

  /* create metalist for each level */

  if (build_metalist(ip, &metalist[0])){
    pp_print(PPVH, "bad metadata tree for dinode %"PRIu64"\n",
         ip->i_di.di_num.no_addr);
    return -1;
  }

  if(height){
    /* remove data (or leaf) blocks */
    list = &metalist[height - 1];
    for (tmp = list->next; tmp != list; tmp = tmp->next){
      bh = osi_list_entry(tmp, osi_buf_t, b_list);
      head_size = 
        (height != 1) ? sizeof(struct gfs_indirect) : sizeof(struct gfs_dinode);
      ptr = (uint64 *)(bh->b_data + head_size);
      for ( ; (char *)ptr < (bh->b_data + bh->b_size); ptr++){
        if (!*ptr)
          continue;
	if(ip->i_di.di_type != GFS_FILE_DIR){
	  remove_leaf_chain(sdp, *ptr);
	} else {
	  fs_set_bitmap(sdp, gfs64_to_cpu(*ptr), GFS_BLKST_FREE);
        }
      }
    }

    /* remove indirect blocks and inode block*/
    top = 1;
    bottom = height - 1;

    while (top <= bottom){
      list = &metalist[top];

      for (tmp = list->next; tmp != list; tmp = tmp->next){
        bh = osi_list_entry(tmp, osi_buf_t, b_list);
        if(fs_get_bitmap(sdp, BH_BLKNO(bh), NULL)){
          error = fs_set_bitmap(sdp, BH_BLKNO(bh), GFS_BLKST_FREE);
        }
        if (error){
          pp_print(PPVH,
                   "Unable to set bitmap for indirect block #%"PRIu64".\n",
                   BH_BLKNO(bh));
          goto free_metalist;
        }
      }
      top++;
    }
  }

  /* remove inode block */
  if(fs_get_bitmap(sdp, ip->i_di.di_num.no_addr, NULL)){
    error = fs_set_bitmap(sdp, ip->i_di.di_num.no_addr, GFS_BLKST_FREE);
    if(error){
      pp_print(PPVH, "Unable to set bitmap for dinode block #%"PRIu64".\n",
	       ip->i_di.di_num.no_addr);
      goto free_metalist;
    }
  }
  
  if(fs_rgrp_read(rgd) || fs_rgrp_recount(rgd))
    die("remove_file:  Unable to recalculate rgrp block counts.\n");

  gfs_rgrp_out(&rgd->rd_rg, BH_DATA(rgd->rd_bh[0]));
  fs_write_buf(rgd->rd_sbd, rgd->rd_bh[0], 0);
  fs_rgrp_relse(rgd);
  
 free_metalist:
  for (i = 0; i < GFS_MAX_META_HEIGHT; i++){
    list = &metalist[i];
    while (!osi_list_empty(list)){
      bh = osi_list_entry(list->next, osi_buf_t, b_list);
      osi_list_del(&bh->b_list);
      fs_relse_buf(ip->i_sbd, bh);
    }
  }

  if(error) pp_print(PPVH, "Failed to remove file.\n");
  return error;
}
