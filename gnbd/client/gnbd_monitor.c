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
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <magma.h>
#include <netdb.h>
#include <netinet/in.h>
#include <linux/gnbd.h>

#include "gnbd_endian.h"
#include "list.h"
#include "trans.h"
#include "gnbd_utils.h"
#include "extern_req.h"
#include "gnbd_monitor.h"

struct pollfd *polls;
int max_id;

cluster_member_list_t *cluster_members;

#define CLUSTER 0
#define CONNECT 1

list_t monitor_list;
char sysfs_buf[4096];

struct monitor_s {
  int minor_nr;
  int timeout;
  int reset;
  list_t list;
};
typedef struct monitor_s monitor_t;

void remove_device(int minor_nr)
{
  list_t *item;
  monitor_t *dev;

  list_foreach(item, &monitor_list){
    dev = list_entry(item, monitor_t, list);
    if (dev->minor_nr == minor_nr){
      list_del(&dev->list);
      free(dev);
      return;
    }
  }
}

int monitor_device(int minor_nr, int timeout)
{
  list_t *item;
  monitor_t *dev;
  
  list_foreach(item, &monitor_list){
    dev = list_entry(item, monitor_t, list);
    if (dev->minor_nr == minor_nr)
      return 0;
  }
  dev = (monitor_t *)malloc(sizeof(monitor_t));
  if (!dev)
    return -ENOMEM;
  dev->minor_nr = minor_nr;
  dev->timeout = timeout;
  dev->reset = 0;
  list_add(&dev->list, &monitor_list);
  return 0;
}

char *get_sysfs_attr(int minor, char *attr_name)
{
  int sysfs_fd;
  int bytes;
  int count = 0;
  char sysfs_path[40];
  
  snprintf(sysfs_path, 40, "/sys/class/gnbd/gnbd%d/%s", minor, attr_name);
  if( (sysfs_fd = open(sysfs_path, O_RDONLY)) < 0){
    printe("cannot open %s : %s\n", sysfs_path, strerror(errno));
    exit(1);
  }
  while (count < 4095){
    bytes = read(sysfs_fd, &sysfs_buf[count], 4095 - count);
    if (bytes < 0 && errno != EINTR){
      printe("cannot read %s : %s\n", sysfs_path, strerror(errno));
      exit(1);
    }
    if (bytes == 0)
      break;
    count += bytes;
  }
  /* overwrite the '\n' with '\0' */
  sysfs_buf[count - 1] = 0;
  if (close(sysfs_fd) < 0){
    printe("close on %s returned error : %s\n", sysfs_path, strerror(errno));
    exit(1);
  }
  return sysfs_buf;
}

int start_request_sock(void)
{
  int sock;
  struct sockaddr_un addr;
  struct stat stat_buf;

  if( (sock = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
    fail_startup("cannot create unix socket : %s\n", strerror(errno));
  
  /* FIXME -- I should take the name out of this function, and put it
     someplace that is user defineable */
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, 108, "%s/gnbd_monitorcomm", program_dir);
  
  if (stat(addr.sun_path, &stat_buf) < 0){
    if (errno != ENOENT)
      fail_startup("cannot stat unix socket file '%s' : %s\n", addr.sun_path,
                   strerror(errno));
  }
  else if (unlink(addr.sun_path) < 0)
    fail_startup("cannot remove unix socket file '%s' : %s\n", addr.sun_path,
                 strerror(errno));

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    fail_startup("cannot bind unix socket to /var/run/gnbd_servcomm : %s\n",
                 strerror(errno));

  if (chmod(addr.sun_path, S_IRUSR | S_IWUSR) < 0)
    fail_startup("cannot set the file permissions on the unix socket : %s\n",
                 strerror(errno));

  if (listen(sock, 1) < 0)
    fail_startup("cannot listen on unix socket : %s\n", strerror(errno));
  
  return sock;
}

void setup_poll(void)
{
  polls = malloc(open_max() * sizeof(struct pollfd));
  if (!polls)
    fail_startup("cannot allocate poller structure : %s\n", strerror(errno));
  polls[CLUSTER].fd = clu_connect(NULL, 0);
  if (polls[CLUSTER].fd < 0)
    fail_startup("cannot connect to cluster manager : %s\n",
                 strerror(-(polls[CLUSTER].fd)));
  cluster_members = clu_member_list(NULL);
  if (!cluster_members)
    fail_startup("cannot get initial member list\n");
  if (memb_resolve_list(cluster_members, NULL) < 0)
    fail_startup("cannot resolve member list\n");
  polls[CLUSTER].events = POLLIN;
  polls[CONNECT].fd = start_request_sock();
  polls[CONNECT].events = POLLIN;
  max_id = 1;
}
 
void close_poller(int index){
  if (index == CLUSTER){
    log_err("lost connection to the cluster manager\n");
    /* FIXME -- should do something different */
    exit(1);
  }
  if (index == CONNECT){
    log_err("lost request socket\n");
    /* FIXME -- again, don't do this */
    exit(1);
  }
  close(polls[index].fd);
  polls[index].fd = -1;
  polls[index].revents = 0;
  while(polls[max_id].fd == -1)
    max_id--;
}

void accept_connection(void)
{
  int sock;
  struct sockaddr_un addr;
  socklen_t len = sizeof(addr);
  int i;

  sock = accept(polls[CONNECT].fd, (struct sockaddr *)&addr, &len);
  if (sock < 0){
    log_err("error accepting connect to unix socket : %s\n", strerror(errno));
    return;
  }
  for (i = 0; polls[i].fd >= 0 && i < open_max(); i++);
  if (i >= open_max()){
    log_err("maximum number of open file descriptors reached\n");
    close(sock);
    return;
  }
  polls[i].fd = sock;
  polls[i].events = POLLIN;
  if (i > max_id) max_id = i;
}

#define DO_TRANS(action, label)\
do {\
  if ((action)){\
    log_err("request transfer failed at line %d : %s\n", \
            __LINE__, strerror(errno));\
    goto label;\
  }\
} while(0)

int get_monitor_list(char **buffer, unsigned int *list_size)
{
  monitor_info_t *ptr;
  monitor_t *dev;
  list_t *item;
  int count = 0;

  *buffer = NULL;
  list_foreach(item, &monitor_list)
    count++;
  if (count == 0){
    *list_size = 0;
    return 0;
  }
  ptr = (monitor_info_t *)malloc(sizeof(monitor_info_t) * count);
  if (!ptr){
    log_err("cannot allocate memory for monitor list\n");
    return -ENOMEM;
  }
  *buffer = (char *)ptr;
  *list_size = (unsigned int)(sizeof(monitor_info_t) * count);
  list_foreach(item, &monitor_list){
    dev = list_entry(item, monitor_t, list);
    ptr->minor_nr = dev->minor_nr;
    ptr->timeout = dev->timeout;
    ptr++;
  }

  return 0;
}


cluster_member_t *memb_ip_to_p(cluster_member_list_t *list, uint32_t beip)
{
  int x;
  struct addrinfo *ai;

  if (!list)
    return NULL;
  
  for (x = 0; x < list->cml_count; x++){
    if (!list->cml_members[x].cm_addrs){
      log_err("cluster member %s doesn't have ipaddrs resolved\n",
              list->cml_members[x].cm_name);
      exit(1);
    }
    for(ai = list->cml_members[x].cm_addrs; ai; ai = ai->ai_next){
      if (ai->ai_family != AF_INET){
        /* FIXME -- need to not do this */
        log_err("FIXME -- cannot handle any addr types other that AF_INET\n");
        exit(1);
      }
      if (beip == ((struct sockaddr_in *)ai->ai_addr)->sin_addr.s_addr)
        return &list->cml_members[x];
    }
  }
  return NULL;
}

void fail_device(monitor_t *dev)
{
  int pid;
  int fd;
  
  if( (fd = open("/dev/gnbd_ctl", O_RDWR)) < 0){
    printe("cannot open /dev/gnbd_ctl : %s\n", strerror(errno));
    exit(1);
  }
  if (sscanf(get_sysfs_attr(dev->minor_nr, "pid"), "%d", &pid) != 1){
    printe("cannot parse /sys/class/gnbd/gnbd%d/pid\n", dev->minor_nr);
    exit(1);
  }
  kill(pid, SIGKILL);
  if (ioctl(fd, GNBD_CLEAR_QUE, (unsigned long)dev->minor_nr) < 0){
    printe("cannot clear gnbd device #%d queue : %s\n", dev->minor_nr,
           strerror(errno));
    exit(1);
  }
  list_del(&dev->list);
  free(dev);
} 

void fail_devices(cluster_member_list_t *nodes)
{
  monitor_t *dev;
  list_t *item, *next;
  cluster_member_t *node;
  ip_t ip;
  unsigned short port;
  
  list_foreach_safe(item, &monitor_list, next){
    dev = list_entry(item, monitor_t, list);
    if (sscanf(get_sysfs_attr(dev->minor_nr, "server"), "%8x:%4hx",
               &ip, &port) != 2){
      printe("cannot parse /sys/class/gnbd/gnbd%d/server\n", dev->minor_nr);
      exit(1);
    }
    node = memb_ip_to_p(nodes, cpu_to_be32(ip));
    if (node)
      fail_device(dev);
  }
}

void handle_cluster_msg(void)
{
  int event;
  cluster_member_list_t *new, *lost;

  event = clu_get_event(polls[CLUSTER].fd);
  if (event == CE_SHUTDOWN){
    log_err("lost connection to cluster manager\n");
    /* FIXME -- need to retry.. Can't just give up */
    exit(1);
  }
  if (event == CE_MEMB_CHANGE){
    new = clu_member_list(NULL);
    lost = clu_members_lost(cluster_members, new);
    memb_resolve_list(new, cluster_members);
    cml_free(cluster_members);
    cluster_members = new;

    fail_devices(lost);
    cml_free(lost);
  }
}

void handle_request(int index){
  uint32_t cmd;
  uint32_t reply = MONITOR_SUCCESS_REPLY;
  int sock;
  int err;

  sock = polls[index].fd;

  DO_TRANS(retry_read(sock, &cmd, sizeof(cmd)), exit);
  
  switch(cmd){
  case MONITOR_REQ:
    {
      monitor_info_t info;
      DO_TRANS(retry_read(sock, &info, sizeof(info)), exit);
      err = monitor_device(info.minor_nr, info.timeout);
      if (err)
        reply = -err;
      DO_TRANS(retry_write(sock, &reply, sizeof(reply)), exit);
    }
  case REMOVE_REQ:
    {
      int minor;
      DO_TRANS(retry_read(sock, &minor, sizeof(minor)), exit);
      remove_device(minor);
      DO_TRANS(retry_write(sock, &reply, sizeof(reply)), exit);
    }
  case LIST_REQ:
    {
      char *buffer = NULL;
      unsigned int size;
      
      err = get_monitor_list(&buffer, &size);
      if (err < 0){
        reply = -err;
        DO_TRANS(retry_write(sock, &reply, sizeof(reply)), exit);
        break;
      }
      DO_TRANS(retry_write(sock, &reply, sizeof(reply)), list_exit);
      DO_TRANS(retry_write(sock, &size, sizeof(size)), list_exit);
      if (size)
        DO_TRANS(retry_write(sock, buffer, size), list_exit);

    list_exit:
      free(buffer);
      break;
    }
  default:
    log_err("unknown request 0x%x\n", cmd);
    reply = ENOTTY;
    DO_TRANS(retry_write(sock, &reply, sizeof(reply)), exit);
  }
 exit:
  close_poller(index);
}

int do_server_connect(ip_t ip, unsigned short port){
  struct sockaddr_in server;
  int sock_fd;
  sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (sock_fd < 0){
    log_err("error creating socket: %s\n", strerror(errno));
    exit(1);
  }
  server.sin_addr.s_addr = ip;
  server.sin_port = htons(port);
  server.sin_family = AF_INET;
  if (connect(sock_fd, (struct sockaddr *)&server, sizeof(server)) < 0){
    log_err("error connecting to server: %s\n", strerror(errno));
    exit(1);
  }
  return sock_fd;
}

int cleanup_server(monitor_t *dev){
  ip_t ip;
  unsigned short port;
  device_req_t kill_req;
  int sock;
  uint32_t msg = EXTERN_KILL_GSERV_REQ;
  
  if (sscanf(get_sysfs_attr(dev->minor_nr, "server"),
             "%8lx:%hx\n", (unsigned long *)&ip, &port) != 2){
      printe("cannot parse /sys/class/gnbd/gnbd%d/server\n", dev->minor_nr);
      exit(1);
  }
  ip = cpu_to_beip(ip);
  if (sscanf(get_sysfs_attr(dev->minor_nr, "name"),
             "%s\n", kill_req.name) != 1){
      printe("cannot parse /sys/class/gnbd/gnbd%d/name\n", dev->minor_nr);
      exit(1);
  }
  /* FIXME -- This needs to be timed */
  if( (sock = do_server_connect(ip, port)) < 0){
    log_err("cannot connect to the server\n");
    return -1;
  }
  if (send_u32(sock, msg)){
    log_err("cannot send request to server\n");
    return -1;
  }
  if (retry_write(sock, &kill_req, sizeof(kill_req))){
    log_err("cannot send data to server\n");
    return -1;
  }
  if (recv_u32(sock, &msg)){
    log_err("cannot receive reply from server\n");
    return -1;
  }
  if (msg != EXTERN_SUCCESS_REPLY){
    log_err("cannot remove existing server from %s\n", beip_to_str(ip));
    return -1;
  }
  return 0;
}
    
void fence_server(monitor_t *dev)
{
  ip_t ip;
  unsigned short port;
  cluster_member_t *server;

  if (sscanf(get_sysfs_attr(dev->minor_nr, "server"),
             "%8lx:%hx\n", (unsigned long *)&ip, &port) != 2){
    log_err("cannot parse /sys/class/gnbd/gnbd%d/server\n", dev->minor_nr);
    exit(1);
  }
  ip = cpu_to_beip(ip);
  server = memb_ip_to_p(cluster_members, ip);
  if (!server){
    log_err("server %s is not a cluster member, cannot fence.\n",
            beip_to_str(ip));
    return;
  }
  if (clu_fence(server) < 0)
    log_err("fence of %s failed\n", beip_to_str(ip));
}  

void check_devices(void)
{
  list_t *item, *next;
  monitor_t *dev;

  list_foreach_safe(item, &monitor_list, next){
    int waittime;
    int pid;
    dev = list_entry(item, monitor_t, list);
    if (sscanf(get_sysfs_attr(dev->minor_nr, "waittime"),
               "%d", &waittime) != 1){
      printe("cannot parse /sys/class/gnbd/gnbd%d/waittime\n", dev->minor_nr);
      exit(1);
    }
    if (waittime <= dev->timeout){
      if (dev->reset)
        dev->reset = 0;
      continue;
    }
    if (dev->reset){
      fail_device(dev);
      continue;
    }
    if (cleanup_server(dev) < 0){
      fence_server(dev);
      continue;
    }
    if (sscanf(get_sysfs_attr(dev->minor_nr, "pid"), "%d", &pid) != 1){
      printe("cannot parse /sys/class/gnbd/gnbd%d/pid\n", dev->minor_nr);
      exit(1);
    }
    if (kill(pid, SIGTERM) < 0){ /* FIXME -- or something */
      printe("cannot send device #%d the term signal : %s\n", dev->minor_nr,
             strerror(errno));
      fail_device(dev); /* FIXME -- already know that pid doesn't exist */
    } else
      dev->reset = 0;
  }
}

void list_monitored_devs(void){
  monitor_info_t *ptr, *devs;
  int i, count;

  if (do_list_monitored_devs(&devs, &count) < 0)
    exit(1);

  printf("device #   timeout\n");
  ptr = devs;
  for(i = 0; i < count; i++, ptr++)
    printf("%8d   %d\n", ptr->minor_nr, ptr->timeout);
  free(devs);
}
    


int main(int argc, char *argv[])
{
  int minor_nr;
  int timeout;

  if (argc > 3){
    fprintf(stderr, "Usage: %s <minor_nr> <timeout>\n", argv[0]);
    exit(1);
  }

  if (argc == 1){
    list_monitored_devs();
    exit(0);
  }

  if (sscanf(argv[1], "%d", &minor_nr) != 1 || minor_nr < 0 ||
      minor_nr >= MAX_GNBD){
    printe("%s is not a valid minor number\n", argv[1]);
    exit(1);
  }

  if (argc == 2){
    if (do_remove_monitored_dev(minor_nr))
      exit(1);
    exit(0);
  }

  if (sscanf(argv[2], "%d", &timeout) != 1 || timeout <= 0){
    printe("%s is not a valid timeout value\n", argv[2]);
    exit(1);
  }

  program_name = "gnbd_monitor";

  if (check_lock("gnbd_monitor.pid", NULL)){
    if (do_add_monitored_dev(minor_nr, timeout) < 0)
      exit(1);
    exit(0);
  }

  daemonize_and_exit_parent();

  if (!pid_lock(""))
    fail_startup("Temporary problem running gnbd_monitor. Please retry");
    
  list_init(&monitor_list);

  setup_poll();

  if (monitor_device(minor_nr, timeout))
    fail_startup("cannot add device #%d to monitor_list\n", minor_nr);
  
  finish_startup("gnbd_monitor started. Monitoring device #%d\n", minor_nr);
  
  while(1){
    int err;
    int i;

    err = poll(polls, max_id + 1, 5 * 1000);
    if (err <= 0){
      if (err < 0 && errno != EINTR)
        log_err("poll error : %s\n", strerror(errno));
      continue;
    }
    if (err == 0){
      check_devices();
    }
    for(i = 0; i <= max_id; i++){
      if (polls[i].revents & POLLNVAL){
        log_err("POLLNVAL on id %d\n", i);
        close_poller(i);
        continue;
      }
      if (polls[i].revents & POLLERR){
        log_err("POLLERR on id %d\n", i);
        close_poller(i);
        continue;
      }
      if (polls[i].revents & POLLHUP){
        log_err("POLLHUP on id %d\n", i);
        close_poller(i);
        continue;
      }
      if (polls[i].revents & POLLIN){
        if (i == CONNECT)
          accept_connection();
        else if (i == CLUSTER)
          handle_cluster_msg();
        else
          handle_request(i);
      }
    }
  }
  return 0;
}
