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
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/mount.h>

#include "list.h"
#include "gnbd_utils.h"
#include "fence.h"

list_decl(timestamp_list);
list_decl(banned_list);

typedef struct timestamp_s {
  uint64_t timestamp;
  ip_t ip;                /* network byte order */
  list_t list;
} timestamp_t;

typedef struct banned_s {
  ip_t ip;                 /* stored in network byte order */
  list_t list;
} banned_t;

int update_timestamp_list(ip_t ip, uint64_t timestamp)
{
  list_t *list_item;
  timestamp_t *client;

  list_foreach(list_item, &timestamp_list) {
    client = list_entry(list_item, timestamp_t, list);
    if (ip == client->ip) {
      if (client->timestamp != timestamp){
        remove_from_banned_list(ip);
        client->timestamp = timestamp;
      }
      return 0;
    }
  }
  client = (timestamp_t *)malloc(sizeof(timestamp_t));
  if (!client){
    log_err("couldn't allocate memory for the client %s timestamp\n",
            beip_to_str(ip));
    return -ENOMEM;
  }
  client->ip = ip;
  client->timestamp = timestamp;
  remove_from_banned_list(ip);
  list_add(&client->list, &timestamp_list);

  return 0;
}

int check_banned_list(ip_t ip)
{
  list_t *list_item;
  banned_t *banned_client;

  list_foreach(list_item, &banned_list) {
    banned_client = list_entry(list_item, banned_t, list);
    if (ip == banned_client->ip) {
      return 1;
    }
  }
  return 0;
}

int add_to_banned_list(ip_t ip)
{
  banned_t *banned_client;
  
  if (check_banned_list(ip)){
    log_err("client %s already banned\n", beip_to_str(ip));
    return 0;
  }
  banned_client = (banned_t *)malloc(sizeof banned_client);
  if (!banned_client) {
    log_err("couldn't allocate memory for the banned client\n");
    return -ENOMEM;
  }
  banned_client->ip = ip;
  list_add(&banned_client->list, &banned_list);
  return 0;
}

void remove_from_banned_list(ip_t ip)
{
  list_t *list_item;
  banned_t *banned_client;

  list_foreach(list_item, &banned_list) {
    banned_client = list_entry(list_item, banned_t, list);
    if (banned_client->ip == ip) {
      list_del(list_item);
      free(banned_client);
      return;
    }
  }
  log_verbose("client %s is not on banned list\n", beip_to_str(ip));
}

int list_banned(char **buffer, uint32_t *list_size)
{
  uint32_t *ptr;
  banned_t *banned_client;
  list_t *list_item;
  int count = 0;
  
  *buffer = NULL;
  list_foreach(list_item, &banned_list)
    count++;
  if (count == 0){
    *list_size = 0;
    return 0;
  }
  ptr = (uint32_t *)malloc(sizeof(uint32_t) * count);
  if (!ptr) {
    log_err("cannot allocate memory for banned list reply\n");
    return -ENOMEM;
  }
  *buffer = (char *)ptr;
  *list_size = (uint32_t)(sizeof(uint32_t) * count);
  list_foreach(list_item, &banned_list) {
    banned_client = list_entry(list_item, banned_t, list);
    *ptr = banned_client->ip;
    ptr++;
  }

  return 0;
}
