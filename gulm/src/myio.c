/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2004  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <errno.h>

/*****************************************************************************/
#ifdef OPEN_MAX
static int openmax = OPEN_MAX;
#else
static int openmax = 0;
#endif /* OPEN_MAX */

#define OM_GUESS 256
/**
 * open_max - clacs max number of open files.
 * Returns: the maximum number of file we can have open at a time.
 *          Or -1 for error.
 */
int open_max(void)
{
   if(openmax == 0) {
      errno =0;
      if((openmax = sysconf(_SC_OPEN_MAX)) < 0) {
         if( errno == 0) {
            openmax = OM_GUESS;
         }else{
            return -1;
         }
      }
   }
   return openmax;
}

/*****************************************************************************/
/* wrapped up io */
ssize_t my_recv(int fd, void *buf, size_t len)
{
   ssize_t cnt=0;
   size_t ttl=0;
   while(len > 0) {
      if( (cnt=read(fd, buf, len)) <=0) return cnt;
      len -= cnt;
      buf += cnt;
      ttl += cnt;
   }
   return ttl;
}

ssize_t my_send(int fd, void *buf, size_t len)
{
   ssize_t cnt=0;
   size_t ttl=0;
   while(len > 0) {
      if( (cnt=write(fd, buf, len)) <=0) return cnt;
      len -= cnt;
      buf += cnt;
      ttl += cnt;
   }
   return ttl;
}

ssize_t my_recv_iov(int fd, struct iovec *iov, size_t iov_size)
{
   size_t byte_cnt=0;
   ssize_t cnt;

   while(iov_size > 0) {
      if( (cnt = readv(fd, iov, iov_size)) <=0)
         return cnt;
      byte_cnt += cnt;
      while (cnt >= iov->iov_len) {
         cnt -= iov->iov_len;
         iov++;
         iov_size--;
      }
      if(iov_size <=0) break;
      iov->iov_base += cnt;
      iov->iov_len -= cnt;
   }
   return byte_cnt;
}

ssize_t my_send_iov(int fd, struct iovec *iov, size_t iov_size)
{
   size_t byte_cnt=0;
   ssize_t cnt;

   while(iov_size > 0) {
      if( (cnt = writev(fd, iov, iov_size)) <=0)
         return cnt;
      byte_cnt += cnt;
      while (cnt >= iov->iov_len) {
         cnt -= iov->iov_len;
         iov++;
         iov_size--;
      }
      if(iov_size <=0) break;
      iov->iov_base += cnt;
      iov->iov_len -= cnt;
   }
   return byte_cnt;
}


/*****************************************************************************/
/**
 * socket_free_send_space - how much buffer space is there left?
 * @sk: 
 * 
 * 
 * Returns: int
 */
int socket_free_send_space(int sk)
{
   int unsent, total, len=sizeof(int);

   if( getsockopt(sk, SOL_SOCKET, SO_SNDBUF, &total, &len)<0) return -1;

   /*  TIOCOUTQ is documented as SIOCOUTQ */
   if( ioctl(sk, TIOCOUTQ, &unsent) < 0 ) return -1;

   return total - unsent;
}

/*****************************************************************************/
/* nice abstraction for getting a socket for listening. */
/**
 * set_opts - set some line options.
 * @sk: < socket descriptor
 * 
 * Returns: err if any
 */
int __inline__ set_opts(int sk)
{
   /* leaving this function here even though it is empty.
    * I may have soem option I want to sent in the future this way, and
    * this leaves all the hooks in place.
    */
#if 0
   int trueint=1;

   if(setsockopt(sk, SOL_SOCKET, SO_KEEPALIVE, &trueint, sizeof(int)) <0)
      return -1;
#endif

   return 0;
}
/**
 * serv_listen - start listening to a port.
 * @port: < Port to bind to in Host Byte Order.
 * 
 * Returns: socket file descriptor
 */
int serv_listen(unsigned int port)
{
   int sk, trueint=1;
   struct sockaddr_in6 adr;

   if( (sk = socket(AF_INET6, SOCK_STREAM, 0)) <0)
      return -1;

   if( setsockopt(sk, SOL_SOCKET, SO_REUSEADDR, &trueint, sizeof(int)) <0)
      goto error_exit;

   if( set_opts(sk) <0)
      goto error_exit;

   memset(&adr, 0, sizeof(struct sockaddr_in6));
   adr.sin6_family = AF_INET6;
   adr.sin6_addr = in6addr_any;
   adr.sin6_port = htons( port );

   if( bind(sk, (struct sockaddr *)&adr, sizeof(struct sockaddr_in6)) <0)
      goto error_exit;

   if( listen(sk, 5) <0)
      goto error_exit;

   return sk;
error_exit:
   close(sk);
   return -1;
}

/* vim: set ai cin et sw=3 ts=3: */
