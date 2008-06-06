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
#include "extern_req.h"
#include "fence.h"

list_decl(timestamp_list);
list_decl(banned_list);

typedef struct timestamp_s {
  uint64_t timestamp;
  char node[65];
  list_t list;
} timestamp_t;

typedef struct banned_s {
  char node[65];
  list_t list;
} banned_t;

int update_timestamp_list(char *node, uint64_t timestamp)
{
  list_t *list_item;
  timestamp_t *client;

  list_foreach(list_item, &timestamp_list) {
    client = list_entry(list_item, timestamp_t, list);
    if (strncmp(node, client->node, 64) == 0) {
      if (client->timestamp != timestamp){
        remove_from_banned_list(node);
        client->timestamp = timestamp;
      }
      return 0;
    }
  }
  client = (timestamp_t *)malloc(sizeof(timestamp_t));
  if (!client){
    log_err("couldn't allocate memory for the client %s timestamp\n", node);
    return -ENOMEM;
  }
  strncpy(client->node, node, 65);
  client->timestamp = timestamp;
  remove_from_banned_list(node);
  list_add(&client->list, &timestamp_list);

  return 0;
}

int check_banned_list(char *node)
{
  list_t *list_item;
  banned_t *banned_client;

  list_foreach(list_item, &banned_list) {
    banned_client = list_entry(list_item, banned_t, list);
    if (strncmp(node, banned_client->node, 64) == 0) {
      return 1;
    }
  }
  return 0;
}

int add_to_banned_list(char *node)
{
  banned_t *banned_client;
  
  if (check_banned_list(node)){
    log_err("client %s already banned\n", node);
    return 0;
  }
  banned_client = (banned_t *)malloc(sizeof(banned_t));
  if (!banned_client) {
    log_err("couldn't allocate memory for the banned client\n");
    return -ENOMEM;
  }
  strncpy(banned_client->node, node, 65);
  list_add(&banned_client->list, &banned_list);
  return 0;
}

void remove_from_banned_list(char *node)
{
  list_t *list_item;
  banned_t *banned_client;
  
  list_foreach(list_item, &banned_list) {
    banned_client = list_entry(list_item, banned_t, list);
    if (strncmp(banned_client->node, node, 64) == 0) {
      list_del(list_item);
      free(banned_client);
      return;
    }
  }
  log_verbose("client %s is not on banned list\n", node);
}

int list_banned(char **buffer, uint32_t *list_size)
{
  node_req_t *ptr;
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
  ptr = (node_req_t *)malloc(sizeof(node_req_t) * count);
  if (!ptr) {
    log_err("cannot allocate memory for banned list reply\n");
    return -ENOMEM;
  }
  *buffer = (char *)ptr;
  *list_size = (uint32_t)(sizeof(node_req_t) * count);
  list_foreach(list_item, &banned_list) {
    banned_client = list_entry(list_item, banned_t, list);
    strncpy(ptr->node_name, banned_client->node, 65);
    ptr++;
  }
  
  return 0;
}
