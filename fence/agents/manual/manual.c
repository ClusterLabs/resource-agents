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
#include <sys/stat.h>
#include <syslog.h>

#include "copyright.cf"

/* FIFO_DIR needs to agree with the same in ack.c */

#define LOCK_DIR "/var/lock"
#define FIFO_DIR "/tmp"

char *pname = NULL;

char args[256];
char name[256];
char ipaddr[256];
char fname[256];
char path[256];
char lockdir[256];
char fifodir[256];

int quiet_flag = 0;

void print_usage(void)
{
  printf("Usage:\n");
  printf("\n");
  printf("%s [options]\n"
         "\n"
         "Options:\n"
	 "  -h               usage\n"
	 "  -q               quiet\n"
	 "  -s <ip>          IP address of machine to fence\n"
	 "  -V               Version\n",pname);
}


void get_options(int argc, char **argv)
{
  int c, rv;
  char *curr;
  char *rest;
  char *value;

  if (argc > 1)
  {        
    while ((c = getopt(argc, argv, "hs:p:qV")) != -1)
    {
      switch(c)
      {
      case 'h':
        print_usage();
        exit(0);
	
      case 's':
        strcpy(ipaddr, optarg);
        break;

      case 'q':
        quiet_flag = 1;
        break;

      case 'p':
        if (strlen(optarg) > 200)
        {
          fprintf(stderr, "path name too long\n");
	  exit(1);
        }
        strncpy(path, optarg, 200);
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
  }
  
  else
  {
    if ((rv = read(0, args, 255)) < 0)
    {
      if(!quiet_flag)
        printf("failed: no input\n");
      exit(1);
    }
    curr = args;
    while ((rest = strchr(curr, '\n')) != NULL){
      *rest = 0;
      rest++;
      if ((value = strchr(curr, '=')) == NULL){
	printf("failed: invalid input\n");
	exit(1);
      }
      *value = 0;
      value++;
      if (!strcmp(curr, "agent")){
	strcpy(name, value);
        pname = name;
      }
      if (!strcmp(curr, "ipaddr"))
	strcpy(ipaddr, value);
      curr = rest;
    }
  }

  if (!strlen(path))
    strcpy(lockdir, LOCK_DIR);
  else
    strcpy(lockdir, path);

  strcpy(fifodir, FIFO_DIR);
}



int main(int argc, char **argv)
{
  int error, fd, lfd;
  char line[256], *mw, *ok;
  struct flock lock;


  openlog("fence_manual", 0, LOG_DAEMON);

  memset(args, 0, 256);
  memset(name, 0, 256);
  memset(ipaddr, 0, 256);
  memset(fname, 0, 256);
  memset(path, 0, 256);
  memset(lockdir, 0, 256);
  memset(fifodir, 0, 256);

  pname = argv[0];

  get_options(argc, argv);

  if (ipaddr[0] == 0)
  {
    if(!quiet_flag)
      printf("failed: %s no IP addr\n", name);
    exit(1);
  }


  /* Serialize multiple fence_manual's by locking a lock file */

  memset(fname, 0, 256);
  sprintf(fname, "%s/fence_manual.lock", lockdir);
  lfd = open(fname, O_WRONLY | O_CREAT,
             (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH));
  if (lfd < 0)
  {
    if (!quiet_flag)
      printf("failed: %s %s lockfile open error\n", pname, ipaddr);
    exit(1);
  }

  lock.l_type = F_WRLCK;
  lock.l_start = 0;
  lock.l_whence = SEEK_SET;
  lock.l_len = 0;

  error = fcntl(lfd, F_SETLKW, &lock);
  if (error < 0)
  {
    if (!quiet_flag)
      printf("failed: fcntl errno %d\n", errno);
    exit(1);
  }


  /* It's our turn to use the fifo */

  syslog(LOG_CRIT, "Node %s requires hard reset.  Run \"fence_ack_manual -s %s\""
                   " after power cycling the machine.\n", 
                   ipaddr, ipaddr);

  umask(0);
  memset(fname, 0, 256);
  sprintf(fname, "%s/fence_manual.fifo", fifodir);
  error = mkfifo(fname, (S_IRUSR | S_IWUSR));
  if (error && errno != EEXIST)
  {
    if (!quiet_flag)
      printf("failed: %s mkfifo error\n", pname);
    exit(1);
  }

  fd = open(fname, O_RDONLY);
  if (fd < 0)
  {
    if (!quiet_flag)
      printf("failed: %s %s open error\n", pname, ipaddr);
    exit(1);
  }


  /* Get result from ack_manual */

  memset(line, 0, 256);
  error = read(fd, line, 256);
  if (error < 0)
  {
    if(!quiet_flag)
      printf("failed: %s %s read error\n", pname, ipaddr);
    exit(1);
  }


  mw = strstr(line, "meatware");
  ok = strstr(line, "ok");

  if (!mw || !ok)
  {
    if(!quiet_flag)
      printf("failed: %s %s improper message\n", pname, ipaddr);
    exit(1);
  }

  if(!quiet_flag)
    printf("success: %s %s\n", pname, ipaddr);
  unlink(fname);
  return 0;
}


