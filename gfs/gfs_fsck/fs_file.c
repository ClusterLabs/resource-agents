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
#include "fs_bio.h"
#include "fs_bmap.h"
#include "fs_inode.h"

#include "fs_file.h"

/**
 * fs_readi - Read a file
 * @ip: The GFS Inode
 * @buf: The buffer to place result into
 * @offset: File offset to begin reading from
 * @size: Amount of data to transfer
 *
 * Returns: The amount of data actually copied or the error
 */
int fs_readi(fs_inode_t *ip, void *buf, uint64 offset, unsigned int size)
{
  fs_sbd_t *sdp = ip->i_sbd;
  osi_buf_t *bh;
  uint64 lblock, dblock=0;
  uint32 extlen = 0;
  unsigned int amount;
  int not_new = 0;
  int journaled = fs_is_jdata(ip);
  int copied = 0;
  int error = 0;

  if (offset >= ip->i_di.di_size){
    pp_print(PPVL, "fs_readi:  Offset (%"PRIu64") is >= "
	     "the file size (%"PRIu64").\n", offset, ip->i_di.di_size);
    goto out;
  }
	
  if ((offset + size) > ip->i_di.di_size)
    size = ip->i_di.di_size - offset;
	
  if (!size){
    pp_print(PPVL, "fs_readi:  Nothing to be read.\n");
    goto out;
  }
	
  if (journaled){
    lblock = offset / sdp->sd_jbsize;
    offset %= sdp->sd_jbsize;
  }
  else{
    lblock = offset >> sdp->sd_sb.sb_bsize_shift;
    offset &= sdp->sd_sb.sb_bsize - 1;
  }
	
  if (fs_is_stuffed(ip))
    offset += sizeof(struct gfs_dinode);
  else if (journaled)
    offset += sizeof(struct gfs_meta_header);
	
	
  while (copied < size){
    amount = size - copied;
    if (amount > sdp->sd_sb.sb_bsize - offset)
      amount = sdp->sd_sb.sb_bsize - offset;
	
    if (!extlen){
      error = fs_block_map(ip, lblock, &not_new, &dblock, &extlen);
      if (error){
	pp_print(PPH, "fs_readi:  The call to fs_block_map() failed.\n");
	goto out;
      }
    }
	
    if (dblock){
      error = fs_get_and_read_buf(ip->i_sbd, dblock, &bh, 0);
      if (error){
	pp_print(PPH, "fs_readi:  Unable to perform fs_get_and_read_buf()\n");
	goto out;
      }
	
      dblock++;
      extlen--;
    }
    else
      bh = NULL;

    if (bh){
      memcpy(buf+copied, BH_DATA(bh)+offset, amount);
      fs_relse_buf(ip->i_sbd, bh);
    } else {
      memset(buf+copied, 0, amount);
    }
    copied += amount;
    lblock++;
    
    offset = (journaled) ? sizeof(struct gfs_meta_header) : 0;
  }

 out:

  return (error < 0) ? error : copied;
}



/**
 * fs_writei - Write bytes to a file
 * @ip: The GFS inode
 * @buf: The buffer containing information to be written
 * @offset: The file offset to start writing at
 * @size: The amount of data to write
 *
 * Returns: The number of bytes correctly written or error code
 */
int fs_writei(fs_inode_t *ip, void *buf, uint64 offset, unsigned int size)
{
  fs_sbd_t *sdp = ip->i_sbd;
  osi_buf_t *dibh, *bh;
  uint64 lblock, dblock;
  uint32 extlen = 0;
  unsigned int amount;
  int new;
  int journaled = fs_is_jdata(ip);
  const uint64 start = offset;
  int copied = 0;
  int error = 0;
	
  /*  Bomb out on writing nothing.
      Posix says we can't change the time here.  */
	
  if (!size)
    goto fail;  /*  Not really an error  */
	
	
  if (fs_is_stuffed(ip) && 
      ((start + size) > (sdp->sd_sb.sb_bsize - sizeof(struct gfs_dinode)))){
    error = fs_unstuff_dinode(ip);
    if (error)
      goto fail;
  }
	
	
  if (journaled){
    lblock = offset / sdp->sd_jbsize;
    offset %= sdp->sd_jbsize;
  }
  else{
    lblock = offset >> sdp->sd_sb.sb_bsize_shift;
    offset &= sdp->sd_sb.sb_bsize - 1;
  }
	
  if (fs_is_stuffed(ip))
    offset += sizeof(struct gfs_dinode);
  else if (journaled)
    offset += sizeof(struct gfs_meta_header);
  
  
  while (copied < size){
    amount = size - copied;
    if (amount > sdp->sd_sb.sb_bsize - offset)
      amount = sdp->sd_sb.sb_bsize - offset;
    
    if (!extlen){
      new = TRUE;
      error = fs_block_map(ip, lblock, &new, &dblock, &extlen);
      if (error)
	goto fail;
      if(!dblock){
	pp_print(PPVH, "fs_writei:  "
		 "Unable to map logical block to real block.\n");
	pp_print(PPVH, "Uncircumventable error.\n");
	exit(EXIT_FAILURE);
      }
    }

    error = fs_get_and_read_buf(ip->i_sbd, dblock, &bh, 0);
    if (error)
      goto fail;
    
    memcpy(BH_DATA(bh)+offset, buf+copied, amount);
    fs_write_buf(ip->i_sbd, bh, 0);
    fs_relse_buf(ip->i_sbd, bh);
    
    copied += amount;
    lblock++;
    dblock++;
    extlen--;
    
    offset = (journaled) ? sizeof(struct gfs_meta_header) : 0;
  }
  
  
 out:
  error = fs_get_and_read_buf(ip->i_sbd, ip->i_num.no_addr, &dibh, 0);
  if (error){
    pp_print(PPVH, "fs_writei:  "
	     "Unable to get inode buffer.\n");
    return -1;
  }

  error = check_meta(dibh, GFS_METATYPE_DI);
  if(error){
    pp_print(PPVH, "fs_writei:  "
	     "Buffer is not a valid inode.\n");
    fs_relse_buf(ip->i_sbd, dibh);
    return -1;
  }

  if (ip->i_di.di_size < start + copied)
    ip->i_di.di_size = start + copied;
  ip->i_di.di_mtime = ip->i_di.di_ctime = osi_current_time();
  
  gfs_dinode_out(&ip->i_di, BH_DATA(dibh));
  fs_write_buf(ip->i_sbd, dibh, 0);
  fs_relse_buf(ip->i_sbd, dibh);
  
  return copied;
  
  
  
 fail:
  if (copied)
    goto out;
  
  return error;
}
	
