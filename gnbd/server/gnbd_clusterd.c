/******************************************************************************
*******************************************************************************
**
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
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <magma.h>

#include "gnbd_utils.h"

#define INTERFACE_GROUP "cluster::usrm"

static int fd = -1;
static int quit = 0;

static void sig_term(int sig)
{
  quit = 1;
}

void kill_gnbd_clusterd(void){
  int pid = 0;
  
  if (check_lock("gnbd_clusterd.pid", &pid) == 0)
    return;
  
  kill(pid, SIGTERM);
}


int main(int argc, char **argv){
  struct sigaction act;

  program_name = "gnbd_clusterd";

  if (argc > 2 || (argc == 2 && strcmp(argv[1], "-k"))){
    printf("Usage: gnbd_clusted [-k]\n");
    exit(1);
  }
  
  if (argc == 2){
    kill_gnbd_clusterd();
    exit(0);
  }

  if (check_lock("gnbd_clusterd.pid", NULL) == 1)
    exit(0);
  
  daemonize_and_exit_parent();
  
  memset(&act, 0, sizeof(act));
  act.sa_handler = sig_term;
  if (sigaction(SIGTERM, &act, NULL) < 0)
    fail_startup("cannot set a handler for SIGTERM : %s\n", strerror(errno));

  if (!pid_lock("")){
    finish_startup("gnbd_clusterd already running\n");
    exit(0);
  }
  fd = clu_connect("gnbd", 1);

  if (fd < 0)
    fail_startup("cannot connect to cluster manager : %s\n", strerror(-fd));
  finish_startup("connected\n");

  while(!quit){
    pause();
  }
  clu_disconnect(fd);
  
  return 0;
} 
