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

static void sig_term(int sig)
{
  if (fd >= 0)
    clu_disconnect(fd);
  exit(0);
}

int main(void){
  struct sigaction act;

  program_name = "receiver";
  
  daemonize_and_exit_parent();
  
  memset(&act, 0, sizeof(act));
  act.sa_handler = sig_term;
  if (sigaction(SIGTERM, &act, NULL) < 0)
    fail_startup("cannot set a handler for SIGTERM : %s\n", strerror(errno));

  if (!pid_lock("")){
    finish_startup("gnbd_clusterd already running\n");
    exit(0);
  }
  fd = clu_connect(NULL, 0);

  if (fd < 0)
    fail_startup("cannot connect to cluster manager : %s\n", strerror(-fd));
  finish_startup("connected\n");

  while(1){
    pause();
  }

  return 0;
} 
