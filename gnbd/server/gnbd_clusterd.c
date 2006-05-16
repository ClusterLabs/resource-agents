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
#include <sys/poll.h>

#include "gnbd_utils.h"
#include "member_cman.h"
#include "group.h"


#define CMAN 0
#define GROUP 1

struct pollfd polls[2];
static int quit = 0;
group_callbacks_t callbacks;

static void sig_usr1(int sig)
{}

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

int can_shutdown(void *unused)
{
  return 0;
}

void setup_poll(void)
{
  polls[CMAN].fd = setup_member(NULL);
  if (polls[CMAN].fd < 0)
    finish_startup("cannot join cman\n");
  polls[GROUP].fd = setup_groupd("gnbd_clusterd");
  if (polls[GROUP].fd < 0) {
    exit_member();
    fail_startup("cannot init group\n");
  }
  if (group_join(gh, "default")) {
    exit_groupd();
    exit_member();
    fail_startup("cannot join group\n");
  }
  polls[CMAN].events = POLLIN;
  polls[CMAN].revents = 0;
  polls[GROUP].events = POLLIN;
  polls[GROUP].revents = 0;
}

void do_poll(void)
{
  int err;

  err = poll(polls, 2, -1);
  if (err < 0) {
    if (errno != EINTR)
      log_err("poll error : %s\n", strerror(errno));
    return;
  }
  if (polls[CMAN].revents & (POLLERR | POLLHUP | POLLNVAL)) {
    log_err("Bad poll result 0x%x from cluster\n", polls[CMAN].revents);
    exit(1);
  }
  if (polls[GROUP].revents & (POLLERR | POLLHUP | POLLNVAL)) {
    log_err("Bad poll result 0x%x from groupd\n", polls[GROUP].revents);
    exit(1);
  }

  if (polls[CMAN].revents & POLLIN)
    default_process_member();
  if (polls[GROUP].revents & POLLIN)
    default_process_groupd();
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
  act.sa_handler = sig_usr1;
  if (sigaction(SIGUSR1, &act, NULL) < 0)
    fail_startup("cannot set a handler for SIGUSR1 : %s\n", strerror(errno));

  if (!pid_lock("")){
    finish_startup("gnbd_clusterd already running\n");
    exit(0);
  }

  setup_poll();
  finish_startup("connected\n");

  while(!quit){
    do_poll();
  }
  group_leave(gh, "default");
  group_exit(gh);
  cman_finish(ch);
  return 0;
} 
