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

#include "iddev.h"



#define die(fmt, args...) \
do \
{ \
  fprintf(stderr, "%s: ", prog_name); \
  fprintf(stderr, fmt, ##args); \
  exit(EXIT_FAILURE); \
} \
while (0)



#define BUFSIZE (1024)



char *prog_name;





int main(int argc, char *argv[])
{
  int fd;
  char buf[BUFSIZE];
  uint64 bytes;
  int error;

  prog_name = argv[0];

  if (argc != 2)
    die("Usage: %s devicename\n", prog_name);

  fd = open(argv[1], O_RDONLY);
  if (fd < 0)
    die("can't open %s: %s\n", argv[1], strerror(errno));

  error = identify_device(fd, buf, BUFSIZE);
  if (error < 0)
    die("error identifying the contents of %s: %s\n", argv[1], strerror(errno));
  else if (error)
    strcpy(buf, "unknown");

  error = device_size(fd, &bytes);
  if (error < 0)
    die("error determining the size of %s: %s\n", argv[1], strerror(errno));

  printf("%s:\n%-15s%s\n%-15s%"PRIu64"\n",
	 argv[1], "  contents:", buf, "  bytes:", bytes);

  close(fd);

  exit(EXIT_SUCCESS);
}

