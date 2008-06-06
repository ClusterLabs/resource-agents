#include <unistd.h>
#include <stdio.h>
#ifndef __USE_XOPEN2K
#define __USE_XOPEN2K  /* needed to get posix_memalign */
#endif
#include <stdlib.h> 
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "gnbd.h"
#include "gnbd_endian.h"
#include "list.h"
#include "gnbd_utils.h"
#include "fence.h"
#include "device.h"
#include "gserv.h"
#include "trans.h"
#include "local_req.h"

#include <sys/socket.h>

int is_gserv = 0; /* This gets set when child is forked. It is needed so that
		     the gserv processes don't execute the atexit commands
		     set up for the main program */

list_decl(waiter_list);
list_decl(gserv_list);
off_t file_offset = (off_t)-1;

struct waiter_s {
  int sock;
  int count;
  pid_t *pids;
  list_t list;
};
typedef struct waiter_s waiter_t;

void send_keep_alive(int sock);

struct gserv_info_s {
  char node[65];
  dev_info_t *dev;
  pid_t pid;
  list_t list;
};
typedef struct gserv_info_s gserv_info_t;

void readit(int fd, void *buf, size_t count, char *msg, int remote)
{
  int bytes;
  
  while(count > 0){
    got_sighup = 0;
    bytes = read(fd, buf, count);
    if (bytes < 0){
      if (errno != EINTR){
        log_err("failed reading %s : %s\n", msg, strerror(errno));
        exit(1);
      }
      log_verbose("read interrupted, retrying\n");
      if (remote && got_sighup)
        send_keep_alive(fd);
      continue;
    }
    if (bytes == 0){
      log_fail("read EOF %s\n", msg);
      exit(1);
    }
    buf += bytes;
    count -= bytes;
  }
}

void writeit(int fd, void *buf, size_t count, char *msg)
{
  int bytes;
  
  while(count > 0){
    bytes = write(fd, buf, count);
    if (bytes < 0){
      if (errno != EINTR){
        log_err("failed writing %s : %s\n", msg, strerror(errno));
        exit(1);
      }
      log_verbose("write interrupted. retrying\n");
      continue;
    }
    /* FIXME -- should I do this */
    if (bytes == 0){
      log_fail("returned 0 when writing %s\n", msg);
      exit(1);
    }
    buf += bytes;
    count -= bytes;
  }
}

#define SEND_REPLY \
do { \
  writeit(sock, &reply, sizeof(reply), "reply"); \
} while(0)

/* FIXME -- Do I need to do something with the offset pointer */
#define SEND_ERR \
do { \
  reply.error = cpu_to_be32(-1); \
  SEND_REPLY; \
  reply.error = 0; \
  file_offset = -1; \
} while(0)

void send_keep_alive(int sock)
{
  struct gnbd_reply reply;
  reply.magic = be32_to_cpu(GNBD_KEEP_ALIVE_MAGIC);
  /*
   *You do this twice, because the first write to a dead socket
   * doesn't fail
   */
  SEND_REPLY;
  SEND_REPLY;
}

#define MAXSIZE 131072

void do_file_read(int fd, char *buf, uint64_t req_offset, uint32_t len)
{ 
  if (req_offset != file_offset){
    if (lseek(fd, req_offset, SEEK_SET) < 0){
      log_err("cannot seek to request location : %s\n", strerror(errno));
      exit(1);
    }
    file_offset = req_offset;
  }
  readit(fd, buf, len, "in do_file_read", 0);
  file_offset += len;
}

void do_file_write(int fd, char *buf, uint64_t req_offset, uint32_t len)
{
  if (req_offset != file_offset){
    if (lseek(fd, req_offset, SEEK_SET) < 0){
      log_err("cannot seek to request location : %s\n", strerror(errno));
      exit(1);
    }
    file_offset = req_offset;
  }
  writeit(fd, buf, len, "in do_file_write");
  file_offset += len;
}

/* This must be called with SIGCHLD blocked */
static list_t *get_next_valid(list_t *list_item, list_t *head){
  gserv_info_t *info;
  list_t *curr, *next;
  curr = list_item;
  next = curr->next;
  while(curr != head){
    info = list_entry(curr, gserv_info_t, list);
    if (info->pid != 0)
      break;
    list_del(&info->list);
    free(info);
    curr = next;
    next = curr->next;
  }
  return curr;
}
    
/* This must be called with SIGCHLD blocked */
#define foreach_gserv(tmp, head) \
  for ((tmp) = get_next_valid((head)->next, (head)); (tmp) != (head); \
       (tmp) = get_next_valid((tmp)->next, (head)))

int get_gserv_info(char **buffer, uint32_t *list_size)
{
  gserv_req_t *ptr;
  gserv_info_t *server;
  list_t *list_item;
  int count = 0;

  *buffer = NULL;
  block_sigchld();
  foreach_gserv(list_item, &gserv_list)
    count++;
  if (count == 0){
    *list_size = 0;
    return 0;
  }
  ptr = (gserv_req_t *)malloc(sizeof(gserv_req_t) * count);
  if (!ptr){
    log_err("cannot allocate memory for server info replay\n");
    return -ENOMEM;
  }
  *buffer = (char *)ptr;
  *list_size = (uint32_t)(sizeof(gserv_req_t) * count);
  list_foreach(list_item, &gserv_list){
    server = list_entry(list_item, gserv_info_t, list);
    strncpy(ptr->node, server->node, 65);
    ptr->pid = (uint32_t)server->pid;
    strncpy(ptr->name, server->dev->name, 32);
    ptr->name[31] = 0;
    ptr++;
  }
  unblock_sigchld();
  return 0;
}

void gserv(int sock, char *node, uint64_t sectors, unsigned int flags,
           char *name, int devfd)
{
  void *buf;
  struct gnbd_request request;
  struct gnbd_reply reply;
  uint64_t device_size = sectors << 9;
  uint64_t offset;
  uint32_t len;
  uint32_t type;
  char from_str[70];
  char to_str[70];

  /* FIXME -- This should be done when I first open the file.. maybe */
  if (posix_memalign(&buf, fpathconf(devfd, _PC_REC_XFER_ALIGN),
                     MAXSIZE) < 0){
    fprintf(stderr, "posix_memalign failed : %s\n", strerror(errno));
    exit(1);
  }

  sprintf(from_str, "from %s", node);
  sprintf(to_str, "to %s", node);

  /* FIXME -- setup signal handling*/
  reply.magic = be32_to_cpu(GNBD_REPLY_MAGIC);
  reply.error = 0;

  while(1){
    readit(sock, &request, sizeof(request), from_str, 1);
    offset = be64_to_cpu(request.from);
    type = be32_to_cpu(request.type);
    len = be32_to_cpu(request.len);
    memcpy(reply.handle, request.handle, sizeof(reply.handle));

    /* If I get these two errors, there is something unfixable wrong with the
       gnbd client */
    if (be32_to_cpu(request.magic) != GNBD_REQUEST_MAGIC){
      log_fail("bad request magic 0x%lx %s, shutting down\n",
               (unsigned long)be32_to_cpu(request.magic), from_str);
      exit(1);
    }
    if (len > MAXSIZE){
      log_err("request len %lu is larger than my buffer, shutting down\n",
              (unsigned long)len);
      exit(1);
    }
    /* If I get these two errors, someone is sending me bunk requests. */
    if ((UINT64_MAX - offset) < len){
      readit(sock, buf, len, from_str, 1);
      log_fail("request %s past the end of the block device\n", from_str);
      SEND_ERR;
      continue;
    }
    if ((offset + len) > device_size){
      readit(sock, buf, len, from_str, 1);
      log_fail("request %s past the end of the device\n", from_str);
      SEND_ERR;
      continue;
    }

    switch(type){
    case GNBD_CMD_READ:
      memcpy(buf, &reply, sizeof(reply));
      do_file_read(devfd, buf, offset, len);
      SEND_REPLY;
      writeit(sock, buf, len, to_str);
      break;
    case GNBD_CMD_WRITE:
      readit(sock, buf, len, from_str, 1);
      do_file_write(devfd, buf, offset, len);
      SEND_REPLY;
      break;
    case GNBD_CMD_DISC:
      /* It's the clients job to make sure that there are no outstanding
         writes */
      log_verbose("got shutdown request, shutting down\n");
      SEND_REPLY;
      exit(0);
    case GNBD_CMD_PING:
      log_verbose("got ping command\n");
      SEND_REPLY;
      break;
    default:
      log_fail("got unknown request type (%d) %s, shutting down\n",
               type, from_str);
      SEND_ERR;
      exit(1);
    }
  }
  free(buf);
  exit(0);
}

/* This must be called with SIGCHLD blocked */
int add_gserv_info(int sock, char *node, dev_info_t *dev, pid_t pid)
{
  gserv_info_t *info;
  
  info = (gserv_info_t *)malloc(sizeof(gserv_info_t));
  if (!info){
    printe("couldn't allocate memory for server info\n");
    return -1;
  }
  strncpy(info->node, node, 65);
  info->dev = dev;
  info->pid = pid;
  list_add(&info->list, &gserv_list);
  
  return 0;
}

void reply_to_waiter(waiter_t *waiter)
{
  uint32_t reply = 0;

  if (retry_write(waiter->sock, &reply, sizeof(reply)) < 0)
    log_err("cannot reply to remove server request : %s\n", strerror(errno));
  close(waiter->sock);
}

/* this must be called with SIGCHLD blocked */
void release_waiters(pid_t pid)
{
  int count;
  list_t *list_item;
  waiter_t *waiter;

  list_foreach(list_item, &waiter_list){
    waiter = list_entry(list_item, waiter_t, list);
    for (count = 0; count < waiter->count; count++){
      if (pid == waiter->pids[count])
        break;
    }
    if (count >= waiter->count)
      continue;
    waiter->count--;
    if (waiter->count)
      waiter->pids[count] = waiter->pids[waiter->count];
    else
      reply_to_waiter(waiter);
  }
}

void sig_chld(int sig)
{
  int status;
  pid_t pid;
  list_t *list_item;
  gserv_info_t *info;
  
  while( (pid = waitpid(-1, &status, WNOHANG)) > 0){
    if(WIFEXITED(status))
      log_msg("server process %d exited with %d", pid, WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
      log_msg("server process %d exited because of signal %d\n", pid,
              WTERMSIG(status));
    list_foreach(list_item, &gserv_list){
      info = list_entry(list_item, gserv_info_t, list);
      if (info->pid == pid){
        info->pid = 0;
        release_waiters(pid);
        break;
      }
    }
    if (list_item == &gserv_list)
      log_err("couldn't find server [pid: %d] in servers list\n", pid);
  }
}

void fork_gserv(int sock, char *node, dev_info_t *dev, int devfd)
{
  struct sigaction act;
  pid_t pid;
  block_sigchld();
  if( (pid = fork()) < 0){
    log_err("cannot for child to handle the connection : %s\n",
            strerror(errno));
    return;
  }
  if (pid != 0){
    if (add_gserv_info(sock, node, dev, pid) < 0)
      kill(pid, SIGTERM);
    unblock_sigchld();
    return;
  }
  is_gserv = 1;
  unblock_sigchld();
  
  memset(&act,0,sizeof(act));
  act.sa_handler = SIG_DFL;
  if( sigaction(SIGTERM, &act, NULL) <0){
    log_err("cannot restore SIGTERM handler : %s\n", strerror(errno));
    exit(1);
  }
  memset(&act,0,sizeof(act));
  act.sa_handler = SIG_DFL;
  if( sigaction(SIGCHLD, &act, NULL) <0){
    log_err("cannot restore SIGCHLD handler : %s\n", strerror(errno));
    exit(1);
  }
  /* FIXME -- is this necessary. I think that it's already set to this */
  memset(&act,0,sizeof(act));
  act.sa_handler = sig_hup;
  if( sigaction(SIGHUP, &act, NULL) <0){
    log_err("cannot set SIGHUP handler : %s\n", strerror(errno));
    exit(1);
  }
  /* FIXME -- need to close and free things, like the external socket, and
     useless memory, and the log... I need to open a new one.
     There is probably some signal stuff that I should do */
  gserv(sock, node, dev->sectors, dev->flags, dev->name, devfd);
  exit(0);
}

int gserv_login(int sock, char *node, login_req_t *login_req,
                dev_info_t **devptr, int *devfd)
{
  uint64_t sectors;
  int err;
  login_reply_t login_reply;
  dev_info_t *dev;
  int fd;

  *devfd = -1;
  *devptr = NULL;
  login_reply.version = 0;
  login_reply.err = 0;

  BE_LOGIN_REQ_TO_CPU(login_req);

  if (login_req->version != PROTOCOL_VERSION){
    log_err("protocol version mismatch: client it using version %d, "
            "server is using version %d\n", login_req->version,
            PROTOCOL_VERSION);
    login_reply.version = PROTOCOL_VERSION;
    err = -EINVAL;
    goto fail_reply;
  }

  err = update_timestamp_list(node, login_req->timestamp);
  if (err)
    goto fail_reply;
  
  if (check_banned_list(node)) {
    log_err("client %s is banned. Canceling login\n", node);
    err = -EPERM;
    goto fail_reply;
  }

  dev = find_device(login_req->devname);
  if (dev == NULL){
    log_err("unknown device '%s'. login failed\n", login_req->devname);
    err = -ENODEV;
    goto fail_reply;
  }
  
  if (dev->flags & GNBD_FLAGS_INVALID){
    log_err("device '%s' is marked invalid. login failed\n",
            login_req->devname);
    err = -ENODEV;
    goto fail_reply;
  }
  
  err = open_file(dev->path, dev->flags, &fd);
  if (err < 0)
    goto fail_reply;

  err = get_size(fd, &sectors);
  if (err < 0)
    goto fail_reply;
  if (sectors != dev->sectors){
    log_fail("size of the exported file %s has changed, aborting\n",
             dev->path);
    goto fail_file;
  }
  
  login_reply.sectors = dev->sectors;

  CPU_TO_BE_LOGIN_REPLY(&login_reply);

  if (retry_write(sock, &login_reply, sizeof(login_reply)) < 0){
    err = -errno;
    log_err("cannot set login reply to %s failed : %s\n", node,
            strerror(errno));
    goto fail_file;
  }
  
  *devfd = fd;
  *devptr = dev;
  return 0;

 fail_file:
  close(fd);

 fail_reply:
  login_reply.err = -err;
  CPU_TO_BE_LOGIN_REPLY(&login_reply);
  retry_write(sock, &login_reply, sizeof(login_reply));
  return err;
}

int __find_gserv_info(char *node, dev_info_t *dev)
{
  list_t *list_item;
  gserv_info_t *info = NULL;
  foreach_gserv(list_item, &gserv_list) {
    info = list_entry(list_item, gserv_info_t, list);
    if ((!node || strncmp(info->node, node, 65) == 0) &&
        (!dev || dev == info->dev)){
      return 1;
    }
  }
  return 0;
}

int find_gserv_info(char *node, dev_info_t *dev)
{
  int ret;
  block_sigchld();
  ret = __find_gserv_info(node, dev);
  unblock_sigchld();
  return ret;
}

/* call with sigchld blocked */
void kill_all_gserv(void)
{
  list_t *list_item;
  gserv_info_t *info = NULL;
  foreach_gserv(list_item, &gserv_list) {
    info = list_entry(list_item, gserv_info_t, list);
    kill(info->pid, SIGTERM);
  }
}
  
void validate_gservs(void)
{
  list_t *list_item;
  gserv_info_t *info;

  foreach_gserv(list_item, &gserv_list) {
    info = list_entry(list_item, gserv_info_t, list);
    kill(info->pid, SIGHUP);
  }
}

int kill_gserv(char *node, dev_info_t *dev, int sock)
{
  int err = 0;
  list_t *list_item, *tmp;
  gserv_info_t *info = NULL;
  waiter_t *waiter = NULL;
  int count = 0;
  
  block_sigchld();

  /* free the dead waiters */
  list_foreach_safe(list_item, &waiter_list, tmp) {
    waiter = list_entry(list_item, waiter_t, list);
    if (!waiter->count){
      if (waiter->pids)
        free(waiter->pids);
      list_del(&waiter->list);
      free(waiter);
    }
  }
  waiter = malloc(sizeof(waiter_t));
  if (!waiter){
    log_err("cannot allocate memory for waiter\n");
    err = -ENOMEM;
    goto out;
  }
  waiter->count = 0;
  waiter->sock = sock;
  waiter->pids = NULL;
  foreach_gserv(list_item, &gserv_list) {
    info = list_entry(list_item, gserv_info_t, list);
    if ((!node || strncmp(info->node, node, 65) == 0) &&
        (!dev || dev == info->dev))
      waiter->count++;
  }
  if (!waiter->count){
    reply_to_waiter(waiter);
    free(waiter);
    goto out;
  }
  waiter->pids = malloc(waiter->count * sizeof(pid_t));
  if (!waiter->pids){
    log_err("cannot allocate memory for waiter pid list\n");
    free(waiter);
    err = -ENOMEM;
    goto out;
  }
  list_add(&waiter->list, &waiter_list);
  foreach_gserv(list_item, &gserv_list) {
    info = list_entry(list_item, gserv_info_t, list);
    if ((!node || strncmp(info->node, node, 65) == 0) &&
        (!dev || dev == info->dev)){
      waiter->pids[count] = info->pid;
      count++;
      /* FIXME -- I need to make sure that I have I can't get EPERM here */
      kill(info->pid, SIGTERM);
    }
  }
 out:
  unblock_sigchld();
  return err;
}
