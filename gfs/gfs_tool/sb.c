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
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "global.h"
#include <linux/gfs_ondisk.h>

#include "gfs_tool.h"



#define do_lseek(fd, off) \
{ \
  if (lseek((fd), (off), SEEK_SET) != (off)) \
    die("bad seek: %s on line %d of file %s\n", \
	strerror(errno),__LINE__, __FILE__); \
}

#define do_read(fd, buff, len) \
{ \
  if (read((fd), (buff), (len)) != (len)) \
    die("bad read: %s on line %d of file %s\n", \
	strerror(errno), __LINE__, __FILE__); \
}

#define do_write(fd, buff, len) \
{ \
  if (write((fd), (buff), (len)) != (len)) \
    die("bad write: %s on line %d of file %s\n", \
	strerror(errno), __LINE__, __FILE__); \
}





/**
 * do_sb - examine/modify a unmounted FS' superblock
 * @argc:
 * @argv:
 *
 */

void do_sb(int argc, char **argv)
{
  int fd;
  unsigned char buf[GFS_BASIC_BLOCK], input[256];
  struct gfs_sb sb;
  int rewrite = FALSE;


  if (argc != 4 && argc != 5)
    die("bad number of arguments\n");

  if (argc == 5)
    rewrite = TRUE;


  fd = open(argv[2], (rewrite) ? O_RDWR : O_RDONLY);
  if (fd < 0)
    die("can't open %s:  %s\n", argv[2], strerror(errno));


  if (rewrite)
  {
    printf("You shouldn't change any of these values if the filesystem is mounted.\n");
    printf("\nAre you sure? [y/n] ");
    fgets(input, 255, stdin);

    if (input[0] != 'y')
      die("aborted\n");

    printf("\n");
  }


  do_lseek(fd, GFS_SB_ADDR * GFS_BASIC_BLOCK);
  do_read(fd, buf, GFS_BASIC_BLOCK);

  gfs_sb_in(&sb, buf);


  if (sb.sb_header.mh_magic != GFS_MAGIC ||
      sb.sb_header.mh_type != GFS_METATYPE_SB)
    die("there isn't a GFS filesystem on %s\n", argv[2]);


  if (strcmp(argv[3], "proto") == 0)
  {
    printf("current lock protocol name = \"%s\"\n", sb.sb_lockproto);

    if (rewrite)
    {
      if (strlen(argv[4]) >= GFS_LOCKNAME_LEN)
	die("new lockproto name is too long\n");
      strcpy(sb.sb_lockproto, argv[4]);
      printf("new lock protocol name = \"%s\"\n", sb.sb_lockproto);
    }
  }
  else if (strcmp(argv[3], "table") == 0)
  {
    printf("current lock table name = \"%s\"\n", sb.sb_locktable);

    if (rewrite)
    {
      if (strlen(argv[4]) >= GFS_LOCKNAME_LEN)
	die("new locktable name is too long\n");
      strcpy(sb.sb_locktable, argv[4]);
      printf("new lock table name = \"%s\"\n", sb.sb_locktable);
    }
  }
  else if (strcmp(argv[3], "ondisk") == 0)
  {
    printf("current ondisk format = %u\n", sb.sb_fs_format);

    if (rewrite)
    {
      sb.sb_fs_format = atoi(argv[4]);
      printf("new ondisk format = %u\n", sb.sb_fs_format);
    }
  }
  else if (strcmp(argv[3], "multihost") == 0)
  {
    printf("current multihost format = %u\n", sb.sb_multihost_format);

    if (rewrite)
    {
      sb.sb_multihost_format = atoi(argv[4]);
      printf("new multihost format = %u\n", sb.sb_multihost_format);
    }
  }
  else if (strcmp(argv[3], "all") == 0)
  {
    gfs_sb_print(&sb);
    rewrite = FALSE;
  }
  else
    die("unknown field %s\n", argv[3]);


  if (rewrite)
  {
    gfs_sb_out(&sb, buf);

    do_lseek(fd, GFS_SB_ADDR * GFS_BASIC_BLOCK);
    do_write(fd, buf, GFS_BASIC_BLOCK);

    fsync(fd);

    printf("Done\n");
  }


  close(fd);
}



