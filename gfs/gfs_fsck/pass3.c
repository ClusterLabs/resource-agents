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
#include "fs_bits.h"
#include "fs_dir.h"
#include "fs_inode.h"
#include "fs_rgrp.h"

#include "pass3.h"

#define DIR_LINEAR 1
#define DIR_EXHASH 2

typedef struct dir_status
{
  uint64 entries;   /* number of dir entries found */
  uint64 dot;       /* the ino number of the . entry if found */
  uint64 dotdot;    /* the ino number of the .. entry if found */
  uint64 subdirs;   /* the number of entries which are subdirs */
  int	 update;    /* corrections made, needs to be written */
  int    remove;    /* this directory should be eliminated */
  int    l_f;       /* this directory should be moved to l+f */
} dir_status_t;

static int rerun = 0;

/**
 * record_dinode_dirent
 * @sdp:
 * @ino:
 *
 */
static int record_dinode_dirent(fs_sbd_t *sdp, uint64 ino)
{
  di_info_t *dinfo;

  switch(get_bitmap(sdp, ino, NULL)){
  case 0: /* bit not set */
    return set_bitmap(sdp, ino, 1);
    break;
  case 1: /* bit already set */
    dinfo = search_list(&sdp->sd_dirent_list, ino);
    if (!dinfo){
      dinfo = (di_info_t *)gfsck_zalloc(sizeof(di_info_t));
      osi_list_add(&dinfo->din_list, &sdp->sd_dirent_list);
      dinfo->din_addr = ino;
      dinfo->din_dirents = 2;
      pp_print(PPVL, "Found 2 references to dinode #%"PRIu64"\n", ino);
    } else {
      dinfo->din_dirents++;
      pp_print(PPVL, "Found %u references to dinode #%"PRIu64"\n",
	       dinfo->din_dirents, ino);
    }
    return 0;
    break;
  default: /* error */
    return -1;
    break;
  }
}

static int record_eattr_blk(fs_sbd_t *sdp, uint64 ino)
{
  switch(get_bitmap(sdp, ino, NULL)){
  case 0: /* bit not set */
    return set_bitmap(sdp, ino, 1);
  case 1: /* bit already set */
    pp_print(PPVL, "Found 2 references to EA blk #%"PRIu64"\n", ino);
  default: /* error */
    return -1;
    break;
  }
}

static int mark_leaf_eattr(fs_inode_t *ip, uint64 leaf_blk){
  int i;
  uint64 *ea_data_ptr;
  fs_sbd_t *sdp = ip->i_sbd;
  osi_buf_t *leaf_bh = NULL;
  struct gfs_ea_header *ea_hdr = NULL;

  if(fs_get_and_read_buf(sdp, leaf_blk, &leaf_bh, 0)){
    pp_print(PPVH, "Unable to read EA leaf block #%"PRIu64".\n", leaf_blk);
    return -1;
  }

  ea_hdr = (struct gfs_ea_header *)(BH_DATA(leaf_bh) +
                                    sizeof(struct gfs_meta_header));
  while(1){
    ea_data_ptr = ((uint64 *)((char *)ea_hdr + sizeof(struct gfs_ea_header) +
                    ((ea_hdr->ea_name_len + 7) & ~7)));

    for(i = 0; i < ea_hdr->ea_num_ptrs; i++){
      /* mark all extended leaf eattrs */
      if(record_eattr_blk(sdp, gfs64_to_cpu(ea_data_ptr[i]))){
	goto fail;
      }
    }
    if(ea_hdr->ea_flags & GFS_EAFLAG_LAST){
      break;
    }
    ea_hdr = (struct gfs_ea_header *)((char *)(ea_hdr) +
                                  gfs32_to_cpu(ea_hdr->ea_rec_len));    
  }
  /* mark this leaf eattr */
  if(record_eattr_blk(sdp, leaf_blk)){
    goto fail;
  }
  fs_relse_buf(sdp, leaf_bh);
  return 0;

 fail:
  if(leaf_bh){
    fs_relse_buf(sdp, leaf_bh);
  }
  return -1;
}

static int mark_indirect_eattr(fs_inode_t *ip, uint64 indir_blk){
  uint64 *ea_leaf_ptr, *end;
  fs_sbd_t *sdp = ip->i_sbd;
  osi_buf_t *indir_bh = NULL;

  if(fs_get_and_read_buf(sdp, indir_blk, &indir_bh, 0)){
    pp_print(PPVH, "Unable to read EA indirect block #%"PRIu64".\n", indir_blk);
    return -1;
  }

  ea_leaf_ptr = (uint64 *)(BH_DATA(indir_bh) + sizeof(struct gfs_indirect)); 
  end = ea_leaf_ptr + ((sdp->sd_sb.sb_bsize - sizeof(struct gfs_indirect)) / 8); 

  while(*ea_leaf_ptr && (ea_leaf_ptr < end)){
    if(mark_leaf_eattr(ip, gfs64_to_cpu(*ea_leaf_ptr))){
      goto fail;
    }
    ea_leaf_ptr++;
  }

  /* mark this indirect eattr */
  if(record_eattr_blk(sdp, indir_blk)){
    goto fail;
  }

  fs_relse_buf(sdp, indir_bh);
  return 0;

 fail:
  fs_relse_buf(sdp, indir_bh);
  return -1;
}


/**
 * check_entries - 
 * 
 * @ip: the inode of the dir being searched
 * @bh: buffer containing entries
 * @rgd: rgrp of the directory (unused in "target" mode)
 * @index: used to verify hash values
 * @type: linear or exhash
 * @status: structure that will containing results of check
 *
 * This routine cycles through all directory entries,  checking and
 * counting them.
 *  status->entries - number of entries found
 *  status->dot - inode number of . entry if found
 *  status->dotdot - inode number of .. entry if found
 *  status->subdirs - number of entries found which are subdirs
 *  status->update - errors were found, but were corrected - write dir
 *  status->remove - errors were found so this dir should be removed
 *  status->l_f - errors were found, move to l+f
 *
 * Returns: 0 on success, -1 on error
 */
static int check_entries(fs_inode_t *ip, osi_buf_t *bh,
			 fs_rgrpd_t *rgd, uint32 index,
			 int type, dir_status_t *status)
{
  fs_sbd_t *sdp = ip->i_sbd;
  struct gfs_dirent *dent;
  struct gfs_dirent de;
  struct gfs_leaf *leaf = NULL;
  fs_inode_t eip_s;
  fs_inode_t *eip = &eip_s;
  char *bh_end, *filename;
  char tmp_name[256];
  uint32 count = 0, off, len;
  int error, val;
  int		first=1;
  struct gfs_dirent	previous;
  struct gfs_dirent	*previous_ptr = NULL;
  char 		*previous_filename = NULL;

  bh_end = BH_DATA(bh) + BH_SIZE(bh);

  if (type == DIR_LINEAR){
    pp_print(PPVL, "Checking dir entries in linear dir #%"PRIu64"\n",
	     ip->i_num.no_addr);
    dent = (struct gfs_dirent *)(BH_DATA(bh) + sizeof(struct gfs_dinode));
  }else{
    pp_print(PPVL, "Checking dir entries in exhash dir #%"PRIu64", "
	     "leaf block #%"PRIu64"\n",
	     ip->i_num.no_addr,
	     BH_BLKNO(bh));
    dent = (struct gfs_dirent *)(BH_DATA(bh) + sizeof(struct gfs_leaf));
    leaf = (struct gfs_leaf *)BH_DATA(bh);
  }

  previous_ptr = dent;
  while (1)                /* foreach entry */
  {
    memset(&de, 0, sizeof(struct gfs_dirent));
    gfs_dirent_in(&de, (char *)dent);
    filename = (char *)dent + sizeof(struct gfs_dirent);

    /* if this is the first dirent and is empty */
    if (!de.de_inum.no_formal_ino){
      if(first){
	pp_print(PPVL, "First dirent is a sentinel (place holder).\n");
	first = 0;
	memset(&previous, 0, sizeof(struct gfs_dirent));
	goto next_no_count;
      }else{
	pp_print(PPN, "Dirent has illegal 0 address\n");
	pp_print(PPN, "Dirent contents:\n"
		 "  de_inum     = [%"PRIu64", %"PRIu64"]\n"
		 "  de_hash     = %u\n"
		 "  de_rec_len  = %u\n"
		 "  de_name_len = %u\n"
		 "  de_type     = %u\n",
		 de.de_inum.no_formal_ino,
		 de.de_inum.no_addr,
		 de.de_hash,
		 de.de_rec_len,
		 de.de_name_len,
		 de.de_type);
	goto remove;
      }
    }
    if(first){
      first=0;
      memset(&previous, 0, sizeof(struct gfs_dirent));
    }

    if (de.de_rec_len < GFS_DIRENT_SIZE(de.de_name_len)){
      pp_print(PPN, "Dir entry with bad record or name length\n"
	       "\tRecord length = %u\n"
	       "\tName length = %u\n",
	       de.de_rec_len,
	       de.de_name_len);
      goto remove;
    }

    if (de.de_hash != gfs_dir_hash(filename, de.de_name_len)){
      pp_print(PPN, "Dir entry with bad hash or name length\n"
	       "\tHash found         = %u\n"
	       "\tName found         = %s\n"
	       "\tName length found  = %u\n"
	       "\tHash expected      = %u\n",
	       de.de_hash,
	       filename,
	       de.de_name_len,
	       gfs_dir_hash(filename, de.de_name_len));
      goto remove;
    }

    memset(tmp_name, 0, 256);
    if(de.de_name_len < 256){
      strncpy(tmp_name, filename, de.de_name_len);
    } else {
      strncpy(tmp_name, filename, 255);
    }

    /* it is possible for any fs_mkdir called during this fsck to **
    ** use space previously on by a dir that was removed...  If   **
    ** that dir still has a dirent somewhere, there could then be **
    ** multiple dirents for the same dir, which is bad.  Since we **
    ** only use fs_mkdir to create an l+f dir, we check for that  **
    ** here.  If there are ever any calls to fs_mkdir for other   **
    ** reasons, those addresses will also have to be checked..... */
    if (sdp->sd_lf_dip &&
	(de.de_inum.no_addr == sdp->sd_lf_dip->i_num.no_addr) &&
	(strcmp(tmp_name, "l+f") && 
	 strcmp(tmp_name, ".") && strcmp(tmp_name, "..")))
    {
      pp_print(PPN, "Dir #%"PRIu64" has stale entry (%s)\n",
	       ip->i_num.no_addr, tmp_name);
      if(query("\tClear dir entry? (y/n) ")){
	previous.de_rec_len += de.de_rec_len;
	gfs_dirent_out(&previous, (char *)previous_ptr);
	status->update=1;
	pp_print(PPN, "Dir entry cleared... awaiting sync.\n");
	goto next_no_step;
      } else {
	pp_print(PPH, "Bad dir entry remains.\n");
	goto next;
      }
    }

    if (type == DIR_EXHASH){
      off = de.de_hash >> (32 - ip->i_di.di_depth);
      len = 1 << (ip->i_di.di_depth - gfs16_to_cpu(leaf->lf_depth));

      if ((index > off) || (off >= (index + len)))
      {
	pp_print(PPN, "Dir #%"PRIu64" has entry (%s) with bad hash value\n"
		 "\tde_hash = 0x%x\n"
		 "\tdi_depth = %u\n"
		 "\tlf_depth = %u\n"
		 "\tindex = %u\n"
		 "\toff = %u\n"
		 "\tlen = %u\n",
		 ip->i_num.no_addr, tmp_name,
		 de.de_hash,
		 ip->i_di.di_depth,
		 gfs16_to_cpu(leaf->lf_depth),
		 index,
		 off,
		 len);
	if(query("\tClear dir entry? (y/n) ")){
	  previous.de_rec_len += de.de_rec_len;
	  gfs_dirent_out(&previous, (char *)previous_ptr);
	  status->update=1;
	  pp_print(PPN, "Dir entry cleared... awaiting sync.\n");
	  goto next_no_step;
	} else {
	  pp_print(PPH, "Bad dir entry remains.\n");
	  goto next;
	}
      }
    }

    error = check_range(sdp, de.de_inum.no_addr);
    if (error)
    {
      pp_print(PPN, "Dir #%"PRIu64" has entry (%s) with "
	       "address out of range\n",
	       ip->i_num.no_addr, tmp_name);
      if(query("\tClear dir entry? (y/n) ")){
	previous.de_rec_len += de.de_rec_len;
	gfs_dirent_out(&previous, (char *)previous_ptr);
	status->update=1;
	pp_print(PPN, "Dir entry cleared... awaiting sync.\n");
	goto next_no_step;
      } else {
	pp_print(PPH, "Bad dir entry remains.\n");
	goto next;
      }
    }
    
    val = fs_get_bitmap(sdp, de.de_inum.no_addr, NULL);

    if (val != GFS_BLKST_USEDMETA)
    {
      if(!strncmp(".", filename, de.de_name_len)){
	pp_print(PPN, "Dir #%"PRIu64" is marked as "
		 "non-existant in FS bitmap.\n",
		 ip->i_num.no_addr);
	if(query("\tUpdate bitmap? (y/n) ")){
	  if(fs_set_bitmap(sdp, de.de_inum.no_addr, GFS_BLKST_USEDMETA)){
	    pp_print(PPVH, "Unable to set bitmap.\n");
	    goto remove;
	  } else {
	    pp_print(PPL, "Bitmap updated.\n");
	  }
	} else {
	  goto remove;
	}
      } else if(!strncmp("..", filename, de.de_name_len)){
	pp_print(PPN,
		 "Directory #%"PRIu64" has parent w/ bad bitmap value.\n",
		 ip->i_num.no_addr);
	pp_print(PPN, "Tagging for lost and found inclusion.\n");
	status->l_f = TRUE;
      } else {
	pp_print(PPN, "Dir #%"PRIu64" has entry (%s) "
		 "with incorrect FS bitmap value, %d.\n",
		 ip->i_num.no_addr, tmp_name, val);
	if(query("\tClear faulty dir entry? (y/n) ")){
	  if(val){
	    fs_set_bitmap(sdp, de.de_inum.no_addr, GFS_BLKST_FREE);
	  }
	  previous.de_rec_len += de.de_rec_len;
	  gfs_dirent_out(&previous, (char *)previous_ptr);
	  status->update=1;
	  pp_print(PPN, "Dir entry cleared... awaiting sync.\n");
	  goto next_no_step;
	} else {
	  pp_print(PPH, "Bad dir entry remains.\n");
	  goto next;
	}
      }
    }

    pp_print(PPD, "dirent (%s,%"PRIu64") looks good, proceeding with "
	     "dinode check.\n", tmp_name, de.de_inum.no_addr);

    memset(eip, 0, sizeof(fs_inode_t));
    eip->i_sbd = sdp;

    eip->i_num.no_addr = eip->i_num.no_formal_ino = de.de_inum.no_addr;

    if(fs_copyin_dinode(eip)){
      pp_print(PPN, "Unable to read dinode for dir entry (%s, %"PRIu64")\n",
	       tmp_name, de.de_inum.no_addr);
      if(query("\tClear dir entry and bitmap? (y/n) ")){
	if(fs_set_bitmap(sdp, de.de_inum.no_addr, GFS_BLKST_FREE)){
	  pp_print(PPVH, "Unable to perform fs_set_bitmap().\n");
	  pp_print(PPVH, "Bad dinode reference remains.\n");
	  goto next;
	}
	previous.de_rec_len += de.de_rec_len;
	gfs_dirent_out(&previous, (char *)previous_ptr);
	status->update=1;
	pp_print(PPN, "Bad dinode reference cleared... awaiting sync.\n");
	goto next_no_step;
      } else {
	pp_print(PPH, "Bad dinode reference remains.\n");
	goto next;
      }
    }

    if((eip->i_di.di_type == GFS_FILE_REG) || 
       (eip->i_di.di_type == GFS_FILE_DIR) ||
       (eip->i_di.di_type == GFS_FILE_LNK) ||
       (eip->i_di.di_type == GFS_FILE_BLK) ||
       (eip->i_di.di_type == GFS_FILE_CHR) ||
       (eip->i_di.di_type == GFS_FILE_FIFO) || 
       (eip->i_di.di_type == GFS_FILE_SOCK)){
    pp_print(PPVL, "Entry #%3d  %20s %10"PRIu64"\tDI: %s  DE: %s\n",
	     (count+1),
	     tmp_name,
	     de.de_inum.no_addr,
	     (eip->i_di.di_type == GFS_FILE_REG) ? 
	     "File":
	     (eip->i_di.di_type == GFS_FILE_DIR) ? 
	     "Directory":
	     (eip->i_di.di_type == GFS_FILE_LNK) ? 
	     "Link":
	     (eip->i_di.di_type == GFS_FILE_BLK) ? 
	     "Block Device":
	     (eip->i_di.di_type == GFS_FILE_CHR) ? 
	     "Character Device":
	     (eip->i_di.di_type == GFS_FILE_FIFO) ? 
	     "FIFO":
	     (eip->i_di.di_type == GFS_FILE_SOCK) ? 
	     "Socket":
	     "Unknown type",
	     (de.de_type == GFS_FILE_REG) ? 
	     "File":
	     (de.de_type == GFS_FILE_DIR) ? 
	     "Directory":
	     (de.de_type == GFS_FILE_LNK) ? 
	     "Link":
	     (de.de_type == GFS_FILE_BLK) ? 
	     "Block Device":
	     (de.de_type == GFS_FILE_CHR) ? 
	     "Character Device":
	     (de.de_type == GFS_FILE_FIFO) ? 
	     "FIFO":
	     (de.de_type == GFS_FILE_SOCK) ? 
	     "Socket":
	     "Unknown type"
	     );
    } else { /* Not a valid type */
      pp_print(PPN, "Dir entry (%s, %"PRIu64") has unknown type.\n",
	       tmp_name, eip->i_di.di_num.no_addr);
      if(query("\tClear dir entry and unmark bitmap? (y/n) ")){
	if(fs_set_bitmap(sdp, eip->i_di.di_num.no_addr, GFS_BLKST_FREEMETA)){
	  pp_print(PPVH, "Unable to perform fs_set_bitmap().\n");
	  pp_print(PPVH, "Bad dir entry remains.\n");
	  goto next;
	}
	previous.de_rec_len += de.de_rec_len;
	gfs_dirent_out(&previous, (char *)previous_ptr);
	status->update=1;
	pp_print(PPN, "Bad dir entry cleared... awaiting sync.\n");
	goto next_no_step;
      } else {
	pp_print(PPH, "Bad dir entry remains.\n");
	goto next;
      }
    }

    if(de.de_type != eip->i_di.di_type){
      pp_print(PPN, "Type in dir entry (%s, %"PRIu64") conflicts with "
	       "type in dinode.\n"
	       "\ti.e.  Dir entry is stale.\n",
	       tmp_name, eip->i_di.di_num.no_addr);
      if(query("\tClear faulty dir entry? (y/n) ")){
	previous.de_rec_len += de.de_rec_len;
	gfs_dirent_out(&previous, (char *)previous_ptr);
	status->update=1;
	pp_print(PPN, "Dir entry cleared... awaiting sync.\n");
	goto next_no_step;
      } else {
	pp_print(PPH, "Bad dir entry remains.\n");
	goto next;
      }
    }

    if (eip->i_di.di_flags & GFS_DIF_UNUSED)
    {
      pp_print(PPN, "Dir entry (%s, %"PRIu64") exists for "
	       "a dinode marked as unused.\n",
	       tmp_name, eip->i_di.di_num.no_addr);
      if(query("\tClear dir entry and unmark bitmap? (y/n) ")){
	if(fs_set_bitmap(sdp, eip->i_di.di_num.no_addr, GFS_BLKST_FREEMETA)){
	  pp_print(PPVH, "Unable to perform fs_set_bitmap().\n");
	  pp_print(PPVH, "Bad dir entry remains.\n");
	  goto next;
	}
	previous.de_rec_len += de.de_rec_len;
	gfs_dirent_out(&previous, (char *)previous_ptr);
	status->update=1;
	pp_print(PPN, "Bad dir entry cleared... awaiting sync.\n");
	goto next_no_step;
      } else {
	pp_print(PPH, "Bad dir entry remains.\n");
	goto next;
      }
    }

    if (de.de_name_len == 1 && !memcmp(filename, ".", 1))
    {
      if (status->dot)
      {
	pp_print(PPN, "Duplicate . dir entries\n");
	goto remove;
      }      
      status->dot = de.de_inum.no_addr;
      if(status->dot != eip->i_di.di_num.no_addr){
	pp_print(PPN, "Bad . address.\n");
	goto remove;
      }
    }
    else if (de.de_name_len == 2 && !memcmp(filename, "..", 2))
    {
      if (status->dotdot)
      {
	pp_print(PPN, "Duplicate .. dir entries\n");
	goto remove;
      }      
      status->dotdot = de.de_inum.no_addr;
    }
    else
    {
      if (eip->i_di.di_type == GFS_FILE_DIR)
	status->subdirs++;
      
      record_dinode_dirent(sdp, de.de_inum.no_addr);
    }

    /* Check EA's */
    if(eip->i_di.di_eattr){
      /* ATTENTION -- should remove the EA if there is a marking failure */
      if(eip->i_di.di_flags & GFS_DIF_EA_INDIRECT){
	mark_indirect_eattr(eip, eip->i_di.di_eattr);
      } else {
	mark_leaf_eattr(eip, eip->i_di.di_eattr);
      }
    }
      

  next:
    count++;
  next_no_count:
    /* Let's call the process of updating the previous information **
    ** "Stepping".  If we do not wish to "step", goto next_no_step */
    previous_ptr = dent;
    previous_filename = filename;
    memcpy(&previous, &de, sizeof(struct gfs_dirent));

  next_no_step:
    /* if this is the last dirent */
    if ((char *)dent + de.de_rec_len >= bh_end){
      pp_print(PPVL, "Last entry processed.\n"); 
      break;
    }

    dent = (struct gfs_dirent *)((char *)dent + de.de_rec_len);
  }

  if ((char *)dent + de.de_rec_len != bh_end)
  {
    pp_print(PPN, "Dir entry with bad record length\n");
    goto remove;
  }

  if (type == DIR_EXHASH && count != gfs16_to_cpu(leaf->lf_entries))
  {
    pp_print(PPN, "Bad leaf (%"PRIu64") entry count. "
	 "found: %u, expected: %u (dir #%"PRIu64")\n", 
	 BH_BLKNO(bh), count, 
	 gfs16_to_cpu(leaf->lf_entries), ip->i_num.no_addr);
    if(leaf->lf_next){
      /* Attention - walk leaf chains */
      pp_print(PPVH, "Leaf chain detected.  Skipping.\n");
    } else {
      leaf->lf_entries = cpu_to_gfs16(count);
      if(fs_write_buf(sdp, bh, 0)){
	pp_print(PPVH, "Leaf entry count correction failed.\n");
	goto remove;
      } else {
	pp_print(PPN, "\tEntry count corrected for leaf (%"PRIu64").\n",
		 BH_BLKNO(bh));
      }
      rerun = 1;
    }
  }
  status->entries += count;

  return 0;

 remove:
  pp_print(PPL, "Removal recommended for dir #%"PRIu64"\n",
	   ip->i_num.no_addr);
  status->remove = TRUE;
  return 0;
}




/**
 * check_leaf
 * @ip:
 * @rgd:
 * @leaf_no: address of leaf block
 *
 * Check the validity of the range of a leaf block as well as
 * meta header information.  This function will check chained
 * leaf blocks, but it will only return the head of the chain.
 *
 * Returns: NULL on failure, head of leaf chain otherwise
 */

static osi_buf_t *check_leaf(fs_inode_t *ip, fs_rgrpd_t *rgd,
			     uint64 leaf_no){
  fs_sbd_t *sdp = ip->i_sbd;
  fs_rgrpd_t *leaf_rgd = fs_blk2rgrpd(sdp, leaf_no);
  uint64 chain_no;
  osi_buf_t *chain_head = NULL;
  osi_buf_t *bh = NULL;
  struct gfs_leaf leaf;
  int error, val;
  int chain=0;

  chain_no = leaf_no;
  do{
    error = check_range(sdp, chain_no);
    if (error){
      pp_print(PPVH, "Bad range for %s block #%"PRIu64"\n",
	       (chain)? "chained leaf" : "leaf", chain_no);
      return NULL;
    }

    error = fs_get_and_read_buf(sdp, chain_no, &bh, 0);

    if(error){
      goto fail;
    }

    gfs_leaf_in(&leaf, BH_DATA(bh));

    if (leaf.lf_header.mh_magic != GFS_MAGIC){
      pp_print(PPVH, "Bad %s block magic number 0x%x\n",
	       (chain)? "chained leaf" : "leaf", leaf.lf_header.mh_magic);
      goto fail;
    }

    if (leaf.lf_header.mh_type != GFS_METATYPE_LF){
      pp_print(PPVH, "Bad %s block type for block %"PRIu64".\n"
	       "\tFound %u, Expected %u\n",
	       (chain)? "chained leaf" : "leaf",
	       chain_no, leaf.lf_header.mh_type, GFS_METATYPE_LF);
      goto fail;
    }

    if(!chain){
      chain = 1;
      chain_head = bh;
      chain_no = leaf.lf_next;
    } else {
      fs_relse_buf(sdp, bh);
      bh = NULL;
    }
  } while(chain_no);

  return chain_head;

 fail:
  if(chain_head){
    if(chain_head == bh){ bh = NULL; }
    fs_relse_buf(sdp, chain_head);
  }
  if(bh)
    fs_relse_buf(sdp, bh);

  if(leaf_rgd != rgd){
    pp_print(PPVH, "The leaf block and dinode are in different rgrps.\n");
    if(fs_rgrp_read(leaf_rgd)){
      pp_print(PPVH, "Unable to read resource group.  "
	       "Can not clear leaf block.\n");
      return NULL;
    }
  }
  val = fs_get_bitmap(sdp, leaf_no, NULL);
  if(query("Clear bitmap for faulty leaf block? (y/n) ")){
    if(val == GFS_BLKST_USEDMETA){
      if(fs_set_bitmap(sdp, leaf_no, GFS_BLKST_FREE)){
	pp_print(PPVH, "Unable to clear FS bitmap for bad leaf block "
		 "#%"PRIu64"\n",
		 leaf_no);
      } else {
	pp_print(PPN, "FS bitmap cleared for leaf block #%"PRIu64"\n",
		 leaf_no);
      }
    } else if(val > 0){
      if(fs_set_bitmap(sdp, leaf_no, GFS_BLKST_FREE)){
	pp_print(PPVH, "Unable to clear FS bitmap for bad leaf block "
		 "#%"PRIu64"\n",
		 leaf_no);
      } else {
	pp_print(PPN, "FS bitmap cleared for leaf block #%"PRIu64"\n",
		 leaf_no);
      }
    } else {
      pp_print(PPN, "FS bitmap for leaf block #%"PRIu64" unchanged (%d)\n",
	       leaf_no, val);
    }
  }
  if(leaf_rgd != rgd){
    fs_rgrp_relse(leaf_rgd);
  }

  return NULL;
}


/**
 * dir_exhash_scan
 * @ip:
 *
 */
static int dir_exhash_scan(fs_inode_t *ip, dir_status_t *status)
{
  fs_sbd_t *sdp = ip->i_sbd;
  fs_rgrpd_t *rgd = fs_blk2rgrpd(sdp, ip->i_num.no_addr);
  osi_buf_t *bh = NULL;
  struct gfs_leaf leaf;
  uint64 leaf_no, old_leaf;
  uint32 ref_count, exp_count; /* reference/expected counts */
  int index, error, val;

  leaf_no = old_leaf = ref_count = exp_count = 0;
  for(index = 0; index < (1 << ip->i_di.di_depth); index++){
    if(get_leaf_nr(ip, index, &leaf_no)){
      pp_print(PPVH, "Unable to get leaf block number in dir #%"PRIu64".\n"
	       "\tDepth = %u\n"
	       "\tindex = %u\n",
	       ip->i_num.no_addr,
	       ip->i_di.di_depth,
	       index);
      return -1;
    }
    if(check_range(ip->i_sbd, leaf_no)){
      pp_print(PPN, "Leaf block #%"PRIu64" is out of range.\n",
	       leaf_no);
      return -1;
    }

    val = fs_get_bitmap(sdp, leaf_no, NULL);
    
    if (val != GFS_BLKST_USEDMETA){
      /* If data exists there, find another solution */
      if(val != GFS_BLKST_USED){
	pp_print(PPN, "Leaf block #%"PRIu64" has bad FS bitmap value.\n",
		 leaf_no);
	if(query("\tSet FS bitmap to correct value? (y/n) ")){
	  /* We can not change directly from FREE to USEDMETA */
	  if(val == GFS_BLKST_FREE){
	    if(fs_set_bitmap(sdp, leaf_no, GFS_BLKST_FREEMETA)){
	      pp_print(PPVH, "Unable to set bitmap to correct value.\n");
	      return -1;
	    }
	  }
	  if(fs_set_bitmap(sdp, leaf_no, GFS_BLKST_USEDMETA)){
	    pp_print(PPVH, "Unable to set bitmap to correct value.\n");
	    return -1;
	  } else {
	    pp_print(PPN, "Bitmap corrected.\n");
	  }
	} else {
	  pp_print(PPH, "Incorrect bitmap value remains.\n");
	}
      } else {
	pp_print(PPVH, "Unable to reconcile FS bitmap "
		 "for leaf block #%"PRIu64"\n"
		 "Found %d, Expected %d\n",
		 leaf_no,
		 val, GFS_BLKST_USEDMETA);
	return -1;
      }
    }

    bh = check_leaf(ip, rgd, leaf_no);

    if (!bh){
      return -1;
    }
    gfs_leaf_in(&leaf, BH_DATA(bh));

    if(old_leaf != leaf_no){
      if(ref_count != exp_count){
	pp_print(PPVH, "Dir #%"PRIu64" has an incorrect number "
		 "of pointers to leaf #%"PRIu64"\n"
		 "\tFound: %u,  Expected: %u\n",
		 ip->i_num.no_addr,
		 leaf_no,
		 ref_count,
		 exp_count);
	goto fail;
      }
      /* expected count */
      exp_count = (1 << (ip->i_di.di_depth - leaf.lf_depth));
      /* reference count - number of times we encounter the same leaf block */
      ref_count = 1;
      old_leaf = leaf_no;
    } else {
      ref_count++;
      fs_relse_buf(sdp, bh);
      continue;
    }

    error = check_entries(ip, bh, rgd, index, DIR_EXHASH, status);
      
    if (error || status->remove){
      pp_print(PPN, "Dir #%"PRIu64" has bad entries.\n", ip->i_num.no_addr);
      if(query("Remove Dir? (y/n) ")){
	if(fs_remove(ip)){
	  pp_print(PPVH, "Unable to remove directory.\n");
	  goto fail;
	} else {
	  pp_print(PPN, "Dir #%"PRIu64" removed.\n", ip->i_num.no_addr);
	  fs_relse_buf(sdp, bh);
	  return 0;
	}
      } else {
	pp_print(PPH, "Dir #%"PRIu64" not removed.\n", ip->i_num.no_addr);
      }
      status->remove = status->update = 0;
    }

    if(status->l_f){
      pp_print(PPN, "Dir #%"PRIu64" has been tagged for l+f inclusion.\n",
	       ip->i_num.no_addr);
      if(query("Add to lost and found? (y/n) ")){
	if(add_inode_to_lf(ip)){
	  pp_print(PPN, "Unable to add to lost and found.\n");
	  if(query("Remove Dir? (y/n) ")){
	    if(fs_remove(ip)){
	      pp_print(PPVH, "Unable to remove directory.\n");
	      goto fail;
	    } else {
	      pp_print(PPN, "Dir #%"PRIu64" removed.\n", ip->i_num.no_addr);
	      fs_relse_buf(sdp, bh);
	      return 0;
	    }
	  } else {
	    pp_print(PPH, "Dir #%"PRIu64" not removed.\n", ip->i_num.no_addr);
	  }
	  status->update = 0;
	} else {
	  pp_print(PPN, "Dir successfully added to lost and found.\n");
	}
      } else {
	if(query("Remove Dir? (y/n) ")){
	  if(fs_remove(ip)){
	    pp_print(PPVH, "Unable to remove directory.\n");
	    goto fail;
	  } else {
	    pp_print(PPN, "Dir #%"PRIu64" removed.\n", ip->i_num.no_addr);
	    fs_relse_buf(sdp, bh);
	    return 0;
	  }
	} else {
	  pp_print(PPH, "Dir #%"PRIu64" not removed.\n", ip->i_num.no_addr);
	}
	status->update = 0;
      }
    }

    if(status->update){
      status->update = 0;
      if(fs_write_buf(ip->i_sbd, bh, 0)){
	pp_print(PPVH, "Unable to perform write_buf().\n");
	pp_print(PPVH, "Sync incomplete.\n");
	goto fail;
      } else {
	rerun = 1;
	pp_print(PPN, "\tSync complete.\n");
      }
    }

    fs_relse_buf(sdp, bh);
  }

  return 0;


 fail:

  if (bh)
    fs_relse_buf(sdp, bh);

  return -1;
}



/**
 * dir_linear_scan
 * @ip:
 *
 */
static int dir_linear_scan(fs_inode_t *ip, dir_status_t *status)
{
  fs_sbd_t *sdp = ip->i_sbd;
  fs_rgrpd_t *rgd = fs_blk2rgrpd(sdp, ip->i_num.no_addr);
  osi_buf_t *bh;

  if(fs_get_and_read_buf(ip->i_sbd, ip->i_num.no_addr, &bh, 0)){
    return -1;
  }

  check_entries(ip, bh, rgd, 0, DIR_LINEAR, status);

  /* if the dir has been marked for removal, screw trying to update */
  if(!status->remove && status->update){
    status->update = 0;
    if(fs_write_buf(ip->i_sbd, bh, 0)){
      pp_print(PPVH, "Unable to perform fs_write_buf().\n");
      pp_print(PPVH, "Sync incomplete.\n");
      fs_relse_buf(sdp, bh);
      return -1;
    } else {
      rerun = 1;
      pp_print(PPN, "Sync complete.\n");
    }
  }

  fs_relse_buf(sdp, bh);

  return 0;
}


/*
 * dir_scan
 * @ip: directory inode
 * @status:
 *
 * Returns: 0 on success, -1 on failure
 */
static int dir_scan(fs_inode_t *ip, dir_status_t *status)
{
  if (ip->i_di.di_flags & GFS_DIF_EXHASH){
    return dir_exhash_scan(ip, status);
  } else {
    return dir_linear_scan(ip, status);
  }
}




/**
 * pass_3
 * @sdp:
 *
 * This function goes through each dinode that is a directory and scans its
 * contents.  It checks to ensure that it has "." and ".." entries.  If these
 * are reported as missing, "." can be fixed, but ".." is set to l+f.
 *
 * Also, the number of entries is checked, as well as the link count.
 *
 */
int pass_3(fs_sbd_t *sdp)
{
  fs_rgrpd_t *rgd;
  dir_status_t *status;
  osi_list_t *tmp;
  fs_inode_t *ip;
  uint64 block; 
  int changed = 0;
  int error, cnt, count, first, restart = 0;  
  int restart_count=0;
  int prev_prcnt = -1, prcnt = 0;

  rerun = 0;
  ip = (fs_inode_t *)gfsck_zalloc(sizeof(fs_inode_t));
  status = (dir_status_t *)gfsck_zalloc(sizeof(dir_status_t));

  ip->i_sbd = sdp;

 restart:
  if(restart_count++ > 50){
	die("Too many restarts\n");
  }
  count=0; cnt=0;

  /* we must always be sure this list is empty, otherwise, if we restart **
  ** we can end up with bad link counts................................. */
  while(!osi_list_empty(&sdp->sd_dirent_list)){
    di_info_t *di;

    di = osi_list_entry(sdp->sd_dirent_list.next, di_info_t, din_list);
    osi_list_del(&di->din_list);
    gfsck_free(di);
  }

  /* It is also important that we clear the gfsck bitmaps */
  free_bitmaps(sdp);
  allocate_bitmaps(sdp);

  for (tmp = sdp->sd_rglist.next; tmp != &sdp->sd_rglist; tmp = tmp->next){
    prcnt = (int)(100.0 * ((float)cnt / (float)sdp->sd_rgcount));
    if(prev_prcnt != prcnt){
      pp_print(PPL, "Pass 3:  %d %% \n", prcnt);
      prev_prcnt = prcnt;
    }
    cnt++;
    pp_print(PPD, "Pass 3:  Starting rgrp %d of %d\n", cnt, sdp->sd_rgcount);

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

      fs_copyin_dinode(ip);

      if (ip->i_di.di_flags & GFS_DIF_UNUSED){
	pp_print(PPVH, "Dinode #%"PRIu64" marked unused !  "
	    "Should be cleared in pass 2.\n"
	    "FS Bitmap has value = %d\n",
	    ip->i_num.no_addr,
	    fs_get_bitmap(sdp, ip->i_di.di_num.no_addr, NULL));
        goto fail;
      }
      if (ip->i_di.di_type != GFS_FILE_DIR)
	continue;

      count++;
      memset(status, 0, sizeof(dir_status_t));

      if(dir_scan(ip, status)){
	pp_print(PPVH, "dir_scan failure on dir #%"PRIu64".\n",
		 ip->i_num.no_addr);
	if(query("Remove directory? (y/n) ")){
	  if(fs_remove(ip)){
	    pp_print(PPVH, "Unable to remove directory inode #%"PRIu64".\n",
		     ip->i_num.no_addr);
	    goto fail;
	  } else {
	    pp_print(PPN, "Directory inode removed - will restart pass 3.\n");
	    restart = 1;
	    continue;
	  }
	} else {
	  pp_print(PPN, "Faulty Directory remains.\n");
	}
      }

      /* If there are uncorrectable errors in this directory */
      if (status->remove){
	pp_print(PPN, "Directory #%"PRIu64" has been marked for removal.\n",
		 ip->i_num.no_addr);
	if(query("Remove directory? (y/n) ")){
	  if(fs_remove(ip)){
	    pp_print(PPVH, "Unable to remove directory inode #%"PRIu64".\n",
		     ip->i_num.no_addr);
	    goto fail;
	  } else {
	    pp_print(PPN, "Directory inode removed - will restart pass 3.\n");
	    restart = 1;
	    continue;
	  }
	} else {
	  pp_print(PPN, "Faulty Directory remains.\n");
	}
      }

      if (!status->dot){
	pp_print(PPN, "Directory #%"PRIu64" has no \".\" entry\n",
		 ip->i_num.no_addr);
	if(fs_dir_add(ip, &(osi_filename_t){".", 1},
		      &ip->i_num, ip->i_di.di_type)){
	  pp_print(PPN, "Directory #%"PRIu64" has no \".\" entry "
		   "- marked for removal.\n",
		   ip->i_num.no_addr);
	  if(query("Remove directory? (y/n) ")){
	    if(fs_remove(ip)){
	      pp_print(PPVH, "Unable to remove directory inode #%"PRIu64".\n",
		       ip->i_num.no_addr);
	      goto fail;
	    } else {
	      pp_print(PPN, "Dir inode removed - will restart pass 3.\n");
	      restart = 1;
	      continue;
	    }
	  } else {
	    pp_print(PPN, "Faulty Directory remains.\n");
	  }
	} else {
	  pp_print(PPN, "\".\" entry added successfully.\n");
	}
      }

      if(!status->dotdot){
	if(sdp->sd_rooti->i_num.no_addr == ip->i_num.no_addr){
	  pp_print(PPN, "Root directory (%"PRIu64") has no \"..\" entry\n",
		   ip->i_num.no_addr);
	  if(fs_dir_add(ip, &(osi_filename_t){"..", 2},
			&ip->i_num, ip->i_di.di_type)){
	    pp_print(PPVH, "Unable to add \"..\" entry to root directory.  "
		     "This is fatal.\n");
	    goto fail;
	  }
	} else if(sdp->sd_lf_dip){
	  pp_print(PPN, "Directory #%"PRIu64" has no \"..\" entry\n",
		   ip->i_num.no_addr);
	  if(query("\tRelink to lost and found? (y/n) ")){
	    if(fs_dir_add(ip, &(osi_filename_t){"..", 2},
			  &(sdp->sd_lf_dip->i_num), GFS_FILE_DIR)){
	      pp_print(PPN, "Unable to add directory to lost and found.\n");
	      if(query("\tRemove directory? (y/n) ")){
		if(fs_remove(ip)){
		  pp_print(PPVH, "Unable to remove directory inode.\n");
		  goto fail;
		} else {
		  pp_print(PPN, "Dir inode removed - will restart pass 3.\n");
		  restart = 1;
		  continue;
		}
	      } else {
		pp_print(PPN, "Faulty Directory remains.\n");
	      }
	    } else {
	      pp_print(PPN, 
		       "Directory successfully added to lost and found.\n");
	    }
	  } else {
	    if(query("\tRemove directory? (y/n) ")){
	      if(fs_remove(ip)){
		pp_print(PPVH, "Unable to remove directory inode.\n");
		goto fail;
	      } else {
		pp_print(PPN, "Dir inode removed - will restart pass 3.\n");
		restart = 1;
		continue;
	      }
	    } else {
	      pp_print(PPN, "Faulty Directory remains.\n");
	    }
	  }
	} else {
	  pp_print(PPN, "Directory #%"PRIu64" has no \"..\" entry "
		   "- marked for removal.\n",
		   ip->i_num.no_addr);
	  if(query("Remove directory? (y/n) ")){
	    if(fs_remove(ip)){
	      pp_print(PPVH, "Unable to remove directory inode.\n");
	      goto fail;
	    } else {
	      pp_print(PPN, "Dir inode removed - will restart pass 3.\n");
	      restart = 1;
	      continue;
	    }
	  } else {
	    pp_print(PPN, "Faulty Directory remains.\n");
	  }
	}
      }	

      /* correctable error */
      if (status->entries != ip->i_di.di_entries){
	pp_print(PPN, "Incorrect number of dir entries found\n");
	if(query("\tCorrect dir entry count? (y/n) ")){
	  ip->i_di.di_entries = status->entries;
	  if(fs_copyout_dinode(ip)){
	    pp_print(PPVH, "Unable to perform fs_copyout_dinode().\n");
	    pp_print(PPVH, "Incorrect count remains.\n");
	  } else {
	    pp_print(PPN, "Dir entry count updated.\n");
	  }
	}
      }


      /* correctable error */
      if (status->subdirs + 2 != ip->i_di.di_nlink){
	pp_print(PPN, "Bad link count on directory #%"PRIu64"\n",
		 ip->i_num.no_addr);
	if(query("\tCorrect link count (%u -> %"PRIu64")? (y/n) ",
		 ip->i_di.di_nlink, status->subdirs + 2)){
	  uint32 old_nlink = ip->i_di.di_nlink;
	  ip->i_di.di_nlink = status->subdirs+2;
	  if(fs_copyout_dinode(ip)){
	    pp_print(PPVH, "Unable to perform fs_copyout_dinode().\n");
	    pp_print(PPVH, "Incorrect count remains.\n");
	  } else {
	    pp_print(PPN, "Link count updated (%u -> %u).\n",
		     old_nlink, ip->i_di.di_nlink);
	  }
	}
      }
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

  if(restart){
    restart=0;
    changed=1;
    goto restart;
  }

  gfsck_free(status);
  gfsck_free(ip);

  pp_print(PPN, "Pass 3:  %d directories checked\n", count);

  return changed;
  
 fail:
  gfsck_free(status);
  gfsck_free(ip);
  return -1;
}
