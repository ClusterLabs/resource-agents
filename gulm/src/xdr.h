/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  2002-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#ifndef __gulm_xdr_h__
#define __gulm_xdr_h__
typedef struct xdr_enc_s xdr_enc_t;
typedef struct xdr_dec_s xdr_dec_t;

/* sockets in kernel space are done a bit different than socket in
 * userspace.  But we need to have them appear to be the same.
 */
#ifdef __KERNEL__

#ifdef __linux__
#include <linux/net.h>
#include <linux/in.h>
#include <linux/socket.h>
#include <net/sock.h>

typedef struct socket* xdr_socket;
#endif /*__linux__*/
#else /*__KERNEL__*/
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <errno.h>
typedef int xdr_socket;
#endif /*__KERNEL__*/

/* start things up */
int xdr_open(xdr_socket *sk);
int xdr_connect(struct sockaddr_in6 *adr, xdr_socket sk);
void xdr_close(xdr_socket *sk);

/* deep, basic io */
#ifdef __KERNEL__
#ifdef __linux__
#define XDR_SOCKET_INIT (NULL)
size_t xdr_send(struct socket *sock, void *buf, size_t size);
size_t xdr_recv(struct socket *sock, void *buf, size_t size);
#endif /*__linux__*/
#else /*__KERNEL__*/
#define XDR_SOCKET_INIT (-1)
ssize_t xdr_recv(int fd, void *buf, size_t len);
ssize_t xdr_send(int fd, void *buf, size_t len);
#endif /*__KERNEL__*/

xdr_enc_t* xdr_enc_init(xdr_socket sk, int buffer_size);
xdr_dec_t* xdr_dec_init(xdr_socket sk, int buffer_size);
int xdr_enc_flush(xdr_enc_t *xdr);
int xdr_enc_release(xdr_enc_t *xdr); /* calls xdr_enc_flush() */
void xdr_enc_force_release(xdr_enc_t *xdr); /* doesn't call xdr_enc_flush() */
void xdr_dec_release(xdr_dec_t *xdr);
/* xdr_enc_force_release() is for when you get and error sending and you
 * want to free that stuff up right away.  If you use the regular release
 * for enc, it will fail if it cannot send data over the filedesciptor.
 */

/* encoders add to a stream */
int __inline__ xdr_enc_uint64(xdr_enc_t *xdr, uint64_t i);
int __inline__ xdr_enc_uint32(xdr_enc_t *xdr, uint32_t i);
int __inline__ xdr_enc_uint16(xdr_enc_t *xdr, uint16_t i);
int __inline__ xdr_enc_uint8(xdr_enc_t *xdr, uint8_t i);
int __inline__ xdr_enc_ipv6(xdr_enc_t *enc, struct in6_addr *ip);
int xdr_enc_raw(xdr_enc_t *xdr, void *pointer, uint16_t len);
int xdr_enc_raw_iov(xdr_enc_t *xdr, int count, struct iovec *iov);
int xdr_enc_string(xdr_enc_t *xdr, uint8_t *s);
int xdr_enc_list_start(xdr_enc_t *xdr);
int xdr_enc_list_stop(xdr_enc_t *xdr);

/* decoders remove from stream */
int xdr_dec_uint64(xdr_dec_t *xdr, uint64_t *i);
int xdr_dec_uint32(xdr_dec_t *xdr, uint32_t *i);
int xdr_dec_uint16(xdr_dec_t *xdr, uint16_t *i);
int xdr_dec_uint8(xdr_dec_t *xdr, uint8_t *i);
int xdr_dec_ipv6(xdr_dec_t *xdr, struct in6_addr *ip);
int xdr_dec_raw(xdr_dec_t *xdr, void *p, uint16_t *l); /* no malloc */
int xdr_dec_raw_m(xdr_dec_t *xdr, void **p, uint16_t *l); /* mallocs p */
int xdr_dec_raw_ag(xdr_dec_t *xdr, void **p, uint16_t *bl, uint16_t *rl);
int xdr_dec_string(xdr_dec_t *xdr, uint8_t **strp); /* mallocs s */
int xdr_dec_string_nm(xdr_dec_t *xdr, uint8_t *strp, size_t l); /* no malloc */
int xdr_dec_string_ag(xdr_dec_t *xdr, uint8_t **s, uint16_t *bl);
int xdr_dec_list_start(xdr_dec_t *xdr);
int xdr_dec_list_stop(xdr_dec_t *xdr);

#endif /*__gulm_xdr_h__*/
/* vim: set ai cin et sw=3 ts=3 : */
