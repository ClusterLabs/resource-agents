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

/*
 * fs_get_buf - get a buffer
 * @sdp: the super block
 * @blkno: blk # that this buffer will be associated with
 * @bhp: the location where the buffer is returned
 *
 * This function allocates space for a buffer head structure
 * and the corresponding data.  It does not fill in the
 * actual data - that is done by read_buf.
 * 
 * Returns: 0 on success, -1 on error
 */
int fs_get_buf(fs_sbd_t *sdp, uint64 blkno, osi_buf_t **bhp){
  osi_buf_t *bh = NULL;

  *bhp = NULL;
  bh = (osi_buf_t *)gfsck_zalloc(sizeof(osi_buf_t));
  if(!bh){
    pp_print(PPVH, "Unable to allocate memory for new buffer head.\n");
    return -1;
  }
  BH_BLKNO(bh) = blkno;
  BH_SIZE(bh) = sdp->sd_sb.sb_bsize;
  BH_STATE(bh) = 0;
  BH_DATA(bh) = gfsck_zalloc(BH_SIZE(bh));

  if(!BH_DATA(bh)){
    gfsck_free(bh);
    pp_print(PPVH, "Unable to allocate memory for new buffer "
	     "blkno = %"PRIu64", size = %u\n", blkno, BH_SIZE(bh));
    return -1;
  }
  *bhp = bh;

  return 0;
}


/*
 * fs_relse_buf - release a buffer
 * @sdp: the super block
 * @bh: the buffer to release
 *
 * This function will release the memory of the buffer
 * and associated buffer head.
 *
 * Returns: nothing
 */
void fs_relse_buf(fs_sbd_t *sdp, osi_buf_t *bh){
  if(bh){
    if(BH_DATA(bh))
      gfsck_free(BH_DATA(bh));
    gfsck_free(bh);
  }
}


/*
 * fs_read_buf - read a buffer
 * @sdp: the super block
 * @blkno: block number
 * @bhp: place where buffer is returned
 * @flags:
 *
 * Returns 0 on success, -1 on error
 */
int fs_read_buf(fs_sbd_t *sdp, osi_buf_t *bh, int flags){
  int disk_fd = sdp->sd_diskfd;

  if(do_lseek(disk_fd, (uint64)(BH_BLKNO(bh)*BH_SIZE(bh)))){
    pp_print(PPVH,
	     "Unable to seek to position %"PRIu64" "
	     "(%"PRIu64" * %u) on storage device.\n",
	     (uint64)(BH_BLKNO(bh)*BH_SIZE(bh)),BH_BLKNO(bh),BH_SIZE(bh));
    return -1;
  }
  if(do_read(disk_fd, BH_DATA(bh), BH_SIZE(bh))){
    pp_print(PPVH, "Unable to read %u bytes from position %"PRIu64"\n",
	     BH_SIZE(bh),(uint64)(BH_BLKNO(bh)*BH_SIZE(bh)));
    return -1;
  }
  return 0;
}


/*
 * fs_write_buf - write a buffer
 * @sdp: the super block
 * @bh: buffer head that describes buffer to write
 * @flags: flags that determine usage
 *
 * Returns: 0 on success, -1 on failure
 */
int fs_write_buf(fs_sbd_t *sdp, osi_buf_t *bh, int flags){
  int disk_fd = sdp->sd_diskfd;

  if(do_lseek(disk_fd, (uint64)(BH_BLKNO(bh)*BH_SIZE(bh))))
    return -1;

  if(do_write(disk_fd, BH_DATA(bh), BH_SIZE(bh)))
    return -1;

  if(flags & BW_WAIT){
    fsync(disk_fd);
  }

  return 0;
}


/*
 * fs_get_and_read_buf - combines get_buf and read_buf functions
 * @sdp
 * @blkno
 * @bhp
 * @flags
 *
 * Returns: 0 on success, -1 on error
 */
int fs_get_and_read_buf(fs_sbd_t *sdp, uint64 blkno, osi_buf_t **bhp, int flags){
  if(fs_get_buf(sdp, blkno, bhp))
    return -1;

  if(fs_read_buf(sdp, *bhp, flags)){
    fs_relse_buf(sdp, *bhp);
    *bhp = NULL;  /* guarantee that ptr is NULL in failure cases */
    return -1;
  }

  return 0;
}

