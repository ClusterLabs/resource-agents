/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/


#include "util.h"
#include "bio.h"

#include "fs_recovery.h"

/*
 * reconstruct_single_journal - write a fresh journal
 * @sdp: superblock
 * @jnum: journal number
 *
 * This function will write a fresh journal over the top of
 * the previous journal.  All journal information is lost.  This
 * process is basically stolen from write_journals() in the mkfs code.
 *
 * Returns: -1 on error, 0 otherwise
 */
static int reconstruct_single_journal(struct fsck_sb *sdp, int jnum){
  struct gfs_log_header	lh;
  struct gfs_jindex    	*jdesc = &(sdp->jindex[jnum]);
  uint32		seg, sequence;
  char			buf[sdp->sb.sb_bsize];

  srandom(time(NULL));
  sequence = jdesc->ji_nsegment / (RAND_MAX + 1.0) * random();

  log_info("Clearing journal %d\n", jnum);

  for (seg = 0; seg < jdesc->ji_nsegment; seg++){
    memset(buf, 0, sdp->sb.sb_bsize);
    memset(&lh, 0, sizeof(struct gfs_log_header));

    lh.lh_header.mh_magic = GFS_MAGIC;
    lh.lh_header.mh_type = GFS_METATYPE_LH;
    lh.lh_header.mh_format = GFS_FORMAT_LH;
    lh.lh_header.mh_generation = 0x101674;
    lh.lh_flags = GFS_LOG_HEAD_UNMOUNT;
    lh.lh_first = jdesc->ji_addr + seg * sdp->sb.sb_seg_size;
    lh.lh_sequence = sequence;

    gfs_log_header_out(&lh, buf);
    gfs_log_header_out(&lh,
		       buf + GFS_BASIC_BLOCK - sizeof(struct gfs_log_header));

    if(do_lseek(sdp->diskfd, lh.lh_first * sdp->sb.sb_bsize) ||
       do_write(sdp->diskfd, buf, sdp->sb.sb_bsize)){
      log_err("Unable to reconstruct journal %d.\n", jnum);
      return -1;
    }

    if (++sequence == jdesc->ji_nsegment)
      sequence = 0;
  }
  return 0;
}


/*
 * reconstruct_journals - write fresh journals
 * sdp: the super block
 *
 * Returns: 0 on success, -1 on failure
 */
int reconstruct_journals(struct fsck_sb *sdp){
  int i;

  log_warn("Clearing journals (this may take a while)\n");
  for(i=0; i < sdp->journals; i++) {
    if((i % 10) == 0)
      log_at_notice(".");
    if(reconstruct_single_journal(sdp, i))
      return -1;
  }
  log_notice("Cleared journals\n");
  return 0;
}
