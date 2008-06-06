#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <inttypes.h>
#include <errno.h>
#include <netdb.h>
#include <string.h>

#include "gnbd_utils.h"
#include "trans.h"
#include "gnbd_monitor.h"

int check_addr_info(struct addrinfo *ai1, struct addrinfo *ai2)
{
  int ret = 0;

  for(; ai1; ai1 = ai1->ai_next){
    for(; ai2; ai2 = ai2->ai_next){
      if (ai1->ai_family != ai2->ai_family)
        continue;
      if (ai1->ai_family != AF_INET && ai1->ai_family != AF_INET6)
        continue;
      if (ai1->ai_family == AF_INET){
        struct sockaddr_in *addr4_1, *addr4_2;

        addr4_1 = (struct sockaddr_in *)ai1->ai_addr;
        addr4_2 = (struct sockaddr_in *)ai2->ai_addr;
        if (addr4_1->sin_addr.s_addr == addr4_2->sin_addr.s_addr)
          ret = 1;
      }
      if (ai1->ai_family == AF_INET6){
        struct sockaddr_in6 *addr6_1, *addr6_2;

        addr6_1 = (struct sockaddr_in6 *)ai1->ai_addr;
        addr6_2 = (struct sockaddr_in6 *)ai2->ai_addr;
        if (IN6_ARE_ADDR_EQUAL(&addr6_1->sin6_addr, &addr6_2->sin6_addr))
          ret = 1;
      }
      if (ret == 1)
        break;
    }
  }
  return ret;
}

int do_add_monitored_dev(int minor_nr, int timeout, char *server)
{
  int sock;
  uint32_t msg = MONITOR_REQ;
  monitor_info_t info;

  info.minor_nr = minor_nr;
  info.timeout = timeout;
  memcpy(info.server, server, 65);
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
