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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>

#include "global.h"
#include <linux/gfs_ioctl.h>

#include "gfs_tool.h"





#ifdef DEBUG_STACK

/**
 * print_stack -
 *
 */

void print_stack(int argc, char **argv)
{
  int fd;

  if (argc != 3)
    die("Usage: gfs_tool stack <mountpoint>\n");

  fd = open(argv[2], O_RDONLY);
  if (fd < 0)
    die("can't open %s:  %s\n", argv[2], strerror(errno));

  check_for_gfs(fd, argv[2]);

  if (ioctl(fd, GFS_STACK_PRINT, NULL) < 0)
    die("error doing ioctl:  %s\n", strerror(errno));

  close(fd);
}

#endif  /*  DEBUG_STACK  */


