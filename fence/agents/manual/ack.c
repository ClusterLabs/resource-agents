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
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "copyright.cf"

#define FIFO_DIR "/tmp"

char *pname = NULL;
char fname[256];
int quiet_flag = 0;
int override_flag = 0;


void print_usage(void)
{
  printf("Usage:\n");
  printf("\n");
  printf("%s [options]\n"
         "\n"
         "Options:\n"
         "  -h               usage\n"
	 "  -O               override\n"
	 "  -n <nodename>    Name of node that was manually fenced\n"
	 "  -s <ip>          IP address of machine that was manually fenced (deprecated)\n"
         "  -V               Version information\n", pname);
}

int main(int argc, char **argv)
{
  int error, fd;
  char line[256];
  char *ipaddr = NULL;
  char response[3];
  int c;

  memset(line, 0, 256);

  pname = argv[0];
    
  while ((c = getopt(argc, argv, "hOs:n:qV")) != -1)
  {
    switch(c)
    {
    case 'h':
      print_usage();
      exit(0);
	
    case 'O':
      override_flag = 1;
      break;

    case 's':
      ipaddr = optarg;
      break;

    case 'n':
      ipaddr = optarg;
      break;

    case 'q':
      quiet_flag = 1;
      break;

    case 'V':
      printf("%s %s (built %s %s)\n", pname, FENCE_RELEASE_NAME,
                __DATE__, __TIME__);
      printf("%s\n", REDHAT_COPYRIGHT);
      exit(0);
      break;

    case ':':
    case '?':
      fprintf(stderr, "Please use '-h' for usage.\n");
      exit(1);
      break;

    default:
      fprintf(stderr, "Bad programmer! You forgot to catch the %c flag\n", c);
      exit(1);
      break;

    }
  }

  if (!ipaddr)
  {
    if(!quiet_flag)
      print_usage();
    exit(1);
  }

  if(!override_flag)
  {
    printf("\nWarning:  If the node \"%s\" has not been manually fenced\n"
	   "(i.e. power cycled or disconnected from shared storage devices)\n"
	   "the GFS file system may become corrupted and all its data\n"
	   "unrecoverable!  Please verify that the node shown above has\n"
	   "been reset or disconnected from storage.\n", ipaddr);

    printf("\nAre you certain you want to continue? [yN] ");
    scanf("%s", response);

    if (tolower(response[0] != 'y'))
    {
      printf("%s operation canceled.\n", pname);
      exit(1);
    }
  }

  memset(fname, 0, 256);
  sprintf(fname, "%s/fence_manual.fifo", FIFO_DIR);

  fd = open(fname, O_WRONLY | O_NONBLOCK);
  if (fd < 0)
  {
    perror("can't open /tmp/fence_manual.fifo");
    exit(1);
  }

  sprintf(line, "meatware ok\n");

  error = write(fd, line, 256);
  if (error < 0)
  {
    perror("can't write to /tmp/fence_manual.fifo");
    exit(1);
  }
    
  if(!quiet_flag)
    printf("done\n");

  return(0);
}


