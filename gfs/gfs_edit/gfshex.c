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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "global.h"
#include <linux/gfs_ondisk.h>
#include "linux_endian.h"

#include "hexedit.h"
#include "gfshex.h"





/******************************************************************************
*******************************************************************************
**
** do_dinode_extended()
**
** Description:
**
** Input(s):
**
** Output(s):
**
** Returns:
**
*******************************************************************************
******************************************************************************/

void do_dinode_extended(struct gfs_dinode *di, char *buf)
{
  unsigned int x, y, count;
  struct gfs_dirent de;
  uint64 p, last;


  if (di->di_height > 0)
  {
    printf("\nIndirect Pointers\n\n");

    for (x = sizeof(struct gfs_dinode), y = 0;
	 x < bsize;
	 x += sizeof(uint64), y++)
    {
      p = gfs64_to_cpu(*(uint64 *)(buf + x));

      if (p)
	printf("  %u -> %"PRIu64"\n", y, p);
    }
  }


  else if (di->di_type == GFS_FILE_DIR &&
	   !(di->di_flags & GFS_DIF_EXHASH))
  {
    printf("\nDirectory Entries:\n");

    for (x = sizeof(struct gfs_dinode); x < bsize; x += de.de_rec_len)
    {
      printf("\n");
      gfs_dirent_in(&de, buf + x);
      if (de.de_inum.no_formal_ino)
	gfs_dirent_print(&de, buf + x + sizeof(struct gfs_dirent));
    }
  }


  else if (di->di_type == GFS_FILE_DIR &&
	   (di->di_flags & GFS_DIF_EXHASH) &&
	   di->di_height == 0)
  {
    printf("\nLeaf Pointers:\n\n");

    last = gfs64_to_cpu(*(uint64 *)(buf + sizeof(struct gfs_dinode)));
    count = 0;
    
    for (x = sizeof(struct gfs_dinode), y = 0;
	 y < (1 << di->di_depth);
	 x += sizeof(uint64), y++)
    {
      p = gfs64_to_cpu(*(uint64 *)(buf + x));

      if (p != last)
      {
	printf("  %u:  %"PRIu64"\n", count, last);
	last = p;
	count = 1;
      }
      else
	count++;

      if ((y + 1) * sizeof(uint64) == di->di_size)
	printf("  %u:  %"PRIu64"\n", count, last);
    }
  }
}


/******************************************************************************
*******************************************************************************
**
** do_indirect_extended()
**
** Description:
**
** Input(s):
**
** Output(s):
**
** Returns:
**
*******************************************************************************
******************************************************************************/

void do_indirect_extended(char *buf)
{
  unsigned int x, y;
  uint64 p;

  printf("\nPointers\n\n");

  for (x = sizeof(struct gfs_indirect), y = 0; x < bsize; x += 8, y++)
  {
    p = gfs64_to_cpu(*(uint64 *)(buf + x));

    if (p)
      printf("  %u -> %"PRIu64"\n", y, p);
  }
}


/******************************************************************************
*******************************************************************************
**
** do_leaf_extended()
**
** Description:
**
** Input(s):
**
** Output(s):
**
** Returns:
**
*******************************************************************************
******************************************************************************/

void do_leaf_extended(char *buf)
{
  struct gfs_dirent de;
  unsigned int x;


  printf("\nDirectory Entries:\n");

  for (x = sizeof(struct gfs_leaf); x < bsize; x += de.de_rec_len)
  {
    printf("\n");
    gfs_dirent_in(&de, buf + x);
    if (de.de_inum.no_formal_ino)
	gfs_dirent_print(&de, buf + x + sizeof(struct gfs_dirent));
  }
}


/******************************************************************************
*******************************************************************************
**
** do_eattr_extended()
**
** Description:
**
** Input(s):
**
** Output(s):
**
** Returns:
**
*******************************************************************************
******************************************************************************/

void do_eattr_extended(char *buf)
{
  struct gfs_ea_header ea;
  unsigned int x;


  printf("\nEattr Entries:\n");

  for (x = sizeof(struct gfs_meta_header); x < bsize; x += ea.ea_rec_len)
  {
    printf("\n");
    gfs_ea_header_in(&ea, buf + x);
    gfs_ea_header_print(&ea, buf + x + sizeof(struct gfs_ea_header));
  }
}


/******************************************************************************
*******************************************************************************
**
** int display_gfs()
**
** Description:
**   This routine...
**
** Input(s):
**  *buffer   - 
**   extended - 
**
** Returns:
**   0 if OK, 1 on error.
**
*******************************************************************************
******************************************************************************/

int display_gfs(int extended)
{
  struct gfs_meta_header mh;
  struct gfs_sb sb;
  struct gfs_rgrp rg;
  struct gfs_dinode di;
  struct gfs_leaf lf;
  struct gfs_log_header lh;
  struct gfs_log_descriptor ld;

  char *buf;
  uint32 magic;


  type_alloc(buf, char, bsize);

  do_lseek(fd, block * bsize);
  do_read(fd, buf, bsize);

  
  magic = gfs32_to_cpu(*(uint32 *)buf);


  switch (magic)
  {
  case GFS_MAGIC:
    gfs_meta_header_in(&mh, buf);

    switch (mh.mh_type)
    {
    case GFS_METATYPE_SB:
      printf("Superblock:\n\n");
      gfs_sb_in(&sb, buf);
      gfs_sb_print(&sb);

      if (extended)
	printf("\nNo Extended data\n");

      break;


    case GFS_METATYPE_RG:
      printf("Resource Group Header:\n\n");
      gfs_rgrp_in(&rg, buf);
      gfs_rgrp_print(&rg);

      if (extended)
	printf("\nNo Extended data\n");

      break;


    case GFS_METATYPE_RB:
      printf("Resource Group Bitmap:\n\n");
      gfs_meta_header_print(&mh);

      if (extended)
	printf("\nNo Extended data\n");

      break;


    case GFS_METATYPE_DI:
      printf("Dinode:\n\n");
      gfs_dinode_in(&di, buf);
      gfs_dinode_print(&di);

      if (extended)
	do_dinode_extended(&di, buf);

      break;


    case GFS_METATYPE_LF:
      printf("Leaf:\n\n");
      gfs_leaf_in(&lf, buf);
      gfs_leaf_print(&lf);

      if (extended)
	do_leaf_extended(buf);

      break;


    case GFS_METATYPE_IN:
      printf("Indirect Block:\n\n");
      gfs_meta_header_print(&mh);

      if (extended)
	do_indirect_extended(buf);

      break;


    case GFS_METATYPE_JD:
      printf("Journaled File Block:\n\n");
      gfs_meta_header_print(&mh);

      if (extended)
	printf("\nNo Extended data\n");

      break;


    case GFS_METATYPE_LH:
      printf("Log Header:\n\n");
      gfs_log_header_in(&lh, buf);
      gfs_log_header_print(&lh);

      if (extended)
	printf("\nNo Extended data\n");

      break;


    case GFS_METATYPE_LD:
      printf("Log Descriptor:\n\n");
      gfs_desc_in(&ld, buf);
      gfs_desc_print(&ld);

      if (extended)
	printf("\nNo Extended data\n");

      break;


    case GFS_METATYPE_EA:
      printf("Eattr Block:\n\n");
      gfs_meta_header_print(&mh);

      if (extended)
	do_eattr_extended(buf);

      break;


    case GFS_METATYPE_ED:
      printf("Eattr Data Block:\n\n");
      gfs_meta_header_print(&mh);

      if (extended)
	printf("\nNo Extended data\n");

      break;


    default:
      printf("Unknown metadata type\n");
      break;
    }

    break;


  default:
    printf("Unknown block type\n");
    break;
  };


  free(buf);


  return(0);
}


/******************************************************************************
*******************************************************************************
**
** int edit_gfs()
**
** Description:
**   This routine...
**
** Input(s):
**  *buffer   - 
**   extended - 
**
** Returns:
**   0 if OK, 1 on error.
**
*******************************************************************************
******************************************************************************/

int edit_gfs(char *arg1, char *arg2, char *arg3)
{
  char buf[512];
  unsigned int row, col, byte;
  uint64 dev_offset;
  unsigned int offset;


  if (!strncmp(arg3, "", 1))
  {
    fprintf(stderr, "%s:  invalid number of arguments\n", prog_name);
    return(-EINVAL);
  }


  row = atoi(arg1);
  col = atoi(arg2);
  sscanf(arg3, "%x", &byte);


  if (row >= SCREEN_HEIGHT)
  {
    fprintf(stderr, "%s:  row is out of range for set\n", prog_name);
    return(-EINVAL);
  }
  
  if (col >= SCREEN_WIDTH)
  {
    fprintf(stderr, "%s:  column is out of range for set\n", prog_name);
    return(-EINVAL);
  }

  if (byte > 255)
  {
    fprintf(stderr, "%s:  byte value is out of range for set\n", prog_name);
    return(-EINVAL);
  }



  /*  Make sure all I/O is 512-byte aligned  */

  dev_offset = (block * bsize + start * SCREEN_WIDTH + row * SCREEN_WIDTH + col) >> 9 << 9;
  offset = (block * bsize + start * SCREEN_WIDTH + row * SCREEN_WIDTH + col) - dev_offset;


  do_lseek(fd, dev_offset);
  do_read(fd, buf, 512);

  buf[offset] = (unsigned char)byte;
  
  do_lseek(fd, dev_offset);
  do_write(fd, buf, 512);


  fsync(fd);

  return(0);
}






