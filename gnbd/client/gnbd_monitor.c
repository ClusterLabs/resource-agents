#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/poll.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/in.h>

#include "gnbd.h"
#include "member_cman.h"
#include "gnbd_endian.h"
#include "list.h"
#include "trans.h"
#include "gnbd_utils.h"
#include "extern_req.h"
#include "gnbd_monitor.h"

struct connection_s {
  uint32_t action;
  int size;
  char *buf;
  int dev; /* minor_nr of device that this connection applies to */
};
typedef struct connection_s connection_t;

struct waiter_s {
  pid_t pid;
  int minor;
  list_t list;
};
typedef struct waiter_s waiter_t;

struct down_node_s {
  int nodeid;
  list_t list;
};
typedef struct down_node_s down_node_t;

#define MAX_NODES 256

list_decl(down_node_list);
list_decl(waiter_list);
connection_t *connections;
struct pollfd *polls;
int max_id;
char node_name[65];
unsigned int checks = 0;
cman_handle_t ch;
cman_node_t nodes[MAX_NODES];
int num_nodes;
int cman_cb;
int cman_reason;

#define BUFSIZE (sizeof(monitor_info_t) + sizeof(uint32_t))
#define RESTART_CHECK 10


#define CLUSTER 0
#define CONNECT 1

list_t monitor_list;

int can_shutdown(void *private){
  return 0;
}

struct monitor_s {
  int minor_nr;
  int timeout;
  int state;
  char server[65];
  list_t list;
};
typedef struct monitor_s monitor_t;

monitor_t *find_device(int minor_nr){
  list_t *item;
  monitor_t *dev;

  list_foreach(item, &monitor_list){
    dev = list_entry(item, monitor_t, list);
    if (dev->minor_nr == minor_nr)
      return dev;
  }
  return NULL;
}

void remove_device(int minor_nr)
{
  monitor_t *dev;

  if( (dev = find_device(minor_nr)) != NULL){
    block_sigchld();
    list_del(&dev->list);
    free(dev);
    unblock_sigchld();
  }
  return;
}

int monitor_device(int minor_nr, int timeout, char *server)
{
  monitor_t *dev;
  
  if (strlen(server) > 64)
    return -EINVAL;

  if (find_device(minor_nr) != NULL)
    return 0;
  dev = (monitor_t *)malloc(sizeof(monitor_t));
  if (!dev)
    return -ENOMEM;
  dev->minor_nr = minor_nr;
  dev->timeout = timeout;
  memcpy(dev->server, server, 65);
  dev->state = NORMAL_STATE;
  list_add(&dev->list, &monitor_list);
  return 0;
}


void setup_poll(void)
{
  int i;

  cman_cb = 0;
  polls = malloc(open_max() * sizeof(struct pollfd));
  if (!polls)
    fail_startup("cannot allocate poller structure : %s\n", strerror(errno));
  connections = malloc(open_max() * sizeof(connection_t));
  if (!connections)
    fail_startup("cannot allocate connection structures : %s\n",
                 strerror(errno));
  polls[CLUSTER].fd = setup_member(NULL);
  if (polls[CLUSTER].fd < 0)
    fail_startup("cannot get cluster fd\n");
  polls[CLUSTER].events = POLLIN;
  connections[CLUSTER].buf = NULL;
  connections[CLUSTER].action = 0;
  connections[CLUSTER].size = 0;
  connections[CLUSTER].dev = -1;
  polls[CONNECT].fd = start_comm_device("gnbd_monitorcomm");
  polls[CONNECT].events = POLLIN;
  for(i = 2; i < open_max(); i++){
    polls[i].fd = -1;
    polls[i].revents = 0;
  }
  max_id = 1;
}
 
void close_poller(int index){
  close(polls[index].fd);
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
  polls[index].fd = -1;
  polls[index].revents = 0;
  free(connections[index].buf);
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
  connections[i].buf = malloc(BUFSIZE);
  if (!connections[i].buf){
    log_err("couldn't allocate memory for connection buffer\n");
    close(sock);
    return;
  }
  connections[i].action = 0;
  connections[i].size = 0;
  connections[i].dev = -1;
  polls[i].fd = sock;
  polls[i].events = POLLIN;
  if (i > max_id)
    max_id = i;
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
    ptr->state = dev->state;
    ptr++;
  }

  return 0;
}

cman_node_t *check_for_node(cman_node_t *list, int len, char *node)
{
  int i;

  for(i = 0; i < len; i++){
    if (!strcmp(list[i].cn_name, node))
      return &list[i];
  }
  return NULL;
}

void do_fail_device(waiter_t *waiter)
{
  int fd;
  pid_t pid;

  if( (pid = fork()) < 0){
    log_err("cannot fork child to fail device #%d : %s\n", waiter->minor,
            strerror(errno));
    exit(1);
  }
  
  if (pid != 0){
    waiter->pid = pid;
    return;
  }

  unblock_sigchld();

  if( (fd = open("/dev/gnbd_ctl", O_RDWR)) < 0){
    log_err("cannot open /dev/gnbd_ctl : %s\n", strerror(errno));
    exit(1);
  }
  if (sscanf(get_sysfs_attr(waiter->minor, "pid"), "%d", &pid) != 1){
    log_err("cannot parse /sys/class/gnbd/gnbd%d/pid\n", waiter->minor);
    exit(1);
  }
  kill(pid, SIGKILL);
  if (ioctl(fd, GNBD_CLEAR_QUE, (unsigned long)waiter->minor) < 0){
    log_err("cannot clear gnbd device #%d queue : %s\n", waiter->minor,
           strerror(errno));
    exit(1);
  }
  exit(0);
} 

void sig_chld(int sig)
{
  int status;
  pid_t pid;
  list_t *list_item;
  waiter_t *tmp, *waiter;
  int redo;
  monitor_t *dev;
  
  while( (pid = waitpid(-1, &status, WNOHANG)) > 0){
    redo = 0;
    waiter = NULL;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
      redo = 1;
    list_foreach(list_item, &waiter_list){
      tmp = list_entry(list_item, waiter_t, list);
      if (tmp->pid == pid){
        waiter = tmp;
        break;
      }
    }
    if (waiter){
      if (redo)
        do_fail_device(waiter);
      else{
        if( (dev = find_device(waiter->minor)) != NULL)
          dev->state = RESET_STATE;
        waiter->pid = -1;
      }
    }
  }
}

void fail_device(monitor_t *dev)
{
  list_t *list_item, *tmp;
  waiter_t *waiter;

  block_sigchld();
  dev->state = FAILED_STATE;
  
  list_foreach_safe(list_item, &waiter_list, tmp) {
    waiter = list_entry(list_item, waiter_t, list);
    if (waiter->pid == -1){
      list_del(&waiter->list);
      free(waiter);
    }
  }
  waiter = malloc(sizeof(waiter_t));
  if (!waiter){
    log_err("cannot allocate memory to fail_device #%d\n", dev->minor_nr);
    exit(1);
  }
  waiter->minor = dev->minor_nr;
  list_add(&waiter->list, &waiter_list);
  do_fail_device(waiter);
  unblock_sigchld();
}

static void fail_devices(char *node)
{
  monitor_t *dev;
  list_t *item;

  list_foreach(item, &monitor_list) {
    dev = list_entry(item, monitor_t, list);
    if (strcmp(dev->server, node) == 0)
      fail_device(dev);
  }
}

static char *nodeid_to_name(int nodeid)
{
  int i;

  for(i = 0; i < num_nodes; i++)
    if (nodes[i].cn_nodeid == nodeid)
      return nodes[i].cn_name;
  log_err("cannot find node that matches nodeid %d\n", nodeid);
  exit(1);
}

static void check_down_nodes(void)
{
  uint64_t fence_time;
  int fenced;
  down_node_t *node;
  list_t *item, *next;
  
  list_foreach_safe(item, &down_node_list, next){
    node = list_entry(item, down_node_t, list);
    if (cman_get_fenceinfo(ch, node->nodeid, &fence_time, &fenced, NULL) < 0) {
      log_err("cannot get fence info for nodeid %d : %s\n", node->nodeid,
              strerror(errno));
      exit(1);
    }
    if (fenced){
      fail_devices(nodeid_to_name(node->nodeid));
      list_del(&node->list);
      free(node);
    }
  }
}

static down_node_t *get_down_node(int nodeid)
{
  list_t *item;
  down_node_t *node;

  list_foreach(item, &down_node_list) {
    node = list_entry(item, down_node_t, list);
    if (node->nodeid == nodeid)
      return node;
  }
  return NULL;
}

static void get_initial_nodelist(void)
{
  if (cman_get_nodes(ch, MAX_NODES, &num_nodes, nodes) < 0) {
    log_err("can't get initial cluster node list : %s\n", strerror(errno));
    exit(1);
  }
}

static void statechange(void)
{
  int ret, i;

  num_nodes = 0;
  memset(&nodes, 0, sizeof(nodes));
  ret = cman_get_nodes(ch, MAX_NODES, &num_nodes, nodes);
  if (ret < 0) {
    log_err("can't get cluster node list : %s\n", strerror(errno));
    exit(1);
  }
  for (i = 0; i < num_nodes; i++){
    if (nodes[i].cn_member) {
      down_node_t *node = get_down_node(nodes[i].cn_nodeid);
      if (!node)
        continue;
      fail_devices(nodes[i].cn_name);
      list_del(&node->list);
      free(node);
    }
    else {
      monitor_t *dev;
      list_t *item;
      if (get_down_node(nodes[i].cn_nodeid))
        continue;
      list_foreach(item, &monitor_list) {
        down_node_t *node;
        dev = list_entry(item, monitor_t, list);
	if (strcmp(dev->server, nodes[i].cn_name) != 0)
          continue;
        if (dev->state == RESET_STATE || dev->state == RESTARTABLE_STATE ||
            dev->state == FAILED_STATE)
          continue;
        node = malloc(sizeof(down_node_t));
        if (!node) {
          log_err("cannot allocate memory for down node %s\n",
                  nodes[i].cn_name);
          exit(1);
        }
        node->nodeid = nodes[i].cn_nodeid;
        list_add(&node->list, &down_node_list);
        break;
      }
    }
  }
}       

void handle_cluster_msg(void)
{
  int ret;

  while(1) {
    ret = cman_dispatch(ch, CMAN_DISPATCH_ONE);
    if (ret < 0)
      break;

    if (cman_cb) {
      cman_cb = 0;
      switch (cman_reason) {
       case CMAN_REASON_STATECHANGE:
        statechange();
        break;
       default:
        break;
      }
    }
    else
      break;
  }
  if (ret == -1 && errno == EHOSTDOWN){
    log_err("lost connection to cluster manager\n");
    exit(1);
  }
}

void handle_msg(int index){
  connection_t *connection = &connections[index];
  uint32_t reply = MONITOR_SUCCESS_REPLY;
  int sock;
  int bytes;
  int err;

  sock = polls[index].fd;
  
  bytes = read(sock, connection->buf + connection->size,
               BUFSIZE - connection->size);
  if (bytes <= 0){
    if (bytes == 0)
      log_err("unexpectedly read EOF on connection, device: %d, action: %d\n",
              connection->dev, connection->action);
    else if (errno != EINTR)
      log_err("cannot read from connection, device: %d, action: %d : %s\n",
              connection->dev, connection->action, strerror(errno));
    log_verbose("total read : %d bytes\n", connection->size);
    close_poller(index);
    return;
  }
  
  connection->size += bytes;
  if (connection->size < sizeof(uint32_t))
    return;
  if (connection->action == 0)
    memcpy(&connection->action, connection->buf, sizeof(uint32_t));
  
  switch(connection->action){
  case MONITOR_REQ:
    {
      monitor_info_t info;
      if (connection->size < sizeof(uint32_t) + sizeof(info))
        return;
      memcpy(&info, connection->buf + sizeof(uint32_t), sizeof(info));
      err = monitor_device(info.minor_nr, info.timeout, info.server);
      if (err)
        reply = -err;
      DO_TRANS(retry_write(sock, &reply, sizeof(reply)), exit);
    }
    break;
  case REMOVE_REQ:
    {
      int minor;
      if (connection->size < sizeof(uint32_t) + sizeof(minor))
        return;
      memcpy(&minor, connection->buf + sizeof(uint32_t), sizeof(minor));
      remove_device(minor);
      DO_TRANS(retry_write(sock, &reply, sizeof(reply)), exit);
    }
    break;
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
    log_err("unknown request 0x%x\n", connection->action);
    reply = ENOTTY;
    DO_TRANS(retry_write(sock, &reply, sizeof(reply)), exit);
  }
 exit:
  close_poller(index);
}

cman_node_t *get_failover_server(monitor_t *dev)
{
  cman_node_t *server;
  list_t *item;
  monitor_t *other_dev;

  server = check_for_node(nodes, num_nodes, dev->server);
  if (server == NULL){
    log_err("server %s is not a cluster member, cannot fence.\n", dev->server);
    return NULL;
  }
  list_foreach(item, &monitor_list){
    other_dev = list_entry(item, monitor_t, list);
    if (!strcmp(other_dev->server, dev->server))
      continue;
    if (other_dev->state == NORMAL_STATE)
      return server;
  }
  return NULL;
}

int check_recvd(monitor_t *dev)
{
  char filename[32];
  int ret;
  int pid;
  
  snprintf(filename, 32, "gnbd_recvd-%d.pid", dev->minor_nr);
  ret = __check_lock(filename, &pid);
  /* If we can't get the lock, ret is either 0 or -1.  If it's -1, we've   */
  /* got an error, in which case we log it.  If 0, the lock file doesn't   */
  /* exist yet, in which case we silently wait for it without complaining. */
  if (ret < 0)
    log_err("cannot check lockfile %s/%s : %s\n", program_dir, filename,
            strerror(errno));
  else
    ret = pid;
  return ret;
}

int check_usage(monitor_t *dev)
{
  int usage;

  if (sscanf(get_sysfs_attr(dev->minor_nr, "usage"), "%d", &usage) != 1){
    log_err("cannot parse /sys/class/gnbd/gnbd%d/usage\n", dev->minor_nr);
    exit(1);
  }
  return usage;
}

int start_recvd(monitor_t *dev)
{
  int i;
  pid_t pid;
  int status;
  char minor_str[4];
  int fd1[2], fd2[2];
  
  snprintf(minor_str, 4, "%d", dev->minor_nr);
  minor_str[3] = 0;
  
  if(pipe(fd1) || pipe(fd2)){
    log_err("pipe error : %s\n", strerror(errno));
    return -1;
  }
  pid = fork();
  if (pid < 0){
    log_err("cannot fork gnbd_recvd : %s\n", strerror(errno));
    return -1;
  }
  
  if (pid){
    close(fd1[0]);
    close(fd2[1]);
    waitpid(pid, &status, 0);
    close(fd1[1]);
    close(fd2[0]);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0){
      log_err("gnbd_recvd failed (%d)\n", WEXITSTATUS(status));
      return -1;
    }
    return 0;
  }
  
  close(fd1[1]);
  close(fd2[0]);
  if (fd2[1] == STDIN_FILENO){
    fd2[1] = dup(fd2[1]);
    if (fd2[1] < 0)
      exit(1);
  }
  if (dup2(fd1[0], STDIN_FILENO) < 0)
    exit(1);
  if (dup2(fd2[1], STDOUT_FILENO) < 0)
    exit(1);
  if (dup2(fd2[1], STDERR_FILENO) < 0)
    exit(1);
  for(i = open_max()-1; i > 2; --i) 
    close(i);
  execlp("gnbd_recvd", "gnbd_recvd", "-f", "-d", minor_str, NULL);
  exit(1);
}

int whack_recvd(monitor_t *dev)
{
  int ret;
  
  ret = check_recvd(dev);
  if (ret < 0)
    return ret;
  else if (ret)
    return kill(ret, SIGHUP);
  else
    return start_recvd(dev);
}

void check_devices(void)
{
  list_t *item, *next;
  monitor_t *dev;

  checks++;

  list_foreach_safe(item, &monitor_list, next){
    int waittime;
    dev = list_entry(item, monitor_t, list);
    if (sscanf(get_sysfs_attr(dev->minor_nr, "waittime"),
               "%d", &waittime) != 1){
      log_err("cannot parse /sys/class/gnbd/gnbd%d/waittime\n", dev->minor_nr);
      exit(1);
    }
    switch(dev->state){
    case NORMAL_STATE:
      if (waittime > dev->timeout){
        whack_recvd(dev);
        dev->state = TIMED_OUT_STATE;
      }
      break;
    case TIMED_OUT_STATE:
      if (waittime <= dev->timeout){
        dev->state = NORMAL_STATE;
      }
      else {
        cman_node_t *server;
        server = get_failover_server(dev);
        if (server){
          cman_handle_t ach;
          ach = cman_admin_init(NULL);
          if (!ach) {
            log_err("cman_admin_init failure : %s\n", strerror(errno));
            goto cant_fence;
          }
          if (cman_kill_node(ach, server->cn_nodeid) < 0){
            log_err("fence of %s failed : %s\n", dev->server, strerror(errno));
            cman_finish(ach);
            goto cant_fence;
          }
          cman_finish(ach);
          dev->state = FENCED_STATE;
          break;
        }
       cant_fence:
        whack_recvd(dev);
      }
      break;
    case RESET_STATE:
      if (!check_recvd (dev))
        dev->state = RESTARTABLE_STATE;
      break;
    case RESTARTABLE_STATE:
      if (check_recvd(dev) > 0)    /* if we got a good lock file  */
        dev->state = NORMAL_STATE; /* go back to the normal state */
      else if (checks % RESTART_CHECK == 0)
        start_recvd(dev);
      break;
    /* FENCED_STATE */
    /* FAILED_STATE */
    }
  }
}

void list_monitored_devs(void){
  char state[12];
  monitor_info_t *ptr, *devs;
  int i, count;

  if (do_list_monitored_devs(&devs, &count) < 0)
    exit(1);

  printf("device #   timeout   state\n");
  ptr = devs;

  for(i = 0; i < count; i++, ptr++){
    switch(ptr->state){
    case NORMAL_STATE:
      strcpy(state, "normal");
      break;
    case TIMED_OUT_STATE:
      strcpy(state, "timed out");
      break;
    case RESET_STATE:
      strcpy(state, "reset");
      break;
    case RESTARTABLE_STATE:
      strcpy(state, "restartable");
      break;
    case FENCED_STATE:
      strcpy(state, "fenced");
      break;
    case FAILED_STATE:
      strcpy(state, "failed");
      break;
    }
    printf("%8d   %7d   %s\n", ptr->minor_nr, ptr->timeout, state);
  }
  free(devs);
}
    
void do_poll(void)
{
  int err;
  int i;
  
  err = poll(polls, max_id + 1, 5 * 1000);
  if (err < 0){
    if (errno != EINTR)
      log_err("poll error : %s\n", strerror(errno));
    return;
  }
  if (err == 0) {
    check_devices();
    check_down_nodes();
    return;
  }
  for (i = 0; i <= max_id; i++){
    if (polls[i].revents & (POLLERR | POLLHUP | POLLNVAL)){
      log_err("Bad poll result, 0x%x on id %d\n", polls[i].revents, i);
      close_poller(i);
      continue;
    }
    if (polls[i].revents & POLLIN){
      if (i == CONNECT)
        accept_connection();
      else if (i == CLUSTER)
        handle_cluster_msg();
      else
        handle_msg(i);
    }
  }
}

void setup_signals(void)
{
  struct sigaction act;

  memset(&act, 0, sizeof(act));
  act.sa_handler = sig_chld;
  if( sigaction(SIGCHLD, &act, NULL) <0)
    fail_startup("cannot setup SIGCHLD handler : %s\n", strerror(errno));
}

int main(int argc, char *argv[])
{
  int minor_nr;
  int timeout;
  int err;

  if (argc != 1 && argc != 2 && argc != 4){
    fprintf(stderr, "Usage: %s <minor_nr> <timeout> <server>\n", argv[0]);
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
    if (do_add_monitored_dev(minor_nr, timeout, argv[3]) < 0)
      exit(1);
    exit(0);
  }

  daemonize_and_exit_parent();

  if (!pid_lock(""))
    fail_startup("Temporary problem running gnbd_monitor. Please retry");
    
  setup_signals();

  if (get_my_nodename(node_name, 1) < 0)
    fail_startup("cannot get node name : %s\n", strerror(errno));
  
  list_init(&monitor_list);

  setup_poll();
  err = monitor_device(minor_nr, timeout, argv[3]);
  if (err)
    fail_startup("cannot add device #%d to monitor_list : %s\n", minor_nr,
                 strerror(err));
  
  finish_startup("gnbd_monitor started. Monitoring device #%d\n", minor_nr);
  get_initial_nodelist();  
  while(1){
    do_poll();
  }
  return 0;
}
