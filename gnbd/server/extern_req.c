#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <inttypes.h>
#include <sys/types.h>
#include <netdb.h>

#include "gnbd_endian.h"
#include "list.h"
#include "gnbd_utils.h"
#include "extern_req.h"
#include "device.h"
#include "gserv.h"
#include "fence.h"
#include "trans.h"

char nodename[NODENAME_SIZE];

int do_startup(int domain, struct sockaddr *addr)
{
  int sock;
  int trueint = 1;

  if( (sock = socket(domain, SOCK_STREAM, IPPROTO_TCP)) < 0)
    return -1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &trueint, sizeof(int)) < 0)
    goto fail;
  if (domain == PF_INET6){
    if (bind(sock, addr, sizeof(struct sockaddr_in6)) < 0)
      goto fail;
  }
  else{
    if (bind(sock, addr, sizeof(struct sockaddr_in)) < 0)
      goto fail;
  }
  if (listen(sock, 5) < 0)
    goto fail;
  return sock;

 fail:
  close(sock);
  return -1;
}

int start_extern_socket(uint16_t port)
{
  int ret;
  char port_str[7];
  struct addrinfo hint, *ai, *tmp;
  memset(&hint, 0, sizeof(hint));
  hint.ai_family = PF_UNSPEC;
  hint.ai_socktype = SOCK_STREAM;
  hint.ai_flags = AI_PASSIVE;

  snprintf(port_str, 6, "%"PRIu16, port);
  port_str[6] = 0;

  ret = getaddrinfo(NULL, port_str, &hint, &ai);
  if (ret)
    return ret;
  for (tmp = ai; tmp; tmp = tmp->ai_next){
    if (tmp->ai_family != AF_INET6)
      continue;
    ret = do_startup(PF_INET6, tmp->ai_addr);
    if (ret >= 0){
      freeaddrinfo(ai);
      return ret;
    }
  }
  for (tmp = ai; tmp; tmp = tmp->ai_next){
    if (tmp->ai_family != AF_INET)
      continue;
    ret = do_startup(PF_INET, tmp->ai_addr);
    if (ret >= 0){
      freeaddrinfo(ai);
      return ret;
    }
  }
  freeaddrinfo(ai);
  return -1;
}

int accept_extern_connection(int listening_sock)
{
  int sock;
  struct sockaddr_in addr;
  socklen_t len = sizeof(addr);
  int trueint = 1;

  sock = accept(listening_sock, (struct sockaddr *)&addr, &len);
  if (sock < 0){
    log_err("error accepting connect to socket : %s\n", strerror(errno));
    return -1;
  }
  if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &trueint, sizeof(int)) < 0){
    log_err("couldn't set socket keepalive option : %s\n", strerror(errno));
    return -1;
  }
  if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &trueint, sizeof(int)) < 0){
    log_err("couldn't set socket nodelay option : %s\n", strerror(errno));
    return -1;
  }
  log_verbose("opened external connection\n");

  return sock;
}

int check_extern_data_len(uint32_t req, int size)
{
  switch(req){
  case EXTERN_NAMES_REQ:
    return 1;
  case EXTERN_FENCE_REQ:
  case EXTERN_UNFENCE_REQ:
    return (size >= sizeof(node_req_t));
  case EXTERN_LIST_BANNED_REQ:
    return 1;
  case EXTERN_KILL_GSERV_REQ:
    return (size >= sizeof(device_req_t) + sizeof(node_req_t));
  case EXTERN_LOGIN_REQ:
    return (size >= sizeof(login_req_t) + sizeof(node_req_t));
  case EXTERN_NODENAME_REQ:
    return 1;
  case EXTERN_UID_REQ:
    return (size >= sizeof(device_req_t));
  default:
    log_err("unknown external request: %u. closing connection.\n",
            (unsigned int)req);
    return -1;
  }
}

#define DO_TRANS(action, label)\
do {\
  if ((action)){\
    log_err("external transfer failed at line %d : %s\n", \
            __LINE__, strerror(errno));\
    goto label;\
  }\
} while(0)

void handle_extern_request(int sock, uint32_t cmd, void *buf)
{
  int err;
  uint32_t reply = EXTERN_SUCCESS_REPLY;
  
  log_verbose("got external command 0x%x\n", (unsigned int)cmd);

  switch(cmd){
  case EXTERN_NAMES_REQ:
    {
      char *buffer = NULL;
      uint32_t size;
      
      err = get_dev_names(&buffer, &size);
      if (err < 0){
        reply = -err;
        DO_TRANS(send_u32(sock, reply), exit);
        break;
      }
      DO_TRANS(send_u32(sock, reply), names_exit);
      DO_TRANS(send_u32(sock, size), names_exit);
      if (size)
        DO_TRANS(retry_write(sock, buffer, size), names_exit);
      
    names_exit:
      free(buffer);
      break;
    }
  case EXTERN_FENCE_REQ:
    {
      node_req_t fence_node;

      if (is_clustered == 0){
        log_err("cannot fence clients when started with the -n option\n");
        reply = ENOTSUP;
        DO_TRANS(send_u32(sock, reply), exit);
        break;
      }

      memcpy(&fence_node, buf, sizeof(fence_node));

      err = add_to_banned_list(fence_node.node_name);
      if (!err)
        err = kill_gserv(fence_node.node_name, NULL, sock);
      if (err < 0){
        reply = -err;
        DO_TRANS(send_u32(sock, reply), exit);
        close(sock);
      }
      return;
    }
  case EXTERN_UNFENCE_REQ:
    {
      node_req_t unfence_node;

      memcpy(&unfence_node, buf, sizeof(unfence_node));
      remove_from_banned_list(unfence_node.node_name);
      DO_TRANS(send_u32(sock, reply), exit);
      break;
    }
  case EXTERN_UID_REQ:
    {
      device_req_t uid_req;
      char *buffer = NULL;
      uint32_t size;

      memcpy(&uid_req, buf, sizeof(uid_req));
      err = get_dev_uid(uid_req.name, &buffer, &size);
      if (err < 0){
        reply = -err;
        DO_TRANS(send_u32(sock,reply), exit);
        break;
      }
      DO_TRANS(send_u32(sock, reply), exit);
      DO_TRANS(send_u32(sock, size), exit);
      if (size)
        DO_TRANS(retry_write(sock, buffer, size), exit);
      break;
    }
  case EXTERN_NODENAME_REQ:
    {
      uint32_t size;

      size = strlen(nodename) + 1;
      DO_TRANS(send_u32(sock, reply), exit);
      DO_TRANS(send_u32(sock, size), exit);
      DO_TRANS(retry_write(sock, nodename, size), exit);
      break;
    }
  case EXTERN_LIST_BANNED_REQ:
    {
      char *buffer = NULL;
      uint32_t size;
      
      err = list_banned(&buffer, &size);
      if (err < 0){
        reply = -err;
        DO_TRANS(send_u32(sock, reply), exit);
        break;
      }
      DO_TRANS(send_u32(sock, reply), banned_exit);
      DO_TRANS(send_u32(sock, size), banned_exit);
      if (size)
        DO_TRANS(retry_write(sock, buffer, size), banned_exit);
    banned_exit:
      free(buffer);
      break;
    }
  case EXTERN_KILL_GSERV_REQ:
    {
      device_req_t kill_req;
      node_req_t node;

      memcpy(&kill_req, buf, sizeof(kill_req)); 
      memcpy(&node, buf + sizeof(device_req_t), sizeof(node));

      dev_info_t *dev;

      dev = find_device(kill_req.name);
      if (!dev){
        reply = ENODEV;
        DO_TRANS(send_u32(sock, reply), exit);
        break;
      }
      /* FIXME -- I also need the fence list for this */
      err = kill_gserv(node.node_name, dev, sock);
      if (err < 0){
        reply = -err;
        DO_TRANS(send_u32(sock, reply), exit);
        close(sock);
      }
      return;
    }
  case EXTERN_LOGIN_REQ:
    {
      login_req_t login_req;
      node_req_t node;
      dev_info_t *dev;
      int devfd;

      memcpy(&login_req, buf, sizeof(login_req));
      memcpy(&node, buf + sizeof(login_req_t), sizeof(node));

      err = gserv_login(sock, node.node_name, &login_req, &dev, &devfd);
      if (!err){
        fork_gserv(sock, node.node_name, dev, devfd);
        close(devfd);
      }
      break;
    }
   default:
    log_err("unknown exteranl request 0x%x\n", cmd);
    reply = ENOTTY;
    DO_TRANS(send_u32(sock, reply), exit);
  }
 exit:
  close(sock);
}
