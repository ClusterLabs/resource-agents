#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <netdb.h>

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

int start_comm_device(char *name){
  int sock;
  struct sockaddr_un addr;
  struct stat stat_buf;
  
  if( (sock = socket(PF_UNIX, SOCK_STREAM, 0)) < 0){
    log_err("cannot create unix socket : %s\n", strerror(errno));
    return -1;
  }

  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, 108, "%s/%s", program_dir, name);
  
  if (stat(addr.sun_path, &stat_buf) < 0){
    if (errno != ENOENT){
      log_err("cannot stat unix socket file '%s' : %s\n",
              addr.sun_path, strerror(errno));
      goto fail;
    }
  }
  else if (unlink(addr.sun_path) < 0){
    log_err("cannot remove unix socket file '%s' : %s\n",
            addr.sun_path, strerror(errno));
    goto fail;
  }

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0){
    log_err("cannot bind unix socket to '%s' : %s\n", addr.sun_path,
            strerror(errno));
    goto fail;
  }

  if (chmod(addr.sun_path, S_IRUSR | S_IWUSR) < 0){
    log_err("cannot set file permissions on unix socket '%s' : %s\n",
            addr.sun_path, strerror(errno));
    goto fail;
  }

  if (listen(sock, 1) < 0){
    log_err("cannot listen on unix socket '%s' : %s\n",
            addr.sun_path, strerror(errno));
    goto fail;
  }
  
  return sock;

 fail:
  close(sock);
  return -1;
}



/* FIXME -- Are you ever called from a process that has stdout/err closed */
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

int do_ipv4_connect(struct in_addr addr, uint16_t port)
{
  struct sockaddr_in server;
  int fd;

  fd = socket(PF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return -1;
  server.sin_family = AF_INET;
  server.sin_port = htons(port);
  memcpy(&server.sin_addr, &addr, sizeof(server.sin_addr));
  
  if (connect(fd, (struct sockaddr *)&server, sizeof(server)) < 0){
    close(fd);
    return -1;
  }
  return fd;
}

int do_ipv6_connect(struct in6_addr addr, uint16_t port)
{
  struct sockaddr_in6 server;
  int fd;

  fd = socket(PF_INET6, SOCK_STREAM, 0);
  if (fd < 0)
    return -1;
  server.sin6_family = AF_INET6;
  server.sin6_port = htons(port);
  server.sin6_flowinfo = 0;
  memcpy(&server.sin6_addr, &addr, sizeof(server.sin6_addr));
  
  if (connect(fd, (struct sockaddr *)&server, sizeof(server)) < 0){
    close(fd);
    return -1;
  }
  return fd;
}
 
/* FIXME -- for non-blocking reasons, I need to be able to set a timeout
   on connections */
/* FIXME -- I really need some macros so that I can print out messages
   in and out of daemons */
int connect_to_server(char *hostname, uint16_t port)
{
  int ret;
  struct addrinfo *ai, *tmp;
  
  ret = getaddrinfo(hostname, NULL, NULL, &ai);
  if (ret)
    return ret;
  for (tmp = ai; tmp; tmp = tmp->ai_next){
    if (tmp->ai_family != AF_INET6)
      continue;
    if (tmp->ai_socktype != SOCK_STREAM)
      continue;
    ret = do_ipv6_connect(((struct sockaddr_in6 *)tmp->ai_addr)->sin6_addr,
                          port);
    if (ret >= 0){
      freeaddrinfo(ai);
      return ret;
    }
  }
  for (tmp = ai; tmp; tmp = tmp->ai_next){
    if (tmp->ai_family != AF_INET)
      continue;
    if (tmp->ai_socktype != SOCK_STREAM)
      continue;
    ret = do_ipv4_connect(((struct sockaddr_in *)tmp->ai_addr)->sin_addr,
                          port);
    if (ret >= 0){
      freeaddrinfo(ai);
      return ret;
    }
  }
  freeaddrinfo(ai);
  return -1;
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
