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

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <inttypes.h>
#include <errno.h>

#include "gnbd_utils.h"
#include "trans.h"
#include "gnbd_monitor.h"

int do_add_monitored_dev(int minor_nr, int timeout)
{
  int sock;
  uint32_t msg = MONITOR_REQ;
  monitor_info_t info;

  info.minor_nr = minor_nr;
  info.timeout = timeout;
  sock = connect_to_comm_device("gnbd_monitor");
  if (sock < 0)
    return -1;
  if (send_cmd(sock, msg, "monitor device") < 0)
    goto fail;
  if (retry_write(sock, &info, sizeof(info)) < 0) {
    printe("cannot send device information to gnbd_monitor : %s\n",
           gstrerror(errno));
    goto fail;
  }
  if (recv_reply(sock, "monitor device") < 0)
    goto fail;

  close(sock);
  return 0;

 fail:
  close(sock);
  return -1;
}

int do_remove_monitored_dev(int minor_nr)
{
  int sock;
  uint32_t msg = REMOVE_REQ;

  sock = connect_to_comm_device("gnbd_monitor");
  if (sock < 0)
    return -1;
  if (send_cmd(sock, msg, "remove") < 0)
    goto fail;
  if (retry_write(sock, &minor_nr, sizeof(minor_nr)) < 0){
    printe("cannot send minor_nr to gnbd_monitor : %s\n", gstrerror(errno));
    goto fail;
  }
  if (recv_reply(sock, "remove") < 0)
    goto fail;
  close(sock);
  return 0;

 fail:
  close(sock);
  return -1;
}

int do_list_monitored_devs(monitor_info_t **devs, int *count)
{
  int sock;
  uint32_t msg = LIST_REQ;
  monitor_info_t *buf;
  unsigned int size;
  
  *devs = NULL;
  *count = 0;
  
  sock = connect_to_comm_device("gnbd_monitor");
  if (sock < 0)
    return -1;
  if (send_cmd(sock, msg, "list") < 0)
    goto fail;
  if (recv_reply(sock, "list") < 0)
    goto fail;
  if (retry_read(sock, &size, sizeof(size)) < 0){
    printe("cannot get list size from gnbd_monitor : %s\n", gstrerror(errno));
    goto fail;
  }
  if (size == 0){
    close(sock);
    return 0;
  }

  buf = (monitor_info_t *)malloc(size);
  if (!buf){
    printe("cannot allocate memory for gnbd_monitor list\n");
    goto fail;
  }
  if (retry_read(sock, buf, size) < 0){
    printe("cannot get list from gnbd_monitor : %s\n", gstrerror(errno));
    goto fail_free;
  }
  *devs = buf;
  *count = size / sizeof(monitor_info_t);
  close(sock);
  return 0;

 fail_free:
  free(buf);
 fail:
  close(sock);
  return -1;
}
