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
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <inttypes.h>

#include "trans.h"
#include "gnbd_utils.h"
#include "gnbd_endian.h"

int got_sighup = 0;

void sig_hup(int sig)
{
  got_sighup = 1;
}

/* I better be able to get away without checking errors here, because
   the code get's a lot uglier it these can fail */
sigset_t block_sigchld(void)
{
  sigset_t set, old;
  sigemptyset(&set);
  sigaddset(&set, SIGCHLD); 
  sigprocmask(SIG_BLOCK, &set, &old);
  return old;
}

void unblock_sigchld(void)
{
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGCHLD);
  sigprocmask(SIG_UNBLOCK, &set, NULL);
}

sigset_t block_sighup(void)
{
  sigset_t set, old;
  sigemptyset(&set);
  sigaddset(&set, SIGHUP); 
  sigprocmask(SIG_BLOCK, &set, &old);
  return old;
}

void unblock_sighup(void)
{
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGHUP);
  sigprocmask(SIG_UNBLOCK, &set, NULL);
}

char *gstrerror(int errcode)
{
  switch(errcode){
  case GNBD_GOT_SIGHUP:
    return "interruped by SIGHUP";
  case GNBD_GOT_EOF:
    return "unexpectly reached EOF";
  default:
    return strerror(errcode);
  }
}

int retry_read(int fd, void *buf, size_t count)
{
  int bytes;
  void *ptr = buf;
  
  got_sighup = 0;
  while(count){
    if (got_sighup){
      errno = GNBD_GOT_SIGHUP;
      return -1;
    }
    bytes = read(fd, ptr, count);
    if (bytes < 0){
      if (errno != EINTR)
        return -1;
      continue;
    }
    if (bytes == 0){
      errno = GNBD_GOT_EOF;
      return -1;
    }
    ptr += bytes;
    count -= bytes;
  }
  return 0;
}

int retry_write(int fd, void *buf, size_t count)
{
  int bytes;
  void *ptr = buf;
  
  got_sighup = 0;
  while(count){
    if (got_sighup){
      errno = GNBD_GOT_SIGHUP;
      return -1;
    }
    bytes = write(fd, ptr, count);
    if (bytes < 0){
      if (errno != EINTR)
        return -1;
      continue;
    }
    if (bytes == 0){
      errno = GNBD_GOT_EOF;
      return -1;
    }
    ptr += bytes;
    count -= bytes;
  }
  return 0;
}

int connect_to_comm_device(char *name)
{
  struct sockaddr_un server;
  int sock_fd;
  
  sock_fd = socket(PF_UNIX, SOCK_STREAM, 0);
  if (sock_fd < 0){
    printe("error creating socket: %s\n", strerror(errno));
    return -1;
  }
  server.sun_family = AF_UNIX;
  snprintf(server.sun_path, 108, "%s/%scomm", program_dir, name);
  server.sun_path[107] = 0;
  if (connect(sock_fd, (struct sockaddr *)&server, sizeof(server)) < 0){
    close(sock_fd);
    printe("error connecting to %s: %s\n", name, strerror(errno));
    return -1;
  }
  return sock_fd;
}

int send_cmd(int fd, uint32_t cmd, char *type)
{
  if (retry_write(fd, &cmd, sizeof(cmd)) < 0) {
    printe("sending %s command failed : %s\n", type, gstrerror(errno));
    return -1;
  }
  return 0;
}

int recv_reply(int fd, char *type)
{
  uint32_t reply;
  
  if (retry_read(fd, &reply, sizeof(reply)) < 0) {
    printe("reading %s reply failed : %s\n", type, gstrerror(errno));
    return -1;
  }
  if (reply)
    printe("%s request failed : %s\n", type, strerror(reply));
  return -reply;
}

int send_u32(int fd, uint32_t msg)
{
  msg = cpu_to_be32(msg);
  return retry_write(fd, &msg, sizeof(msg));
}

int recv_u32(int fd, uint32_t *msg)
{
  if (retry_read(fd, msg, sizeof(uint32_t)) < 0)
    return -1;
  *msg = be32_to_cpu(*msg);
  return 0;
}
